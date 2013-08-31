/* -*- Mode: JavaScript; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/*
 * This is an implementation of a "Shared Worker" using an iframe in the
 * hidden DOM window.  A subset of new APIs are introduced to the window
 * by cloning methods from the worker's JS origin.
 */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/MessagePortBase.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "SocialService",
  "resource://gre/modules/SocialService.jsm");

this.EXPORTED_SYMBOLS = ["getFrameWorkerHandle"];

var workerCache = {}; // keyed by URL.
var _nextPortId = 1;

// Retrieves a reference to a WorkerHandle associated with a FrameWorker and a
// new ClientPort.
this.getFrameWorkerHandle =
 function getFrameWorkerHandle(url, clientWindow, name, origin) {
  // first create the client port we are going to use.  Later we will
  // message the worker to create the worker port.
  let portid = _nextPortId++;
  let clientPort = new ClientPort(portid, clientWindow);

  let existingWorker = workerCache[url];
  if (!existingWorker) {
    // setup the worker and add this connection to the pending queue
    let worker = new FrameWorker(url, name, origin);
    worker.pendingPorts.push(clientPort);
    existingWorker = workerCache[url] = worker;
  } else {
    // already have a worker - either queue or make the connection.
    if (existingWorker.loaded) {
      try {
        clientPort._createWorkerAndEntangle(existingWorker);
      }
      catch (ex) {
        Cu.reportError("FrameWorker: Failed to connect a port: " + e + "\n" + e.stack);
      }
    } else {
      existingWorker.pendingPorts.push(clientPort);
    }
  }

  // return the pseudo worker object.
  return new WorkerHandle(clientPort, existingWorker);
};

/**
 * FrameWorker
 *
 * A FrameWorker is an iframe that is attached to the hiddenWindow,
 * which contains a pair of MessagePorts.  It is constructed with the
 * URL of some JavaScript that will be run in the context of the window;
 * the script does not have a full DOM but is instead run in a sandbox
 * that has a select set of methods cloned from the URL's domain.
 */
function FrameWorker(url, name, origin) {
  this.url = url;
  this.name = name || url;
  this.ports = {};
  this.pendingPorts = [];
  this.loaded = false;
  this.reloading = false;
  this.origin = origin;

  this.frame = makeHiddenFrame();
  this.load();
}

