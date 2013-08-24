/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

let tempScope = {};
Components.utils.import("resource:///modules/devtools/TiltGL.jsm", tempScope);
Components.utils.import("resource:///modules/devtools/TiltMath.jsm", tempScope);
Components.utils.import("resource:///modules/devtools/TiltUtils.jsm", tempScope);
Components.utils.import("resource:///modules/devtools/TiltVisualizer.jsm", tempScope);
Components.utils.import("resource:///modules/devtools/LayoutHelpers.jsm", tempScope);
let TiltGL = tempScope.TiltGL;
let EPSILON = tempScope.EPSILON;
let TiltMath = tempScope.TiltMath;
let vec3 = tempScope.vec3;
let mat3 = tempScope.mat3;
let mat4 = tempScope.mat4;
let quat4 = tempScope.quat4;
let TiltUtils = tempScope.TiltUtils;
let TiltVisualizer = tempScope.TiltVisualizer;
let LayoutHelpers = tempScope.LayoutHelpers;


const DEFAULT_HTML = "data:text/html," +
  "<DOCTYPE html>" +
  "<html>" +
    "<head>" +
      "<meta charset='utf-8'/>" +
      "<title>Three Laws</title>" +
    "</head>" +
    "<body>" +
      "<div id='first-law'>" +
        "A robot may not injure a human being or, through inaction, allow a " +
        "human being to come to harm." +
      "</div>" +
      "<div>" +
        "A robot must obey the orders given to it by human beings, except " +
        "where such orders would conflict with the First Law." +
      "</div>" +
      "<div>" +
        "A robot must protect its own existence as long as such protection " +
        "does not conflict with the First or Second Laws." +
      "</div>" +
      "<div id='far-far-away' style='position: absolute; top: 250%;'>" +
        "I like bacon." +
      "</div>" +
    "<body>" +
  "</html>";

const INSPECTOR_OPENED = InspectorUI.INSPECTOR_NOTIFICATIONS.OPENED;
const INSPECTOR_CLOSED = InspectorUI.INSPECTOR_NOTIFICATIONS.CLOSED;

const INITIALIZING = Tilt.NOTIFICATIONS.INITIALIZING;
const INITIALIZED = Tilt.NOTIFICATIONS.INITIALIZED;
const DESTROYING = Tilt.NOTIFICATIONS.DESTROYING;
const BEFORE_DESTROYED = Tilt.NOTIFICATIONS.BEFORE_DESTROYED;
const DESTROYED = Tilt.NOTIFICATIONS.DESTROYED;
const SHOWN = Tilt.NOTIFICATIONS.SHOWN;
const HIDDEN = Tilt.NOTIFICATIONS.HIDDEN;
const HIGHLIGHTING = Tilt.NOTIFICATIONS.HIGHLIGHTING;
const UNHIGHLIGHTING = Tilt.NOTIFICATIONS.UNHIGHLIGHTING;
const NODE_REMOVED = Tilt.NOTIFICATIONS.NODE_REMOVED;

const TILT_ENABLED = Services.prefs.getBoolPref("devtools.tilt.enabled");
const INSP_ENABLED = Services.prefs.getBoolPref("devtools.inspector.enabled");


function isTiltEnabled() {
  let enabled = TILT_ENABLED && INSP_ENABLED;

  info("Apparently, Tilt is" + (enabled ? "" : " not") + " enabled.");
  return enabled;
}

function isWebGLSupported() {
  let supported = !TiltGL.isWebGLForceEnabled() &&
                   TiltGL.isWebGLSupported() &&
                   TiltGL.create3DContext(createCanvas());

  info("Apparently, WebGL is" + (supported ? "" : " not") + " supported.");
  return supported;
}

function isApprox(num1, num2, delta) {
  if (Math.abs(num1 - num2) > (delta || EPSILON)) {
    info("isApprox expected " + num1 + ", got " + num2 + " instead.");
    return false;
  }
  return true;
}

function isApproxVec(vec1, vec2, delta) {
  vec1 = Array.prototype.slice.call(vec1);
  vec2 = Array.prototype.slice.call(vec2);

  if (vec1.length !== vec2.length) {
    return false;
  }
  for (let i = 0, len = vec1.length; i < len; i++) {
    if (!isApprox(vec1[i], vec2[i], delta)) {
      info("isApproxVec expected [" + vec1 + "], got [" + vec2 + "] instead.");
      return false;
    }
  }
  return true;
}

