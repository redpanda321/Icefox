/* -*- Mode: javascript; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const Cu = Components.utils;

// Tilt notifications dispatched through the nsIObserverService.
const TILT_NOTIFICATIONS = {

  // Fires when Tilt starts the initialization.
  INITIALIZING: "tilt-initializing",

  // Fires immediately after initialization is complete.
  // (when the canvas overlay is visible and the 3D mesh is completely created)
  INITIALIZED: "tilt-initialized",

  // Fires immediately before the destruction is started.
  DESTROYING: "tilt-destroying",

  // Fires immediately before the destruction is finished.
  // (just before the canvas overlay is removed from its parent node)
  BEFORE_DESTROYED: "tilt-before-destroyed",

  // Fires when Tilt is completely destroyed.
  DESTROYED: "tilt-destroyed",

  // Fires when Tilt is shown (after a tab-switch).
  SHOWN: "tilt-shown",

  // Fires when Tilt is hidden (after a tab-switch).
  HIDDEN: "tilt-hidden",

  // Fires once Tilt highlights an element in the page.
  HIGHLIGHTING: "tilt-highlighting",

  // Fires once Tilt stops highlighting any element.
  UNHIGHLIGHTING: "tilt-unhighlighting",

  // Fires when a node is removed from the 3D mesh.
  NODE_REMOVED: "tilt-node-removed"
};

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource:///modules/devtools/TiltGL.jsm");
Cu.import("resource:///modules/devtools/TiltUtils.jsm");
Cu.import("resource:///modules/devtools/TiltVisualizer.jsm");

this.EXPORTED_SYMBOLS = ["TiltManager"];

this.TiltManager = {
  _instances: new WeakMap(),
  getTiltForBrowser: function(aChromeWindow)
  {
    if (this._instances.has(aChromeWindow)) {
      return this._instances.get(aChromeWindow);
    } else {
      let tilt = new Tilt(aChromeWindow);
      this._instances.set(aChromeWindow, tilt);
      return tilt;
    }
  },
}

/**
 * Object managing instances of the visualizer.
 *
 * @param {Window} aWindow
 *                 the chrome window used by each visualizer instance
 */
this.Tilt = function Tilt(aWindow)
{
  /**
   * Save a reference to the top-level window.
   */
  this.chromeWindow = aWindow;

  /**
   * All the instances of TiltVisualizer.
   */
  this.visualizers = {};

  /**
   * Shortcut for accessing notifications strings.
   */
  this.NOTIFICATIONS = TILT_NOTIFICATIONS;

  this.setup();
}

Tilt.prototype = {

  /**
   * Initializes a visualizer for the current tab or closes it if already open.
   */
  toggle: function T_toggle()
  {
    let contentWindow = this.chromeWindow.gBrowser.selectedBrowser.contentWindow;
    let id = this.currentWindowId;
    let self = this;

    contentWindow.addEventListener("beforeunload", function onUnload() {
      contentWindow.removeEventListener("beforeunload", onUnload, false);
      self.destroy(id, true);
    }, false);

    // if the visualizer for the current tab is already open, destroy it now
    if (this.visualizers[id]) {
      this.destroy(id, true);
      return;
    }

    // create a visualizer instance for the current tab
    this.visualizers[id] = new TiltVisualizer({
      chromeWindow: this.chromeWindow,
      contentWindow: this.chromeWindow.gBrowser.selectedBrowser.contentWindow,
      parentNode: this.chromeWindow.gBrowser.selectedBrowser.parentNode,
      notifications: this.NOTIFICATIONS,
      tab: this.chromeWindow.gBrowser.selectedTab
    });

    // make sure the visualizer object was initialized properly
    if (!this.visualizers[id].isInitialized()) {
      this.destroy(id);
      this.failureCallback && this.failureCallback();
      return;
    }

    Services.obs.notifyObservers(null, TILT_NOTIFICATIONS.INITIALIZING, null);
  },

  /**
   * Starts destroying a specific instance of the visualizer.
   *
   * @param {String} aId
   *                 the identifier of the instance in the visualizers array
   * @param {Boolean} aAnimateFlag
   *                  optional, set to true to display a destruction transition
   */
  destroy: function T_destroy(aId, aAnimateFlag)
  {
    // if the visualizer is destroyed or destroying, don't do anything
    if (!this.visualizers[aId] || this._isDestroying) {
      return;
    }
    this._isDestroying = true;

    let controller = this.visualizers[aId].controller;
    let presenter = this.visualizers[aId].presenter;

    let content = presenter.contentWindow;
    let pageXOffset = content.pageXOffset * presenter.transforms.zoom;
    let pageYOffset = content.pageYOffset * presenter.transforms.zoom;
    TiltUtils.setDocumentZoom(this.chromeWindow, presenter.transforms.zoom);

    // if we're not doing any outro animation, just finish destruction directly
    if (!aAnimateFlag) {
      this._finish(aId);
      return;
    }

    // otherwise, trigger the outro animation and notify necessary observers
    Services.obs.notifyObservers(null, TILT_NOTIFICATIONS.DESTROYING, null);

    controller.removeEventListeners();
    controller.arcball.reset([-pageXOffset, -pageYOffset]);
    presenter.executeDestruction(this._finish.bind(this, aId));
  },

  /**
   * Finishes detroying a specific instance of the visualizer.
   *
   * @param {String} aId
   *                 the identifier of the instance in the visualizers array
   */
  _finish: function T__finish(aId)
  {
    this.visualizers[aId].removeOverlay();
    this.visualizers[aId].cleanup();
    this.visualizers[aId] = null;

    this._isDestroying = false;
    this.chromeWindow.gBrowser.selectedBrowser.focus();
    Services.obs.notifyObservers(null, TILT_NOTIFICATIONS.DESTROYED, null);
  },

  /**
   * Handles the event fired when a tab is selected.
   */
  _onTabSelect: function T__onTabSelect()
  {
    if (this.currentInstance) {
      Services.obs.notifyObservers(null, TILT_NOTIFICATIONS.SHOWN, null);
    } else {
      Services.obs.notifyObservers(null, TILT_NOTIFICATIONS.HIDDEN, null);
    }
  },

  /**
   * Add the browser event listeners to handle state changes.
   */
  setup: function T_setup()
  {
    // load the preferences from the devtools.tilt branch
    TiltVisualizer.Prefs.load();

    this.chromeWindow.gBrowser.tabContainer.addEventListener(
      "TabSelect", this._onTabSelect.bind(this), false);
  },

  /**
   * Returns true if this tool is enabled.
   */
  get enabled()
  {
    return (TiltVisualizer.Prefs.enabled &&
           (TiltGL.isWebGLForceEnabled() || TiltGL.isWebGLSupported()));
  },

  /**
   * Gets the ID of the current window object to identify the visualizer.
   */
  get currentWindowId()
  {
    return TiltUtils.getWindowId(
      this.chromeWindow.gBrowser.selectedBrowser.contentWindow);
  },

  /**
   * Gets the visualizer instance for the current tab.
   */
  get currentInstance()
  {
    return this.visualizers[this.currentWindowId];
  },
};