FrameWorker.prototype = {
  load: function FrameWorker_loadWorker() {
    var self = this;
    Services.obs.addObserver(function injectController(doc, topic, data) {
      if (!doc.defaultView || doc.defaultView != self.frame.contentWindow) {
        return;
      }
      Services.obs.removeObserver(injectController, "document-element-inserted");
      try {
        self.createSandbox();
      } catch (e) {
        Cu.reportError("FrameWorker: failed to create sandbox for " + url + ". " + e);
      }
    }, "document-element-inserted", false);

    this.frame.setAttribute("src", this.url);
  },

  reload: function FrameWorker_reloadWorker() {
    // push all the ports into pending ports, they will be re-entangled
    // during the call to createSandbox after the document is reloaded
    for (let [portid, port] in Iterator(this.ports)) {
      port._window = null;
      this.pendingPorts.push(port);
    }
    this.ports = {};
    // Mark the provider as unloaded now, so that any new ports created after
    // this point but before the unload has fired are properly queued up.
    this.loaded = false;
    // reset the iframe to about:blank - this will fire the unload event
    // but not remove the iframe from the DOM.  Our unload handler will
    // see this.reloading is true and reload for us.
    this.reloading = true;
    this.frame.setAttribute("src", "about:blank");
  },

  createSandbox: function createSandbox() {
    let workerWindow = this.frame.contentWindow;
    let sandbox = new Cu.Sandbox(workerWindow);

    // copy the window apis onto the sandbox namespace only functions or
    // objects that are naturally a part of an iframe, I'm assuming they are
    // safe to import this way
    let workerAPI = ['WebSocket', 'localStorage', 'atob', 'btoa',
                     'clearInterval', 'clearTimeout', 'dump',
                     'setInterval', 'setTimeout', 'XMLHttpRequest',
                     'MozBlobBuilder', 'FileReader', 'Blob',
                     'location'];
    workerAPI.forEach(function(fn) {
      try {
        // Bug 798660 - XHR and WebSocket have issues in a sandbox and need
        // to be unwrapped to work
        if (fn == "XMLHttpRequest" || fn == "WebSocket")
          sandbox[fn] = XPCNativeWrapper.unwrap(workerWindow)[fn];
        else
          sandbox[fn] = workerWindow[fn];
      }
      catch(e) {
        Cu.reportError("FrameWorker: failed to import API "+fn+"\n"+e+"\n");
      }
    });
    // the "navigator" object in a worker is a subset of the full navigator;
    // specifically, just the interfaces 'NavigatorID' and 'NavigatorOnLine'
    let navigator = {
      __exposedProps__: {
        "appName": "r",
        "appVersion": "r",
        "platform": "r",
        "userAgent": "r",
        "onLine": "r"
      },
      // interface NavigatorID
      appName: workerWindow.navigator.appName,
      appVersion: workerWindow.navigator.appVersion,
      platform: workerWindow.navigator.platform,
      userAgent: workerWindow.navigator.userAgent,
      // interface NavigatorOnLine
      get onLine() workerWindow.navigator.onLine
    };
    sandbox.navigator = navigator;

    // Our importScripts function needs to 'eval' the script code from inside
    // a function, but using eval() directly means functions in the script
    // don't end up in the global scope.
    sandbox._evalInSandbox = function(s) {
      Cu.evalInSandbox(s, sandbox);
    };

    // and we delegate ononline and onoffline events to the worker.
    // See http://www.whatwg.org/specs/web-apps/current-work/multipage/workers.html#workerglobalscope
    workerWindow.addEventListener('offline', function fw_onoffline(event) {
      Cu.evalInSandbox("onoffline();", sandbox);
    }, false);
    workerWindow.addEventListener('online', function fw_ononline(event) {
      Cu.evalInSandbox("ononline();", sandbox);
    }, false);

    sandbox._postMessage = function fw_postMessage(d, o) {
      workerWindow.postMessage(d, o)
    };
    sandbox._addEventListener = function fw_addEventListener(t, l, c) {
      workerWindow.addEventListener(t, l, c)
    };

    // Note we don't need to stash |sandbox| in |this| as the unload handler
    // has a reference in its closure, so it can't die until that handler is
    // removed - at which time we've explicitly killed it anyway.
    let worker = this;

    workerWindow.addEventListener("DOMContentLoaded", function loadListener() {
      workerWindow.removeEventListener("DOMContentLoaded", loadListener);

      // no script, error out now rather than creating ports, etc
      let scriptText = workerWindow.document.body.textContent.trim();
      if (!scriptText) {
        Cu.reportError("FrameWorker: Empty worker script received");
        notifyWorkerError(worker);
        return;
      }

      // the iframe has loaded the js file as text - first inject the magic
      // port-handling code into the sandbox.
      try {
        Services.scriptloader.loadSubScript("resource://gre/modules/MessagePortBase.jsm", sandbox);
        Services.scriptloader.loadSubScript("resource://gre/modules/MessagePortWorker.js", sandbox);
      }
      catch (e) {
        Cu.reportError("FrameWorker: Error injecting port code into content side of the worker: " + e + "\n" + e.stack);
        notifyWorkerError(worker);
        return;
      }

      // and wire up the client message handling.
      try {
        initClientMessageHandler(worker, workerWindow);
      }
      catch (e) {
        Cu.reportError("FrameWorker: Error setting up event listener for chrome side of the worker: " + e + "\n" + e.stack);
        notifyWorkerError();
        return;
      }

      // Now get the worker js code and eval it into the sandbox
      try {
        Cu.evalInSandbox(scriptText, sandbox, "1.8", workerWindow.location.href, 1);
      } catch (e) {
        Cu.reportError("FrameWorker: Error evaluating worker script for " + worker.name + ": " + e + "; " +
            (e.lineNumber ? ("Line #" + e.lineNumber) : "") +
            (e.stack ? ("\n" + e.stack) : ""));
        notifyWorkerError(worker);
        return;
      }

      // so finally we are ready to roll - dequeue all the pending connects
      worker.loaded = true;
      for (let port of worker.pendingPorts) {
        try {
          port._createWorkerAndEntangle(worker);
        }
        catch(e) {
          Cu.reportError("FrameWorker: Failed to create worker port: " + e + "\n" + e.stack);
        }
      }
      worker.pendingPorts = [];
    });

    // the 'unload' listener cleans up the worker and the sandbox.  This
    // will be triggered via either our 'terminate' function or by the
    // window unloading as part of shutdown.
    workerWindow.addEventListener("unload", function unloadListener() {
      workerWindow.removeEventListener("unload", unloadListener);
      for (let [portid, port] in Iterator(worker.ports)) {
        try {
          port.close();
        } catch (ex) {
          Cu.reportError("FrameWorker: failed to close port. " + ex);
        }
      }
      // Closing the ports also removed it from this.ports via port-close,
      // but be safe incase one failed to close.  This must remain an array
      // incase we are being reloaded.
      worker.ports = [];
      // The worker window may not have fired a load event yet, so pendingPorts
      // might still have items in it - close them too.
      worker.loaded = false;
      // If the worker is reloading, when we don't actually close the pending
      // ports as they are the ports which need to be re-entangled.
      if (!worker.reloading) {
        for (let port of worker.pendingPorts) {
          try {
            port.close();
          } catch (ex) {
            Cu.reportError("FrameWorker: failed to close pending port. " + ex);
          }
        }
        worker.pendingPorts = [];
      }

      if (sandbox) {
        Cu.nukeSandbox(sandbox);
        sandbox = null;
      }
      if (worker.reloading) {
        Services.tm.mainThread.dispatch(function doReload() {
          worker.reloading = false;
          worker.load();
        }, Ci.nsIThread.DISPATCH_NORMAL);
      }
    });
  },

  terminate: function terminate() {
    if (!(this.url in workerCache)) {
      // terminating an already terminated worker - ignore it
      return;
    }
    // we want to "forget" about this worker now even though the termination
    // may not be complete for a little while...
    delete workerCache[this.url];
    // let pending events get delivered before actually removing the frame,
    // then we perform the actual cleanup in the unload handler.
    Services.tm.mainThread.dispatch(function deleteWorkerFrame() {
      // now nuke the iframe itself and forget everything about this worker.
      this.frame.parentNode.removeChild(this.frame);
    }.bind(this), Ci.nsIThread.DISPATCH_NORMAL);
  }
};

