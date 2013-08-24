/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is Places Frontend Code.
 *
 * The Initial Developer of the Original Code is
 * Google Inc.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Annie Sullivan <annie.sullivan@gmail.com>
 *   Ben Goodger <beng@google.com>
 *   Myk Melez <myk@mozilla.org>
 *   Marco Bonardo <mak77@bonardo.net>
 *   Asaf Romano <mano@mozilla.com>
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

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

/**
 * The base view implements everything that's common to the toolbar and
 * menu views.
 */
function PlacesViewBase(aPlace) {
  this.place = aPlace;
  this._controller = new PlacesController(this);
  this._viewElt.controllers.appendController(this._controller);
}

PlacesViewBase.prototype = {
  // The xul element that holds the entire view.
  _viewElt: null,
  get viewElt() this._viewElt,

  // The xul element that represents the root container.
  _rootElt: null,

  // Set to true for views that are represented by native widgets (i.e.
  // the native mac menu).
  _nativeView: false,

  QueryInterface: XPCOMUtils.generateQI(
    [Components.interfaces.nsINavHistoryResultObserver,
     Components.interfaces.nsISupportsWeakReference]),

  _place: "",
  get place() this._place,
  set place(val) {
    this._place = val;

    let history = PlacesUtils.history;
    let queries = { }, options = { };
    history.queryStringToQueries(val, queries, { }, options);
    if (!queries.value.length)
      queries.value = [history.getNewQuery()];

    let result = history.executeQueries(queries.value, queries.value.length,
                                        options.value);
    result.addObserver(this, false);
    return val;
  },

  _result: null,
  get result() this._result,
  set result(val) {
    if (this._result == val)
      return;

    if (this._result) {
      this._result.removeObserver(this);
      this._resultNode.containerOpen = false;
    }

    if (this._rootElt.localName == "menupopup")
      this._rootElt._built = false;

    this._result = val;
    if (val) {
      this._resultNode = val.root;
      this._rootElt._placesNode = this._resultNode;
      this._resultNode._DOMElement = this._rootElt;

      // This calls _rebuild through invalidateContainer.
      this._resultNode.containerOpen = true;
    }
    else {
      this._resultNode = null;
    }

    return val;
  },

  get controller() this._controller,

  get selType() "single",
  selectItems: function() { },
  selectAll: function() { },

  get selectedNode() {
    if (this._contextMenuShown) {
      let popup = document.popupNode;
      return popup._placesNode || popup.parentNode._placesNode || null;
    }
    return null;
  },

  get hasSelection() this.selectedNode != null,

  get selectedNodes() {
    let selectedNode = this.selectedNode;
    return selectedNode ? [selectedNode] : [];
  },

  get removableSelectionRanges() {
    // On static content the current selectedNode would be the selection's
    // parent node. We don't want to allow removing a node when the
    // selection is not explicit.
    if (document.popupNode &&
        (document.popupNode == "menupopup" || !document.popupNode._placesNode))
      return [];

    return [this.selectedNodes];
  },

  get draggableSelection() [this._draggedElt],

  get insertionPoint() {
    // There is no insertion point for history queries, so bail out now and
    // save a lot of work when updating commands.
    let resultNode = this._resultNode;
    if (PlacesUtils.nodeIsQuery(resultNode) &&
        PlacesUtils.asQuery(resultNode).queryOptions.queryType ==
          Ci.nsINavHistoryQueryOptions.QUERY_TYPE_HISTORY)
      return null;

    // By default, the insertion point is at the top level, at the end.
    let index = PlacesUtils.bookmarks.DEFAULT_INDEX;
    let container = this._resultNode;
    let orientation = Ci.nsITreeView.DROP_BEFORE;
    let isTag = false;

    let selectedNode = this.selectedNode;
    if (selectedNode) {
      let popup = document.popupNode;
      if (!popup._placesNode || popup._placesNode == this._resultNode) {
        // If a static menuitem is selected, or if the root node is selected,
        // the insertion point is inside the folder, at the end.
        container = selectedNode;
        orientation = Ci.nsITreeView.DROP_ON;
      }
      else {
        // In all other cases the insertion point is before that node.
        container = selectedNode.parent;
        index = container.getChildIndex(selectedNode);
        isTag = PlacesUtils.nodeIsTagQuery(container);
      }
    }

    if (PlacesControllerDragHelper.disallowInsertion(container))
      return null;

    return new InsertionPoint(PlacesUtils.getConcreteItemId(container),
                              index, orientation, isTag);
  },

  buildContextMenu: function PVB_buildContextMenu(aPopup) {
    this._contextMenuShown = true;
    window.updateCommands("places");
    return this.controller.buildContextMenu(aPopup);
  },

  destroyContextMenu: function PVB_destroyContextMenu(aPopup) {
    this._contextMenuShown = false;
    if (window.content)
      window.content.focus();
  },

  _cleanPopup: function PVB_cleanPopup(aPopup) {
    // Remove places popup children and update markers to keep track of
    // their indices.
    let start = aPopup._startMarker != -1 ? aPopup._startMarker + 1 : 0;
    let end = aPopup._endMarker != -1 ? aPopup._endMarker :
                                        aPopup.childNodes.length;
    let items = [];
    let placesNodeFound = false;
    for (let i = start; i < end; ++i) {
      let item = aPopup.childNodes[i];
      if (item.getAttribute("builder") == "end") {
        // we need to do this for menus that have static content at the end but
        // are initially empty, eg. the history menu, we need to know where to
        // start inserting new items.
        aPopup._endMarker = i;
        break;
      }
      if (item._placesNode) {
        items.push(item);
        placesNodeFound = true;
      }
      else {
        // This is static content...
        if (!placesNodeFound)
          // ...at the start of the popup
          // Initialized in menu.xml, in the base binding
          aPopup._startMarker++;
        else {
          // ...after places nodes
          aPopup._endMarker = i;
          break;
        }
      }
    }

    for (let i = 0; i < items.length; ++i) {
      aPopup.removeChild(items[i]);
      if (aPopup._endMarker != -1)
        aPopup._endMarker--;
    }
  },

  _rebuildPopup: function PVB__rebuildPopup(aPopup) {
    this._cleanPopup(aPopup);

    // If this is a livemark container check if the status menuitem has
    // to be added or removed.
    if (PlacesUtils.nodeIsLivemarkContainer(aPopup._placesNode))
      this._ensureLivemarkStatusMenuItem(aPopup);

    let resultNode = aPopup._placesNode;
    if (!resultNode.containerOpen)
      return;

    let cc = resultNode.childCount;
    if (cc > 0) {
      aPopup.removeAttribute("emptyplacesresult");
      if (aPopup._emptyMenuItem)
        aPopup._emptyMenuItem.hidden = true;

      for (let i = 0; i < cc; ++i) {
        let child = resultNode.getChild(i);
        this._insertNewItemToPopup(child, aPopup, null);
      }
    }
    else {
      aPopup.setAttribute("emptyplacesresult", "true");
      // This menu is empty.  If there is no static content, add
      // an element to show it is empty.
      if (aPopup._startMarker == -1 && aPopup._endMarker == -1)
        this._showEmptyMenuItem(aPopup);
    }
    aPopup._built = true;
  },

  _removeChild: function PVB__removeChild(aChild) {
    // If document.popupNode pointed to this child, null it out,
    // otherwise controller's command-updating may rely on the removed
    // item still being "selected".
    if (document.popupNode == aChild)
      document.popupNode = null;

    aChild.parentNode.removeChild(aChild);
  },

  _showEmptyMenuItem: function PVB__showEmptyMenuItem(aPopup) {
    if (aPopup._emptyMenuItem) {
      aPopup._emptyMenuItem.hidden = false;
      return;
    }

    let label = PlacesUIUtils.getString("bookmarksMenuEmptyFolder");
    aPopup._emptyMenuItem = document.createElement("menuitem");
    aPopup._emptyMenuItem.setAttribute("label", label);
    aPopup._emptyMenuItem.setAttribute("disabled", true);
    aPopup.appendChild(aPopup._emptyMenuItem);
  },

  _createMenuItemForPlacesNode:
  function PVB__createMenuItemForPlacesNode(aPlacesNode) {
    let element;
    let type = aPlacesNode.type;
    if (type == Ci.nsINavHistoryResultNode.RESULT_TYPE_SEPARATOR)
      element = document.createElement("menuseparator");
    else {
      if (PlacesUtils.uriTypes.indexOf(type) != -1) {
        element = document.createElement("menuitem");
        element.className = "menuitem-iconic bookmark-item menuitem-with-favicon";
        element.setAttribute("scheme",
                             PlacesUIUtils.guessUrlSchemeForUI(aPlacesNode.uri));
      }
      else if (PlacesUtils.containerTypes.indexOf(type) != -1) {
        element = document.createElement("menu");
        element.setAttribute("container", "true");

        if (aPlacesNode.type == Ci.nsINavHistoryResultNode.RESULT_TYPE_QUERY) {
          element.setAttribute("query", "true");
          if (PlacesUtils.nodeIsTagQuery(aPlacesNode))
            element.setAttribute("tagContainer", "true");
          else if (PlacesUtils.nodeIsDay(aPlacesNode))
            element.setAttribute("dayContainer", "true");
          else if (PlacesUtils.nodeIsHost(aPlacesNode))
            element.setAttribute("hostContainer", "true");
        }
        else if (aPlacesNode.itemId != -1) {
          if (PlacesUtils.nodeIsLivemarkContainer(aPlacesNode))
            element.setAttribute("livemark", "true");
        }

        let popup = document.createElement("menupopup");
        popup._placesNode = PlacesUtils.asContainer(aPlacesNode);
        if (this._nativeView) {
          popup._startMarker = -1;
          popup._endMarker = -1;
        }
        else
          popup.setAttribute("placespopup", "true");
#ifdef XP_MACOSX
        // No context menu on mac.
        popup.setAttribute("context", "placesContext");
#endif
        element.appendChild(popup);
        element.className = "menu-iconic bookmark-item";

        aPlacesNode._DOMElement = popup;
      }
      else
        throw "Unexpected node";

      element.setAttribute("label", PlacesUIUtils.getBestTitle(aPlacesNode));

      let icon = aPlacesNode.icon;
      if (icon)
        element.setAttribute("image", icon);
    }

    element._placesNode = aPlacesNode;
    if (!aPlacesNode._DOMElement)
      aPlacesNode._DOMElement = element;

    return element;
  },

  _insertNewItemToPopup:
  function PVB__insertNewItemToPopup(aNewChild, aPopup, aBefore) {
    let element = this._createMenuItemForPlacesNode(aNewChild);

    if (aBefore) {
      aPopup.insertBefore(element, aBefore);
    }
    else {
      // Add the new element to the menu.  If there is static content at
      // the end of the menu, add the element before that.  Otherwise,
      // just add to the end.
      if (aPopup._endMarker != -1) {
        let lastElt = aPopup.childNodes[aPopup._endMarker];
        aPopup.insertBefore(element, lastElt);
      }
      else {
        aPopup.appendChild(element);
      }
    }

    if (aPopup._endMarker != -1)
      aPopup._endMarker++;

    return element;
  },

  /**
   * Add, update or remove the livemark status menuitem.
   * @param aPopup
   *        The livemark container popup
   */
  _ensureLivemarkStatusMenuItem:
  function PVB_ensureLivemarkStatusMenuItem(aPopup) {
    let itemId = aPopup._placesNode.itemId;
    let as = PlacesUtils.annotations;

    let lmStatus = null;
    if (as.itemHasAnnotation(itemId, "livemark/loadfailed"))
      lmStatus = "bookmarksLivemarkFailed";
    else if (as.itemHasAnnotation(itemId, "livemark/loading"))
      lmStatus = "bookmarksLivemarkLoading";

    let lmStatusElt = aPopup._lmStatusMenuItem;
    if (lmStatus && !lmStatusElt) {
      // Create the status menuitem and cache it in the popup object.
      lmStatusElt = document.createElement("menuitem");
      lmStatusElt.setAttribute("lmStatus", lmStatus);
      lmStatusElt.setAttribute("label", PlacesUIUtils.getString(lmStatus));
      lmStatusElt.setAttribute("disabled", true);
      aPopup.insertBefore(lmStatusElt,
                          aPopup.childNodes.item(aPopup._startMarker + 1));
      aPopup._lmStatusMenuItem = lmStatusElt;
      aPopup._startMarker++;
    }
    else if (lmStatus && lmStatusElt.getAttribute("lmStatus") != lmStatus) {
      // Status has changed, update the cached status menuitem.
      lmStatusElt.setAttribute("label", this.getString(lmStatus));
    }
    else if (!lmStatus && lmStatusElt) {
      // No status, remove the cached menuitem.
      aPopup.removeChild(aPopup._lmStatusMenuItem);
      aPopup._lmStatusMenuItem = null;
      aPopup._startMarker--;
    }
  },

  nodeURIChanged: function PVB_nodeURIChanged(aPlacesNode, aURIString) {
    let elt = aPlacesNode._DOMElement;
    if (!elt)
      throw "aPlacesNode must have _DOMElement set";

    // Here we need the <menu>.
    if (elt.localName == "menupopup")
      elt = elt.parentNode;

    elt.setAttribute("scheme", PlacesUIUtils.guessUrlSchemeForUI(aURIString));
  },

  nodeIconChanged: function PT_nodeIconChanged(aPlacesNode) {
    let elt = aPlacesNode._DOMElement;
    if (!elt)
      throw "aPlacesNode must have _DOMElement set";

    // There's no UI representation for the root node, thus there's nothing to
    // be done when the icon changes.
    if (elt == this._rootElt)
      return;

    // Here we need the <menu>.
    if (elt.localName == "menupopup")
      elt = elt.parentNode;

    let icon = aPlacesNode.icon;
    if (!icon)
      elt.removeAttribute("image");
    else if (icon != elt.getAttribute("image"))
      elt.setAttribute("image", icon);
  },

  nodeAnnotationChanged:
  function PVB_nodeAnnotationChanged(aPlacesNode, aAnno) {
    // Ensure the changed annotation is a livemark one.
    if (/^livemark\//.test(aAnno) &&
        PlacesUtils.nodeIsLivemarkContainer(aPlacesNode)) {
      let elt = aPlacesNode._DOMElement;
      if (!elt)
        throw "aPlacesNode must have _DOMElement set";

      let menu = elt.parentNode;
      if (!menu.hasAttribute("livemark"))
        menu.setAttribute("livemark", "true");

      // Add or remove the livemark status menuitem.
      this._ensureLivemarkStatusMenuItem(elt);
    }
  },

  nodeTitleChanged:
  function PM_nodeTitleChanged(aPlacesNode, aNewTitle) {
    let elt = aPlacesNode._DOMElement;
    if (!elt)
      throw "aPlacesNode must have _DOMElement set";

    // There's no UI representation for the root node, thus there's
    // nothing to be done when the title changes.
    if (elt == this._rootElt)
      return;

    // Here we need the <menu>.
    if (elt.localName == "menupopup")
      elt = elt.parentNode;

    if (!aNewTitle && elt.localName != "toolbarbutton") {
      // Many users consider toolbars as shortcuts containers, so explicitly
      // allow empty labels on toolbarbuttons.  For any other element try to be
      // smarter, guessing a title from the uri.
      elt.label = PlacesUIUtils.getBestTitle(aPlacesNode);
    }
    else {
      elt.label = aNewTitle;
    }
  },

  nodeRemoved:
  function PVB_nodeRemoved(aParentPlacesNode, aPlacesNode, aIndex) {
    let parentElt = aParentPlacesNode._DOMElement;
    let elt = aPlacesNode._DOMElement;

    if (!parentElt)
      throw "aParentPlacesNode must have _DOMElement set";
    if (!elt)
      throw "aPlacesNode must have _DOMElement set";

    // Here we need the <menu>.
    if (elt.localName == "menupopup")
      elt = elt.parentNode;

    if (parentElt._built) {
      parentElt.removeChild(elt);

      // Figure out if we need to show the "<Empty>" menu-item.
      // TODO Bug 517701: This doesn't seem to handle the case of an empty
      // root.
      if (!parentElt.hasChildNodes() ||
          (parentElt.childNodes.length == 1 &&
          parentElt.firstChild == parentElt._emptyMenuItem))
        this._showEmptyMenuItem(parentElt);

      if (parentElt._endMarker != -1)
        parentElt._endMarker--;
    }
  },

  nodeReplaced:
  function PBV_nodeReplaced(aParentPlacesNode, aOldPlacesNode, aNewPlacesNode, aIndex) {
    let parentElt = aParentPlacesNode._DOMElement;
    if (!parentElt)
      throw "aParentPlacesNode node must have _DOMElement set";

    if (parentElt._built) {
      let elt = aOldPlacesNode._DOMElement;
      if (!elt)
        throw "aOldPlacesNode must have _DOMElement set";

      // Here we need the <menu>.
      if (elt.localName == "menupopup")
        elt = elt.parentNode;

      parentElt.removeChild(elt);

      // No worries: If elt is the last item (i.e. no nextSibling),
      // _insertNewItem/_insertNewItemToPopup will insert the new element as
      // the last item.
      let nextElt = elt.nextSibling;
      this._insertNewItemToPopup(aNewPlacesNode, parentElt, nextElt);
    }
  },

  nodeHistoryDetailsChanged: function() { },
  nodeTagsChanged: function() { },
  nodeDateAddedChanged: function() { },
  nodeLastModifiedChanged: function() { },
  nodeKeywordChanged: function() { },
  sortingChanged: function() { },
  // Replaced by containerStateChanged.
  containerOpened: function() { },
  containerClosed: function() { },

  nodeInserted:
  function PVB_nodeInserted(aParentPlacesNode, aPlacesNode, aIndex) {
    let parentElt = aParentPlacesNode._DOMElement;
    if (!parentElt)
      throw "aParentPlacesNode node must have _DOMElement set";

    if (!parentElt._built)
      return;

    let index = parentElt._startMarker + 1 + aIndex;
    this._insertNewItemToPopup(aPlacesNode, parentElt,
                               parentElt.childNodes[index]);
    if (parentElt._emptyMenuItem)
      parentElt._emptyMenuItem.hidden = true;
  },

  nodeMoved:
  function PBV_nodeMoved(aPlacesNode,
                         aOldParentPlacesNode, aOldIndex,
                         aNewParentPlacesNode, aNewIndex) {
    // Note: the current implementation of moveItem does not actually
    // use this notification when the item in question is moved from one
    // folder to another.  Instead, it calls nodeRemoved and nodeInserted
    // for the two folders.  Thus, we can assume old-parent == new-parent.
    let elt = aPlacesNode._DOMElement;
    if (!elt)
      throw "aPlacesNode must have _DOMElement set";

    // Here we need the <menu>.
    if (elt.localName == "menupopup")
      elt = elt.parentNode;

    // If our root node is a folder, it might be moved. There's nothing
    // we need to do in that case.
    if (elt == this._rootElt)
      return;

    let parentElt = aNewParentPlacesNode._DOMElement;
    if (!parentElt)
      throw "aNewParentPlacesNode node must have _DOMElement set";

    if (parentElt._built) {
      // Move the node.
      parentElt.removeChild(elt);
      let index = parentElt._startMarker + 1 + aNewIndex;
      parentElt.insertBefore(elt, parentElt.childNodes[index]);
    }
  },

  containerStateChanged:
  function PVB_containerStateChanged(aPlacesNode, aOldState, aNewState) {
    if (aNewState == Ci.nsINavHistoryContainerResultNode.STATE_OPENED ||
        aNewState == Ci.nsINavHistoryContainerResultNode.STATE_CLOSED) {
      this.invalidateContainer(aPlacesNode);
    }
    else {
      throw "Unexpected state passed to containerStateChanged";
    }
  },

  invalidateContainer: function PVB_invalidateContainer(aPlacesNode) {
    let elt = aPlacesNode._DOMElement;
    if (!elt)
      throw "aPlacesNode must have _DOMElement set";

    elt._built = false;

    // If the menupopup is open we should live-update it.
    if (elt.parentNode.open)
      this._rebuildPopup(elt);
  },

  uninit: function PVB_uninit() {
    if (this._result) {
      this._result.removeObserver(this);
      this._resultNode.containerOpen = false;
      this._resultNode = null;
      this._result = null;
    }

    delete this._viewElt._placesView;
  },

  get isRTL() {
    if ("_isRTL" in this)
      return this._isRTL;

    return this._isRTL = document.defaultView.getComputedStyle(this, "")
                                 .direction == "rtl"
  },

  /**
   * Adds an "Open All in Tabs" menuitem to the bottom of the popup.
   * @param aPopup
   *        a Places popup.
   */
  _mayAddCommandsItems: function PVB__mayAddCommandsItems(aPopup) {
    // The command items are never added to the root popup.
    if (aPopup == this._rootElt)
      return;

    // Check if the popup contains at least 2 menuitems with places nodes
    let numURINodes = 0;
    let currentChild = aPopup.firstChild;
    while (currentChild) {
      if (currentChild.localName == "menuitem" && currentChild._placesNode) {
        if (++numURINodes == 2)
          break;
      }
      currentChild = currentChild.nextSibling;
    }

    let hasMultipleURIs = numURINodes > 1;
    let itemId = aPopup._placesNode.itemId;
    let siteURIString = "";
    if (itemId != -1 && PlacesUtils.itemIsLivemark(itemId)) {
      let siteURI = PlacesUtils.livemarks.getSiteURI(itemId);
      if (siteURI)
        siteURIString = siteURI.spec;
    }

    if (!siteURIString && aPopup._endOptOpenSiteURI) {
      aPopup.removeChild(aPopup._endOptOpenSiteURI);
      aPopup._endOptOpenSiteURI = null;
    }

    if (!hasMultipleURIs && aPopup._endOptOpenAllInTabs) {
      aPopup.removeChild(aPopup._endOptOpenAllInTabs);
      aPopup._endOptOpenAllInTabs = null;
    }

    if (!(hasMultipleURIs || siteURIString)) {
      // We don't have to show any option.
      if (aPopup._endOptSeparator) {
        aPopup.removeChild(aPopup._endOptSeparator);
        aPopup._endOptSeparator = null;
        aPopup._endMarker = -1;
      }
      return;
    }

    if (!aPopup._endOptSeparator) {
      // Create a separator before options.
      aPopup._endOptSeparator = document.createElement("menuseparator");
      aPopup._endOptSeparator.className = "bookmarks-actions-menuseparator";
      aPopup._endMarker = aPopup.childNodes.length;
      aPopup.appendChild(aPopup._endOptSeparator);
    }

    if (siteURIString && !aPopup._endOptOpenSiteURI) {
      // Add "Open (Feed Name)" menuitem if it's a livemark with a siteURI.
      aPopup._endOptOpenSiteURI = document.createElement("menuitem");
      aPopup._endOptOpenSiteURI.className = "openlivemarksite-menuitem";
      aPopup._endOptOpenSiteURI.setAttribute("targetURI", siteURIString);
      aPopup._endOptOpenSiteURI.setAttribute("oncommand",
          "openUILink(this.getAttribute('targetURI'), event);");

      // If a user middle-clicks this item we serve the oncommand event
      // We are using checkForMiddleClick because of Bug 246720
      // Note: stopPropagation is needed to avoid serving middle-click
      // with BT_onClick that would open all items in tabs.
      aPopup._endOptOpenSiteURI.setAttribute("onclick",
          "checkForMiddleClick(this, event); event.stopPropagation();");
      aPopup._endOptOpenSiteURI.setAttribute("label",
          PlacesUIUtils.getFormattedString("menuOpenLivemarkOrigin.label",
          [aPopup.parentNode.getAttribute("label")]));
      aPopup.appendChild(aPopup._endOptOpenSiteURI);
    }

    if (hasMultipleURIs && !aPopup._endOptOpenAllInTabs) {
      // Add the "Open All in Tabs" menuitem if there are
      // at least two menuitems with places result nodes.
      aPopup._endOptOpenAllInTabs = document.createElement("menuitem");
      aPopup._endOptOpenAllInTabs.className = "openintabs-menuitem";
      aPopup._endOptOpenAllInTabs.setAttribute("oncommand",
        "PlacesUIUtils.openContainerNodeInTabs(this.parentNode._placesNode, event);");
      aPopup._endOptOpenAllInTabs.setAttribute("onclick",
        "checkForMiddleClick(this, event); event.stopPropagation();");
      aPopup._endOptOpenAllInTabs.setAttribute("label",
        gNavigatorBundle.getString("menuOpenAllInTabs.label"));
      aPopup.appendChild(aPopup._endOptOpenAllInTabs);
    }
  },

  _onPopupShowing: function PVB__onPopupShowing(aEvent) {
    // Avoid handling popupshowing of inner views.
    let popup = aEvent.originalTarget;
    if (popup._placesNode && PlacesUIUtils.getViewForNode(popup) == this) {
      if (!popup._placesNode.containerOpen)
        popup._placesNode.containerOpen = true;
      if (!popup._built)
        this._rebuildPopup(popup);

      this._mayAddCommandsItems(popup);
    }
  },

  _addEventListeners:
  function PVB__addEventListeners(aObject, aEventNames, aCapturing) {
    for (let i = 0; i < aEventNames.length; i++) {
      aObject.addEventListener(aEventNames[i], this, aCapturing);
    }
  },

  _removeEventListeners:
  function PVB__removeEventListeners(aObject, aEventNames, aCapturing) {
    for (let i = 0; i < aEventNames.length; i++) {
      aObject.removeEventListener(aEventNames[i], this, aCapturing);
    }
  },
};

