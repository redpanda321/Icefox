# -*- Mode: Java; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is mozilla.org code.
#
# The Initial Developer of the Original Code is
# Netscape Communications Corporation.
# Portions created by the Initial Developer are Copyright (C) 1998
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Alec Flett <alecf@netscape.com>
#   Ehsan Akhgari <ehsan.akhgari@gmail.com>
#   Gavin Sharp <gavin@gavinsharp.com>
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****

// Services = object with smart getters for common XPCOM services
Components.utils.import("resource://gre/modules/Services.jsm");

var TAB_DROP_TYPE = "application/x-moz-tabbrowser-tab";

var gBidiUI = false;

function getBrowserURL()
{
  return "chrome://browser/content/browser.xul";
}

function goToggleToolbar( id, elementID )
{
  var toolbar = document.getElementById(id);
  var element = document.getElementById(elementID);
  if (toolbar)
  {
    var isHidden = toolbar.hidden;
    toolbar.hidden = !isHidden;
    document.persist(id, 'hidden');
    if (element) {
      element.setAttribute("checked", isHidden ? "true" : "false");
      document.persist(elementID, 'checked');
    }
  }
}

function getTopWin()
{
  return Services.wm.getMostRecentWindow("navigator:browser");
}

function openTopWin( url )
{
  openUILink(url, {})
}

function getBoolPref(prefname, def)
{
  try {
    return Services.prefs.getBoolPref(prefname);
  }
  catch(er) {
    return def;
  }
}

// openUILink handles clicks on UI elements that cause URLs to load.
function openUILink( url, e, ignoreButton, ignoreAlt, allowKeywordFixup, postData, referrerUrl )
{
  var where = whereToOpenLink(e, ignoreButton, ignoreAlt);
  openUILinkIn(url, where, allowKeywordFixup, postData, referrerUrl);
}


/* whereToOpenLink() looks at an event to decide where to open a link.
 *
 * The event may be a mouse event (click, double-click, middle-click) or keypress event (enter).
 *
 * On Windows, the modifiers are:
 * Ctrl        new tab, selected
 * Shift       new window
 * Ctrl+Shift  new tab, in background
 * Alt         save
 *
 * You can swap Ctrl and Ctrl+shift by toggling the hidden pref
 * browser.tabs.loadBookmarksInBackground (not browser.tabs.loadInBackground, which
 * is for content area links).
 *
 * Middle-clicking is the same as Ctrl+clicking (it opens a new tab) and it is
 * subject to the shift modifier and pref in the same way.
 *
 * Exceptions: 
 * - Alt is ignored for menu items selected using the keyboard so you don't accidentally save stuff.  
 *    (Currently, the Alt isn't sent here at all for menu items, but that will change in bug 126189.)
 * - Alt is hard to use in context menus, because pressing Alt closes the menu.
 * - Alt can't be used on the bookmarks toolbar because Alt is used for "treat this as something draggable".
 * - The button is ignored for the middle-click-paste-URL feature, since it's always a middle-click.
 */
function whereToOpenLink( e, ignoreButton, ignoreAlt )
{
  // This method must treat a null event like a left click without modifier keys (i.e.
  // e = { shiftKey:false, ctrlKey:false, metaKey:false, altKey:false, button:0 })
  // for compatibility purposes.
  if (!e)
    return "current";

  var shift = e.shiftKey;
  var ctrl =  e.ctrlKey;
  var meta =  e.metaKey;
  var alt  =  e.altKey && !ignoreAlt;

  // ignoreButton allows "middle-click paste" to use function without always opening in a new window.
  var middle = !ignoreButton && e.button == 1;
  var middleUsesTabs = getBoolPref("browser.tabs.opentabfor.middleclick", true);

  // Don't do anything special with right-mouse clicks.  They're probably clicks on context menu items.

#ifdef XP_MACOSX
  if (meta || (middle && middleUsesTabs))
#else
  if (ctrl || (middle && middleUsesTabs))
#endif
    return shift ? "tabshifted" : "tab";

  if (alt)
    return "save";

  if (shift || (middle && !middleUsesTabs))
    return "window";

  return "current";
}

