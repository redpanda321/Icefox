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
# The Original Code is the Browser Search Service.
#
# The Initial Developer of the Original Code is
# Giorgio Maone
# Portions created by the Initial Developer are Copyright (C) 2005
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Giorgio Maone <g.maone@informaction.com>
#   Seth Spitzer <sspitzer@mozilla.com>
#   Asaf Romano <mano@mozilla.com>
#   Marco Bonardo <mak77@bonardo.net>
#   Dietrich Ayala <dietrich@mozilla.com>
#   Ehsan Akhgari <ehsan.akhgari@gmail.com>
#   Nils Maier <maierman@web.de>
#   Robert Strong <robert.bugzilla@gmail.com>
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

const Ci = Components.interfaces;
const Cc = Components.classes;
const Cr = Components.results;
const Cu = Components.utils;

const XULNS = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

XPCOMUtils.defineLazyGetter(this, "NetUtil", function() {
  Cu.import("resource://gre/modules/NetUtil.jsm");
  return NetUtil;
});

const PREF_EM_NEW_ADDONS_LIST = "extensions.newAddons";
const PREF_PLUGINS_NOTIFYUSER = "plugins.update.notifyUser";
const PREF_PLUGINS_UPDATEURL  = "plugins.update.url";

// We try to backup bookmarks at idle times, to avoid doing that at shutdown.
// Number of idle seconds before trying to backup bookmarks.  15 minutes.
const BOOKMARKS_BACKUP_IDLE_TIME = 15 * 60;
// Minimum interval in milliseconds between backups.
const BOOKMARKS_BACKUP_INTERVAL = 86400 * 1000;
// Maximum number of backups to create.  Old ones will be purged.
const BOOKMARKS_BACKUP_MAX_BACKUPS = 10;

// Factory object
const BrowserGlueServiceFactory = {
  _instance: null,
  createInstance: function BGSF_createInstance(outer, iid) {
    if (outer != null)
      throw Components.results.NS_ERROR_NO_AGGREGATION;
    return this._instance == null ?
      this._instance = new BrowserGlue() : this._instance;
  }
};

// Constructor

function BrowserGlue() {
  XPCOMUtils.defineLazyServiceGetter(this, "_idleService",
                                     "@mozilla.org/widget/idleservice;1",
                                     "nsIIdleService");

  XPCOMUtils.defineLazyGetter(this, "_distributionCustomizer", function() {
                                Cu.import("resource:///modules/distribution.js");
                                return new DistributionCustomizer();
                              });

  XPCOMUtils.defineLazyGetter(this, "_sanitizer",
    function() {
      let sanitizerScope = {};
      Services.scriptloader.loadSubScript("chrome://browser/content/sanitize.js", sanitizerScope);
      return sanitizerScope.Sanitizer;
    });

  this._init();
}

#ifndef XP_MACOSX
# OS X has the concept of zero-window sessions and therefore ignores the
# browser-lastwindow-close-* topics.
#define OBSERVE_LASTWINDOW_CLOSE_TOPICS 1
#endif

