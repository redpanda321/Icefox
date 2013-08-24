/* -*- Mode: js2; js2-basic-offset: 2; indent-tabs-mode: nil; -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is DevTools (HeadsUpDisplay) Console Code
 *
 * The Initial Developer of the Original Code is
 *   Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   David Dahl <ddahl@mozilla.com> (original author)
 *   Rob Campbell <rcampbell@mozilla.com>
 *   Johnathan Nightingale <jnightingale@mozilla.com>
 *   Patrick Walton <pcwalton@mozilla.com>
 *   Julian Viereck <jviereck@mozilla.com>
 *   Mihai Șucan <mihai.sucan@gmail.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

var EXPORTED_SYMBOLS = ["HUDService"];

XPCOMUtils.defineLazyServiceGetter(this, "scriptError",
                                   "@mozilla.org/scripterror;1",
                                   "nsIScriptError");

XPCOMUtils.defineLazyServiceGetter(this, "activityDistributor",
                                   "@mozilla.org/network/http-activity-distributor;1",
                                   "nsIHttpActivityDistributor");

XPCOMUtils.defineLazyServiceGetter(this, "sss",
                                   "@mozilla.org/content/style-sheet-service;1",
                                   "nsIStyleSheetService");

XPCOMUtils.defineLazyGetter(this, "PropertyPanel", function () {
  var obj = {};
  try {
    Cu.import("resource://gre/modules/PropertyPanel.jsm", obj);
  } catch (err) {
    Cu.reportError(err);
  }
  return obj.PropertyPanel;
});


function LogFactory(aMessagePrefix)
{
  function log(aMessage) {
    var _msg = aMessagePrefix + " " + aMessage + "\n";
    dump(_msg);
  }
  return log;
}

let log = LogFactory("*** HUDService:");

const HUD_STYLESHEET_URI = "chrome://global/skin/webConsole.css";
const HUD_STRINGS_URI = "chrome://global/locale/headsUpDisplay.properties";

XPCOMUtils.defineLazyGetter(this, "stringBundle", function () {
  return Services.strings.createBundle(HUD_STRINGS_URI);
});

// The amount of time in milliseconds that must pass between messages to
// trigger the display of a new group.
const NEW_GROUP_DELAY = 5000;

// The amount of time in milliseconds that we wait before performing a live
// search.
const SEARCH_DELAY = 200;

const ERRORS = { LOG_MESSAGE_MISSING_ARGS:
                 "Missing arguments: aMessage, aConsoleNode and aMessageNode are required.",
                 CANNOT_GET_HUD: "Cannot getHeads Up Display with provided ID",
                 MISSING_ARGS: "Missing arguments",
                 LOG_OUTPUT_FAILED: "Log Failure: Could not append messageNode to outputNode",
};

/**
 * Helper object for networking stuff.
 *
 * All of the following functions have been taken from the Firebug source. They
 * have been modified to match the Firefox coding rules.
 */

// FIREBUG CODE BEGIN.

/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2007, Parakey Inc.
 * All rights reserved.
 *
 * Redistribution and use of this software in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above
 *   copyright notice, this list of conditions and the
 *   following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the
 *   following disclaimer in the documentation and/or other
 *   materials provided with the distribution.
 *
 * * Neither the name of Parakey Inc. nor the names of its
 *   contributors may be used to endorse or promote products
 *   derived from this software without specific prior
 *   written permission of Parakey Inc.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Creator:
 *  Joe Hewitt
 * Contributors
 *  John J. Barton (IBM Almaden)
 *  Jan Odvarko (Mozilla Corp.)
 *  Max Stepanov (Aptana Inc.)
 *  Rob Campbell (Mozilla Corp.)
 *  Hans Hillen (Paciello Group, Mozilla)
 *  Curtis Bartley (Mozilla Corp.)
 *  Mike Collins (IBM Almaden)
 *  Kevin Decker
 *  Mike Ratcliffe (Comartis AG)
 *  Hernan Rodríguez Colmeiro
 *  Austin Andrews
 *  Christoph Dorn
 *  Steven Roussey (AppCenter Inc, Network54)
 */
var NetworkHelper =
{
  /**
   * Gets the nsIDOMWindow that is associated with aRequest.
   *
   * @param nsIHttpChannel aRequest
   * @returns nsIDOMWindow or null
   */
  getWindowForRequest: function NH_getWindowForRequest(aRequest)
  {
    let loadContext = this.getRequestLoadContext(aRequest);
    if (loadContext) {
      return loadContext.associatedWindow;
    }
    return null;
  },

  /**
   * Gets the nsILoadContext that is associated with aRequest.
   *
   * @param nsIHttpChannel aRequest
   * @returns nsILoadContext or null
   */
  getRequestLoadContext: function NH_getRequestLoadContext(aRequest)
  {
    if (aRequest && aRequest.notificationCallbacks) {
      try {
        return aRequest.notificationCallbacks.getInterface(Ci.nsILoadContext);
      } catch (ex) { }
    }

    if (aRequest && aRequest.loadGroup
                 && aRequest.loadGroup.notificationCallbacks) {
      try {
        return aRequest.loadGroup.notificationCallbacks.getInterface(Ci.nsILoadContext);
      } catch (ex) { }
    }

    return null;
   }
}

// FIREBUG CODE END.

function HUD_SERVICE()
{
  // TODO: provide mixins for FENNEC: bug 568621
  if (appName() == "FIREFOX") {
    var mixins = new FirefoxApplicationHooks();
  }
  else {
    throw new Error("Unsupported Application");
  }

  this.mixins = mixins;
  this.storage = new ConsoleStorage();
  this.defaultFilterPrefs = this.storage.defaultDisplayPrefs;
  this.defaultGlobalConsolePrefs = this.storage.defaultGlobalConsolePrefs;

  // load stylesheet with StyleSheetService
  var uri = Services.io.newURI(HUD_STYLESHEET_URI, null, null);
  sss.loadAndRegisterSheet(uri, sss.AGENT_SHEET);

  // begin observing HTTP traffic
  this.startHTTPObservation();
};