/* openUILinkIn opens a URL in a place specified by the parameter |where|.
 *
 * |where| can be:
 *  "current"     current tab            (if there aren't any browser windows, then in a new window instead)
 *  "tab"         new tab                (if there aren't any browser windows, then in a new window instead)
 *  "tabshifted"  same as "tab" but in background if default is to select new tabs, and vice versa
 *  "window"      new window
 *  "save"        save to disk (with no filename hint!)
 *
 * aAllowThirdPartyFixup controls whether third party services such as Google's
 * I Feel Lucky are allowed to interpret this URL. This parameter may be
 * undefined, which is treated as false.
 *
 * Instead of aAllowThirdPartyFixup, you may also pass an object with any of
 * these properties:
 *   allowThirdPartyFixup (boolean)
 *   postData             (nsIInputStream)
 *   referrerURI          (nsIURI)
 *   relatedToCurrent     (boolean)
 */
function openUILinkIn(url, where, aAllowThirdPartyFixup, aPostData, aReferrerURI) {
  if (!where || !url)
    return;

  var aRelatedToCurrent;
  if (arguments.length == 3 &&
      typeof arguments[2] == "object") {
    let params = arguments[2];
    aAllowThirdPartyFixup = params.allowThirdPartyFixup;
    aPostData             = params.postData;
    aReferrerURI          = params.referrerURI;
    aRelatedToCurrent     = params.relatedToCurrent;
  }

  if (where == "save") {
    saveURL(url, null, null, true, null, aReferrerURI);
    return;
  }
  const Cc = Components.classes;
  const Ci = Components.interfaces;

  var w = getTopWin();

  if (!w || where == "window") {
    var sa = Cc["@mozilla.org/supports-array;1"].
             createInstance(Ci.nsISupportsArray);

    var wuri = Cc["@mozilla.org/supports-string;1"].
               createInstance(Ci.nsISupportsString);
    wuri.data = url;

    var allowThirdPartyFixupSupports = Cc["@mozilla.org/supports-PRBool;1"].
                                       createInstance(Ci.nsISupportsPRBool);
    allowThirdPartyFixupSupports.data = aAllowThirdPartyFixup;

    sa.AppendElement(wuri);
    sa.AppendElement(null);
    sa.AppendElement(aReferrerURI);
    sa.AppendElement(aPostData);
    sa.AppendElement(allowThirdPartyFixupSupports);

    var ww = Cc["@mozilla.org/embedcomp/window-watcher;1"].
             getService(Ci.nsIWindowWatcher);

    ww.openWindow(w || window,
                  getBrowserURL(),
                  null,
                  "chrome,dialog=no,all",
                  sa);

    return;
  }

  var loadInBackground = getBoolPref("browser.tabs.loadBookmarksInBackground");

  switch (where) {
  case "current":
    w.loadURI(url, aReferrerURI, aPostData, aAllowThirdPartyFixup);
    break;
  case "tabshifted":
    loadInBackground = !loadInBackground;
    // fall through
  case "tab":
    let browser = w.gBrowser;
    browser.loadOneTab(url, {
                       referrerURI: aReferrerURI,
                       postData: aPostData,
                       inBackground: loadInBackground,
                       allowThirdPartyFixup: aAllowThirdPartyFixup,
                       relatedToCurrent: aRelatedToCurrent});
    break;
  }

  // If this window is active, focus the target window. Otherwise, focus the
  // content but don't raise the window, since the URI we just loaded may have
  // resulted in a new frontmost window (e.g. "javascript:window.open('');").
  var fm = Components.classes["@mozilla.org/focus-manager;1"].
             getService(Components.interfaces.nsIFocusManager);
  if (window == fm.activeWindow)
    w.content.focus();
  else
    w.gBrowser.selectedBrowser.focus();
}

// Used as an onclick handler for UI elements with link-like behavior.
// e.g. onclick="checkForMiddleClick(this, event);"
function checkForMiddleClick(node, event) {
  // We should be using the disabled property here instead of the attribute,
  // but some elements that this function is used with don't support it (e.g.
  // menuitem).
  if (node.getAttribute("disabled") == "true")
    return; // Do nothing

  if (event.button == 1) {
    /* Execute the node's oncommand or command.
     *
     * XXX: we should use node.oncommand(event) once bug 246720 is fixed.
     */
    var target = node.hasAttribute("oncommand") ? node :
                 node.ownerDocument.getElementById(node.getAttribute("command"));
    var fn = new Function("event", target.getAttribute("oncommand"));
    fn.call(target, event);

    // If the middle-click was on part of a menu, close the menu.
    // (Menus close automatically with left-click but not with middle-click.)
    closeMenus(event.target);
  }
}

