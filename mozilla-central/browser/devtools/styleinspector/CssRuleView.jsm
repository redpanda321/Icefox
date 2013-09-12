/* -*- Mode: javascript; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

const HTML_NS = "http://www.w3.org/1999/xhtml";
const XUL_NS = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";

const FOCUS_FORWARD = Ci.nsIFocusManager.MOVEFOCUS_FORWARD;
const FOCUS_BACKWARD = Ci.nsIFocusManager.MOVEFOCUS_BACKWARD;

/**
 * These regular expressions are adapted from firebug's css.js, and are
 * used to parse CSSStyleDeclaration's cssText attribute.
 */

// Used to split on css line separators
const CSS_LINE_RE = /(?:[^;\(]*(?:\([^\)]*?\))?[^;\(]*)*;?/g;

// Used to parse a single property line.
const CSS_PROP_RE = /\s*([^:\s]*)\s*:\s*(.*?)\s*(?:! (important))?;?$/;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource:///modules/devtools/CssLogic.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

this.EXPORTED_SYMBOLS = ["CssRuleView",
                         "_ElementStyle",
                         "editableItem",
                         "_editableField",
                         "_getInplaceEditorForSpan"];

/**
 * Our model looks like this:
 *
 * ElementStyle:
 *   Responsible for keeping track of which properties are overridden.
 *   Maintains a list of Rule objects that apply to the element.
 * Rule:
 *   Manages a single style declaration or rule.
 *   Responsible for applying changes to the properties in a rule.
 *   Maintains a list of TextProperty objects.
 * TextProperty:
 *   Manages a single property from the cssText attribute of the
 *     relevant declaration.
 *   Maintains a list of computed properties that come from this
 *     property declaration.
 *   Changes to the TextProperty are sent to its related Rule for
 *     application.
 */

/**
 * ElementStyle maintains a list of Rule objects for a given element.
 *
 * @param {Element} aElement
 *        The element whose style we are viewing.
 * @param {object} aStore
 *        The ElementStyle can use this object to store metadata
 *        that might outlast the rule view, particularly the current
 *        set of disabled properties.
 *
 * @constructor
 */
function ElementStyle(aElement, aStore)
{
  this.element = aElement;
  this.store = aStore || {};

  // We don't want to overwrite this.store.userProperties so we only create it
  // if it doesn't already exist.
  if (!("userProperties" in this.store)) {
    this.store.userProperties = new UserProperties();
  }

  if (this.store.disabled) {
    this.store.disabled = aStore.disabled;
  } else {
    // FIXME: This should be a WeakMap once bug 753517 is fixed.
    // See Bug 777373 for details.
    this.store.disabled = new Map();
  }

  let doc = aElement.ownerDocument;

  // To figure out how shorthand properties are interpreted by the
  // engine, we will set properties on a dummy element and observe
  // how their .style attribute reflects them as computed values.
  this.dummyElement = doc.createElementNS(this.element.namespaceURI,
                                          this.element.tagName);
  this.populate();
}
// We're exporting _ElementStyle for unit tests.
this._ElementStyle = ElementStyle;

ElementStyle.prototype = {

  // The element we're looking at.
  element: null,

  // Empty, unconnected element of the same type as this node, used
  // to figure out how shorthand properties will be parsed.
  dummyElement: null,

  domUtils: Cc["@mozilla.org/inspector/dom-utils;1"].getService(Ci.inIDOMUtils),

  /**
   * Called by the Rule object when it has been changed through the
   * setProperty* methods.
   */
  _changed: function ElementStyle_changed()
  {
    if (this.onChanged) {
      this.onChanged();
    }
  },

  /**
   * Refresh the list of rules to be displayed for the active element.
   * Upon completion, this.rules[] will hold a list of Rule objects.
   */
  populate: function ElementStyle_populate()
  {
    // Store the current list of rules (if any) during the population
    // process.  They will be reused if possible.
    this._refreshRules = this.rules;

    this.rules = [];

    let element = this.element;
    do {
      this._addElementRules(element);
    } while ((element = element.parentNode) &&
             element.nodeType === Ci.nsIDOMNode.ELEMENT_NODE);

    // Mark overridden computed styles.
    this.markOverridden();

    // We're done with the previous list of rules.
    delete this._refreshRules;
  },

  _addElementRules: function ElementStyle_addElementRules(aElement)
  {
    let inherited = aElement !== this.element ? aElement : null;

    // Include the element's style first.
    this._maybeAddRule({
      style: aElement.style,
      selectorText: CssLogic.l10n("rule.sourceElement"),
      inherited: inherited
    });

    // Get the styles that apply to the element.
    var domRules = this.domUtils.getCSSStyleRules(aElement);

    // getCSStyleRules returns ordered from least-specific to
    // most-specific.
    for (let i = domRules.Count() - 1; i >= 0; i--) {
      let domRule = domRules.GetElementAt(i);

      // XXX: Optionally provide access to system sheets.
      let contentSheet = CssLogic.isContentStylesheet(domRule.parentStyleSheet);
      if (!contentSheet) {
        continue;
      }

      if (domRule.type !== Ci.nsIDOMCSSRule.STYLE_RULE) {
        continue;
      }

      this._maybeAddRule({
        domRule: domRule,
        inherited: inherited
      });
    }
  },

  /**
   * Add a rule if it's one we care about.  Filters out duplicates and
   * inherited styles with no inherited properties.
   *
   * @param {object} aOptions
   *        Options for creating the Rule, see the Rule constructor.
   *
   * @return {bool} true if we added the rule.
   */
  _maybeAddRule: function ElementStyle_maybeAddRule(aOptions)
  {
    // If we've already included this domRule (for example, when a
    // common selector is inherited), ignore it.
    if (aOptions.domRule &&
        this.rules.some(function(rule) rule.domRule === aOptions.domRule)) {
      return false;
    }

    let rule = null;

    // If we're refreshing and the rule previously existed, reuse the
    // Rule object.
    for (let r of (this._refreshRules || [])) {
      if (r.matches(aOptions)) {
        rule = r;
        rule.refresh();
        break;
      }
    }

    // If this is a new rule, create its Rule object.
    if (!rule) {
      rule = new Rule(this, aOptions);
    }

    // Ignore inherited rules with no properties.
    if (aOptions.inherited && rule.textProps.length == 0) {
      return false;
    }

    this.rules.push(rule);
  },

  /**
   * Mark the properties listed in this.rules with an overridden flag
   * if an earlier property overrides it.
   */
  markOverridden: function ElementStyle_markOverridden()
  {
    // Gather all the text properties applied by these rules, ordered
    // from more- to less-specific.
    let textProps = [];
    for each (let rule in this.rules) {
      textProps = textProps.concat(rule.textProps.slice(0).reverse());
    }

    // Gather all the computed properties applied by those text
    // properties.
    let computedProps = [];
    for each (let textProp in textProps) {
      computedProps = computedProps.concat(textProp.computed);
    }

    // Walk over the computed properties.  As we see a property name
    // for the first time, mark that property's name as taken by this
    // property.
    //
    // If we come across a property whose name is already taken, check
    // its priority against the property that was found first:
    //
    //   If the new property is a higher priority, mark the old
    //   property overridden and mark the property name as taken by
    //   the new property.
    //
    //   If the new property is a lower or equal priority, mark it as
    //   overridden.
    //
    // _overriddenDirty will be set on each prop, indicating whether its
    // dirty status changed during this pass.
    let taken = {};
    for each (let computedProp in computedProps) {
      let earlier = taken[computedProp.name];
      let overridden;
      if (earlier
          && computedProp.priority === "important"
          && earlier.priority !== "important") {
        // New property is higher priority.  Mark the earlier property
        // overridden (which will reverse its dirty state).
        earlier._overriddenDirty = !earlier._overriddenDirty;
        earlier.overridden = true;
        overridden = false;
      } else {
        overridden = !!earlier;
      }

      computedProp._overriddenDirty = (!!computedProp.overridden != overridden);
      computedProp.overridden = overridden;
      if (!computedProp.overridden && computedProp.textProp.enabled) {
        taken[computedProp.name] = computedProp;
      }
    }

    // For each TextProperty, mark it overridden if all of its
    // computed properties are marked overridden.  Update the text
    // property's associated editor, if any.  This will clear the
    // _overriddenDirty state on all computed properties.
    for each (let textProp in textProps) {
      // _updatePropertyOverridden will return true if the
      // overridden state has changed for the text property.
      if (this._updatePropertyOverridden(textProp)) {
        textProp.updateEditor();
      }
    }
  },

  /**
   * Mark a given TextProperty as overridden or not depending on the
   * state of its computed properties.  Clears the _overriddenDirty state
   * on all computed properties.
   *
   * @param {TextProperty} aProp
   *        The text property to update.
   *
   * @return {bool} true if the TextProperty's overridden state (or any of its
   *         computed properties overridden state) changed.
   */
  _updatePropertyOverridden: function ElementStyle_updatePropertyOverridden(aProp)
  {
    let overridden = true;
    let dirty = false;
    for each (let computedProp in aProp.computed) {
      if (!computedProp.overridden) {
        overridden = false;
      }
      dirty = computedProp._overriddenDirty || dirty;
      delete computedProp._overriddenDirty;
    }

    dirty = (!!aProp.overridden != overridden) || dirty;
    aProp.overridden = overridden;
    return dirty;
  }
};

/**
 * A single style rule or declaration.
 *
 * @param {ElementStyle} aElementStyle
 *        The ElementStyle to which this rule belongs.
 * @param {object} aOptions
 *        The information used to construct this rule.  Properties include:
 *          domRule: the nsIDOMCSSStyleRule to view, if any.
 *          style: the nsIDOMCSSStyleDeclaration to view.  If omitted,
 *            the domRule's style will be used.
 *          selectorText: selector text to display.  If omitted, the domRule's
 *            selectorText will be used.
 *          inherited: An element this rule was inherited from.  If omitted,
 *            the rule applies directly to the current element.
 * @constructor
 */
function Rule(aElementStyle, aOptions)
{
  this.elementStyle = aElementStyle;
  this.domRule = aOptions.domRule || null;
  this.style = aOptions.style || this.domRule.style;
  this.selectorText = aOptions.selectorText || this.domRule.selectorText;
  this.inherited = aOptions.inherited || null;

  if (this.domRule) {
    let parentRule = this.domRule.parentRule;
    if (parentRule && parentRule.type == Ci.nsIDOMCSSRule.MEDIA_RULE) {
      this.mediaText = parentRule.media.mediaText;
    }
  }

  // Populate the text properties with the style's current cssText
  // value, and add in any disabled properties from the store.
  this.textProps = this._getTextProperties();
  this.textProps = this.textProps.concat(this._getDisabledProperties());
}

Rule.prototype = {
  mediaText: "",

  get title()
  {
    if (this._title) {
      return this._title;
    }
    this._title = CssLogic.shortSource(this.sheet);
    if (this.domRule) {
      this._title += ":" + this.ruleLine;
    }

    return this._title + (this.mediaText ? " @media " + this.mediaText : "");
  },

  get inheritedSource()
  {
    if (this._inheritedSource) {
      return this._inheritedSource;
    }
    this._inheritedSource = "";
    if (this.inherited) {
      let eltText = this.inherited.tagName.toLowerCase();
      if (this.inherited.id) {
        eltText += "#" + this.inherited.id;
      }
      this._inheritedSource =
        CssLogic._strings.formatStringFromName("rule.inheritedFrom", [eltText], 1);
    }
    return this._inheritedSource;
  },

  /**
   * The rule's stylesheet.
   */
  get sheet()
  {
    return this.domRule ? this.domRule.parentStyleSheet : null;
  },

  /**
   * The rule's line within a stylesheet
   */
  get ruleLine()
  {
    if (!this.sheet) {
      // No stylesheet, no ruleLine
      return null;
    }
    return this.elementStyle.domUtils.getRuleLine(this.domRule);
  },

  /**
   * Returns true if the rule matches the creation options
   * specified.
   *
   * @param {object} aOptions
   *        Creation options.  See the Rule constructor for documentation.
   */
  matches: function Rule_matches(aOptions)
  {
    return (this.style === (aOptions.style || aOptions.domRule.style));
  },

  /**
   * Create a new TextProperty to include in the rule.
   *
   * @param {string} aName
   *        The text property name (such as "background" or "border-top").
   * @param {string} aValue
   *        The property's value (not including priority).
   * @param {string} aPriority
   *        The property's priority (either "important" or an empty string).
   */
  createProperty: function Rule_createProperty(aName, aValue, aPriority)
  {
    let prop = new TextProperty(this, aName, aValue, aPriority);
    this.textProps.push(prop);
    this.applyProperties();
    return prop;
  },

  /**
   * Reapply all the properties in this rule, and update their
   * computed styles.  Store disabled properties in the element
   * style's store.  Will re-mark overridden properties.
   *
   * @param {string} [aName]
   *        A text property name (such as "background" or "border-top") used
   *        when calling from setPropertyValue & setPropertyName to signify
   *        that the property should be saved in store.userProperties.
   */
  applyProperties: function Rule_applyProperties(aName)
  {
    let disabledProps = [];
    let store = this.elementStyle.store;

    for each (let prop in this.textProps) {
      if (!prop.enabled) {
        disabledProps.push({
          name: prop.name,
          value: prop.value,
          priority: prop.priority
        });
        continue;
      }

      this.style.setProperty(prop.name, prop.value, prop.priority);

      if (aName && prop.name == aName) {
        store.userProperties.setProperty(
          this.style, prop.name,
          this.style.getPropertyValue(prop.name),
          prop.value);
      }

      // Refresh the property's priority from the style, to reflect
      // any changes made during parsing.
      prop.priority = this.style.getPropertyPriority(prop.name);
      prop.updateComputed();
    }
    this.elementStyle._changed();

    // Store disabled properties in the disabled store.
    let disabled = this.elementStyle.store.disabled;
    if (disabledProps.length > 0) {
      disabled.set(this.style, disabledProps);
    } else {
      disabled.delete(this.style);
    }

    this.elementStyle.markOverridden();
  },

  /**
   * Renames a property.
   *
   * @param {TextProperty} aProperty
   *        The property to rename.
   * @param {string} aName
   *        The new property name (such as "background" or "border-top").
   */
  setPropertyName: function Rule_setPropertyName(aProperty, aName)
  {
    if (aName === aProperty.name) {
      return;
    }
    this.style.removeProperty(aProperty.name);
    aProperty.name = aName;
    this.applyProperties(aName);
  },

  /**
   * Sets the value and priority of a property.
   *
   * @param {TextProperty} aProperty
   *        The property to manipulate.
   * @param {string} aValue
   *        The property's value (not including priority).
   * @param {string} aPriority
   *        The property's priority (either "important" or an empty string).
   */
  setPropertyValue: function Rule_setPropertyValue(aProperty, aValue, aPriority)
  {
    if (aValue === aProperty.value && aPriority === aProperty.priority) {
      return;
    }
    aProperty.value = aValue;
    aProperty.priority = aPriority;
    this.applyProperties(aProperty.name);
  },

  /**
   * Disables or enables given TextProperty.
   */
  setPropertyEnabled: function Rule_enableProperty(aProperty, aValue)
  {
    aProperty.enabled = !!aValue;
    if (!aProperty.enabled) {
      this.style.removeProperty(aProperty.name);
    }
    this.applyProperties();
  },

  /**
   * Remove a given TextProperty from the rule and update the rule
   * accordingly.
   */
  removeProperty: function Rule_removeProperty(aProperty)
  {
    this.textProps = this.textProps.filter(function(prop) prop != aProperty);
    this.style.removeProperty(aProperty);
    // Need to re-apply properties in case removing this TextProperty
    // exposes another one.
    this.applyProperties();
  },

  /**
   * Get the list of TextProperties from the style.  Needs
   * to parse the style's cssText.
   */
  _getTextProperties: function Rule_getTextProperties()
  {
    let textProps = [];
    let store = this.elementStyle.store;
    let lines = this.style.cssText.match(CSS_LINE_RE);
    for each (let line in lines) {
      let matches = CSS_PROP_RE.exec(line);
      if (!matches || !matches[2])
        continue;

      let name = matches[1];
      if (this.inherited &&
          !this.elementStyle.domUtils.isInheritedProperty(name)) {
        continue;
      }
      let value = store.userProperties.getProperty(this.style, name, matches[2]);
      let prop = new TextProperty(this, name, value, matches[3] || "");
      textProps.push(prop);
    }

    return textProps;
  },

  /**
   * Return the list of disabled properties from the store for this rule.
   */
  _getDisabledProperties: function Rule_getDisabledProperties()
  {
    let store = this.elementStyle.store;

    // Include properties from the disabled property store, if any.
    let disabledProps = store.disabled.get(this.style);
    if (!disabledProps) {
      return [];
    }

    let textProps = [];

    for each (let prop in disabledProps) {
      let value = store.userProperties.getProperty(this.style, prop.name, prop.value);
      let textProp = new TextProperty(this, prop.name, value, prop.priority);
      textProp.enabled = false;
      textProps.push(textProp);
    }

    return textProps;
  },

  /**
   * Reread the current state of the rules and rebuild text
   * properties as needed.
   */
  refresh: function Rule_refresh()
  {
    let newTextProps = this._getTextProperties();

    // Update current properties for each property present on the style.
    // This will mark any touched properties with _visited so we
    // can detect properties that weren't touched (because they were
    // removed from the style).
    // Also keep track of properties that didn't exist in the current set
    // of properties.
    let brandNewProps = [];
    for (let newProp of newTextProps) {
      if (!this._updateTextProperty(newProp)) {
        brandNewProps.push(newProp);
      }
    }

    // Refresh editors and disabled state for all the properties that
    // were updated.
    for (let prop of this.textProps) {
      // Properties that weren't touched during the update
      // process must no longer exist on the node.  Mark them disabled.
      if (!prop._visited) {
        prop.enabled = false;
        prop.updateEditor();
      } else {
        delete prop._visited;
      }
    }

    // Add brand new properties.
    this.textProps = this.textProps.concat(brandNewProps);

    // Refresh the editor if one already exists.
    if (this.editor) {
      this.editor.populate();
    }
  },

  /**
   * Update the current TextProperties that match a given property
   * from the cssText.  Will choose one existing TextProperty to update
   * with the new property's value, and will disable all others.
   *
   * When choosing the best match to reuse, properties will be chosen
   * by assigning a rank and choosing the highest-ranked property:
   *   Name, value, and priority match, enabled. (6)
   *   Name, value, and priority match, disabled. (5)
   *   Name and value match, enabled. (4)
   *   Name and value match, disabled. (3)
   *   Name matches, enabled. (2)
   *   Name matches, disabled. (1)
   *
   * If no existing properties match the property, nothing happens.
   *
   * @param {TextProperty} aNewProp
   *        The current version of the property, as parsed from the
   *        cssText in Rule._getTextProperties().
   *
   * @return {bool} true if a property was updated, false if no properties
   *         were updated.
   */
  _updateTextProperty: function Rule__updateTextProperty(aNewProp) {
    let match = { rank: 0, prop: null };

    for each (let prop in this.textProps) {
      if (prop.name != aNewProp.name)
        continue;

      // Mark this property visited.
      prop._visited = true;

      // Start at rank 1 for matching name.
      let rank = 1;

      // Value and Priority matches add 2 to the rank.
      // Being enabled adds 1.  This ranks better matches higher,
      // with priority breaking ties.
      if (prop.value === aNewProp.value) {
        rank += 2;
        if (prop.priority === aNewProp.priority) {
          rank += 2;
        }
      }

      if (prop.enabled) {
        rank += 1;
      }

      if (rank > match.rank) {
        if (match.prop) {
          // We outrank a previous match, disable it.
          match.prop.enabled = false;
          match.prop.updateEditor();
        }
        match.rank = rank;
        match.prop = prop;
      } else if (rank) {
        // A previous match outranks us, disable ourself.
        prop.enabled = false;
        prop.updateEditor();
      }
    }

    // If we found a match, update its value with the new text property
    // value.
    if (match.prop) {
      match.prop.set(aNewProp);
      return true;
    }

    return false;
  },
};

/**
 * A single property in a rule's cssText.
 *
 * @param {Rule} aRule
 *        The rule this TextProperty came from.
 * @param {string} aName
 *        The text property name (such as "background" or "border-top").
 * @param {string} aValue
 *        The property's value (not including priority).
 * @param {string} aPriority
 *        The property's priority (either "important" or an empty string).
 *
 */
function TextProperty(aRule, aName, aValue, aPriority)
{
  this.rule = aRule;
  this.name = aName;
  this.value = aValue;
  this.priority = aPriority;
  this.enabled = true;
  this.updateComputed();
}

TextProperty.prototype = {
  /**
   * Update the editor associated with this text property,
   * if any.
   */
  updateEditor: function TextProperty_updateEditor()
  {
    if (this.editor) {
      this.editor.update();
    }
  },

  /**
   * Update the list of computed properties for this text property.
   */
  updateComputed: function TextProperty_updateComputed()
  {
    if (!this.name) {
      return;
    }

    // This is a bit funky.  To get the list of computed properties
    // for this text property, we'll set the property on a dummy element
    // and see what the computed style looks like.
    let dummyElement = this.rule.elementStyle.dummyElement;
    let dummyStyle = dummyElement.style;
    dummyStyle.cssText = "";
    dummyStyle.setProperty(this.name, this.value, this.priority);

    this.computed = [];
    for (let i = 0, n = dummyStyle.length; i < n; i++) {
      let prop = dummyStyle.item(i);
      this.computed.push({
        textProp: this,
        name: prop,
        value: dummyStyle.getPropertyValue(prop),
        priority: dummyStyle.getPropertyPriority(prop),
      });
    }
  },

  /**
   * Set all the values from another TextProperty instance into
   * this TextProperty instance.
   *
   * @param {TextProperty} aOther
   *        The other TextProperty instance.
   */
  set: function TextProperty_set(aOther)
  {
    let changed = false;
    for (let item of ["name", "value", "priority", "enabled"]) {
      if (this[item] != aOther[item]) {
        this[item] = aOther[item];
        changed = true;
      }
    }

    if (changed) {
      this.updateEditor();
    }
  },

  setValue: function TextProperty_setValue(aValue, aPriority)
  {
    this.rule.setPropertyValue(this, aValue, aPriority);
    this.updateEditor();
  },

  setName: function TextProperty_setName(aName)
  {
    this.rule.setPropertyName(this, aName);
    this.updateEditor();
  },

  setEnabled: function TextProperty_setEnabled(aValue)
  {
    this.rule.setPropertyEnabled(this, aValue);
    this.updateEditor();
  },

  remove: function TextProperty_remove()
  {
    this.rule.removeProperty(this);
  }
};


/**
 * View hierarchy mostly follows the model hierarchy.
 *
 * CssRuleView:
 *   Owns an ElementStyle and creates a list of RuleEditors for its
 *    Rules.
 * RuleEditor:
 *   Owns a Rule object and creates a list of TextPropertyEditors
 *     for its TextProperties.
 *   Manages creation of new text properties.
 * TextPropertyEditor:
 *   Owns a TextProperty object.
 *   Manages changes to the TextProperty.
 *   Can be expanded to display computed properties.
 *   Can mark a property disabled or enabled.
 */

/**
 * CssRuleView is a view of the style rules and declarations that
 * apply to a given element.  After construction, the 'element'
 * property will be available with the user interface.
 *
 * @param {Document} aDoc
 *        The document that will contain the rule view.
 * @param {object} aStore
 *        The CSS rule view can use this object to store metadata
 *        that might outlast the rule view, particularly the current
 *        set of disabled properties.
 * @constructor
 */
this.CssRuleView = function CssRuleView(aDoc, aStore)
{
  this.doc = aDoc;
  this.store = aStore;
  this.element = this.doc.createElementNS(XUL_NS, "vbox");
  this.element.setAttribute("tabindex", "0");
  this.element.classList.add("ruleview");
  this.element.flex = 1;

  this._boundCopy = this._onCopy.bind(this);
  this.element.addEventListener("copy", this._boundCopy);

  this._createContextMenu();
  this._showEmpty();
}

CssRuleView.prototype = {
  // The element that we're inspecting.
  _viewedElement: null,

  /**
   * Return {bool} true if the rule view currently has an input editor visible.
   */
  get isEditing() {
    return this.element.querySelectorAll(".styleinspector-propertyeditor").length > 0;
  },

  destroy: function CssRuleView_destroy()
  {
    this.clear();

    this.element.removeEventListener("copy", this._boundCopy);
    this._copyItem.removeEventListener("command", this._boundCopy);
    delete this._boundCopy;

    this._ruleItem.removeEventListener("command", this._boundCopyRule);
    delete this._boundCopyRule;

    this._declarationItem.removeEventListener("command", this._boundCopyDeclaration);
    delete this._boundCopyDeclaration;

    this._propertyItem.removeEventListener("command", this._boundCopyProperty);
    delete this._boundCopyProperty;

    this._propertyValueItem.removeEventListener("command", this._boundCopyPropertyValue);
    delete this._boundCopyPropertyValue;

    this._contextMenu.removeEventListener("popupshowing", this._boundMenuUpdate);
    delete this._boundMenuUpdate;
    delete this._contextMenu;

    if (this.element.parentNode) {
      this.element.parentNode.removeChild(this.element);
    }
  },

  /**
   * Update the highlighted element.
   *
   * @param {nsIDOMElement} aElement
   *        The node whose style rules we'll inspect.
   */
  highlight: function CssRuleView_highlight(aElement)
  {
    if (this._viewedElement === aElement) {
      return;
    }

    this.clear();

    if (this._elementStyle) {
      delete this._elementStyle;
    }

    this._viewedElement = aElement;
    if (!this._viewedElement) {
      this._showEmpty();
      return;
    }

    this._elementStyle = new ElementStyle(aElement, this.store);
    this._elementStyle.onChanged = function() {
      this._changed();
    }.bind(this);

    this._createEditors();
  },

  /**
   * Update the rules for the currently highlighted element.
   */
  nodeChanged: function CssRuleView_nodeChanged()
  {
    // Ignore refreshes during editing.
    if (this.isEditing) {
      return;
    }

    this._clearRules();

    // Repopulate the element style.
    this._elementStyle.populate();

    // Refresh the rule editors.
    this._createEditors();

    // Notify anyone that cares that we refreshed.
    var evt = this.doc.createEvent("Events");
    evt.initEvent("CssRuleViewRefreshed", true, false);
    this.element.dispatchEvent(evt);
  },

  /**
   * Show the user that the rule view has no node selected.
   */
  _showEmpty: function CssRuleView_showEmpty()
  {
    if (this.doc.getElementById("noResults") > 0) {
      return;
    }

    createChild(this.element, "div", {
      id: "noResults",
      textContent: CssLogic.l10n("rule.empty")
    });
  },

  /**
   * Clear the rules.
   */
  _clearRules: function CssRuleView_clearRules()
  {
    while (this.element.hasChildNodes()) {
      this.element.removeChild(this.element.lastChild);
    }
  },

  /**
   * Clear the rule view.
   */
  clear: function CssRuleView_clear()
  {
    this._clearRules();
    this._viewedElement = null;
    this._elementStyle = null;
  },

  /**
   * Called when the user has made changes to the ElementStyle.
   * Emits an event that clients can listen to.
   */
  _changed: function CssRuleView_changed()
  {
    var evt = this.doc.createEvent("Events");
    evt.initEvent("CssRuleViewChanged", true, false);
    this.element.dispatchEvent(evt);
  },

  /**
   * Creates editor UI for each of the rules in _elementStyle.
   */
  _createEditors: function CssRuleView_createEditors()
  {
    // Run through the current list of rules, attaching
    // their editors in order.  Create editors if needed.
    let lastInheritedSource = "";
    for each (let rule in this._elementStyle.rules) {

      let inheritedSource = rule.inheritedSource;
      if (inheritedSource != lastInheritedSource) {
        let h2 = this.doc.createElementNS(HTML_NS, "div");
        h2.className = "ruleview-rule-inheritance";
        h2.textContent = inheritedSource;
        lastInheritedSource = inheritedSource;
        this.element.appendChild(h2);
      }

      if (!rule.editor) {
        new RuleEditor(this, rule);
      }

      this.element.appendChild(rule.editor.element);
    }
  },

  /**
   * Add a context menu to the rule view.
   */
  _createContextMenu: function CssRuleView_createContextMenu()
  {
    let popupSet = this.doc.createElement("popupset");
    this.doc.documentElement.appendChild(popupSet);

    let menu = this.doc.createElement("menupopup");
    menu.id = "rule-view-context-menu";

    this._boundMenuUpdate = this._onMenuUpdate.bind(this);
    menu.addEventListener("popupshowing", this._boundMenuUpdate);

    // Copy selection
    this._copyItem = createMenuItem(menu, {
      label: "rule.contextmenu.copyselection",
      accesskey: "rule.contextmenu.copyselection.accesskey",
      command: this._boundCopy
    });

    // Copy rule
    this._boundCopyRule = this._onCopyRule.bind(this);
    this._ruleItem = createMenuItem(menu, {
      label: "rule.contextmenu.copyrule",
      accesskey: "rule.contextmenu.copyrule.accesskey",
      command: this._boundCopyRule
    });

    // Copy declaration
    this._boundCopyDeclaration = this._onCopyDeclaration.bind(this);
    this._declarationItem = createMenuItem(menu, {
      label: "rule.contextmenu.copydeclaration",
      accesskey: "rule.contextmenu.copydeclaration.accesskey",
      command: this._boundCopyDeclaration
    });

    this._boundCopyProperty = this._onCopyProperty.bind(this);
    this._propertyItem = createMenuItem(menu, {
      label: "rule.contextmenu.copyproperty",
      accesskey: "rule.contextmenu.copyproperty.accesskey",
      command: this._boundCopyProperty
    });

    this._boundCopyPropertyValue = this._onCopyPropertyValue.bind(this);
    this._propertyValueItem = createMenuItem(menu,{
      label: "rule.contextmenu.copypropertyvalue",
      accesskey: "rule.contextmenu.copypropertyvalue.accesskey",
      command: this._boundCopyPropertyValue
    });

    popupSet.appendChild(menu);
    this.element.setAttribute("context", menu.id);

    this._contextMenu = menu;
  },

  /**
   * Update the rule view's context menu by disabling irrelevant menuitems and
   * enabling relevant ones.
   *
   * @param {Event} aEvent
   *        The event object.
   */
  _onMenuUpdate: function CssRuleView_onMenuUpdate(aEvent)
  {
    let node = this.doc.popupNode;

    // Copy selection.
    let editorSelection = node.className == "styleinspector-propertyeditor" &&
                          node.selectionEnd - node.selectionStart != 0;
    let disable = this.doc.defaultView.getSelection().isCollapsed &&
                  !editorSelection;
    this._copyItem.disabled = disable;

    // Copy property, copy property name & copy property value.
    if (!node) {
      return;
    }

    if (!node.classList.contains("ruleview-property") &&
        !node.classList.contains("ruleview-computed")) {
      while (node = node.parentElement) {
        if (node.classList.contains("ruleview-property") ||
          node.classList.contains("ruleview-computed")) {
          break;
        }
      }
    }
    let disablePropertyItems = !node || (node &&
      !node.classList.contains("ruleview-property") &&
      !node.classList.contains("ruleview-computed"));

    this._declarationItem.disabled = disablePropertyItems;
    this._propertyItem.disabled = disablePropertyItems;
    this._propertyValueItem.disabled = disablePropertyItems;
  },

  /**
   * Copy selected text from the rule view.
   *
   * @param {Event} aEvent
   *        The event object.
   */
  _onCopy: function CssRuleView_onCopy(aEvent)
  {
    let target = this.doc.popupNode || aEvent.target;
    let text;

    if (target.nodeName == "input") {
      let start = Math.min(target.selectionStart, target.selectionEnd);
      let end = Math.max(target.selectionStart, target.selectionEnd);
      let count = end - start;
      text = target.value.substr(start, count);
    } else {
      let win = this.doc.defaultView;
      text = win.getSelection().toString();

      // Remove any double newlines.
      text = text.replace(/(\r?\n)\r?\n/g, "$1");

      // Remove "inline"
      let inline = _strings.GetStringFromName("rule.sourceInline");
      let rx = new RegExp("^" + inline + "\\r?\\n?", "g");
      text = text.replace(rx, "");
    }

    clipboardHelper.copyString(text, this.doc);

    if (aEvent) {
      aEvent.preventDefault();
    }
  },

  /**
   * Copy a rule from the rule view.
   *
   * @param {Event} aEvent
   *        The event object.
   */
  _onCopyRule: function CssRuleView_onCopyRule(aEvent)
  {
    let terminator;
    let node = this.doc.popupNode;
    if (!node) {
      return;
    }

    if (node.className != "ruleview-rule") {
      while (node = node.parentElement) {
        if (node.className == "ruleview-rule") {
          break;
        }
      }
    }
    node = node.cloneNode();

    let computedLists = node.querySelectorAll(".ruleview-computedlist");
    for (let computedList of computedLists) {
      computedList.parentNode.removeChild(computedList);
    }

    let autosizers = node.querySelectorAll(".autosizer");
    for (let autosizer of autosizers) {
      autosizer.parentNode.removeChild(autosizer);
    }
    let selector = node.querySelector(".ruleview-selector").textContent;
    let propertyNames = node.querySelectorAll(".ruleview-propertyname");
    let propertyValues = node.querySelectorAll(".ruleview-propertyvalue");

    // Format the rule
    if (osString == "WINNT") {
      terminator = "\r\n";
    } else {
      terminator = "\n";
    }

    let out = selector + " {" + terminator;
    for (let i = 0; i < propertyNames.length; i++) {
      let name = propertyNames[i].textContent;
      let value = propertyValues[i].textContent;
      out += "    " + name + ": " + value + ";" + terminator;
    }
    out += "}" + terminator;

    clipboardHelper.copyString(out, this.doc);
  },

  /**
   * Copy a declaration from the rule view.
   *
   * @param {Event} aEvent
   *        The event object.
   */
  _onCopyDeclaration: function CssRuleView_onCopyDeclaration(aEvent)
  {
    let node = this.doc.popupNode;
    if (!node) {
      return;
    }

    if (!node.classList.contains("ruleview-property") &&
        !node.classList.contains("ruleview-computed")) {
      while (node = node.parentElement) {
        if (node.classList.contains("ruleview-property") ||
            node.classList.contains("ruleview-computed")) {
          break;
        }
      }
    }

    // We need to strip expanded properties from the node because we use
    // node.textContent below, which also gets text from hidden nodes. The
    // simplest way to do this is to clone the node and remove them from the
    // clone.
    node = node.cloneNode();
    let computedLists = node.querySelectorAll(".ruleview-computedlist");
    for (let computedList of computedLists) {
      computedList.parentNode.removeChild(computedList);
    }

    let propertyName = node.querySelector(".ruleview-propertyname").textContent;
    let propertyValue = node.querySelector(".ruleview-propertyvalue").textContent;
    let out = propertyName + ": " + propertyValue + ";";

    clipboardHelper.copyString(out, this.doc);
  },

  /**
   * Copy a property name from the rule view.
   *
   * @param {Event} aEvent
   *        The event object.
   */
  _onCopyProperty: function CssRuleView_onCopyProperty(aEvent)
  {
    let node = this.doc.popupNode;

    if (!node) {
      return;
    }

    if (!node.classList.contains("ruleview-propertyname")) {
      node = node.parentNode.parentNode.querySelector(".ruleview-propertyname");
    }

    if (node) {
      clipboardHelper.copyString(node.textContent, this.doc);
    }
  },

 /**
   * Copy a property value from the rule view.
   *
   * @param {Event} aEvent
   *        The event object.
   */
  _onCopyPropertyValue: function CssRuleView_onCopyPropertyValue(aEvent)
  {
    let node = this.doc.popupNode;
    if (!node) {
      return;
    }

    if (!node.classList.contains("ruleview-propertyvalue")) {
      node = node.parentNode.parentNode.querySelector(".ruleview-propertyvalue");
    }

    if (node) {
      clipboardHelper.copyString(node.textContent, this.doc);
    }
  }
};

/**
 * Create a RuleEditor.
 *
 * @param {CssRuleView} aRuleView
 *        The CssRuleView containg the document holding this rule editor.
 * @param {Rule} aRule
 *        The Rule object we're editing.
 * @constructor
 */
function RuleEditor(aRuleView, aRule)
{
  this.ruleView = aRuleView;
  this.doc = this.ruleView.doc;
  this.rule = aRule;
  this.rule.editor = this;

  this._onNewProperty = this._onNewProperty.bind(this);
  this._newPropertyDestroy = this._newPropertyDestroy.bind(this);

  this._create();
}

RuleEditor.prototype = {
  _create: function RuleEditor_create()
  {
    this.element = this.doc.createElementNS(HTML_NS, "div");
    this.element.className = "ruleview-rule";
    this.element._ruleEditor = this;

    // Give a relative position for the inplace editor's measurement
    // span to be placed absolutely against.
    this.element.style.position = "relative";

    // Add the source link.
    let source = createChild(this.element, "div", {
      class: "ruleview-rule-source",
      textContent: this.rule.title
    });
    source.addEventListener("click", function() {
      let rule = this.rule;
      let evt = this.doc.createEvent("CustomEvent");
      evt.initCustomEvent("CssRuleViewCSSLinkClicked", true, false, {
        rule: rule,
      });
      this.element.dispatchEvent(evt);
    }.bind(this));

    let code = createChild(this.element, "div", {
      class: "ruleview-code"
    });

    let header = createChild(code, "div", {});

    this.selectorText = createChild(header, "span", {
      class: "ruleview-selector"
    });

    this.openBrace = createChild(header, "span", {
      class: "ruleview-ruleopen",
      textContent: " {"
    });

    code.addEventListener("click", function() {
      let selection = this.doc.defaultView.getSelection();
      if (selection.isCollapsed) {
        this.newProperty();
      }
    }.bind(this), false);

    this.element.addEventListener("mousedown", function() {
      this.doc.defaultView.focus();

      let editorNodes =
        this.doc.querySelectorAll(".styleinspector-propertyeditor");

      if (editorNodes) {
        for (let node of editorNodes) {
          if (node.inplaceEditor) {
            node.inplaceEditor._clear();
          }
        }
      }
    }.bind(this), false);

    this.propertyList = createChild(code, "ul", {
      class: "ruleview-propertylist"
    });

    this.populate();

    this.closeBrace = createChild(code, "div", {
      class: "ruleview-ruleclose",
      tabindex: "0",
      textContent: "}"
    });

    // Create a property editor when the close brace is clicked.
    editableItem({ element: this.closeBrace }, function(aElement) {
      this.newProperty();
    }.bind(this));
  },

  /**
   * Update the rule editor with the contents of the rule.
   */
  populate: function RuleEditor_populate()
  {
    // Clear out existing viewers.
    while (this.selectorText.hasChildNodes()) {
      this.selectorText.removeChild(this.selectorText.lastChild);
    }

    // If selector text comes from a css rule, highlight selectors that
    // actually match.  For custom selector text (such as for the 'element'
    // style, just show the text directly.
    if (this.rule.domRule && this.rule.domRule.selectorText) {
      let selectors = CssLogic.getSelectors(this.rule.selectorText);
      let element = this.rule.inherited || this.ruleView._viewedElement;
      for (let i = 0; i < selectors.length; i++) {
        let selector = selectors[i];
        if (i != 0) {
          createChild(this.selectorText, "span", {
            class: "ruleview-selector-separator",
            textContent: ", "
          });
        }
        let cls = element.mozMatchesSelector(selector) ? "ruleview-selector-matched" :
                                                         "ruleview-selector-unmatched";
        createChild(this.selectorText, "span", {
          class: cls,
          textContent: selector
        });
      }
    } else {
      this.selectorText.textContent = this.rule.selectorText;
    }

    for (let prop of this.rule.textProps) {
      if (!prop.editor) {
        new TextPropertyEditor(this, prop);
        this.propertyList.appendChild(prop.editor.element);
      }
    }
  },

  /**
   * Programatically add a new property to the rule.
   *
   * @param {string} aName
   *        Property name.
   * @param {string} aValue
   *        Property value.
   * @param {string} aPriority
   *        Property priority.
   */
  addProperty: function RuleEditor_addProperty(aName, aValue, aPriority)
  {
    let prop = this.rule.createProperty(aName, aValue, aPriority);
    let editor = new TextPropertyEditor(this, prop);
    this.propertyList.appendChild(editor.element);
  },

  /**
   * Create a text input for a property name.  If a non-empty property
   * name is given, we'll create a real TextProperty and add it to the
   * rule.
   */
  newProperty: function RuleEditor_newProperty()
  {
    // If we're already creating a new property, ignore this.
    if (!this.closeBrace.hasAttribute("tabindex")) {
      return;
    }

    // While we're editing a new property, it doesn't make sense to
    // start a second new property editor, so disable focusing the
    // close brace for now.
    this.closeBrace.removeAttribute("tabindex");

    this.newPropItem = createChild(this.propertyList, "li", {
      class: "ruleview-property ruleview-newproperty",
    });

    this.newPropSpan = createChild(this.newPropItem, "span", {
      class: "ruleview-propertyname",
      tabindex: "0"
    });

    new InplaceEditor({
      element: this.newPropSpan,
      done: this._onNewProperty,
      destroy: this._newPropertyDestroy,
      advanceChars: ":"
    });
  },

  /**
   * Called when the new property input has been dismissed.
   * Will create a new TextProperty if necessary.
   *
   * @param {string} aValue
   *        The value in the editor.
   * @param {bool} aCommit
   *        True if the value should be committed.
   */
  _onNewProperty: function RuleEditor__onNewProperty(aValue, aCommit)
  {
    if (!aValue || !aCommit) {
      return;
    }

    // Create an empty-valued property and start editing it.
    let prop = this.rule.createProperty(aValue, "", "");
    let editor = new TextPropertyEditor(this, prop);
    this.propertyList.appendChild(editor.element);
    editor.valueSpan.click();
  },

  /**
   * Called when the new property editor is destroyed.
   */
  _newPropertyDestroy: function RuleEditor__newPropertyDestroy()
  {
    // We're done, make the close brace focusable again.
    this.closeBrace.setAttribute("tabindex", "0");

    this.propertyList.removeChild(this.newPropItem);
    delete this.newPropItem;
    delete this.newPropSpan;
  }
};

/**
 * Create a TextPropertyEditor.
 *
 * @param {RuleEditor} aRuleEditor
 *        The rule editor that owns this TextPropertyEditor.
 * @param {TextProperty} aProperty
 *        The text property to edit.
 * @constructor
 */
function TextPropertyEditor(aRuleEditor, aProperty)
{
  this.doc = aRuleEditor.doc;
  this.prop = aProperty;
  this.prop.editor = this;

  this._onEnableClicked = this._onEnableClicked.bind(this);
  this._onExpandClicked = this._onExpandClicked.bind(this);
  this._onStartEditing = this._onStartEditing.bind(this);
  this._onNameDone = this._onNameDone.bind(this);
  this._onValueDone = this._onValueDone.bind(this);

  this._create();
  this.update();
}

TextPropertyEditor.prototype = {
  get editing() {
    return !!(this.nameSpan.inplaceEditor || this.valueSpan.inplaceEditor);
  },

  /**
   * Create the property editor's DOM.
   */
  _create: function TextPropertyEditor_create()
  {
    this.element = this.doc.createElementNS(HTML_NS, "li");
    this.element.classList.add("ruleview-property");

    // The enable checkbox will disable or enable the rule.
    this.enable = createChild(this.element, "input", {
      class: "ruleview-enableproperty",
      type: "checkbox",
      tabindex: "-1"
    });
    this.enable.addEventListener("click", this._onEnableClicked, true);

    // Click to expand the computed properties of the text property.
    this.expander = createChild(this.element, "span", {
      class: "ruleview-expander"
    });
    this.expander.addEventListener("click", this._onExpandClicked, true);

    this.nameContainer = createChild(this.element, "span", {
      class: "ruleview-namecontainer"
    });
    this.nameContainer.addEventListener("click", function(aEvent) {
      // Clicks within the name shouldn't propagate any further.
      aEvent.stopPropagation();
      if (aEvent.target === propertyContainer) {
        this.nameSpan.click();
      }
    }.bind(this), false);

    // Property name, editable when focused.  Property name
    // is committed when the editor is unfocused.
    this.nameSpan = createChild(this.nameContainer, "span", {
      class: "ruleview-propertyname",
      tabindex: "0",
    });

    editableField({
      start: this._onStartEditing,
      element: this.nameSpan,
      done: this._onNameDone,
      advanceChars: ':'
    });

    appendText(this.nameContainer, ": ");

    // Create a span that will hold the property and semicolon.
    // Use this span to create a slightly larger click target
    // for the value.
    let propertyContainer = createChild(this.element, "span", {
      class: "ruleview-propertycontainer"
    });
    propertyContainer.addEventListener("click", function(aEvent) {
      // Clicks within the value shouldn't propagate any further.
      aEvent.stopPropagation();
      if (aEvent.target === propertyContainer) {
        this.valueSpan.click();
      }
    }.bind(this), false);

    // Property value, editable when focused.  Changes to the
    // property value are applied as they are typed, and reverted
    // if the user presses escape.
    this.valueSpan = createChild(propertyContainer, "span", {
      class: "ruleview-propertyvalue",
      tabindex: "0",
    });

    // Save the initial value as the last committed value,
    // for restoring after pressing escape.
    this.committed = { name: this.prop.name,
                       value: this.prop.value,
                       priority: this.prop.priority };

    appendText(propertyContainer, ";");

    this.warning = createChild(this.element, "div", {
      hidden: "",
      class: "ruleview-warning",
      title: CssLogic.l10n("rule.warning.title"),
    });

    // Holds the viewers for the computed properties.
    // will be populated in |_updateComputed|.
    this.computed = createChild(this.element, "ul", {
      class: "ruleview-computedlist",
    });

    editableField({
      start: this._onStartEditing,
      element: this.valueSpan,
      done: this._onValueDone,
      validate: this._validate.bind(this),
      warning: this.warning,
      advanceChars: ';'
    });
  },

  /**
   * Populate the span based on changes to the TextProperty.
   */
  update: function TextPropertyEditor_update()
  {
    if (this.prop.enabled) {
      this.enable.style.removeProperty("visibility");
      this.enable.setAttribute("checked", "");
    } else {
      this.enable.style.visibility = "visible";
      this.enable.removeAttribute("checked");
    }

    if (this.prop.overridden && !this.editing) {
      this.element.classList.add("ruleview-overridden");
    } else {
      this.element.classList.remove("ruleview-overridden");
    }

    let name = this.prop.name;
    this.nameSpan.textContent = name;

    // Combine the property's value and priority into one string for
    // the value.
    let val = this.prop.value;
    if (this.prop.priority) {
      val += " !" + this.prop.priority;
    }
    this.valueSpan.textContent = val;
    this.warning.hidden = this._validate();

    let store = this.prop.rule.elementStyle.store;
    let propDirty = store.userProperties.contains(this.prop.rule.style, name);
    if (propDirty) {
      this.element.setAttribute("dirty", "");
    } else {
      this.element.removeAttribute("dirty");
    }

    // Populate the computed styles.
    this._updateComputed();
  },

  _onStartEditing: function TextPropertyEditor_onStartEditing()
  {
    this.element.classList.remove("ruleview-overridden");
  },

  /**
   * Populate the list of computed styles.
   */
  _updateComputed: function TextPropertyEditor_updateComputed()
  {
    // Clear out existing viewers.
    while (this.computed.hasChildNodes()) {
      this.computed.removeChild(this.computed.lastChild);
    }

    let showExpander = false;
    for each (let computed in this.prop.computed) {
      // Don't bother to duplicate information already
      // shown in the text property.
      if (computed.name === this.prop.name) {
        continue;
      }

      showExpander = true;

      let li = createChild(this.computed, "li", {
        class: "ruleview-computed"
      });

      if (computed.overridden) {
        li.classList.add("ruleview-overridden");
      }

      createChild(li, "span", {
        class: "ruleview-propertyname",
        textContent: computed.name
      });
      appendText(li, ": ");

      createChild(li, "span", {
        class: "ruleview-propertyvalue",
        textContent: computed.value
      });
      appendText(li, ";");
    }

    // Show or hide the expander as needed.
    if (showExpander) {
      this.expander.style.visibility = "visible";
    } else {
      this.expander.style.visibility = "hidden";
    }
  },

  /**
   * Handles clicks on the disabled property.
   */
  _onEnableClicked: function TextPropertyEditor_onEnableClicked(aEvent)
  {
    this.prop.setEnabled(this.enable.checked);
    aEvent.stopPropagation();
  },

  /**
   * Handles clicks on the computed property expander.
   */
  _onExpandClicked: function TextPropertyEditor_onExpandClicked(aEvent)
  {
    this.expander.classList.toggle("styleinspector-open");
    this.computed.classList.toggle("styleinspector-open");
    aEvent.stopPropagation();
  },

  /**
   * Called when the property name's inplace editor is closed.
   * Ignores the change if the user pressed escape, otherwise
   * commits it.
   *
   * @param {string} aValue
   *        The value contained in the editor.
   * @param {boolean} aCommit
   *        True if the change should be applied.
   */
  _onNameDone: function TextPropertyEditor_onNameDone(aValue, aCommit)
  {
    if (!aCommit) {
      if (this.prop.overridden) {
        this.element.classList.add("ruleview-overridden");
      }

      return;
    }
    if (!aValue) {
      this.prop.remove();
      this.element.parentNode.removeChild(this.element);
      return;
    }
    this.prop.setName(aValue);
  },

  /**
   * Pull priority (!important) out of the value provided by a
   * value editor.
   *
   * @param {string} aValue
   *        The value from the text editor.
   * @return {object} an object with 'value' and 'priority' properties.
   */
  _parseValue: function TextPropertyEditor_parseValue(aValue)
  {
    let pieces = aValue.split("!", 2);
    return {
      value: pieces[0].trim(),
      priority: (pieces.length > 1 ? pieces[1].trim() : "")
    };
  },

  /**
   * Called when a value editor closes.  If the user pressed escape,
   * revert to the value this property had before editing.
   *
   * @param {string} aValue
   *        The value contained in the editor.
   * @param {bool} aCommit
   *        True if the change should be applied.
   */
   _onValueDone: function PropertyEditor_onValueDone(aValue, aCommit)
  {
    if (aCommit) {
      let val = this._parseValue(aValue);
      this.prop.setValue(val.value, val.priority);
      this.committed.value = this.prop.value;
      this.committed.priority = this.prop.priority;
    } else {
      this.prop.setValue(this.committed.value, this.committed.priority);
    }
  },

  /**
   * Validate this property.
   *
   * @param {string} [aValue]
   *        Override the actual property value used for validation without
   *        applying property values e.g. validate as you type.
   *
   * @return {bool} true if the property value is valid, false otherwise.
   */
  _validate: function TextPropertyEditor_validate(aValue)
  {
    let name = this.prop.name;
    let value = typeof aValue == "undefined" ? this.prop.value : aValue;
    let val = this._parseValue(value);
    let style = this.doc.createElementNS(HTML_NS, "div").style;
    let prefs = Services.prefs;

    // We toggle output of errors whilst the user is typing a property value.
    let prefVal = Services.prefs.getBoolPref("layout.css.report_errors");
    prefs.setBoolPref("layout.css.report_errors", false);

    try {
      style.setProperty(name, val.value, val.priority);
    } finally {
      prefs.setBoolPref("layout.css.report_errors", prefVal);
    }
    return !!style.getPropertyValue(name);
  },
};

/**
 * Mark a span editable.  |editableField| will listen for the span to
 * be focused and create an InlineEditor to handle text input.
 * Changes will be committed when the InlineEditor's input is blurred
 * or dropped when the user presses escape.
 *
 * @param {object} aOptions
 *    Options for the editable field, including:
 *    {Element} element:
 *      (required) The span to be edited on focus.
 *    {function} canEdit:
 *       Will be called before creating the inplace editor.  Editor
 *       won't be created if canEdit returns false.
 *    {function} start:
 *       Will be called when the inplace editor is initialized.
 *    {function} change:
 *       Will be called when the text input changes.  Will be called
 *       with the current value of the text input.
 *    {function} done:
 *       Called when input is committed or blurred.  Called with
 *       current value and a boolean telling the caller whether to
 *       commit the change.  This function is called before the editor
 *       has been torn down.
 *    {function} destroy:
 *       Called when the editor is destroyed and has been torn down.
 *    {string} advanceChars:
 *       If any characters in advanceChars are typed, focus will advance
 *       to the next element.
 *    {boolean} stopOnReturn:
 *       If true, the return key will not advance the editor to the next
 *       focusable element.
 *    {string} trigger: The DOM event that should trigger editing,
 *      defaults to "click"
 */
function editableField(aOptions)
{
  return editableItem(aOptions, function(aElement, aEvent) {
    new InplaceEditor(aOptions, aEvent);
  });
}

/**
 * Handle events for an element that should respond to
 * clicks and sit in the editing tab order, and call
 * a callback when it is activated.
 *
 * @param {object} aOptions
 *    The options for this editor, including:
 *    {Element} element: The DOM element.
 *    {string} trigger: The DOM event that should trigger editing,
 *      defaults to "click"
 * @param {function} aCallback
 *        Called when the editor is activated.
 */
this.editableItem = function editableItem(aOptions, aCallback)
{
  let trigger = aOptions.trigger || "click"
  let element = aOptions.element;
  element.addEventListener(trigger, function(evt) {
    let win = this.ownerDocument.defaultView;
    let selection = win.getSelection();
    if (trigger != "click" || selection.isCollapsed) {
      aCallback(element, evt);
    }
    evt.stopPropagation();
  }, false);

  // If focused by means other than a click, start editing by
  // pressing enter or space.
  element.addEventListener("keypress", function(evt) {
    if (evt.keyCode === Ci.nsIDOMKeyEvent.DOM_VK_RETURN ||
        evt.charCode === Ci.nsIDOMKeyEvent.DOM_VK_SPACE) {
      aCallback(element);
    }
  }, true);

  // Ugly workaround - the element is focused on mousedown but
  // the editor is activated on click/mouseup.  This leads
  // to an ugly flash of the focus ring before showing the editor.
  // So hide the focus ring while the mouse is down.
  element.addEventListener("mousedown", function(evt) {
    let cleanup = function() {
      element.style.removeProperty("outline-style");
      element.removeEventListener("mouseup", cleanup, false);
      element.removeEventListener("mouseout", cleanup, false);
    };
    element.style.setProperty("outline-style", "none");
    element.addEventListener("mouseup", cleanup, false);
    element.addEventListener("mouseout", cleanup, false);
  }, false);

  // Mark the element editable field for tab
  // navigation while editing.
  element._editable = true;
}

this._editableField = editableField;

function InplaceEditor(aOptions, aEvent)
{
  this.elt = aOptions.element;
  let doc = this.elt.ownerDocument;
  this.doc = doc;
  this.elt.inplaceEditor = this;

  this.change = aOptions.change;
  this.done = aOptions.done;
  this.destroy = aOptions.destroy;
  this.initial = aOptions.initial ? aOptions.initial : this.elt.textContent;
  this.multiline = aOptions.multiline || false;
  this.stopOnReturn = !!aOptions.stopOnReturn;

  this._onBlur = this._onBlur.bind(this);
  this._onKeyPress = this._onKeyPress.bind(this);
  this._onInput = this._onInput.bind(this);
  this._onKeyup = this._onKeyup.bind(this);

  this._createInput();
  this._autosize();

  // Pull out character codes for advanceChars, listing the
  // characters that should trigger a blur.
  this._advanceCharCodes = {};
  let advanceChars = aOptions.advanceChars || '';
  for (let i = 0; i < advanceChars.length; i++) {
    this._advanceCharCodes[advanceChars.charCodeAt(i)] = true;
  }

  // Hide the provided element and add our editor.
  this.originalDisplay = this.elt.style.display;
  this.elt.style.display = "none";
  this.elt.parentNode.insertBefore(this.input, this.elt);

  if (typeof(aOptions.selectAll) == "undefined" || aOptions.selectAll) {
    this.input.select();
  }
  this.input.focus();

  this.input.addEventListener("blur", this._onBlur, false);
  this.input.addEventListener("keypress", this._onKeyPress, false);
  this.input.addEventListener("input", this._onInput, false);
  this.input.addEventListener("mousedown", function(aEvt) { aEvt.stopPropagation(); }, false);

  this.warning = aOptions.warning;
  this.validate = aOptions.validate;

  if (this.warning && this.validate) {
    this.input.addEventListener("keyup", this._onKeyup, false);
  }

  if (aOptions.start) {
    aOptions.start(this, aEvent);
  }
}

InplaceEditor.prototype = {
  _createInput: function InplaceEditor_createEditor()
  {
    this.input = this.doc.createElementNS(HTML_NS, this.multiline ? "textarea" : "input");
    this.input.inplaceEditor = this;
    this.input.classList.add("styleinspector-propertyeditor");
    this.input.value = this.initial;

    copyTextStyles(this.elt, this.input);
  },

  /**
   * Get rid of the editor.
   */
  _clear: function InplaceEditor_clear()
  {
    if (!this.input) {
      // Already cleared.
      return;
    }

    this.input.removeEventListener("blur", this._onBlur, false);
    this.input.removeEventListener("keypress", this._onKeyPress, false);
    this.input.removeEventListener("keyup", this._onKeyup, false);
    this.input.removeEventListener("oninput", this._onInput, false);
    this._stopAutosize();

    this.elt.style.display = this.originalDisplay;
    this.elt.focus();

    if (this.destroy) {
      this.destroy();
    }

    this.elt.parentNode.removeChild(this.input);
    this.input = null;

    delete this.elt.inplaceEditor;
    delete this.elt;
  },

  /**
   * Keeps the editor close to the size of its input string.  This is pretty
   * crappy, suggestions for improvement welcome.
   */
  _autosize: function InplaceEditor_autosize()
  {
    // Create a hidden, absolutely-positioned span to measure the text
    // in the input.  Boo.

    // We can't just measure the original element because a) we don't
    // change the underlying element's text ourselves (we leave that
    // up to the client), and b) without tweaking the style of the
    // original element, it might wrap differently or something.
    this._measurement = this.doc.createElementNS(HTML_NS, this.multiline ? "pre" : "span");
    this._measurement.className = "autosizer";
    this.elt.parentNode.appendChild(this._measurement);
    let style = this._measurement.style;
    style.visibility = "hidden";
    style.position = "absolute";
    style.top = "0";
    style.left = "0";
    copyTextStyles(this.input, this._measurement);
    this._updateSize();
  },

  /**
   * Clean up the mess created by _autosize().
   */
  _stopAutosize: function InplaceEditor_stopAutosize()
  {
    if (!this._measurement) {
      return;
    }
    this._measurement.parentNode.removeChild(this._measurement);
    delete this._measurement;
  },

  /**
   * Size the editor to fit its current contents.
   */
  _updateSize: function InplaceEditor_updateSize()
  {
    // Replace spaces with non-breaking spaces.  Otherwise setting
    // the span's textContent will collapse spaces and the measurement
    // will be wrong.
    this._measurement.textContent = this.input.value.replace(/ /g, '\u00a0');

    // We add a bit of padding to the end.  Should be enough to fit
    // any letter that could be typed, otherwise we'll scroll before
    // we get a chance to resize.  Yuck.
    let width = this._measurement.offsetWidth + 10;

    if (this.multiline) {
      // Make sure there's some content in the current line.  This is a hack to account
      // for the fact that after adding a newline the <pre> doesn't grow unless there's
      // text content on the line.
      width += 15;
      this._measurement.textContent += "M";
      this.input.style.height = this._measurement.offsetHeight + "px";
    }

    this.input.style.width = width + "px";
  },

   /**
   * Increment property values in rule view.
   *
   * @param {number} increment 
   *        The amount to increase/decrease the property value.
   * @return {bool} true if value has been incremented.
   */
  _incrementValue: function InplaceEditor_incrementValue(increment)
  {
    let value = this.input.value;
    let selectionStart = this.input.selectionStart;
    let selectionEnd = this.input.selectionEnd;

    let newValue = this._incrementCSSValue(value, increment, selectionStart, selectionEnd);

    if (!newValue) {
      return false;
    }

    this.input.value = newValue.value;
    this.input.setSelectionRange(newValue.start, newValue.end);

    return true;
  },

  /**
   * Increment the property value based on the property type.
   *
   * @param {string} value
   *        Property value.
   * @param {number} increment
   *        Amount to increase/decrease the property value.
   * @param {number} selStart
   *        Starting index of the value.
   * @param {number} selEnd
   *        Ending index of the value.
   * @return {object} object with properties 'value', 'start', and 'end'.
   */
  _incrementCSSValue: function InplaceEditor_incrementCSSValue(value, increment, selStart, 
                                                               selEnd)
  {
    let range = this._parseCSSValue(value, selStart);
    let type = (range && range.type) || "";
    let rawValue = (range ? value.substring(range.start, range.end) : "");
    let incrementedValue = null, selection;

    if (type === "num") {
      let newValue = this._incrementRawValue(rawValue, increment);
      if (newValue !== null) {
        incrementedValue = newValue;
        selection = [0, incrementedValue.length];
      }
    } else if (type === "hex") {
      let exprOffset = selStart - range.start;
      let exprOffsetEnd = selEnd - range.start;
      let newValue = this._incHexColor(rawValue, increment, exprOffset, exprOffsetEnd);
      if (newValue) {
        incrementedValue = newValue.value;
        selection = newValue.selection;
      }
    } else {
      let info;
      if (type === "rgb" || type === "hsl") {
        info = {};
        let part = value.substring(range.start, selStart).split(",").length - 1;
        if (part === 3) { // alpha
          info.minValue = 0;
          info.maxValue = 1;
        } else if (type === "rgb") {
          info.minValue = 0;
          info.maxValue = 255;
        } else if (part !== 0) { // hsl percentage
          info.minValue = 0;
          info.maxValue = 100;

          // select the previous number if the selection is at the end of a percentage sign
          if (value.charAt(selStart - 1) === "%") {
            --selStart;
          }
        }
      }
      return this._incrementGenericValue(value, increment, selStart, selEnd, info);
    }

    if (incrementedValue === null) {
      return;
    }

    let preRawValue = value.substr(0, range.start);
    let postRawValue = value.substr(range.end);

    return {
      value: preRawValue + incrementedValue + postRawValue,
      start: range.start + selection[0],
      end: range.start + selection[1]
    };
  },

  /**
   * Parses the property value and type.
   *
   * @param {string} value 
   *        Property value.
   * @param {number} offset 
   *        Starting index of value.
   * @return {object} object with properties 'value', 'start', 'end', and 'type'.
   */
   _parseCSSValue: function InplaceEditor_parseCSSValue(value, offset)
  {
    const reSplitCSS = /(url\("?[^"\)]+"?\)?)|(rgba?\([^)]*\)?)|(hsla?\([^)]*\)?)|(#[\dA-Fa-f]+)|(-?\d+(\.\d+)?(%|[a-z]{1,4})?)|"([^"]*)"?|'([^']*)'?|([^,\s\/!\(\)]+)|(!(.*)?)/;
    let start = 0;
    let m;

    // retreive values from left to right until we find the one at our offset
    while ((m = reSplitCSS.exec(value)) &&
          (m.index + m[0].length < offset)) {
      value = value.substr(m.index + m[0].length);
      start += m.index + m[0].length;
      offset -= m.index + m[0].length;
    }

    if (!m) {
      return;
    }

    let type;
    if (m[1]) {
      type = "url";
    } else if (m[2]) {
      type = "rgb";
    } else if (m[3]) {
      type = "hsl";
    } else if (m[4]) {
      type = "hex";
    } else if (m[5]) {
      type = "num";
    }

    return {
      value: m[0],
      start: start + m.index,
      end: start + m.index + m[0].length,
      type: type
    };
  },

  /**
   * Increment the property value for types other than
   * number or hex, such as rgb, hsl, and file names.
   *
   * @param {string} value 
   *        Property value.
   * @param {number} increment 
   *        Amount to increment/decrement.
   * @param {number} offset 
   *        Starting index of the property value.
   * @param {number} offsetEnd 
   *        Ending index of the property value.
   * @param {object} info 
   *        Object with details about the property value.
   * @return {object} object with properties 'value', 'start', and 'end'.
   */
  _incrementGenericValue: function InplaceEditor_incrementGenericValue(value, increment, offset,
                                                                       offsetEnd, info)
  {
    // Try to find a number around the cursor to increment.
    let start, end;
    // Check if we are incrementing in a non-number context (such as a URL)
    if (/^-?[0-9.]/.test(value.substring(offset, offsetEnd)) &&
      !(/\d/.test(value.charAt(offset - 1) + value.charAt(offsetEnd)))) {
      // We have a number selected, possibly with a suffix, and we are not in
      // the disallowed case of just part of a known number being selected.
      // Use that number.
      start = offset;
      end = offsetEnd;
    } else {
      // Parse periods as belonging to the number only if we are in a known number
      // context. (This makes incrementing the 1 in 'image1.gif' work.)
      let pattern = "[" + (info ? "0-9." : "0-9") + "]*";
      let before = new RegExp(pattern + "$").exec(value.substr(0, offset))[0].length;
      let after = new RegExp("^" + pattern).exec(value.substr(offset))[0].length;

      start = offset - before;
      end = offset + after;

      // Expand the number to contain an initial minus sign if it seems
      // free-standing.
      if (value.charAt(start - 1) === "-" &&
         (start - 1 === 0 || /[ (:,='"]/.test(value.charAt(start - 2)))) {
        --start;
      }
    }

    if (start !== end)
    {
      // Include percentages as part of the incremented number (they are
      // common enough).
      if (value.charAt(end) === "%") {
        ++end;
      }

      let first = value.substr(0, start);
      let mid = value.substring(start, end);
      let last = value.substr(end);

      mid = this._incrementRawValue(mid, increment, info);

      if (mid !== null) {
        return {
          value: first + mid + last,
          start: start,
          end: start + mid.length
        };
      }
    }
  },

  /**
   * Increment the property value for numbers.
   *
   * @param {string} rawValue 
   *        Raw value to increment.
   * @param {number} increment 
   *        Amount to increase/decrease the raw value.
   * @param {object} info 
   *        Object with info about the property value.
   * @return {string} the incremented value.
   */
  _incrementRawValue: function InplaceEditor_incrementRawValue(rawValue, increment, info)
  {
    let num = parseFloat(rawValue);

    if (isNaN(num)) {
      return null;
    }

    let number = /\d+(\.\d+)?/.exec(rawValue);
    let units = rawValue.substr(number.index + number[0].length);

    // avoid rounding errors
    let newValue = Math.round((num + increment) * 1000) / 1000;

    if (info && "minValue" in info) {
      newValue = Math.max(newValue, info.minValue);
    }
    if (info && "maxValue" in info) {
      newValue = Math.min(newValue, info.maxValue);
    }

    newValue = newValue.toString();

    return newValue + units;
  },

  /**
   * Increment the property value for hex.
   *
   * @param {string} value 
   *        Property value.
   * @param {number} increment 
   *        Amount to increase/decrease the property value.
   * @param {number} offset 
   *        Starting index of the property value.
   * @param {number} offsetEnd 
   *        Ending index of the property value.
   * @return {object} object with properties 'value' and 'selection'.
   */
  _incHexColor: function InplaceEditor_incHexColor(rawValue, increment, offset, offsetEnd)
  {
    // Return early if no part of the rawValue is selected.
    if (offsetEnd > rawValue.length && offset >= rawValue.length) {
      return;
    }
    if (offset < 1 && offsetEnd <= 1) {
      return;
    }
    // Ignore the leading #.
    rawValue = rawValue.substr(1);
    --offset;
    --offsetEnd;

    // Clamp the selection to within the actual value.
    offset = Math.max(offset, 0);
    offsetEnd = Math.min(offsetEnd, rawValue.length);
    offsetEnd = Math.max(offsetEnd, offset);

    // Normalize #ABC -> #AABBCC.
    if (rawValue.length === 3) {
      rawValue = rawValue.charAt(0) + rawValue.charAt(0) +
                 rawValue.charAt(1) + rawValue.charAt(1) +
                 rawValue.charAt(2) + rawValue.charAt(2);
      offset *= 2;
      offsetEnd *= 2;
    }

    if (rawValue.length !== 6) {
      return;
    }

    // If no selection, increment an adjacent color, preferably one to the left.
    if (offset === offsetEnd) {
      if (offset === 0) {
        offsetEnd = 1;
      } else {
        offset = offsetEnd - 1;
      }
    }

    // Make the selection cover entire parts.
    offset -= offset % 2;
    offsetEnd += offsetEnd % 2;

    // Remap the increments from [0.1, 1, 10] to [1, 1, 16].
    if (-1 < increment && increment < 1) {
      increment = (increment < 0 ? -1 : 1);
    }
    if (Math.abs(increment) === 10) {
      increment = (increment < 0 ? -16 : 16);
    }

    let isUpper = (rawValue.toUpperCase() === rawValue);

    for (let pos = offset; pos < offsetEnd; pos += 2) {
      // Increment the part in [pos, pos+2).
      let mid = rawValue.substr(pos, 2);
      let value = parseInt(mid, 16);

      if (isNaN(value)) {
        return;
      }

      mid = Math.min(Math.max(value + increment, 0), 255).toString(16);

      while (mid.length < 2) {
        mid = "0" + mid;
      }
      if (isUpper) {
        mid = mid.toUpperCase();
      }

      rawValue = rawValue.substr(0, pos) + mid + rawValue.substr(pos + 2);
    }

    return {
      value: "#" + rawValue,
      selection: [offset + 1, offsetEnd + 1]
    };
  },

  /**
   * Call the client's done handler and clear out.
   */
  _apply: function InplaceEditor_apply(aEvent)
  {
    if (this._applied) {
      return;
    }

    this._applied = true;

    if (this.done) {
      let val = this.input.value.trim();
      return this.done(this.cancelled ? this.initial : val, !this.cancelled);
    }

    return null;
  },

  /**
   * Handle loss of focus by calling done if it hasn't been called yet.
   */
  _onBlur: function InplaceEditor_onBlur(aEvent, aDoNotClear)
  {
    this._apply();
    if (!aDoNotClear) {
      this._clear();
    }
  },

  /**
   * Handle the input field's keypress event.
   */
  _onKeyPress: function InplaceEditor_onKeyPress(aEvent)
  {
    let prevent = false;

    const largeIncrement = 100;
    const mediumIncrement = 10;
    const smallIncrement = 0.1;

    let increment = 0;

    if (aEvent.keyCode === Ci.nsIDOMKeyEvent.DOM_VK_UP
       || aEvent.keyCode === Ci.nsIDOMKeyEvent.DOM_VK_PAGE_UP) {
      increment = 1;
    } else if (aEvent.keyCode === Ci.nsIDOMKeyEvent.DOM_VK_DOWN
       || aEvent.keyCode === Ci.nsIDOMKeyEvent.DOM_VK_PAGE_DOWN) {
      increment = -1;
    }

    if (aEvent.shiftKey && !aEvent.altKey) {
      if (aEvent.keyCode === Ci.nsIDOMKeyEvent.DOM_VK_PAGE_UP
           ||  aEvent.keyCode === Ci.nsIDOMKeyEvent.DOM_VK_PAGE_DOWN) {
        increment *= largeIncrement;
      } else {
        increment *= mediumIncrement;
      }
    } else if (aEvent.altKey && !aEvent.shiftKey) {
      increment *= smallIncrement;
    }

    if (increment && this._incrementValue(increment) ) {
      this._updateSize();
      prevent = true;
    }

    if (this.multiline &&
        aEvent.keyCode === Ci.nsIDOMKeyEvent.DOM_VK_RETURN &&
        aEvent.shiftKey) {
      prevent = false;
    } else if (aEvent.charCode in this._advanceCharCodes
       || aEvent.keyCode === Ci.nsIDOMKeyEvent.DOM_VK_RETURN
       || aEvent.keyCode === Ci.nsIDOMKeyEvent.DOM_VK_TAB) {
      prevent = true;

      let direction = FOCUS_FORWARD;
      if (aEvent.keyCode === Ci.nsIDOMKeyEvent.DOM_VK_TAB &&
          aEvent.shiftKey) {
        this.cancelled = true;
        direction = FOCUS_BACKWARD;
      }
      if (this.stopOnReturn && aEvent.keyCode === Ci.nsIDOMKeyEvent.DOM_VK_RETURN) {
        direction = null;
      }

      let input = this.input;

      this._apply();

      let fm = Cc["@mozilla.org/focus-manager;1"].getService(Ci.nsIFocusManager);
      if (direction !== null && fm.focusedElement === input) {
        // If the focused element wasn't changed by the done callback,
        // move the focus as requested.
        let next = moveFocus(this.doc.defaultView, direction);

        // If the next node to be focused has been tagged as an editable
        // node, send it a click event to trigger
        if (next && next.ownerDocument === this.doc && next._editable) {
          next.click();
        }
      }

      this._clear();
    } else if (aEvent.keyCode === Ci.nsIDOMKeyEvent.DOM_VK_ESCAPE) {
      // Cancel and blur ourselves.
      prevent = true;
      this.cancelled = true;
      this._apply();
      this._clear();
      aEvent.stopPropagation();
    } else if (aEvent.keyCode === Ci.nsIDOMKeyEvent.DOM_VK_SPACE) {
      // No need for leading spaces here.  This is particularly
      // noticable when adding a property: it's very natural to type
      // <name>: (which advances to the next property) then spacebar.
      prevent = !this.input.value;
    }

    if (prevent) {
      aEvent.preventDefault();
    }
  },

  /**
   * Handle the input field's keyup event.
   */
  _onKeyup: function(aEvent) {
    // Validate the entered value.
    this.warning.hidden = this.validate(this.input.value);
    this._applied = false;
    this._onBlur(null, true);
  },

  /**
   * Handle changes to the input text.
   */
  _onInput: function InplaceEditor_onInput(aEvent)
  {
    // Validate the entered value.
    if (this.warning && this.validate) {
      this.warning.hidden = this.validate(this.input.value);
    }

    // Update size if we're autosizing.
    if (this._measurement) {
      this._updateSize();
    }

    // Call the user's change handler if available.
    if (this.change) {
      this.change(this.input.value.trim());
    }
  }
};

/*
 * Various API consumers (especially tests) sometimes want to grab the
 * inplaceEditor expando off span elements. However, when each global has its
 * own compartment, those expandos live on Xray wrappers that are only visible
 * within this JSM. So we provide a little workaround here.
 */
this._getInplaceEditorForSpan = function _getInplaceEditorForSpan(aSpan)
{
  return aSpan.inplaceEditor;
};

/**
 * Store of CSSStyleDeclarations mapped to properties that have been changed by
 * the user.
 */
function UserProperties()
{
  // FIXME: This should be a WeakMap once bug 753517 is fixed.
  // See Bug 777373 for details.
  this.map = new Map();
}

UserProperties.prototype = {
  /**
   * Get a named property for a given CSSStyleDeclaration.
   *
   * @param {CSSStyleDeclaration} aStyle
   *        The CSSStyleDeclaration against which the property is mapped.
   * @param {string} aName
   *        The name of the property to get.
   * @param {string} aComputedValue
   *        The computed value of the property.  The user value will only be
   *        returned if the computed value hasn't changed since, and this will
   *        be returned as the default if no user value is available.
   * @return {string}
   *          The property value if it has previously been set by the user, null
   *          otherwise.
   */
  getProperty: function UP_getProperty(aStyle, aName, aComputedValue) {
    let entry = this.map.get(aStyle, null);

    if (entry && aName in entry) {
      let item = entry[aName];
      if (item.computed != aComputedValue) {
        delete entry[aName];
        return aComputedValue;
      }

      return item.user;
    }
    return aComputedValue;

  },

  /**
   * Set a named property for a given CSSStyleDeclaration.
   *
   * @param {CSSStyleDeclaration} aStyle
   *        The CSSStyleDeclaration against which the property is to be mapped.
   * @param {String} aName
   *        The name of the property to set.
   * @param {String} aComputedValue
   *        The computed property value.  The user value will not be used if the
   *        computed value changes.
   * @param {String} aUserValue
   *        The value of the property to set.
   */
  setProperty: function UP_setProperty(aStyle, aName, aComputedValue, aUserValue) {
    let entry = this.map.get(aStyle, null);
    if (entry) {
      entry[aName] = { computed: aComputedValue, user: aUserValue };
    } else {
      let props = {};
      props[aName] = { computed: aComputedValue, user: aUserValue };
      this.map.set(aStyle, props);
    }
  },

  /**
   * Check whether a named property for a given CSSStyleDeclaration is stored.
   *
   * @param {CSSStyleDeclaration} aStyle
   *        The CSSStyleDeclaration against which the property would be mapped.
   * @param {String} aName
   *        The name of the property to check.
   */
  contains: function UP_contains(aStyle, aName) {
    let entry = this.map.get(aStyle, null);
    return !!entry && aName in entry;
  },
};

/**
 * Helper functions
 */

/**
 * Create a child element with a set of attributes.
 *
 * @param {Element} aParent
 *        The parent node.
 * @param {string} aTag
 *        The tag name.
 * @param {object} aAttributes
 *        A set of attributes to set on the node.
 */
function createChild(aParent, aTag, aAttributes)
{
  let elt = aParent.ownerDocument.createElementNS(HTML_NS, aTag);
  for (let attr in aAttributes) {
    if (aAttributes.hasOwnProperty(attr)) {
      if (attr === "textContent") {
        elt.textContent = aAttributes[attr];
      } else {
        elt.setAttribute(attr, aAttributes[attr]);
      }
    }
  }
  aParent.appendChild(elt);
  return elt;
}

function createMenuItem(aMenu, aAttributes)
{
  let item = aMenu.ownerDocument.createElementNS(XUL_NS, "menuitem");
  item.setAttribute("label", _strings.GetStringFromName(aAttributes.label));
  item.setAttribute("accesskey", _strings.GetStringFromName(aAttributes.accesskey));
  item.addEventListener("command", aAttributes.command);

  aMenu.appendChild(item);

  return item;
}

/**
 * Append a text node to an element.
 */
function appendText(aParent, aText)
{
  aParent.appendChild(aParent.ownerDocument.createTextNode(aText));
}

/**
 * Copy text-related styles from one element to another.
 */
function copyTextStyles(aFrom, aTo)
{
  let win = aFrom.ownerDocument.defaultView;
  let style = win.getComputedStyle(aFrom);
  aTo.style.fontFamily = style.getPropertyCSSValue("font-family").cssText;
  aTo.style.fontSize = style.getPropertyCSSValue("font-size").cssText;
  aTo.style.fontWeight = style.getPropertyCSSValue("font-weight").cssText;
  aTo.style.fontStyle = style.getPropertyCSSValue("font-style").cssText;
}

/**
 * Trigger a focus change similar to pressing tab/shift-tab.
 */
function moveFocus(aWin, aDirection)
{
  let fm = Cc["@mozilla.org/focus-manager;1"].getService(Ci.nsIFocusManager);
  return fm.moveFocus(aWin, null, aDirection, 0);
}

XPCOMUtils.defineLazyGetter(this, "clipboardHelper", function() {
  return Cc["@mozilla.org/widget/clipboardhelper;1"].
    getService(Ci.nsIClipboardHelper);
});

XPCOMUtils.defineLazyGetter(this, "_strings", function() {
  return Services.strings.createBundle(
    "chrome://browser/locale/devtools/styleinspector.properties");
});

XPCOMUtils.defineLazyGetter(this, "osString", function() {
  return Cc["@mozilla.org/xre/app-info;1"].getService(Ci.nsIXULRuntime).OS;
});
