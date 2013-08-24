/*
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
# The Original Code is the Extension Manager.
#
# The Initial Developer of the Original Code is
# the Mozilla Foundation.
# Portions created by the Initial Developer are Copyright (C) 2010
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Dave Townsend <dtownsend@oxymoronical.com>
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
*/

/**
 * This is a default implementation of amIWebInstallListener that should work
 * for most applications but can be overriden. It notifies the observer service
 * about blocked installs. For normal installs it pops up an install
 * confirmation when all the add-ons have been downloaded.
 */

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");
Components.utils.import("resource://gre/modules/AddonManager.jsm");
Components.utils.import("resource://gre/modules/Services.jsm");

["LOG", "WARN", "ERROR"].forEach(function(aName) {
  this.__defineGetter__(aName, function() {
    Components.utils.import("resource://gre/modules/AddonLogging.jsm");

    LogManager.getLogger("addons.weblistener", this);
    return this[aName];
  });
}, this);

function notifyObservers(aTopic, aWindow, aUri, aInstalls) {
  let info = {
    originatingWindow: aWindow,
    originatingURI: aUri,
    installs: aInstalls,

    QueryInterface: XPCOMUtils.generateQI([Ci.amIWebInstallInfo])
  };
  Services.obs.notifyObservers(info, aTopic, null);
}

/**
 * Creates a new installer to monitor downloads and prompt to install when
 * ready
 *
 * @param  aWindow
 *         The window that started the installations
 * @param  aUrl
 *         The URL that started the installations
 * @param  aInstalls
 *         An array of AddonInstalls
 */
function Installer(aWindow, aUrl, aInstalls) {
  this.window = aWindow;
  this.url = aUrl;
  this.downloads = aInstalls;
  this.installed = [];

  notifyObservers("addon-install-started", aWindow, aUrl, aInstalls);

  aInstalls.forEach(function(aInstall) {
    aInstall.addListener(this);

    // Start downloading if it hasn't already begun
    if (aInstall.state == AddonManager.STATE_AVAILABLE)
      aInstall.install();
  }, this);

  this.checkAllDownloaded();
}