HUD_SERVICE.prototype =
{
  /**
   * L10N shortcut function
   *
   * @param string aName
   * @returns string
   */
  getStr: function HS_getStr(aName)
  {
    return stringBundle.GetStringFromName(aName);
  },

  /**
   * L10N shortcut function
   *
   * @param string aName
   * @returns (format) string
   */
  getFormatStr: function HS_getFormatStr(aName, aArray)
  {
    return stringBundle.formatStringFromName(aName, aArray, aArray.length);
  },

  /**
   * getter for UI commands to be used by the frontend
   *
   * @returns object
   */
  get consoleUI() {
    return HeadsUpDisplayUICommands;
  },

  /**
   * Collection of HUDIds that map to the tabs/windows/contexts
   * that a HeadsUpDisplay can be activated for.
   */
  activatedContexts: [],

  /**
   * Registry of HeadsUpDisplay DOM node ids
   */
  _headsUpDisplays: {},

  /**
   * Mapping of HUDIds to URIspecs
   */
  displayRegistry: {},

  /**
   * Mapping of HUDIds to contentWindows.
   */
  windowRegistry: {},

  /**
   * Mapping of URISpecs to HUDIds
   */
  uriRegistry: {},

  /**
   * The sequencer is a generator (after initialization) that returns unique
   * integers
   */
  sequencer: null,

  /**
   * Each HeadsUpDisplay has a set of filter preferences
   */
  filterPrefs: {},

  /**
   * Event handler to get window errors
   * TODO: a bit of a hack but is able to associate
   * errors thrown in a window's scope we do not know
   * about because of the nsIConsoleMessages not having a
   * window reference.
   * see bug 567165
   *
   * @param nsIDOMWindow aWindow
   * @returns boolean
   */
  setOnErrorHandler: function HS_setOnErrorHandler(aWindow) {
    var self = this;
    var window = aWindow.wrappedJSObject;
    var console = window.console;
    var origOnerrorFunc = window.onerror;
    window.onerror = function windowOnError(aErrorMsg, aURL, aLineNumber)
    {
      if (aURL && !(aURL in self.uriRegistry)) {
        var lineNum = "";
        if (aLineNumber) {
          lineNum = self.getFormatStr("errLine", [aLineNumber]);
        }
        console.error(aErrorMsg + " @ " + aURL + " " + lineNum);
      }

      if (origOnerrorFunc) {
        origOnerrorFunc(aErrorMsg, aURL, aLineNumber);
      }

      return false;
    };
  },

  /**
   * Tell the HUDService that a HeadsUpDisplay can be activated
   * for the window or context that has 'aContextDOMId' node id
   *
   * @param string aContextDOMId
   * @return void
   */
  registerActiveContext: function HS_registerActiveContext(aContextDOMId)
  {
    this.activatedContexts.push(aContextDOMId);
  },

  /**
   * Firefox-specific current tab getter
   *
   * @returns nsIDOMWindow
   */
  currentContext: function HS_currentContext() {
    return this.mixins.getCurrentContext();
  },

  /**
   * Tell the HUDService that a HeadsUpDisplay should be deactivated
   *
   * @param string aContextDOMId
   * @return void
   */
  unregisterActiveContext: function HS_deregisterActiveContext(aContextDOMId)
  {
    var domId = aContextDOMId.split("_")[1];
    var idx = this.activatedContexts.indexOf(domId);
    if (idx > -1) {
      this.activatedContexts.splice(idx, 1);
    }
  },

  /**
   * Tells callers that a HeadsUpDisplay can be activated for the context
   *
   * @param string aContextDOMId
   * @return boolean
   */
  canActivateContext: function HS_canActivateContext(aContextDOMId)
  {
    var domId = aContextDOMId.split("_")[1];
    for (var idx in this.activatedContexts) {
      if (this.activatedContexts[idx] == domId){
        return true;
      }
    }
    return false;
  },

  /**
   * Activate a HeadsUpDisplay for the current window
   *
   * @param nsIDOMWindow aContext
   * @returns void
   */
  activateHUDForContext: function HS_activateHUDForContext(aContext)
  {
    var window = aContext.linkedBrowser.contentWindow;
    var id = aContext.linkedBrowser.parentNode.getAttribute("id");
    this.registerActiveContext(id);
    HUDService.windowInitializer(window);
  },

  /**
   * Deactivate a HeadsUpDisplay for the current window
   *
   * @param nsIDOMWindow aContext
   * @returns void
   */
  deactivateHUDForContext: function HS_deactivateHUDForContext(aContext)
  {
    var gBrowser = HUDService.currentContext().gBrowser;
    var window = aContext.linkedBrowser.contentWindow;
    var browser = gBrowser.getBrowserForDocument(window.top.document);
    var tabId = gBrowser.getNotificationBox(browser).getAttribute("id");
    var hudId = "hud_" + tabId;
    var displayNode = this.getHeadsUpDisplay(hudId);

    this.unregisterActiveContext(hudId);
    this.unregisterDisplay(hudId);
    window.wrappedJSObject.console = null;

  },

  /**
   * Clear the specified HeadsUpDisplay
   *
   * @param string aId
   * @returns void
   */
  clearDisplay: function HS_clearDisplay(aId)
  {
    var displayNode = this.getOutputNodeById(aId);
    var outputNode = displayNode.querySelectorAll(".hud-output-node")[0];

    while (outputNode.firstChild) {
      outputNode.removeChild(outputNode.firstChild);
    }

    outputNode.lastTimestamp = 0;
  },

  /**
   * get a unique ID from the sequence generator
   *
   * @returns integer
   */
  sequenceId: function HS_sequencerId()
  {
    if (!this.sequencer) {
      this.sequencer = this.createSequencer(-1);
    }
    return this.sequencer.next();
  },

  /**
   * get the default filter prefs
   *
   * @param string aHUDId
   * @returns JS Object
   */
  getDefaultFilterPrefs: function HS_getDefaultFilterPrefs(aHUDId) {
    return this.filterPrefs[aHUDId];
  },

  /**
   * get the current filter prefs
   *
   * @param string aHUDId
   * @returns JS Object
   */
  getFilterPrefs: function HS_getFilterPrefs(aHUDId) {
    return this.filterPrefs[aHUDId];
  },

  /**
   * get the filter state for a specific toggle button on a heads up display
   *
   * @param string aHUDId
   * @param string aToggleType
   * @returns boolean
   */
  getFilterState: function HS_getFilterState(aHUDId, aToggleType)
  {
    if (!aHUDId) {
      return false;
    }
    try {
      var bool = this.filterPrefs[aHUDId][aToggleType];
      return bool;
    }
    catch (ex) {
      return false;
    }
  },

  /**
   * set the filter state for a specific toggle button on a heads up display
   *
   * @param string aHUDId
   * @param string aToggleType
   * @param boolean aState
   * @returns void
   */
  setFilterState: function HS_setFilterState(aHUDId, aToggleType, aState)
  {
    this.filterPrefs[aHUDId][aToggleType] = aState;
    this.adjustVisibilityForMessageType(aHUDId, aToggleType, aState);
  },

  /**
   * Temporarily lifts the subtree rooted at the given node out of the DOM for
   * the duration of the supplied callback. This allows DOM mutations performed
   * inside the callback to avoid triggering reflows.
   *
   * @param nsIDOMNode aNode
   *        The node to remove from the tree.
   * @param function aCallback
   *        The callback, which should take no parameters. The return value of
   *        the callback, if any, is ignored.
   * @returns void
   */
  liftNode: function(aNode, aCallback) {
    let parentNode = aNode.parentNode;
    let siblingNode = aNode.nextSibling;
    parentNode.removeChild(aNode);
    aCallback();
    parentNode.insertBefore(aNode, siblingNode);
  },

  /**
   * Turns the display of log nodes on and off appropriately to reflect the
   * adjustment of the message type filter named by @aMessageType.
   *
   * @param string aHUDId
   *        The ID of the HUD to alter.
   * @param string aMessageType
   *        The message type being filtered ("network", "css", etc.)
   * @param boolean aState
   *        True if the filter named by @aMessageType is being turned on; false
   *        otherwise.
   * @returns void
   */
  adjustVisibilityForMessageType:
  function HS_adjustVisibilityForMessageType(aHUDId, aMessageType, aState)
  {
    let displayNode = this.getOutputNodeById(aHUDId);
    let outputNode = displayNode.querySelector(".hud-output-node");
    let doc = outputNode.ownerDocument;

    this.liftNode(outputNode, function() {
      let xpath = ".//*[contains(@class, 'hud-msg-node') and " +
        "contains(@class, 'hud-" + aMessageType + "')]";
      let result = doc.evaluate(xpath, outputNode, null,
        Ci.nsIDOMXPathResult.UNORDERED_NODE_SNAPSHOT_TYPE, null);
      for (let i = 0; i < result.snapshotLength; i++) {
        if (aState) {
          result.snapshotItem(i).classList.remove("hud-filtered-by-type");
        } else {
          result.snapshotItem(i).classList.add("hud-filtered-by-type");
        }
      }
    });
  },

  /**
   * Returns the source code of the XPath contains() function necessary to
   * match the given query string.
   *
   * @param string The query string to convert.
   * @returns string
   */
  buildXPathFunctionForString: function HS_buildXPathFunctionForString(aStr)
  {
    let words = aStr.split(/\s+/), results = [];
    for (let i = 0; i < words.length; i++) {
      let word = words[i];
      if (word === "") {
        continue;
      }

      let result;
      if (word.indexOf('"') === -1) {
        result = '"' + word + '"';
      }
      else if (word.indexOf("'") === -1) {
        result = "'" + word + "'";
      }
      else {
        result = 'concat("' + word.replace(/"/g, "\", '\"', \"") + '")';
      }

      results.push("contains(., " + result + ")");
    }

    return (results.length === 0) ? "true()" : results.join(" and ");
  },

  /**
   * Turns the display of log nodes on and off appropriately to reflect the
   * adjustment of the search string.
   *
   * @param string aHUDId
   *        The ID of the HUD to alter.
   * @param string aSearchString
   *        The new search string.
   * @returns void
   */
  adjustVisibilityOnSearchStringChange:
  function HS_adjustVisibilityOnSearchStringChange(aHUDId, aSearchString)
  {
    let fn = this.buildXPathFunctionForString(aSearchString);
    let displayNode = this.getOutputNodeById(aHUDId);
    let outputNode = displayNode.querySelector(".hud-output-node");
    let doc = outputNode.ownerDocument;
    this.liftNode(outputNode, function() {
      let xpath = './/*[contains(@class, "hud-msg-node") and ' +
        'not(contains(@class, "hud-filtered-by-string")) and not(' + fn + ')]';
      let result = doc.evaluate(xpath, outputNode, null,
        Ci.nsIDOMXPathResult.UNORDERED_NODE_SNAPSHOT_TYPE, null);
      for (let i = 0; i < result.snapshotLength; i++) {
        result.snapshotItem(i).classList.add("hud-filtered-by-string");
      }

      xpath = './/*[contains(@class, "hud-msg-node") and contains(@class, ' +
        '"hud-filtered-by-string") and ' + fn + ']';
      result = doc.evaluate(xpath, outputNode, null,
        Ci.nsIDOMXPathResult.UNORDERED_NODE_SNAPSHOT_TYPE, null);
      for (let i = 0; i < result.snapshotLength; i++) {
        result.snapshotItem(i).classList.remove("hud-filtered-by-string");
      }
    });
  },

  /**
   * Makes a newly-inserted node invisible if the user has filtered it out.
   *
   * @param string aHUDId
   *        The ID of the HUD to alter.
   * @param nsIDOMNode aNewNode
   *        The newly-inserted console message.
   * @returns void
   */
  adjustVisibilityForNewlyInsertedNode:
  function HS_adjustVisibilityForNewlyInsertedNode(aHUDId, aNewNode) {
    // Filter on the search string.
    let searchString = this.getFilterStringByHUDId(aHUDId);
    let xpath = ".[" + this.buildXPathFunctionForString(searchString) + "]";
    let doc = aNewNode.ownerDocument;
    let result = doc.evaluate(xpath, aNewNode, null,
      Ci.nsIDOMXPathResult.UNORDERED_NODE_SNAPSHOT_TYPE, null);
    if (result.snapshotLength === 0) {
      // The string filter didn't match, so the node is filtered.
      aNewNode.classList.add("hud-filtered-by-string");
    }

    // Filter by the message type.
    let classes = aNewNode.classList;
    let msgType = null;
    for (let i = 0; i < classes.length; i++) {
      let klass = classes.item(i);
      if (klass !== "hud-msg-node" && klass.indexOf("hud-") === 0) {
        msgType = klass.substring(4);   // Strip off "hud-".
        break;
      }
    }
    if (msgType !== null && !this.getFilterState(aHUDId, msgType)) {
      // The node is filtered by type.
      aNewNode.classList.add("hud-filtered-by-type");
    }
  },

  /**
   * Keeps a weak reference for each HeadsUpDisplay that is created
   *
   */
  hudWeakReferences: {},

  /**
   * Register a weak reference of each HeadsUpDisplay that is created
   *
   * @param object aHUDRef
   * @param string aHUDId
   * @returns void
   */
  registerHUDWeakReference:
  function HS_registerHUDWeakReference(aHUDRef, aHUDId)
  {
    this.hudWeakReferences[aHUDId] = aHUDRef;
  },

  /**
   * Deletes a HeadsUpDisplay object from memory
   *
   * @param string aHUDId
   * @returns void
   */
  deleteHeadsUpDisplay: function HS_deleteHeadsUpDisplay(aHUDId)
  {
    delete this.hudWeakReferences[aHUDId].get();
  },

  /**
   * Register a new Heads Up Display
   *
   * @param string aHUDId
   * @param nsIDOMWindow aContentWindow
   * @returns void
   */
  registerDisplay: function HS_registerDisplay(aHUDId, aContentWindow)
  {
    // register a display DOM node Id and HUD uriSpec with the service

    if (!aHUDId || !aContentWindow){
      throw new Error(ERRORS.MISSING_ARGS);
    }
    var URISpec = aContentWindow.document.location.href
    this.filterPrefs[aHUDId] = this.defaultFilterPrefs;
    this.displayRegistry[aHUDId] = URISpec;
    this._headsUpDisplays[aHUDId] = { id: aHUDId, };
    this.registerActiveContext(aHUDId);
    // init storage objects:
    this.storage.createDisplay(aHUDId);

    var huds = this.uriRegistry[URISpec];
    var foundHUDId = false;

    if (huds) {
      var len = huds.length;
      for (var i = 0; i < len; i++) {
        if (huds[i] == aHUDId) {
          foundHUDId = true;
          break;
        }
      }
      if (!foundHUDId) {
        this.uriRegistry[URISpec].push(aHUDId);
      }
    }
    else {
      this.uriRegistry[URISpec] = [aHUDId];
    }

    var windows = this.windowRegistry[aHUDId];
    if (!windows) {
      this.windowRegistry[aHUDId] = [aContentWindow];
    }
    else {
      windows.push(aContentWindow);
    }
  },

  /**
   * When a display is being destroyed, unregister it first
   *
   * @param string aId
   * @returns void
   */
  unregisterDisplay: function HS_unregisterDisplay(aId)
  {
    // remove HUD DOM node and
    // remove display references from local registries get the outputNode
    var outputNode = this.mixins.getOutputNodeById(aId);
    var parent = outputNode.parentNode;
    var splitters = parent.querySelectorAll("splitter");
    var len = splitters.length;
    for (var i = 0; i < len; i++) {
      if (splitters[i].getAttribute("class") == "hud-splitter") {
        splitters[i].parentNode.removeChild(splitters[i]);
        break;
      }
    }
    // remove the DOM Nodes
    parent.removeChild(outputNode);
    // remove our record of the DOM Nodes from the registry
    delete this._headsUpDisplays[aId];
    // remove the HeadsUpDisplay object from memory
    this.deleteHeadsUpDisplay(aId);
    // remove the related storage object
    this.storage.removeDisplay(aId);
    // remove the related window objects
    delete this.windowRegistry[aId];

    let displays = this.displays();

    var uri  = this.displayRegistry[aId];
    var specHudArr = this.uriRegistry[uri];

    for (var i = 0; i < specHudArr.length; i++) {
      if (specHudArr[i] == aId) {
        specHudArr.splice(i, 1);
      }
    }
    delete displays[aId];
    delete this.displayRegistry[aId];
  },

  /**
   * Shutdown all HeadsUpDisplays on xpcom-shutdown
   *
   * @returns void
   */
  shutdown: function HS_shutdown()
  {
    for (var displayId in this._headsUpDisplays) {
      this.unregisterDisplay(displayId);
    }
    // delete the storage as it holds onto channels
    delete this.storage;

     var xulWindow = aContentWindow.QueryInterface(Ci.nsIInterfaceRequestor)
       .getInterface(Ci.nsIWebNavigation)
       .QueryInterface(Ci.nsIDocShellTreeItem)
       .rootTreeItem
       .QueryInterface(Ci.nsIInterfaceRequestor)
       .getInterface(Ci.nsIDOMWindow);

    xulWindow = XPCNativeWrapper.unwrap(xulWindow);
    var gBrowser = xulWindow.gBrowser;
    gBrowser.tabContainer.removeEventListener("TabClose", this.onTabClose, false);
  },

  /**
   * get the nsIDOMNode outputNode via a nsIURI.spec
   *
   * @param string aURISpec
   * @returns nsIDOMNode
   */
  getDisplayByURISpec: function HS_getDisplayByURISpec(aURISpec)
  {
    // TODO: what about data:uris? see bug 568626
    var hudIds = this.uriRegistry[aURISpec];
    if (hudIds.length == 1) {
      // only one HUD connected to this URISpec
      return this.getHeadsUpDisplay(hudIds[0]);
    }
    else {
      // TODO: how to determine more fully the origination of this activity?
      // see bug 567165
      return this.getHeadsUpDisplay(hudIds[0]);
    }
  },

  /**
   * Returns the hudId that is corresponding to the hud activated for the
   * passed aContentWindow. If there is no matching hudId null is returned.
   *
   * @param nsIDOMWindow aContentWindow
   * @returns string or null
   */
  getHudIdByWindow: function HS_getHudIdByWindow(aContentWindow)
  {
    for (let hudId in this.windowRegistry) {
      if (this.windowRegistry[hudId] &&
          this.windowRegistry[hudId].indexOf(aContentWindow) != -1) {
        return hudId;
      }
    }
    return null;
  },

  /**
   * Gets HUD DOM Node
   * @param string id
   *        The Heads Up Display DOM Id
   * @returns nsIDOMNode
   */
  getHeadsUpDisplay: function HS_getHeadsUpDisplay(aId)
  {
    return this.mixins.getOutputNodeById(aId);
  },

  /**
   * gets the nsIDOMNode outputNode by ID via the gecko app mixins
   *
   * @param string aId
   * @returns nsIDOMNode
   */
  getOutputNodeById: function HS_getOutputNodeById(aId)
  {
    return this.mixins.getOutputNodeById(aId);
  },

  /**
   * Gets an object that contains active DOM Node Ids for all Heads Up Displays
   *
   * @returns object
   */
  displays: function HS_displays() {
    return this._headsUpDisplays;
  },

  /**
   * Get an array of HUDIds that match a uri.spec
   *
   * @param string aURISpec
   * @returns array
   */
  getHUDIdsForURISpec: function HS_getHUDIdsForURISpec(aURISpec)
  {
    if (this.uriRegistry[aURISpec]) {
      return this.uriRegistry[aURISpec];
    }
    return [];
  },

  /**
   * Gets an array that contains active DOM Node Ids for all HUDs
   * @returns array
   */
  displaysIndex: function HS_displaysIndex()
  {
    var props = [];
    for (var prop in this._headsUpDisplays) {
      props.push(prop);
    }
    return props;
  },

  /**
   * get the current filter string for the HeadsUpDisplay
   *
   * @param string aHUDId
   * @returns string
   */
  getFilterStringByHUDId: function HS_getFilterStringbyHUDId(aHUDId) {
    var hud = this.getHeadsUpDisplay(aHUDId);
    var filterStr = hud.querySelectorAll(".hud-filter-box")[0].value;
    return filterStr;
  },

  /**
   * Update the filter text in the internal tracking object for all
   * filter strings
   *
   * @param nsIDOMNode aTextBoxNode
   * @returns void
   */
  updateFilterText: function HS_updateFiltertext(aTextBoxNode)
  {
    var hudId = aTextBoxNode.getAttribute("hudId");
    this.adjustVisibilityOnSearchStringChange(hudId, aTextBoxNode.value);
  },

  /**
   * Logs a HUD-generated console message
   * @param object aMessage
   *        The message to log, which is a JS object, this is the
   *        "raw" log message
   * @param nsIDOMNode aConsoleNode
   *        The output DOM node to log the messageNode to
   * @param nsIDOMNode aMessageNode
   *        The message DOM Node that will be appended to aConsoleNode
   * @returns void
   */
  logHUDMessage: function HS_logHUDMessage(aMessage,
                                           aConsoleNode,
                                           aMessageNode)
  {
    if (!aMessage) {
      throw new Error(ERRORS.MISSING_ARGS);
    }

    let lastGroupNode = this.appendGroupIfNecessary(aConsoleNode,
                                                    aMessage.timestamp);

    lastGroupNode.appendChild(aMessageNode);
    ConsoleUtils.scrollToVisible(aMessageNode);

    // store this message in the storage module:
    this.storage.recordEntry(aMessage.hudId, aMessage);
  },

  /**
   * logs a message to the Heads Up Display that originates
   * in the nsIConsoleService
   *
   * @param nsIConsoleMessage aMessage
   * @param nsIDOMNode aConsoleNode
   * @param nsIDOMNode aMessageNode
   * @returns void
   */
  logConsoleMessage: function HS_logConsoleMessage(aMessage,
                                                   aConsoleNode,
                                                   aMessageNode)
  {
    aConsoleNode.appendChild(aMessageNode);
    ConsoleUtils.scrollToVisible(aMessageNode);

    // store this message in the storage module:
    this.storage.recordEntry(aMessage.hudId, aMessage);
  },

  /**
   * Logs a Message.
   * @param aMessage
   *        The message to log, which is a JS object, this is the
   *        "raw" log message
   * @param aConsoleNode
   *        The output DOM node to log the messageNode to
   * @param The message DOM Node that will be appended to aConsoleNode
   * @returns void
   */
  logMessage: function HS_logMessage(aMessage, aConsoleNode, aMessageNode)
  {
    if (!aMessage) {
      throw new Error(ERRORS.MISSING_ARGS);
    }

    var hud = this.getHeadsUpDisplay(aMessage.hudId);
    switch (aMessage.origin) {
      case "network":
      case "HUDConsole":
      case "console-listener":
        this.logHUDMessage(aMessage, aConsoleNode, aMessageNode);
        break;
      default:
        // noop
        break;
    }
  },

  /**
   * report consoleMessages recieved via the HUDConsoleObserver service
   * @param nsIConsoleMessage aConsoleMessage
   * @returns void
   */
  reportConsoleServiceMessage:
  function HS_reportConsoleServiceMessage(aConsoleMessage)
  {
    this.logActivity("console-listener", null, aConsoleMessage);
  },

  /**
   * report scriptErrors recieved via the HUDConsoleObserver service
   * @param nsIScriptError aScriptError
   * @returns void
   */
  reportConsoleServiceContentScriptError:
  function HS_reportConsoleServiceContentScriptError(aScriptError)
  {
    try {
      var uri = Services.io.newURI(aScriptError.sourceName, null, null);
    }
    catch(ex) {
      var uri = { spec: "" };
    }
    this.logActivity("console-listener", uri, aScriptError);
  },

  /**
   * generates an nsIScriptError
   *
   * @param object aMessage
   * @param integer flag
   * @returns nsIScriptError
   */
  generateConsoleMessage:
  function HS_generateConsoleMessage(aMessage, flag)
  {
    let message = scriptError; // nsIScriptError
    message.init(aMessage.message, null, null, 0, 0, flag,
                 "HUDConsole");
    return message;
  },

  /**
   * Register a Gecko app's specialized ApplicationHooks object
   *
   * @returns void or throws "UNSUPPORTED APPLICATION" error
   */
  registerApplicationHooks:
  function HS_registerApplications(aAppName, aHooksObject)
  {
    switch(aAppName) {
      case "FIREFOX":
        this.applicationHooks = aHooksObject;
        return;
      default:
        throw new Error("MOZ APPLICATION UNSUPPORTED");
    }
  },

  /**
   * Registry of ApplicationHooks used by specified Gecko Apps
   *
   * @returns Specific Gecko 'ApplicationHooks' Object/Mixin
   */
  applicationHooks: null,

  getChromeWindowFromContentWindow:
  function HS_getChromeWindowFromContentWindow(aContentWindow)
  {
    if (!aContentWindow) {
      throw new Error("Cannot get contentWindow via nsILoadContext");
    }
    var win = aContentWindow.QueryInterface(Ci.nsIDOMWindow)
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIWebNavigation)
      .QueryInterface(Ci.nsIDocShellTreeItem)
      .rootTreeItem
      .QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIDOMWindow)
      .QueryInterface(Ci.nsIDOMChromeWindow);
    return win;
  },

  /**
   * Begin observing HTTP traffic that we care about,
   * namely traffic that originates inside any context that a Heads Up Display
   * is active for.
   */
  startHTTPObservation: function HS_httpObserverFactory()
  {
    // creates an observer for http traffic
    var self = this;
    var httpObserver = {
      observeActivity :
      function (aChannel, aActivityType, aActivitySubtype,
                aTimestamp, aExtraSizeData, aExtraStringData)
      {
        var loadGroup;
        if (aActivityType ==
            activityDistributor.ACTIVITY_TYPE_HTTP_TRANSACTION) {

          aChannel = aChannel.QueryInterface(Ci.nsIHttpChannel);

          var transCodes = this.httpTransactionCodes;

          if (aActivitySubtype ==
              activityDistributor.ACTIVITY_SUBTYPE_REQUEST_HEADER ) {
            // Try to get the source window of the request.
            let win = NetworkHelper.getWindowForRequest(aChannel);
            if (!win) {
              return;
            }

            // Try to get the hudId that is associated to the window.
            let hudId = self.getHudIdByWindow(win);
            if (!hudId) {
              return;
            }

            var httpActivity = {
              channel: aChannel,
              type: aActivityType,
              subType: aActivitySubtype,
              timestamp: aTimestamp,
              extraSizeData: aExtraSizeData,
              extraStringData: aExtraStringData,
              stage: transCodes[aActivitySubtype],
              hudId: hudId
            };

            // create a unique ID to track this transaction and be able to
            // update the logged node with subsequent http transactions
            httpActivity.httpId = self.sequenceId();
            let loggedNode =
              self.logActivity("network", aChannel.URI, httpActivity);
            self.httpTransactions[aChannel] =
              new Number(httpActivity.httpId);
          }
        }
      },

      httpTransactionCodes: {
        0x5001: "REQUEST_HEADER",
        0x5002: "REQUEST_BODY_SENT",
        0x5003: "RESPONSE_START",
        0x5004: "RESPONSE_HEADER",
        0x5005: "RESPONSE_COMPLETE",
        0x5006: "TRANSACTION_CLOSE",
      }
    };

    activityDistributor.addObserver(httpObserver);
  },

  // keep tracked of trasactions where the request header was logged
  // update logged transactions thereafter.
  httpTransactions: {},

  /**
   * Logs network activity
   *
   * @param nsIURI aURI
   * @param object aActivityObject
   * @returns void
   */
  logNetActivity: function HS_logNetActivity(aType, aURI, aActivityObject)
  {
    var outputNode, hudId;
    try {
      hudId = aActivityObject.hudId;
      outputNode = this.getHeadsUpDisplay(hudId).
                                  querySelector(".hud-output-node");

      // get an id to attach to the dom node for lookup of node
      // when updating the log entry with additional http transactions
      var domId = "hud-log-node-" + this.sequenceId();

      var message = { logLevel: aType,
                      activityObj: aActivityObject,
                      hudId: hudId,
                      origin: "network",
                      domId: domId,
                    };
      var msgType = this.getStr("typeNetwork");
      var msg = msgType + " " +
        aActivityObject.channel.requestMethod +
        " " +
        aURI.spec;
      message.message = msg;
      var messageObject =
      this.messageFactory(message, aType, outputNode, aActivityObject);
      this.logMessage(messageObject.messageObject, outputNode, messageObject.messageNode);
    }
    catch (ex) {
      Cu.reportError(ex);
    }
  },

  /**
   * Logs console listener activity
   *
   * @param nsIURI aURI
   * @param object aActivityObject
   * @returns void
   */
  logConsoleActivity: function HS_logConsoleActivity(aURI, aActivityObject)
  {
    var displayNode, outputNode, hudId;
    try {
        var hudIds = this.uriRegistry[aURI.spec];
        hudId = hudIds[0];
    }
    catch (ex) {
      // TODO: uri spec is not tracked becasue the net request is
      // using a different loadGroup
      // see bug 568034
      if (!displayNode) {
        return;
      }
    }

    var _msgLogLevel = this.scriptMsgLogLevel[aActivityObject.flags];
    var msgLogLevel = this.getStr(_msgLogLevel);

    var logLevel = "warn";

    if (aActivityObject.flags in this.scriptErrorFlags) {
      logLevel = this.scriptErrorFlags[aActivityObject.flags];
    }

    // in this case, the "activity object" is the
    // nsIScriptError or nsIConsoleMessage
    var message = {
      activity: aActivityObject,
      origin: "console-listener",
      hudId: hudId,
    };

    var lineColSubs = [aActivityObject.columnNumber,
                       aActivityObject.lineNumber];
    var lineCol = this.getFormatStr("errLineCol", lineColSubs);

    var errFileSubs = [aActivityObject.sourceName];
    var errFile = this.getFormatStr("errFile", errFileSubs);

    var msgCategory = this.getStr("msgCategory");

    message.logLevel = logLevel;
    message.level = logLevel;

    message.message = msgLogLevel + " " +
                      aActivityObject.errorMessage + " " +
                      errFile + " " +
                      lineCol + " " +
                      msgCategory + " " + aActivityObject.category;

    displayNode = this.getHeadsUpDisplay(hudId);
    outputNode = displayNode.querySelectorAll(".hud-output-node")[0];

    var messageObject =
    this.messageFactory(message, message.level, outputNode, aActivityObject);

    this.logMessage(messageObject.messageObject, outputNode, messageObject.messageNode);
  },

  /**
   * Parse log messages for origin or listener type
   * Get the correct outputNode if it exists
   * Finally, call logMessage to write this message to
   * storage and optionally, a DOM output node
   *
   * @param string aType
   * @param nsIURI aURI
   * @param object (or nsIScriptError) aActivityObj
   * @returns void
   */
  logActivity: function HS_logActivity(aType, aURI, aActivityObject)
  {
    var displayNode, outputNode, hudId;

    if (aType == "network") {
      var result = this.logNetActivity(aType, aURI, aActivityObject);
    }
    else if (aType == "console-listener") {
      this.logConsoleActivity(aURI, aActivityObject);
    }
  },

  /**
   * Builds and appends a group to the console if enough time has passed since
   * the last message.
   *
   * @param nsIDOMNode aConsoleNode
   *        The DOM node that holds the output of the console (NB: not the HUD
   *        node itself).
   * @param number aTimestamp
   *        The timestamp of the newest message in milliseconds.
   * @returns nsIDOMNode
   *          The group into which the next message should be written.
   */
  appendGroupIfNecessary:
  function HS_appendGroupIfNecessary(aConsoleNode, aTimestamp)
  {
    let hudBox = aConsoleNode;
    while (hudBox != null && hudBox.getAttribute("class") !== "hud-box") {
      hudBox = hudBox.parentNode;
    }

    let lastTimestamp = hudBox.lastTimestamp;
    let delta = aTimestamp - lastTimestamp;
    hudBox.lastTimestamp = aTimestamp;
    if (delta < NEW_GROUP_DELAY) {
      // No new group needed. Return the most recently-added group, if there is
      // one.
      let lastGroupNode = aConsoleNode.querySelector(".hud-group:last-child");
      if (lastGroupNode != null) {
        return lastGroupNode;
      }
    }

    let chromeDocument = aConsoleNode.ownerDocument;
    let groupNode = chromeDocument.createElement("vbox");
    groupNode.setAttribute("class", "hud-group");

    let separatorNode = chromeDocument.createElement("separator");
    separatorNode.setAttribute("class", "groove hud-divider");
    separatorNode.setAttribute("orient", "horizontal");
    groupNode.appendChild(separatorNode);

    aConsoleNode.appendChild(groupNode);
    return groupNode;
  },

  /**
   * gets the DOM Node that maps back to what context/tab that
   * activity originated via the URI
   *
   * @param nsIURI aURI
   * @returns nsIDOMNode
   */
  getActivityOutputNode: function HS_getActivityOutputNode(aURI)
  {
    // determine which outputNode activity tied to aURI should be logged to.
    var display = this.getDisplayByURISpec(aURI.spec);
    if (display) {
      return this.getOutputNodeById(display);
    }
    else {
      throw new Error("Cannot get outputNode by hudId");
    }
  },

  /**
   * Wrapper method that generates a LogMessage object
   *
   * @param object aMessage
   * @param string aLevel
   * @param nsIDOMNode aOutputNode
   * @param object aActivityObject
   * @returns
   */
  messageFactory:
  function messageFactory(aMessage, aLevel, aOutputNode, aActivityObject)
  {
    // generate a LogMessage object
    return new LogMessage(aMessage, aLevel, aOutputNode,  aActivityObject);
  },

  /**
   * Initialize the JSTerm object to create a JS Workspace
   *
   * @param nsIDOMWindow aContext
   * @param nsIDOMNode aParentNode
   * @returns void
   */
  initializeJSTerm: function HS_initializeJSTerm(aContext, aParentNode)
  {
    // create Initial JS Workspace:
    var context = Cu.getWeakReference(aContext);
    var firefoxMixin = new JSTermFirefoxMixin(context, aParentNode);
    var jsTerm = new JSTerm(context, aParentNode, firefoxMixin);
    // TODO: injection of additional functionality needs re-thinking/api
    // see bug 559748
  },

  /**
   * Passed a HUDId, the corresponding window is returned
   *
   * @param string aHUDId
   * @returns nsIDOMWindow
   */
  getContentWindowFromHUDId: function HS_getContentWindowFromHUDId(aHUDId)
  {
    var hud = this.getHeadsUpDisplay(aHUDId);
    var nodes = hud.parentNode.childNodes;

    for (var i = 0; i < nodes.length; i++) {
      if (nodes[i].contentWindow) {
        return nodes[i].contentWindow;
      }
    }
    throw new Error("HS_getContentWindowFromHUD: Cannot get contentWindow");
  },

  /**
   * Creates a generator that always returns a unique number for use in the
   * indexes
   *
   * @returns Generator
   */
  createSequencer: function HS_createSequencer(aInt)
  {
    function sequencer(aInt)
    {
      while(1) {
        aInt++;
        yield aInt;
      }
    }
    return sequencer(aInt);
  },

  scriptErrorFlags: {
    0: "error",
    1: "warn",
    2: "exception",
    4: "strict"
  },

  /**
   * replacement strings (L10N)
   */
  scriptMsgLogLevel: {
    0: "typeError",
    1: "typeWarning",
    2: "typeException",
    4: "typeStrict",
  },

  /**
   * onTabClose event handler function
   *
   * @param aEvent
   * @returns void
   */
  onTabClose: function HS_onTabClose(aEvent)
  {
    var browser = aEvent.target;
    var tabId = gBrowser.getNotificationBox(browser).getAttribute("id");
    var hudId = "hud_" + tabId;
    this.unregisterDisplay(hudId);
  },

  /**
   * windowInitializer - checks what Gecko app is running and inits the HUD
   *
   * @param nsIDOMWindow aContentWindow
   * @returns void
   */
  windowInitializer: function HS_WindowInitalizer(aContentWindow)
  {
    var xulWindow = aContentWindow.QueryInterface(Ci.nsIInterfaceRequestor)
      .getInterface(Ci.nsIWebNavigation)
                      .QueryInterface(Ci.nsIDocShell)
                      .chromeEventHandler.ownerDocument.defaultView;

    let xulWindow = XPCNativeWrapper.unwrap(xulWindow);

    let docElem = xulWindow.document.documentElement;
    if (!docElem || docElem.getAttribute("windowtype") != "navigator:browser" ||
        !xulWindow.gBrowser) {
      // Do not do anything unless we have a browser window.
      // This may be a view-source window or other type of non-browser window.
      return;
    }

    if (aContentWindow.document.location.href == "about:blank" &&
        HUDWindowObserver.initialConsoleCreated == false) {
      // TODO: need to make this work with about:blank in the future
      // see bug 568661
      return;
    }

    let gBrowser = xulWindow.gBrowser;


    var container = gBrowser.tabContainer;
    container.addEventListener("TabClose", this.onTabClose, false);

    if (gBrowser && !HUDWindowObserver.initialConsoleCreated) {
      HUDWindowObserver.initialConsoleCreated = true;
    }

    let _browser =
      gBrowser.getBrowserForDocument(aContentWindow.document.wrappedJSObject);
    let nBox = gBrowser.getNotificationBox(_browser);
    let nBoxId = nBox.getAttribute("id");
    let hudId = "hud_" + nBoxId;

    if (!this.canActivateContext(hudId)) {
      return;
    }

    this.registerDisplay(hudId, aContentWindow);

    // check if aContentWindow has a console Object
    let _console = aContentWindow.wrappedJSObject.console;
    if (!_console) {
      // no console exists. does the HUD exist?
      let hudNode;
      let childNodes = nBox.childNodes;

      for (var i = 0; i < childNodes.length; i++) {
        let id = childNodes[i].getAttribute("id");
        if (id.split("_")[0] == "hud") {
          hudNode = childNodes[i];
          break;
        }
      }

      if (!hudNode) {
        // get nBox object and call new HUD
        let config = { parentNode: nBox,
                       contentWindow: aContentWindow
                     };

        let _hud = new HeadsUpDisplay(config);

        let hudWeakRef = Cu.getWeakReference(_hud);
        HUDService.registerHUDWeakReference(hudWeakRef, hudId);
      }
      else {
        // only need to attach a console object to the window object
        let config = { hudNode: hudNode,
                       consoleOnly: true,
                       contentWindow: aContentWindow
                     };

        let _hud = new HeadsUpDisplay(config);

        let hudWeakRef = Cu.getWeakReference(_hud);
        HUDService.registerHUDWeakReference(hudWeakRef, hudId);

        aContentWindow.wrappedJSObject.console = _hud.console;
      }
    }
    // capture JS Errors
    this.setOnErrorHandler(aContentWindow);
  }
};

