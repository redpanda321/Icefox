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
#   Alon Zakai <azakai@mozilla.com>
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

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

const MSG_INSTALL_ENABLED  = "WebInstallerIsInstallEnabled";
const MSG_INSTALL_ADDONS   = "WebInstallerInstallAddonsFromWebpage";
const MSG_INSTALL_CALLBACK = "WebInstallerInstallCallback";

var gIoService = Components.classes["@mozilla.org/network/io-service;1"]
                           .getService(Components.interfaces.nsIIOService);

function InstallTrigger(installerId, window) {
  this.installerId = installerId;
  this.window = window;
}

InstallTrigger.prototype = {
  __exposedProps__: {
    SKIN: "r",
    LOCALE: "r",
    CONTENT: "r",
    PACKAGE: "r",
    enabled: "r",
    updateEnabled: "r",
    install: "r",
    installChrome: "r",
    startSoftwareUpdate: "r",
    toSource: "r", // XXX workaround for bug 582100
  },

  // == Public interface ==

  SKIN: Ci.amIInstallTrigger.SKIN,
  LOCALE: Ci.amIInstallTrigger.LOCALE,
  CONTENT: Ci.amIInstallTrigger.CONTENT,
  PACKAGE: Ci.amIInstallTrigger.PACKAGE,

  /**
   * @see amIInstallTriggerInstaller.idl
   */
  enabled: function() {
    return sendSyncMessage(MSG_INSTALL_ENABLED, {
      mimetype: "application/x-xpinstall", referer: this.window.location.href
    })[0];
  },

  /**
   * @see amIInstallTriggerInstaller.idl
   */
  updateEnabled: function() {
    return this.enabled();
  },

  /**
   * @see amIInstallTriggerInstaller.idl
   */
  install: function(aArgs, aCallback) {
    var params = {
      installerId: this.installerId,
      mimetype: "application/x-xpinstall",
      referer: this.window.location.href,
      uris: [],
      hashes: [],
      names: [],
      icons: [],
    };

    for (var name in aArgs) {
      var item = aArgs[name];
      if (typeof item === 'string') {
        item = { URL: item };
      } else if (!("URL" in item)) {
        throw new Error("Missing URL property for '" + name + "'");
      }

      // Resolve and validate urls
      var url = this.resolveURL(item.URL);
      if (!this.checkLoadURIFromScript(url))
        throw new Error("insufficient permissions to install: " + url);

      var iconUrl = null;
      if ("IconURL" in item) {
        iconUrl = this.resolveURL(item.IconURL);
        if (!this.checkLoadURIFromScript(iconUrl)) {
          iconUrl = null; // If page can't load the icon, just ignore it
        }
      }
      params.uris.push(url.spec);
      params.hashes.push("Hash" in item ? item.Hash : null);
      params.names.push(name);
      params.icons.push(iconUrl ? iconUrl.spec : null);
    }
    // Add callback Id, done here, so only if we actually got here
    params.callbackId = this.addCallback(aCallback, params.uris);
    // Send message
    return sendSyncMessage(MSG_INSTALL_ADDONS, params)[0];
  },

  /**
   * @see amIInstallTriggerInstaller.idl
   */
  startSoftwareUpdate: function(aUrl, aFlags) {
    var url = gIoService.newURI(aUrl, null, null)
                        .QueryInterface(Ci.nsIURL).filename;
    var object = {};
    object[url] = { "URL": aUrl };
    return this.install(object);
  },

  /**
   * @see amIInstallTriggerInstaller.idl
   */
  installChrome: function(aType, aUrl, aSkin) {
    return this.startSoftwareUpdate(aUrl);
  },

  // == Internal, hidden machinery ==

  callbacks: {},

  /**
   * Adds a callback to the list of callbacks we may receive messages
   * about from the parent process. We save them here; only callback IDs
   * are sent over IPC.
   *
   * @param  callback
   *         The callback function
   * @param  urls
   *         The urls this callback function will receive responses for.
   *         After all the callbacks have arrived, we can forget about the
   *         callback.
   *
   * @return The callback ID, an integer identifying this callback.
   */
  addCallback: function(aCallback, aUrls) {
    if (!aCallback)
      return -1;
    var callbackId = 0;
    while (callbackId in this.callbacks)
      callbackId++;
    this.callbacks[callbackId] = {
      callback: aCallback,
      urls: aUrls.slice(0), // Clone the urls for our own use (it lets
                            // us know when no further callbacks will
                            // occur)
    };
    return callbackId;
  },

  /**
   * Resolves a URL in the context of our current window. We need to do
   * this before sending URLs to the parent process.
   *
   * @param  aUrl
   *         The url to resolve.
   *
   * @return A resolved, absolute nsURI object.
   */
  resolveURL: function(aUrl) {
    return gIoService.newURI(aUrl, null,
                             this.window.document.documentURIObject);
  },

  /**
   * @see amInstallTrigger.cpp
   * TODO: When e10s lands on m-c, consider removing amInstallTrigger.cpp
   *       See bug 571166
   */
  checkLoadURIFromScript: function(aUri) {
    var secman = Cc["@mozilla.org/scriptsecuritymanager;1"].
                 getService(Ci.nsIScriptSecurityManager);
    var principal = this.window.content.document.nodePrincipal;
    try {
      secman.checkLoadURIWithPrincipal(principal, aUri,
        Ci.nsIScriptSecurityManager.DISALLOW_INHERIT_PRINCIPAL);
      return true;
    }
    catch(e) {
      return false;
    }
  },
};

