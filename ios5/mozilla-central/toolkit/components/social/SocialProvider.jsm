/* -*- Mode: JavaScript; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/FrameWorker.jsm");
Cu.import("resource://gre/modules/WorkerAPI.jsm");

const EXPORTED_SYMBOLS = ["SocialProvider"];

/**
 * The SocialProvider object represents a social provider, and allows
 * access to its FrameWorker (if it has one).
 *
 * @constructor
 * @param {jsobj} object representing the manifest file describing this provider
 * @param {bool} whether the provider should be initially enabled (defaults to true)
 */
function SocialProvider(input, enabled) {
  if (!input.name)
    throw new Error("SocialProvider must be passed a name");
  if (!input.origin)
    throw new Error("SocialProvider must be passed an origin");

  this.name = input.name;
  this.workerURL = input.workerURL;
  this.origin = input.origin;

  // If enabled is |undefined|, default to true.
  this._enabled = !(enabled == false);
  if (this._enabled)
    this._activate();
}

SocialProvider.prototype = {
  // Provider enabled/disabled state. Disabled providers do not have active
  // connections to their FrameWorkers.
  _enabled: true,
  get enabled() {
    return this._enabled;
  },
  set enabled(val) {
    let enable = !!val;
    if (enable == this._enabled)
      return;

    this._enabled = enable;

    if (enable) {
      this._activate();
    } else {
      this._terminate();
    }
  },

  // Active port to the provider's FrameWorker. Null if the provider has no
  // FrameWorker, or is disabled.
  port: null,

  // Reference to a workerAPI object for this provider. Null if the provider has
  // no FrameWorker, or is disabled.
  workerAPI: null,

  // Internal helper methods
  _activate: function _activate() {
    // Initialize the workerAPI and its port first, so that its initialization
    // occurs before any other messages are processed by other ports.
    let workerAPIPort = this._getWorkerPort();
    if (workerAPIPort)
      this.workerAPI = new WorkerAPI(workerAPIPort);

    this.port = this._getWorkerPort();
  },

  _terminate: function _terminate() {
    if (this.workerURL) {
      try {
        getFrameWorkerHandle(this.workerURL, null).terminate();
      } catch (e) {
        Cu.reportError("SocialProvider FrameWorker termination failed: " + e);
      }
    }
    this.port = null;
    this.workerAPI = null;
  },

  /**
   * Instantiates a FrameWorker for the provider if one doesn't exist, and
   * returns a reference to a new port to that FrameWorker.
   *
   * Returns null if this provider has no workerURL, or is disabled.
   *
   * @param {DOMWindow} window (optional)
   */
  _getWorkerPort: function _getWorkerPort(window) {
    if (!this.workerURL || !this.enabled)
      return null;
    try {
      return getFrameWorkerHandle(this.workerURL, window).port;
    } catch (ex) {
      Cu.reportError("SocialProvider: retrieving worker port failed:" + ex);
      return null;
    }
  }
}
