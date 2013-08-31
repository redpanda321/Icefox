/* -*- Mode: javascript; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";
/**
 * Toolkit glue for the remote debugging protocol, loaded into the
 * debugging global.
 */

const Ci = Components.interfaces;
const Cc = Components.classes;
const CC = Components.Constructor;
const Cu = Components.utils;
const Cr = Components.results;
const DBG_STRINGS_URI = "chrome://global/locale/devtools/debugger.properties";

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");
let wantLogging = Services.prefs.getBoolPref("devtools.debugger.log");

Cu.import("resource://gre/modules/jsdebugger.jsm");
addDebuggerToGlobal(this);

Cu.import("resource://gre/modules/devtools/_Promise.jsm");

function dumpn(str) {
  if (wantLogging) {
    dump("DBG-SERVER: " + str + "\n");
  }
}

function dbg_assert(cond, e) {
  if (!cond) {
    return e;
  }
}

/* Turn the error e into a string, without fail. */
function safeErrorString(aError) {
  try {
    var s = aError.toString();
    if (typeof s === "string")
      return s;
  } catch (ee) { }

  return "<failed trying to find error description>";
}

loadSubScript.call(this, "chrome://global/content/devtools/dbg-transport.js");

// XPCOM constructors
const ServerSocket = CC("@mozilla.org/network/server-socket;1",
                        "nsIServerSocket",
                        "init");

/***
 * Public API
 */
