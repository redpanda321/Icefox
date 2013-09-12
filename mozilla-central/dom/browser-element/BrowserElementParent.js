/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

let Cu = Components.utils;
let Ci = Components.interfaces;
let Cc = Components.classes;
let Cr = Components.results;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/BrowserElementPromptService.jsm");

XPCOMUtils.defineLazyGetter(this, "DOMApplicationRegistry", function () {
  Cu.import("resource://gre/modules/Webapps.jsm");
  return DOMApplicationRegistry;
});

const NS_PREFBRANCH_PREFCHANGE_TOPIC_ID = "nsPref:changed";
const BROWSER_FRAMES_ENABLED_PREF = "dom.mozBrowserFramesEnabled";
const TOUCH_EVENTS_ENABLED_PREF = "dom.w3c_touch_events.enabled";

function debug(msg) {
  //dump("BrowserElementParent - " + msg + "\n");
}

function getBoolPref(prefName, def) {
  try {
    return Services.prefs.getBoolPref(prefName);
  }
  catch(err) {
    return def;
  }
}

function getIntPref(prefName, def) {
  try {
    return Services.prefs.getIntPref(prefName);
  }
  catch(err) {
    return def;
  }
}

function exposeAll(obj) {
  // Filter for Objects and Arrays.
  if (typeof obj !== "object" || !obj)
    return;

  // Recursively expose our children.
  Object.keys(obj).forEach(function(key) {
    exposeAll(obj[key]);
  });

  // If we're not an Array, generate an __exposedProps__ object for ourselves.
  if (obj instanceof Array)
    return;
  var exposed = {};
  Object.keys(obj).forEach(function(key) {
    exposed[key] = 'rw';
  });
  obj.__exposedProps__ = exposed;
}

function defineAndExpose(obj, name, value) {
  obj[name] = value;
  if (!('__exposedProps__' in obj))
    obj.__exposedProps__ = {};
  obj.__exposedProps__[name] = 'r';
}

/**
 * BrowserElementParent implements one half of <iframe mozbrowser>.  (The other
 * half is, unsurprisingly, BrowserElementChild.)
 *
 * BrowserElementParentFactory detects when we create a windows or docshell
 * contained inside a <iframe mozbrowser> and creates a BrowserElementParent
 * object for that window.
 *
 * BrowserElementParent injects script to listen for certain events in the
 * child.  We then listen to messages from the child script and take
 * appropriate action here in the parent.
 */

function BrowserElementParentFactory() {
  this._initialized = false;
}

BrowserElementParentFactory.prototype = {
  classID: Components.ID("{ddeafdac-cb39-47c4-9cb8-c9027ee36d26}"),
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIObserver,
                                         Ci.nsISupportsWeakReference]),

  /**
   * Called on app startup, and also when the browser frames enabled pref is
   * changed.
   */
  _init: function() {
    if (this._initialized) {
      return;
    }

    // If the pref is disabled, do nothing except wait for the pref to change.
    // (This is important for tests, if nothing else.)
    if (!this._browserFramesPrefEnabled()) {
      var prefs = Cc["@mozilla.org/preferences-service;1"].getService(Ci.nsIPrefBranch);
      prefs.addObserver(BROWSER_FRAMES_ENABLED_PREF, this, /* ownsWeak = */ true);
      return;
    }

    debug("_init");
    this._initialized = true;

    // Maps frame elements to BrowserElementParent objects.  We never look up
    // anything in this map; the purpose is to keep the BrowserElementParent
    // alive for as long as its frame element lives.
    this._bepMap = new WeakMap();

    var os = Cc["@mozilla.org/observer-service;1"].getService(Ci.nsIObserverService);
    os.addObserver(this, 'remote-browser-frame-shown', /* ownsWeak = */ true);
    os.addObserver(this, 'in-process-browser-or-app-frame-shown', /* ownsWeak = */ true);
  },

  _browserFramesPrefEnabled: function() {
    var prefs = Cc["@mozilla.org/preferences-service;1"].getService(Ci.nsIPrefBranch);
    try {
      return prefs.getBoolPref(BROWSER_FRAMES_ENABLED_PREF);
    }
    catch(e) {
      return false;
    }
  },

  _observeInProcessBrowserFrameShown: function(frameLoader) {
    debug("In-process browser frame shown " + frameLoader);
    this._createBrowserElementParent(frameLoader, /* hasRemoteFrame = */ false);
  },

  _observeRemoteBrowserFrameShown: function(frameLoader) {
    debug("Remote browser frame shown " + frameLoader);
    this._createBrowserElementParent(frameLoader, /* hasRemoteFrame = */ true);
  },

  _createBrowserElementParent: function(frameLoader, hasRemoteFrame) {
    let frameElement = frameLoader.QueryInterface(Ci.nsIFrameLoader).ownerElement;
    this._bepMap.set(frameElement, new BrowserElementParent(frameLoader, hasRemoteFrame));
  },

  observe: function(subject, topic, data) {
    switch(topic) {
    case 'app-startup':
      this._init();
      break;
    case NS_PREFBRANCH_PREFCHANGE_TOPIC_ID:
      if (data == BROWSER_FRAMES_ENABLED_PREF) {
        this._init();
      }
      break;
    case 'remote-browser-frame-shown':
      this._observeRemoteBrowserFrameShown(subject);
      break;
    case 'in-process-browser-or-app-frame-shown':
      this._observeInProcessBrowserFrameShown(subject);
      break;
    case 'content-document-global-created':
      this._observeContentGlobalCreated(subject);
      break;
    }
  },
};

