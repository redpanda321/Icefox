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
 * The Original Code is browser notifications.
 *
 * The Initial Developer of the Original Code is
 * the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Gavin Sharp <gavin@gavinsharp.com> (Original Author)
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

var EXPORTED_SYMBOLS = ["PopupNotifications"];

var Cc = Components.classes, Ci = Components.interfaces;

Components.utils.import("resource://gre/modules/Services.jsm");

/**
 * Notification object describes a single popup notification.
 *
 * @see PopupNotifications.show()
 */
function Notification(id, message, anchorID, mainAction, secondaryActions,
                      browser, owner, options) {
  this.id = id;
  this.message = message;
  this.anchorID = anchorID;
  this.mainAction = mainAction;
  this.secondaryActions = secondaryActions || [];
  this.browser = browser;
  this.owner = owner;
  this.options = options || {};
}

Notification.prototype = {
  /**
   * Removes the notification and updates the popup accordingly if needed.
   */
  remove: function Notification_remove() {
    this.owner.remove(this);
  },

  get anchorElement() {
    let anchorElement = null;
    if (this.anchorID)
      anchorElement = this.owner.iconBox.querySelector("#"+this.anchorID);

    if (!anchorElement)
      anchorElement = this.owner.iconBox;

    return anchorElement;
  }
};

/**
 * The PopupNotifications object manages popup notifications for a given browser
 * window.
 * @param tabbrowser
 *        window's <xul:tabbrowser/>. Used to observe tab switching events and
 *        for determining the active browser element.
 * @param panel
 *        The <xul:panel/> element to use for notifications. The panel is
 *        populated with <popupnotification> children and displayed it as
 *        needed.
 * @param iconBox
 *        Reference to a container element that should be hidden or
 *        unhidden when notifications are hidden or shown. It should be the
 *        parent of anchor elements whose IDs are passed to show().
 *        It is used as a fallback popup anchor if notifications specify
 *        invalid or non-existent anchor IDs.
 */
function PopupNotifications(tabbrowser, panel, iconBox) {
  if (!(tabbrowser instanceof Ci.nsIDOMXULElement))
    throw "Invalid tabbrowser";
  if (!(iconBox instanceof Ci.nsIDOMXULElement))
    throw "Invalid iconBox";
  if (!(panel instanceof Ci.nsIDOMXULElement))
    throw "Invalid panel";

  this.window = tabbrowser.ownerDocument.defaultView;
  this.panel = panel;
  this.iconBox = iconBox;
  this.tabbrowser = tabbrowser;

  let self = this;
  this.iconBox.addEventListener("click", function (event) {
    self._onIconBoxCommand(event);
  }, false);
  this.iconBox.addEventListener("keypress", function (event) {
    self._onIconBoxCommand(event);
  }, false);
  this.panel.addEventListener("popuphidden", function (event) {
    self._onPopupHidden(event);
  }, true);

  function updateFromListeners() {
    // setTimeout(..., 0) needed, otherwise openPopup from "activate" event
    // handler results in the popup being hidden again for some reason...
    self.window.setTimeout(function () {
      self._update();
    }, 0);
  }
  this.window.addEventListener("activate", updateFromListeners, true);
  this.tabbrowser.tabContainer.addEventListener("TabSelect", updateFromListeners, true);
}