function PlacesToolbar(aPlace) {
  // Add some smart getters for our elements.
  let thisView = this;
  [
    ["_viewElt",              "PlacesToolbar"],
    ["_rootElt",              "PlacesToolbarItems"],
    ["_dropIndicator",        "PlacesToolbarDropIndicator"],
    ["_chevron",              "PlacesChevron"],
    ["_chevronPopup",         "PlacesChevronPopup"]
  ].forEach(function (elementGlobal) {
    let [name, id] = elementGlobal;
    thisView.__defineGetter__(name, function () {
      let element = document.getElementById(id);
      if (!element)
        return null;

      delete thisView[name];
      return thisView[name] = element;
    });
  });

  this._viewElt._placesView = this;

  this._addEventListeners(this._viewElt, this._cbEvents, false);
  this._addEventListeners(this._rootElt, ["popupshowing", "popuphidden"], true);
  this._addEventListeners(this._rootElt, ["overflow", "underflow"], true);
  this._addEventListeners(window, ["resize", "unload"], false);

  PlacesViewBase.call(this, aPlace);
}

PlacesToolbar.prototype = {
  __proto__: PlacesViewBase.prototype,

  _cbEvents: ["dragstart", "dragover", "dragleave", "dragend", "drop",
#ifdef XP_UNIX
#ifndef XP_MACOSX
              "mousedown", "mouseup",
#endif
#endif
              "mousemove", "mouseover", "mouseout"],

  QueryInterface: function PT_QueryInterface(aIID) {
    if (aIID.equals(Ci.nsIDOMEventListener) ||
        aIID.equals(Ci.nsITimerCallback))
      return this;

    return PlacesViewBase.prototype.QueryInterface.apply(this, arguments);
  },

  uninit: function PT_uninit() {
    this._removeEventListeners(this._viewElt, this._cbEvents, false);
    this._removeEventListeners(this._rootElt, ["popupshowing", "popuphidden"],
                               true);
    this._removeEventListeners(this._rootElt, ["overflow", "underflow"], true);
    this._removeEventListeners(window, ["resize", "unload"], false);

    PlacesViewBase.prototype.uninit.apply(this, arguments);
  },

  _openedMenuButton: null,
  _allowPopupShowing: true,

  _rebuild: function PT__rebuild() {
    // Clear out references to existing nodes, since they will be removed
    // and re-added.
    if (this._overFolder.elt)
      this._clearOverFolder();

    this._openedMenuButton = null;
    while (this._rootElt.hasChildNodes()) {
      this._rootElt.removeChild(this._rootElt.firstChild);
    }

    let cc = this._resultNode.childCount;
    for (let i = 0; i < cc; ++i) {
      this._insertNewItem(this._resultNode.getChild(i), null);
    }

    if (this._chevronPopup.hasAttribute("type")) {
      // Chevron has already been initialized, but since we are forcing
      // a rebuild of the toolbar, it has to be rebuilt.
      // Otherwise, it will be initialized when the toolbar overflows.
      this._chevronPopup.place = this.place;
    }
  },

  _insertNewItem:
  function PT__insertNewItem(aChild, aBefore) {
    let type = aChild.type;
    let button;
    if (type == Ci.nsINavHistoryResultNode.RESULT_TYPE_SEPARATOR) {
      button = document.createElement("toolbarseparator");
    }
    else {
      button = document.createElement("toolbarbutton");
      button.className = "bookmark-item";
      button.setAttribute("label", aChild.title);
      let icon = aChild.icon;
      if (icon)
        button.setAttribute("image", icon);

      if (PlacesUtils.containerTypes.indexOf(type) != -1) {
        button.setAttribute("type", "menu");
        button.setAttribute("container", "true");

        if (PlacesUtils.nodeIsQuery(aChild)) {
          button.setAttribute("query", "true");
          if (PlacesUtils.nodeIsTagQuery(aChild))
            button.setAttribute("tagContainer", "true");
        }
        else if (PlacesUtils.nodeIsLivemarkContainer(aChild)) {
          button.setAttribute("livemark", "true");
        }

        let popup = document.createElement("menupopup");
        popup.setAttribute("placespopup", "true");
        button.appendChild(popup);
        popup._placesNode = PlacesUtils.asContainer(aChild);
#ifndef XP_MACOSX
        popup.setAttribute("context", "placesContext");
#endif

        aChild._DOMElement = popup;
      }
      else if (PlacesUtils.nodeIsURI(aChild)) {
        button.setAttribute("scheme",
                            PlacesUIUtils.guessUrlSchemeForUI(aChild.uri));
      }
    }

    button._placesNode = aChild;
    if (!aChild._DOMElement)
      aChild._DOMElement = button;

    if (aBefore)
      this._rootElt.insertBefore(button, aBefore);
    else
      this._rootElt.appendChild(button);
  },

  _updateChevronPopupNodesVisibility:
  function PT__updateChevronPopupNodesVisibility() {
    for (let i = 0; i < this._chevronPopup.childNodes.length; i++) {
      this._chevronPopup.childNodes[i].hidden =
        this._rootElt.childNodes[i].style.visibility != "hidden";
    }
  },

  _onChevronPopupShowing:
  function PT__onChevronPopupShowing(aEvent) {
    // Handle popupshowing only for the chevron popup, not for nested ones.
    if (aEvent.target != this._chevronPopup)
      return;

    if (!this._chevron._placesView)
      this._chevron._placesView = new PlacesMenu(aEvent, this.place);

    this._updateChevronPopupNodesVisibility();
  },

  handleEvent: function PT_handleEvent(aEvent) {
    switch (aEvent.type) {
      case "unload":
        this.uninit();
        break;
      case "resize":
        // This handler updates nodes visibility in both the toolbar
        // and the chevron popup when a window resize does not change
        // the overflow status of the toolbar.
        this.updateChevron();
        break;
      case "overflow":
        if (aEvent.target != aEvent.currentTarget)
          return;

        // Ignore purely vertical overflows.
        if (aEvent.detail == 0)
          return;

        // Attach the popup binding to the chevron popup if it has not yet
        // been initialized.
        if (!this._chevronPopup.hasAttribute("type")) {
          this._chevronPopup.setAttribute("place", this.place);
          this._chevronPopup.setAttribute("type", "places");
        }
        this._chevron.collapsed = false;
        this.updateChevron();
        break;
      case "underflow":
        if (aEvent.target != aEvent.currentTarget)
          return;

        // Ignore purely vertical underflows.
        if (aEvent.detail == 0)
          return;

        this._chevron.collapsed = true;
        this.updateChevron();
        break;
      case "dragstart":
        this._onDragStart(aEvent);
        break;
      case "dragover":
        this._onDragOver(aEvent);
        break;
      case "dragleave":
        this._onDragLeave(aEvent);
        break;
      case "dragend":
        this._onDragEnd(aEvent);
        break;
      case "drop":
        this._onDrop(aEvent);
        break;
      case "mouseover":
        this._onMouseOver(aEvent);
        break;
      case "mousemove":
        this._onMouseMove(aEvent);
        break;
      case "mouseout":
        this._onMouseOut(aEvent);
        break;
#ifdef XP_UNIX
#ifndef XP_MACOSX
      case "mouseup":
        this._onMouseUp(aEvent);
        break;
      case "mousedown":
        this._onMouseDown(aEvent);
        break;
#endif
#endif
      case "popupshowing":
        this._onPopupShowing(aEvent);
        break;
      case "popuphidden":
        this._onPopupHidden(aEvent);
        break;
      default:
        throw "Trying to handle unexpected event.";
    }
  },

  updateChevron: function PT_updateChevron() {
    // If the chevron is collapsed there's nothing to update.
    if (this._chevron.collapsed)
      return;

    // XXX (bug 508816) Scrollbox does not handle correctly RTL mode.
    // This workarounds the issue scrolling the box to the right.
    if (this._isRTL)
      this._rootElt.scrollLeft = this._rootElt.scrollWidth;

    // Update the chevron on a timer.  This will avoid repeated work when
    // lot of changes happen in a small timeframe.
    if (this._updateChevronTimer)
      this._updateChevronTimer.cancel();

    this._updateChevronTimer = this._setTimer(100);
  },

  _updateChevronTimerCallback: function PT__updateChevronTimerCallback() {
    let scrollRect = this._rootElt.getBoundingClientRect();
    let childOverflowed = false;
    for (let i = 0; i < this._rootElt.childNodes.length; i++) {
      let child = this._rootElt.childNodes[i];
      // Once a child overflows, all the next ones will.
      if (!childOverflowed) {
        let childRect = child.getBoundingClientRect();
        childOverflowed = this._isRTL ? (childRect.left < scrollRect.left)
                                      : (childRect.right > scrollRect.right);
      }
      child.style.visibility = childOverflowed ? "hidden" : "visible";
    }

    // We rebuild the chevron on popupShowing, so if it is open
    // we must update it.
    if (this._chevron.open)
      this._updateChevronPopupNodesVisibility();
  },

  nodeInserted:
  function PT_nodeInserted(aParentPlacesNode, aPlacesNode, aIndex) {
    let parentElt = aParentPlacesNode._DOMElement;
    if (!parentElt) 
      throw "aParentPlacesNode node must have _DOMElement set";

    if (parentElt == this._rootElt) {
      let children = this._rootElt.childNodes;
      this._insertNewItem(aPlacesNode,
        aIndex < children.length ? children[aIndex] : null);
      this.updateChevron();
      return;
    }

    PlacesViewBase.prototype.nodeInserted.apply(this, arguments);
  },

  nodeRemoved:
  function PT_nodeRemoved(aParentPlacesNode, aPlacesNode, aIndex) {
    let parentElt = aParentPlacesNode._DOMElement;
    let elt = aPlacesNode._DOMElement;

    if (!parentElt)
      throw "aParentPlacesNode node must have _DOMElement set";
    if (!elt)
      throw "aPlacesNode must have _DOMElement set";

    // Here we need the <menu>.
    if (elt.localName == "menupopup")
      elt = elt.parentNode;

    if (parentElt == this._rootElt) {
      this._removeChild(elt);
      this.updateChevron();
      return;
    }

    PlacesViewBase.prototype.nodeRemoved.apply(this, arguments);
  },

  nodeMoved:
  function PT_nodeMoved(aPlacesNode,
                        aOldParentPlacesNode, aOldIndex,
                        aNewParentPlacesNode, aNewIndex) {
    let parentElt = aNewParentPlacesNode._DOMElement;
    if (!parentElt) 
      throw "aNewParentPlacesNode node must have _DOMElement set";

    if (parentElt == this._rootElt) {
      // Container is on the toolbar.

      // Move the element.
      let elt = aPlacesNode._DOMElement;
      if (!elt)
        throw "aPlacesNode must have _DOMElement set";

      // Here we need the <menu>.
      if (elt.localName == "menupopup")
        elt = elt.parentNode;

      this._removeChild(elt);
      this._rootElt.insertBefore(elt, this._rootElt.childNodes[aNewIndex]);

      // If the chevron popup is open, keep it in sync.
      if (this._chevron.open) {
        let chevronPopup = this._chevronPopup;
        let menuitem = chevronPopup.childNodes[aOldIndex];
        chevronPopup.removeChild(menuitem);
        chevronPopup.insertBefore(menuitem,
                                  chevronPopup.childNodes[aNewIndex]);
      }
      this.updateChevron();
      return;
    }

    PlacesViewBase.prototype.nodeMoved.apply(this, arguments);
  },

  nodeTitleChanged: function PT_nodeTitleChanged(aPlacesNode, aNewTitle) {
    let elt = aPlacesNode._DOMElement;
    if (!elt)
      throw "aPlacesNode must have _DOMElement set";

    // There's no UI representation for the root node, thus there's
    // nothing to be done when the title changes.
    if (elt == this._rootElt)
      return;

    PlacesViewBase.prototype.nodeTitleChanged.apply(this, arguments);

    // Here we need the <menu>.
    if (elt.localName == "menupopup")
      elt = elt.parentNode;

    if (elt.parentNode == this._rootElt) {
      // Node is on the toolbar
      this.updateChevron();
    }
  },

  nodeReplaced:
  function PT_nodeReplaced(aParentPlacesNode,
                           aOldPlacesNode, aNewPlacesNode, aIndex) {
    let parentElt = aParentPlacesNode._DOMElement;
    if (!parentElt) 
      throw "aParentPlacesNode node must have _DOMElement set";

    if (parentElt == this._rootElt) {
      let elt = aOldPlacesNode._DOMElement;
      if (!elt)
        throw "aOldPlacesNode must have _DOMElement set";

      // Here we need the <menu>.
      if (elt.localName == "menupopup")
        elt = elt.parentNode;

      this._removeChild(elt);

      // No worries: If elt is the last item (i.e. no nextSibling),
      // _insertNewItem/_insertNewItemToPopup will insert the new element as
      // the last item.
      let next = elt.nextSibling;
      this._insertNewItem(aNewPlacesNode, next);
      this.updateChevron();
      return;
    }

    PlacesViewBase.prototype.nodeReplaced.apply(this, arguments);
  },

  invalidateContainer: function PT_invalidateContainer(aPlacesNode) {
    let elt = aPlacesNode._DOMElement;
    if (!elt)
      throw "aPlacesNode must have _DOMElement set";

    if (elt == this._rootElt) {
      // Container is the toolbar itself.
      this._rebuild();
      return;
    }

    PlacesViewBase.prototype.invalidateContainer.apply(this, arguments);
  },

  _overFolder: { elt: null,
                 openTimer: null,
                 hoverTime: 350,
                 closeTimer: null },

  _clearOverFolder: function PT__clearOverFolder() {
    // The mouse is no longer dragging over the stored menubutton.
    // Close the menubutton, clear out drag styles, and clear all
    // timers for opening/closing it.
    if (this._overFolder.elt && this._overFolder.elt.lastChild) {
      if (!this._overFolder.elt.lastChild.hasAttribute("dragover")) {
        this._overFolder.elt.lastChild.hidePopup();
      }
      this._overFolder.elt.removeAttribute("dragover");
      this._overFolder.elt = null;
    }
    if (this._overFolder.openTimer) {
      this._overFolder.openTimer.cancel();
      this._overFolder.openTimer = null;
    }
    if (this._overFolder.closeTimer) {
      this._overFolder.closeTimer.cancel();
      this._overFolder.closeTimer = null;
    }
  },

  /**
   * This function returns information about where to drop when dragging over
   * the toolbar.  The returned object has the following properties:
   * - ip: the insertion point for the bookmarks service.
   * - beforeIndex: child index to drop before, for the drop indicator.
   * - folderElt: the folder to drop into, if applicable.
   */
  _getDropPoint: function PT__getDropPoint(aEvent) {
    let result = this.result;
    if (!PlacesUtils.nodeIsFolder(this._resultNode))
      return null;

    let dropPoint = { ip: null, beforeIndex: null, folderElt: null };
    let elt = aEvent.target;
    if (elt._placesNode && elt != this._rootElt &&
        elt.localName != "menupopup") {
      let eltRect = elt.getBoundingClientRect();
      let eltIndex = Array.indexOf(this._rootElt.childNodes, elt);
      if (PlacesUtils.nodeIsFolder(elt._placesNode) &&
          !PlacesUtils.nodeIsReadOnly(elt._placesNode)) {
        // This is a folder.
        // If we are in the middle of it, drop inside it.
        // Otherwise, drop before it, with regards to RTL mode.
        let threshold = eltRect.width * 0.25;
        if (this._isRTL ? (aEvent.clientX > eltRect.right - threshold)
                        : (aEvent.clientX < eltRect.left + threshold)) {
          // Drop before this folder.
          dropPoint.ip =
            new InsertionPoint(PlacesUtils.getConcreteItemId(this._resultNode),
                               eltIndex, Ci.nsITreeView.DROP_BEFORE);
          dropPoint.beforeIndex = eltIndex;
        }
        else if (this._isRTL ? (aEvent.clientX > eltRect.left + threshold)
                             : (aEvent.clientX < eltRect.right - threshold)) {
          // Drop inside this folder.
          dropPoint.ip =
            new InsertionPoint(PlacesUtils.getConcreteItemId(elt._placesNode),
                               -1, Ci.nsITreeView.DROP_ON,
                               PlacesUtils.nodeIsTagQuery(elt._placesNode));
          dropPoint.beforeIndex = eltIndex;
          dropPoint.folderElt = elt;
        }
        else {
          // Drop after this folder.
          let beforeIndex =
            (eltIndex == this._rootElt.childNodes.length - 1) ?
            -1 : eltIndex + 1;

          dropPoint.ip =
            new InsertionPoint(PlacesUtils.getConcreteItemId(this._resultNode),
                               beforeIndex, Ci.nsITreeView.DROP_BEFORE);
          dropPoint.beforeIndex = beforeIndex;
        }
      }
      else {
        // This is a non-folder node or a read-only folder.
        // Drop before it with regards to RTL mode.
        let threshold = eltRect.width * 0.5;
        if (this._isRTL ? (aEvent.clientX > eltRect.left + threshold)
                        : (aEvent.clientX < eltRect.left + threshold)) {
          // Drop before this bookmark.
          dropPoint.ip =
            new InsertionPoint(PlacesUtils.getConcreteItemId(this._resultNode),
                               eltIndex, Ci.nsITreeView.DROP_BEFORE);
          dropPoint.beforeIndex = eltIndex;
        }
        else {
          // Drop after this bookmark.
          let beforeIndex =
            eltIndex == this._rootElt.childNodes.length - 1 ?
            -1 : eltIndex + 1;
          dropPoint.ip =
            new InsertionPoint(PlacesUtils.getConcreteItemId(this._resultNode),
                               beforeIndex, Ci.nsITreeView.DROP_BEFORE);
          dropPoint.beforeIndex = beforeIndex;
        }
      }
    }
    else {
      // We are most likely dragging on the empty area of the
      // toolbar, we should drop after the last node.
      dropPoint.ip =
        new InsertionPoint(PlacesUtils.getConcreteItemId(this._resultNode),
                           -1, Ci.nsITreeView.DROP_BEFORE);
      dropPoint.beforeIndex = -1;
    }

    return dropPoint;
  },

  _setTimer: function PT_setTimer(aTime) {
    let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    timer.initWithCallback(this, aTime, timer.TYPE_ONE_SHOT);
    return timer;
  },

  notify: function PT_notify(aTimer) {
    if (aTimer == this._updateChevronTimer) {
      this._updateChevronTimer = null;
      this._updateChevronTimerCallback();
    }

    // * Timer to turn off indicator bar.
    else if (aTimer == this._ibTimer) {
      this._dropIndicator.collapsed = true;
      this._ibTimer = null;
    }

    // * Timer to open a menubutton that's being dragged over.
    else if (aTimer == this._overFolder.openTimer) {
      // Set the autoopen attribute on the folder's menupopup so that
      // the menu will automatically close when the mouse drags off of it.
      this._overFolder.elt.lastChild.setAttribute("autoopened", "true");
      this._overFolder.elt.open = true;
      this._overFolder.openTimer = null;
    }

    // * Timer to close a menubutton that's been dragged off of.
    else if (aTimer == this._overFolder.closeTimer) {
      // Close the menubutton if we are not dragging over it or one of
      // its children.  The autoopened attribute will let the menu know to
      // close later if the menu is still being dragged over.
      let currentPlacesNode = PlacesControllerDragHelper.currentDropTarget;
      let inHierarchy = false;
      while (currentPlacesNode) {
        if (currentPlacesNode == this._rootElt) {
          inHierarchy = true;
          break;
        }
        currentPlacesNode = currentPlacesNode.parentNode;
      }
      // The _clearOverFolder() function will close the menu for
      // _overFolder.elt.  So null it out if we don't want to close it.
      if (inHierarchy)
        this._overFolder.elt = null;

      // Clear out the folder and all associated timers.
      this._clearOverFolder();
    }
  },

  _onMouseOver: function PT__onMouseOver(aEvent) {
    let button = aEvent.target;
    if (button.parentNode == this._rootElt && button._placesNode &&
        PlacesUtils.nodeIsURI(button._placesNode))
      window.XULBrowserWindow.setOverLink(aEvent.target._placesNode.uri, null);
  },

  _onMouseOut: function PT__onMouseOut(aEvent) {
    window.XULBrowserWindow.setOverLink("", null);
  },

  _cleanupDragDetails: function PT__cleanupDragDetails() {
    // Called on dragend and drop.
    PlacesControllerDragHelper.currentDropTarget = null;
    this._draggedElt = null;
    if (this._ibTimer)
      this._ibTimer.cancel();

    this._dropIndicator.collapsed = true;
  },

  _onDragStart: function PT__onDragStart(aEvent) {
    // Sub menus have their own d&d handlers.
    let draggedElt = aEvent.target;
    if (draggedElt.parentNode != this._rootElt || !draggedElt._placesNode)
      return;

    if (draggedElt.localName == "toolbarbutton" &&
        draggedElt.getAttribute("type") == "menu") {
      // If the drag gesture on a container is toward down we open instead
      // of dragging.
#ifdef XP_UNIX
#ifndef XP_MACOSX
      if (this._mouseDownTimer) {
        this._mouseDownTimer.cancel();
        this._mouseDownTimer = null;
      }
#endif
#endif
      let translateY = this._cachedMouseMoveEvent.clientY - aEvent.clientY;
      let translateX = this._cachedMouseMoveEvent.clientX - aEvent.clientX;
      if ((translateY) >= Math.abs(translateX/2)) {
        // Don't start the drag.
        aEvent.preventDefault();
        // Open the menu.
        draggedElt.open = true;
        return;
      }

      // If the menu is open, close it.
      if (draggedElt.open) {
        draggedElt.lastChild.hidePopup();
        draggedElt.open = false;
      }
    }

    // Activate the view and cache the dragged element.
    this._draggedElt = draggedElt._placesNode;
    this._rootElt.focus();

    this._controller.setDataTransfer(aEvent);
    aEvent.stopPropagation();
  },

  _onDragOver: function PT__onDragOver(aEvent) {
    // Cache the dataTransfer
    PlacesControllerDragHelper.currentDropTarget = aEvent.target;
    let dt = aEvent.dataTransfer;

    let dropPoint = this._getDropPoint(aEvent);
    if (!dropPoint || !dropPoint.ip ||
        !PlacesControllerDragHelper.canDrop(dropPoint.ip, dt)) {
      this._dropIndicator.collapsed = true;
      aEvent.stopPropagation();
      return;
    }

    if (this._ibTimer) {
      this._ibTimer.cancel();
      this._ibTimer = null;
    }

    if (dropPoint.folderElt || aEvent.originalTarget == this._chevron) {
      // Dropping over a menubutton or chevron button.
      // Set styles and timer to open relative menupopup.
      let overElt = dropPoint.folderElt || this._chevron;
      if (this._overFolder.elt != overElt) {
        this._clearOverFolder();
        this._overFolder.elt = overElt;
        this._overFolder.openTimer = this._setTimer(this._overFolder.hoverTime);
      }
      if (!this._overFolder.elt.hasAttribute("dragover"))
        this._overFolder.elt.setAttribute("dragover", "true");

      this._dropIndicator.collapsed = true;
    }
    else {
      // Dragging over a normal toolbarbutton,
      // show indicator bar and move it to the appropriate drop point.
      let ind = this._dropIndicator;
      let halfInd = ind.clientWidth / 2;
      let translateX;
      if (this._isRTL) {
        halfInd = Math.ceil(halfInd);
        translateX = 0 - this._rootElt.getBoundingClientRect().right - halfInd;
        if (this._rootElt.firstChild) {
          if (dropPoint.beforeIndex == -1)
            translateX += this._rootElt.lastChild.getBoundingClientRect().left;
          else {
            translateX += this._rootElt.childNodes[dropPoint.beforeIndex]
                              .getBoundingClientRect().right;
          }
        }
      }
      else {
        halfInd = Math.floor(halfInd);
        translateX = 0 - this._rootElt.getBoundingClientRect().left +
                     halfInd;
        if (this._rootElt.firstChild) {
          if (dropPoint.beforeIndex == -1)
            translateX += this._rootElt.lastChild.getBoundingClientRect().right;
          else {
            translateX += this._rootElt.childNodes[dropPoint.beforeIndex]
                              .getBoundingClientRect().left;
          }
        }
      }

      ind.style.MozTransform = "translate(" + Math.round(translateX) + "px)";
      ind.style.MozMarginStart = (-ind.clientWidth) + "px";
      ind.collapsed = false;

      // Clear out old folder information.
      this._clearOverFolder();
    }

    aEvent.preventDefault();
    aEvent.stopPropagation();
  },

  _onDrop: function PT__onDrop(aEvent) {
    PlacesControllerDragHelper.currentDropTarget = aEvent.target;

    let dropPoint = this._getDropPoint(aEvent);
    if (dropPoint && dropPoint.ip) {
      PlacesControllerDragHelper.onDrop(dropPoint.ip, aEvent.dataTransfer)
      aEvent.preventDefault();
    }

    this._cleanupDragDetails();
    aEvent.stopPropagation();
  },

  _onDragLeave: function PT__onDragLeave(aEvent) {
    PlacesControllerDragHelper.currentDropTarget = null;

    // Set timer to turn off indicator bar (if we turn it off
    // here, dragenter might be called immediately after, creating
    // flicker).
    if (this._ibTimer)
      this._ibTimer.cancel();
    this._ibTimer = this._setTimer(10);

    // If we hovered over a folder, close it now.
    if (this._overFolder.elt)
        this._overFolder.closeTimer = this._setTimer(this._overFolder.hoverTime);
  },

  _onDragEnd: function PT_onDragEnd(aEvent) {
    this._cleanupDragDetails();
  },

  _onPopupShowing: function PT__onPopupShowing(aEvent) {
    if (!this._allowPopupShowing) {
      this._allowPopupShowing = true;
      aEvent.preventDefault();
      return;
    }

    let parent = aEvent.target.parentNode;
    if (parent.localName == "toolbarbutton")
      this._openedMenuButton = parent;

    return PlacesViewBase.prototype._onPopupShowing.apply(this, arguments);
  },

  _onPopupHidden: function PT__onPopupHidden(aEvent) {
    let popup = aEvent.target;

    // Avoid handling popuphidden of inner views
    if (popup._placesNode && PlacesUIUtils.getViewForNode(popup) == this) {
      // UI performance: folder queries are cheap, keep the resultnode open
      // so we don't rebuild its contents whenever the popup is reopened.
      if (!PlacesUtils.nodeIsFolder(popup._placesNode))
        popup._placesNode.containerOpen = false;
    }

    let parent = popup.parentNode;
    if (parent.localName == "toolbarbutton") {
      this._openedMenuButton = null;
      // Clear the dragover attribute if present, if we are dragging into a
      // folder in the hierachy of current opened popup we don't clear
      // this attribute on clearOverFolder.  See Notify for closeTimer.
      if (parent.hasAttribute("dragover"))
        parent.removeAttribute("dragover");
    }
  },

#ifdef XP_UNIX
#ifndef XP_MACOSX
  _onMouseDown: function PT__onMouseDown(aEvent) {
    let target = aEvent.target;
    if (aEvent.button == 0 &&
        target.localName == "toolbarbutton" &&
        target.getAttribute("type") == "menu") {
      this._allowPopupShowing = false;
      // On Linux we can open the popup only after a delay.
      // Indeed as soon as the menupopup opens we are unable to start a
      // drag aEvent.  See bug 500081 for details.
      this._mouseDownTimer = Cc["@mozilla.org/timer;1"].
                             createInstance(Ci.nsITimer);
      let callback = {
        _self: this,
        _target: target,
        notify: function(timer) {
          this._target.open = true;
          this._mouseDownTimer = null;
        }
      };

      this._mouseDownTimer.initWithCallback(callback, 300,
                                            Ci.nsITimer.TYPE_ONE_SHOT);
    }
  },

  _onMouseUp: function PT__onMouseUp(aEvent) {
    if (aEvent.button != 0)
      return;

    if (this._mouseDownTimer) {
      // On a click (down/up), we should open the menu popup.
      this._mouseDownTimer.cancel();
      this._mouseDownTimer = null;
      aEvent.target.open = true;
    }
  },
#endif
#endif

  _onMouseMove: function PT__onMouseMove(aEvent) {
    // Used in dragStart to prevent dragging folders when dragging down.
    this._cachedMouseMoveEvent = aEvent;

    if (this._openedMenuButton == null ||
        PlacesControllerDragHelper.getSession())
      return;

    let target = aEvent.originalTarget;
    if (this._openedMenuButton != target &&
        target.localName == "toolbarbutton" &&
        target.type == "menu") {
      this._openedMenuButton.open = false;
      target.open = true;
    }
  }
};

