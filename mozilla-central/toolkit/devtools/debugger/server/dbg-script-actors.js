/* -*- Mode: javascript; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; js-indent-level: 2; -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * JSD2 actors.
 */
/**
 * Creates a ThreadActor.
 *
 * ThreadActors manage a JSInspector object and manage execution/inspection
 * of debuggees.
 *
 * @param aHooks object
 *        An object with preNest and postNest methods for calling when entering
 *        and exiting a nested event loop, addToParentPool and
 *        removeFromParentPool methods for handling the lifetime of actors that
 *        will outlive the thread, like breakpoints.
 * @param aGlobal object [optional]
 *        An optional (for content debugging only) reference to the content
 *        window.
 */
function ThreadActor(aHooks, aGlobal)
{
  this._state = "detached";
  this._frameActors = [];
  this._environmentActors = [];
  this._hooks = {};
  this._hooks = aHooks;
  this.global = aGlobal;

  /**
   * A script cache that maps script URLs to arrays of different Debugger.Script
   * instances that have the same URL. For example, when an inline <script> tag
   * in a web page contains a function declaration, the JS engine creates two
   * Debugger.Script objects, one for the function and one for the script tag
   * as a whole. The two objects will usually have different startLine and/or
   * lineCount properties. For the edge case where two scripts are contained in
   * the same line we need column support.
   *
   * The sparse array that is mapped to each URL serves as an additional mapping
   * from startLine numbers to Debugger.Script objects, facilitating retrieval
   * of the scripts that contain a particular line number. For example, if a
   * cache holds two scripts with the URL http://foo.com/ starting at lines 4
   * and 10, then the corresponding cache will be:
   * this._scripts: {
   *   'http://foo.com/': [,,,,[Debugger.Script],,,,,,[Debugger.Script]]
   * }
   */
  this._scripts = {};

  this.findGlobals = this.globalManager.findGlobals.bind(this);
  this.onNewGlobal = this.globalManager.onNewGlobal.bind(this);
}

/**
 * The breakpoint store must be shared across instances of ThreadActor so that
 * page reloads don't blow away all of our breakpoints.
 */
ThreadActor._breakpointStore = {};