PopupNotifications.prototype = {
  /**
   * Retrieve a Notification object associated with the browser/ID pair.
   * @param id
   *        The Notification ID to search for.
   * @param browser
   *        The browser whose notifications should be searched. If null, the
   *        currently selected browser's notifications will be searched.
   *
   * @returns the corresponding Notification object, or null if no such
   *          notification exists.
   */
  getNotification: function PopupNotifications_getNotification(id, browser) {
    let n = null;
    let notifications = this._getNotificationsForBrowser(browser || this.tabbrowser.selectedBrowser);
    notifications.some(function(x) x.id == id && (n = x))
    return n;
  },

  /**
   * Adds a new popup notification.
   * @param browser
   *        The <xul:browser> element associated with the notification. Must not
   *        be null.
   * @param id
   *        A unique ID that identifies the type of notification (e.g.
   *        "geolocation"). Only one notification with a given ID can be visible
   *        at a time. If a notification already exists with the given ID, it
   *        will be replaced.
   * @param message
   *        The text to be displayed in the notification.
   * @param anchorID
   *        The ID of the element that should be used as this notification
   *        popup's anchor. May be null, in which case the notification will be
   *        anchored to the iconBox.
   * @param mainAction
   *        A JavaScript object literal describing the notification button's
   *        action. If present, it must have the following properties:
   *          - label (string): the button's label.
   *          - accessKey (string): the button's accessKey.
   *          - callback (function): a callback to be invoked when the button is
   *            pressed.
   *        If null, the notification will not have a button, and
   *        secondaryActions will be ignored.
   * @param secondaryActions
   *        An optional JavaScript array describing the notification's alternate
   *        actions. The array should contain objects with the same properties
   *        as mainAction. These are used to populate the notification button's
   *        dropdown menu.
   * @param options
   *        An options JavaScript object holding additional properties for the
   *        notification. The following properties are currently supported:
   *        persistence: An integer. The notification will not automatically
   *                     dismiss for this many page loads.
   *        timeout:     A time in milliseconds. The notification will not
   *                     automatically dismiss before this time.
   *        dismissed:   Whether the notification should be added as a dismissed
   *                     notification. Dismissed notifications can be activated
   *                     by clicking on their anchorElement.
   *        neverShow:   Indicate that no popup should be shown for this
   *                     notification. Useful for just showing the anchor icon.
   * @returns the Notification object corresponding to the added notification.
   */
  show: function PopupNotifications_show(browser, id, message, anchorID,
                                         mainAction, secondaryActions, options) {
    function isInvalidAction(a) {
      return !a || !(typeof(a.callback) == "function") || !a.label || !a.accessKey;
    }

    if (!browser)
      throw "PopupNotifications_show: invalid browser";
    if (!id)
      throw "PopupNotifications_show: invalid ID";
    if (mainAction && isInvalidAction(mainAction))
      throw "PopupNotifications_show: invalid mainAction";
    if (secondaryActions && secondaryActions.some(isInvalidAction))
      throw "PopupNotifications_show: invalid secondaryActions";

    let notification = new Notification(id, message, anchorID, mainAction,
                                        secondaryActions, browser, this, options);

    if (options && options.dismissed)
      notification.dismissed = true;

    let existingNotification = this.getNotification(id, browser);
    if (existingNotification)
      this._remove(existingNotification);

    let notifications = this._getNotificationsForBrowser(browser);
    notifications.push(notification);

    let fm = Cc["@mozilla.org/focus-manager;1"].getService(Ci.nsIFocusManager);
    if (browser == this.tabbrowser.selectedBrowser && fm.activeWindow == this.window) {
      // show panel now
      this._update(notification.anchorElement);
    } else {
      // Otherwise, update() will display the notification the next time the
      // relevant tab/window is selected.

      // Notify observers that we're not showing the popup (useful for testing)
      this._notify("backgroundShow");
    }

    return notification;
  },

  /**
   * Returns true if the notification popup is currently being displayed.
   */
  get isPanelOpen() {
    let panelState = this.panel.state;

    return panelState == "showing" || panelState == "open";
  },

  /**
   * Called by the consumer to indicate that the current browser's location has
   * changed, so that we can update the active notifications accordingly.
   */
  locationChange: function PopupNotifications_locationChange() {
    this._currentNotifications = this._currentNotifications.filter(function(notification) {
      // The persistence option allows a notification to persist across multiple
      // page loads
      if ("persistence" in notification.options &&
          notification.options.persistence) {
        notification.options.persistence--;
        return true;
      }

      // The timeout option allows a notification to persist until a certain time
      if ("timeout" in notification.options &&
          Date.now() <= notification.options.timeout) {
        return true;
      }

      return false;
    });

    this._update();
  },

  /**
   * Removes a Notification.
   * @param notification
   *        The Notification object to remove.
   */
  remove: function PopupNotifications_remove(notification) {
    let isCurrent = this._currentNotifications.indexOf(notification) != -1;
    this._remove(notification);

    // update the panel, if needed
    if (this.isPanelOpen && isCurrent)
      this._update();
  },

////////////////////////////////////////////////////////////////////////////////
// Utility methods
////////////////////////////////////////////////////////////////////////////////

  /**
   * Gets and sets notifications for the currently selected browser.
   */
  get _currentNotifications() {
    return this._getNotificationsForBrowser(this.tabbrowser.selectedBrowser);
  },
  set _currentNotifications(a) {
    return this.tabbrowser.selectedBrowser.popupNotifications = a;
  },

  _remove: function PopupNotifications_removeHelper(notification) {
    // This notification may already be removed, in which case let's just fail
    // silently.
    let notifications = this._getNotificationsForBrowser(notification.browser);
    if (!notifications)
      return;

    var index = notifications.indexOf(notification);
    if (index == -1)
      return;

    // remove the notification
    notifications.splice(index, 1);
  },

  /**
   * Hides the notification popup.
   */
  _hidePanel: function PopupNotifications_hide() {
    this._ignoreDismissal = true;
    this.panel.hidePopup();
    this._ignoreDismissal = false;
  },

  /**
   *
   */
  _refreshPanel: function PopupNotifications_refreshPanel(notificationsToShow) {
    while (this.panel.lastChild)
      this.panel.removeChild(this.panel.lastChild);

    const XUL_NS = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";

    notificationsToShow.forEach(function (n) {
      let doc = this.window.document;
      let popupnotification = doc.createElementNS(XUL_NS, "popupnotification");
      popupnotification.setAttribute("label", n.message);
      // Append "-notification" to the ID to try to avoid ID conflicts with other stuff
      // in the document.
      popupnotification.setAttribute("id", n.id + "-notification");
      popupnotification.setAttribute("popupid", n.id);
      if (n.mainAction) {
        popupnotification.setAttribute("buttonlabel", n.mainAction.label);
        popupnotification.setAttribute("buttonaccesskey", n.mainAction.accessKey);
        popupnotification.setAttribute("buttoncommand", "PopupNotifications._onButtonCommand(event);");
        if (n.secondaryActions.length) {
          popupnotification.setAttribute("buttontype", "menu-button");
          popupnotification.setAttribute("menucommand", "PopupNotifications._onMenuCommand(event);");
        }
      }
      popupnotification.notification = n;

      this.panel.appendChild(popupnotification);

      if (n.secondaryActions) {
        n.secondaryActions.forEach(function (a) {
          let item = doc.createElementNS(XUL_NS, "menuitem");
          item.setAttribute("label", a.label);
          item.setAttribute("accesskey", a.accessKey);
          item.notification = n;
          item.action = a;

          popupnotification.appendChild(item);
        }, this);
      }
    }, this);
  },

  _showPanel: function PopupNotifications_showPanel(notificationsToShow, anchorElement) {
    this.panel.hidden = false;

    this._refreshPanel(notificationsToShow);

    if (this.isPanelOpen && this._currentAnchorElement == anchorElement)
      return;

    // Make sure the identity popup hangs in the correct direction.
    var position = (this.window.getComputedStyle(this.panel, "").direction == "rtl") ? "after_end" : "after_start";

    this._currentAnchorElement = anchorElement;

    this.panel.openPopup(anchorElement, position);
  },

  /**
   * Updates the notification state in response to window activation or tab
   * selection changes.
   */
  _update: function PopupNotifications_update(anchor) {
    let anchorElement, notificationsToShow = [];
    let haveNotifications = this._currentNotifications.length > 0;
    if (haveNotifications) {
      // Only show the notifications that have the passed-in anchor (or the
      // first notification's anchor, if none was passed in). Other
      // notifications will be shown once these are dismissed.
      anchorElement = anchor || this._currentNotifications[0].anchorElement;

      this.iconBox.hidden = false;
      this.iconBox.setAttribute("anchorid", anchorElement.id);

      // Also filter out notifications that have been dismissed.
      notificationsToShow = this._currentNotifications.filter(function (n) {
        return !n.dismissed && n.anchorElement == anchorElement &&
               !n.options.neverShow;
      });
    }

    if (notificationsToShow.length > 0) {
      this._showPanel(notificationsToShow, anchorElement);
    } else {
      // Notify observers that we're not showing the popup (useful for testing)
      this._notify("updateNotShowing");

      this._hidePanel();

      // Only hide the iconBox if we actually have no notifications (as opposed
      // to not having any showable notifications)
      if (this.iconBox && !haveNotifications)
        this.iconBox.hidden = true;
    }
  },

  _getNotificationsForBrowser: function PopupNotifications_getNotifications(browser) {
    if (browser.popupNotifications)
      return browser.popupNotifications;

    return browser.popupNotifications = [];
  },

  _onIconBoxCommand: function PopupNotifications_onIconBoxCommand(event) {
    // Left click, space or enter only
    let type = event.type;
    if (type == "click" && event.button != 0)
      return;

    if (type == "keypress" &&
        !(event.charCode == Ci.nsIDOMKeyEvent.DOM_VK_SPACE ||
          event.keyCode == Ci.nsIDOMKeyEvent.DOM_VK_RETURN))
      return;

    if (this._currentNotifications.length == 0)
      return;

    // Get the anchor that is the immediate child of the icon box
    let anchor = event.target;
    while (anchor && anchor.parentNode != this.iconBox)
      anchor = anchor.parentNode;

    // Mark notifications anchored to this anchor as un-dismissed
    this._currentNotifications.forEach(function (n) {
      if (n.anchorElement == anchor)
        n.dismissed = false;
    });

    // ...and then show them.
    this._update(anchor);
  },

  _onPopupHidden: function PopupNotifications_onPopupHidden(event) {
    if (event.target != this.panel || this._ignoreDismissal)
      return;

    // Mark notifications as dismissed
    Array.forEach(this.panel.childNodes, function (nEl) {
      let notificationObj = nEl.notification;
      notificationObj.dismissed = true;
    }, this);

    this._update();
  },

  _onButtonCommand: function PopupNotifications_onButtonCommand(event) {
    // Need to find the associated notification object, which is a bit tricky
    // since it isn't associated with the button directly - this is kind of
    // gross and very dependent on the structure of the popupnotification
    // binding's content.
    let target = event.originalTarget;
    let notificationEl;
    let parent = target;
    while (parent && (parent = target.ownerDocument.getBindingParent(parent)))
      notificationEl = parent;

    if (!notificationEl)
      throw "PopupNotifications_onButtonCommand: couldn't find notification element";

    if (!notificationEl.notification)
      throw "PopupNotifications_onButtonCommand: couldn't find notification";

    let notification = notificationEl.notification;
    notification.mainAction.callback.call();

    this._remove(notification);
    this._update();
  },

  _onMenuCommand: function PopupNotifications_onMenuCommand(event) {
    let target = event.originalTarget;
    if (!target.action || !target.notification)
      throw "menucommand target has no associated action/notification";

    event.stopPropagation();
    target.action.callback.call();

    this._remove(target.notification);
    this._update();
  },

  _notify: function PopupNotifications_notify(topic) {
    Services.obs.notifyObservers(null, "PopupNotifications-" + topic, "");
  }
}