function makeHiddenFrame() {
  let hiddenDoc = Services.appShell.hiddenDOMWindow.document;
  let iframe = hiddenDoc.createElementNS("http://www.w3.org/1999/xhtml", "iframe");
  iframe.setAttribute("mozframetype", "content");
  // allow-same-origin is necessary for localStorage to work in the sandbox.
  iframe.setAttribute("sandbox", "allow-same-origin");

  hiddenDoc.documentElement.appendChild(iframe);

  // Disable some types of content
  let docShell = iframe.contentWindow.QueryInterface(Ci.nsIInterfaceRequestor).getInterface(Ci.nsIDocShell);
  docShell.allowAuth = false;
  docShell.allowPlugins = false;
  docShell.allowImages = false;
  docShell.allowWindowControl = false;
  // TODO: disable media (bug 759964)
  return iframe;
}

// public methods on WorkerHandle should conform to the SharedWorker api
function WorkerHandle(port, worker) {
  this.port = port;
  this._worker = worker;
}
WorkerHandle.prototype = {
  // XXX - workers have no .close() method, but *do* have a .terminate()
  // method which we should implement. However, the worker spec doesn't define
  // a callback to be made in the worker when this happens - it all just dies.
  // TODO: work out a sane impl for 'terminate'.
  terminate: function terminate() {
    this._worker.terminate();
  }
};