BrowserGlue.prototype = {
  _saveSession: false,
  _isIdleObserver: false,
  _isPlacesInitObserver: false,
  _isPlacesLockedObserver: false,
  _isPlacesShutdownObserver: false,
  _isPlacesDatabaseLocked: false,

  _setPrefToSaveSession: function BG__setPrefToSaveSession(aForce) {
    if (!this._saveSession && !aForce)
      return;

    Services.prefs.setBoolPref("browser.sessionstore.resume_session_once", true);

    // This method can be called via [NSApplication terminate:] on Mac, which
    // ends up causing prefs not to be flushed to disk, so we need to do that
    // explicitly here. See bug 497652.
    Services.prefs.savePrefFile(null);
  },

#ifdef MOZ_SERVICES_SYNC
  _setSyncAutoconnectDelay: function BG__setSyncAutoconnectDelay() {
    // Assume that a non-zero value for services.sync.autoconnectDelay should override
    if (Services.prefs.prefHasUserValue("services.sync.autoconnectDelay")) {
      let prefDelay = Services.prefs.getIntPref("services.sync.autoconnectDelay");

      if (prefDelay > 0)
        return;
    }

    // delays are in seconds
    const MAX_DELAY = 300;
    let delay = 3;
    let enum = Services.wm.getEnumerator("navigator:browser");
    while (enum.hasMoreElements()) {
      delay += enum.getNext().gBrowser.tabs.length;
    }
    delay = delay <= MAX_DELAY ? delay : MAX_DELAY;

    let syncTemp = {};
    Cu.import("resource://services-sync/service.js", syncTemp);
    syncTemp.Weave.Service.delayedAutoConnect(delay);
  },
#endif

  // nsIObserver implementation 
  observe: function BG_observe(subject, topic, data) {
    switch (topic) {
      case "xpcom-shutdown":
        this._dispose();
        break;
      case "prefservice:after-app-defaults":
        this._onAppDefaults();
        break;
      case "final-ui-startup":
        this._onProfileStartup();
        break;
      case "sessionstore-windows-restored":
        this._onBrowserStartup();
        break;
      case "browser:purge-session-history":
        // reset the console service's error buffer
        Services.console.logStringMessage(null); // clear the console (in case it's open)
        Services.console.reset();
        break;
      case "quit-application-requested":
        this._onQuitRequest(subject, data);
        break;
      case "quit-application-granted":
        // This pref must be set here because SessionStore will use its value
        // on quit-application.
        this._setPrefToSaveSession();
        break;
#ifdef OBSERVE_LASTWINDOW_CLOSE_TOPICS
      case "browser-lastwindow-close-requested":
        // The application is not actually quitting, but the last full browser
        // window is about to be closed.
        this._onQuitRequest(subject, "lastwindow");
        break;
      case "browser-lastwindow-close-granted":
        this._setPrefToSaveSession();
        break;
#endif
#ifdef MOZ_SERVICES_SYNC
      case "weave:service:ready":
        this._setSyncAutoconnectDelay();
        break;
#endif
      case "session-save":
        this._setPrefToSaveSession(true);
        subject.QueryInterface(Ci.nsISupportsPRBool);
        subject.data = true;
        break;
      case "places-init-complete":
        this._initPlaces();
        Services.obs.removeObserver(this, "places-init-complete");
        this._isPlacesInitObserver = false;
        // no longer needed, since history was initialized completely.
        Services.obs.removeObserver(this, "places-database-locked");
        this._isPlacesLockedObserver = false;

        // Now apply distribution customized bookmarks.
        // This should always run after Places initialization.
        this._distributionCustomizer.applyBookmarks();
        break;
      case "places-database-locked":
        this._isPlacesDatabaseLocked = true;
        // Stop observing, so further attempts to load history service
        // will not show the prompt.
        Services.obs.removeObserver(this, "places-database-locked");
        this._isPlacesLockedObserver = false;
        break;
      case "places-shutdown":
        if (this._isPlacesShutdownObserver) {
          Services.obs.removeObserver(this, "places-shutdown");
          this._isPlacesShutdownObserver = false;
        }
        // places-shutdown is fired when the profile is about to disappear.
        this._onProfileShutdown();
        break;
      case "idle":
        if (this._idleService.idleTime > BOOKMARKS_BACKUP_IDLE_TIME * 1000)
          this._backupBookmarks();
        break;
      case "distribution-customization-complete":
        Services.obs.removeObserver(this, "distribution-customization-complete");
        // Customization has finished, we don't need the customizer anymore.
        delete this._distributionCustomizer;
        break;
      case "bookmarks-restore-success":
      case "bookmarks-restore-failed":
        Services.obs.removeObserver(this, "bookmarks-restore-success");
        Services.obs.removeObserver(this, "bookmarks-restore-failed");
        if (topic == "bookmarks-restore-success" && data == "html-initial")
          this.ensurePlacesDefaultQueriesInitialized();
        break;
      case "browser-glue-test": // used by tests
        if (data == "post-update-notification") {
          if (Services.prefs.prefHasUserValue("app.update.postupdate"))
            this._showUpdateNotification();
          break;
        }
        break;
    }
  }, 

  // initialization (called on application startup) 
  _init: function BG__init() {
    let os = Services.obs;
    os.addObserver(this, "xpcom-shutdown", false);
    os.addObserver(this, "prefservice:after-app-defaults", false);
    os.addObserver(this, "final-ui-startup", false);
    os.addObserver(this, "sessionstore-windows-restored", false);
    os.addObserver(this, "browser:purge-session-history", false);
    os.addObserver(this, "quit-application-requested", false);
    os.addObserver(this, "quit-application-granted", false);
#ifdef OBSERVE_LASTWINDOW_CLOSE_TOPICS
    os.addObserver(this, "browser-lastwindow-close-requested", false);
    os.addObserver(this, "browser-lastwindow-close-granted", false);
#endif
#ifdef MOZ_SERVICES_SYNC
    os.addObserver(this, "weave:service:ready", false);
#endif
    os.addObserver(this, "session-save", false);
    os.addObserver(this, "places-init-complete", false);
    this._isPlacesInitObserver = true;
    os.addObserver(this, "places-database-locked", false);
    this._isPlacesLockedObserver = true;
    os.addObserver(this, "distribution-customization-complete", false);
    os.addObserver(this, "places-shutdown", false);
    this._isPlacesShutdownObserver = true;
  },

  // cleanup (called on application shutdown)
  _dispose: function BG__dispose() {
    let os = Services.obs;
    os.removeObserver(this, "xpcom-shutdown");
    os.removeObserver(this, "prefservice:after-app-defaults");
    os.removeObserver(this, "final-ui-startup");
    os.removeObserver(this, "sessionstore-windows-restored");
    os.removeObserver(this, "browser:purge-session-history");
    os.removeObserver(this, "quit-application-requested");
    os.removeObserver(this, "quit-application-granted");
#ifdef OBSERVE_LASTWINDOW_CLOSE_TOPICS
    os.removeObserver(this, "browser-lastwindow-close-requested");
    os.removeObserver(this, "browser-lastwindow-close-granted");
#endif
#ifdef MOZ_SERVICES_SYNC
    os.removeObserver(this, "weave:service:ready", false);
#endif
    os.removeObserver(this, "session-save");
    if (this._isIdleObserver)
      this._idleService.removeIdleObserver(this, BOOKMARKS_BACKUP_IDLE_TIME);
    if (this._isPlacesInitObserver)
      os.removeObserver(this, "places-init-complete");
    if (this._isPlacesLockedObserver)
      os.removeObserver(this, "places-database-locked");
    if (this._isPlacesShutdownObserver)
      os.removeObserver(this, "places-shutdown");
  },

  _onAppDefaults: function BG__onAppDefaults() {
    // apply distribution customizations (prefs)
    // other customizations are applied in _onProfileStartup()
    this._distributionCustomizer.applyPrefDefaults();
  },

  // profile startup handler (contains profile initialization routines)
  _onProfileStartup: function BG__onProfileStartup() {
    this._sanitizer.onStartup();
    // check if we're in safe mode
    if (Services.appinfo.inSafeMode) {
      Services.ww.openWindow(null, "chrome://browser/content/safeMode.xul", 
                             "_blank", "chrome,centerscreen,modal,resizable=no", null);
    }

    // apply distribution customizations
    // prefs are applied in _onAppDefaults()
    this._distributionCustomizer.applyCustomizations();

    // handle any UI migration
    this._migrateUI();

    // if ioService is managing the offline status, then ioservice.offline
    // is already set correctly. We will continue to allow the ioService
    // to manage its offline state until the user uses the "Work Offline" UI.
    if (!Services.io.manageOfflineStatus) {
      // set the initial state
      try {
        Services.io.offline = Services.prefs.getBoolPref("browser.offline");
      }
      catch (e) {
        Services.io.offline = false;
      }
    }

    Services.obs.notifyObservers(null, "browser-ui-startup-complete", "");
  },

  // profile shutdown handler (contains profile cleanup routines)
  _onProfileShutdown: function BG__onProfileShutdown() {
#ifdef MOZ_UPDATER
#ifdef WINCE
    // If there's a pending update, clear cache to free up disk space.
    try {
      let um = Cc["@mozilla.org/updates/update-manager;1"].
               getService(Ci.nsIUpdateManager);
      if (um.activeUpdate && um.activeUpdate.state == "pending") {
        let cacheService = Cc["@mozilla.org/network/cache-service;1"].
                           getService(Ci.nsICacheService);
        cacheService.evictEntries(Ci.nsICache.STORE_ANYWHERE);
      }
    } catch (e) { }
#endif
#endif
    this._shutdownPlaces();
    this._sanitizer.onShutdown();
  },

  // Browser startup complete. All initial windows have opened.
  _onBrowserStartup: function BG__onBrowserStartup() {
    // Show about:rights notification, if needed.
    if (this._shouldShowRights())
      this._showRightsNotification();

    // Show update notification, if needed.
    if (Services.prefs.prefHasUserValue("app.update.postupdate"))
      this._showUpdateNotification();

    // If new add-ons were installed during startup open the add-ons manager.
    if (Services.prefs.prefHasUserValue(PREF_EM_NEW_ADDONS_LIST)) {
      var args = Cc["@mozilla.org/supports-array;1"].
                 createInstance(Ci.nsISupportsArray);
      var str = Cc["@mozilla.org/supports-string;1"].
                createInstance(Ci.nsISupportsString);
      str.data = "";
      args.AppendElement(str);
      var str = Cc["@mozilla.org/supports-string;1"].
                createInstance(Ci.nsISupportsString);
      str.data = Services.prefs.getCharPref(PREF_EM_NEW_ADDONS_LIST);
      args.AppendElement(str);
      const EMURL = "chrome://mozapps/content/extensions/extensions.xul";
      const EMFEATURES = "chrome,menubar,extra-chrome,toolbar,dialog=no,resizable";
      Services.ww.openWindow(null, EMURL, "_blank", EMFEATURES, args);
      Services.prefs.clearUserPref(PREF_EM_NEW_ADDONS_LIST);
    }

    // Load the "more info" page for a locked places.sqlite
    // This property is set earlier in the startup process:
    // nsPlacesDBFlush loads after profile-after-change and initializes
    // the history service, which sends out places-database-locked
    // which sets this property.
    if (this._isPlacesDatabaseLocked) {
      this._showPlacesLockedNotificationBox();
    }

    // If there are plugins installed that are outdated, and the user hasn't
    // been warned about them yet, open the plugins update page.
    if (Services.prefs.getBoolPref(PREF_PLUGINS_NOTIFYUSER))
      this._showPluginUpdatePage();

#ifdef XP_WIN
#ifndef WINCE
    // For windows seven, initialize the jump list module.
    const WINTASKBAR_CONTRACTID = "@mozilla.org/windows-taskbar;1";
    if (WINTASKBAR_CONTRACTID in Cc &&
        Cc[WINTASKBAR_CONTRACTID].getService(Ci.nsIWinTaskbar).available) {
      let temp = {};
      Cu.import("resource://gre/modules/WindowsJumpLists.jsm", temp);
      temp.WinTaskbarJumpList.startup();
    }
#endif
#endif
  },

  _onQuitRequest: function BG__onQuitRequest(aCancelQuit, aQuitType) {
    // If user has already dismissed quit request, then do nothing
    if ((aCancelQuit instanceof Ci.nsISupportsPRBool) && aCancelQuit.data)
      return;

    var windowcount = 0;
    var pagecount = 0;
    var browserEnum = Services.wm.getEnumerator("navigator:browser");
    while (browserEnum.hasMoreElements()) {
      windowcount++;

      var browser = browserEnum.getNext();
      var tabbrowser = browser.document.getElementById("content");
      if (tabbrowser)
        pagecount += tabbrowser.browsers.length - tabbrowser._numPinnedTabs;
    }

    this._saveSession = false;
    if (pagecount < 2)
      return;

    if (aQuitType != "restart")
      aQuitType = "quit";

    var showPrompt = true;
    try {
      // browser.warnOnQuit is a hidden global boolean to override all quit prompts
      // browser.warnOnRestart specifically covers app-initiated restarts where we restart the app
      // browser.tabs.warnOnClose is the global "warn when closing multiple tabs" pref

      var sessionWillBeSaved = Services.prefs.getIntPref("browser.startup.page") == 3 ||
                               Services.prefs.getBoolPref("browser.sessionstore.resume_session_once");
      if (sessionWillBeSaved || !Services.prefs.getBoolPref("browser.warnOnQuit"))
        showPrompt = false;
      else if (aQuitType == "restart")
        showPrompt = Services.prefs.getBoolPref("browser.warnOnRestart");
      else
        showPrompt = Services.prefs.getBoolPref("browser.tabs.warnOnClose");
    } catch (ex) {}

    // Never show a prompt inside the private browsing mode
    var inPrivateBrowsing = Cc["@mozilla.org/privatebrowsing;1"].
                            getService(Ci.nsIPrivateBrowsingService).
                            privateBrowsingEnabled;
    if (!showPrompt || inPrivateBrowsing)
      return;

    var quitBundle = Services.strings.createBundle("chrome://browser/locale/quitDialog.properties");
    var brandBundle = Services.strings.createBundle("chrome://branding/locale/brand.properties");

    var appName = brandBundle.GetStringFromName("brandShortName");
    var quitDialogTitle = quitBundle.formatStringFromName(aQuitType + "DialogTitle",
                                                          [appName], 1);

    var message;
    if (aQuitType == "restart")
      message = quitBundle.formatStringFromName("messageRestart",
                                                [appName], 1);
    else if (windowcount == 1)
      message = quitBundle.formatStringFromName("messageNoWindows",
                                                [appName], 1);
    else
      message = quitBundle.formatStringFromName("message",
                                                [appName], 1);

    var promptService = Services.prompt;

    var flags = promptService.BUTTON_TITLE_IS_STRING * promptService.BUTTON_POS_0 +
                promptService.BUTTON_TITLE_IS_STRING * promptService.BUTTON_POS_1 +
                promptService.BUTTON_POS_0_DEFAULT;

    var neverAsk = {value:false};
    var button0Title, button2Title;
    var button1Title = quitBundle.GetStringFromName("cancelTitle");
    var neverAskText = quitBundle.GetStringFromName("neverAsk");

    if (aQuitType == "restart")
      button0Title = quitBundle.GetStringFromName("restartTitle");
    else {
      flags += promptService.BUTTON_TITLE_IS_STRING * promptService.BUTTON_POS_2;
      button0Title = quitBundle.GetStringFromName("saveTitle");
      button2Title = quitBundle.GetStringFromName("quitTitle");
    }

    var mostRecentBrowserWindow = Services.wm.getMostRecentWindow("navigator:browser");
    var buttonChoice =
      promptService.confirmEx(mostRecentBrowserWindow, quitDialogTitle, message,
                              flags, button0Title, button1Title, button2Title,
                              neverAskText, neverAsk);

    switch (buttonChoice) {
    case 2: // Quit
      if (neverAsk.value)
        Services.prefs.setBoolPref("browser.tabs.warnOnClose", false);
      break;
    case 1: // Cancel
      aCancelQuit.QueryInterface(Ci.nsISupportsPRBool);
      aCancelQuit.data = true;
      break;
    case 0: // Save & Quit
      this._saveSession = true;
      if (neverAsk.value) {
        if (aQuitType == "restart")
          Services.prefs.setBoolPref("browser.warnOnRestart", false);
        else {
          // always save state when shutting down
          Services.prefs.setIntPref("browser.startup.page", 3);
        }
      }
      break;
    }
  },

  /*
   * _shouldShowRights - Determines if the user should be shown the
   * about:rights notification. The notification should *not* be shown if
   * we've already shown the current version, or if the override pref says to
   * never show it. The notification *should* be shown if it's never been seen
   * before, if a newer version is available, or if the override pref says to
   * always show it.
   */
  _shouldShowRights: function BG__shouldShowRights() {
    // Look for an unconditional override pref. If set, do what it says.
    // (true --> never show, false --> always show)
    try {
      return !Services.prefs.getBoolPref("browser.rights.override");
    } catch (e) { }
    // Ditto, for the legacy EULA pref.
    try {
      return !Services.prefs.getBoolPref("browser.EULA.override");
    } catch (e) { }

#ifndef OFFICIAL_BUILD
    // Non-official builds shouldn't shouldn't show the notification.
    return false;
#endif

    // Look to see if the user has seen the current version or not.
    var currentVersion = Services.prefs.getIntPref("browser.rights.version");
    try {
      return !Services.prefs.getBoolPref("browser.rights." + currentVersion + ".shown");
    } catch (e) { }

    // Legacy: If the user accepted a EULA, we won't annoy them with the
    // equivalent about:rights page until the version changes.
    try {
      return !Services.prefs.getBoolPref("browser.EULA." + currentVersion + ".accepted");
    } catch (e) { }

    // We haven't shown the notification before, so do so now.
    return true;
  },

  _showRightsNotification: function BG__showRightsNotification() {
    // Stick the notification onto the selected tab of the active browser window.
    var win = this.getMostRecentBrowserWindow();
    var browser = win.gBrowser; // for closure in notification bar callback
    var notifyBox = browser.getNotificationBox();

    var brandBundle  = Services.strings.createBundle("chrome://branding/locale/brand.properties");
    var rightsBundle = Services.strings.createBundle("chrome://global/locale/aboutRights.properties");

    var buttonLabel      = rightsBundle.GetStringFromName("buttonLabel");
    var buttonAccessKey  = rightsBundle.GetStringFromName("buttonAccessKey");
    var productName      = brandBundle.GetStringFromName("brandFullName");
    var notifyRightsText = rightsBundle.formatStringFromName("notifyRightsText", [productName], 1);
    
    var buttons = [
                    {
                      label:     buttonLabel,
                      accessKey: buttonAccessKey,
                      popup:     null,
                      callback: function(aNotificationBar, aButton) {
                        browser.selectedTab = browser.addTab("about:rights");
                      }
                    }
                  ];

    // Set pref to indicate we've shown the notification.
    var currentVersion = Services.prefs.getIntPref("browser.rights.version");
    Services.prefs.setBoolPref("browser.rights." + currentVersion + ".shown", true);

    var box = notifyBox.appendNotification(notifyRightsText, "about-rights", null, notifyBox.PRIORITY_INFO_LOW, buttons);
    box.persistence = 3; // arbitrary number, just so bar sticks around for a bit
  },

  _showUpdateNotification: function BG__showUpdateNotification() {
    Services.prefs.clearUserPref("app.update.postupdate");

    var um = Cc["@mozilla.org/updates/update-manager;1"].
             getService(Ci.nsIUpdateManager);
    try {
      // If the updates.xml file is deleted then getUpdateAt will throw.
      var update = um.getUpdateAt(0).QueryInterface(Ci.nsIPropertyBag);
    }
    catch (e) {
      // This should never happen.
      Cu.reportError("Unable to find update: " + e);
      return;
    }

    var actions = update.getProperty("actions");
    if (!actions || actions.indexOf("silent") != -1)
      return;

    var formatter = Cc["@mozilla.org/toolkit/URLFormatterService;1"].
                    getService(Ci.nsIURLFormatter);
    var browserBundle = Services.strings.createBundle("chrome://browser/locale/browser.properties");
    var brandBundle = Services.strings.createBundle("chrome://branding/locale/brand.properties");
    var appName = brandBundle.GetStringFromName("brandShortName");

    function getNotifyString(aPropData) {
      var propValue = update.getProperty(aPropData.propName);
      if (!propValue) {
        if (aPropData.prefName)
          propValue = formatter.formatURLPref(aPropData.prefName);
        else if (aPropData.stringParams)
          propValue = browserBundle.formatStringFromName(aPropData.stringName,
                                                         aPropData.stringParams,
                                                         aPropData.stringParams.length);
        else
          propValue = browserBundle.GetStringFromName(aPropData.stringName);
      }
      return propValue;
    }

    if (actions.indexOf("showNotification") != -1) {
      let text = getNotifyString({propName: "notificationText",
                                  stringName: "puNotifyText",
                                  stringParams: [appName]});
      let url = getNotifyString({propName: "notificationURL",
                                 prefName: "startup.homepage_override_url"});
      let label = getNotifyString({propName: "notificationButtonLabel",
                                   stringName: "pu.notifyButton.label"});
      let key = getNotifyString({propName: "notificationButtonAccessKey",
                                 stringName: "pu.notifyButton.accesskey"});

      let win = this.getMostRecentBrowserWindow();
      let browser = win.gBrowser; // for closure in notification bar callback
      let notifyBox = browser.getNotificationBox();

      let buttons = [
                      {
                        label:     label,
                        accessKey: key,
                        popup:     null,
                        callback: function(aNotificationBar, aButton) {
                          browser.selectedTab = browser.addTab(url);
                        }
                      }
                    ];

      let box = notifyBox.appendNotification(text, "post-update-notification",
                                             null, notifyBox.PRIORITY_INFO_LOW,
                                             buttons);
      box.persistence = 3;
    }

    if (actions.indexOf("showAlert") == -1)
      return;

    let notifier;
    try {
      notifier = Cc["@mozilla.org/alerts-service;1"].
                 getService(Ci.nsIAlertsService);
    }
    catch (e) {
      // nsIAlertsService is not available for this platform
      return;
    }

    let title = getNotifyString({propName: "alertTitle",
                                 stringName: "puAlertTitle",
                                 stringParams: [appName]});
    let text = getNotifyString({propName: "alertText",
                                stringName: "puAlertText",
                                stringParams: [appName]});
    let url = getNotifyString({propName: "alertURL",
                               prefName: "startup.homepage_override_url"});

    var self = this;
    function clickCallback(subject, topic, data) {
      // This callback will be called twice but only once with this topic
      if (topic != "alertclickcallback")
        return;
      let win = self.getMostRecentBrowserWindow();
      let browser = win.gBrowser;
      browser.selectedTab = browser.addTab(data);
    }

    try {
      // This will throw NS_ERROR_NOT_AVAILABLE if the notification cannot
      // be displayed per the idl.
      notifier.showAlertNotification("post-update-notification", title, text,
                                     true, url, clickCallback);
    }
    catch (e) {
    }
  },

  _showPluginUpdatePage: function BG__showPluginUpdatePage() {
    Services.prefs.setBoolPref(PREF_PLUGINS_NOTIFYUSER, false);

    var formatter = Cc["@mozilla.org/toolkit/URLFormatterService;1"].
                    getService(Ci.nsIURLFormatter);
    var updateUrl = formatter.formatURLPref(PREF_PLUGINS_UPDATEURL);

    var win = this.getMostRecentBrowserWindow();
    var browser = win.gBrowser;
    browser.selectedTab = browser.addTab(updateUrl);
  },

  /**
   * Initialize Places
   * - imports the bookmarks html file if bookmarks database is empty, try to
   *   restore bookmarks from a JSON backup if the backend indicates that the
   *   database was corrupt.
   *
   * These prefs can be set up by the frontend:
   *
   * WARNING: setting these preferences to true will overwite existing bookmarks
   *
   * - browser.places.importBookmarksHTML
   *   Set to true will import the bookmarks.html file from the profile folder.
   * - browser.places.smartBookmarksVersion
   *   Set during HTML import to indicate that Smart Bookmarks were created.
   *   Set to -1 to disable Smart Bookmarks creation.
   *   Set to 0 to restore current Smart Bookmarks.
   * - browser.bookmarks.restore_default_bookmarks
   *   Set to true by safe-mode dialog to indicate we must restore default
   *   bookmarks.
   */
  _initPlaces: function BG__initPlaces() {
    // We must instantiate the history service since it will tell us if we
    // need to import or restore bookmarks due to first-run, corruption or
    // forced migration (due to a major schema change).
    var histsvc = Cc["@mozilla.org/browser/nav-history-service;1"].
                  getService(Ci.nsINavHistoryService);

    // If the database is corrupt or has been newly created we should
    // import bookmarks.
    var databaseStatus = histsvc.databaseStatus;
    var importBookmarks = databaseStatus == histsvc.DATABASE_STATUS_CREATE ||
                          databaseStatus == histsvc.DATABASE_STATUS_CORRUPT;

    if (databaseStatus == histsvc.DATABASE_STATUS_CREATE) {
      // If the database has just been created, but we already have any
      // bookmark, this is not the initial import.  This can happen after a
      // migration from a different browser since migrators run before us.
      // In such a case we should not import, unless some pref has been set.
      var bmsvc = Cc["@mozilla.org/browser/nav-bookmarks-service;1"].
                  getService(Ci.nsINavBookmarksService);
      if (bmsvc.getIdForItemAt(bmsvc.bookmarksMenuFolder, 0) != -1 ||
          bmsvc.getIdForItemAt(bmsvc.toolbarFolder, 0) != -1)
        importBookmarks = false;
    }

    // Check if user or an extension has required to import bookmarks.html
    var importBookmarksHTML = false;
    try {
      importBookmarksHTML =
        Services.prefs.getBoolPref("browser.places.importBookmarksHTML");
      if (importBookmarksHTML)
        importBookmarks = true;
    } catch(ex) {}

    // Check if Safe Mode or the user has required to restore bookmarks from
    // default profile's bookmarks.html
    var restoreDefaultBookmarks = false;
    try {
      restoreDefaultBookmarks =
        Services.prefs.getBoolPref("browser.bookmarks.restore_default_bookmarks");
      if (restoreDefaultBookmarks) {
        // Ensure that we already have a bookmarks backup for today.
        this._backupBookmarks();
        importBookmarks = true;
      }
    } catch(ex) {}

    // If the user did not require to restore default bookmarks, or import
    // from bookmarks.html, we will try to restore from JSON
    if (importBookmarks && !restoreDefaultBookmarks && !importBookmarksHTML) {
      // get latest JSON backup
      Cu.import("resource://gre/modules/PlacesUtils.jsm");
      var bookmarksBackupFile = PlacesUtils.backups.getMostRecent("json");
      if (bookmarksBackupFile) {
        // restore from JSON backup
        PlacesUtils.restoreBookmarksFromJSONFile(bookmarksBackupFile);
        importBookmarks = false;
      }
      else {
        // We have created a new database but we don't have any backup available
        importBookmarks = true;
        var dirService = Cc["@mozilla.org/file/directory_service;1"].
                         getService(Ci.nsIProperties);
        var bookmarksHTMLFile = dirService.get("BMarks", Ci.nsILocalFile);
        if (bookmarksHTMLFile.exists()) {
          // If bookmarks.html is available in current profile import it...
          importBookmarksHTML = true;
        }
        else {
          // ...otherwise we will restore defaults
          restoreDefaultBookmarks = true;
        }
      }
    }

    // If bookmarks are not imported, then initialize smart bookmarks.  This
    // happens during a common startup.
    // Otherwise, if any kind of import runs, smart bookmarks creation should be
    // delayed till the import operations has finished.  Not doing so would
    // cause them to be overwritten by the newly imported bookmarks.
    if (!importBookmarks) {
      this.ensurePlacesDefaultQueriesInitialized();
    }
    else {
      // An import operation is about to run.
      // Don't try to recreate smart bookmarks if autoExportHTML is true or
      // smart bookmarks are disabled.
      var autoExportHTML = false;
      try {
        autoExportHTML = Services.prefs.getBoolPref("browser.bookmarks.autoExportHTML");
      } catch(ex) {}
      var smartBookmarksVersion = 0;
      try {
        smartBookmarksVersion = Services.prefs.getIntPref("browser.places.smartBookmarksVersion");
      } catch(ex) {}
      if (!autoExportHTML && smartBookmarksVersion != -1)
        Services.prefs.setIntPref("browser.places.smartBookmarksVersion", 0);

      // Get bookmarks.html file location
      var dirService = Cc["@mozilla.org/file/directory_service;1"].
                       getService(Ci.nsIProperties);

      var bookmarksURI = null;
      if (restoreDefaultBookmarks) {
        // User wants to restore bookmarks.html file from default profile folder
        bookmarksURI = NetUtil.newURI("resource:///defaults/profile/bookmarks.html");
      }
      else {
        var bookmarksFile = dirService.get("BMarks", Ci.nsILocalFile);
        if (bookmarksFile.exists())
          bookmarksURI = NetUtil.newURI(bookmarksFile);
      }

      if (bookmarksURI) {
        // Add an import observer.  It will ensure that smart bookmarks are
        // created once the operation is complete.
        Services.obs.addObserver(this, "bookmarks-restore-success", false);
        Services.obs.addObserver(this, "bookmarks-restore-failed", false);

        // Import from bookmarks.html file.
        try {
          var importer = Cc["@mozilla.org/browser/places/import-export-service;1"].
                         getService(Ci.nsIPlacesImportExportService);
          importer.importHTMLFromURI(bookmarksURI, true /* overwrite existing */);
        } catch (err) {
          // Report the error, but ignore it.
          Cu.reportError("Bookmarks.html file could be corrupt. " + err);
          Services.obs.removeObserver(this, "bookmarks-restore-success");
          Services.obs.removeObserver(this, "bookmarks-restore-failed");
        }
      }
      else
        Cu.reportError("Unable to find bookmarks.html file.");

      // Reset preferences, so we won't try to import again at next run
      if (importBookmarksHTML)
        Services.prefs.setBoolPref("browser.places.importBookmarksHTML", false);
      if (restoreDefaultBookmarks)
        Services.prefs.setBoolPref("browser.bookmarks.restore_default_bookmarks",
                                   false);
    }

    // Initialize bookmark archiving on idle.
    // Once a day, either on idle or shutdown, bookmarks are backed up.
    if (!this._isIdleObserver) {
      this._idleService.addIdleObserver(this, BOOKMARKS_BACKUP_IDLE_TIME);
      this._isIdleObserver = true;
    }
  },

  /**
   * Places shut-down tasks
   * - back up bookmarks if needed.
   * - export bookmarks as HTML, if so configured.
   *
   * Note: quit-application-granted notification is received twice
   *       so replace this method with a no-op when first called.
   */
  _shutdownPlaces: function BG__shutdownPlaces() {
    if (this._isIdleObserver) {
      this._idleService.removeIdleObserver(this, BOOKMARKS_BACKUP_IDLE_TIME);
      this._isIdleObserver = false;
    }
    this._backupBookmarks();

    // Backup bookmarks to bookmarks.html to support apps that depend
    // on the legacy format.
    var autoExportHTML = false;
    try {
      autoExportHTML = Services.prefs.getBoolPref("browser.bookmarks.autoExportHTML");
    } catch(ex) { /* Don't export */ }

    if (autoExportHTML) {
      Cc["@mozilla.org/browser/places/import-export-service;1"].
        getService(Ci.nsIPlacesImportExportService).
        backupBookmarksFile();
    }
  },

  /**
   * Backup bookmarks if needed.
   */
  _backupBookmarks: function BG__backupBookmarks() {
    Cu.import("resource://gre/modules/PlacesUtils.jsm");

    let lastBackupFile = PlacesUtils.backups.getMostRecent();

    // Backup bookmarks if there are no backups or the maximum interval between
    // backups elapsed.
    if (!lastBackupFile ||
        new Date() - PlacesUtils.backups.getDateForFile(lastBackupFile) > BOOKMARKS_BACKUP_INTERVAL) {
      let maxBackups = BOOKMARKS_BACKUP_MAX_BACKUPS;
      try {
        maxBackups = Services.prefs.getIntPref("browser.bookmarks.max_backups");
      }
      catch(ex) { /* Use default. */ }

      PlacesUtils.backups.create(maxBackups); // Don't force creation.
    }
  },

  /**
   * Show the notificationBox for a locked places database.
   */
  _showPlacesLockedNotificationBox: function BG__showPlacesLockedNotificationBox() {
    var brandBundle  = Services.strings.createBundle("chrome://branding/locale/brand.properties");
    var applicationName = brandBundle.GetStringFromName("brandShortName");
    var placesBundle = Services.strings.createBundle("chrome://browser/locale/places/places.properties");
    var title = placesBundle.GetStringFromName("lockPrompt.title");
    var text = placesBundle.formatStringFromName("lockPrompt.text", [applicationName], 1);
    var buttonText = placesBundle.GetStringFromName("lockPromptInfoButton.label");
    var accessKey = placesBundle.GetStringFromName("lockPromptInfoButton.accessKey");

    var helpTopic = "places-locked";
    var url = Cc["@mozilla.org/toolkit/URLFormatterService;1"].
              getService(Components.interfaces.nsIURLFormatter).
              formatURLPref("app.support.baseURL");
    url += helpTopic;

    var browser = this.getMostRecentBrowserWindow().gBrowser;

    var buttons = [
                    {
                      label:     buttonText,
                      accessKey: accessKey,
                      popup:     null,
                      callback:  function(aNotificationBar, aButton) {
                        browser.selectedTab = browser.addTab(url);
                      }
                    }
                  ];

    var notifyBox = browser.getNotificationBox();
    var box = notifyBox.appendNotification(text, title, null,
                                           notifyBox.PRIORITY_CRITICAL_MEDIUM,
                                           buttons);
    box.persistence = -1; // Until user closes it
  },

  _migrateUI: function BG__migrateUI() {
    const UI_VERSION = 2;
    let currentUIVersion = 0;
    try {
      currentUIVersion = Services.prefs.getIntPref("browser.migration.version");
    } catch(ex) {}
    if (currentUIVersion >= UI_VERSION)
      return;

    this._rdf = Cc["@mozilla.org/rdf/rdf-service;1"].getService(Ci.nsIRDFService);
    this._dataSource = this._rdf.GetDataSource("rdf:local-store");
    this._dirty = false;

    if (currentUIVersion < 1) {
      // this code should always migrate pre-FF3 profiles to the current UI state
      let currentsetResource = this._rdf.GetResource("currentset");
      let toolbars = ["nav-bar", "toolbar-menubar", "PersonalToolbar"];
      for (let i = 0; i < toolbars.length; i++) {
        let toolbar = this._rdf.GetResource("chrome://browser/content/browser.xul#" + toolbars[i]);
        let currentset = this._getPersist(toolbar, currentsetResource);
        if (!currentset) {
          // toolbar isn't customized
          if (i == 0)
            // new button is in the defaultset, nothing to migrate
            break;
          continue;
        }
        if (/(?:^|,)unified-back-forward-button(?:$|,)/.test(currentset))
          // new button is already there, nothing to migrate
          break;
        if (/(?:^|,)back-button(?:$|,)/.test(currentset)) {
          let newset = currentset.replace(/(^|,)back-button($|,)/,
                                          "$1unified-back-forward-button,back-button$2")
          this._setPersist(toolbar, currentsetResource, newset);
          // done migrating
          break;
        }
      }
    }

    if (currentUIVersion < 2) {
      // This code adds the customizable bookmarks button.
      let currentsetResource = this._rdf.GetResource("currentset");
      let toolbarResource = this._rdf.GetResource("chrome://browser/content/browser.xul#nav-bar");
      let currentset = this._getPersist(toolbarResource, currentsetResource);
      // Need to migrate only if toolbar is customized and the element is not found.
      if (currentset &&
          currentset.indexOf("bookmarks-menu-button-container") == -1) {
        if (currentset.indexOf("fullscreenflex") != -1) {
          currentset = currentset.replace(/(^|,)fullscreenflex($|,)/,
                                          "$1bookmarks-menu-button-container,fullscreenflex$2")
        }
        else {
          currentset += ",bookmarks-menu-button-container";
        }
        this._setPersist(toolbarResource, currentsetResource, currentset);
      }
    }

    if (this._dirty)
      this._dataSource.QueryInterface(Ci.nsIRDFRemoteDataSource).Flush();

    delete this._rdf;
    delete this._dataSource;

    // Update the migration version.
    Services.prefs.setIntPref("browser.migration.version", UI_VERSION);
  },

  _getPersist: function BG__getPersist(aSource, aProperty) {
    var target = this._dataSource.GetTarget(aSource, aProperty, true);
    if (target instanceof Ci.nsIRDFLiteral)
      return target.Value;
    return null;
  },

  _setPersist: function BG__setPersist(aSource, aProperty, aTarget) {
    this._dirty = true;
    try {
      var oldTarget = this._dataSource.GetTarget(aSource, aProperty, true);
      if (oldTarget) {
        if (aTarget)
          this._dataSource.Change(aSource, aProperty, oldTarget, this._rdf.GetLiteral(aTarget));
        else
          this._dataSource.Unassert(aSource, aProperty, oldTarget);
      }
      else {
        this._dataSource.Assert(aSource, aProperty, this._rdf.GetLiteral(aTarget), true);
      }
    }
    catch(ex) {}
  },

  // ------------------------------
  // public nsIBrowserGlue members
  // ------------------------------
  
  sanitize: function BG_sanitize(aParentWindow) {
    this._sanitizer.sanitize(aParentWindow);
  },

  ensurePlacesDefaultQueriesInitialized:
  function BG_ensurePlacesDefaultQueriesInitialized() {
    // This is actual version of the smart bookmarks, must be increased every
    // time smart bookmarks change.
    // When adding a new smart bookmark below, its newInVersion property must
    // be set to the version it has been added in, we will compare its value
    // to users' smartBookmarksVersion and add new smart bookmarks without
    // recreating old deleted ones.
    const SMART_BOOKMARKS_VERSION = 2;
    const SMART_BOOKMARKS_ANNO = "Places/SmartBookmark";
    const SMART_BOOKMARKS_PREF = "browser.places.smartBookmarksVersion";

    // TODO bug 399268: should this be a pref?
    const MAX_RESULTS = 10;

    // get current smart bookmarks version
    // By default, if the pref is not set up, we must create Smart Bookmarks
    var smartBookmarksCurrentVersion = 0;
    try {
      smartBookmarksCurrentVersion = Services.prefs.getIntPref(SMART_BOOKMARKS_PREF);
    } catch(ex) { /* no version set, new profile */ }

    // bail out if we don't have to create or update Smart Bookmarks
    if (smartBookmarksCurrentVersion == -1 ||
        smartBookmarksCurrentVersion >= SMART_BOOKMARKS_VERSION)
      return;

    var bmsvc = Cc["@mozilla.org/browser/nav-bookmarks-service;1"].
                getService(Ci.nsINavBookmarksService);
    var annosvc = Cc["@mozilla.org/browser/annotation-service;1"].
                  getService(Ci.nsIAnnotationService);

    var callback = {
      _uri: function BG_EPDQI__uri(aSpec) {
        return Services.io.newURI(aSpec, null, null);
      },

      runBatched: function BG_EPDQI_runBatched() {
        var smartBookmarks = [];
        var bookmarksMenuIndex = 0;
        var bookmarksToolbarIndex = 0;

        var placesBundle = Services.strings.createBundle("chrome://browser/locale/places/places.properties");

        // MOST VISITED
        var smart = {queryId: "MostVisited", // don't change this
                     itemId: null,
                     title: placesBundle.GetStringFromName("mostVisitedTitle"),
                     uri: this._uri("place:redirectsMode=" +
                                    Ci.nsINavHistoryQueryOptions.REDIRECTS_MODE_TARGET +
                                    "&sort=" +
                                    Ci.nsINavHistoryQueryOptions.SORT_BY_VISITCOUNT_DESCENDING +
                                    "&maxResults=" + MAX_RESULTS),
                     parent: bmsvc.toolbarFolder,
                     position: bookmarksToolbarIndex++,
                     newInVersion: 1 };
        smartBookmarks.push(smart);

        // RECENTLY BOOKMARKED
        smart = {queryId: "RecentlyBookmarked", // don't change this
                 itemId: null,
                 title: placesBundle.GetStringFromName("recentlyBookmarkedTitle"),
                 uri: this._uri("place:folder=BOOKMARKS_MENU" +
                                "&folder=UNFILED_BOOKMARKS" +
                                "&folder=TOOLBAR" +
                                "&queryType=" +
                                Ci.nsINavHistoryQueryOptions.QUERY_TYPE_BOOKMARKS +
                                "&sort=" +
                                Ci.nsINavHistoryQueryOptions.SORT_BY_DATEADDED_DESCENDING +
                                "&excludeItemIfParentHasAnnotation=livemark%2FfeedURI" +
                                "&maxResults=" + MAX_RESULTS +
                                "&excludeQueries=1"),
                 parent: bmsvc.bookmarksMenuFolder,
                 position: bookmarksMenuIndex++,
                 newInVersion: 1 };
        smartBookmarks.push(smart);

        // RECENT TAGS
        smart = {queryId: "RecentTags", // don't change this
                 itemId: null,
                 title: placesBundle.GetStringFromName("recentTagsTitle"),
                 uri: this._uri("place:"+
                    "type=" +
                    Ci.nsINavHistoryQueryOptions.RESULTS_AS_TAG_QUERY +
                    "&sort=" +
                    Ci.nsINavHistoryQueryOptions.SORT_BY_LASTMODIFIED_DESCENDING +
                    "&maxResults=" + MAX_RESULTS),
                 parent: bmsvc.bookmarksMenuFolder,
                 position: bookmarksMenuIndex++,
                 newInVersion: 1 };
        smartBookmarks.push(smart);

        var smartBookmarkItemIds = annosvc.getItemsWithAnnotation(SMART_BOOKMARKS_ANNO);
        // Set current itemId, parent and position if Smart Bookmark exists,
        // we will use these informations to create the new version at the same
        // position.
        for each(var itemId in smartBookmarkItemIds) {
          var queryId = annosvc.getItemAnnotation(itemId, SMART_BOOKMARKS_ANNO);
          for (var i = 0; i < smartBookmarks.length; i++){
            if (smartBookmarks[i].queryId == queryId) {
              smartBookmarks[i].found = true;
              smartBookmarks[i].itemId = itemId;
              smartBookmarks[i].parent = bmsvc.getFolderIdForItem(itemId);
              smartBookmarks[i].position = bmsvc.getItemIndex(itemId);
              // remove current item, since it will be replaced
              bmsvc.removeItem(itemId);
              break;
            }
            // We don't remove old Smart Bookmarks because user could still
            // find them useful, or could have personalized them.
            // Instead we remove the Smart Bookmark annotation.
            if (i == smartBookmarks.length - 1)
              annosvc.removeItemAnnotation(itemId, SMART_BOOKMARKS_ANNO);
          }
        }

        // create smart bookmarks
        for each(var smartBookmark in smartBookmarks) {
          // We update or create only changed or new smart bookmarks.
          // Also we respect user choices, so we won't try to create a smart
          // bookmark if it has been removed.
          if (smartBookmarksCurrentVersion > 0 &&
              smartBookmark.newInVersion <= smartBookmarksCurrentVersion &&
              !smartBookmark.found)
            continue;

          smartBookmark.itemId = bmsvc.insertBookmark(smartBookmark.parent,
                                                      smartBookmark.uri,
                                                      smartBookmark.position,
                                                      smartBookmark.title);
          annosvc.setItemAnnotation(smartBookmark.itemId,
                                    SMART_BOOKMARKS_ANNO, smartBookmark.queryId,
                                    0, annosvc.EXPIRE_NEVER);
        }
        
        // If we are creating all Smart Bookmarks from ground up, add a
        // separator below them in the bookmarks menu.
        if (smartBookmarksCurrentVersion == 0 &&
            smartBookmarkItemIds.length == 0) {
          let id = bmsvc.getIdForItemAt(bmsvc.bookmarksMenuFolder,
                                        bookmarksMenuIndex);
          // Don't add a separator if the menu was empty or there is one already.
          if (id != -1 && bmsvc.getItemType(id) != bmsvc.TYPE_SEPARATOR)
            bmsvc.insertSeparator(bmsvc.bookmarksMenuFolder, bookmarksMenuIndex);
        }
      }
    };

    try {
      bmsvc.runInBatchMode(callback, null);
    }
    catch(ex) {
      Components.utils.reportError(ex);
    }
    finally {
      Services.prefs.setIntPref(SMART_BOOKMARKS_PREF, SMART_BOOKMARKS_VERSION);
      Services.prefs.savePrefFile(null);
    }
  },

#ifndef XP_WIN
#define BROKEN_WM_Z_ORDER
#endif

  // this returns the most recent non-popup browser window
  getMostRecentBrowserWindow: function BG_getMostRecentBrowserWindow() {
    function isFullBrowserWindow(win) {
      return !win.closed &&
             !win.document.documentElement.getAttribute("chromehidden");
    }

#ifdef BROKEN_WM_Z_ORDER
    var win = Services.wm.getMostRecentWindow("navigator:browser");

    // if we're lucky, this isn't a popup, and we can just return this
    if (win && !isFullBrowserWindow(win)) {
      win = null;
      let windowList = Services.wm.getEnumerator("navigator:browser");
      // this is oldest to newest, so this gets a bit ugly
      while (windowList.hasMoreElements()) {
        let nextWin = windowList.getNext();
        if (isFullBrowserWindow(nextWin))
          win = nextWin;
      }
    }
    return win;
#else
    var windowList = Services.wm.getZOrderDOMWindowEnumerator("navigator:browser", true);
    while (windowList.hasMoreElements()) {
      let win = windowList.getNext();
      if (isFullBrowserWindow(win))
        return win;
    }
    return null;
#endif
  },


  // for XPCOM
  classID:          Components.ID("{eab9012e-5f74-4cbc-b2b5-a590235513cc}"),

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIObserver,
                                         Ci.nsISupportsWeakReference,
                                         Ci.nsIBrowserGlue]),

  // redefine the default factory for XPCOMUtils
  _xpcom_factory: BrowserGlueServiceFactory,
}