ThreadActor.prototype = {
  actorPrefix: "context",

  get state() { return this._state; },
  get attached() this.state == "attached" ||
                 this.state == "running" ||
                 this.state == "paused",

  get _breakpointStore() { return ThreadActor._breakpointStore; },

  get threadLifetimePool() {
    if (!this._threadLifetimePool) {
      this._threadLifetimePool = new ActorPool(this.conn);
      this.conn.addActorPool(this._threadLifetimePool);
      this._threadLifetimePool.objectActors = new WeakMap();
    }
    return this._threadLifetimePool;
  },

  clearDebuggees: function TA_clearDebuggees() {
    if (this.dbg) {
      this.dbg.removeAllDebuggees();
    }
    this.conn.removeActorPool(this._threadLifetimePool || undefined);
    this._threadLifetimePool = null;
    this._scripts = {};
  },

  /**
   * Add a debuggee global to the Debugger object.
   */
  addDebuggee: function TA_addDebuggee(aGlobal) {
    try {
      this.dbg.addDebuggee(aGlobal);
    } catch (e) {
      // Ignore attempts to add the debugger's compartment as a debuggee.
      dumpn("Ignoring request to add the debugger's compartment as a debuggee");
    }
  },

  /**
   * Initialize the Debugger.
   */
  _initDebugger: function TA__initDebugger() {
    this.dbg = new Debugger();
    this.dbg.uncaughtExceptionHook = this.uncaughtExceptionHook.bind(this);
    this.dbg.onDebuggerStatement = this.onDebuggerStatement.bind(this);
    this.dbg.onNewScript = this.onNewScript.bind(this);
    this.dbg.onNewGlobalObject = this.globalManager.onNewGlobal.bind(this);
    // Keep the debugger disabled until a client attaches.
    this.dbg.enabled = this._state != "detached";
  },

  /**
   * Remove a debuggee global from the JSInspector.
   */
  removeDebugee: function TA_removeDebuggee(aGlobal) {
    try {
      this.dbg.removeDebuggee(aGlobal);
    } catch(ex) {
      // XXX: This debuggee has code currently executing on the stack,
      // we need to save this for later.
    }
  },

  /**
   * Add the provided window and all windows in its frame tree as debuggees.
   */
  _addDebuggees: function TA__addDebuggees(aWindow) {
    this.addDebuggee(aWindow);
    let frames = aWindow.frames;
    if (frames) {
      for (let i = 0; i < frames.length; i++) {
        this._addDebuggees(frames[i]);
      }
    }
  },

  /**
   * An object that will be used by ThreadActors to tailor their behavior
   * depending on the debugging context being required (chrome or content).
   */
  globalManager: {
    findGlobals: function TA_findGlobals() {
      this._addDebuggees(this.global);
    },

    /**
     * A function that the engine calls when a new global object has been
     * created.
     *
     * @param aGlobal Debugger.Object
     *        The new global object that was created.
     */
    onNewGlobal: function TA_onNewGlobal(aGlobal) {
      // Content debugging only cares about new globals in the contant window,
      // like iframe children.
      if (aGlobal.hostAnnotations &&
          aGlobal.hostAnnotations.type == "document" &&
          aGlobal.hostAnnotations.element === this.global) {
        this.addDebuggee(aGlobal);
      }
      // Notify the client.
      this.conn.send({
        from: this.actorID,
        type: "newGlobal",
        // TODO: after bug 801084 lands see if we need to JSONify this.
        hostAnnotations: aGlobal.hostAnnotations
      });
    }
  },

  disconnect: function TA_disconnect() {
    if (this._state == "paused") {
      this.onResume();
    }

    this._state = "exited";

    this.clearDebuggees();

    if (!this.dbg) {
      return;
    }
    this.dbg.enabled = false;
    this.dbg = null;
  },

  /**
   * Disconnect the debugger and put the actor in the exited state.
   */
  exit: function TA_exit() {
    this.disconnect();
  },

  // Request handlers
  onAttach: function TA_onAttach(aRequest) {
    if (this.state === "exited") {
      return { type: "exited" };
    }

    if (this.state !== "detached") {
      return { error: "wrongState" };
    }

    this._state = "attached";

    if (!this.dbg) {
      this._initDebugger();
    }
    this.findGlobals();
    this.dbg.enabled = true;
    try {
      // Put ourselves in the paused state.
      let packet = this._paused();
      if (!packet) {
        return { error: "notAttached" };
      }
      packet.why = { type: "attached" };

      // Send the response to the attach request now (rather than
      // returning it), because we're going to start a nested event loop
      // here.
      this.conn.send(packet);

      // Start a nested event loop.
      this._nest();

      // We already sent a response to this request, don't send one
      // now.
      return null;
    } catch(e) {
      Cu.reportError(e);
      return { error: "notAttached", message: e.toString() };
    }
  },

  onDetach: function TA_onDetach(aRequest) {
    this.disconnect();
    return { type: "detached" };
  },

  /**
   * Pause the debuggee, by entering a nested event loop, and return a 'paused'
   * packet to the client.
   *
   * @param Debugger.Frame aFrame
   *        The newest debuggee frame in the stack.
   * @param object aReason
   *        An object with a 'type' property containing the reason for the pause.
   */
  _pauseAndRespond: function TA__pauseAndRespond(aFrame, aReason) {
    try {
      let packet = this._paused(aFrame);
      if (!packet) {
        return undefined;
      }
      packet.why = aReason;
      this.conn.send(packet);
      return this._nest();
    } catch(e) {
      let msg = "Got an exception during TA__pauseAndRespond: " + e +
                ": " + e.stack;
      Cu.reportError(msg);
      dumpn(msg);
      return undefined;
    }
  },

  /**
   * Handle a protocol request to resume execution of the debuggee.
   */
  onResume: function TA_onResume(aRequest) {
    if (aRequest && aRequest.forceCompletion) {
      // TODO: remove this when Debugger.Frame.prototype.pop is implemented in
      // bug 736733.
      if (typeof this.frame.pop != "function") {
        return { error: "notImplemented",
                 message: "forced completion is not yet implemented." };
      }

      this.dbg.getNewestFrame().pop(aRequest.completionValue);
      let packet = this._resumed();
      DebuggerServer.xpcInspector.exitNestedEventLoop();
      return { type: "resumeLimit", frameFinished: aRequest.forceCompletion };
    }

    if (aRequest && aRequest.resumeLimit) {
      // Bind these methods because some of the hooks are called with 'this'
      // set to the current frame.
      let pauseAndRespond = this._pauseAndRespond.bind(this);
      let createValueGrip = this.createValueGrip.bind(this);

      let startFrame = this._youngestFrame;
      let startLine;
      if (this._youngestFrame.script) {
        let offset = this._youngestFrame.offset;
        startLine = this._youngestFrame.script.getOffsetLine(offset);
      }

      // Define the JS hook functions for stepping.

      let onEnterFrame = function TA_onEnterFrame(aFrame) {
        return pauseAndRespond(aFrame, { type: "resumeLimit" });
      };

      let onPop = function TA_onPop(aCompletion) {
        // onPop is called with 'this' set to the current frame.

        // Note that we're popping this frame; we need to watch for
        // subsequent step events on its caller.
        this.reportedPop = true;

        return pauseAndRespond(this, { type: "resumeLimit" });
      }

      let onStep = function TA_onStep() {
        // onStep is called with 'this' set to the current frame.

        // If we've changed frame or line, then report that.
        if (this !== startFrame ||
            (this.script &&
             this.script.getOffsetLine(this.offset) != startLine)) {
          return pauseAndRespond(this, { type: "resumeLimit" });
        }

        // Otherwise, let execution continue.
        return undefined;
      }

      let steppingType = aRequest.resumeLimit.type;
      if (["step", "next", "finish"].indexOf(steppingType) == -1) {
            return { error: "badParameterType",
                     message: "Unknown resumeLimit type" };
      }
      // Make sure there is still a frame on the stack if we are to continue
      // stepping.
      let stepFrame = this._getNextStepFrame(startFrame);
      if (stepFrame) {
        switch (steppingType) {
          case "step":
            this.dbg.onEnterFrame = onEnterFrame;
            // Fall through.
          case "next":
            stepFrame.onStep = onStep;
            stepFrame.onPop = onPop;
            break;
          case "finish":
            stepFrame.onPop = onPop;
        }
      }
    }

    if (aRequest && aRequest.pauseOnExceptions) {
      this.dbg.onExceptionUnwind = this.onExceptionUnwind.bind(this);
    }
    let packet = this._resumed();
    DebuggerServer.xpcInspector.exitNestedEventLoop();
    return packet;
  },

  /**
   * Helper method that returns the next frame when stepping.
   */
  _getNextStepFrame: function TA__getNextStepFrame(aFrame) {
    let stepFrame = aFrame.reportedPop ? aFrame.older : aFrame;
    if (!stepFrame || !stepFrame.script) {
      stepFrame = null;
    }
    return stepFrame;
  },

  onClientEvaluate: function TA_onClientEvaluate(aRequest) {
    if (this.state !== "paused") {
      return { error: "wrongState",
               message: "Debuggee must be paused to evaluate code." };
    };

    let frame = this._requestFrame(aRequest.frame);
    if (!frame) {
      return { error: "unknownFrame",
               message: "Evaluation frame not found" };
    }

    if (!frame.environment) {
      return { error: "notDebuggee",
               message: "cannot access the environment of this frame." };
    };

    // We'll clobber the youngest frame if the eval causes a pause, so
    // save our frame now to be restored after eval returns.
    // XXX: or we could just start using dbg.getNewestFrame() now that it
    // works as expected.
    let youngest = this._youngestFrame;

    // Put ourselves back in the running state and inform the client.
    let resumedPacket = this._resumed();
    this.conn.send(resumedPacket);

    // Run the expression.
    // XXX: test syntax errors
    let completion = frame.eval(aRequest.expression);

    // Put ourselves back in the pause state.
    let packet = this._paused(youngest);
    packet.why = { type: "clientEvaluated",
                   frameFinished: this.createProtocolCompletionValue(completion) };

    // Return back to our previous pause's event loop.
    return packet;
  },

  onFrames: function TA_onFrames(aRequest) {
    if (this.state !== "paused") {
      return { error: "wrongState",
               message: "Stack frames are only available while the debuggee is paused."};
    }

    let start = aRequest.start ? aRequest.start : 0;
    let count = aRequest.count;

    // Find the starting frame...
    let frame = this._youngestFrame;
    let i = 0;
    while (frame && (i < start)) {
      frame = frame.older;
      i++;
    }

    // Return request.count frames, or all remaining
    // frames if count is not defined.
    let frames = [];
    for (; frame && (!count || i < (start + count)); i++) {
      let form = this._createFrameActor(frame).form();
      form.depth = i;
      frames.push(form);
      frame = frame.older;
    }

    return { frames: frames };
  },

  onReleaseMany: function TA_onReleaseMany(aRequest) {
    if (!aRequest.actors) {
      return { error: "missingParameter",
               message: "no actors were specified" };
    }

    let res;
    for each (let actorID in aRequest.actors) {
      let actor = this.threadLifetimePool.get(actorID);
      if (!actor) {
        if (!res) {
          res = { error: "notReleasable",
                  message: "Only thread-lifetime actors can be released." };
        }
        continue;
      }
      actor.onRelease();
    }
    return res ? res : {};
  },

  /**
   * Handle a protocol request to set a breakpoint.
   */
  onSetBreakpoint: function TA_onSetBreakpoint(aRequest) {
    if (this.state !== "paused") {
      return { error: "wrongState",
               message: "Breakpoints can only be set while the debuggee is paused."};
    }

    let location = aRequest.location;
    let line = location.line;
    if (!this._scripts[location.url] || line < 0) {
      return { error: "noScript" };
    }

    // Add the breakpoint to the store for later reuse, in case it belongs to a
    // script that hasn't appeared yet.
    if (!this._breakpointStore[location.url]) {
      this._breakpointStore[location.url] = [];
    }
    let scriptBreakpoints = this._breakpointStore[location.url];
    scriptBreakpoints[line] = {
      url: location.url,
      line: line,
      column: location.column
    };

    return this._setBreakpoint(location);
  },

  /**
   * Set a breakpoint using the jsdbg2 API. If the line on which the breakpoint
   * is being set contains no code, then the breakpoint will slide down to the
   * next line that has runnable code. In this case the server breakpoint cache
   * will be updated, so callers that iterate over the breakpoint cache should
   * take that into account.
   *
   * @param object aLocation
   *        The location of the breakpoint as specified in the protocol.
   */
  _setBreakpoint: function TA__setBreakpoint(aLocation) {
    // Fetch the list of scripts in that url.
    let scripts = this._scripts[aLocation.url];
    // Fetch the outermost script in that list.
    let script = null;
    for (let i = 0; i <= aLocation.line; i++) {
      // Stop when the first script that contains this location is found.
      if (scripts[i]) {
        // If that first script does not contain the line specified, it's no
        // good. Note that |i === scripts[i].startLine| in this case, so the
        // following check makes sure we are not considering a script that does
        // not include |aLocation.line|.
        if (i + scripts[i].lineCount < aLocation.line) {
          continue;
        }
        script = scripts[i];
        break;
      }
    }

    let location = { url: aLocation.url, line: aLocation.line };
    // Get the list of cached breakpoints in this URL.
    let scriptBreakpoints = this._breakpointStore[location.url];
    let bpActor;
    if (scriptBreakpoints &&
        scriptBreakpoints[location.line] &&
        scriptBreakpoints[location.line].actor) {
      bpActor = scriptBreakpoints[location.line].actor;
    }
    if (!bpActor) {
      bpActor = new BreakpointActor(this, location);
      this._hooks.addToParentPool(bpActor);
      if (scriptBreakpoints[location.line]) {
        scriptBreakpoints[location.line].actor = bpActor;
      }
    }

    if (!script) {
      return { error: "noScript", actor: bpActor.actorID };
    }

    let inner, codeFound = false;
    // We need to set the breakpoint in every script that has bytecode in the
    // specified line.
    for (let s of this._getContainers(script, aLocation.line)) {
      // The first result of the iteration is the innermost script.
      if (!inner) {
        inner = s;
      }

      let offsets = s.getLineOffsets(aLocation.line);
      if (offsets.length) {
        bpActor.addScript(s, this);
        for (let i = 0; i < offsets.length; i++) {
          s.setBreakpoint(offsets[i], bpActor);
          codeFound = true;
        }
      }
    }

    let actualLocation;
    if (!codeFound) {
      // No code at that line in any script, skipping forward in the innermost
      // script.
      let lines = inner.getAllOffsets();
      let oldLine = aLocation.line;
      for (let line = oldLine; line < lines.length; ++line) {
        if (lines[line]) {
          for (let i = 0; i < lines[line].length; i++) {
            inner.setBreakpoint(lines[line][i], bpActor);
            codeFound = true;
          }
          bpActor.addScript(inner, this);
          actualLocation = {
            url: aLocation.url,
            line: line,
            column: aLocation.column
          };
          // If there wasn't already a breakpoint at that line, update the cache
          // as well.
          if (scriptBreakpoints[line] && scriptBreakpoints[line].actor) {
            let existing = scriptBreakpoints[line].actor;
            bpActor.onDelete();
            delete scriptBreakpoints[oldLine];
            return { actor: existing.actorID, actualLocation: actualLocation };
          }
          bpActor.location = actualLocation;
          scriptBreakpoints[line] = scriptBreakpoints[oldLine];
          scriptBreakpoints[line].line = line;
          delete scriptBreakpoints[oldLine];
          break;
        }
      }
    }

    if (!codeFound) {
      return  { error: "noCodeAtLineColumn", actor: bpActor.actorID };
    }

    return { actor: bpActor.actorID, actualLocation: actualLocation };
  },

  /**
   * A recursive generator function for iterating over the scripts that contain
   * the specified line, by looking through child scripts of the supplied
   * script. As an example, an inline <script> tag has the top-level functions
   * declared in it as its children.
   *
   * @param aScript Debugger.Script
   *        The source script.
   * @param aLine number
   *        The line number.
   */
  _getContainers: function TA__getContainers(aScript, aLine) {
    let children = aScript.getChildScripts();
    if (children.length > 0) {
      for (let i = 0; i < children.length; i++) {
        let child = children[i];
        // Iterate over the children that contain this location.
        if (child.startLine <= aLine &&
            child.startLine + child.lineCount > aLine) {
          for (let j of this._getContainers(child, aLine)) {
            yield j;
          }
        }
      }
    }
    // Include this script in the iteration, too.
    yield aScript;
  },

  /**
   * Handle a protocol request to return the list of loaded scripts.
   */
  onScripts: function TA_onScripts(aRequest) {
    // Get the script list from the debugger.
    for (let s of this.dbg.findScripts()) {
      this._addScript(s);
    }
    // Build the cache.
    let scripts = [];
    for (let url in this._scripts) {
      for (let i = 0; i < this._scripts[url].length; i++) {
        if (!this._scripts[url][i]) {
          continue;
        }

        let script = {
          url: url,
          startLine: i,
          lineCount: this._scripts[url][i].lineCount,
          source: this.sourceGrip(this._scripts[url][i], this)
        };
        scripts.push(script);
      }
    }

    let packet = { from: this.actorID,
                   scripts: scripts };
    return packet;
  },

  /**
   * Handle a protocol request to pause the debuggee.
   */
  onInterrupt: function TA_onInterrupt(aRequest) {
    if (this.state == "exited") {
      return { type: "exited" };
    } else if (this.state == "paused") {
      // TODO: return the actual reason for the existing pause.
      return { type: "paused", why: { type: "alreadyPaused" } };
    } else if (this.state != "running") {
      return { error: "wrongState",
               message: "Received interrupt request in " + this.state +
                        " state." };
    }

    try {
      // Put ourselves in the paused state.
      let packet = this._paused();
      if (!packet) {
        return { error: "notInterrupted" };
      }
      packet.why = { type: "interrupted" };

      // Send the response to the interrupt request now (rather than
      // returning it), because we're going to start a nested event loop
      // here.
      this.conn.send(packet);

      // Start a nested event loop.
      this._nest();

      // We already sent a response to this request, don't send one
      // now.
      return null;
    } catch(e) {
      Cu.reportError(e);
      return { error: "notInterrupted", message: e.toString() };
    }
  },

  /**
   * Return the Debug.Frame for a frame mentioned by the protocol.
   */
  _requestFrame: function TA_requestFrame(aFrameID) {
    if (!aFrameID) {
      return this._youngestFrame;
    }

    if (this._framePool.has(aFrameID)) {
      return this._framePool.get(aFrameID).frame;
    }

    return undefined;
  },

  _paused: function TA_paused(aFrame) {
    // We don't handle nested pauses correctly.  Don't try - if we're
    // paused, just continue running whatever code triggered the pause.
    // We don't want to actually have nested pauses (although we
    // have nested event loops).  If code runs in the debuggee during
    // a pause, it should cause the actor to resume (dropping
    // pause-lifetime actors etc) and then repause when complete.

    if (this.state === "paused") {
      return undefined;
    }

    // Clear stepping hooks.
    this.dbg.onEnterFrame = undefined;
    this.dbg.onExceptionUnwind = undefined;
    if (aFrame) {
      aFrame.onStep = undefined;
      aFrame.onPop = undefined;
    }

    this._state = "paused";

    // Save the pause frame (if any) as the youngest frame for
    // stack viewing.
    this._youngestFrame = aFrame;

    // Create the actor pool that will hold the pause actor and its
    // children.
    dbg_assert(!this._pausePool);
    this._pausePool = new ActorPool(this.conn);
    this.conn.addActorPool(this._pausePool);

    // Give children of the pause pool a quick link back to the
    // thread...
    this._pausePool.threadActor = this;

    // Create the pause actor itself...
    dbg_assert(!this._pauseActor);
    this._pauseActor = new PauseActor(this._pausePool);
    this._pausePool.addActor(this._pauseActor);

    // Update the list of frames.
    let poppedFrames = this._updateFrames();

    // Send off the paused packet and spin an event loop.
    let packet = { from: this.actorID,
                   type: "paused",
                   actor: this._pauseActor.actorID };
    if (aFrame) {
      packet.frame = this._createFrameActor(aFrame).form();
    }

    if (poppedFrames) {
      packet.poppedFrames = poppedFrames;
    }

    return packet;
  },

  _nest: function TA_nest() {
    if (this._hooks.preNest) {
      var nestData = this._hooks.preNest();
    }

    DebuggerServer.xpcInspector.enterNestedEventLoop();

    dbg_assert(this.state === "running");

    if (this._hooks.postNest) {
      this._hooks.postNest(nestData)
    }

    // "continue" resumption value.
    return undefined;
  },

  _resumed: function TA_resumed() {
    this._state = "running";

    // Drop the actors in the pause actor pool.
    this.conn.removeActorPool(this._pausePool);

    this._pausePool = null;
    this._pauseActor = null;
    this._youngestFrame = null;

    return { from: this.actorID, type: "resumed" };
  },

  /**
   * Expire frame actors for frames that have been popped.
   *
   * @returns A list of actor IDs whose frames have been popped.
   */
  _updateFrames: function TA_updateFrames() {
    let popped = [];

    // Create the actor pool that will hold the still-living frames.
    let framePool = new ActorPool(this.conn);
    let frameList = [];

    for each (let frameActor in this._frameActors) {
      if (frameActor.frame.live) {
        framePool.addActor(frameActor);
        frameList.push(frameActor);
      } else {
        popped.push(frameActor.actorID);
      }
    }

    // Remove the old frame actor pool, this will expire
    // any actors that weren't added to the new pool.
    if (this._framePool) {
      this.conn.removeActorPool(this._framePool);
    }

    this._frameActors = frameList;
    this._framePool = framePool;
    this.conn.addActorPool(framePool);

    return popped;
  },

  _createFrameActor: function TA_createFrameActor(aFrame) {
    if (aFrame.actor) {
      return aFrame.actor;
    }

    let actor = new FrameActor(aFrame, this);
    this._frameActors.push(actor);
    this._framePool.addActor(actor);
    aFrame.actor = actor;

    return actor;
  },

  /**
   * Create and return an environment actor that corresponds to the provided
   * Debugger.Environment.
   * @param Debugger.Environment aEnvironment
   *        The lexical environment we want to extract.
   * @param object aPool
   *        The pool where the newly-created actor will be placed.
   * @return The EnvironmentActor for aEnvironment or undefined for host
   *         functions or functions scoped to a non-debuggee global.
   */
  createEnvironmentActor:
  function TA_createEnvironmentActor(aEnvironment, aPool) {
    if (!aEnvironment) {
      return undefined;
    }

    if (aEnvironment.actor) {
      return aEnvironment.actor;
    }

    let actor = new EnvironmentActor(aEnvironment, this);
    this._environmentActors.push(actor);
    aPool.addActor(actor);
    aEnvironment.actor = actor;

    return actor;
  },

  /**
   * Create a grip for the given debuggee value.  If the value is an
   * object, will create an actor with the given lifetime.
   */
  createValueGrip: function TA_createValueGrip(aValue, aPool=false) {
    if (!aPool) {
      aPool = this._pausePool;
    }
    let type = typeof(aValue);

    if (type === "string" && this._stringIsLong(aValue)) {
      return this.longStringGrip(aValue, aPool);
    }

    if (type === "boolean" || type === "string" || type === "number") {
      return aValue;
    }

    if (aValue === null) {
      return { type: "null" };
    }

    if (aValue === undefined) {
      return { type: "undefined" }
    }

    if (typeof(aValue) === "object") {
      return this.objectGrip(aValue, aPool);
    }

    dbg_assert(false, "Failed to provide a grip for: " + aValue);
    return null;
  },

  /**
   * Return a protocol completion value representing the given
   * Debugger-provided completion value.
   */
  createProtocolCompletionValue:
  function TA_createProtocolCompletionValue(aCompletion) {
    let protoValue = {};
    if ("return" in aCompletion) {
      protoValue.return = this.createValueGrip(aCompletion.return);
    } else if ("yield" in aCompletion) {
      protoValue.return = this.createValueGrip(aCompletion.yield);
    } else if ("throw" in aCompletion) {
      protoValue.throw = this.createValueGrip(aCompletion.throw);
    } else {
      protoValue.terminated = true;
    }
    return protoValue;
  },

  /**
   * Create a grip for the given debuggee object.
   *
   * @param aValue Debugger.Object
   *        The debuggee object value.
   * @param aPool ActorPool
   *        The actor pool where the new object actor will be added.
   */
  objectGrip: function TA_objectGrip(aValue, aPool) {
    if (!aPool.objectActors) {
      aPool.objectActors = new WeakMap();
    }

    if (aPool.objectActors.has(aValue)) {
      return aPool.objectActors.get(aValue).grip();
    } else if (this.threadLifetimePool.objectActors.has(aValue)) {
      return this.threadLifetimePool.objectActors.get(aValue).grip();
    }

    let actor = new ObjectActor(aValue, this);
    aPool.addActor(actor);
    aPool.objectActors.set(aValue, actor);
    return actor.grip();
  },

  /**
   * Create a grip for the given debuggee object with a pause lifetime.
   *
   * @param aValue Debugger.Object
   *        The debuggee object value.
   */
  pauseObjectGrip: function TA_pauseObjectGrip(aValue) {
    if (!this._pausePool) {
      throw "Object grip requested while not paused.";
    }

    return this.objectGrip(aValue, this._pausePool);
  },

  /**
   * Extend the lifetime of the provided object actor to thread lifetime.
   *
   * @param aActor object
   *        The object actor.
   */
  threadObjectGrip: function TA_threadObjectGrip(aActor) {
    // We want to reuse the existing actor ID, so we just remove it from the
    // current pool's weak map and then let pool.addActor do the rest.
    aActor.registeredPool.objectActors.delete(aActor.obj);
    this.threadLifetimePool.addActor(aActor);
    this.threadLifetimePool.objectActors.set(aActor.obj, aActor);
  },

  /**
   * Handle a protocol request to promote multiple pause-lifetime grips to
   * thread-lifetime grips.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onThreadGrips: function OA_onThreadGrips(aRequest) {
    if (this.state != "paused") {
      return { error: "wrongState" };
    }

    if (!aRequest.actors) {
      return { error: "missingParameter",
               message: "no actors were specified" };
    }

    for (let actorID of aRequest.actors) {
      let actor = this._pausePool.get(actorID);
      if (actor) {
        this.threadObjectGrip(actor);
      }
    }
    return {};
  },

  /**
   * Create a grip for the given string.
   *
   * @param aString String
   *        The string we are creating a grip for.
   * @param aPool ActorPool
   *        The actor pool where the new actor will be added.
   */
  longStringGrip: function TA_longStringGrip(aString, aPool) {
    if (!aPool.longStringActors) {
      aPool.longStringActors = {};
    }

    if (aPool.longStringActors.hasOwnProperty(aString)) {
      return aPool.longStringActors[aString].grip();
    }

    let actor = new LongStringActor(aString, this);
    aPool.addActor(actor);
    aPool.longStringActors[aString] = actor;
    return actor.grip();
  },

  /**
   * Create a long string grip that is scoped to a pause.
   *
   * @param aString String
   *        The string we are creating a grip for.
   */
  pauseLongStringGrip: function TA_pauseLongStringGrip (aString) {
    return this.longStringGrip(aString, this._pausePool);
  },

  /**
   * Create a long string grip that is scoped to a thread.
   *
   * @param aString String
   *        The string we are creating a grip for.
   */
  threadLongStringGrip: function TA_pauseLongStringGrip (aString) {
    return this.longStringGrip(aString, this._threadLifetimePool);
  },

  /**
   * Returns true if the string is long enough to use a LongStringActor instead
   * of passing the value directly over the protocol.
   *
   * @param aString String
   *        The string we are checking the length of.
   */
  _stringIsLong: function TA__stringIsLong(aString) {
    return aString.length >= DebuggerServer.LONG_STRING_LENGTH;
  },

  /**
   * Create a source grip for the given script.
   */
  sourceGrip: function TA_sourceGrip(aScript) {
    // TODO: Once we have Debugger.Source, this should be replaced with a
    // weakmap mapping Debugger.Source instances to SourceActor instances.
    if (!this.threadLifetimePool.sourceActors) {
      this.threadLifetimePool.sourceActors = {};
    }

    if (this.threadLifetimePool.sourceActors[aScript.url]) {
      return this.threadLifetimePool.sourceActors[aScript.url].grip();
    }

    let actor = new SourceActor(aScript, this);
    this.threadLifetimePool.addActor(actor);
    this.threadLifetimePool.sourceActors[aScript.url] = actor;
    return actor.grip();
  },

  // JS Debugger API hooks.

  /**
   * A function that the engine calls when a call to a debug event hook,
   * breakpoint handler, watchpoint handler, or similar function throws some
   * exception.
   *
   * @param aException exception
   *        The exception that was thrown in the debugger code.
   */
  uncaughtExceptionHook: function TA_uncaughtExceptionHook(aException) {
    dumpn("Got an exception:" + aException);
  },

  /**
   * A function that the engine calls when a debugger statement has been
   * executed in the specified frame.
   *
   * @param aFrame Debugger.Frame
   *        The stack frame that contained the debugger statement.
   */
  onDebuggerStatement: function TA_onDebuggerStatement(aFrame) {
    return this._pauseAndRespond(aFrame, { type: "debuggerStatement" });
  },

  /**
   * A function that the engine calls when an exception has been thrown and has
   * propagated to the specified frame.
   *
   * @param aFrame Debugger.Frame
   *        The youngest remaining stack frame.
   * @param aValue object
   *        The exception that was thrown.
   */
  onExceptionUnwind: function TA_onExceptionUnwind(aFrame, aValue) {
    try {
      let packet = this._paused(aFrame);
      if (!packet) {
        return undefined;
      }

      packet.why = { type: "exception",
                     exception: this.createValueGrip(aValue) };
      this.conn.send(packet);
      return this._nest();
    } catch(e) {
      Cu.reportError("Got an exception during TA_onExceptionUnwind: " + e +
                     ": " + e.stack);
      return undefined;
    }
  },

  /**
   * A function that the engine calls when a new script has been loaded into the
   * scope of the specified debuggee global.
   *
   * @param aScript Debugger.Script
   *        The source script that has been loaded into a debuggee compartment.
   * @param aGlobal Debugger.Object
   *        A Debugger.Object instance whose referent is the global object.
   */
  onNewScript: function TA_onNewScript(aScript, aGlobal) {
    if (this._addScript(aScript)) {
      // Notify the client.
      this.conn.send({
        from: this.actorID,
        type: "newScript",
        url: aScript.url,
        startLine: aScript.startLine,
        lineCount: aScript.lineCount,
        source: this.sourceGrip(aScript, this)
      });
    }
  },

  /**
   * Check if the provided script is allowed to be stored in the cache.
   *
   * @param aScript Debugger.Script
   *        The source script that will be stored.
   * @returns true, if the script can be added, false otherwise.
   */
  _allowScript: function TA__allowScript(aScript) {
    // Ignore anything we don't have a URL for (eval scripts, for example).
    if (!aScript.url)
      return false;
    // Ignore XBL bindings for content debugging.
    if (aScript.url.indexOf("chrome://") == 0) {
      return false;
    }
    // Ignore about:* pages for content debugging.
    if (aScript.url.indexOf("about:") == 0) {
      return false;
    }
    return true;
  },

  /**
   * Add the provided script to the server cache.
   *
   * @param aScript Debugger.Script
   *        The source script that will be stored.
   * @returns true, if the script was added, false otherwise.
   */
  _addScript: function TA__addScript(aScript) {
    if (!this._allowScript(aScript)) {
      return false;
    }
    // Use a sparse array for storing the scripts for each URL in order to
    // optimize retrieval.
    if (!this._scripts[aScript.url]) {
      this._scripts[aScript.url] = [];
    }
    this._scripts[aScript.url][aScript.startLine] = aScript;

    // Set any stored breakpoints.
    let existing = this._breakpointStore[aScript.url];
    if (existing) {
      let endLine = aScript.startLine + aScript.lineCount - 1;
      // Iterate over the lines backwards, so that sliding breakpoints don't
      // affect the loop.
      for (let line = existing.length - 1; line >= 0; line--) {
        let bp = existing[line];
        // Limit search to the line numbers contained in the new script.
        if (bp && line >= aScript.startLine && line <= endLine) {
          this._setBreakpoint(bp);
        }
      }
    }
    return true;
  }

};