/**
 * Child part of InstallTrigger e10s handling.
 *
 * Sets up InstallTriggers on newly-created windows,
 * that will relay messages for InstallTrigger
 * activity. We also process the parameters for
 * the InstallTrigger to proper parameters for
 * amIWebInstaller.
 */
function InstallTriggerManager() {
  this.installerIds = [];
  this.nextInstallerId = 0;

  addMessageListener(MSG_INSTALL_CALLBACK, this);

  addEventListener("DOMWindowCreated", this, false);

  var self = this;
  addEventListener("unload", function() {
    // Clean up all references, to help gc work quickly
    for (var installerId in self.installerIds) {
      self.installerIds[installerId].callbacks = null;
      self.installerIds[installerId] = null;
    }
    self.installerIds = null;
  }, false);
}

InstallTriggerManager.prototype = {
  handleEvent: function handleEvent(aEvent) {
    var window = aEvent.originalTarget.defaultView.content;

    // Need to make sure we are called on what we care about -
    // content windows. DOMWindowCreated is called on *all* HTMLDocuments,
    // some of which belong to ChromeWindows or lack defaultView.content
    // altogether.
    //
    // Note about the syntax used here: |"wrappedJSObject" in window|
    // will silently fail, without even letting us catch it as an
    // exception, and checking in the way that we do check in some
    // cases still throws an exception; see bug 582108 about both.
    try {
      if (!window || !window.wrappedJSObject) {
        return;
      }
    }
    catch(e) {
      return;
    }

    // This event happens for each HTMLDocument, so it can happen more than
    // once per Window. We only need to work once per Window though.
    if (window.wrappedJSObject.InstallTrigger)
        return;

    // Create the public object which web scripts can see
    var installerId = this.nextInstallerId ++;
    var installTrigger = new InstallTrigger(installerId, window);
    this.installerIds[installerId] = installTrigger;
    window.wrappedJSObject.InstallTrigger = installTrigger;
  },

  /**
   * Receives a message about a callback. Performs the actual callback
   * (for the callback with the ID we are given). When
   * all URLs are exhausted, can free the callbackId and linked stuff.
   *
   * @param  message
   *         The IPC message. Contains IDs of the installer and the
   *         callback.
   *
   */
  receiveMessage: function(aMessage) {
    var payload = aMessage.json;
    var installer = this.installerIds[payload.installerId];
    var callbackId = payload.callbackId;
    var url = payload.url;
    var status = payload.status;
    var callbackObj = installer.callbacks[callbackId];
    if (!callbackObj)
      return;
    try {
      callbackObj.callback(url, status);
    }
    catch (e) {
      dump("InstallTrigger callback threw an exception: " + e + "\n");
    }
    callbackObj.urls.splice(callbackObj.urls.indexOf(url), 1);
    if (callbackObj.urls.length == 0)
      installer.callbacks[callbackId] = null;
  },
};

new InstallTriggerManager();