//////////////////////////////////////////////////////////////////////////
// HeadsUpDisplay
//////////////////////////////////////////////////////////////////////////

/*
 * HeadsUpDisplay is an interactive console initialized *per tab*  that
 * displays console log data as well as provides an interactive terminal to
 * manipulate the current tab's document content.
 * */
function HeadsUpDisplay(aConfig)
{
  // sample config: { parentNode: aDOMNode,
  //                  // or
  //                  parentNodeId: "myHUDParent123",
  //
  //                  placement: "appendChild"
  //                  // or
  //                  placement: "insertBefore",
  //                  placementChildNodeIndex: 0,
  //                }
  //
  // or, just create a new console - as there is already a HUD in place
  // config: { hudNode: existingHUDDOMNode,
  //           consoleOnly: true,
  //           contentWindow: aWindow
  //         }

  if (aConfig.consoleOnly) {
    this.HUDBox = aConfig.hudNode;
    this.parentNode = aConfig.hudNode.parentNode;
    this.notificationBox = this.parentNode;
    this.contentWindow = aConfig.contentWindow;
    this.uriSpec = aConfig.contentWindow.location.href;
    this.reattachConsole();
    this.HUDBox.querySelectorAll(".jsterm-input-node")[0].focus();
    return;
  }

  this.HUDBox = null;

  if (aConfig.parentNode) {
    // TODO: need to replace these DOM calls with internal functions
    // that operate on each application's node structure
    // better yet, we keep these functions in a "bridgeModule" or the HUDService
    // to keep a registry of nodeGetters for each application
    // see bug 568647
    this.parentNode = aConfig.parentNode;
    this.notificationBox = aConfig.parentNode;
    this.chromeDocument = aConfig.parentNode.ownerDocument;
    this.contentWindow = aConfig.contentWindow;
    this.uriSpec = aConfig.contentWindow.location.href;
    this.hudId = "hud_" + aConfig.parentNode.getAttribute("id");
  }
  else {
    // parentNodeId is the node's id where we attach the HUD
    // TODO: is the "navigator:browser" below used in all Gecko Apps?
    // see bug 568647
    let windowEnum = Services.wm.getEnumerator("navigator:browser");
    let parentNode;
    let contentDocument;
    let contentWindow;
    let chromeDocument;

    // TODO: the following  part is still very Firefox specific
    // see bug 568647

    while (windowEnum.hasMoreElements()) {
      let window = windowEnum.getNext();
      try {
        let gBrowser = window.gBrowser;
        let _browsers = gBrowser.browsers;
        let browserLen = _browsers.length;

        for (var i = 0; i < browserLen; i++) {
          var _notificationBox = gBrowser.getNotificationBox(_browsers[i]);
          this.notificationBox = _notificationBox;

          if (_notificationBox.getAttribute("id") == aConfig.parentNodeId) {
            this.parentNodeId = _notificationBox.getAttribute("id");
            this.hudId = "hud_" + this.parentNodeId;

            parentNode = _notificationBox;

            this.contentDocument =
              _notificationBox.childNodes[0].contentDocument;
            this.contentWindow =
              _notificationBox.childNodes[0].contentWindow;
            this.uriSpec = aConfig.contentWindow.location.href;

            this.chromeDocument =
              _notificationBox.ownerDocument;

            break;
          }
        }
      }
      catch (ex) {
        Cu.reportError(ex);
      }

      if (parentNode) {
        break;
      }
    }
    if (!parentNode) {
      throw new Error(this.ERRORS.PARENTNODE_NOT_FOUND);
    }
    this.parentNode = parentNode;
  }

  // create textNode Factory:
  this.textFactory = NodeFactory("text", "xul", this.chromeDocument);

  this.chromeWindow = HUDService.getChromeWindowFromContentWindow(this.contentWindow);

  // create a panel dynamically and attach to the parentNode
  let hudBox = this.createHUD();

  let splitter = this.chromeDocument.createElement("splitter");
  splitter.setAttribute("class", "hud-splitter");

  this.notificationBox.insertBefore(splitter,
                                    this.notificationBox.childNodes[1]);

  let console = this.createConsole();

  this.HUDBox.lastTimestamp = 0;

  this.contentWindow.wrappedJSObject.console = console;

  // create the JSTerm input element
  try {
    this.createConsoleInput(this.contentWindow, this.consoleWrap, this.outputNode);
    this.HUDBox.querySelectorAll(".jsterm-input-node")[0].focus();
  }
  catch (ex) {
    Cu.reportError(ex);
  }
}