ThreadActor.prototype.requestTypes = {
  "attach": ThreadActor.prototype.onAttach,
  "detach": ThreadActor.prototype.onDetach,
  "resume": ThreadActor.prototype.onResume,
  "clientEvaluate": ThreadActor.prototype.onClientEvaluate,
  "frames": ThreadActor.prototype.onFrames,
  "interrupt": ThreadActor.prototype.onInterrupt,
  "releaseMany": ThreadActor.prototype.onReleaseMany,
  "setBreakpoint": ThreadActor.prototype.onSetBreakpoint,
  "scripts": ThreadActor.prototype.onScripts,
  "threadGrips": ThreadActor.prototype.onThreadGrips
};


/**
 * Creates a PauseActor.
 *
 * PauseActors exist for the lifetime of a given debuggee pause.  Used to
 * scope pause-lifetime grips.
 *
 * @param ActorPool aPool
 *        The actor pool created for this pause.
 */
function PauseActor(aPool)
{
  this.pool = aPool;
}

PauseActor.prototype = {
  actorPrefix: "pause"
};


/**
 * A base actor for any actors that should only respond receive messages in the
 * paused state. Subclasses may expose a `threadActor` which is used to help
 * determine when we are in a paused state. Subclasses should set their own
 * "constructor" property if they want better error messages. You should never
 * instantiate a PauseScopedActor directly, only through subclasses.
 */
