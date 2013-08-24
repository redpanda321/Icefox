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
# The Original Code is Private Browsing.
#
# The Initial Developer of the Original Code is
# Ehsan Akhgari.
# Portions created by the Initial Developer are Copyright (C) 2008
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#  Ehsan Akhgari <ehsan.akhgari@gmail.com> (Original Author)
#  Simon Bünzli <zeniko@gmail.com>
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

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

#ifndef XP_WIN
#define BROKEN_WM_Z_ORDER
#endif

////////////////////////////////////////////////////////////////////////////////
//// Utilities

/**
 * Returns true if the string passed in is part of the root domain of the
 * current string.  For example, if this is "www.mozilla.org", and we pass in
 * "mozilla.org", this will return true.  It would return false the other way
 * around.
 */
String.prototype.hasRootDomain = function hasRootDomain(aDomain)
{
  let index = this.indexOf(aDomain);
  // If aDomain is not found, we know we do not have it as a root domain.
  if (index == -1)
    return false;

  // If the strings are the same, we obviously have a match.
  if (this == aDomain)
    return true;

  // Otherwise, we have aDomain as our root domain iff the index of aDomain is
  // aDomain.length subtracted from our length and (since we do not have an
  // exact match) the character before the index is a dot or slash.
  let prevChar = this[index - 1];
  return (index == (this.length - aDomain.length)) &&
         (prevChar == "." || prevChar == "/");
}

////////////////////////////////////////////////////////////////////////////////
//// Constants

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;
const Cr = Components.results;

const STATE_IDLE = 0;
const STATE_TRANSITION_STARTED = 1;
const STATE_WAITING_FOR_RESTORE = 2;
const STATE_RESTORE_FINISHED = 3;

////////////////////////////////////////////////////////////////////////////////
//// PrivateBrowsingService

function PrivateBrowsingService() {
  this._obs = Cc["@mozilla.org/observer-service;1"].
              getService(Ci.nsIObserverService);
  this._obs.addObserver(this, "profile-after-change", true);
  this._obs.addObserver(this, "quit-application-granted", true);
  this._obs.addObserver(this, "private-browsing", true);
  this._obs.addObserver(this, "command-line-startup", true);
  this._obs.addObserver(this, "sessionstore-browser-state-restored", true);
}