HeadsUpDisplay.prototype = {
  /**
   * L10N shortcut function
   *
   * @param string aName
   * @returns string
   */
  getStr: function HUD_getStr(aName)
  {
    return stringBundle.GetStringFromName(aName);
  },

  /**
   * L10N shortcut function
   *
   * @param string aName
   * @param array aArray
   * @returns string
   */
  getFormatStr: function HUD_getFormatStr(aName, aArray)
  {
    return stringBundle.formatStringFromName(aName, aArray, aArray.length);
  },

  /**
   * The JSTerm object that contains the console's inputNode
   *
   */
  jsterm: null,

  /**
   * creates and attaches the console input node
   *
   * @param nsIDOMWindow aWindow
   * @returns void
   */
  createConsoleInput:
  function HUD_createConsoleInput(aWindow, aParentNode, aExistingConsole)
  {
    var context = Cu.getWeakReference(aWindow);

    if (appName() == "FIREFOX") {
      let outputCSSClassOverride = "hud-msg-node";
      let mixin = new JSTermFirefoxMixin(context, aParentNode, aExistingConsole, outputCSSClassOverride);
      this.jsterm = new JSTerm(context, aParentNode, mixin);
    }
    else {
      throw new Error("Unsupported Gecko Application");
    }
  },

  /**
   * Re-attaches a console when the contentWindow is recreated
   *
   * @returns void
   */
  reattachConsole: function HUD_reattachConsole()
  {
    this.hudId = this.HUDBox.getAttribute("id");

    this.outputNode = this.HUDBox.querySelectorAll(".hud-output-node")[0];

    this.chromeWindow = HUDService.
      getChromeWindowFromContentWindow(this.contentWindow);
    this.chromeDocument = this.HUDBox.ownerDocument;

    if (this.outputNode) {
      // createConsole
      this.createConsole();
    }
    else {
      throw new Error("Cannot get output node");
    }
  },

  /**
   * Shortcut to make XUL nodes
   *
   * @param string aTag
   * @returns nsIDOMNode
   */
  makeXULNode:
  function HUD_makeXULNode(aTag)
  {
    return this.chromeDocument.createElement(aTag);
  },

  /**
   * Build the UI of each HeadsUpDisplay
   *
   * @returns nsIDOMNode
   */
  makeHUDNodes: function HUD_makeHUDNodes()
  {
    let self = this;
    this.HUDBox = this.makeXULNode("vbox");
    this.HUDBox.setAttribute("id", this.hudId);
    this.HUDBox.setAttribute("class", "hud-box");

    var height = Math.ceil((this.contentWindow.innerHeight * .33)) + "px";
    var style = "height: " + height + ";";
    this.HUDBox.setAttribute("style", style);

    let outerWrap = this.makeXULNode("vbox");
    outerWrap.setAttribute("class", "hud-outer-wrapper");
    outerWrap.setAttribute("flex", "1");

    let consoleCommandSet = this.makeXULNode("commandset");
    outerWrap.appendChild(consoleCommandSet);

    let consoleWrap = this.makeXULNode("vbox");
    this.consoleWrap = consoleWrap;
    consoleWrap.setAttribute("class", "hud-console-wrapper");
    consoleWrap.setAttribute("flex", "1");

    this.outputNode = this.makeXULNode("scrollbox");
    this.outputNode.setAttribute("class", "hud-output-node");
    this.outputNode.setAttribute("flex", "1");
    this.outputNode.setAttribute("orient", "vertical");
    this.outputNode.setAttribute("context", this.hudId + "-output-contextmenu");

    this.outputNode.addEventListener("DOMNodeInserted", function(ev) {
      // DOMNodeInserted is also called when the output node is being *itself*
      // (re)inserted into the DOM (which happens during a search, for
      // example). For this reason, we need to ensure that we only check
      // message nodes.
      let node = ev.target;
      if (node.nodeType === node.ELEMENT_NODE &&
          node.classList.contains("hud-msg-node")) {
        HUDService.adjustVisibilityForNewlyInsertedNode(self.hudId, ev.target);
      }
    }, false);

    this.filterSpacer = this.makeXULNode("spacer");
    this.filterSpacer.setAttribute("flex", "1");

    this.filterBox = this.makeXULNode("textbox");
    this.filterBox.setAttribute("class", "compact hud-filter-box");
    this.filterBox.setAttribute("hudId", this.hudId);
    this.filterBox.setAttribute("placeholder", this.getStr("stringFilter"));
    this.filterBox.setAttribute("type", "search");

    this.setFilterTextBoxEvents();

    this.createConsoleMenu(this.consoleWrap);

    this.filterPrefs = HUDService.getDefaultFilterPrefs(this.hudId);

    let consoleFilterToolbar = this.makeFilterToolbar();
    consoleFilterToolbar.setAttribute("id", "viewGroup");
    this.consoleFilterToolbar = consoleFilterToolbar;
    consoleWrap.appendChild(consoleFilterToolbar);

    consoleWrap.appendChild(this.outputNode);

    outerWrap.appendChild(consoleWrap);

    this.HUDBox.lastTimestamp = 0;

    this.jsTermParentNode = outerWrap;
    this.HUDBox.appendChild(outerWrap);
    return this.HUDBox;
  },


  /**
   * sets the click events for all binary toggle filter buttons
   *
   * @returns void
   */
  setFilterTextBoxEvents: function HUD_setFilterTextBoxEvents()
  {
    var filterBox = this.filterBox;
    function onChange()
    {
      // To improve responsiveness, we let the user finish typing before we
      // perform the search.

      if (this.timer == null) {
        let timerClass = Cc["@mozilla.org/timer;1"];
        this.timer = timerClass.createInstance(Ci.nsITimer);
      } else {
        this.timer.cancel();
      }

      let timerEvent = {
        notify: function setFilterTextBoxEvents_timerEvent_notify() {
          HUDService.updateFilterText(filterBox);
        }
      };

      this.timer.initWithCallback(timerEvent, SEARCH_DELAY,
        Ci.nsITimer.TYPE_ONE_SHOT);
    }

    filterBox.addEventListener("command", onChange, false);
    filterBox.addEventListener("input", onChange, false);
  },

  /**
   * Make the filter toolbar where we can toggle logging filters
   *
   * @returns nsIDOMNode
   */
  makeFilterToolbar: function HUD_makeFilterToolbar()
  {
    let buttons = ["Network", "CSSParser", "Exception", "Error",
                   "Info", "Warn", "Log",];

    const pageButtons = [
      { prefKey: "network", name: "PageNet" },
      { prefKey: "cssparser", name: "PageCSS" },
      { prefKey: "exception", name: "PageJS" }
    ];
    const consoleButtons = [
      { prefKey: "error", name: "ConsoleErrors" },
      { prefKey: "warn", name: "ConsoleWarnings" },
      { prefKey: "info", name: "ConsoleInfo" },
      { prefKey: "log", name: "ConsoleLog" }
    ];

    let toolbar = this.makeXULNode("toolbar");
    toolbar.setAttribute("class", "hud-console-filter-toolbar");
    toolbar.setAttribute("mode", "text");

    let pageCategoryTitle = this.getStr("categoryPage");
    this.addButtonCategory(toolbar, pageCategoryTitle, pageButtons);

    let separator = this.makeXULNode("separator");
    separator.setAttribute("orient", "vertical");
    toolbar.appendChild(separator);

    let consoleCategoryTitle = this.getStr("categoryConsole");
    this.addButtonCategory(toolbar, consoleCategoryTitle, consoleButtons);

    toolbar.appendChild(this.filterSpacer);
    toolbar.appendChild(this.filterBox);
    return toolbar;
  },

  /**
   * Creates the context menu on the console, which contains the "clear
   * console" functionality.
   *
   * @param nsIDOMNode aOutputNode
   *        The console output DOM node.
   * @returns void
   */
  createConsoleMenu: function HUD_createConsoleMenu(aConsoleWrapper) {
    let menuPopup = this.makeXULNode("menupopup");
    let id = this.hudId + "-output-contextmenu";
    menuPopup.setAttribute("id", id);

    let copyItem = this.makeXULNode("menuitem");
    copyItem.setAttribute("label", this.getStr("copyCmd.label"));
    copyItem.setAttribute("accesskey", this.getStr("copyCmd.accesskey"));
    copyItem.setAttribute("key", "key_copy");
    copyItem.setAttribute("command", "cmd_copy");
    menuPopup.appendChild(copyItem);

    menuPopup.appendChild(this.makeXULNode("menuseparator"));

    let clearItem = this.makeXULNode("menuitem");
    clearItem.setAttribute("label", this.getStr("itemClear"));
    clearItem.setAttribute("hudId", this.hudId);
    clearItem.setAttribute("buttonType", "clear");
    clearItem.setAttribute("oncommand", "HUDConsoleUI.command(this);");
    menuPopup.appendChild(clearItem);

    aConsoleWrapper.appendChild(menuPopup);
    aConsoleWrapper.setAttribute("context", id);
  },

  makeButton: function HUD_makeButton(aName, aPrefKey, aType)
  {
    var self = this;
    let prefKey = aPrefKey;

    let btn;
    if (aType == "checkbox") {
      btn = this.makeXULNode("checkbox");
      btn.setAttribute("type", aType);
    } else {
      btn = this.makeXULNode("toolbarbutton");
    }

    btn.setAttribute("hudId", this.hudId);
    btn.setAttribute("buttonType", prefKey);
    btn.setAttribute("class", "hud-filter-btn");
    let key = "btn" + aName;
    btn.setAttribute("label", this.getStr(key));
    key = "tip" + aName;
    btn.setAttribute("tooltip", this.getStr(key));

    if (aType == "checkbox") {
      btn.setAttribute("checked", this.filterPrefs[prefKey]);
      function toggle(btn) {
        self.consoleFilterCommands.toggle(btn);
      };

      btn.setAttribute("oncommand", "HUDConsoleUI.toggleFilter(this);");
    }
    else {
      var command = "HUDConsoleUI.command(this)";
      btn.setAttribute("oncommand", command);
    }
    return btn;
  },

  /**
   * Appends a category title and a series of buttons to the filter bar.
   *
   * @param nsIDOMNode aToolbar
   *        The DOM node to which to add the category.
   * @param string aTitle
   *        The title for the category.
   * @param Array aButtons
   *        The buttons, specified as objects with "name" and "prefKey"
   *        properties.
   * @returns nsIDOMNode
   */
  addButtonCategory: function(aToolbar, aTitle, aButtons) {
    let lbl = this.makeXULNode("label");
    lbl.setAttribute("class", "hud-filter-cat");
    lbl.setAttribute("value", aTitle);
    aToolbar.appendChild(lbl);

    for (let i = 0; i < aButtons.length; i++) {
      let btn = aButtons[i];
      aToolbar.appendChild(this.makeButton(btn.name, btn.prefKey, "checkbox"));
    }
  },

  createHUD: function HUD_createHUD()
  {
    let self = this;
    if (this.HUDBox) {
      return this.HUDBox;
    }
    else  {
      this.makeHUDNodes();

      let nodes = this.notificationBox.insertBefore(this.HUDBox,
        this.notificationBox.childNodes[0]);

      return this.HUDBox;
    }
  },

  get console() { return this._console || this.createConsole(); },

  getLogCount: function HUD_getLogCount()
  {
    return this.outputNode.childNodes.length;
  },

  getLogNodes: function HUD_getLogNodes()
  {
    return this.outputNode.childNodes;
  },

  /**
   * This console will accept a message, get the tab's meta-data and send
   * properly-formatted message object to the service identifying
   * where it came from, etc...
   *
   * @returns console
   */
  createConsole: function HUD_createConsole()
  {
    return new HUDConsole(this);
  },

  ERRORS: {
    HUD_BOX_DOES_NOT_EXIST: "Heads Up Display does not exist",
    TAB_ID_REQUIRED: "Tab DOM ID is required",
    PARENTNODE_NOT_FOUND: "parentNode element not found"
  }
};