function PauseScopedActor()
{
}

/**
 * A function decorator for creating methods to handle protocol messages that
 * should only be received while in the paused state.
 *
 * @param aMethod Function
 *        The function we are decorating.
 */
PauseScopedActor.withPaused = function PSA_withPaused(aMethod) {
  return function () {
    if (this.isPaused()) {
      return aMethod.apply(this, arguments);
    } else {
      return this._wrongState();
    }
  };
};

PauseScopedActor.prototype = {

  /**
   * Returns true if we are in the paused state.
   */
  isPaused: function PSA_isPaused() {
    // When there is not a ThreadActor available (like in the webconsole) we
    // have to be optimistic and assume that we are paused so that we can
    // respond to requests.
    return this.threadActor ? this.threadActor.state === "paused" : true;
  },

  /**
   * Returns the wrongState response packet for this actor.
   */
  _wrongState: function PSA_wrongState() {
    return {
      error: "wrongState",
      message: this.constructor.name +
        " actors can only be accessed while the thread is paused."
    }
  }
};


/**
 * A SourceActor provides information about the source of a script.
 *
 * @param aScript Debugger.Script
 *        The script whose source we are representing.
 * @param aThreadActor ThreadActor
 *        The current thread actor.
 */
function SourceActor(aScript, aThreadActor) {
  this._threadActor = aThreadActor;
  this._script = aScript;
}