/**
 * View for Places menus.  This object should be created during the first
 * popupshowing that's dispatched on the menu.
 */
function PlacesMenu(aPopupShowingEvent, aPlace) {
  this._rootElt = aPopupShowingEvent.target; // <menupopup>
  this._viewElt = this._rootElt.parentNode;   // <menu>
  this._viewElt._placesView = this;
  this._addEventListeners(this._rootElt, ["popupshowing", "popuphidden"], true);
  this._addEventListeners(window, ["unload"], false);

#ifdef XP_MACOSX
  if (this._viewElt.parentNode.localName == "menubar") {
    this._nativeView = true;
    this._rootElt._startMarker = -1;
    this._rootElt._endMarker = -1;
  }
#endif

  PlacesViewBase.call(this, aPlace);
  this._onPopupShowing(aPopupShowingEvent);
}

PlacesMenu.prototype = {
  __proto__: PlacesViewBase.prototype,

  QueryInterface: function PM_QueryInterface(aIID) {
    if (aIID.equals(Ci.nsIDOMEventListener))
      return this;

    return PlacesViewBase.prototype.QueryInterface.apply(this, arguments);
  },

  _removeChild: function PM_removeChild(aChild) {
    PlacesViewBase.prototype._removeChild.apply(this, arguments);
    if (this._endMarker != -1)
      this._endMarker--;
  },

  uninit: function PM_uninit() {
    this._removeEventListeners(this._rootElt, ["popupshowing", "popuphidden"],
                               true);
    this._removeEventListeners(window, ["unload"], false);

    PlacesViewBase.prototype.uninit.apply(this, arguments);
  },

  handleEvent: function PM_handleEvent(aEvent) {
    switch (aEvent.type) {
      case "unload":
        this.uninit();
        break;
      case "popupshowing":
        this._onPopupShowing(aEvent);
        break;
      case "popuphidden":
        this._onPopupHidden(aEvent);
        break;
    }
  },

  _onPopupHidden: function PM__onPopupHidden(aEvent) {
    // Avoid handling popuphidden of inner views.
    let popup = aEvent.originalTarget;
    if (!popup._placesNode || PlacesUIUtils.getViewForNode(popup) != this)
      return;

    // UI performance: folder queries are cheap, keep the resultnode open
    // so we don't rebuild its contents whenever the popup is reopened.
    if (!PlacesUtils.nodeIsFolder(popup._placesNode))
      popup._placesNode.containerOpen = false;

    // The autoopened attribute is set for folders which have been
    // automatically opened when dragged over.  Turn off this attribute
    // when the folder closes because it is no longer applicable.
    popup.removeAttribute("autoopened");
    popup.removeAttribute("dragstart");
  }
};