//////////////////////////////////////////////////////////////////////////////
// HUDConsole factory function
//////////////////////////////////////////////////////////////////////////////

/**
 * The console object that is attached to each contentWindow
 *
 * @param object aHeadsUpDisplay
 * @returns object
 */
function HUDConsole(aHeadsUpDisplay)
{
  let hud = aHeadsUpDisplay;
  let hudId = hud.hudId;
  let outputNode = hud.outputNode;
  let chromeDocument = hud.chromeDocument;

  aHeadsUpDisplay._console = this;

  let sendToHUDService = function console_send(aLevel, aArguments)
  {
    let ts = ConsoleUtils.timestamp();
    let messageNode = hud.makeXULNode("label");

    let klass = "hud-msg-node hud-" + aLevel;

    messageNode.setAttribute("class", klass);

    let argumentArray = [];
    for (var i = 0; i < aArguments.length; i++) {
      argumentArray.push(aArguments[i]);
    }

    let message = argumentArray.join(' ');
    let timestampedMessage = ConsoleUtils.timestampString(ts) + ": " +
      message;

    messageNode.appendChild(chromeDocument.createTextNode(timestampedMessage));

    // need a constructor here to properly set all attrs
    let messageObject = {
      logLevel: aLevel,
      hudId: hud.hudId,
      message: message,
      timestamp: ts,
      origin: "HUDConsole",
    };

    HUDService.logMessage(messageObject, hud.outputNode, messageNode);
  }

  //////////////////////////////////////////////////////////////////////////////
  // Console API.
  this.log = function console_log()
  {
    sendToHUDService("log", arguments);
  },

  this.info = function console_info()
  {
    sendToHUDService("info", arguments);
  },

  this.warn = function console_warn()
  {
    sendToHUDService("warn", arguments);
  },

  this.error = function console_error()
  {
    sendToHUDService("error", arguments);
  },

  this.exception = function console_exception()
  {
    sendToHUDService("exception", arguments);
  }
};

/**
 * Creates a DOM Node factory for XUL nodes - as well as textNodes
 * @param   aFactoryType
 *          "xul" or "text"
 * @returns DOM Node Factory function
 */
function NodeFactory(aFactoryType, aNameSpace, aDocument)
{
  // aDocument is presumed to be a XULDocument
  if (aFactoryType == "text") {
    function factory(aText) {
      return aDocument.createTextNode(aText);
    }
    return factory;
  }
  else {
    if (aNameSpace == "xul") {
      function factory(aTag)
      {
        return aDocument.createElement(aTag);
      }
      return factory;
    }
  }
}

//////////////////////////////////////////////////////////////////////////
// JS Completer
//////////////////////////////////////////////////////////////////////////

const STATE_NORMAL = 0;
const STATE_QUOTE = 2;
const STATE_DQUOTE = 3;

const OPEN_BODY = '{[('.split('');
const CLOSE_BODY = '}])'.split('');
const OPEN_CLOSE_BODY = {
  '{': '}',
  '[': ']',
  '(': ')'
};

/**
 * Analyses a given string to find the last statement that is interesting for
 * later completion.
 *
 * @param   string aStr
 *          A string to analyse.
 *
 * @returns object
 *          If there was an error in the string detected, then a object like
 *
 *            { err: "ErrorMesssage" }
 *
 *          is returned, otherwise a object like
 *
 *            {
 *              state: STATE_NORMAL|STATE_QUOTE|STATE_DQUOTE,
 *              startPos: index of where the last statement begins
 *            }
 */
function findCompletionBeginning(aStr)
{
  let bodyStack = [];

  let state = STATE_NORMAL;
  let start = 0;
  let c;
  for (let i = 0; i < aStr.length; i++) {
    c = aStr[i];

    switch (state) {
      // Normal JS state.
      case STATE_NORMAL:
        if (c == '"') {
          state = STATE_DQUOTE;
        }
        else if (c == '\'') {
          state = STATE_QUOTE;
        }
        else if (c == ';') {
          start = i + 1;
        }
        else if (c == ' ') {
          start = i + 1;
        }
        else if (OPEN_BODY.indexOf(c) != -1) {
          bodyStack.push({
            token: c,
            start: start
          });
          start = i + 1;
        }
        else if (CLOSE_BODY.indexOf(c) != -1) {
          var last = bodyStack.pop();
          if (OPEN_CLOSE_BODY[last.token] != c) {
            return {
              err: "syntax error"
            };
          }
          if (c == '}') {
            start = i + 1;
          }
          else {
            start = last.start;
          }
        }
        break;

      // Double quote state > " <
      case STATE_DQUOTE:
        if (c == '\\') {
          i ++;
        }
        else if (c == '\n') {
          return {
            err: "unterminated string literal"
          };
        }
        else if (c == '"') {
          state = STATE_NORMAL;
        }
        break;

      // Single quoate state > ' <
      case STATE_QUOTE:
        if (c == '\\') {
          i ++;
        }
        else if (c == '\n') {
          return {
            err: "unterminated string literal"
          };
          return;
        }
        else if (c == '\'') {
          state = STATE_NORMAL;
        }
        break;
    }
  }

  return {
    state: state,
    startPos: start
  };
}

/**
 * Provides a list of properties, that are possible matches based on the passed
 * scope and inputValue.
 *
 * @param object aScope
 *        Scope to use for the completion.
 *
 * @param string aInputValue
 *        Value that should be completed.
 *
 * @returns null or object
 *          If no completion valued could be computed, null is returned,
 *          otherwise a object with the following form is returned:
 *            {
 *              matches: [ string, string, string ],
 *              matchProp: Last part of the inputValue that was used to find
 *                         the matches-strings.
 *            }
 */