// Closes all popups that are ancestors of the node.
function closeMenus(node)
{
  if ("tagName" in node) {
    if (node.namespaceURI == "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul"
    && (node.tagName == "menupopup" || node.tagName == "popup"))
      node.hidePopup();

    closeMenus(node.parentNode);
  }
}

// Gather all descendent text under given document node.
function gatherTextUnder ( root ) 
{
  var text = "";
  var node = root.firstChild;
  var depth = 1;
  while ( node && depth > 0 ) {
    // See if this node is text.
    if ( node.nodeType == Node.TEXT_NODE ) {
      // Add this text to our collection.
      text += " " + node.data;
    } else if ( node instanceof HTMLImageElement) {
      // If it has an alt= attribute, use that.
      var altText = node.getAttribute( "alt" );
      if ( altText && altText != "" ) {
        text = altText;
        break;
      }
    }
    // Find next node to test.
    // First, see if this node has children.
    if ( node.hasChildNodes() ) {
      // Go to first child.
      node = node.firstChild;
      depth++;
    } else {
      // No children, try next sibling.
      if ( node.nextSibling ) {
        node = node.nextSibling;
      } else {
        // Last resort is our next oldest uncle/aunt.
        node = node.parentNode.nextSibling;
        depth--;
      }
    }
  }
  // Strip leading whitespace.
  text = text.replace( /^\s+/, "" );
  // Strip trailing whitespace.
  text = text.replace( /\s+$/, "" );
  // Compress remaining whitespace.
  text = text.replace( /\s+/g, " " );
  return text;
}

function getShellService()
{
  var shell = null;
  try {
    shell = Components.classes["@mozilla.org/browser/shell-service;1"]
      .getService(Components.interfaces.nsIShellService);
  } catch (e) {dump("*** e = " + e + "\n");}
  return shell;
}

function isBidiEnabled() {
  // first check the pref.
  if (getBoolPref("bidi.browser.ui", false))
    return true;

  // if the pref isn't set, check for an RTL locale and force the pref to true
  // if we find one.
  var rv = false;

  try {
    var localeService = Components.classes["@mozilla.org/intl/nslocaleservice;1"]
                                  .getService(Components.interfaces.nsILocaleService);
    var systemLocale = localeService.getSystemLocale().getCategory("NSILOCALE_CTYPE").substr(0,3);

    switch (systemLocale) {
      case "ar-":
      case "he-":
      case "fa-":
      case "ur-":
      case "syr":
        rv = true;
        Services.prefs.setBoolPref("bidi.browser.ui", true);
    }
  } catch (e) {}

  return rv;
}

function openAboutDialog() {
  var enumerator = Services.wm.getEnumerator("Browser:About");
  while (enumerator.hasMoreElements()) {
    let win = enumerator.getNext();
#ifdef XP_WIN
    if (win.opener != window)
      continue;
#endif
    win.focus();
    return;
  }

#ifdef XP_MACOSX
  var features = "chrome,resizable=no,minimizable=no";
#else
#ifdef XP_WIN
  var features = "chrome,centerscreen,dependent";
#else
  var features = "chrome,centerscreen";
#endif
#endif
  window.openDialog("chrome://browser/content/aboutDialog.xul", "", features);
}

function openPreferences(paneID, extraArgs)
{
  var instantApply = getBoolPref("browser.preferences.instantApply", false);
  var features = "chrome,titlebar,toolbar,centerscreen" + (instantApply ? ",dialog=no" : ",modal");

  var win = Services.wm.getMostRecentWindow("Browser:Preferences");
  if (win) {
    win.focus();
    if (paneID) {
      var pane = win.document.getElementById(paneID);
      win.document.documentElement.showPane(pane);
    }

    if (extraArgs && extraArgs["advancedTab"]) {
      var advancedPaneTabs = win.document.getElementById("advancedPrefs");
      advancedPaneTabs.selectedTab = win.document.getElementById(extraArgs["advancedTab"]);
    }

    return win;
  }

  return openDialog("chrome://browser/content/preferences/preferences.xul",
                    "Preferences", features, paneID, extraArgs);
}

function openAdvancedPreferences(tabID)
{
  return openPreferences("paneAdvanced", { "advancedTab" : tabID });
}

/**
 * Opens the release notes page for this version of the application.
 */