PrivateBrowsingService.prototype = {
  // Preferences Service
  get _prefs() {
    let prefs = Cc["@mozilla.org/preferences-service;1"].
                getService(Ci.nsIPrefBranch);
    this.__defineGetter__("_prefs", function() prefs);
    return this._prefs;
  },

  // Whether the private browsing mode is currently active or not.
  _inPrivateBrowsing: false,

  // Saved browser state before entering the private mode.
  _savedBrowserState: null,

  // Whether we're in the process of shutting down
  _quitting: false,

  // How to treat the non-private session
  _saveSession: true,

  // The current status of the private browsing service
  _currentStatus: STATE_IDLE,

  // Whether the private browsing mode has been started automatically (ie. always-on)
  _autoStarted: false,

  // List of view source window URIs for restoring later
  _viewSrcURLs: [],

  // List of nsIXULWindows we are going to be closing during the transition
  _windowsToClose: [],

  // XPCOM registration
  classID: Components.ID("{c31f4883-839b-45f6-82ad-a6a9bc5ad599}"),

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIPrivateBrowsingService, 
                                         Ci.nsIObserver,
                                         Ci.nsISupportsWeakReference,
                                         Ci.nsICommandLineHandler]),

  _unload: function PBS__destroy() {
    // Force an exit from the private browsing mode on shutdown
    this._quitting = true;
    if (this._inPrivateBrowsing)
      this.privateBrowsingEnabled = false;
  },

  _onBeforePrivateBrowsingModeChange: function PBS__onBeforePrivateBrowsingModeChange() {
    // nothing needs to be done here if we're enabling at startup
    if (!this._autoStarted) {
      let ss = Cc["@mozilla.org/browser/sessionstore;1"].
               getService(Ci.nsISessionStore);
      let blankState = JSON.stringify({
        "windows": [{
          "tabs": [{
            "entries": [{
              "url": "about:blank"
            }]
          }],
          "_closedTabs": []
        }]
      });

      if (this._inPrivateBrowsing) {
        // save the whole browser state in order to restore all windows/tabs later
        if (this._saveSession && !this._savedBrowserState) {
          if (this._getBrowserWindow())
            this._savedBrowserState = ss.getBrowserState();
          else // no open browser windows, just restore a blank state on exit
            this._savedBrowserState = blankState;
        }
      }

      this._closePageInfoWindows();

      // save view-source windows URIs and close them
      let viewSrcWindowsEnum = Cc["@mozilla.org/appshell/window-mediator;1"].
                               getService(Ci.nsIWindowMediator).
                               getEnumerator("navigator:view-source");
      while (viewSrcWindowsEnum.hasMoreElements()) {
        let win = viewSrcWindowsEnum.getNext();
        if (this._inPrivateBrowsing) {
          let plainURL = win.gBrowser.currentURI.spec;
          if (plainURL.indexOf("view-source:") == 0) {
            plainURL = plainURL.substr(12);
            this._viewSrcURLs.push(plainURL);
          }
        }
        win.close();
      }

      if (!this._quitting && this._saveSession) {
        let browserWindow = this._getBrowserWindow();

        // if there are open browser windows, load a dummy session to get a distinct 
        // separation between private and non-private sessions
        if (browserWindow) {
          // set an empty session to transition from/to pb mode, see bug 476463
          ss.setBrowserState(blankState);

          // just in case the only remaining window after setBrowserState is different.
          // it probably shouldn't be with the current sessionstore impl, but we shouldn't
          // rely on behaviour the API doesn't guarantee
          browserWindow = this._getBrowserWindow();
          let browser = browserWindow.gBrowser;

          // this ensures a clean slate from which to transition into or out of
          // private browsing
          browser.addTab();
          browser.getBrowserForTab(browser.tabContainer.firstChild).stop();
          browser.removeTab(browser.tabContainer.firstChild);
          browserWindow.getInterface(Ci.nsIWebNavigation)
                       .QueryInterface(Ci.nsIDocShellTreeItem)
                       .treeOwner
                       .QueryInterface(Ci.nsIInterfaceRequestor)
                       .getInterface(Ci.nsIXULWindow)
                       .docShell.contentViewer.resetCloseWindow();
        }
      }
    }
    else
      this._saveSession = false;
  },

  _onAfterPrivateBrowsingModeChange: function PBS__onAfterPrivateBrowsingModeChange() {
    // nothing to do here if we're enabling at startup or the current session is being
    // used
    if (!this._autoStarted && this._saveSession) {
      let ss = Cc["@mozilla.org/browser/sessionstore;1"].
               getService(Ci.nsISessionStore);
      // if we have transitioned out of private browsing mode and the session is
      // to be restored, do it now
      if (!this._inPrivateBrowsing) {
        this._currentStatus = STATE_WAITING_FOR_RESTORE;
        ss.setBrowserState(this._savedBrowserState);
        this._savedBrowserState = null;

        this._closePageInfoWindows();

        // re-open all view-source windows
        let windowWatcher = Cc["@mozilla.org/embedcomp/window-watcher;1"].
                            getService(Ci.nsIWindowWatcher);
        this._viewSrcURLs.forEach(function(uri) {
          let args = Cc["@mozilla.org/supports-array;1"].
                     createInstance(Ci.nsISupportsArray);
          let str = Cc["@mozilla.org/supports-string;1"].
                    createInstance(Ci.nsISupportsString);
          str.data = uri;
          args.AppendElement(str);
          args.AppendElement(null); // charset
          args.AppendElement(null); // page descriptor
          args.AppendElement(null); // line number
          let forcedCharset = Cc["@mozilla.org/supports-PRBool;1"].
                              createInstance(Ci.nsISupportsPRBool);
          forcedCharset.data = false;
          args.AppendElement(forcedCharset);
          windowWatcher.openWindow(null, "chrome://global/content/viewSource.xul",
            "_blank", "all,dialog=no", args);
        });
        this._viewSrcURLs = [];
      }
      else {
        // otherwise, if we have transitioned into private browsing mode, load
        // about:privatebrowsing
        let privateBrowsingState = {
          "windows": [{
            "tabs": [{
              "entries": [{
                "url": "about:privatebrowsing"
              }]
            }],
            "_closedTabs": []
          }]
        };
        // Transition into private browsing mode
        this._currentStatus = STATE_WAITING_FOR_RESTORE;
        ss.setBrowserState(JSON.stringify(privateBrowsingState));
      }
    }
  },

  _notifyIfTransitionComplete: function PBS__notifyIfTransitionComplete() {
    switch (this._currentStatus) {
      case STATE_TRANSITION_STARTED:
        // no session store operation was needed, so just notify of transition completion
      case STATE_RESTORE_FINISHED:
        // restore has been completed
        this._currentStatus = STATE_IDLE;
        this._obs.notifyObservers(null, "private-browsing-transition-complete", "");
        break;
      case STATE_WAITING_FOR_RESTORE:
        // too soon to notify...
        break;
      case STATE_IDLE:
        // no need to notify
        break;
      default:
        // unexpected state observed
        Cu.reportError("Unexpected private browsing status reached: " +
                       this._currentStatus);
        break;
    }
  },

  _canEnterPrivateBrowsingMode: function PBS__canEnterPrivateBrowsingMode() {
    let cancelEnter = Cc["@mozilla.org/supports-PRBool;1"].
                      createInstance(Ci.nsISupportsPRBool);
    cancelEnter.data = false;
    this._obs.notifyObservers(cancelEnter, "private-browsing-cancel-vote", "enter");
    return !cancelEnter.data;
  },

  _canLeavePrivateBrowsingMode: function PBS__canLeavePrivateBrowsingMode() {
    let cancelLeave = Cc["@mozilla.org/supports-PRBool;1"].
                      createInstance(Ci.nsISupportsPRBool);
    cancelLeave.data = false;
    this._obs.notifyObservers(cancelLeave, "private-browsing-cancel-vote", "exit");
    return !cancelLeave.data;
  },

  _getBrowserWindow: function PBS__getBrowserWindow() {
    var wm = Cc["@mozilla.org/appshell/window-mediator;1"].
             getService(Ci.nsIWindowMediator);

    var win = wm.getMostRecentWindow("navigator:browser");

    // We don't just return |win| now because of bug 528706.

    if (!win)
      return null;
    if (!win.closed)
      return win;

#ifdef BROKEN_WM_Z_ORDER
    win = null;
    var windowsEnum = wm.getEnumerator("navigator:browser");
    // this is oldest to newest, so this gets a bit ugly
    while (windowsEnum.hasMoreElements()) {
      let nextWin = windowsEnum.getNext();
      if (!nextWin.closed)
        win = nextWin;
    }
    return win;
#else
    var windowsEnum = wm.getZOrderDOMWindowEnumerator("navigator:browser", true);
    while (windowsEnum.hasMoreElements()) {
      win = windowsEnum.getNext();
      if (!win.closed)
        return win;
    }
    return null;
#endif
  },

  _ensureCanCloseWindows: function PBS__ensureCanCloseWindows() {
    // whether we should save and close the current session
    this._saveSession = true;
    try {
      if (this._prefs.getBoolPref("browser.privatebrowsing.keep_current_session")) {
        this._saveSession = false;
        return;
      }
    } catch (ex) {}

    let windowMediator = Cc["@mozilla.org/appshell/window-mediator;1"].
                         getService(Ci.nsIWindowMediator);
    let windowsEnum = windowMediator.getEnumerator("navigator:browser");

    while (windowsEnum.hasMoreElements()) {
      let win = windowsEnum.getNext();
      if (win.closed)
        continue;
      let xulWin = win.QueryInterface(Ci.nsIInterfaceRequestor).
                   getInterface(Ci.nsIWebNavigation).
                   QueryInterface(Ci.nsIDocShellTreeItem).
                   treeOwner.QueryInterface(Ci.nsIInterfaceRequestor).
                   getInterface(Ci.nsIXULWindow);
      if (xulWin.docShell.contentViewer.permitUnload(true))
        this._windowsToClose.push(xulWin);
      else
        throw Cr.NS_ERROR_ABORT;
    }
  },

  _closePageInfoWindows: function PBS__closePageInfoWindows() {
    let pageInfoEnum = Cc["@mozilla.org/appshell/window-mediator;1"].
                       getService(Ci.nsIWindowMediator).
                       getEnumerator("Browser:page-info");
    while (pageInfoEnum.hasMoreElements()) {
      let win = pageInfoEnum.getNext();
      win.close();
    }
  },

  // nsIObserver

  observe: function PBS_observe(aSubject, aTopic, aData) {
    switch (aTopic) {
      case "profile-after-change":
        // If the autostart prefs has been set, simulate entering the
        // private browsing mode upon startup.
        // This won't interfere with the session store component, because
        // that component will be initialized on final-ui-startup.
        if (!this._autoStarted) {
          this._autoStarted = this._prefs.getBoolPref("browser.privatebrowsing.autostart");
          if (this._autoStarted)
            this.privateBrowsingEnabled = true;
        }
        this._obs.removeObserver(this, "profile-after-change");
        break;
      case "quit-application-granted":
        this._unload();
        break;
      case "private-browsing":
        // clear all auth tokens
        let sdr = Cc["@mozilla.org/security/sdr;1"].
                  getService(Ci.nsISecretDecoderRing);
        sdr.logoutAndTeardown();
    
        // clear plain HTTP auth sessions
        let authMgr = Cc['@mozilla.org/network/http-auth-manager;1'].
                      getService(Ci.nsIHttpAuthManager);
        authMgr.clearAll();

        try {
          this._prefs.deleteBranch("geo.wifi.access_token.");
        } catch (ex) {}

        if (!this._inPrivateBrowsing) {
          // Clear the error console
          let consoleService = Cc["@mozilla.org/consoleservice;1"].
                               getService(Ci.nsIConsoleService);
          consoleService.logStringMessage(null); // trigger the listeners
          consoleService.reset();
        }
        break;
      case "command-line-startup":
        this._obs.removeObserver(this, "command-line-startup");
        aSubject.QueryInterface(Ci.nsICommandLine);
        if (aSubject.findFlag("private", false) >= 0) {
          this.privateBrowsingEnabled = true;
          this._autoStarted = true;
        }
        break;
      case "sessionstore-browser-state-restored":
        if (this._currentStatus == STATE_WAITING_FOR_RESTORE) {
          this._currentStatus = STATE_RESTORE_FINISHED;
          this._notifyIfTransitionComplete();
        }
        break;
    }
  },

  // nsICommandLineHandler

  handle: function PBS_handle(aCmdLine) {
    if (aCmdLine.handleFlag("private", false))
      ; // It has already been handled
    else if (aCmdLine.handleFlag("private-toggle", false)) {
      this.privateBrowsingEnabled = !this.privateBrowsingEnabled;
      this._autoStarted = false;
    }
  },

  get helpInfo() {
    return "  -private           Enable private browsing mode.\n" +
           "  -private-toggle    Toggle private browsing mode.\n";
  },

  // nsIPrivateBrowsingService

  /**
   * Return the current status of private browsing.
   */
  get privateBrowsingEnabled() {
    return this._inPrivateBrowsing;
  },

  /**
   * Enter or leave private browsing mode.
   */
  set privateBrowsingEnabled(val) {
    // Allowing observers to set the private browsing status from their
    // notification handlers is not desired, because it will change the
    // status of the service while it's in the process of another transition.
    // So, we detect a reentrant call here and throw an error.
    // This is documented in nsIPrivateBrowsingService.idl.
    if (this._currentStatus != STATE_IDLE)
      throw Cr.NS_ERROR_FAILURE;

    try {
      this._currentStatus = STATE_TRANSITION_STARTED;

      if (val != this._inPrivateBrowsing) {
        if (val) {
          if (!this._canEnterPrivateBrowsingMode())
            return;
        }
        else {
          if (!this._canLeavePrivateBrowsingMode())
            return;
        }

        this._ensureCanCloseWindows();

        this._autoStarted = this._prefs.getBoolPref("browser.privatebrowsing.autostart");
        this._inPrivateBrowsing = val != false;

        let data = val ? "enter" : "exit";

        let quitting = Cc["@mozilla.org/supports-PRBool;1"].
                       createInstance(Ci.nsISupportsPRBool);
        quitting.data = this._quitting;

        // notify observers of the pending private browsing mode change
        this._obs.notifyObservers(quitting, "private-browsing-change-granted", data);

        // destroy the current session and start initial cleanup
        this._onBeforePrivateBrowsingModeChange();

        this._obs.notifyObservers(quitting, "private-browsing", data);

        // load the appropriate session
        this._onAfterPrivateBrowsingModeChange();
      }
    } catch (ex) {
      // We aborted the transition to/from private browsing, we must restore the
      // beforeunload handling on all the windows for which we switched it off.
      for (let i = 0; i < this._windowsToClose.length; i++)
        this._windowsToClose[i].docShell.contentViewer.resetCloseWindow();
      // We don't log an error when the transition is canceled from beforeunload
      if (ex != Cr.NS_ERROR_ABORT)
        Cu.reportError("Exception thrown while processing the " +
          "private browsing mode change request: " + ex.toString());
    } finally {
      this._windowsToClose = [];
      this._notifyIfTransitionComplete();
    }
  },

  /**
   * Whether private browsing has been started automatically.
   */
  get autoStarted() {
    return this._inPrivateBrowsing && this._autoStarted;
  },

  removeDataFromDomain: function PBS_removeDataFromDomain(aDomain)
  {

    // clear any and all network geolocation provider sessions
    try {
        this._prefs.deleteBranch("geo.wifi.access_token.");
    } catch (e) {}
    
    // History
    let (bh = Cc["@mozilla.org/browser/global-history;2"].
              getService(Ci.nsIBrowserHistory)) {
      bh.removePagesFromHost(aDomain, true);
    }

    // Cache
    let (cs = Cc["@mozilla.org/network/cache-service;1"].
              getService(Ci.nsICacheService)) {
      // NOTE: there is no way to clear just that domain, so we clear out
      //       everything)
      try {
        cs.evictEntries(Ci.nsICache.STORE_ANYWHERE);
      } catch (ex) {
        Cu.reportError("Exception thrown while clearing the cache: " +
          ex.toString());
      }
    }

    // Image Cache
    let (imageCache = Cc["@mozilla.org/image/cache;1"].
                      getService(Ci.imgICache)) {
      try {
        imageCache.clearCache(false); // true=chrome, false=content
      } catch (ex) {
        Cu.reportError("Exception thrown while clearing the image cache: " +
          ex.toString());
      }
    }

    // Cookies
    let (cm = Cc["@mozilla.org/cookiemanager;1"].
              getService(Ci.nsICookieManager2)) {
      let enumerator = cm.getCookiesFromHost(aDomain);
      while (enumerator.hasMoreElements()) {
        let cookie = enumerator.getNext().QueryInterface(Ci.nsICookie);
        cm.remove(cookie.host, cookie.name, cookie.path, false);
      }
    }

    // Downloads
    let (dm = Cc["@mozilla.org/download-manager;1"].
              getService(Ci.nsIDownloadManager)) {
      // Active downloads
      let enumerator = dm.activeDownloads;
      while (enumerator.hasMoreElements()) {
        let dl = enumerator.getNext().QueryInterface(Ci.nsIDownload);
        if (dl.source.host.hasRootDomain(aDomain)) {
          dm.cancelDownload(dl.id);
          dm.removeDownload(dl.id);
        }
      }

      // Completed downloads
      let db = dm.DBConnection;
      // NOTE: This is lossy, but we feel that it is OK to be lossy here and not
      //       invoke the cost of creating a URI for each download entry and
      //       ensure that the hostname matches.
      let stmt = db.createStatement(
        "DELETE FROM moz_downloads " +
        "WHERE source LIKE ?1 ESCAPE '/' " +
        "AND state NOT IN (?2, ?3, ?4)"
      );
      let pattern = stmt.escapeStringForLIKE(aDomain, "/");
      stmt.bindStringParameter(0, "%" + pattern + "%");
      stmt.bindInt32Parameter(1, Ci.nsIDownloadManager.DOWNLOAD_DOWNLOADING);
      stmt.bindInt32Parameter(2, Ci.nsIDownloadManager.DOWNLOAD_PAUSED);
      stmt.bindInt32Parameter(3, Ci.nsIDownloadManager.DOWNLOAD_QUEUED);
      try {
        stmt.execute();
      }
      finally {
        stmt.finalize();
      }

      // We want to rebuild the list if the UI is showing, so dispatch the
      // observer topic
      let os = Cc["@mozilla.org/observer-service;1"].
               getService(Ci.nsIObserverService);
      os.notifyObservers(null, "download-manager-remove-download", null);
    }

    // Passwords
    let (lm = Cc["@mozilla.org/login-manager;1"].
              getService(Ci.nsILoginManager)) {
      // Clear all passwords for domain
      try {
        let logins = lm.getAllLogins();
        for (let i = 0; i < logins.length; i++)
          if (logins[i].hostname.hasRootDomain(aDomain))
            lm.removeLogin(logins[i]);
      }
      // XXXehsan: is there a better way to do this rather than this
      // hacky comparison?
      catch (ex if ex.message.indexOf("User canceled Master Password entry") != -1) { }

      // Clear any "do not save for this site" for this domain
      let disabledHosts = lm.getAllDisabledHosts();
      for (let i = 0; i < disabledHosts.length; i++)
        if (disabledHosts[i].hasRootDomain(aDomain))
          lm.setLoginSavingEnabled(disabledHosts, true);
    }

    // Permissions
    let (pm = Cc["@mozilla.org/permissionmanager;1"].
              getService(Ci.nsIPermissionManager)) {
      // Enumerate all of the permissions, and if one matches, remove it
      let enumerator = pm.enumerator;
      while (enumerator.hasMoreElements()) {
        let perm = enumerator.getNext().QueryInterface(Ci.nsIPermission);
        if (perm.host.hasRootDomain(aDomain))
          pm.remove(perm.host, perm.type);
      }
    }

    // Content Preferences
    let (cp = Cc["@mozilla.org/content-pref/service;1"].
              getService(Ci.nsIContentPrefService)) {
      let db = cp.DBConnection;
      // First we need to get the list of "groups" which are really just domains
      let names = [];
      let stmt = db.createStatement(
        "SELECT name " +
        "FROM groups " +
        "WHERE name LIKE ?1 ESCAPE '/'"
      );
      let pattern = stmt.escapeStringForLIKE(aDomain, "/");
      stmt.bindStringParameter(0, "%" + pattern);
      try {
        while (stmt.executeStep())
          if (stmt.getString(0).hasRootDomain(aDomain))
            names.push(stmt.getString(0));
      }
      finally {
        stmt.finalize();
      }

      // Now, for each name we got back, remove all of its prefs.
      for (let i = 0; i < names.length; i++) {
        let uri = names[i];
        let enumerator = cp.getPrefs(uri).enumerator;
        while (enumerator.hasMoreElements()) {
          let pref = enumerator.getNext().QueryInterface(Ci.nsIProperty);
          cp.removePref(uri, pref.name);
        }
      }
    }

    // Everybody else (including extensions)
    this._obs.notifyObservers(null, "browser:purge-domain-data", aDomain);
  }
};

var NSGetFactory = XPCOMUtils.generateNSGetFactory([PrivateBrowsingService]);