function JSPropertyProvider(aScope, aInputValue)
{
  let obj = aScope;

  // Analyse the aInputValue and find the beginning of the last part that
  // should be completed.
  let beginning = findCompletionBeginning(aInputValue);

  // There was an error analysing the string.
  if (beginning.err) {
    return null;
  }

  // If the current state is not STATE_NORMAL, then we are inside of an string
  // which means that no completion is possible.
  if (beginning.state != STATE_NORMAL) {
    return null;
  }

  let completionPart = aInputValue.substring(beginning.startPos);

  // Don't complete on just an empty string.
  if (completionPart.trim() == "") {
    return null;
  }

  let properties = completionPart.split('.');
  let matchProp;
  if (properties.length > 1) {
      matchProp = properties[properties.length - 1].trimLeft();
      properties.pop();
      for each (var prop in properties) {
        prop = prop.trim();

        // If obj is undefined or null, then there is no change to run
        // completion on it. Exit here.
        if (typeof obj === "undefined" || obj === null) {
          return null;
        }

        // Check if prop is a getter function on obj. Functions can change other
        // stuff so we can't execute them to get the next object. Stop here.
        if (obj.__lookupGetter__(prop)) {
          return null;
        }
        obj = obj[prop];
      }
  }
  else {
    matchProp = properties[0].trimLeft();
  }

  // If obj is undefined or null, then there is no change to run
  // completion on it. Exit here.
  if (typeof obj === "undefined" || obj === null) {
    return null;
  }

  let matches = [];
  for (var prop in obj) {
    matches.push(prop);
  }

  matches = matches.filter(function(item) {
    return item.indexOf(matchProp) == 0;
  }).sort();

  return {
    matchProp: matchProp,
    matches: matches
  };
}

//////////////////////////////////////////////////////////////////////////
// JSTerm
//////////////////////////////////////////////////////////////////////////

/**
 * JSTerm
 *
 * JavaScript Terminal: creates input nodes for console code interpretation
 * and 'JS Workspaces'
 */

/**
 * Create a JSTerminal or attach a JSTerm input node to an existing output node
 *
 *
 *
 * @param object aContext
 *        Usually nsIDOMWindow, but doesn't have to be
 * @param nsIDOMNode aParentNode
 * @param object aMixin
 *        Gecko-app (or Jetpack) specific utility object
 * @returns void
 */
function JSTerm(aContext, aParentNode, aMixin)
{
  // set the context, attach the UI by appending to aParentNode

  this.application = appName();
  this.context = aContext;
  this.parentNode = aParentNode;
  this.mixins = aMixin;

  this.xulElementFactory =
    NodeFactory("xul", "xul", aParentNode.ownerDocument);

  this.textFactory = NodeFactory("text", "xul", aParentNode.ownerDocument);

  this.setTimeout = aParentNode.ownerDocument.defaultView.setTimeout;

  this.historyIndex = 0;
  this.historyPlaceHolder = 0;  // this.history.length;
  this.log = LogFactory("*** JSTerm:");
  this.init();
}

JSTerm.prototype = {

  propertyProvider: JSPropertyProvider,

  COMPLETE_FORWARD: 0,
  COMPLETE_BACKWARD: 1,
  COMPLETE_HINT_ONLY: 2,

  init: function JST_init()
  {
    this.createSandbox();
    this.inputNode = this.mixins.inputNode;
    let eventHandlerKeyDown = this.keyDown();
    this.inputNode.addEventListener('keypress', eventHandlerKeyDown, false);
    let eventHandlerInput = this.inputEventHandler();
    this.inputNode.addEventListener('input', eventHandlerInput, false);
    this.outputNode = this.mixins.outputNode;
    if (this.mixins.cssClassOverride) {
      this.cssClassOverride = this.mixins.cssClassOverride;
    }
  },

  get codeInputString()
  {
    // TODO: filter the input for windows line breaks, conver to unix
    // see bug 572812
    return this.inputNode.value;
  },

  generateUI: function JST_generateUI()
  {
    this.mixins.generateUI();
  },

  attachUI: function JST_attachUI()
  {
    this.mixins.attachUI();
  },

  createSandbox: function JST_setupSandbox()
  {
    // create a JS Sandbox out of this.context
    this._window.wrappedJSObject.jsterm = {};
    this.console = this._window.wrappedJSObject.console;
    this.sandbox = new Cu.Sandbox(this._window);
    this.sandbox.window = this._window;
    this.sandbox.console = this.console;
    this.sandbox.__proto__ = this._window.wrappedJSObject;
  },

  get _window()
  {
    return this.context.get().QueryInterface(Ci.nsIDOMWindowInternal);
  },
  /**
   * Evaluates a string in the sandbox. The string is currently wrapped by a
   * with(window) { aString } construct, see bug 574033.
   *
   * @param string aString
   *        String to evaluate in the sandbox.
   * @returns something
   *          The result of the evaluation.
   */
  evalInSandbox: function JST_evalInSandbox(aString)
  {
    let execStr = "with(window) {" + aString + "}";
    return Cu.evalInSandbox(execStr,  this.sandbox, "default", "HUD Console", 1);
  },


  execute: function JST_execute(aExecuteString)
  {
    // attempt to execute the content of the inputNode
    aExecuteString = aExecuteString || this.inputNode.value;
    if (!aExecuteString) {
      this.console.log("no value to execute");
      return;
    }

    this.writeOutput(aExecuteString, true);

    try {
      var result = this.evalInSandbox(aExecuteString);

      if (result || result === false) {
        this.writeOutputJS(aExecuteString, result);
      }
      else if (result === undefined) {
        this.writeOutput("undefined", false);
      }
      else if (result === null) {
        this.writeOutput("null", false);
      }
    }
    catch (ex) {
      this.console.error(ex);
    }

    this.history.push(aExecuteString);
    this.historyIndex++;
    this.historyPlaceHolder = this.history.length;
    this.inputNode.value = "";
  },

  /**
   * Opens a new PropertyPanel. The panel has two buttons: "Update" reexecutes
   * the passed aEvalString and places the result inside of the tree. The other
   * button closes the panel.
   *
   * @param string aEvalString
   *        String that was used to eval the aOutputObject. Used as title
   *        and to update the tree content.
   * @param object aOutputObject
   *        Object to display/inspect inside of the tree.
   * @param nsIDOMNode aAnchor
   *        A node to popup the panel next to (using "after_pointer").
   * @returns object the created and opened propertyPanel.
   */
  openPropertyPanel: function JST_openPropertyPanel(aEvalString, aOutputObject,
                                                    aAnchor)
  {
    let self = this;
    let propPanel;
    // The property panel has two buttons:
    // 1. `Update`: reexecutes the string executed on the command line. The
    //    result will be inspected by this panel.
    // 2. `Close`: destroys the panel.
    let buttons = [];

    // If there is a evalString passed to this function, then add a `Update`
    // button to the panel so that the evalString can be reexecuted to update
    // the content of the panel.
    if (aEvalString !== null) {
      buttons.push({
        label: HUDService.getStr("update.button"),
        accesskey: HUDService.getStr("update.accesskey"),
        oncommand: function () {
          try {
            var result = self.evalInSandbox(aEvalString);

            if (result !== undefined) {
              // TODO: This updates the value of the tree.
              // However, the states of opened nodes is not saved.
              // See bug 586246.
              propPanel.treeView.data = result;
            }
          }
          catch (ex) {
            self.console.error(ex);
          }
        }
      });
    }

    buttons.push({
      label: HUDService.getStr("close.button"),
      accesskey: HUDService.getStr("close.accesskey"),
      oncommand: function () {
        propPanel.destroy();
      }
    });

    let doc = self.parentNode.ownerDocument;
    let parent = doc.getElementById("mainPopupSet");
    let title = (aEvalString
        ? HUDService.getFormatStr("jsPropertyInspectTitle", [aEvalString])
        : HUDService.getStr("jsPropertyTitle"));
    propPanel = new PropertyPanel(parent, doc, title, aOutputObject, buttons);

    let panel = propPanel.panel;
    panel.openPopup(aAnchor, "after_pointer", 0, 0, false, false);
    panel.sizeTo(200, 400);
    return propPanel;
  },

  /**
   * Writes a JS object to the JSTerm outputNode. If the user clicks on the
   * written object, openPropertyPanel is called to open up a panel to inspect
   * the object.
   *
   * @param string aEvalString
   *        String that was evaluated to get the aOutputObject.
   * @param object aOutputObject
   *        Object to be written to the outputNode.
   */
  writeOutputJS: function JST_writeOutputJS(aEvalString, aOutputObject)
  {
    let lastGroupNode = HUDService.appendGroupIfNecessary(this.outputNode,
                                                      Date.now());

    var self = this;
    var node = this.xulElementFactory("label");
    node.setAttribute("class", "jsterm-output-line");
    node.setAttribute("aria-haspopup", "true");
    node.onclick = function() {
      self.openPropertyPanel(aEvalString, aOutputObject, node);
    }

    // TODO: format the aOutputObject and don't just use the
    // aOuputObject.toString() function: [object object] -> Object {prop, ...}
    // See bug 586249.
    let textNode = this.textFactory(aOutputObject);
    node.appendChild(textNode);

    lastGroupNode.appendChild(node);
    ConsoleUtils.scrollToVisible(node);
  },

  /**
   * Writes a message to the HUD that originates from the interactive
   * JavaScript console.
   *
   * @param string aOutputMessage
   *        The message to display.
   * @param boolean aIsInput
   *        True if the message is the user's input, false if the message is
   *        the result of the expression the user typed.
   * @returns void
   */
  writeOutput: function JST_writeOutput(aOutputMessage, aIsInput)
  {
    let lastGroupNode = HUDService.appendGroupIfNecessary(this.outputNode,
                                                          Date.now());

    var node = this.xulElementFactory("label");
    if (aIsInput) {
      node.setAttribute("class", "jsterm-input-line");
      aOutputMessage = "> " + aOutputMessage;
    }
    else {
      node.setAttribute("class", "jsterm-output-line");
    }

    if (this.cssClassOverride) {
      let classes = this.cssClassOverride.split(" ");
      for (let i = 0; i < classes.length; i++) {
        node.classList.add(classes[i]);
      }
    }

    var textNode = this.textFactory(aOutputMessage);
    node.appendChild(textNode);

    lastGroupNode.appendChild(node);
    ConsoleUtils.scrollToVisible(node);
  },

  clearOutput: function JST_clearOutput()
  {
    let outputNode = this.outputNode;

    while (outputNode.firstChild) {
      outputNode.removeChild(outputNode.firstChild);
    }

    outputNode.lastTimestamp = 0;
  },

  inputEventHandler: function JSTF_inputEventHandler()
  {
    var self = this;
    function handleInputEvent(aEvent) {
      self.inputNode.setAttribute("rows",
        Math.min(8, self.inputNode.value.split("\n").length));
    }
    return handleInputEvent;
  },

  keyDown: function JSTF_keyDown(aEvent)
  {
    var self = this;
    function handleKeyDown(aEvent) {
      // ctrl-a
      var setTimeout = aEvent.target.ownerDocument.defaultView.setTimeout;
      var target = aEvent.target;
      var tmp;

      if (aEvent.ctrlKey) {
        switch (aEvent.charCode) {
          case 97:
            // control-a
            tmp = self.codeInputString;
            setTimeout(function() {
              self.inputNode.value = tmp;
              self.inputNode.setSelectionRange(0, 0);
            }, 0);
            break;
          case 101:
            // control-e
            tmp = self.codeInputString;
            self.inputNode.value = "";
            setTimeout(function(){
              var endPos = tmp.length + 1;
              self.inputNode.value = tmp;
            }, 0);
            break;
          default:
            return;
        }
        return;
      }
      else if (aEvent.shiftKey && aEvent.keyCode == 13) {
        // shift return
        // TODO: expand the inputNode height by one line
        return;
      }
      else {
        switch(aEvent.keyCode) {
          case 13:
            // return
            self.execute();
            aEvent.preventDefault();
            break;
          case 38:
            // up arrow: history previous
            if (self.caretInFirstLine()){
              self.historyPeruse(true);
              if (aEvent.cancelable) {
                let inputEnd = self.inputNode.value.length;
                self.inputNode.setSelectionRange(inputEnd, inputEnd);
                aEvent.preventDefault();
              }
            }
            break;
          case 40:
            // down arrow: history next
            if (self.caretInLastLine()){
              self.historyPeruse(false);
              if (aEvent.cancelable) {
                let inputEnd = self.inputNode.value.length;
                self.inputNode.setSelectionRange(inputEnd, inputEnd);
                aEvent.preventDefault();
              }
            }
            break;
          case 9:
            // tab key
            // If there are more than one possible completion, pressing tab
            // means taking the next completion, shift_tab means taking
            // the previous completion.
            if (aEvent.shiftKey) {
              self.complete(self.COMPLETE_BACKWARD);
            }
            else {
              self.complete(self.COMPLETE_FORWARD);
            }
            var bool = aEvent.cancelable;
            if (bool) {
              aEvent.preventDefault();
            }
            else {
              // noop
            }
            aEvent.target.focus();
            break;
          case 8:
            // backspace key
          case 46:
            // delete key
            // necessary so that default is not reached.
            break;
          default:
            // all not handled keys
            // Store the current inputNode value. If the value is the same
            // after keyDown event was handled (after 0ms) then the user
            // moved the cursor. If the value changed, then call the complete
            // function to show completion on new value.
            var value = self.inputNode.value;
            setTimeout(function() {
              if (self.inputNode.value !== value) {
                self.complete(self.COMPLETE_HINT_ONLY);
              }
            }, 0);
            break;
        }
        return;
      }
    }
    return handleKeyDown;
  },

  historyPeruse: function JST_historyPeruse(aFlag) {
    if (!this.history.length) {
      return;
    }

    // Up Arrow key
    if (aFlag) {
      if (this.historyPlaceHolder <= 0) {
        return;
      }

      let inputVal = this.history[--this.historyPlaceHolder];
      if (inputVal){
        this.inputNode.value = inputVal;
      }
    }
    // Down Arrow key
    else {
      if (this.historyPlaceHolder == this.history.length - 1) {
        this.historyPlaceHolder ++;
        this.inputNode.value = "";
        return;
      }
      else if (this.historyPlaceHolder >= (this.history.length)) {
        return;
      }
      else {
        let inputVal = this.history[++this.historyPlaceHolder];
        if (inputVal){
          this.inputNode.value = inputVal;
        }
      }
    }
  },

  refocus: function JSTF_refocus()
  {
    this.inputNode.blur();
    this.inputNode.focus();
  },

  caretInFirstLine: function JSTF_caretInFirstLine()
  {
    var firstLineBreak = this.codeInputString.indexOf("\n");
    return ((firstLineBreak == -1) ||
            (this.inputNode.selectionStart <= firstLineBreak));
  },

  caretInLastLine: function JSTF_caretInLastLine()
  {
    var lastLineBreak = this.codeInputString.lastIndexOf("\n");
    return (this.inputNode.selectionEnd > lastLineBreak);
  },

  history: [],

  // Stores the data for the last completion.
  lastCompletion: null,

  /**
   * Completes the current typed text in the inputNode. Completion is performed
   * only if the selection/cursor is at the end of the string. If no completion
   * is found, the current inputNode value and cursor/selection stay.
   *
   * @param int type possible values are
   *    - this.COMPLETE_FORWARD: If there is more than one possible completion
   *          and the input value stayed the same compared to the last time this
   *          function was called, then the next completion of all possible
   *          completions is used. If the value changed, then the first possible
   *          completion is used and the selection is set from the current
   *          cursor position to the end of the completed text.
   *          If there is only one possible completion, then this completion
   *          value is used and the cursor is put at the end of the completion.
   *    - this.COMPLETE_BACKWARD: Same as this.COMPLETE_FORWARD but if the
   *          value stayed the same as the last time the function was called,
   *          then the previous completion of all possible completions is used.
   *    - this.COMPLETE_HINT_ONLY: If there is more than one possible
   *          completion and the input value stayed the same compared to the
   *          last time this function was called, then the same completion is
   *          used again. If there is only one possible completion, then
   *          the inputNode.value is set to this value and the selection is set
   *          from the current cursor position to the end of the completed text.
   *
   * @returns void
   */
  complete: function JSTF_complete(type)
  {
    let inputNode = this.inputNode;
    let inputValue = inputNode.value;
    // If the inputNode has no value, then don't try to complete on it.
    if (!inputValue) {
      return;
    }
    let selStart = inputNode.selectionStart, selEnd = inputNode.selectionEnd;

    // 'Normalize' the selection so that end is always after start.
    if (selStart > selEnd) {
      let newSelEnd = selStart;
      selStart = selEnd;
      selEnd = newSelEnd;
    }

    // Only complete if the selection is at the end of the input.
    if (selEnd != inputValue.length) {
      this.lastCompletion = null;
      return;
    }

    // Remove the selected text from the inputValue.
    inputValue = inputValue.substring(0, selStart);

    let matches;
    let matchIndexToUse;
    let matchOffset;
    let completionStr;

    // If there is a saved completion from last time and the used value for
    // completion stayed the same, then use the stored completion.
    if (this.lastCompletion && inputValue == this.lastCompletion.value) {
      matches = this.lastCompletion.matches;
      matchOffset = this.lastCompletion.matchOffset;
      if (type === this.COMPLETE_BACKWARD) {
        this.lastCompletion.index --;
      }
      else if (type === this.COMPLETE_FORWARD) {
        this.lastCompletion.index ++;
      }
      matchIndexToUse = this.lastCompletion.index;
    }
    else {
      // Look up possible completion values.
      let completion = this.propertyProvider(this.sandbox.window, inputValue);
      if (!completion) {
        return;
      }
      matches = completion.matches;
      matchIndexToUse = 0;
      matchOffset = completion.matchProp.length
      // Store this match;
      this.lastCompletion = {
        index: 0,
        value: inputValue,
        matches: matches,
        matchOffset: matchOffset
      };
    }

    if (matches.length != 0) {
      // Ensure that the matchIndexToUse is always a valid array index.
      if (matchIndexToUse < 0) {
        matchIndexToUse = matches.length + (matchIndexToUse % matches.length);
        if (matchIndexToUse == matches.length) {
          matchIndexToUse = 0;
        }
      }
      else {
        matchIndexToUse = matchIndexToUse % matches.length;
      }

      completionStr = matches[matchIndexToUse].substring(matchOffset);
      this.inputNode.value = inputValue +  completionStr;

      selEnd = inputValue.length + completionStr.length;

      // If there is more than one possible completion or the completed part
      // should get displayed only without moving the cursor at the end of the
      // completion.
      if (matches.length > 1 || type === this.COMPLETE_HINT_ONLY) {
        inputNode.setSelectionRange(selStart, selEnd);
      }
      else {
        inputNode.setSelectionRange(selEnd, selEnd);
      }
    }
  }
};

