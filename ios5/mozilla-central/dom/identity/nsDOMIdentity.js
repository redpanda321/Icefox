/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

const PREF_DEBUG = "toolkit.identity.debug";
const PREF_ENABLED = "dom.identity.enabled";

// Maximum length of a string that will go through IPC
const MAX_STRING_LENGTH = 2048;
// Maximum number of times navigator.id.request can be called for a document
const MAX_RP_CALLS = 100;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");

// This is the child process corresponding to nsIDOMIdentity.


function nsDOMIdentity(aIdentityInternal) {
  this._identityInternal = aIdentityInternal;
}
nsDOMIdentity.prototype = {
  __exposedProps__: {
    // Relying Party (RP)
    watch: 'r',
    request: 'r',
    logout: 'r',

    // Provisioning
    beginProvisioning: 'r',
    genKeyPair: 'r',
    registerCertificate: 'r',
    raiseProvisioningFailure: 'r',

    // Authentication
    beginAuthentication: 'r',
    completeAuthentication: 'r',
    raiseAuthenticationFailure: 'r',
  },

  // nsIDOMIdentity
  /**
   * Relying Party (RP) APIs
   */

  watch: function nsDOMIdentity_watch(aOptions) {
    this._log("watch");
    if (this._rpWatcher) {
      throw new Error("navigator.id.watch was already called");
    }

    if (!aOptions || typeof(aOptions) !== "object") {
      throw new Error("options argument to watch is required");
    }

    // Check for required callbacks
    let requiredCallbacks = ["onlogin", "onlogout"];
    for (let cbName of requiredCallbacks) {
      if ((!(cbName in aOptions))
          || typeof(aOptions[cbName]) !== "function") {
           throw new Error(cbName + " callback is required.");
         }
    }

    // Optional callback "onready"
    if (aOptions["onready"]
        && typeof(aOptions['onready']) !== "function") {
      throw new Error("onready must be a function");
    }

    let message = this.DOMIdentityMessage();

    // loggedInEmail
    message.loggedInEmail = null;
    let emailType = typeof(aOptions["loggedInEmail"]);
    if (aOptions["loggedInEmail"] && aOptions["loggedInEmail"] !== "undefined") {
      if (emailType !== "string") {
        throw new Error("loggedInEmail must be a String or null");
      }

      // TODO: Bug 767610 - check email format.
      // See nsHTMLInputElement::IsValidEmailAddress
      if (aOptions["loggedInEmail"].indexOf("@") == -1
          || aOptions["loggedInEmail"].length > MAX_STRING_LENGTH) {
        throw new Error("loggedInEmail is not valid");
      }
      // Set loggedInEmail in this block that "undefined" doesn't get through.
      message.loggedInEmail = aOptions.loggedInEmail;
    }
    this._log("loggedInEmail: " + message.loggedInEmail);

    this._rpWatcher = aOptions;
    this._identityInternal._mm.sendAsyncMessage("Identity:RP:Watch", message);
  },

  request: function nsDOMIdentity_request(aOptions) {
    // TODO: Bug 769569 - "must be invoked from within a click handler"

    // Has the caller called watch() before this?
    if (!this._rpWatcher) {
      throw new Error("navigator.id.request called before navigator.id.watch");
    }
    if (this._rpCalls > MAX_RP_CALLS) {
      throw new Error("navigator.id.request called too many times");
    }

    let message = this.DOMIdentityMessage();

    if (aOptions) {
      // Optional string properties
      let optionalStringProps = ["privacyPolicy", "termsOfService"];
      for (let propName of optionalStringProps) {
        if (!aOptions[propName] || aOptions[propName] === "undefined")
          continue;
        if (typeof(aOptions[propName]) !== "string") {
          throw new Error(propName + " must be a string representing a URL.");
        }
        if (aOptions[propName].length > MAX_STRING_LENGTH) {
          throw new Error(propName + " is invalid.");
        }
        message[propName] = aOptions[propName];
      }

      if (aOptions["oncancel"]
            && typeof(aOptions["oncancel"]) !== "function") {
        throw new Error("oncancel is not a function");
      } else {
        // Store optional cancel callback for later.
        this._onCancelRequestCallback = aOptions.oncancel;
      }
    }

    this._rpCalls++;
    this._identityInternal._mm.sendAsyncMessage("Identity:RP:Request", message);
  },

  logout: function nsDOMIdentity_logout() {
    if (!this._rpWatcher) {
      throw new Error("navigator.id.logout called before navigator.id.watch");
    }
    if (this._rpCalls > MAX_RP_CALLS) {
      throw new Error("navigator.id.logout called too many times");
    }

    this._rpCalls++;
    let message = this.DOMIdentityMessage();
    this._identityInternal._mm.sendAsyncMessage("Identity:RP:Logout", message);
  },

  /**
   *  Identity Provider (IDP) Provisioning APIs
   */

  beginProvisioning: function nsDOMIdentity_beginProvisioning(aCallback) {
    this._log("beginProvisioning");
    if (this._beginProvisioningCallback) {
      throw new Error("navigator.id.beginProvisioning already called.");
    }
    if (!aCallback || typeof(aCallback) !== "function") {
      throw new Error("beginProvisioning callback is required.");
    }

    this._beginProvisioningCallback = aCallback;
    this._identityInternal._mm.sendAsyncMessage("Identity:IDP:BeginProvisioning",
                                                this.DOMIdentityMessage());
  },

  genKeyPair: function nsDOMIdentity_genKeyPair(aCallback) {
    this._log("genKeyPair");
    if (!this._beginProvisioningCallback) {
      throw new Error("navigator.id.genKeyPair called outside of provisioning");
    }
    if (this._genKeyPairCallback) {
      throw new Error("navigator.id.genKeyPair already called.");
    }
    if (!aCallback || typeof(aCallback) !== "function") {
      throw new Error("genKeyPair callback is required.");
    }

    this._genKeyPairCallback = aCallback;
    this._identityInternal._mm.sendAsyncMessage("Identity:IDP:GenKeyPair",
                                                this.DOMIdentityMessage());
  },

  registerCertificate: function nsDOMIdentity_registerCertificate(aCertificate) {
    this._log("registerCertificate");
    if (!this._genKeyPairCallback) {
      throw new Error("navigator.id.registerCertificate called outside of provisioning");
    }
    if (this._provisioningEnded) {
      throw new Error("Provisioning already ended");
    }
    this._provisioningEnded = true;

    let message = this.DOMIdentityMessage();
    message.cert = aCertificate;
    this._identityInternal._mm.sendAsyncMessage("Identity:IDP:RegisterCertificate", message);
  },

  raiseProvisioningFailure: function nsDOMIdentity_raiseProvisioningFailure(aReason) {
    this._log("raiseProvisioningFailure '" + aReason + "'");
    if (this._provisioningEnded) {
      throw new Error("Provisioning already ended");
    }
    if (!aReason || typeof(aReason) != "string") {
      throw new Error("raiseProvisioningFailure reason is required");
    }
    this._provisioningEnded = true;

    let message = this.DOMIdentityMessage();
    message.reason = aReason;
    this._identityInternal._mm.sendAsyncMessage("Identity:IDP:ProvisioningFailure", message);
  },

  /**
   *  Identity Provider (IDP) Authentication APIs
   */

  beginAuthentication: function nsDOMIdentity_beginAuthentication(aCallback) {
    this._log("beginAuthentication");
    if (this._beginAuthenticationCallback) {
      throw new Error("navigator.id.beginAuthentication already called.");
    }
    if (typeof(aCallback) !== "function") {
      throw new Error("beginAuthentication callback is required.");
    }
    if (!aCallback || typeof(aCallback) !== "function") {
      throw new Error("beginAuthentication callback is required.");
    }

    this._beginAuthenticationCallback = aCallback;
    this._identityInternal._mm.sendAsyncMessage("Identity:IDP:BeginAuthentication",
                                                this.DOMIdentityMessage());
  },

  completeAuthentication: function nsDOMIdentity_completeAuthentication() {
    if (this._authenticationEnded) {
      throw new Error("Authentication already ended");
    }
    if (!this._beginAuthenticationCallback) {
      throw new Error("navigator.id.completeAuthentication called outside of authentication");
    }
    this._authenticationEnded = true;

    this._identityInternal._mm.sendAsyncMessage("Identity:IDP:CompleteAuthentication",
                                                this.DOMIdentityMessage());
  },

  raiseAuthenticationFailure: function nsDOMIdentity_raiseAuthenticationFailure(aReason) {
    if (this._authenticationEnded) {
      throw new Error("Authentication already ended");
    }
    if (!aReason || typeof(aReason) != "string") {
      throw new Error("raiseProvisioningFailure reason is required");
    }

    let message = this.DOMIdentityMessage();
    message.reason = aReason;
    this._identityInternal._mm.sendAsyncMessage("Identity:IDP:AuthenticationFailure", message);
  },

  // Private.
  _init: function nsDOMIdentity__init(aWindow) {

    this._initializeState();

    // Store window and origin URI.
    this._window = aWindow;
    this._origin = aWindow.document.nodePrincipal.origin;

    // Setup identifiers for current window.
    let util = aWindow.QueryInterface(Ci.nsIInterfaceRequestor)
                      .getInterface(Ci.nsIDOMWindowUtils);
    this._id = util.outerWindowID;
  },

  /**
   * Called during init and shutdown.
   */
  _initializeState: function nsDOMIdentity__initializeState() {
    // Some state to prevent abuse
    // Limit the number of calls to .request
    this._rpCalls = 0;
    this._provisioningEnded = false;
    this._authenticationEnded = false;

    this._rpWatcher = null;
    this._onCancelRequestCallback = null;
    this._beginProvisioningCallback = null;
    this._genKeyPairCallback = null;
    this._beginAuthenticationCallback = null;
  },

  _receiveMessage: function nsDOMIdentity_receiveMessage(aMessage) {
    let msg = aMessage.json;
    this._log("receiveMessage: " + aMessage.name);

    switch (aMessage.name) {
      case "Identity:ResetState":
        if (!this._identityInternal._debug) {
          return;
        }
        this._initializeState();
        Services.obs.notifyObservers(null, "identity-DOM-state-reset", this._id);
        break;
      case "Identity:RP:Watch:OnLogin":
        // Do we have a watcher?
        if (!this._rpWatcher) {
          return;
        }

        if (this._rpWatcher.onlogin) {
          this._rpWatcher.onlogin(msg.assertion);
        }
        break;
      case "Identity:RP:Watch:OnLogout":
        // Do we have a watcher?
        if (!this._rpWatcher) {
          return;
        }

        if (this._rpWatcher.onlogout) {
          this._rpWatcher.onlogout();
        }
        break;
      case "Identity:RP:Watch:OnReady":
        // Do we have a watcher?
        if (!this._rpWatcher) {
          return;
        }

        if (this._rpWatcher.onready) {
          this._rpWatcher.onready();
        }
        break;
      case "Identity:RP:Request:OnCancel":
        // Do we have a watcher?
        if (!this._rpWatcher) {
          return;
        }

        if (this._onCancelRequestCallback) {
          this._onCancelRequestCallback();
        }
        break;
      case "Identity:IDP:CallBeginProvisioningCallback":
        this._callBeginProvisioningCallback(msg);
        break;
      case "Identity:IDP:CallGenKeyPairCallback":
        this._callGenKeyPairCallback(msg);
        break;
      case "Identity:IDP:CallBeginAuthenticationCallback":
        this._callBeginAuthenticationCallback(msg);
        break;
    }
  },

  _log: function nsDOMIdentity__log(msg) {
    this._identityInternal._log(msg);
  },

  _callGenKeyPairCallback: function nsDOMIdentity__callGenKeyPairCallback(message) {
    // create a pubkey object that works
    let chrome_pubkey = JSON.parse(message.publicKey);

    // bunch of stuff to create a proper object in window context
    function genPropDesc(value) {
      return {
        enumerable: true, configurable: true, writable: true, value: value
      };
    }

    let propList = {};
    for (let k in chrome_pubkey) {
      propList[k] = genPropDesc(chrome_pubkey[k]);
    }

    let pubkey = Cu.createObjectIn(this._window);
    Object.defineProperties(pubkey, propList);
    Cu.makeObjectPropsNormal(pubkey);

    // do the callback
    this._genKeyPairCallback(pubkey);
  },

  _callBeginProvisioningCallback:
      function nsDOMIdentity__callBeginProvisioningCallback(message) {
    let identity = message.identity;
    let certValidityDuration = message.certDuration;
    this._beginProvisioningCallback(identity,
                                    certValidityDuration);
  },

  _callBeginAuthenticationCallback:
      function nsDOMIdentity__callBeginAuthenticationCallback(message) {
    let identity = message.identity;
    this._beginAuthenticationCallback(identity);
  },

  /**
   * Helper to create messages to send using a message manager
   */
  DOMIdentityMessage: function DOMIdentityMessage() {
    return {
      id: this._id,
      origin: this._origin,
    };
  },

};

