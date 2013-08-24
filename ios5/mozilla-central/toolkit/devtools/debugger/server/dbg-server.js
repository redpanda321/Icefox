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

Cu.import("resource://gre/modules/Services.jsm");
let wantLogging = Services.prefs.getBoolPref("devtools.debugger.log");

Cu.import("resource://gre/modules/jsdebugger.jsm");
addDebuggerToGlobal(this);

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
  _allowConnection: null,

  /**
   * Initialize the debugger server.
   *
   * @param function aAllowConnectionCallback
   *        The embedder-provider callback, that decides whether an incoming
   *        remote protocol conection should be allowed or refused.
   */
  init: function DH_init(aAllowConnectionCallback) {
    if (this.initialized) {
      return;
    }

    this.xpcInspector = Cc["@mozilla.org/jsinspector;1"].getService(Ci.nsIJSInspector);
    this.initTransport(aAllowConnectionCallback);
    this.addActors("chrome://global/content/devtools/dbg-script-actors.js");
  },

  /**
   * Initialize the debugger server's transport variables.  This can be
   * in place of init() for cases where the jsdebugger isn't needed.
   *
   * @param function aAllowConnectionCallback
   *        The embedder-provider callback, that decides whether an incoming
   *        remote protocol conection should be allowed or refused.
   */
  initTransport: function DH_initTransport(aAllowConnectionCallback) {
    if (this._transportInitialized) {
      return;
    }

    this._connections = {};
    this._nextConnID = 0;
    this._transportInitialized = true;
    this._allowConnection = aAllowConnectionCallback;
  },

  get initialized() { return !!this.xpcInspector; },

  /**
   * Load a subscript into the debugging global.
   *
   * @param aURL string A url that will be loaded as a subscript into the
   *        debugging global.  The user must load at least one script
   *        that implements a createRootActor() function to create the
   *        server's root actor.
   */
  addActors: function DH_addActors(aURL) {
    loadSubScript.call(this, aURL);
  },

  /**
   * Install Firefox-specific actors.
   */
  addBrowserActors: function DH_addBrowserActors() {
    this.addActors("chrome://global/content/devtools/dbg-browser-actors.js");
  },

  /**
   * Listens on the given port for remote debugger connections.
   *
   * @param aPort int
   *        The port to listen on.
   */
  openListener: function DH_openListener(aPort) {
    if (!Services.prefs.getBoolPref("devtools.debugger.remote-enabled")) {
      return false;
    }
    this._checkInit();

    if (this._listener) {
      throw "Debugging listener already open.";
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

    return true;
  },

  /**
   * Close a previously-opened TCP listener.
   */
  closeListener: function DH_closeListener() {
    this._checkInit();

    if (!this._listener) {
      return false;
    }

    this._listener.close();
    this._listener = null;

    return true;
  },

  /**
   * Creates a new connection to the local debugger speaking over an
   * nsIPipe.
   *
   * @returns a client-side DebuggerTransport for communicating with
   *          the newly-created connection.
   */
  connectPipe: function DH_connectPipe() {
    this._checkInit();

    let toServer = Cc["@mozilla.org/pipe;1"].createInstance(Ci.nsIPipe);
    toServer.init(true, true, 0, 0, null);
    let toClient = Cc["@mozilla.org/pipe;1"].createInstance(Ci.nsIPipe);
    toClient.init(true, true, 0, 0, null);

    let serverTransport = new DebuggerTransport(toServer.inputStream,
                                                toClient.outputStream);
    this._onConnection(serverTransport);

    return new DebuggerTransport(toClient.inputStream, toServer.outputStream);
  },


  // nsIServerSocketListener implementation

  onSocketAccepted: function DH_onSocketAccepted(aSocket, aTransport) {
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

  onStopListening: function DH_onStopListening() { },

  /**
   * Raises an exception if the server has not been properly initialized.
   */
  _checkInit: function DH_checkInit() {
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
  _onConnection: function DH_onConnection(aTransport) {
    if (!this._allowConnection()) {
      return;
    }
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
  _connectionClosed: function DH_connectionClosed(aConnection) {
    delete this._connections[aConnection.prefix];
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
      aActor.actorID = this.conn.allocID(aActor.actorPrefix || undefined);
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
   * Remove an actor from the actor pool.
   */
  removeActor: function AP_remove(aActorID) {
    delete this._actors[aActorID];
    delete this._cleanups[aActorID];
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
   */
  removeActorPool: function DSC_removeActorPool(aActorPool) {
    let index = this._extraPools.splice(this._extraPools.lastIndexOf(aActorPool), 1);
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
  }
};
