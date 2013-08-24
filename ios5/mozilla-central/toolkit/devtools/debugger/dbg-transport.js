/* -*- Mode: javascript; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";
Cu.import("resource://gre/modules/NetUtil.jsm");

/**
 * An adapter that handles data transfers between the debugger client and
 * server. It can work with both nsIPipe and nsIServerSocket transports so
 * long as the properly created input and output streams are specified.
 *
 * @param aInput nsIInputStream
 *        The input stream.
 * @param aOutput nsIOutputStream
 *        The output stream.
 *
 * Given a DebuggerTransport instance dt:
 * 1) Set dt.hooks to a packet handler object (described below).
 * 2) Call dt.ready() to begin watching for input packets.
 * 3) Send packets as you please, and handle incoming packets passed to 
 *    hook.onPacket.
 * 4) Call dt.close() to close the connection, and disengage from the event
 *    loop.
 *
 * A packet handler object is an object with two methods:
 *
 * - onPacket(packet) - called when we have received a complete packet.
 *   |Packet| is the parsed form of the packet --- a JavaScript value, not
 *   a JSON-syntax string.
 *
 * - onClosed(status) - called when the connection is closed. |Status| is
 *   an nsresult, of the sort passed to nsIRequestObserver.
 * 
 * Data is transferred as a JSON packet serialized into a string, with the
 * string length prepended to the packet, followed by a colon
 * ([length]:[packet]). The contents of the JSON packet are specified in
 * the Remote Debugging Protocol specification.
 */
function DebuggerTransport(aInput, aOutput)
{
  this._input = aInput;
  this._output = aOutput;

  this._converter = Cc["@mozilla.org/intl/scriptableunicodeconverter"]
    .createInstance(Ci.nsIScriptableUnicodeConverter);
  this._converter.charset = "UTF-8";

  this._outgoing = "";
  this._incoming = "";

  this.hooks = null;
}

DebuggerTransport.prototype = {
  /**
   * Transmit a packet.
   * 
   * This method returns immediately, without waiting for the entire
   * packet to be transmitted, registering event handlers as needed to
   * transmit the entire packet. Packets are transmitted in the order
   * they are passed to this method.
   */
  send: function DT_send(aPacket) {
    // TODO (bug 709088): remove pretty printing when the protocol is done.
    let data = JSON.stringify(aPacket, null, 2);
    data = this._converter.ConvertFromUnicode(data);
    data = data.length + ':' + data;
    this._outgoing += data;
    this._flushOutgoing();
  },

  /**
   * Close the transport.
   */
  close: function DT_close() {
    this._input.close();
    this._output.close();
  },

  /**
   * Flush the outgoing stream.
   */
  _flushOutgoing: function DT_flushOutgoing() {
    if (this._outgoing.length > 0) {
      var threadManager = Cc["@mozilla.org/thread-manager;1"].getService();
      this._output.asyncWait(this, 0, 0, threadManager.currentThread);
    }
  },

  onOutputStreamReady: function DT_onOutputStreamReady(aStream) {
    let written = aStream.write(this._outgoing, this._outgoing.length);
    this._outgoing = this._outgoing.slice(written);
    this._flushOutgoing();
  },

  /**
   * Initialize the input stream for reading. Once this method has been
   * called, we watch for packets on the input stream, and pass them to
   * this.hook.onPacket.
   */
  ready: function DT_ready() {
    let pump = Cc["@mozilla.org/network/input-stream-pump;1"]
      .createInstance(Ci.nsIInputStreamPump);
    pump.init(this._input, -1, -1, 0, 0, false);
    pump.asyncRead(this, null);
  },

  // nsIStreamListener
  onStartRequest: function DT_onStartRequest(aRequest, aContext) {},

  onStopRequest: function DT_onStopRequest(aRequest, aContext, aStatus) {
    this.close();
    this.hooks.onClosed(aStatus);
  },

  onDataAvailable: function DT_onDataAvailable(aRequest, aContext,
                                                aStream, aOffset, aCount) {
    try {
      this._incoming += NetUtil.readInputStreamToString(aStream,
                                                        aStream.available());
      while (this._processIncoming()) {};
    } catch(e) {
      dumpn("Unexpected error reading from debugging connection: " + e + " - " + e.stack);
      this.close();
      return;
    }
  },

  /**
   * Process incoming packets. Returns true if a packet has been received, either
   * if it was properly parsed or not. Returns false if the incoming stream does
   * not contain a full packet yet. After a proper packet is parsed, the dispatch
   * handler DebuggerTransport.hooks.onPacket is called with the packet as a
   * parameter.
   */
  _processIncoming: function DT__processIncoming() {
    // Well this is ugly.
    let sep = this._incoming.indexOf(':');
    if (sep < 0) {
      return false;
    }

    let count = parseInt(this._incoming.substring(0, sep));
    if (this._incoming.length - (sep + 1) < count) {
      // Don't have a complete request yet.
      return false;
    }

    // We have a complete request, pluck it out of the data and parse it.
    this._incoming = this._incoming.substring(sep + 1);
    let packet = this._incoming.substring(0, count);
    this._incoming = this._incoming.substring(count);

    try {
      packet = this._converter.ConvertToUnicode(packet);
      var parsed = JSON.parse(packet);
    } catch(e) {
      dumpn("Error parsing incoming packet: " + packet + " (" + e + " - " + e.stack + ")");
      return true;
    }

    try {
      dumpn("Got: " + packet);
      let thr = Cc["@mozilla.org/thread-manager;1"].getService().currentThread;
      let self = this;
      thr.dispatch({run: function() {
        self.hooks.onPacket(parsed);
      }}, 0);
    } catch(e) {
      dumpn("Error handling incoming packet: " + e + " - " + e.stack);
      dumpn("Packet was: " + packet);
    }

    return true;
  }
}