function BrowserElementParent(frameLoader, hasRemoteFrame) {
  debug("Creating new BrowserElementParent object for " + frameLoader);
  this._domRequestCounter = 0;
  this._pendingDOMRequests = {};
  this._hasRemoteFrame = hasRemoteFrame;
  this._nextPaintListeners = [];

  this._frameLoader = frameLoader;
  this._frameElement = frameLoader.QueryInterface(Ci.nsIFrameLoader).ownerElement;
  if (!this._frameElement) {
    debug("No frame element?");
    return;
  }

  this._mm = frameLoader.messageManager;

  // Messages we receive are handed to functions which take a (data) argument,
  // where |data| is the message manager's data object.

  let self = this;
  function addMessageListener(msg, handler) {
    function checkedHandler() {
      if (self._isAlive()) {
        return handler.apply(self, arguments);
      }
    }
    self._mm.addMessageListener('browser-element-api:' + msg, checkedHandler);
  }

  addMessageListener("hello", this._recvHello);
  addMessageListener("get-name", this._recvGetName);
  addMessageListener("get-fullscreen-allowed", this._recvGetFullscreenAllowed);
  addMessageListener("contextmenu", this._fireCtxMenuEvent);
  addMessageListener("locationchange", this._fireEventFromMsg);
  addMessageListener("loadstart", this._fireEventFromMsg);
  addMessageListener("loadend", this._fireEventFromMsg);
  addMessageListener("titlechange", this._fireEventFromMsg);
  addMessageListener("iconchange", this._fireEventFromMsg);
  addMessageListener("close", this._fireEventFromMsg);
  addMessageListener("securitychange", this._fireEventFromMsg);
  addMessageListener("error", this._fireEventFromMsg);
  addMessageListener("scroll", this._fireEventFromMsg);
  addMessageListener("firstpaint", this._fireEventFromMsg);
  addMessageListener("nextpaint", this._recvNextPaint);
  addMessageListener("keyevent", this._fireKeyEvent);
  addMessageListener("showmodalprompt", this._handleShowModalPrompt);
  addMessageListener('got-purge-history', this._gotDOMRequestResult);
  addMessageListener('got-screenshot', this._gotDOMRequestResult);
  addMessageListener('got-can-go-back', this._gotDOMRequestResult);
  addMessageListener('got-can-go-forward', this._gotDOMRequestResult);
  addMessageListener('fullscreen-origin-change', this._remoteFullscreenOriginChange);
  addMessageListener('rollback-fullscreen', this._remoteFrameFullscreenReverted);
  addMessageListener('exit-fullscreen', this._exitFullscreen);

  let os = Cc["@mozilla.org/observer-service;1"].getService(Ci.nsIObserverService);
  os.addObserver(this, 'ask-children-to-exit-fullscreen', /* ownsWeak = */ true);
  os.addObserver(this, 'oop-frameloader-crashed', /* ownsWeak = */ true);

  function defineMethod(name, fn) {
    XPCNativeWrapper.unwrap(self._frameElement)[name] = function() {
      if (self._isAlive()) {
        return fn.apply(self, arguments);
      }
    };
  }

  function defineDOMRequestMethod(domName, msgName) {
    XPCNativeWrapper.unwrap(self._frameElement)[domName] = function() {
      if (self._isAlive()) {
        return self._sendDOMRequest(msgName);
      }
    };
  }

  // Define methods on the frame element.
  defineMethod('setVisible', this._setVisible);
  defineMethod('sendMouseEvent', this._sendMouseEvent);

  // 0 = disabled, 1 = enabled, 2 - auto detect
  if (getIntPref(TOUCH_EVENTS_ENABLED_PREF, 0) != 0) {
    defineMethod('sendTouchEvent', this._sendTouchEvent);
  }
  defineMethod('goBack', this._goBack);
  defineMethod('goForward', this._goForward);
  defineMethod('reload', this._reload);
  defineMethod('stop', this._stop);
  defineMethod('purgeHistory', this._purgeHistory);
  defineMethod('getScreenshot', this._getScreenshot);
  defineMethod('addNextPaintListener', this._addNextPaintListener);
  defineMethod('removeNextPaintListener', this._removeNextPaintListener);
  defineDOMRequestMethod('getCanGoBack', 'get-can-go-back');
  defineDOMRequestMethod('getCanGoForward', 'get-can-go-forward');

  // Listen to visibilitychange on the iframe's owner window, and forward it
  // down to the child.
  this._window.addEventListener('visibilitychange',
                                this._ownerVisibilityChange.bind(this),
                                /* useCapture = */ false,
                                /* wantsUntrusted = */ false);

  // Insert ourself into the prompt service.
  BrowserElementPromptService.mapFrameToBrowserElementParent(this._frameElement, this);

  // If this browser represents an app then let the Webapps module register for
  // any messages that it needs.
  let appManifestURL =
    this._frameElement.QueryInterface(Ci.nsIMozBrowserFrame).appManifestURL;
  if (appManifestURL) {
    let appId =
      DOMApplicationRegistry.getAppLocalIdByManifestURL(appManifestURL);
    if (appId != Ci.nsIScriptSecurityManager.NO_APP_ID) {
      DOMApplicationRegistry.registerBrowserElementParentForApp(this, appId);
    }
  }
}