function openReleaseNotes()
{
  var formatter = Components.classes["@mozilla.org/toolkit/URLFormatterService;1"]
                            .getService(Components.interfaces.nsIURLFormatter);
  var relnotesURL = formatter.formatURLPref("app.releaseNotesURL");
  
  openUILinkIn(relnotesURL, "tab");
}

/**
 * Opens the troubleshooting information (about:support) page for this version
 * of the application.
 */
function openTroubleshootingPage()
{
  openUILinkIn("about:support", "tab");
}

/**
 * Opens the feedback page for this version of the application.
 */
function openFeedbackPage()
{
  openUILinkIn("http://input.mozilla.com/sad", "tab");
}


#ifdef MOZ_UPDATER
/**
 * Opens the update manager and checks for updates to the application.
 */
function checkForUpdates()
{
  var um = 
      Components.classes["@mozilla.org/updates/update-manager;1"].
      getService(Components.interfaces.nsIUpdateManager);
  var prompter = 
      Components.classes["@mozilla.org/updates/update-prompt;1"].
      createInstance(Components.interfaces.nsIUpdatePrompt);

  // If there's an update ready to be applied, show the "Update Downloaded"
  // UI instead and let the user know they have to restart the browser for
  // the changes to be applied. 
  if (um.activeUpdate && um.activeUpdate.state == "pending")
    prompter.showUpdateDownloaded(um.activeUpdate);
  else
    prompter.checkForUpdates();
}
#endif

function buildHelpMenu()
{
  // Enable/disable the "Report Web Forgery" menu item.  safebrowsing object
  // may not exist in OSX
  if (typeof safebrowsing != "undefined")
    safebrowsing.setReportPhishingMenu();

#ifdef MOZ_UPDATER
  var updates = 
      Components.classes["@mozilla.org/updates/update-service;1"].
      getService(Components.interfaces.nsIApplicationUpdateService);
  var um = 
      Components.classes["@mozilla.org/updates/update-manager;1"].
      getService(Components.interfaces.nsIUpdateManager);

  // Disable the UI if the update enabled pref has been locked by the 
  // administrator or if we cannot update for some other reason
  var checkForUpdates = document.getElementById("checkForUpdates");
  var canCheckForUpdates = updates.canCheckForUpdates;
  checkForUpdates.setAttribute("disabled", !canCheckForUpdates);
  if (!canCheckForUpdates)
    return; 

  var strings = document.getElementById("bundle_browser");
  var activeUpdate = um.activeUpdate;
  
  // If there's an active update, substitute its name into the label
  // we show for this item, otherwise display a generic label.
  function getStringWithUpdateName(key) {
    if (activeUpdate && activeUpdate.name)
      return strings.getFormattedString(key, [activeUpdate.name]);
    return strings.getString(key + "Fallback");
  }
  
  // By default, show "Check for Updates..."
  var key = "default";
  if (activeUpdate) {
    switch (activeUpdate.state) {
    case "downloading":
      // If we're downloading an update at present, show the text:
      // "Downloading Firefox x.x..." otherwise we're paused, and show
      // "Resume Downloading Firefox x.x..."
      key = updates.isDownloading ? "downloading" : "resume";
      break;
    case "pending":
      // If we're waiting for the user to restart, show: "Apply Downloaded
      // Updates Now..."
      key = "pending";
      break;
    }
  }
  checkForUpdates.label = getStringWithUpdateName("updatesItem_" + key);
  checkForUpdates.accessKey = strings.getString("updatesItem_" + key + ".accesskey");
  if (um.activeUpdate && updates.isDownloading)
    checkForUpdates.setAttribute("loading", "true");
  else
    checkForUpdates.removeAttribute("loading");
#else
  // Needed by safebrowsing for inserting its menuitem so just hide it
  document.getElementById("updateSeparator").hidden = true;
#endif
}

function isElementVisible(aElement)
{
  if (!aElement)
    return false;

  // If aElement or a direct or indirect parent is hidden or collapsed,
  // height, width or both will be 0.
  var bo = aElement.boxObject;
  return (bo.height > 0 && bo.width > 0);
}

function makeURLAbsolute(aBase, aUrl)
{
  // Note:  makeURI() will throw if aUri is not a valid URI
  return makeURI(aUrl, null, makeURI(aBase)).spec;
}