SourceActor.prototype = {
  constructor: SourceActor,
  actorPrefix: "source",

  get threadActor() { return this._threadActor; },

  grip: function SA_grip() {
    return this.actorID;
  },

  disconnect: function LSA_disconnect() {
    if (this.registeredPool && this.registeredPool.sourceActors) {
      delete this.registeredPool.sourceActors[this.actorID];
    }
  },

  /**
   * Handler for the "source" packet.
   */
  onSource: function SA_onSource(aRequest) {
    this
      ._loadSource()
      .chainPromise(function(aSource) {
        return this._threadActor.createValueGrip(
          aSource, this.threadActor.threadLifetimePool);
      }.bind(this))
      .chainPromise(function (aSourceGrip) {
        return {
          from: this.actorID,
          source: aSourceGrip
        };
      }.bind(this))
      .trap(function (aError) {
        return {
          "from": this.actorID,
          "error": "loadSourceError",
          "message": "Could not load the source for " + this._script.url + "."
        };
      }.bind(this))
      .chainPromise(function (aPacket) {
        this.conn.send(aPacket);
      }.bind(this));
  },

  /**
   * Convert a given string, encoded in a given character set, to unicode.
   * @param string aString
   *        A string.
   * @param string aCharset
   *        A character set.
   * @return string
   *         A unicode string.
   */
  _convertToUnicode: function SS__convertToUnicode(aString, aCharset) {
    // Decoding primitives.
    let converter = Cc["@mozilla.org/intl/scriptableunicodeconverter"]
        .createInstance(Ci.nsIScriptableUnicodeConverter);

    try {
      converter.charset = aCharset || "UTF-8";
      return converter.ConvertToUnicode(aString);
    } catch(e) {
      return aString;
    }
  },

  /**
   * Performs a request to load the desired URL and returns a promise.
   *
   * @param aURL String
   *        The URL we will request.
   * @returns Promise
   *
   * XXX: It may be better to use nsITraceableChannel to get to the sources
   * without relying on caching when we can (not for eval, etc.):
   * http://www.softwareishard.com/blog/firebug/nsitraceablechannel-intercept-http-traffic/
   */
  _loadSource: function SA__loadSource() {
    let promise = new Promise();
    let url = this._script.url;
    let scheme;
    try {
      scheme = Services.io.extractScheme(url);
    } catch (e) {
      // In the xpcshell tests, the script url is the absolute path of the test
      // file, which will make a malformed URI error be thrown. Add the file
      // scheme prefix ourselves.
      url = "file://" + url;
      scheme = Services.io.extractScheme(url);
    }

    switch (scheme) {
      case "file":
      case "chrome":
      case "resource":
        try {
          NetUtil.asyncFetch(url, function onFetch(aStream, aStatus) {
            if (!Components.isSuccessCode(aStatus)) {
              promise.reject(new Error("Request failed"));
              return;
            }

            let source = NetUtil.readInputStreamToString(aStream, aStream.available());
            promise.resolve(this._convertToUnicode(source));
            aStream.close();
          }.bind(this));
        } catch (ex) {
          promise.reject(new Error("Request failed"));
        }
        break;

      default:
        let channel;
        try {
          channel = Services.io.newChannel(url, null, null);
        } catch (e if e.name == "NS_ERROR_UNKNOWN_PROTOCOL") {
          // On Windows xpcshell tests, c:/foo/bar can pass as a valid URL, but
          // newChannel won't be able to handle it.
          url = "file:///" + url;
          channel = Services.io.newChannel(url, null, null);
        }
        let chunks = [];
        let streamListener = {
          onStartRequest: function(aRequest, aContext, aStatusCode) {
            if (!Components.isSuccessCode(aStatusCode)) {
              promise.reject("Request failed");
            }
          },
          onDataAvailable: function(aRequest, aContext, aStream, aOffset, aCount) {
            chunks.push(NetUtil.readInputStreamToString(aStream, aCount));
          },
          onStopRequest: function(aRequest, aContext, aStatusCode) {
            if (!Components.isSuccessCode(aStatusCode)) {
              promise.reject("Request failed");
              return;
            }

            promise.resolve(this._convertToUnicode(chunks.join(""),
                                                   channel.contentCharset));
          }.bind(this)
        };

        channel.loadFlags = channel.LOAD_FROM_CACHE;
        channel.asyncOpen(streamListener, null);
        break;
    }

    return promise;
  }

};