BrowserElementParent.prototype = {

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIObserver,
                                         Ci.nsISupportsWeakReference]),

  /**
   * You shouldn't touch this._frameElement or this._window if _isAlive is
   * false.  (You'll likely get an exception if you do.)
   */
  _isAlive: function() {
    return !Cu.isDeadWrapper(this._frameElement) &&
           !Cu.isDeadWrapper(this._frameElement.ownerDocument) &&
           !Cu.isDeadWrapper(this._frameElement.ownerDocument.defaultView);
  },

  get _window() {
    return this._frameElement.ownerDocument.defaultView;
  },

  get _windowUtils() {
    return this._window.QueryInterface(Ci.nsIInterfaceRequestor)
                       .getInterface(Ci.nsIDOMWindowUtils);
  },

  promptAuth: function(authDetail, callback) {
    let evt;
    let self = this;
    let callbackCalled = false;
    let cancelCallback = function() {
      if (!callbackCalled) {
        callbackCalled = true;
        callback(false, null, null);
      }
    };

    if (authDetail.isOnlyPassword) {
      // We don't handle password-only prompts, so just cancel it.
      cancelCallback();
      return;
    } else { /* username and password */
      let detail = {
        host:     authDetail.host,
        realm:    authDetail.realm
      };

      evt = this._createEvent('usernameandpasswordrequired', detail,
                              /* cancelable */ true);
      defineAndExpose(evt.detail, 'authenticate', function(username, password) {
        if (callbackCalled)
          return;
        callbackCalled = true;
        callback(true, username, password);
      });
    }

    defineAndExpose(evt.detail, 'cancel', function() {
      cancelCallback();
    });

    this._frameElement.dispatchEvent(evt);

    if (!evt.defaultPrevented) {
      cancelCallback();
    }
  },

  _sendAsyncMsg: function(msg, data) {
    try {
      this._mm.sendAsyncMessage('browser-element-api:' + msg, data);
    } catch (e) {
      return false;
    }
    return true;
  },

  _recvHello: function(data) {
    debug("recvHello");

    // Inform our child if our owner element's document is invisible.  Note
    // that we must do so here, rather than in the BrowserElementParent
    // constructor, because the BrowserElementChild may not be initialized when
    // we run our constructor.
    if (this._window.document.hidden) {
      this._ownerVisibilityChange();
    }
  },

  _recvGetName: function(data) {
    return this._frameElement.getAttribute('name');
  },

  _recvGetFullscreenAllowed: function(data) {
    return this._frameElement.hasAttribute('allowfullscreen') ||
           this._frameElement.hasAttribute('mozallowfullscreen');
  },

  _fireCtxMenuEvent: function(data) {
    let evtName = data.name.substring('browser-element-api:'.length);
    let detail = data.json;

    debug('fireCtxMenuEventFromMsg: ' + evtName + ' ' + detail);
    let evt = this._createEvent(evtName, detail);

    if (detail.contextmenu) {
      var self = this;
      defineAndExpose(evt.detail, 'contextMenuItemSelected', function(id) {
        self._sendAsyncMsg('fire-ctx-callback', {menuitem: id});
      });
    }
    // The embedder may have default actions on context menu events, so
    // we fire a context menu event even if the child didn't define a
    // custom context menu
    this._frameElement.dispatchEvent(evt);
  },

  /**
   * Fire either a vanilla or a custom event, depending on the contents of
   * |data|.
   */
  _fireEventFromMsg: function(data) {
    let name = data.name.substring('browser-element-api:'.length);
    let detail = data.json;

    debug('fireEventFromMsg: ' + name + ', ' + detail);
    let evt = this._createEvent(name, detail,
                                /* cancelable = */ false);
    this._frameElement.dispatchEvent(evt);
  },

  _handleShowModalPrompt: function(data) {
    // Fire a showmodalprmopt event on the iframe.  When this method is called,
    // the child is spinning in a nested event loop waiting for an
    // unblock-modal-prompt message.
    //
    // If the embedder calls preventDefault() on the showmodalprompt event,
    // we'll block the child until event.detail.unblock() is called.
    //
    // Otherwise, if preventDefault() is not called, we'll send the
    // unblock-modal-prompt message to the child as soon as the event is done
    // dispatching.

    let detail = data.json;
    debug('handleShowPrompt ' + JSON.stringify(detail));

    // Strip off the windowID property from the object we send along in the
    // event.
    let windowID = detail.windowID;
    delete detail.windowID;
    debug("Event will have detail: " + JSON.stringify(detail));
    let evt = this._createEvent('showmodalprompt', detail,
                                /* cancelable = */ true);

    let self = this;
    let unblockMsgSent = false;
    function sendUnblockMsg() {
      if (unblockMsgSent) {
        return;
      }
      unblockMsgSent = true;

      // We don't need to sanitize evt.detail.returnValue (e.g. converting the
      // return value of confirm() to a boolean); Gecko does that for us.

      let data = { windowID: windowID,
                   returnValue: evt.detail.returnValue };
      self._sendAsyncMsg('unblock-modal-prompt', data);
    }

    defineAndExpose(evt.detail, 'unblock', function() {
      sendUnblockMsg();
    });

    this._frameElement.dispatchEvent(evt);

    if (!evt.defaultPrevented) {
      // Unblock the inner frame immediately.  Otherwise we'll unblock upon
      // evt.detail.unblock().
      sendUnblockMsg();
    }
  },

  _createEvent: function(evtName, detail, cancelable) {
    // This will have to change if we ever want to send a CustomEvent with null
    // detail.  For now, it's OK.
    if (detail !== undefined && detail !== null) {
      exposeAll(detail);
      return new this._window.CustomEvent('mozbrowser' + evtName,
                                          { bubbles: true,
                                            cancelable: cancelable,
                                            detail: detail });
    }

    return new this._window.Event('mozbrowser' + evtName,
                                  { bubbles: true,
                                    cancelable: cancelable });
  },

  /**
   * Kick off a DOMRequest in the child process.
   *
   * We'll fire an event called |msgName| on the child process, passing along
   * an object with two fields:
   *
   *  - id:  the ID of this request.
   *  - arg: arguments to pass to the child along with this request.
   *
   * We expect the child to pass the ID back to us upon completion of the
   * request.  See _gotDOMRequestResult.
   */
  _sendDOMRequest: function(msgName, args) {
    let id = 'req_' + this._domRequestCounter++;
    let req = Services.DOMRequest.createRequest(this._window);
    if (this._sendAsyncMsg(msgName, {id: id, args: args})) {
      this._pendingDOMRequests[id] = req;
    } else {
      Services.DOMRequest.fireErrorAsync(req, "fail");
    }
    return req;
  },

  /**
   * Called when the child process finishes handling a DOMRequest.  data.json
   * must have the fields [id, successRv], if the DOMRequest was successful, or
   * [id, errorMsg], if the request was not successful.
   *
   * The fields have the following meanings:
   *
   *  - id:        the ID of the DOM request (see _sendDOMRequest)
   *  - successRv: the request's return value, if the request succeeded
   *  - errorMsg:  the message to pass to DOMRequest.fireError(), if the request
   *               failed.
   *
   */
  _gotDOMRequestResult: function(data) {
    let req = this._pendingDOMRequests[data.json.id];
    delete this._pendingDOMRequests[data.json.id];

    if ('successRv' in data.json) {
      debug("Successful gotDOMRequestResult.");
      Services.DOMRequest.fireSuccess(req, data.json.successRv);
    }
    else {
      debug("Got error in gotDOMRequestResult.");
      Services.DOMRequest.fireErrorAsync(req, data.json.errorMsg);
    }
  },

  _setVisible: function(visible) {
    this._sendAsyncMsg('set-visible', {visible: visible});
  },

  _sendMouseEvent: function(type, x, y, button, clickCount, modifiers) {
    this._sendAsyncMsg("send-mouse-event", {
      "type": type,
      "x": x,
      "y": y,
      "button": button,
      "clickCount": clickCount,
      "modifiers": modifiers
    });
  },

  _sendTouchEvent: function(type, identifiers, touchesX, touchesY,
                            radiisX, radiisY, rotationAngles, forces,
                            count, modifiers) {
    this._sendAsyncMsg("send-touch-event", {
      "type": type,
      "identifiers": identifiers,
      "touchesX": touchesX,
      "touchesY": touchesY,
      "radiisX": radiisX,
      "radiisY": radiisY,
      "rotationAngles": rotationAngles,
      "forces": forces,
      "count": count,
      "modifiers": modifiers
    });
  },

  _goBack: function() {
    this._sendAsyncMsg('go-back');
  },

  _goForward: function() {
    this._sendAsyncMsg('go-forward');
  },

  _reload: function(hardReload) {
    this._sendAsyncMsg('reload', {hardReload: hardReload});
  },

  _stop: function() {
    this._sendAsyncMsg('stop');
  },

  _purgeHistory: function() {
    return this._sendDOMRequest('purge-history');
  },

  _getScreenshot: function(_width, _height) {
    let width = parseInt(_width);
    let height = parseInt(_height);
    if (isNaN(width) || isNaN(height) || width < 0 || height < 0) {
      throw Components.Exception("Invalid argument",
                                 Cr.NS_ERROR_INVALID_ARG);
    }

    return this._sendDOMRequest('get-screenshot',
                                {width: width, height: height});
  },

  _recvNextPaint: function(data) {
    let listeners = this._nextPaintListeners;
    this._nextPaintListeners = [];
    for (let listener of listeners) {
      try {
        listener();
      } catch (e) {
        // If a listener throws we'll continue.
      }
    }
  },

  _addNextPaintListener: function(listener) {
    if (typeof listener != 'function')
      throw Components.Exception("Invalid argument", Cr.NS_ERROR_INVALID_ARG);

    if (this._nextPaintListeners.push(listener) == 1)
      this._sendAsyncMsg('activate-next-paint-listener');
  },

  _removeNextPaintListener: function(listener) {
    if (typeof listener != 'function')
      throw Components.Exception("Invalid argument", Cr.NS_ERROR_INVALID_ARG);

    for (let i = this._nextPaintListeners.length - 1; i >= 0; i--) {
      if (this._nextPaintListeners[i] == listener) {
        this._nextPaintListeners.splice(i, 1);
        break;
      }
    }

    if (this._nextPaintListeners.length == 0)
      this._sendAsyncMsg('deactivate-next-paint-listener');
  },

  _fireKeyEvent: function(data) {
    let evt = this._window.document.createEvent("KeyboardEvent");
    evt.initKeyEvent(data.json.type, true, true, this._window,
                     false, false, false, false, // modifiers
                     data.json.keyCode,
                     data.json.charCode);

    this._frameElement.dispatchEvent(evt);
  },

  /**
   * Called when the visibility of the window which owns this iframe changes.
   */
  _ownerVisibilityChange: function() {
    this._sendAsyncMsg('owner-visibility-change',
                       {visible: !this._window.document.hidden});
  },

  _exitFullscreen: function() {
    this._windowUtils.exitFullscreen();
  },

  _remoteFullscreenOriginChange: function(data) {
    let origin = data.json;
    this._windowUtils.remoteFrameFullscreenChanged(this._frameElement, origin);
  },

  _remoteFrameFullscreenReverted: function(data) {
    this._windowUtils.remoteFrameFullscreenReverted();
  },

  _fireFatalError: function() {
    let evt = this._createEvent('error', {type: 'fatal'},
                                /* cancelable = */ false);
    this._frameElement.dispatchEvent(evt);
  },

  observe: function(subject, topic, data) {
    switch(topic) {
    case 'oop-frameloader-crashed':
      if (this._isAlive() && subject == this._frameLoader) {
        this._fireFatalError();
      }
      break;
    case 'ask-children-to-exit-fullscreen':
      if (this._isAlive() &&
          this._frameElement.ownerDocument == subject &&
          this._hasRemoteFrame) {
        this._sendAsyncMsg('exit-fullscreen');
      }
      break;
    default:
      debug('Unknown topic: ' + topic);
      break;
    };
  },
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([BrowserElementParentFactory]);