/**
 * JSTermFirefoxMixin
 *
 * JavaScript Terminal Firefox Mixin
 *
 */
function
JSTermFirefoxMixin(aContext,
                   aParentNode,
                   aExistingConsole,
                   aCSSClassOverride)
{
  // aExisting Console is the existing outputNode to use in favor of
  // creating a new outputNode - this is so we can just attach the inputNode to
  // a normal HeadsUpDisplay console output, and re-use code.
  this.cssClassOverride = aCSSClassOverride;
  this.context = aContext;
  this.parentNode = aParentNode;
  this.existingConsoleNode = aExistingConsole;
  this.setTimeout = aParentNode.ownerDocument.defaultView.setTimeout;

  if (aParentNode.ownerDocument) {
    this.xulElementFactory =
      NodeFactory("xul", "xul", aParentNode.ownerDocument);

    this.textFactory = NodeFactory("text", "xul", aParentNode.ownerDocument);
    this.generateUI();
    this.attachUI();
  }
  else {
    throw new Error("aParentNode should be a DOM node with an ownerDocument property ");
  }
}

JSTermFirefoxMixin.prototype = {
  /**
   * Generates and attaches the UI for an entire JS Workspace or
   * just the input node used under the console output
   *
   * @returns void
   */
  generateUI: function JSTF_generateUI()
  {
    let inputNode = this.xulElementFactory("textbox");
    inputNode.setAttribute("class", "jsterm-input-node");
    inputNode.setAttribute("multiline", "true");
    inputNode.setAttribute("rows", "1");

    if (this.existingConsoleNode == undefined) {
      // create elements
      let term = this.xulElementFactory("vbox");
      term.setAttribute("class", "jsterm-wrapper-node");
      term.setAttribute("flex", "1");

      let outputNode = this.xulElementFactory("vbox");
      outputNode.setAttribute("class", "jsterm-output-node");

      // construction
      term.appendChild(outputNode);
      term.appendChild(inputNode);

      this.outputNode = outputNode;
      this.inputNode = inputNode;
      this.term = term;
    }
    else {
      this.inputNode = inputNode;
      this.term = inputNode;
      this.outputNode = this.existingConsoleNode;
    }
  },

  get inputValue()
  {
    return this.inputNode.value;
  },

  attachUI: function JSTF_attachUI()
  {
    this.parentNode.appendChild(this.term);
  }
};

/**
 * LogMessage represents a single message logged to the "outputNode" console
 */
function LogMessage(aMessage, aLevel, aOutputNode, aActivityObject)
{
  if (!aOutputNode || !aOutputNode.ownerDocument) {
    throw new Error("aOutputNode is required and should be type nsIDOMNode");
  }
  if (!aMessage.origin) {
    throw new Error("Cannot create and log a message without an origin");
  }
  this.message = aMessage;
  if (aMessage.domId) {
    // domId is optional - we only need it if the logmessage is
    // being asynchronously updated
    this.domId = aMessage.domId;
  }
  this.activityObject = aActivityObject;
  this.outputNode = aOutputNode;
  this.level = aLevel;
  this.origin = aMessage.origin;

  this.xulElementFactory =
  NodeFactory("xul", "xul", aOutputNode.ownerDocument);

  this.textFactory = NodeFactory("text", "xul", aOutputNode.ownerDocument);

  this.createLogNode();
}

LogMessage.prototype = {

  /**
   * create a console log div node
   *
   * @returns nsIDOMNode
   */
  createLogNode: function LM_createLogNode()
  {
    this.messageNode = this.xulElementFactory("label");

    var ts = ConsoleUtils.timestamp();
    var timestampedMessage = ConsoleUtils.timestampString(ts) + ": " +
      this.message.message;
    var messageTxtNode = this.textFactory(timestampedMessage);

    this.messageNode.appendChild(messageTxtNode);

    var klass = "hud-msg-node hud-" + this.level;
    this.messageNode.setAttribute("class", klass);

    var self = this;

    var messageObject = {
      logLevel: self.level,
      message: self.message,
      timestamp: ts,
      activity: self.activityObject,
      origin: self.origin,
      hudId: self.message.hudId,
    };

    this.messageObject = messageObject;
  }
};


/**
 * Firefox-specific Application Hooks.
 * Each Gecko-based application will need an object like this in
 * order to use the Heads Up Display
 */
function FirefoxApplicationHooks()
{ }

FirefoxApplicationHooks.prototype = {

  /**
   * Firefox-specific method for getting an array of chrome Window objects
   */
  get chromeWindows()
  {
    var windows = [];
    var enumerator = Services.ww.getWindowEnumerator(null);
    while (enumerator.hasMoreElements()) {
      windows.push(enumerator.getNext());
    }
    return windows;
  },

  /**
   * Firefox-specific method for getting the DOM node (per tab) that message
   * nodes are appended to.
   * @param aId
   *        The DOM node's id.
   */
  getOutputNodeById: function FAH_getOutputNodeById(aId)
  {
    if (!aId) {
      throw new Error("FAH_getOutputNodeById: id is null!!");
    }
    var enumerator = Services.ww.getWindowEnumerator(null);
    while (enumerator.hasMoreElements()) {
      let window = enumerator.getNext();
      let node = window.document.getElementById(aId);
      if (node) {
        return node;
      }
    }
    throw new Error("Cannot get outputNode by id");
  },

  /**
   * gets the current contentWindow (Firefox-specific)
   *
   * @returns nsIDOMWindow
   */
  getCurrentContext: function FAH_getCurrentContext()
  {
    return Services.wm.getMostRecentWindow("navigator:browser");
  }
};

//////////////////////////////////////////////////////////////////////////////
// Utility functions used by multiple callers
//////////////////////////////////////////////////////////////////////////////

/**
 * ConsoleUtils: a collection of globally used functions
 *
 */

ConsoleUtils = {

  /**
   * Generates a millisecond resolution timestamp.
   *
   * @returns integer
   */
  timestamp: function ConsoleUtils_timestamp()
  {
    return Date.now();
  },

  /**
   * Generates a formatted timestamp string for displaying in console messages.
   *
   * @param integer [ms] Optional, allows you to specify the timestamp in
   * milliseconds since the UNIX epoch.
   * @returns string The timestamp formatted for display.
   */
  timestampString: function ConsoleUtils_timestampString(ms)
  {
    // TODO: L10N see bug 568656
    var d = new Date(ms ? ms : null);

    function pad(n, mil)
    {
      if (mil) {
        return n < 100 ? "0" + n : n;
      }
      else {
        return n < 10 ? "0" + n : n;
      }
    }

    return pad(d.getHours()) + ":"
      + pad(d.getMinutes()) + ":"
      + pad(d.getSeconds()) + ":"
      + pad(d.getMilliseconds(), true);
  },

  /**
   * Scrolls a node so that it's visible in its containing XUL "scrollbox"
   * element.
   *
   * @param nsIDOMNode aNode
   *        The node to make visible.
   * @returns void
   */
  scrollToVisible: function ConsoleUtils_scrollToVisible(aNode) {
    let scrollBoxNode = aNode.parentNode;
    while (scrollBoxNode.tagName !== "scrollbox") {
      scrollBoxNode = scrollBoxNode.parentNode;
    }

    let boxObject = scrollBoxNode.boxObject;
    let nsIScrollBoxObject = boxObject.QueryInterface(Ci.nsIScrollBoxObject);
    nsIScrollBoxObject.ensureElementIsVisible(aNode);
  }
};

/**
 * Creates a DOM Node factory for XUL nodes - as well as textNodes
 * @param   aFactoryType
 *          "xul" or "text"
 * @returns DOM Node Factory function
 */
function NodeFactory(aFactoryType, aNameSpace, aDocument)
{
  // aDocument is presumed to be a XULDocument
  if (aFactoryType == "text") {
    function factory(aText) {
      return aDocument.createTextNode(aText);
    }
    return factory;
  }
  else {
    if (aNameSpace == "xul") {
      function factory(aTag) {
        return aDocument.createElement(aTag);
      }
      return factory;
    }
  }
}


