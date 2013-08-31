// -*- Mode: js2; tab-width: 2; indent-tabs-mode: nil; js2-basic-offset: 2; js2-skip-preprocessor-directives: t; -*-
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var ContextCommands = {
  copy: function cc_copy() {
    let clipboard = Cc["@mozilla.org/widget/clipboardhelper;1"].getService(Ci.nsIClipboardHelper);
    clipboard.copyString(ContextHelper.popupState.string, Browser.contentWindow.document);

    let target = ContextHelper.popupState.target;
    if (target)
      target.focus();
  },

#ifdef ANDROID
  selectInput: function cc_selectInput() {
    let imePicker = Cc["@mozilla.org/imepicker;1"].getService(Ci.nsIIMEPicker);
    imePicker.show();
  },
#endif

  paste: function cc_paste() {
    let target = ContextHelper.popupState.target;
    if (target.localName == "browser") {
      let x = ContextHelper.popupState.x;
      let y = ContextHelper.popupState.y;
      let json = {x: x, y: y, command: "paste" };
      target.messageManager.sendAsyncMessage("Browser:ContextCommand", json);
    } else {
      target.editor.paste(Ci.nsIClipboard.kGlobalClipboard);
      target.focus();
    }
  },

  pasteAndGo: function cc_pasteAndGo() {
    let target = ContextHelper.popupState.target;
    target.editor.selectAll();
    target.editor.paste(Ci.nsIClipboard.kGlobalClipboard);
    BrowserUI.goToURI();
  },

  selectAll: function cc_selectAll() {
    let target = ContextHelper.popupState.target;
    if (target.localName == "browser") {
      let x = ContextHelper.popupState.x;
      let y = ContextHelper.popupState.y;
      let json = {x: x, y: y, command: "select-all" };
      target.messageManager.sendAsyncMessage("Browser:ContextCommand", json);
    } else {
      target.editor.selectAll();
      target.focus();
    }
  },

  openInNewTab: function cc_openInNewTab() {
    Browser.addTab(ContextHelper.popupState.linkURL, false, Browser.selectedTab);
  },

  saveImage: function cc_saveImage() {
    let popupState = ContextHelper.popupState;
    let browser = popupState.target;

    // Bug 638523
    // Using directly SaveImageURL fails here since checking the cache for a
    // remote page seems to not work (could it be nsICacheSession prohibition)?
    ContentAreaUtils.internalSave(popupState.mediaURL, null, null,
                                  popupState.contentDisposition,
                                  popupState.contentType, false, "SaveImageTitle",
                                  null, browser.documentURI, true, null);
  },

  copyLink: function cc_copyLink() {
    let clipboard = Cc["@mozilla.org/widget/clipboardhelper;1"].getService(Ci.nsIClipboardHelper);
    clipboard.copyString(ContextHelper.popupState.linkURL, Browser.contentWindow.document);
  },

  copyEmail: function cc_copyEmail() {
      let clipboard = Cc["@mozilla.org/widget/clipboardhelper;1"].getService(Ci.nsIClipboardHelper);
      clipboard.copyString(ContextHelper.popupState.linkURL.substr(ContextHelper.popupState.linkURL.indexOf(':')+1), Browser.contentWindow.document);
  },

  copyPhone: function cc_copyPhone() {
      let clipboard = Cc["@mozilla.org/widget/clipboardhelper;1"].getService(Ci.nsIClipboardHelper);
      clipboard.copyString(ContextHelper.popupState.linkURL.substr(ContextHelper.popupState.linkURL.indexOf(':')+1), Browser.contentWindow.document);
  },

  copyImageLocation: function cc_copyImageLocation() {
      let clipboard = Cc["@mozilla.org/widget/clipboardhelper;1"].getService(Ci.nsIClipboardHelper);
      clipboard.copyString(ContextHelper.popupState.mediaURL, Browser.contentWindow.document);
  },

  shareLink: function cc_shareLink() {
    let state = ContextHelper.popupState;
    SharingUI.show(state.linkURL, state.linkTitle);
  },

  shareMedia: function cc_shareMedia() {
    SharingUI.show(ContextHelper.popupState.mediaURL, null);
  },

  bookmarkLink: function cc_bookmarkLink() {
    let state = ContextHelper.popupState;
    let bookmarks = PlacesUtils.bookmarks;
    try {
      bookmarks.insertBookmark(BookmarkList.panel.mobileRoot,
                               Util.makeURI(state.linkURL),
                               bookmarks.DEFAULT_INDEX,
                               state.linkTitle || state.linkURL);
    } catch (e) {
      return;
    }

    let message = Strings.browser.GetStringFromName("alertLinkBookmarked");
    let toaster = Cc["@mozilla.org/toaster-alerts-service;1"].getService(Ci.nsIAlertsService);
    toaster.showAlertNotification(null, message, "", false, "", null);
  },

  sendCommand: function cc_playVideo(aCommand) {
    let browser = ContextHelper.popupState.target;
    browser.messageManager.sendAsyncMessage("Browser:ContextCommand", { command: aCommand });
  },

  editBookmark: function cc_editBookmark() {
    let target = ContextHelper.popupState.target;
    target.startEditing();
  },

  removeBookmark: function cc_removeBookmark() {
    let target = ContextHelper.popupState.target;
    target.remove();
  },

  shortcutBookmark: function cc_shortcutBookmark() {
    let target = ContextHelper.popupState.target;
    Util.createShortcut(target.getAttribute("title"), target.getAttribute("uri"), target.getAttribute("src"), "bookmark");
  }
};