var DebuggerServer = {
  _listener: null,
  _transportInitialized: false,
  xpcInspector: null,
  // Number of currently open TCP connections.
  _socketConnections: 0,
  // Map of global actor names to actor constructors provided by extensions.
  globalActorFactories: null,
  // Map of tab actor names to actor constructors provided by extensions.
  tabActorFactories: null,

  LONG_STRING_LENGTH: 10000,
  LONG_STRING_INITIAL_LENGTH: 1000,

  /**
   * A handler function that prompts the user to accept or decline the incoming
   * connection.
   */
  _allowConnection: null,

  /**
   * Prompt the user to accept or decline the incoming connection. This is the
   * default implementation that products embedding the debugger server may
   * choose to override.
   *
   * @return true if the connection should be permitted, false otherwise
   */
  _defaultAllowConnection: function DS__defaultAllowConnection() {
    let title = L10N.getStr("remoteIncomingPromptTitle");
    let msg = L10N.getStr("remoteIncomingPromptMessage");
    let disableButton = L10N.getStr("remoteIncomingPromptDisable");
    let prompt = Services.prompt;
    let flags = prompt.BUTTON_POS_0 * prompt.BUTTON_TITLE_OK +
                prompt.BUTTON_POS_1 * prompt.BUTTON_TITLE_CANCEL +
                prompt.BUTTON_POS_2 * prompt.BUTTON_TITLE_IS_STRING +
                prompt.BUTTON_POS_1_DEFAULT;
    let result = prompt.confirmEx(null, title, msg, flags, null, null,
                                  disableButton, null, { value: false });
    if (result == 0) {
      return true;
    }
    if (result == 2) {
      DebuggerServer.closeListener(true);
      Services.prefs.setBoolPref("devtools.debugger.remote-enabled", false);
    }
    return false;
  },

  /**
   * Initialize the debugger server.
   *
   * @param function aAllowConnectionCallback
   *        The embedder-provider callback, that decides whether an incoming
   *        remote protocol conection should be allowed or refused.
   */
  init: function DS_init(aAllowConnectionCallback) {
    if (this.initialized) {
      return;
    }

    this.xpcInspector = Cc["@mozilla.org/jsinspector;1"].getService(Ci.nsIJSInspector);
    this.initTransport(aAllowConnectionCallback);
    this.addActors("chrome://global/content/devtools/dbg-script-actors.js");

    this.globalActorFactories = {};
    this.tabActorFactories = {};
  },

  /**
   * Initialize the debugger server's transport variables.  This can be
   * in place of init() for cases where the jsdebugger isn't needed.
   *
   * @param function aAllowConnectionCallback
   *        The embedder-provider callback, that decides whether an incoming
   *        remote protocol conection should be allowed or refused.
   */
  initTransport: function DS_initTransport(aAllowConnectionCallback) {
    if (this._transportInitialized) {
      return;
    }

    this._connections = {};
    this._nextConnID = 0;
    this._transportInitialized = true;
    this._allowConnection = aAllowConnectionCallback ?
                            aAllowConnectionCallback :
                            this._defaultAllowConnection;
  },

  get initialized() { return !!this.globalActorFactories; },

  /**
   * Performs cleanup tasks before shutting down the debugger server, if no
   * connections are currently open. Such tasks include clearing any actor
   * constructors added at runtime. This method should be called whenever a
   * debugger server is no longer useful, to avoid memory leaks. After this
   * method returns, the debugger server must be initialized again before use.
   */
  destroy: function DS_destroy() {
    if (Object.keys(this._connections).length == 0) {
      this.closeListener();
      delete this.globalActorFactories;
      delete this.tabActorFactories;
      delete this._allowConnection;
      this._transportInitialized = false;
      dumpn("Debugger server is shut down.");
    }
  },

  /**
   * Load a subscript into the debugging global.
   *
   * @param aURL string A url that will be loaded as a subscript into the
   *        debugging global.  The user must load at least one script
   *        that implements a createRootActor() function to create the
   *        server's root actor.
   */
  addActors: function DS_addActors(aURL) {
    loadSubScript.call(this, aURL);
  },

  /**
   * Install Firefox-specific actors.
   */
  addBrowserActors: function DS_addBrowserActors() {
    this.addActors("chrome://global/content/devtools/dbg-browser-actors.js");
    this.addActors("chrome://global/content/devtools/dbg-webconsole-actors.js");
    this.addTabActor(this.WebConsoleActor, "consoleActor");
    this.addGlobalActor(this.WebConsoleActor, "consoleActor");
    if ("nsIProfiler" in Ci)
      this.addActors("chrome://global/content/devtools/dbg-profiler-actors.js");
  },

  /**
   * Listens on the given port for remote debugger connections.
   *
   * @param aPort int
   *        The port to listen on.
   */
  openListener: function DS_openListener(aPort) {
    if (!Services.prefs.getBoolPref("devtools.debugger.remote-enabled")) {
      return false;
    }
    this._checkInit();

    // Return early if the server is already listening.
    if (this._listener) {
      return true;
    }

    let localOnly = false;
    // A preference setting can force binding on the loopback interface.
    if (Services.prefs.getBoolPref("devtools.debugger.force-local")) {
      localOnly = true;
    }

    try {
      let socket = new ServerSocket(aPort, localOnly, 4);
      socket.asyncListen(this);
      this._listener = socket;
    } catch (e) {
      dumpn("Could not start debugging listener on port " + aPort + ": " + e);
      throw Cr.NS_ERROR_NOT_AVAILABLE;
    }
    this._socketConnections++;

    return true;
  },

  /**
   * Close a previously-opened TCP listener.
   *
   * @param aForce boolean [optional]
   *        If set to true, then the socket will be closed, regardless of the
   *        number of open connections.
   */
  closeListener: function DS_closeListener(aForce) {
    if (!this._listener || this._socketConnections == 0) {
      return false;
    }

    // Only close the listener when the last connection is closed, or if the
    // aForce flag is passed.
    if (--this._socketConnections == 0 || aForce) {
      this._listener.close();
      this._listener = null;
      this._socketConnections = 0;
    }

    return true;
  },

  /**
   * Creates a new connection to the local debugger speaking over a fake
   * transport. This connection results in straightforward calls to the onPacket
   * handlers of each side.
   *
   * @returns a client-side DebuggerTransport for communicating with
   *          the newly-created connection.
   */
  connectPipe: function DS_connectPipe() {
    this._checkInit();

    let serverTransport = new LocalDebuggerTransport;
    let clientTransport = new LocalDebuggerTransport(serverTransport);
    serverTransport.other = clientTransport;
    this._onConnection(serverTransport);

    return clientTransport;
  },


  // nsIServerSocketListener implementation

  onSocketAccepted: function DS_onSocketAccepted(aSocket, aTransport) {
    if (!this._allowConnection()) {
      return;
    }
    dumpn("New debugging connection on " + aTransport.host + ":" + aTransport.port);

    try {
      let input = aTransport.openInputStream(0, 0, 0);
      let output = aTransport.openOutputStream(0, 0, 0);
      let transport = new DebuggerTransport(input, output);
      DebuggerServer._onConnection(transport);
    } catch (e) {
      dumpn("Couldn't initialize connection: " + e + " - " + e.stack);
    }
  },

  onStopListening: function DS_onStopListening(aSocket, status) {
    dumpn("onStopListening, status: " + status);
  },

  /**
   * Raises an exception if the server has not been properly initialized.
   */
  _checkInit: function DS_checkInit() {
    if (!this._transportInitialized) {
      throw "DebuggerServer has not been initialized.";
    }

    if (!this.createRootActor) {
      throw "Use DebuggerServer.addActors() to add a root actor implementation.";
    }
  },

  /**
   * Create a new debugger connection for the given transport.  Called
   * after connectPipe() or after an incoming socket connection.
   */
  _onConnection: function DS_onConnection(aTransport) {
    let connID = "conn" + this._nextConnID++ + '.';
    let conn = new DebuggerServerConnection(connID, aTransport);
    this._connections[connID] = conn;

    // Create a root actor for the connection and send the hello packet.
    conn.rootActor = this.createRootActor(conn);
    conn.addActor(conn.rootActor);
    aTransport.send(conn.rootActor.sayHello());
    aTransport.ready();
  },

  /**
   * Remove the connection from the debugging server.
   */
  _connectionClosed: function DS_connectionClosed(aConnection) {
    delete this._connections[aConnection.prefix];
  },

  // DebuggerServer extension API.

  /**
   * Registers handlers for new tab-scoped request types defined dynamically.
   * This is used for example by add-ons to augment the functionality of the tab
   * actor. Note that the name or actorPrefix of the request type is not allowed
   * to clash with existing protocol packet properties, like 'title', 'url' or
   * 'actor', since that would break the protocol.
   *
   * @param aFunction function
   *        The constructor function for this request type.
   * @param aName string [optional]
   *        The name of the new request type. If this is not present, the
   *        actorPrefix property of the constructor prototype is used.
   */
  addTabActor: function DS_addTabActor(aFunction, aName) {
    let name = aName ? aName : aFunction.prototype.actorPrefix;
    if (["title", "url", "actor"].indexOf(name) != -1) {
      throw Error(name + " is not allowed");
    }
    if (DebuggerServer.tabActorFactories.hasOwnProperty(name)) {
      throw Error(name + " already exists");
    }
    DebuggerServer.tabActorFactories[name] = aFunction;
  },

  /**
   * Unregisters the handler for the specified tab-scoped request type.
   * This may be used for example by add-ons when shutting down or upgrading.
   *
   * @param aFunction function
   *        The constructor function for this request type.
   */
  removeTabActor: function DS_removeTabActor(aFunction) {
    for (let name in DebuggerServer.tabActorFactories) {
      let handler = DebuggerServer.tabActorFactories[name];
      if (handler.name == aFunction.name) {
        delete DebuggerServer.tabActorFactories[name];
      }
    }
  },

  /**
   * Registers handlers for new browser-scoped request types defined
   * dynamically. This is used for example by add-ons to augment the
   * functionality of the root actor. Note that the name or actorPrefix of the
   * request type is not allowed to clash with existing protocol packet
   * properties, like 'from', 'tabs' or 'selected', since that would break the
   * protocol.
   *
   * @param aFunction function
   *        The constructor function for this request type.
   * @param aName string [optional]
   *        The name of the new request type. If this is not present, the
   *        actorPrefix property of the constructor prototype is used.
   */
  addGlobalActor: function DS_addGlobalActor(aFunction, aName) {
    let name = aName ? aName : aFunction.prototype.actorPrefix;
    if (["from", "tabs", "selected"].indexOf(name) != -1) {
      throw Error(name + " is not allowed");
    }
    if (DebuggerServer.globalActorFactories.hasOwnProperty(name)) {
      throw Error(name + " already exists");
    }
    DebuggerServer.globalActorFactories[name] = aFunction;
  },

  /**
   * Unregisters the handler for the specified browser-scoped request type.
   * This may be used for example by add-ons when shutting down or upgrading.
   *
   * @param aFunction function
   *        The constructor function for this request type.
   */
  removeGlobalActor: function DS_removeGlobalActor(aFunction) {
    for (let name in DebuggerServer.globalActorFactories) {
      let handler = DebuggerServer.globalActorFactories[name];
      if (handler.name == aFunction.name) {
        delete DebuggerServer.globalActorFactories[name];
      }
    }
  }
};