/**
 * openNewTabWith: opens a new tab with the given URL.
 *
 * @param aURL
 *        The URL to open (as a string).
 * @param aDocument
 *        The document from which the URL came, or null. This is used to set the
 *        referrer header and to do a security check of whether the document is
 *        allowed to reference the URL. If null, there will be no referrer
 *        header and no security check.
 * @param aPostData
 *        Form POST data, or null.
 * @param aEvent
 *        The triggering event (for the purpose of determining whether to open
 *        in the background), or null.
 * @param aAllowThirdPartyFixup
 *        If true, then we allow the URL text to be sent to third party services
 *        (e.g., Google's I Feel Lucky) for interpretation. This parameter may
 *        be undefined in which case it is treated as false.
 * @param [optional] aReferrer
 *        If aDocument is null, then this will be used as the referrer.
 *        There will be no security check.
 */ 
function openNewTabWith(aURL, aDocument, aPostData, aEvent,
                        aAllowThirdPartyFixup, aReferrer)
{
  if (aDocument)
    urlSecurityCheck(aURL, aDocument.nodePrincipal);

  var loadInBackground = getBoolPref("browser.tabs.loadInBackground");

  if (aEvent && aEvent.shiftKey)
    loadInBackground = !loadInBackground;

  // As in openNewWindowWith(), we want to pass the charset of the
  // current document over to a new tab. 
  var wintype = document.documentElement.getAttribute("windowtype");
  var originCharset;
  if (wintype == "navigator:browser")
    originCharset = window.content.document.characterSet;

  // open link in new tab
  var referrerURI = aDocument ? aDocument.documentURIObject : aReferrer;
  var browser = top.document.getElementById("content");
  return browser.loadOneTab(aURL, {
                            referrerURI: referrerURI,
                            charset: originCharset,
                            postData: aPostData,
                            inBackground: loadInBackground,
                            allowThirdPartyFixup: aAllowThirdPartyFixup});
}

function openNewWindowWith(aURL, aDocument, aPostData, aAllowThirdPartyFixup,
                           aReferrer)
{
  if (aDocument)
    urlSecurityCheck(aURL, aDocument.nodePrincipal);

  // if and only if the current window is a browser window and it has a
  // document with a character set, then extract the current charset menu
  // setting from the current document and use it to initialize the new browser
  // window...
  var charsetArg = null;
  var wintype = document.documentElement.getAttribute("windowtype");
  if (wintype == "navigator:browser")
    charsetArg = "charset=" + window.content.document.characterSet;

  var referrerURI = aDocument ? aDocument.documentURIObject : aReferrer;
  return window.openDialog(getBrowserURL(), "_blank", "chrome,all,dialog=no",
                           aURL, charsetArg, referrerURI, aPostData,
                           aAllowThirdPartyFixup);
}

/**
 * isValidFeed: checks whether the given data represents a valid feed.
 *
 * @param  aLink
 *         An object representing a feed with title, href and type.
 * @param  aPrincipal
 *         The principal of the document, used for security check.
 * @param  aIsFeed
 *         Whether this is already a known feed or not, if true only a security
 *         check will be performed.
 */ 
function isValidFeed(aLink, aPrincipal, aIsFeed)
{
  if (!aLink || !aPrincipal)
    return false;

  var type = aLink.type.toLowerCase().replace(/^\s+|\s*(?:;.*)?$/g, "");
  if (!aIsFeed) {
    aIsFeed = (type == "application/rss+xml" ||
               type == "application/atom+xml");
  }

  if (aIsFeed) {
    try {
      urlSecurityCheck(aLink.href, aPrincipal,
                       Components.interfaces.nsIScriptSecurityManager.DISALLOW_INHERIT_PRINCIPAL);
      return type || "application/rss+xml";
    }
    catch(ex) {
    }
  }

  return null;
}

// aCalledFromModal is optional
function openHelpLink(aHelpTopic, aCalledFromModal) {
  var url = Components.classes["@mozilla.org/toolkit/URLFormatterService;1"]
                      .getService(Components.interfaces.nsIURLFormatter)
                      .formatURLPref("app.support.baseURL");
  url += aHelpTopic;

  var where = aCalledFromModal ? "window" : "tab";
  openUILinkIn(url, where);
}

function openPrefsHelp() {
  // non-instant apply prefwindows are usually modal, so we can't open in the topmost window, 
  // since its probably behind the window.
  var instantApply = getBoolPref("browser.preferences.instantApply");

  var helpTopic = document.getElementsByTagName("prefwindow")[0].currentPane.helpTopic;
  openHelpLink(helpTopic, !instantApply);
}