/**
 * Internal functions that shouldn't be exposed to content.
 */
function nsDOMIdentityInternal() {
}
nsDOMIdentityInternal.prototype = {

  // nsIFrameMessageListener
  receiveMessage: function nsDOMIdentityInternal_receiveMessage(aMessage) {
    let msg = aMessage.json;
    // Is this message intended for this window?
    if (msg.id != this._id) {
      return;
    }
    this._identity._receiveMessage(aMessage);
  },

  // nsIObserver
  observe: function nsDOMIdentityInternal_observe(aSubject, aTopic, aData) {
    let wId = aSubject.QueryInterface(Ci.nsISupportsPRUint64).data;
    if (wId != this._innerWindowID) {
      return;
    }

    Services.obs.removeObserver(this, "inner-window-destroyed");
    this._identity._initializeState();
    this._identity = null;

    // TODO: Also send message to DOMIdentity notifiying window is no longer valid
    // ie. in the case that the user closes the auth. window and we need to know.

    try {
      for (let msgName of this._messages) {
        this._mm.removeMessageListener(msgName, this);
      }
    } catch (ex) {
      // Avoid errors when removing more than once.
    }

    this._mm = null;
  },

  // nsIDOMGlobalPropertyInitializer
  init: function nsDOMIdentityInternal_init(aWindow) {
    if (Services.prefs.getPrefType(PREF_ENABLED) != Ci.nsIPrefBranch.PREF_BOOL
        || !Services.prefs.getBoolPref(PREF_ENABLED)) {
      return null;
    }

    this._debug =
      Services.prefs.getPrefType(PREF_DEBUG) == Ci.nsIPrefBranch.PREF_BOOL
      && Services.prefs.getBoolPref(PREF_DEBUG);

    this._identity = new nsDOMIdentity(this);

    this._identity._init(aWindow);

    let util = aWindow.QueryInterface(Ci.nsIInterfaceRequestor)
                      .getInterface(Ci.nsIDOMWindowUtils);
    this._id = util.outerWindowID;
    this._innerWindowID = util.currentInnerWindowID;

    this._log("init was called from " + aWindow.document.location);

    this._mm = aWindow.QueryInterface(Ci.nsIInterfaceRequestor)
                      .getInterface(Ci.nsIWebNavigation)
                      .QueryInterface(Ci.nsIInterfaceRequestor)
                      .getInterface(Ci.nsIContentFrameMessageManager);

    // Setup listeners for messages from parent process.
    this._messages = [
      "Identity:ResetState",
      "Identity:RP:Watch:OnLogin",
      "Identity:RP:Watch:OnLogout",
      "Identity:RP:Watch:OnReady",
      "Identity:RP:Request:OnCancel",
      "Identity:IDP:CallBeginProvisioningCallback",
      "Identity:IDP:CallGenKeyPairCallback",
      "Identity:IDP:CallBeginAuthenticationCallback",
    ];
    this._messages.forEach((function(msgName) {
      this._mm.addMessageListener(msgName, this);
    }).bind(this));

    // Setup observers so we can remove message listeners.
    Services.obs.addObserver(this, "inner-window-destroyed", false);

    return this._identity;
  },

  // Private.
  _log: function nsDOMIdentityInternal__log(msg) {
    if (!this._debug) {
      return;
    }
    dump("nsDOMIdentity (" + this._id + "): " + msg + "\n");
  },

  // Component setup.
  classID: Components.ID("{8bcac6a3-56a4-43a4-a44c-cdf42763002f}"),

  QueryInterface: XPCOMUtils.generateQI(
    [Ci.nsIDOMGlobalPropertyInitializer, Ci.nsIFrameMessageListener]
  ),

  classInfo: XPCOMUtils.generateCI({
    classID: Components.ID("{8bcac6a3-56a4-43a4-a44c-cdf42763002f}"),
    contractID: "@mozilla.org/dom/identity;1",
    interfaces: [],
    classDescription: "Identity DOM Implementation"
  })

};

const NSGetFactory = XPCOMUtils.generateNSGetFactory([nsDOMIdentityInternal]);