/**
 * Construct an ActorPool.
 *
 * ActorPools are actorID -> actor mapping and storage.  These are
 * used to accumulate and quickly dispose of groups of actors that
 * share a lifetime.
 */
function ActorPool(aConnection)
{
  this.conn = aConnection;
  this._cleanups = {};
  this._actors = {};
}

ActorPool.prototype = {
  /**
   * Add an actor to the actor pool.  If the actor doesn't have an ID,
   * allocate one from the connection.
   *
   * @param aActor object
   *        The actor implementation.  If the object has a
   *        'disconnected' property, it will be called when the actor
   *        pool is cleaned up.
   */
  addActor: function AP_addActor(aActor) {
    aActor.conn = this.conn;
    if (!aActor.actorID) {
      let prefix = aActor.actorPrefix;
      if (typeof aActor == "function") {
        prefix = aActor.prototype.actorPrefix;
      }
      aActor.actorID = this.conn.allocID(prefix || undefined);
    }

    if (aActor.registeredPool) {
      aActor.registeredPool.removeActor(aActor);
    }
    aActor.registeredPool = this;

    this._actors[aActor.actorID] = aActor;
    if (aActor.disconnect) {
      this._cleanups[aActor.actorID] = aActor;
    }
  },

  get: function AP_get(aActorID) {
    return this._actors[aActorID];
  },

  has: function AP_has(aActorID) {
    return aActorID in this._actors;
  },

  /**
   * Returns true if the pool is empty.
   */
  isEmpty: function AP_isEmpty() {
    return Object.keys(this._actors).length == 0;
  },

  /**
   * Remove an actor from the actor pool.
   */
  removeActor: function AP_remove(aActor) {
    delete this._actors[aActor.actorID];
    delete this._cleanups[aActor.actorID];
  },

  /**
   * Run all cleanups previously registered with addCleanup.
   */
  cleanup: function AP_cleanup() {
    for each (let actor in this._cleanups) {
      actor.disconnect();
    }
    this._cleanups = {};
  }
}