function GeolocationPrompt() {}

GeolocationPrompt.prototype = {
  classID:          Components.ID("{C6E8C44D-9F39-4AF7-BCC0-76E38A8310F5}"),

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIGeolocationPrompt]),

  prompt: function GP_prompt(request) {
    var requestingURI = request.requestingURI;

    // Ignore requests from non-nsIStandardURLs
    if (!(requestingURI instanceof Ci.nsIStandardURL))
      return;

    var result = Services.perms.testExactPermission(requestingURI, "geo");

    if (result == Ci.nsIPermissionManager.ALLOW_ACTION) {
      request.allow();
      return;
    }
    
    if (result == Ci.nsIPermissionManager.DENY_ACTION) {
      request.cancel();
      return;
    }

    function getChromeWindow(aWindow) {
      var chromeWin = aWindow 
        .QueryInterface(Ci.nsIInterfaceRequestor)
        .getInterface(Ci.nsIWebNavigation)
        .QueryInterface(Ci.nsIDocShellTreeItem)
        .rootTreeItem
        .QueryInterface(Ci.nsIInterfaceRequestor)
        .getInterface(Ci.nsIDOMWindow)
        .QueryInterface(Ci.nsIDOMChromeWindow);
      return chromeWin;
    }

    var browserBundle = Services.strings.createBundle("chrome://browser/locale/browser.properties");

    var mainAction = {
      label: browserBundle.GetStringFromName("geolocation.shareLocation"),
      accessKey: browserBundle.GetStringFromName("geolocation.shareLocation.accesskey"),
      callback: function(notification) {
        request.allow();
      },
    };

    // XXX Bug 573536
    // browserBundle.GetStringFromName("geolocation.learnMore")
    //var formatter = Cc["@mozilla.org/toolkit/URLFormatterService;1"].getService(Ci.nsIURLFormatter);
    //link.href = formatter.formatURLPref("browser.geolocation.warning.infoURL");

    var message;
    var secondaryActions = [];

    // Different message/options if it is a local file
    if (requestingURI.schemeIs("file")) {
      message = browserBundle.formatStringFromName("geolocation.fileWantsToKnow",
                                                   [request.requestingURI.path], 1);
    } else {
      message = browserBundle.formatStringFromName("geolocation.siteWantsToKnow",
                                                   [requestingURI.host], 1);

      // Don't offer to "always/never share" in PB mode
      var inPrivateBrowsing = Cc["@mozilla.org/privatebrowsing;1"].
                              getService(Ci.nsIPrivateBrowsingService).
                              privateBrowsingEnabled;

      if (!inPrivateBrowsing) {
        secondaryActions.push({
          label: browserBundle.GetStringFromName("geolocation.alwaysShare"),
          accessKey: browserBundle.GetStringFromName("geolocation.alwaysShare.accesskey"),
          callback: function () {
            Services.perms.add(requestingURI, "geo", Ci.nsIPermissionManager.ALLOW_ACTION);
            request.allow();
          }
        });
        secondaryActions.push({
          label: browserBundle.GetStringFromName("geolocation.neverShare"),
          accessKey: browserBundle.GetStringFromName("geolocation.neverShare.accesskey"),
          callback: function () {
            Services.perms.add(requestingURI, "geo", Ci.nsIPermissionManager.DENY_ACTION);
            request.cancel();
          }
        });
      }
    }

    var requestingWindow = request.requestingWindow.top;
    var chromeWin = getChromeWindow(requestingWindow).wrappedJSObject;
    var browser = chromeWin.gBrowser.getBrowserForDocument(requestingWindow.document);

    chromeWin.PopupNotifications.show(browser, "geolocation", message, "geo-notification-icon",
                                      mainAction, secondaryActions);
  }
};

var components = [BrowserGlue, GeolocationPrompt];
var NSGetFactory = XPCOMUtils.generateNSGetFactory(components);