SourceActor.prototype.requestTypes = {
  "source": SourceActor.prototype.onSource
};


/**
 * Creates an actor for the specified object.
 *
 * @param aObj Debugger.Object
 *        The debuggee object.
 * @param aThreadActor ThreadActor
 *        The parent thread actor for this object.
 */
function ObjectActor(aObj, aThreadActor)
{
  this.obj = aObj;
  this.threadActor = aThreadActor;
}

ObjectActor.prototype = Object.create(PauseScopedActor.prototype);

update(ObjectActor.prototype, {
  constructor: ObjectActor,
  actorPrefix: "obj",

  /**
   * Returns a grip for this actor for returning in a protocol message.
   */
  grip: function OA_grip() {
    let g = { "type": "object",
              "class": this.obj.class,
              "actor": this.actorID };

    // Add additional properties for functions.
    if (this.obj.class === "Function") {
      if (this.obj.name) {
        g.name = this.obj.name;
      } else if (this.obj.displayName) {
        g.displayName = this.obj.displayName;
      }

      // Check if the developer has added a de-facto standard displayName
      // property for us to use.
      let desc = this.obj.getOwnPropertyDescriptor("displayName");
      if (desc && desc.value && typeof desc.value == "string") {
        g.userDisplayName = this.threadActor.createValueGrip(desc.value);
      }
    }

    return g;
  },

  /**
   * Releases this actor from the pool.
   */
  release: function OA_release() {
    this.registeredPool.objectActors.delete(this.obj);
    this.registeredPool.removeActor(this);
  },

  /**
   * Handle a protocol request to provide the names of the properties defined on
   * the object and not its prototype.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onOwnPropertyNames:
  PauseScopedActor.withPaused(function OA_onOwnPropertyNames(aRequest) {
    return { from: this.actorID,
             ownPropertyNames: this.obj.getOwnPropertyNames() };
  }),

  /**
   * Handle a protocol request to provide the prototype and own properties of
   * the object.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onPrototypeAndProperties:
  PauseScopedActor.withPaused(function OA_onPrototypeAndProperties(aRequest) {
    let ownProperties = {};
    for each (let name in this.obj.getOwnPropertyNames()) {
      try {
        let desc = this.obj.getOwnPropertyDescriptor(name);
        ownProperties[name] = this._propertyDescriptor(desc);
      } catch (e if e.name == "NS_ERROR_XPC_BAD_OP_ON_WN_PROTO") {
        // Calling getOwnPropertyDescriptor on wrapped native prototypes is not
        // allowed.
        dumpn("Error while getting the property descriptor for " + name +
              ": " + e.name);
      }
    }
    return { from: this.actorID,
             prototype: this.threadActor.createValueGrip(this.obj.proto),
             ownProperties: ownProperties };
  }),

  /**
   * Handle a protocol request to provide the prototype of the object.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onPrototype: PauseScopedActor.withPaused(function OA_onPrototype(aRequest) {
    return { from: this.actorID,
             prototype: this.threadActor.createValueGrip(this.obj.proto) };
  }),

  /**
   * Handle a protocol request to provide the property descriptor of the
   * object's specified property.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onProperty: PauseScopedActor.withPaused(function OA_onProperty(aRequest) {
    if (!aRequest.name) {
      return { error: "missingParameter",
               message: "no property name was specified" };
    }

    let desc = this.obj.getOwnPropertyDescriptor(aRequest.name);
    return { from: this.actorID,
             descriptor: this._propertyDescriptor(desc) };
  }),

  /**
   * A helper method that creates a property descriptor for the provided object,
   * properly formatted for sending in a protocol response.
   *
   * @param aObject object
   *        The object that the descriptor is generated for.
   */
  _propertyDescriptor: function OA_propertyDescriptor(aObject) {
    let descriptor = {};
    descriptor.configurable = aObject.configurable;
    descriptor.enumerable = aObject.enumerable;
    if (aObject.value !== undefined) {
      descriptor.writable = aObject.writable;
      descriptor.value = this.threadActor.createValueGrip(aObject.value);
    } else {
      descriptor.get = this.threadActor.createValueGrip(aObject.get);
      descriptor.set = this.threadActor.createValueGrip(aObject.set);
    }
    return descriptor;
  },

  /**
   * Handle a protocol request to provide the source code of a function.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onDecompile: PauseScopedActor.withPaused(function OA_onDecompile(aRequest) {
    if (this.obj.class !== "Function") {
      return { error: "objectNotFunction",
               message: "decompile request is only valid for object grips " +
                        "with a 'Function' class." };
    }

    return { from: this.actorID,
             decompiledCode: this.obj.decompile(!!aRequest.pretty) };
  }),

  /**
   * Handle a protocol request to provide the lexical scope of a function.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onScope: PauseScopedActor.withPaused(function OA_onScope(aRequest) {
    if (this.obj.class !== "Function") {
      return { error: "objectNotFunction",
               message: "scope request is only valid for object grips with a" +
                        " 'Function' class." };
    }

    let envActor = this.threadActor.createEnvironmentActor(this.obj.environment,
                                                           this.registeredPool);
    if (!envActor) {
      return { error: "notDebuggee",
               message: "cannot access the environment of this function." };
    }

    // XXX: the following call of env.form() won't work until bug 747514 lands.
    // We can't get to the frame that defined this function's environment,
    // neither here, nor during ObjectActor's construction. Luckily, we don't
    // use the 'scope' request in the debugger frontend.
    return { name: this.obj.name || null,
             scope: envActor.form(this.obj) };
  }),

  /**
   * Handle a protocol request to provide the parameters of a function.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onParameterNames: PauseScopedActor.withPaused(function OA_onParameterNames(aRequest) {
    if (this.obj.class !== "Function") {
      return { error: "objectNotFunction",
               message: "'parameterNames' request is only valid for object " +
                        "grips with a 'Function' class." };
    }

    return { parameterNames: this.obj.parameterNames };
  }),

  /**
   * Handle a protocol request to promote a pause-lifetime grip to a
   * thread-lifetime grip.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onThreadGrip: PauseScopedActor.withPaused(function OA_onThreadGrip(aRequest) {
    this.threadActor.threadObjectGrip(this);
    return {};
  }),

  /**
   * Handle a protocol request to release a thread-lifetime grip.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onRelease: PauseScopedActor.withPaused(function OA_onRelease(aRequest) {
    if (this.registeredPool !== this.threadActor.threadLifetimePool) {
      return { error: "notReleasable",
               message: "Only thread-lifetime actors can be released." };
    }

    this.release();
    return {};
  }),
});

ObjectActor.prototype.requestTypes = {
  "parameterNames": ObjectActor.prototype.onParameterNames,
  "prototypeAndProperties": ObjectActor.prototype.onPrototypeAndProperties,
  "prototype": ObjectActor.prototype.onPrototype,
  "property": ObjectActor.prototype.onProperty,
  "ownPropertyNames": ObjectActor.prototype.onOwnPropertyNames,
  "scope": ObjectActor.prototype.onScope,
  "decompile": ObjectActor.prototype.onDecompile,
  "threadGrip": ObjectActor.prototype.onThreadGrip,
  "release": ObjectActor.prototype.onRelease,
};


/**
 * Creates an actor for the specied "very long" string. "Very long" is specified
 * at the server's discretion.
 *
 * @param aString String
 *        The string.
 */
function LongStringActor(aString)
{
  this.string = aString;
  this.stringLength = aString.length;
}

LongStringActor.prototype = {

  actorPrefix: "longString",

  disconnect: function LSA_disconnect() {
    // Because longStringActors is not a weak map, we won't automatically leave
    // it so we need to manually leave on disconnect so that we don't leak
    // memory.
    if (this.registeredPool && this.registeredPool.longStringActors) {
      delete this.registeredPool.longStringActors[this.actorID];
    }
  },

  /**
   * Returns a grip for this actor for returning in a protocol message.
   */
  grip: function LSA_grip() {
    return {
      "type": "longString",
      "initial": this.string.substring(
        0, DebuggerServer.LONG_STRING_INITIAL_LENGTH),
      "length": this.stringLength,
      "actor": this.actorID
    };
  },

  /**
   * Handle a request to extract part of this actor's string.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onSubstring: function LSA_onSubString(aRequest) {
    return {
      "from": this.actorID,
      "substring": this.string.substring(aRequest.start, aRequest.end)
    };
  },

  /**
   * Handle a request to release this LongStringActor instance.
   */
  onRelease: function LSA_onRelease() {
    // TODO: also check if registeredPool === threadActor.threadLifetimePool
    // when the web console moves aray from manually releasing pause-scoped
    // actors.
    if (this.registeredPool.longStringActors) {
      delete this.registeredPool.longStringActors[this.actorID];
    }
    this.registeredPool.removeActor(this);
    return {};
  },
};

LongStringActor.prototype.requestTypes = {
  "substring": LongStringActor.prototype.onSubstring,
  "release": LongStringActor.prototype.onRelease
};


/**
 * Creates an actor for the specified stack frame.
 *
 * @param aFrame Debugger.Frame
 *        The debuggee frame.
 * @param aThreadActor ThreadActor
 *        The parent thread actor for this frame.
 */
function FrameActor(aFrame, aThreadActor)
{
  this.frame = aFrame;
  this.threadActor = aThreadActor;
}

FrameActor.prototype = {
  actorPrefix: "frame",

  /**
   * A pool that contains frame-lifetime objects, like the environment.
   */
  _frameLifetimePool: null,
  get frameLifetimePool() {
    if (!this._frameLifetimePool) {
      this._frameLifetimePool = new ActorPool(this.conn);
      this.conn.addActorPool(this._frameLifetimePool);
    }
    return this._frameLifetimePool;
  },

  /**
   * Finalization handler that is called when the actor is being evicted from
   * the pool.
   */
  disconnect: function FA_disconnect() {
    this.conn.removeActorPool(this._frameLifetimePool);
    this._frameLifetimePool = null;
  },

  /**
   * Returns a frame form for use in a protocol message.
   */
  form: function FA_form() {
    let form = { actor: this.actorID,
                 type: this.frame.type };
    if (this.frame.type === "call") {
      form.callee = this.threadActor.createValueGrip(this.frame.callee);
    }

    let envActor = this.threadActor
                       .createEnvironmentActor(this.frame.environment,
                                               this.frameLifetimePool);
    form.environment = envActor ? envActor.form(this.frame) : envActor;
    form.this = this.threadActor.createValueGrip(this.frame.this);
    form.arguments = this._args();
    if (this.frame.script) {
      form.where = { url: this.frame.script.url,
                     line: this.frame.script.getOffsetLine(this.frame.offset) };
    }

    if (!this.frame.older) {
      form.oldest = true;
    }

    return form;
  },

  _args: function FA__args() {
    if (!this.frame.arguments) {
      return [];
    }

    return [this.threadActor.createValueGrip(arg)
            for each (arg in this.frame.arguments)];
  },

  /**
   * Handle a protocol request to pop this frame from the stack.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onPop: function FA_onPop(aRequest) {
    // TODO: remove this when Debugger.Frame.prototype.pop is implemented
    if (typeof this.frame.pop != "function") {
      return { error: "notImplemented",
               message: "Popping frames is not yet implemented." };
    }

    while (this.frame != this.threadActor.dbg.getNewestFrame()) {
      this.threadActor.dbg.getNewestFrame().pop();
    }
    this.frame.pop(aRequest.completionValue);

    // TODO: return the watches property when frame pop watch actors are
    // implemented.
    return { from: this.actorID };
  }
};

FrameActor.prototype.requestTypes = {
  "pop": FrameActor.prototype.onPop,
};


/**
 * Creates a BreakpointActor. BreakpointActors exist for the lifetime of their
 * containing thread and are responsible for deleting breakpoints, handling
 * breakpoint hits and associating breakpoints with scripts.
 *
 * @param ThreadActor aThreadActor
 *        The parent thread actor that contains this breakpoint.
 * @param object aLocation
 *        The location of the breakpoint as specified in the protocol.
 */
function BreakpointActor(aThreadActor, aLocation)
{
  this.scripts = [];
  this.threadActor = aThreadActor;
  this.location = aLocation;
}

BreakpointActor.prototype = {
  actorPrefix: "breakpoint",

  /**
   * Called when this same breakpoint is added to another Debugger.Script
   * instance, in the case of a page reload.
   *
   * @param aScript Debugger.Script
   *        The new source script on which the breakpoint has been set.
   * @param ThreadActor aThreadActor
   *        The parent thread actor that contains this breakpoint.
   */
  addScript: function BA_addScript(aScript, aThreadActor) {
    this.threadActor = aThreadActor;
    this.scripts.push(aScript);
  },

  /**
   * A function that the engine calls when a breakpoint has been hit.
   *
   * @param aFrame Debugger.Frame
   *        The stack frame that contained the breakpoint.
   */
  hit: function BA_hit(aFrame) {
    // TODO: add the rest of the breakpoints on that line (bug 676602).
    let reason = { type: "breakpoint", actors: [ this.actorID ] };
    return this.threadActor._pauseAndRespond(aFrame, reason);
  },

  /**
   * Handle a protocol request to remove this breakpoint.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onDelete: function BA_onDelete(aRequest) {
    // Remove from the breakpoint store.
    let scriptBreakpoints = this.threadActor._breakpointStore[this.location.url];
    delete scriptBreakpoints[this.location.line];
    // Remove the actual breakpoint.
    this.threadActor._hooks.removeFromParentPool(this);
    for (let script of this.scripts) {
      script.clearBreakpoint(this);
    }
    this.scripts = null;

    return { from: this.actorID };
  }
};

BreakpointActor.prototype.requestTypes = {
  "delete": BreakpointActor.prototype.onDelete
};


/**
 * Creates an EnvironmentActor. EnvironmentActors are responsible for listing
 * the bindings introduced by a lexical environment and assigning new values to
 * those identifier bindings.
 *
 * @param Debugger.Environment aEnvironment
 *        The lexical environment that will be used to create the actor.
 * @param ThreadActor aThreadActor
 *        The parent thread actor that contains this environment.
 */
function EnvironmentActor(aEnvironment, aThreadActor)
{
  this.obj = aEnvironment;
  this.threadActor = aThreadActor;
}

EnvironmentActor.prototype = {
  actorPrefix: "environment",

  /**
   * Returns an environment form for use in a protocol message. Note that the
   * requirement of passing the frame as a parameter is only temporary, since
   * when bug 747514 lands, the environment will have a callee property that
   * will contain it.
   *
   * @param Debugger.Frame aObject
   *        The stack frame object whose environment bindings are being
   *        generated.
   */
  form: function EA_form(aObject) {
    // Debugger.Frame might be dead by the time we get here, which will cause
    // accessing its properties to throw.
    if (!aObject.live) {
      return undefined;
    }

    let parent;
    if (this.obj.parent) {
      let thread = this.threadActor;
      parent = thread.createEnvironmentActor(this.obj.parent,
                                             this.registeredPool);
    }
    // Deduce the frame that created the parent scope in order to pass it to
    // parent.form(). TODO: this can be removed after bug 747514 is done.
    let parentFrame = aObject;
    if (this.obj.type == "declarative" && aObject.older) {
      parentFrame = aObject.older;
    }
    let form = { actor: this.actorID,
                 parent: parent ? parent.form(parentFrame) : parent };

    if (this.obj.type == "with") {
      form.type = "with";
      form.object = this.threadActor.createValueGrip(this.obj.object);
    } else if (this.obj.type == "object") {
      form.type = "object";
      form.object = this.threadActor.createValueGrip(this.obj.object);
    } else { // this.obj.type == "declarative"
      if (aObject.callee) {
        form.type = "function";
        form.function = this.threadActor.createValueGrip(aObject.callee);
      } else {
        form.type = "block";
      }
      form.bindings = this._bindings(aObject);
    }

    return form;
  },

  /**
   * Return the identifier bindings object as required by the remote protocol
   * specification. Note that the requirement of passing the frame as a
   * parameter is only temporary, since when bug 747514 lands, the environment
   * will have a callee property that will contain it.
   *
   * @param Debugger.Frame aObject [optional]
   *        The stack frame whose environment bindings are being generated. When
   *        left unspecified, the bindings do not contain an 'arguments'
   *        property.
   */
  _bindings: function EA_bindings(aObject) {
    let bindings = { arguments: [], variables: {} };

    // TODO: this part should be removed in favor of the commented-out part
    // below when getVariableDescriptor lands (bug 725815).
    if (typeof this.obj.getVariable != "function") {
    //if (typeof this.obj.getVariableDescriptor != "function") {
      return bindings;
    }

    let parameterNames;
    if (aObject && aObject.callee) {
      parameterNames = aObject.callee.parameterNames;
    }
    for each (let name in parameterNames) {
      let arg = {};
      // TODO: this part should be removed in favor of the commented-out part
      // below when getVariableDescriptor lands (bug 725815).
      let desc = {
        value: this.obj.getVariable(name),
        configurable: false,
        writable: true,
        enumerable: true
      };

      // let desc = this.obj.getVariableDescriptor(name);
      let descForm = {
        enumerable: true,
        configurable: desc.configurable
      };
      if ("value" in desc) {
        descForm.value = this.threadActor.createValueGrip(desc.value);
        descForm.writable = desc.writable;
      } else {
        descForm.get = this.threadActor.createValueGrip(desc.get);
        descForm.set = this.threadActor.createValueGrip(desc.set);
      }
      arg[name] = descForm;
      bindings.arguments.push(arg);
    }

    for each (let name in this.obj.names()) {
      if (bindings.arguments.some(function exists(element) {
                                    return !!element[name];
                                  })) {
        continue;
      }

      // TODO: this part should be removed in favor of the commented-out part
      // below when getVariableDescriptor lands.
      let desc = {
        configurable: false,
        writable: true,
        enumerable: true
      };
      try {
        desc.value = this.obj.getVariable(name);
      } catch (e) {
        // Avoid "Debugger scope is not live" errors for |arguments|, introduced
        // in bug 746601.
        if (name != "arguments") {
          throw e;
        }
      }
      //let desc = this.obj.getVariableDescriptor(name);
      let descForm = {
        enumerable: true,
        configurable: desc.configurable
      };
      if ("value" in desc) {
        descForm.value = this.threadActor.createValueGrip(desc.value);
        descForm.writable = desc.writable;
      } else {
        descForm.get = this.threadActor.createValueGrip(desc.get);
        descForm.set = this.threadActor.createValueGrip(desc.set);
      }
      bindings.variables[name] = descForm;
    }

    return bindings;
  },

  /**
   * Handle a protocol request to change the value of a variable bound in this
   * lexical environment.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onAssign: function EA_onAssign(aRequest) {
    // TODO: enable the commented-out part when getVariableDescriptor lands
    // (bug 725815).
    /*let desc = this.obj.getVariableDescriptor(aRequest.name);

    if (!desc.writable) {
      return { error: "immutableBinding",
               message: "Changing the value of an immutable binding is not " +
                        "allowed" };
    }*/

    try {
      this.obj.setVariable(aRequest.name, aRequest.value);
    } catch (e) {
      if (e instanceof Debugger.DebuggeeWouldRun) {
        return { error: "threadWouldRun",
                 cause: e.cause ? e.cause : "setter",
                 message: "Assigning a value would cause the debuggee to run" };
      }
      // This should never happen, so let it complain loudly if it does.
      throw e;
    }
    return { from: this.actorID };
  },

  /**
   * Handle a protocol request to fully enumerate the bindings introduced by the
   * lexical environment.
   *
   * @param aRequest object
   *        The protocol request object.
   */
  onBindings: function EA_onBindings(aRequest) {
    return { from: this.actorID,
             bindings: this._bindings() };
  }
};

EnvironmentActor.prototype.requestTypes = {
  "assign": EnvironmentActor.prototype.onAssign,
  "bindings": EnvironmentActor.prototype.onBindings
};

/**
 * Override the toString method in order to get more meaningful script output
 * for debugging the debugger.
 */
Debugger.Script.prototype.toString = function() {
  let output = "";
  if (this.url) {
    output += this.url;
  }
  if (typeof this.startLine != "undefined") {
    output += ":" + this.startLine;
    if (this.lineCount && this.lineCount > 1) {
      output += "-" + (this.startLine + this.lineCount - 1);
    }
  }
  if (this.strictMode) {
    output += ":strict";
  }
  return output;
};

/**
 * Helper property for quickly getting to the line number a stack frame is
 * currently paused at.
 */
Object.defineProperty(Debugger.Frame.prototype, "line", {
  configurable: true,
  get: function() {
    if (this.script) {
      return this.script.getOffsetLine(this.offset);
    } else {
      return null;
    }
  }
});


/**
 * Creates an actor for handling chrome debugging. ChromeDebuggerActor is a
 * thin wrapper over ThreadActor, slightly changing some of its behavior.
 *
 * @param aHooks object
 *        An object with preNest and postNest methods for calling when entering
 *        and exiting a nested event loop and also addToParentPool and
 *        removeFromParentPool methods for handling the lifetime of actors that
 *        will outlive the thread, like breakpoints.
 */
function ChromeDebuggerActor(aHooks)
{
  ThreadActor.call(this, aHooks);
}

ChromeDebuggerActor.prototype = Object.create(ThreadActor.prototype);

update(ChromeDebuggerActor.prototype, {
  constructor: ChromeDebuggerActor,

  // A constant prefix that will be used to form the actor ID by the server.
  actorPrefix: "chromeDebugger",

  /**
   * Override the eligibility check for scripts to make sure every script with a
   * URL is stored when debugging chrome.
   */
  _allowScript: function(aScript) !!aScript.url,

   /**
   * An object that will be used by ThreadActors to tailor their behavior
   * depending on the debugging context being required (chrome or content).
   * The methods that this object provides must be bound to the ThreadActor
   * before use.
   */
  globalManager: {
    findGlobals: function CDA_findGlobals() {
      // Fetch the list of globals from the debugger.
      for (let g of this.dbg.findAllGlobals()) {
        this.addDebuggee(g);
      }
    },

    /**
     * A function that the engine calls when a new global object has been
     * created.
     *
     * @param aGlobal Debugger.Object
     *        The new global object that was created.
     */
    onNewGlobal: function CDA_onNewGlobal(aGlobal) {
      this.addDebuggee(aGlobal);
      // Notify the client.
      this.conn.send({
        from: this.actorID,
        type: "newGlobal",
        // TODO: after bug 801084 lands see if we need to JSONify this.
        hostAnnotations: aGlobal.hostAnnotations
      });
    }
  }
});


// Utility functions.

/**
 * Utility function for updating an object with the properties of another
 * object.
 *
 * @param aTarget Object
 *        The object being updated.
 * @param aNewAttrs Object
 *        The new attributes being set on the target.
 */
function update(aTarget, aNewAttrs) {
  for (let key in aNewAttrs) {
    let desc = Object.getOwnPropertyDescriptor(aNewAttrs, key);

    if (desc) {
      Object.defineProperty(aTarget, key, desc);
    }
  }
}