// This is the message listener for the *client* (ie, chrome) side of the world.
function initClientMessageHandler(worker, workerWindow) {
  function _messageHandler(event) {
    // We will ignore all messages destined for otherType.
    let data = event.data;
    let portid = data.portId;
    let port;
    if (!data.portFromType || data.portFromType === "client") {
      // this is a message posted by ourself so ignore it.
      return;
    }
    switch (data.portTopic) {
      // No "port-create" here - client ports are created explicitly.
      case "port-connection-error":
        // onconnect failed, we cannot connect the port, the worker has
        // become invalid
        notifyWorkerError(worker);
        break;
      case "port-close":
        // the worker side of the port was closed, so close this side too.
        port = worker.ports[portid];
        if (!port) {
          // port already closed (which will happen when we call port.close()
          // below - the worker side will send us this message but we've
          // already closed it.)
          return;
        }
        delete worker.ports[portid];
        port.close();
        break;

      case "port-message":
        // the client posted a message to this worker port.
        port = worker.ports[portid];
        if (!port) {
          return;
        }
        port._onmessage(data.data);
        break;

      default:
        break;
    }
  }
  // this can probably go once debugged and working correctly!
  function messageHandler(event) {
    try {
      _messageHandler(event);
    } catch (ex) {
      Cu.reportError("FrameWorker: Error handling client port control message: " + ex + "\n" + ex.stack);
    }
  }
  workerWindow.addEventListener('message', messageHandler);
}

/**
 * ClientPort
 *
 * Client side of the entangled ports. The ClientPort is used by both XUL
 * windows and Content windows to communicate with the worker
 *
 * constructor:
 * @param {integer} portid
 * @param {nsiDOMWindow} clientWindow, optional
 */
function ClientPort(portid, clientWindow) {
  this._clientWindow = clientWindow;
  this._window = null;
  // messages posted to the worker before the worker has loaded.
  this._pendingMessagesOutgoing = [];
  AbstractPort.call(this, portid);
}

ClientPort.prototype = {
  __exposedProps__: {
    onmessage: "rw",
    postMessage: "r",
    close: "r",
    toString: "r"
  },
  __proto__: AbstractPort.prototype,
  _portType: "client",

  _JSONParse: function fw_ClientPort_JSONParse(data) {
    if (this._clientWindow) {
      return XPCNativeWrapper.unwrap(this._clientWindow).JSON.parse(data);
    }
    return JSON.parse(data);
  },

  _createWorkerAndEntangle: function fw_ClientPort_createWorkerAndEntangle(worker) {
    this._window = worker.frame.contentWindow;
    worker.ports[this._portid] = this;
    this._postControlMessage("port-create");
    for (let message of this._pendingMessagesOutgoing) {
      this._dopost(message);
    }
    this._pendingMessagesOutgoing = [];
    // The client side of the port might have been closed before it was
    // "entangled" with the worker, in which case we need to disentangle it
    if (this._closed) {
      this._window = null;
      delete worker.ports[this._portid];
    }
  },

  _dopost: function fw_ClientPort_dopost(data) {
    if (!this._window) {
      this._pendingMessagesOutgoing.push(data);
    } else {
      this._window.postMessage(data, "*");
    }
  },

  _onerror: function fw_ClientPort_onerror(err) {
    Cu.reportError("FrameWorker: Port " + this + " handler failed: " + err + "\n" + err.stack);
  },

  close: function fw_ClientPort_close() {
    if (this._closed) {
      return; // already closed.
    }
    // a leaky abstraction due to the worker spec not specifying how the
    // other end of a port knows it is closing.
    this.postMessage({topic: "social.port-closing"});
    AbstractPort.prototype.close.call(this);
    this._window = null;
    this._clientWindow = null;
    // this._pendingMessagesOutgoing should still be drained, as a closed
    // port will still get "entangled" quickly enough to deliver the messages.
  }
}

function notifyWorkerError(worker) {
  // Try to retrieve the worker's associated provider, if it has one, to set its
  // error state.
  SocialService.getProvider(worker.origin, function (provider) {
    if (provider)
      provider.errorState = "frameworker-error";
    Services.obs.notifyObservers(null, "social:frameworker-error", worker.origin);
  });
}
