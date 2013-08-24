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
 * The Original Code is ui.js.
 *
 * The Initial Developer of the Original Code is
 * the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * Ian Gilman <ian@iangilman.com>
 * Aza Raskin <aza@mozilla.com>
 * Michael Yoshitaka Erlewine <mitcho@mitcho.com>
 * Ehsan Akhgari <ehsan@mozilla.com>
 * Raymond Lee <raymond@appcoast.com>
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

// **********
// Title: ui.js

(function() {

window.Keys = { meta: false };

// ##########
// Class: UIManager
// Singleton top-level UI manager.
var UIManager = {
  // Variable: _frameInitalized
  // True if the Tab View UI frame has been initialized.
  _frameInitalized: false,

  // Variable: _pageBounds
  // Stores the page bounds.
  _pageBounds : null,

  // Variable: _closedLastVisibleTab
  // If true, the last visible tab has just been closed in the tab strip.
  _closedLastVisibleTab : false,

  // Variable: _closedSelectedTabInTabView
  // If true, a select tab has just been closed in TabView.
  _closedSelectedTabInTabView : false,

  // Variable: _reorderTabItemsOnShow
  // Keeps track of the <GroupItem>s which their tab items' tabs have been moved
  // and re-orders the tab items when switching to TabView.
  _reorderTabItemsOnShow : [],

  // Variable: _reorderTabsOnHide
  // Keeps track of the <GroupItem>s which their tab items have been moved in
  // TabView UI and re-orders the tabs when switcing back to main browser.
  _reorderTabsOnHide : [],

  // Variable: _currentTab
  // Keeps track of which xul:tab we are currently on.
  // Used to facilitate zooming down from a previous tab.
  _currentTab : null,

  // ----------
  // Function: init
  // Must be called after the object is created.
  init: function() {
    try {
      let self = this;

      // ___ storage
      Storage.init();
      let data = Storage.readUIData(gWindow);
      this._storageSanity(data);
      this._pageBounds = data.pageBounds;

      // ___ hook into the browser
      gWindow.addEventListener("tabviewshow", function() {
        self.showTabView(true);
      }, false);

      // ___ currentTab
      this._currentTab = gBrowser.selectedTab;

      // ___ Dev Menu
      // This dev menu is not meant for shipping, nor is it of general
      // interest, but we still need it for the time being. Change the 
      // false below to enable; just remember to change back before 
      // committing. Bug 586721 will track the ultimate removal. 
      if (false)
        this._addDevMenu();

      // When you click on the background/empty part of TabView,
      // we create a new groupItem.
      iQ(gTabViewFrame.contentDocument).mousedown(function(e) {
        if (iQ(":focus").length > 0) {
          iQ(":focus").each(function(element) {
            if (element.nodeName == "INPUT")
              element.blur();
          });
        }
        if (e.originalTarget.id == "content")
          self._createGroupItemOnDrag(e)
      });

      iQ(window).bind("beforeunload", function() {
        Array.forEach(gBrowser.tabs, function(tab) {
          gBrowser.showTab(tab);
        });
      });
      iQ(window).bind("unload", function() {
        self.uninit();
      });

      gWindow.addEventListener("tabviewhide", function() {
        var activeTab = self.getActiveTab();
        if (activeTab)
          activeTab.zoomIn();
      }, false);

      // ___ setup key handlers
      this._setTabViewFrameKeyHandlers();

      // ___ add tab action handlers
      this._addTabActionHandlers();

      // ___ Storage

      GroupItems.init();

      var groupItemsData = Storage.readGroupItemsData(gWindow);
      var firstTime = !groupItemsData || Utils.isEmptyObject(groupItemsData);
      var groupItemData = Storage.readGroupItemData(gWindow);
      GroupItems.reconstitute(groupItemsData, groupItemData);
      GroupItems.killNewTabGroup(); // temporary?

      // ___ tabs
      TabItems.init();
      TabItems.pausePainting();

      if (firstTime) {
        var padding = 10;
        var infoWidth = 350;
        var infoHeight = 350;
        var pageBounds = Items.getPageBounds();
        pageBounds.inset(padding, padding);

        // ___ make a fresh groupItem
        var box = new Rect(pageBounds);
        box.width =
          Math.min(box.width * 0.667, pageBounds.width - (infoWidth + padding));
        box.height = box.height * 0.667;
        var options = {
          bounds: box
        };

        var groupItem = new GroupItem([], options);

        var items = TabItems.getItems();
        items.forEach(function(item) {
          if (item.parent)
            item.parent.remove(item);

          groupItem.add(item);
        });

        // ___ make info item
        let welcome = "How to organize your tabs";
        let more = "";
        let video = "http://videos-cdn.mozilla.net/firefox4beta/tabcandy_howto.webm";
        var html =
          "<div class='intro'>"
            + "<h1>" + welcome + "</h1>"
            + ( more && more.length ? "<div>" + more + "</div><br>" : "")
            + "<video src='" + video + "' "
            + "width='100%' preload controls>"
          + "</div>";

        box.left = box.right + padding;
        box.width = infoWidth;
        box.height = infoHeight;
        var infoItem = new InfoItem(box);
        infoItem.html(html);
      }

      // ___ resizing
      if (this._pageBounds)
        this._resize(true);
      else
        this._pageBounds = Items.getPageBounds();

      iQ(window).resize(function() {
        self._resize();
      });

      // ___ setup observer to save canvas images
      var observer = {
        observe : function(subject, topic, data) {
          if (topic == "quit-application-requested") {
            if (self._isTabViewVisible())
              TabItems.saveAll(true);
            self._save();
          }
        }
      };
      Services.obs.addObserver(observer, "quit-application-requested", false);

      // ___ Done
      this._frameInitalized = true;
      this._save();
    } catch(e) {
      Utils.log(e);
    }
  },

  uninit: function() {
    TabItems.uninit();
    GroupItems.uninit();
    Storage.uninit();

    this._currentTab = null;
    this._pageBounds = null;
    this._reorderTabItemsOnShow = null;
    this._reorderTabsOnHide = null;
  },

  // ----------
  // Function: getActiveTab
  // Returns the currently active tab as a <TabItem>
  //
  getActiveTab: function() {
    return this._activeTab;
  },

  // ----------
  // Function: setActiveTab
  // Sets the currently active tab. The idea of a focused tab is useful
  // for keyboard navigation and returning to the last zoomed-in tab.
  // Hitting return/esc brings you to the focused tab, and using the
  // arrow keys lets you navigate between open tabs.
  //
  // Parameters:
  //  - Takes a <TabItem>
  setActiveTab: function(tab) {
    if (tab == this._activeTab)
      return;

    if (this._activeTab) {
      this._activeTab.makeDeactive();
      this._activeTab.removeSubscriber(this, "close");
    }
    this._activeTab = tab;

    if (this._activeTab) {
      var self = this;
      this._activeTab.addSubscriber(this, "close", function() {
        self._activeTab = null;
      });

      this._activeTab.makeActive();
    }
  },

  // ----------
  // Function: _isTabViewVisible
  // Returns true if the TabView UI is currently shown.
  _isTabViewVisible: function() {
    return gTabViewDeck.selectedIndex == 1;
  },

  // ----------
  // Function: showTabView
  // Shows TabView and hides the main browser UI.
  // Parameters:
  //   zoomOut - true for zoom out animation, false for nothing.
  showTabView: function(zoomOut) {
    if (this._isTabViewVisible())
      return;

    var self = this;
    var currentTab = this._currentTab;
    var item = null;

    this._reorderTabItemsOnShow.forEach(function(groupItem) {
      groupItem.reorderTabItemsBasedOnTabOrder();
    });
    this._reorderTabItemsOnShow = [];

#ifdef XP_WIN
    // Restore the full height when showing TabView
    gTabViewFrame.style.marginTop = "";
#endif
    gTabViewDeck.selectedIndex = 1;
    gTabViewFrame.contentWindow.focus();

    gBrowser.updateTitlebar();
#ifdef XP_MACOSX
    this._setActiveTitleColor(true);
#endif
    let event = document.createEvent("Events");
    event.initEvent("tabviewshown", true, false);

    if (zoomOut && currentTab && currentTab.tabItem) {
      item = currentTab.tabItem;
      // If there was a previous currentTab we want to animate
      // its thumbnail (canvas) for the zoom out.
      // Note that we start the animation on the chrome thread.

      // Zoom out!
      item.zoomOut(function() {
        if (!currentTab.tabItem) // if the tab's been destroyed
          item = null;

        self.setActiveTab(item);

        if (item.parent) {
          var activeGroupItem = GroupItems.getActiveGroupItem();
          if (activeGroupItem)
            activeGroupItem.setTopChild(item);
        }

        self._resize(true);
        dispatchEvent(event);
      });
    } else
      dispatchEvent(event);

    TabItems.resumePainting();
  },

  // ----------
  // Function: hideTabView
  // Hides TabView and shows the main browser UI.
  hideTabView: function() {
    if (!this._isTabViewVisible())
      return;

    TabItems.pausePainting();

    this._reorderTabsOnHide.forEach(function(groupItem) {
      groupItem.reorderTabsBasedOnTabItemOrder();
    });
    this._reorderTabsOnHide = [];

#ifdef XP_WIN
    // Push the top of TabView frame to behind the tabbrowser, so glass can show
    // XXX bug 586679: avoid shrinking the iframe and squishing iframe contents
    // as well as avoiding the flash of black as we animate out
    gTabViewFrame.style.marginTop = gBrowser.boxObject.y + "px";
#endif
    gTabViewDeck.selectedIndex = 0;
    gBrowser.contentWindow.focus();

    // set the close button on tab
    gBrowser.tabContainer.adjustTabstrip();

    gBrowser.updateTitlebar();
#ifdef XP_MACOSX
    this._setActiveTitleColor(false);
#endif
    let event = document.createEvent("Events");
    event.initEvent("tabviewhidden", true, false);
    dispatchEvent(event);
  },

#ifdef XP_MACOSX
  // ----------
  // Function: _setActiveTitleColor
  // Used on the Mac to make the title bar match the gradient in the rest of the
  // TabView UI.
  //
  // Parameters:
  //   set - true for the special TabView color, false for the normal color.
  _setActiveTitleColor: function(set) {
    // Mac Only
    var mainWindow = gWindow.document.getElementById("main-window");
    if (set)
      mainWindow.setAttribute("activetitlebarcolor", "#C4C4C4");
    else
      mainWindow.removeAttribute("activetitlebarcolor");
  },
#endif

  // ----------
  // Function: _addTabActionHandlers
  // Adds handlers to handle tab actions.
  _addTabActionHandlers: function() {
    var self = this;

    AllTabs.register("close", function(tab) {
      if (tab.ownerDocument.defaultView != gWindow)
        return;

      if (self._isTabViewVisible()) {
        // just closed the selected tab in the TabView interface.
        if (self._currentTab == tab)
          self._closedSelectedTabInTabView = true;
      } else {
        // if not closing the last tab
        if (gBrowser.tabs.length > 1) {
          var groupItem = GroupItems.getActiveGroupItem();

          // 1) Only go back to the TabView tab when there you close the last
          // tab of a groupItem.
          // 2) Take care of the case where you've closed the last tab in
          // an un-named groupItem, which means that the groupItem is gone (null) and
          // there are no visible tabs.
          // Can't use timeout here because user would see a flicker of
          // switching to another tab before the TabView interface shows up.
          if ((groupItem && groupItem._children.length == 1) ||
              (groupItem == null && gBrowser.visibleTabs.length == 1)) {
            // for the tab focus event to pick up.
            self._closedLastVisibleTab = true;
            // remove the zoom prep.
            if (tab && tab.tabItem)
              tab.tabItem.setZoomPrep(false);
            self.showTabView();
          }
        }
      }
    });

    AllTabs.register("move", function(tab) {
      if (tab.ownerDocument.defaultView != gWindow)
        return;

      let activeGroupItem = GroupItems.getActiveGroupItem();
      if (activeGroupItem)
        self.setReorderTabItemsOnShow(activeGroupItem);
    });

    AllTabs.register("select", function(tab) {
      if (tab.ownerDocument.defaultView != gWindow)
        return;

      self.onTabSelect(tab);
    });
  },

  // ----------
  // Function: onTabSelect
  // Called when the user switches from one tab to another outside of the TabView UI.
  onTabSelect: function(tab) {
    let currentTab = this._currentTab;
    this._currentTab = tab;

    // if the last visible tab has just been closed, don't show the chrome UI.
    if (this._isTabViewVisible() &&
        (this._closedLastVisibleTab || this._closedSelectedTabInTabView)) {
      this._closedLastVisibleTab = false;
      this._closedSelectedTabInTabView = false;
      return;
    }
    // reset these vars, just in case.
    this._closedLastVisibleTab = false;
    this._closedSelectedTabInTabView = false;

    // if TabView is visible but we didn't just close the last tab or
    // selected tab, show chrome.
    if (this._isTabViewVisible())
      this.hideTabView();

    let oldItem = null;
    let newItem = null;
    
    if (currentTab && currentTab.tabItem)
      oldItem = currentTab.tabItem;
    if (tab && tab.tabItem) {
      newItem = tab.tabItem;
      GroupItems.updateActiveGroupItemAndTabBar(newItem);
    }

    // ___ prepare for when we return to TabView
    if (newItem != oldItem) {
      if (oldItem)
        oldItem.setZoomPrep(false);
      if (newItem)
        newItem.setZoomPrep(true);
    } else if (oldItem)
      oldItem.setZoomPrep(true);
  },

  // ----------
  // Function: setReorderTabsOnHide
  // Sets the groupItem which the tab items' tabs should be re-ordered when
  // switching to the main browser UI.
  // Parameters:
  //   groupItem - the groupItem which would be used for re-ordering tabs.
  setReorderTabsOnHide: function(groupItem) {
    if (this._isTabViewVisible()) {
      var index = this._reorderTabsOnHide.indexOf(groupItem);
      if (index == -1)
        this._reorderTabsOnHide.push(groupItem);
    }
  },

  // ----------
  // Function: setReorderTabItemsOnShow
  // Sets the groupItem which the tab items should be re-ordered when
  // switching to the tab view UI.
  // Parameters:
  //   groupItem - the groupItem which would be used for re-ordering tab items.
  setReorderTabItemsOnShow: function(groupItem) {
    if (!this._isTabViewVisible()) {
      var index = this._reorderTabItemsOnShow.indexOf(groupItem);
      if (index == -1)
        this._reorderTabItemsOnShow.push(groupItem);
    }
  },

  // ----------
  // Function: _setTabViewFrameKeyHandlers
  // Sets up the key handlers for navigating between tabs within the TabView UI.
  _setTabViewFrameKeyHandlers: function() {
    var self = this;

    iQ(window).keyup(function(event) {
      if (!event.metaKey) window.Keys.meta = false;
    });

    iQ(window).keydown(function(event) {
      if (event.metaKey) window.Keys.meta = true;

      if (!self.getActiveTab() || iQ(":focus").length > 0) {
        // prevent the default action when tab is pressed so it doesn't gives
        // us problem with content focus.
        if (event.keyCode == KeyEvent.DOM_VK_TAB) {
          event.stopPropagation();
          event.preventDefault();
        }
        return;
      }

      function getClosestTabBy(norm) {
        var centers =
          [[item.bounds.center(), item] for each(item in TabItems.getItems())];
        var myCenter = self.getActiveTab().bounds.center();
        var matches = centers
          .filter(function(item){return norm(item[0], myCenter)})
          .sort(function(a,b){
            return myCenter.distance(a[0]) - myCenter.distance(b[0]);
          });
        if (matches.length > 0)
          return matches[0][1];
        return null;
      }

      var norm = null;
      switch (event.keyCode) {
        case KeyEvent.DOM_VK_RIGHT:
          norm = function(a, me){return a.x > me.x};
          break;
        case KeyEvent.DOM_VK_LEFT:
          norm = function(a, me){return a.x < me.x};
          break;
        case KeyEvent.DOM_VK_DOWN:
          norm = function(a, me){return a.y > me.y};
          break;
        case KeyEvent.DOM_VK_UP:
          norm = function(a, me){return a.y < me.y}
          break;
      }

      if (norm != null) {
        var nextTab = getClosestTabBy(norm);
        if (nextTab) {
          if (nextTab.inStack() && !nextTab.parent.expanded)
            nextTab = nextTab.parent.getChild(0);
          self.setActiveTab(nextTab);
        }
        event.stopPropagation();
        event.preventDefault();
      } else if (event.keyCode == KeyEvent.DOM_VK_SPACE) {
        // alt/control + space to zoom into the active tab.
#ifdef XP_MACOSX
        if (event.altKey && !event.metaKey && !event.shiftKey &&
            !event.ctrlKey) {
#else
        if (event.ctrlKey && !event.metaKey && !event.shiftKey &&
            !event.altKey) {
#endif
          var activeTab = self.getActiveTab();
          if (activeTab)
            activeTab.zoomIn();
          event.stopPropagation();
          event.preventDefault();
        }
      } else if (event.keyCode == KeyEvent.DOM_VK_ESCAPE || 
                 event.keyCode == KeyEvent.DOM_VK_RETURN ||
                 event.keyCode == KeyEvent.DOM_VK_ENTER) {
        let activeTab = self.getActiveTab();
        let activeGroupItem = GroupItems.getActiveGroupItem();

        if (activeGroupItem && activeGroupItem.expanded && 
            event.keyCode == KeyEvent.DOM_VK_ESCAPE)
          activeGroupItem.collapse();
        else if (activeTab)
            activeTab.zoomIn();

        event.stopPropagation();
        event.preventDefault();
      } else if (event.keyCode == KeyEvent.DOM_VK_TAB) {
        // tab/shift + tab to go to the next tab.
        var activeTab = self.getActiveTab();
        if (activeTab) {
          var tabItems = (activeTab.parent ? activeTab.parent.getChildren() :
                          [activeTab]);
          var length = tabItems.length;
          var currentIndex = tabItems.indexOf(activeTab);

          if (length > 1) {
            if (event.shiftKey) {
              if (currentIndex == 0)
                newIndex = (length - 1);
              else
                newIndex = (currentIndex - 1);
            } else {
              if (currentIndex == (length - 1))
                newIndex = 0;
              else
                newIndex = (currentIndex + 1);
            }
            self.setActiveTab(tabItems[newIndex]);
          }
        }
        event.stopPropagation();
        event.preventDefault();
      }
    });
  },

  // ----------
  // Function: _createGroupItemOnDrag
  // Called in response to a mousedown in empty space in the TabView UI;
  // creates a new groupItem based on the user's drag.
  _createGroupItemOnDrag: function(e) {
    const minSize = 60;
    const minMinSize = 15;

    let lastActiveGroupItem = GroupItems.getActiveGroupItem();
    GroupItems.setActiveGroupItem(null);

    var startPos = { x: e.clientX, y: e.clientY };
    var phantom = iQ("<div>")
      .addClass("groupItem phantom activeGroupItem")
      .css({
        position: "absolute",
        opacity: .7,
        zIndex: -1,
        cursor: "default"
      })
      .appendTo("body");

    var item = { // a faux-Item
      container: phantom,
      isAFauxItem: true,
      bounds: {},
      getBounds: function FauxItem_getBounds() {
        return this.container.bounds();
      },
      setBounds: function FauxItem_setBounds(bounds) {
        this.container.css(bounds);
      },
      setZ: function FauxItem_setZ(z) {
        this.container.css("z-index", z);
      },
      setOpacity: function FauxItem_setOpacity(opacity) {
        this.container.css("opacity", opacity);
      },
      // we don't need to pushAway the phantom item at the end, because
      // when we create a new GroupItem, it'll do the actual pushAway.
      pushAway: function () {},
    };
    item.setBounds(new Rect(startPos.y, startPos.x, 0, 0));

    var dragOutInfo = new Drag(item, e, true); // true = isResizing

    function updateSize(e) {
      var box = new Rect();
      box.left = Math.min(startPos.x, e.clientX);
      box.right = Math.max(startPos.x, e.clientX);
      box.top = Math.min(startPos.y, e.clientY);
      box.bottom = Math.max(startPos.y, e.clientY);
      item.setBounds(box);

      // compute the stationaryCorner
      var stationaryCorner = "";

      if (startPos.y == box.top)
        stationaryCorner += "top";
      else
        stationaryCorner += "bottom";

      if (startPos.x == box.left)
        stationaryCorner += "left";
      else
        stationaryCorner += "right";

      dragOutInfo.snap(stationaryCorner, false, false); // null for ui, which we don't use anyway.

      box = item.getBounds();
      if (box.width > minMinSize && box.height > minMinSize &&
         (box.width > minSize || box.height > minSize))
        item.setOpacity(1);
      else
        item.setOpacity(0.7);

      e.preventDefault();
    }

    function collapse() {
      phantom.animate({
        width: 0,
        height: 0,
        top: phantom.position().x + phantom.height()/2,
        left: phantom.position().y + phantom.width()/2
      }, {
        duration: 300,
        complete: function() {
          phantom.remove();
        }
      });
      GroupItems.setActiveGroupItem(lastActiveGroupItem);
    }

    function finalize(e) {
      iQ(window).unbind("mousemove", updateSize);
      dragOutInfo.stop();
      if (phantom.css("opacity") != 1)
        collapse();
      else {
        var bounds = item.getBounds();

        // Add all of the orphaned tabs that are contained inside the new groupItem
        // to that groupItem.
        var tabs = GroupItems.getOrphanedTabs();
        var insideTabs = [];
        for each(tab in tabs) {
          if (bounds.contains(tab.bounds))
            insideTabs.push(tab);
        }

        var groupItem = new GroupItem(insideTabs,{bounds:bounds});
        GroupItems.setActiveGroupItem(groupItem);
        phantom.remove();
        dragOutInfo = null;
      }
    }

    iQ(window).mousemove(updateSize)
    iQ(gWindow).one("mouseup", finalize);
    e.preventDefault();
    return false;
  },

  // ----------
  // Function: _resize
  // Update the TabView UI contents in response to a window size change.
  // Won't do anything if it doesn't deem the resize necessary.
  // Parameters:
  //   force - true to update even when "unnecessary"; default false
  _resize: function(force) {
    if (typeof force == "undefined")
      force = false;

    // If TabView isn't focused and is not showing, don't perform a resize.
    // This resize really slows things down.
    if (!force && !this._isTabViewVisible())
      return;

    var oldPageBounds = new Rect(this._pageBounds);
    var newPageBounds = Items.getPageBounds();
    if (newPageBounds.equals(oldPageBounds))
      return;

    var items = Items.getTopLevelItems();

    // compute itemBounds: the union of all the top-level items' bounds.
    var itemBounds = new Rect(this._pageBounds);
    // We start with pageBounds so that we respect the empty space the user
    // has left on the page.
    itemBounds.width = 1;
    itemBounds.height = 1;
    items.forEach(function(item) {
      if (item.locked.bounds)
        return;

      var bounds = item.getBounds();
      itemBounds = (itemBounds ? itemBounds.union(bounds) : new Rect(bounds));
    });

    if (newPageBounds.width < this._pageBounds.width &&
        newPageBounds.width > itemBounds.width)
      newPageBounds.width = this._pageBounds.width;

    if (newPageBounds.height < this._pageBounds.height &&
        newPageBounds.height > itemBounds.height)
      newPageBounds.height = this._pageBounds.height;

    var wScale;
    var hScale;
    if (Math.abs(newPageBounds.width - this._pageBounds.width)
         > Math.abs(newPageBounds.height - this._pageBounds.height)) {
      wScale = newPageBounds.width / this._pageBounds.width;
      hScale = newPageBounds.height / itemBounds.height;
    } else {
      wScale = newPageBounds.width / itemBounds.width;
      hScale = newPageBounds.height / this._pageBounds.height;
    }

    var scale = Math.min(hScale, wScale);
    var self = this;
    var pairs = [];
    items.forEach(function(item) {
      if (item.locked.bounds)
        return;

      var bounds = item.getBounds();
      bounds.left += newPageBounds.left - self._pageBounds.left;
      bounds.left *= scale;
      bounds.width *= scale;

      bounds.top += newPageBounds.top - self._pageBounds.top;
      bounds.top *= scale;
      bounds.height *= scale;

      pairs.push({
        item: item,
        bounds: bounds
      });
    });

    Items.unsquish(pairs);

    pairs.forEach(function(pair) {
      pair.item.setBounds(pair.bounds, true);
      pair.item.snap();
    });

    this._pageBounds = Items.getPageBounds();
    this._save();
  },

  // ----------
  // Function: _addDevMenu
  // Fills out the "dev menu" in the TabView UI.
  _addDevMenu: function() {
    try {
      var self = this;

      var $select = iQ("<select>")
        .css({
          position: "absolute",
          bottom: 5,
          right: 5,
          zIndex: 99999,
          opacity: .2
        })
        .appendTo("#content")
        .change(function () {
          var index = iQ(this).val();
          try {
            commands[index].code.apply(commands[index].element);
          } catch(e) {
            Utils.log("dev menu error", e);
          }
          iQ(this).val(0);
        });

      var commands = [{
        name: "dev menu",
        code: function() { }
      }, {
        name: "show trenches",
        code: function() {
          Trenches.toggleShown();
          iQ(this).html((Trenches.showDebug ? "hide" : "show") + " trenches");
        }
      }, {
/*
        name: "refresh",
        code: function() {
          location.href = "tabview.html";
        }
      }, {
        name: "reset",
        code: function() {
          self._reset();
        }
      }, {
*/
        name: "save",
        code: function() {
          self._saveAll();
        }
      }, {
        name: "group sites",
        code: function() {
          self._arrangeBySite();
        }
      }];

      var count = commands.length;
      var a;
      for (a = 0; a < count; a++) {
        commands[a].element = (iQ("<option>")
          .val(a)
          .html(commands[a].name)
          .appendTo($select))[0];
      }
    } catch(e) {
      Utils.log(e);
    }
  },

  // -----------
  // Function: _reset
  // Wipes all TabView storage and refreshes, giving you the "first-run" state.
  _reset: function() {
    Storage.wipe();
    location.href = "";
  },

  // ----------
  // Function: storageSanity
  // Given storage data for this object, returns true if it looks valid.
  _storageSanity: function(data) {
    if (Utils.isEmptyObject(data))
      return true;

    if (!Utils.isRect(data.pageBounds)) {
      Utils.log("UI.storageSanity: bad pageBounds", data.pageBounds);
      data.pageBounds = null;
      return false;
    }

    return true;
  },

  // ----------
  // Function: _save
  // Saves the data for this object to persistent storage
  _save: function() {
    if (!this._frameInitalized)
      return;

    var data = {
      pageBounds: this._pageBounds
    };

    if (this._storageSanity(data))
      Storage.saveUIData(gWindow, data);
  },

  // ----------
  // Function: _saveAll
  // Saves all data associated with TabView.
  // TODO: Save info items
  _saveAll: function() {
    this._save();
    GroupItems.saveAll();
    TabItems.saveAll();
  },

  // ----------
  // Function: _arrangeBySite
  // Blows away all existing groupItems and organizes the tabs into new groupItems based
  // on domain.
  _arrangeBySite: function() {
    function putInGroupItem(set, key) {
      var groupItem = GroupItems.getGroupItemWithTitle(key);
      if (groupItem) {
        set.forEach(function(el) {
          groupItem.add(el);
        });
      } else
        new GroupItem(set, { dontPush: true, dontArrange: true, title: key });
    }

    GroupItems.removeAll();

    var groupItems = [];
    var leftovers = [];
    var items = TabItems.getItems();
    items.forEach(function(item) {
      var url = item.tab.linkedBrowser.currentURI.spec;
      var domain = url.split('/')[2];

      if (!domain)
        leftovers.push(item.container);
      else {
        var domainParts = domain.split(".");
        var mainDomain = domainParts[domainParts.length - 2];
        if (groupItems[mainDomain])
          groupItems[mainDomain].push(item.container);
        else
          groupItems[mainDomain] = [item.container];
      }
    });

    for (key in groupItems) {
      var set = groupItems[key];
      if (set.length > 1) {
        putInGroupItem(set, key);
      } else
        leftovers.push(set[0]);
    }

    if (leftovers.length)
      putInGroupItem(leftovers, "mixed");

    GroupItems.arrange();
  },
};

// ----------
window.UI = UIManager;
window.UI.init();

})();