//////////////////////////////////////////////////////////////////////////
// HeadsUpDisplayUICommands
//////////////////////////////////////////////////////////////////////////

HeadsUpDisplayUICommands = {
  toggleHUD: function UIC_toggleHUD() {
    var window = HUDService.currentContext();
    var gBrowser = window.gBrowser;
    var linkedBrowser = gBrowser.selectedTab.linkedBrowser;
    var tabId = gBrowser.getNotificationBox(linkedBrowser).getAttribute("id");
    var hudId = "hud_" + tabId;
    var hud = gBrowser.selectedTab.ownerDocument.getElementById(hudId);
    if (hud) {
      HUDService.deactivateHUDForContext(gBrowser.selectedTab);
    }
    else {
      HUDService.activateHUDForContext(gBrowser.selectedTab);
    }
  },

  toggleFilter: function UIC_toggleFilter(aButton) {
    var filter = aButton.getAttribute("buttonType");
    var hudId = aButton.getAttribute("hudId");
    var state = HUDService.getFilterState(hudId, filter);
    if (state) {
      HUDService.setFilterState(hudId, filter, false);
      aButton.setAttribute("checked", false);
    }
    else {
      HUDService.setFilterState(hudId, filter, true);
      aButton.setAttribute("checked", true);
    }
  },

  command: function UIC_command(aButton) {
    var filter = aButton.getAttribute("buttonType");
    var hudId = aButton.getAttribute("hudId");
    if (filter == "clear") {
      HUDService.clearDisplay(hudId);
    }
  },

};

//////////////////////////////////////////////////////////////////////////
// ConsoleStorage
//////////////////////////////////////////////////////////////////////////

var prefs = Services.prefs;

const GLOBAL_STORAGE_INDEX_ID = "GLOBAL_CONSOLE";
const PREFS_BRANCH_PREF = "devtools.hud.display.filter";
const PREFS_PREFIX = "devtools.hud.display.filter.";
const PREFS = { network: PREFS_PREFIX + "network",
                cssparser: PREFS_PREFIX + "cssparser",
                exception: PREFS_PREFIX + "exception",
                error: PREFS_PREFIX + "error",
                info: PREFS_PREFIX + "info",
                warn: PREFS_PREFIX + "warn",
                log: PREFS_PREFIX + "log",
                global: PREFS_PREFIX + "global",
              };

function ConsoleStorage()
{
  this.sequencer = null;
  this.consoleDisplays = {};
  // each display will have an index that tracks each ConsoleEntry
  this.displayIndexes = {};
  this.globalStorageIndex = [];
  this.globalDisplay = {};
  this.createDisplay(GLOBAL_STORAGE_INDEX_ID);
  // TODO: need to create a method that truncates the message
  // see bug 570543

  // store an index of display prefs
  this.displayPrefs = {};

  // check prefs for existence, create & load if absent, load them if present
  let filterPrefs;
  let defaultDisplayPrefs;

  try {
    filterPrefs = prefs.getBoolPref(PREFS_BRANCH_PREF);
  }
  catch (ex) {
    filterPrefs = false;
  }

  // TODO: for FINAL release,
  // use the sitePreferencesService to save specific site prefs
  // see bug 570545

  if (filterPrefs) {
    defaultDisplayPrefs = {
      network: (prefs.getBoolPref(PREFS.network) ? true: false),
      cssparser: (prefs.getBoolPref(PREFS.cssparser) ? true: false),
      exception: (prefs.getBoolPref(PREFS.exception) ? true: false),
      error: (prefs.getBoolPref(PREFS.error) ? true: false),
      info: (prefs.getBoolPref(PREFS.info) ? true: false),
      warn: (prefs.getBoolPref(PREFS.warn) ? true: false),
      log: (prefs.getBoolPref(PREFS.log) ? true: false),
      global: (prefs.getBoolPref(PREFS.global) ? true: false),
    };
  }
  else {
    prefs.setBoolPref(PREFS_BRANCH_PREF, false);
    // default prefs for each HeadsUpDisplay
    prefs.setBoolPref(PREFS.network, true);
    prefs.setBoolPref(PREFS.cssparser, true);
    prefs.setBoolPref(PREFS.exception, true);
    prefs.setBoolPref(PREFS.error, true);
    prefs.setBoolPref(PREFS.info, true);
    prefs.setBoolPref(PREFS.warn, true);
    prefs.setBoolPref(PREFS.log, true);
    prefs.setBoolPref(PREFS.global, false);

    defaultDisplayPrefs = {
      network: prefs.getBoolPref(PREFS.network),
      cssparser: prefs.getBoolPref(PREFS.cssparser),
      exception: prefs.getBoolPref(PREFS.exception),
      error: prefs.getBoolPref(PREFS.error),
      info: prefs.getBoolPref(PREFS.info),
      warn: prefs.getBoolPref(PREFS.warn),
      log: prefs.getBoolPref(PREFS.log),
      global: prefs.getBoolPref(PREFS.global),
    };
  }
  this.defaultDisplayPrefs = defaultDisplayPrefs;
}

ConsoleStorage.prototype = {

  updateDefaultDisplayPrefs:
  function CS_updateDefaultDisplayPrefs(aPrefsObject) {
    prefs.setBoolPref(PREFS.network, (aPrefsObject.network ? true : false));
    prefs.setBoolPref(PREFS.cssparser, (aPrefsObject.cssparser ? true : false));
    prefs.setBoolPref(PREFS.exception, (aPrefsObject.exception ? true : false));
    prefs.setBoolPref(PREFS.error, (aPrefsObject.error ? true : false));
    prefs.setBoolPref(PREFS.info, (aPrefsObject.info ? true : false));
    prefs.setBoolPref(PREFS.warn, (aPrefsObject.warn ? true : false));
    prefs.setBoolPref(PREFS.log, (aPrefsObject.log ? true : false));
    prefs.setBoolPref(PREFS.global, (aPrefsObject.global ? true : false));
  },

  sequenceId: function CS_sequencerId()
  {
    if (!this.sequencer) {
      this.sequencer = this.createSequencer();
    }
    return this.sequencer.next();
  },

  createSequencer: function CS_createSequencer()
  {
    function sequencer(aInt) {
      while(1) {
        aInt++;
        yield aInt;
      }
    }
    return sequencer(-1);
  },

  globalStore: function CS_globalStore(aIndex)
  {
    return this.displayStore(GLOBAL_CONSOLE_DOM_NODE_ID);
  },

  displayStore: function CS_displayStore(aId)
  {
    var self = this;
    var idx = -1;
    var id = aId;
    var aLength = self.displayIndexes[id].length;

    function displayStoreGenerator(aInt, aLength)
    {
      // create a generator object to iterate through any of the display stores
      // from any index-starting-point
      while(1) {
        // throw if we exceed the length of displayIndexes?
        aInt++;
        var indexIt = self.displayIndexes[id];
        var index = indexIt[aInt];
        if (aLength < aInt) {
          // try to see if we have more entries:
          var newLength = self.displayIndexes[id].length;
          if (newLength > aLength) {
            aLength = newLength;
          }
          else {
            throw new StopIteration();
          }
        }
        var entry = self.consoleDisplays[id][index];
        yield entry;
      }
    }

    return displayStoreGenerator(-1, aLength);
  },

  recordEntries: function CS_recordEntries(aHUDId, aConfigArray)
  {
    var len = aConfigArray.length;
    for (var i = 0; i < len; i++){
      this.recordEntry(aHUDId, aConfigArray[i]);
    }
  },


  recordEntry: function CS_recordEntry(aHUDId, aConfig)
  {
    var id = this.sequenceId();

    this.globalStorageIndex[id] = { hudId: aHUDId };

    var displayStorage = this.consoleDisplays[aHUDId];

    var displayIndex = this.displayIndexes[aHUDId];

    if (displayStorage && displayIndex) {
      var entry = new ConsoleEntry(aConfig, id);
      displayIndex.push(entry.id);
      displayStorage[entry.id] = entry;
      return entry;
    }
    else {
      throw new Error("Cannot get displayStorage or index object for id " + aHUDId);
    }
  },

  getEntry: function CS_getEntry(aId)
  {
    var display = this.globalStorageIndex[aId];
    var storName = display.hudId;
    return this.consoleDisplays[storName][aId];
  },

  updateEntry: function CS_updateEntry(aUUID)
  {
    // update an individual entry
    // TODO: see bug 568634
  },

  createDisplay: function CS_createdisplay(aId)
  {
    if (!this.consoleDisplays[aId]) {
      this.consoleDisplays[aId] = {};
      this.displayIndexes[aId] = [];
    }
  },

  removeDisplay: function CS_removeDisplay(aId)
  {
    try {
      delete this.consoleDisplays[aId];
      delete this.displayIndexes[aId];
    }
    catch (ex) {
      Cu.reportError("Could not remove console display for id " + aId);
    }
  }
};

/**
 * A Console log entry
 *
 * @param JSObject aConfig, object literal with ConsolEntry properties
 * @param integer aId
 * @returns void
 */

function ConsoleEntry(aConfig, id)
{
  if (!aConfig.logLevel && aConfig.message) {
    throw new Error("Missing Arguments when creating a console entry");
  }

  this.config = aConfig;
  this.id = id;
  for (var prop in aConfig) {
    if (!(typeof aConfig[prop] == "function")){
      this[prop] = aConfig[prop];
    }
  }

  if (aConfig.logLevel == "network") {
    this.transactions = { };
    if (aConfig.activity) {
      this.transactions[aConfig.activity.stage] = aConfig.activity;
    }
  }

}

ConsoleEntry.prototype = {

  updateTransaction: function CE_updateTransaction(aActivity) {
    this.transactions[aActivity.stage] = aActivity;
  }
};

//////////////////////////////////////////////////////////////////////////
// HUDWindowObserver
//////////////////////////////////////////////////////////////////////////

HUDWindowObserver = {
  QueryInterface: XPCOMUtils.generateQI(
    [Ci.nsIObserver,]
  ),

  init: function HWO_init()
  {
    Services.obs.addObserver(this, "xpcom-shutdown", false);
    Services.obs.addObserver(this, "content-document-global-created", false);
  },

  observe: function HWO_observe(aSubject, aTopic, aData)
  {
    if (aTopic == "content-document-global-created") {
      HUDService.windowInitializer(aSubject);
    }
    else if (aTopic == "xpcom-shutdown") {
      this.uninit();
    }
  },

  uninit: function HWO_uninit()
  {
    Services.obs.removeObserver(this, "content-document-global-created");
    HUDService.shutdown();
  },

  /**
   * once an initial console is created set this to true so we don't
   * over initialize
   */
  initialConsoleCreated: false,
};

///////////////////////////////////////////////////////////////////////////////
// HUDConsoleObserver
///////////////////////////////////////////////////////////////////////////////

/**
 * HUDConsoleObserver: Observes nsIConsoleService for global consoleMessages,
 * if a message originates inside a contentWindow we are tracking,
 * then route that message to the HUDService for logging.
 */

HUDConsoleObserver = {
  QueryInterface: XPCOMUtils.generateQI(
    [Ci.nsIObserver]
  ),

  init: function HCO_init()
  {
    Services.console.registerListener(this);
    Services.obs.addObserver(this, "xpcom-shutdown", false);
  },

  observe: function HCO_observe(aSubject, aTopic, aData)
  {
    if (aTopic == "xpcom-shutdown") {
      Services.console.unregisterListener(this);
    }

    if (aSubject instanceof Ci.nsIScriptError) {
      switch (aSubject.category) {
        case "XPConnect JavaScript":
        case "component javascript":
        case "chrome javascript":
          // we ignore these CHROME-originating errors as we only
          // care about content
          return;
        case "HUDConsole":
        case "CSS Parser":
        case "content javascript":
          HUDService.reportConsoleServiceContentScriptError(aSubject);
          return;
        default:
          HUDService.reportConsoleServiceMessage(aSubject);
          return;
      }
    }
  }
};

///////////////////////////////////////////////////////////////////////////
// appName
///////////////////////////////////////////////////////////////////////////

/**
 * Get the app's name so we can properly dispatch app-specific
 * methods per API call
 * @returns Gecko application name
 */
function appName()
{
  let APP_ID = Services.appinfo.QueryInterface(Ci.nsIXULRuntime).ID;

  let APP_ID_TABLE = {
    "{ec8030f7-c20a-464f-9b0e-13a3a9e97384}": "FIREFOX" ,
    "{3550f703-e582-4d05-9a08-453d09bdfdc6}": "THUNDERBIRD",
    "{a23983c0-fd0e-11dc-95ff-0800200c9a66}": "FENNEC" ,
    "{92650c4d-4b8e-4d2a-b7eb-24ecf4f6b63a}": "SEAMONKEY",
  };

  let name = APP_ID_TABLE[APP_ID];

  if (name){
    return name;
  }
  throw new Error("appName: UNSUPPORTED APPLICATION UUID");
}

///////////////////////////////////////////////////////////////////////////
// HUDService (exported symbol)
///////////////////////////////////////////////////////////////////////////

try {
  // start the HUDService
  // This is in a try block because we want to kill everything if
  // *any* of this fails
  var HUDService = new HUD_SERVICE();
  HUDWindowObserver.init();
  HUDConsoleObserver.init();
}
catch (ex) {
  Cu.reportError("HUDService failed initialization.\n" + ex);
  // TODO: kill anything that may have started up
  // see bug 568665
}