function isEqualVec(vec1, vec2) {
  vec1 = Array.prototype.slice.call(vec1);
  vec2 = Array.prototype.slice.call(vec2);

  if (vec1.length !== vec2.length) {
    return false;
  }
  for (let i = 0, len = vec1.length; i < len; i++) {
    if (vec1[i] !== vec2[i]) {
      info("isEqualVec expected [" + vec1 + "], got [" + vec2 + "] instead.");
      return false;
    }
  }
  return true;
}

function createCanvas() {
  return document.createElementNS("http://www.w3.org/1999/xhtml", "canvas");
}


function createTab(callback, location) {
  info("Creating a tab, with callback " + typeof callback +
                      ", and location " + location + ".");

  let tab = gBrowser.selectedTab = gBrowser.addTab();

  gBrowser.selectedBrowser.addEventListener("load", function onLoad() {
    gBrowser.selectedBrowser.removeEventListener("load", onLoad, true);
    callback(tab);
  }, true);

  gBrowser.selectedBrowser.contentWindow.location = location || DEFAULT_HTML;
  return tab;
}


function createTilt(callbacks, close, suddenDeath) {
  info("Creating Tilt, with callbacks {" + Object.keys(callbacks) + "}" +
                   ", autoclose param " + close +
          ", and sudden death handler " + typeof suddenDeath + ".");

  Services.prefs.setBoolPref("webgl.verbose", true);
  TiltUtils.Output.suppressAlerts = true;

  info("Attempting to start the inspector.");
  Services.obs.addObserver(onInspectorOpen, INSPECTOR_OPENED, false);
  InspectorUI.toggleInspectorUI();

  function onInspectorOpen() {
    info("Inspector was opened.");
    Services.obs.removeObserver(onInspectorOpen, INSPECTOR_OPENED);

    executeSoon(function() {
      if ("function" === typeof callbacks.onInspectorOpen) {
        info("Calling 'onInspectorOpen'.");
        callbacks.onInspectorOpen();
      }
      executeSoon(function() {
        info("Attempting to start Tilt.");
        Services.obs.addObserver(onTiltOpen, INITIALIZING, false);
        handleFailure(suddenDeath);
        Tilt.initialize();
      });
    });
  }

  function onTiltOpen() {
    info("Tilt was opened.");
    Services.obs.removeObserver(onTiltOpen, INITIALIZING);

    executeSoon(function() {
      if ("function" === typeof callbacks.onTiltOpen) {
        info("Calling 'onTiltOpen'.");
        callbacks.onTiltOpen(Tilt.visualizers[Tilt.currentWindowId]);
      }
      if (close) {
        executeSoon(function() {
          info("Attempting to close Tilt.");
          Services.obs.addObserver(onTiltClose, DESTROYED, false);
          Tilt.destroy(Tilt.currentWindowId);
        });
      }
    });
  }

  function onTiltClose() {
    info("Tilt was closed.");
    Services.obs.removeObserver(onTiltClose, DESTROYED);

    executeSoon(function() {
      if ("function" === typeof callbacks.onTiltClose) {
        info("Calling 'onTiltClose'.");
        callbacks.onTiltClose();
      }
      if (close) {
        executeSoon(function() {
          info("Attempting to close the Inspector.");
          Services.obs.addObserver(onInspectorClose, INSPECTOR_CLOSED, false);
          InspectorUI.closeInspectorUI();
        });
      }
    });
  }

  function onInspectorClose() {
    info("Inspector was closed.");
    Services.obs.removeObserver(onInspectorClose, INSPECTOR_CLOSED);

    executeSoon(function() {
      if ("function" === typeof callbacks.onInspectorClose) {
        info("Calling 'onInspectorClose'.");
        callbacks.onInspectorClose();
      }
      if ("function" === typeof callbacks.onEnd) {
        info("Calling 'onEnd'.");
        callbacks.onEnd();
      }
    });
  }

  function handleFailure(suddenDeath) {
    Tilt.failureCallback = function() {
      info("Tilt FAIL.");
      Services.obs.removeObserver(onTiltOpen, INITIALIZING);

      info("Now relying on sudden death handler " + typeof suddenDeath + ".");
      suddenDeath && suddenDeath();
    }
  }
}

function getPickablePoint(presenter) {
  let vertices = presenter._meshStacks[0].vertices.components;

  let topLeft = vec3.create([vertices[0], vertices[1], vertices[2]]);
  let bottomRight = vec3.create([vertices[6], vertices[7], vertices[8]]);
  let center = vec3.lerp(topLeft, bottomRight, 0.5, []);

  let renderer = presenter._renderer;
  let viewport = [0, 0, renderer.width, renderer.height];

  return vec3.project(center, viewport, renderer.mvMatrix, renderer.projMatrix);
}