/**
 * Creates a DebuggerServerConnection.
 *
 * Represents a connection to this debugging global from a client.
 * Manages a set of actors and actor pools, allocates actor ids, and
 * handles incoming requests.
 *
 * @param aPrefix string
 *        All actor IDs created by this connection should be prefixed
 *        with aPrefix.
 * @param aTransport transport
 *        Packet transport for the debugging protocol.
 */
function DebuggerServerConnection(aPrefix, aTransport)
{
  this._prefix = aPrefix;
  this._transport = aTransport;
  this._transport.hooks = this;
  this._nextID = 1;

  this._actorPool = new ActorPool(this);
  this._extraPools = [];
}

DebuggerServerConnection.prototype = {
  _prefix: null,
  get prefix() { return this._prefix },

  _transport: null,
  get transport() { return this._transport },

  send: function DSC_send(aPacket) {
    this.transport.send(aPacket);
  },

  allocID: function DSC_allocID(aPrefix) {
    return this.prefix + (aPrefix || '') + this._nextID++;
  },

  /**
   * Add a map of actor IDs to the connection.
   */
  addActorPool: function DSC_addActorPool(aActorPool) {
    this._extraPools.push(aActorPool);
  },

  /**
   * Remove a previously-added pool of actors to the connection.
   *
   * @param ActorPool aActorPool
   *        The ActorPool instance you want to remove.
   * @param boolean aCleanup
   *        True if you want to disconnect each actor from the pool, false
   *        otherwise.
   */
  removeActorPool: function DSC_removeActorPool(aActorPool, aCleanup) {
    let index = this._extraPools.lastIndexOf(aActorPool);
    if (index > -1) {
      let pool = this._extraPools.splice(index, 1);
      if (aCleanup) {
        pool.map(function(p) { p.cleanup(); });
      }
    }
  },

  /**
   * Add an actor to the default actor pool for this connection.
   */
  addActor: function DSC_addActor(aActor) {
    this._actorPool.addActor(aActor);
  },

  /**
   * Remove an actor to the default actor pool for this connection.
   */
  removeActor: function DSC_removeActor(aActor) {
    this._actorPool.removeActor(aActor);
  },

  /**
   * Add a cleanup to the default actor pool for this connection.
   */
  addCleanup: function DSC_addCleanup(aCleanup) {
    this._actorPool.addCleanup(aCleanup);
  },

  /**
   * Look up an actor implementation for an actorID.  Will search
   * all the actor pools registered with the connection.
   *
   * @param aActorID string
   *        Actor ID to look up.
   */
  getActor: function DSC_getActor(aActorID) {
    if (this._actorPool.has(aActorID)) {
      return this._actorPool.get(aActorID);
    }

    for each (let pool in this._extraPools) {
      if (pool.has(aActorID)) {
        return pool.get(aActorID);
      }
    }

    if (aActorID === "root") {
      return this.rootActor;
    }

    return null;
  },

  // Transport hooks.

  /**
   * Called by DebuggerTransport to dispatch incoming packets as appropriate.
   *
   * @param aPacket object
   *        The incoming packet.
   */
  onPacket: function DSC_onPacket(aPacket) {
    let actor = this.getActor(aPacket.to);
    if (!actor) {
      this.transport.send({ from: aPacket.to ? aPacket.to : "root",
                            error: "noSuchActor" });
      return;
    }

    // Dyamically-loaded actors have to be created lazily.
    if (typeof actor == "function") {
      let instance;
      try {
        instance = new actor();
      } catch (e) {
        Cu.reportError(e);
        this.transport.send({
          error: "unknownError",
          message: ("error occurred while creating actor '" + actor.name +
                    "': " + safeErrorString(e))
        });
      }
      instance.parentID = actor.parentID;
      // We want the newly-constructed actor to completely replace the factory
      // actor. Reusing the existing actor ID will make sure ActorPool.addActor
      // does the right thing.
      instance.actorID = actor.actorID;
      actor.registeredPool.addActor(instance);
      actor = instance;
    }

    var ret = null;

    // Dispatch the request to the actor.
    if (actor.requestTypes && actor.requestTypes[aPacket.type]) {
      try {
        ret = actor.requestTypes[aPacket.type].bind(actor)(aPacket);
      } catch(e) {
        Cu.reportError(e);
        ret = { error: "unknownError",
                message: ("error occurred while processing '" + aPacket.type +
                          "' request: " + safeErrorString(e)) };
      }
    } else {
      ret = { error: "unrecognizedPacketType",
              message: ('Actor "' + actor.actorID +
                        '" does not recognize the packet type "' +
                        aPacket.type + '"') };
    }

    if (!ret) {
      // XXX: The actor wasn't ready to reply yet, don't process new
      // requests until it does.
      return;
    }

    if (!ret.from) {
      ret.from = aPacket.to;
    }

    this.transport.send(ret);
  },

  /**
   * Called by DebuggerTransport when the underlying stream is closed.
   *
   * @param aStatus nsresult
   *        The status code that corresponds to the reason for closing
   *        the stream.
   */
  onClosed: function DSC_onClosed(aStatus) {
    dumpn("Cleaning up connection.");

    this._actorPool.cleanup();
    this._actorPool = null;
    this._extraPools.map(function(p) { p.cleanup(); });
    this._extraPools = null;

    DebuggerServer._connectionClosed(this);
  },

  /*
   * Debugging helper for inspecting the state of the actor pools.
   */
  _dumpPools: function DSC_dumpPools() {
    dumpn("/-------------------- dumping pools:");
    if (this._actorPool) {
      dumpn("--------------------- actorPool actors: " +
            uneval(Object.keys(this._actorPool._actors)));
    }
    for each (let pool in this._extraPools)
      dumpn("--------------------- extraPool actors: " +
            uneval(Object.keys(pool._actors)));
  },

  /*
   * Debugging helper for inspecting the state of an actor pool.
   */
  _dumpPool: function DSC_dumpPools(aPool) {
    dumpn("/-------------------- dumping pool:");
    dumpn("--------------------- actorPool actors: " +
          uneval(Object.keys(aPool._actors)));
  }
};

/**
 * Localization convenience methods.
 */
let L10N = {

  /**
   * L10N shortcut function.
   *
   * @param string aName
   * @return string
   */
  getStr: function L10N_getStr(aName) {
    return this.stringBundle.GetStringFromName(aName);
  }
};

XPCOMUtils.defineLazyGetter(L10N, "stringBundle", function() {
  return Services.strings.createBundle(DBG_STRINGS_URI);
});