Installer.prototype = {
  window: null,
  downloads: null,
  installed: null,
  isDownloading: true,

  /**
   * Checks if all downloads are now complete and if so prompts to install.
   */
  checkAllDownloaded: function() {
    // Prevent re-entrancy caused by the confirmation dialog cancelling unwanted
    // installs.
    if (!this.isDownloading)
      return;

    var failed = [];
    var installs = [];

    for (let i = 0; i < this.downloads.length; i++) {
      let install = this.downloads[i];
      switch (install.state) {
      case AddonManager.STATE_AVAILABLE:
      case AddonManager.STATE_DOWNLOADING:
        // Exit early if any add-ons haven't started downloading yet or are
        // still downloading
        return;
      case AddonManager.STATE_DOWNLOAD_FAILED:
        failed.push(install);
        break;
      case AddonManager.STATE_DOWNLOADED:
        // App disabled items are not compatible and so fail to install
        if (install.addon.appDisabled)
          failed.push(install);
        else
          installs.push(install);

        if (install.linkedInstalls) {
          install.linkedInstalls.forEach(function(aInstall) {
            aInstall.addListener(this);
            // App disabled items are not compatible and so fail to install
            if (aInstall.addon.appDisabled)
              failed.push(aInstall);
            else
              installs.push(aInstall);
          }, this);
        }
        break;
      default:
        WARN("Download of " + install.sourceURI + " in unexpected state " +
             install.state);
      }
    }

    this.isDownloading = false;
    this.downloads = installs;

    if (failed.length > 0) {
      // Stop listening and cancel any installs that are failed because of
      // compatibility reasons.
      failed.forEach(function(aInstall) {
        if (aInstall.state == AddonManager.STATE_DOWNLOADED) {
          aInstall.removeListener(this);
          aInstall.cancel();
        }
      }, this);
      notifyObservers("addon-install-failed", this.window, this.url, failed);
    }

    // If none of the downloads were successful then exit early
    if (this.downloads.length == 0)
      return;

    // Check for a custom installation prompt that may be provided by the
    // applicaton
    if ("@mozilla.org/addons/web-install-prompt;1" in Cc) {
      try {
        let prompt = Cc["@mozilla.org/addons/web-install-prompt;1"].
                     getService(Ci.amIWebInstallPrompt);
        prompt.confirm(this.window, this.url, this.downloads, this.downloads.length);
        return;
      }
      catch (e) {}
    }

    let args = {};
    args.url = this.url;
    args.installs = this.downloads;
    args.wrappedJSObject = args;

    Services.ww.openWindow(this.window, "chrome://mozapps/content/xpinstall/xpinstallConfirm.xul",
                           null, "chrome,modal,centerscreen", args);
  },

  /**
   * Checks if all installs are now complete and if so notifies observers.
   */
  checkAllInstalled: function() {
    var failed = [];

    for (let i = 0; i < this.downloads.length; i++) {
      let install = this.downloads[i];
      switch(install.state) {
      case AddonManager.STATE_DOWNLOADED:
      case AddonManager.STATE_INSTALLING:
        // Exit early if any add-ons haven't started installing yet or are
        // still installing
        return;
      case AddonManager.STATE_INSTALL_FAILED:
        failed.push(install);
        break;
      }
    }

    this.downloads = null;

    if (failed.length > 0)
      notifyObservers("addon-install-failed", this.window, this.url, failed);

    if (this.installed.length > 0)
      notifyObservers("addon-install-complete", this.window, this.url, this.installed);
    this.installed = null;
  },

  onDownloadCancelled: function(aInstall) {
    aInstall.removeListener(this);
    this.checkAllDownloaded();
  },

  onDownloadFailed: function(aInstall) {
    aInstall.removeListener(this);
    this.checkAllDownloaded();
  },

  onDownloadEnded: function(aInstall) {
    this.checkAllDownloaded();
    return false;
  },

  onInstallCancelled: function(aInstall) {
    aInstall.removeListener(this);
    this.checkAllInstalled();
  },

  onInstallFailed: function(aInstall) {
    aInstall.removeListener(this);
    this.checkAllInstalled();
  },

  onInstallEnded: function(aInstall) {
    aInstall.removeListener(this);
    this.installed.push(aInstall);

    // If installing a theme that is disabled and can be enabled then enable it
    if (aInstall.addon.type == "theme" &&
        aInstall.addon.userDisabled == true &&
        aInstall.addon.appDisabled == false) {
      aInstall.addon.userDisabled = false;
    }

    this.checkAllInstalled();
  }
};

function extWebInstallListener() {
}

extWebInstallListener.prototype = {
  /**
   * @see amIWebInstallListener.idl
   */
  onWebInstallBlocked: function(aWindow, aUri, aInstalls) {
    let info = {
      originatingWindow: aWindow,
      originatingURI: aUri,
      installs: aInstalls,

      install: function() {
        new Installer(this.originatingWindow, this.originatingURI, this.installs);
      },

      QueryInterface: XPCOMUtils.generateQI([Ci.amIWebInstallInfo])
    };
    Services.obs.notifyObservers(info, "addon-install-blocked", null);

    return false;
  },

  /**
   * @see amIWebInstallListener.idl
   */
  onWebInstallRequested: function(aWindow, aUri, aInstalls) {
    new Installer(aWindow, aUri, aInstalls);

    // We start the installs ourself
    return false;
  },

  classDescription: "XPI Install Handler",
  contractID: "@mozilla.org/addons/web-install-listener;1",
  classID: Components.ID("{0f38e086-89a3-40a5-8ffc-9b694de1d04a}"),
  QueryInterface: XPCOMUtils.generateQI([Ci.amIWebInstallListener])
};

var NSGetFactory = XPCOMUtils.generateNSGetFactory([extWebInstallListener]);
