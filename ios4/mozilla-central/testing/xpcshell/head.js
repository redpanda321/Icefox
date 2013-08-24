/* vim:set ts=2 sw=2 sts=2 et: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Google Inc.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Darin Fisher <darin@meer.net>
 *  Boris Zbarsky <bzbarsky@mit.edu>
 *  Jeff Walden <jwalden+code@mit.edu>
 *  Serge Gautherie <sgautherie.bz@free.fr>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/*
 * This file contains common code that is loaded before each test file(s).
 * See http://developer.mozilla.org/en/docs/Writing_xpcshell-based_unit_tests
 * for more information.
 */

var _quit = false;
var _passed = true;
var _tests_pending = 0;
var _passedChecks = 0, _falsePassedChecks = 0;
var _cleanupFunctions = [];
var _pendingTimers = [];

function _dump(str) {
  if (typeof _XPCSHELL_PROCESS == "undefined") {
    dump(str);
  } else {
    dump(_XPCSHELL_PROCESS + ": " + str);
  }
}

// Disable automatic network detection, so tests work correctly when
// not connected to a network.
let (ios = Components.classes["@mozilla.org/network/io-service;1"]
           .getService(Components.interfaces.nsIIOService2)) {
  ios.manageOfflineStatus = false;
  ios.offline = false;
}

// Enable crash reporting, if possible
// We rely on the Python harness to set MOZ_CRASHREPORTER_NO_REPORT
// and handle checking for minidumps.
// Note that if we're in a child process, we don't want to init the
// crashreporter component.
try { // nsIXULRuntime is not available in some configurations.
  let processType = Components.classes["@mozilla.org/xre/runtime;1"].
    getService(Components.interfaces.nsIXULRuntime).processType;
  if (processType == Components.interfaces.nsIXULRuntime.PROCESS_TYPE_DEFAULT &&
      "@mozilla.org/toolkit/crash-reporter;1" in Components.classes) {
    // Remember to update </toolkit/crashreporter/test/unit/test_crashreporter.js>
    // too if you change this initial setting.
    let (crashReporter =
          Components.classes["@mozilla.org/toolkit/crash-reporter;1"]
          .getService(Components.interfaces.nsICrashReporter)) {
      crashReporter.enabled = true;
      crashReporter.minidumpPath = do_get_cwd();
    }
  }
}
catch (e) { }

/**
 * Date.now() is not necessarily monotonically increasing (insert sob story
 * about times not being the right tool to use for measuring intervals of time,
 * robarnold can tell all), so be wary of error by erring by at least
 * _timerFuzz ms.
 */
const _timerFuzz = 15;

function _Timer(func, delay) {
  delay = Number(delay);
  if (delay < 0)
    do_throw("do_timeout() delay must be nonnegative");

  if (typeof func !== "function")
    do_throw("string callbacks no longer accepted; use a function!");

  this._func = func;
  this._start = Date.now();
  this._delay = delay;

  var timer = Components.classes["@mozilla.org/timer;1"]
                        .createInstance(Components.interfaces.nsITimer);
  timer.initWithCallback(this, delay + _timerFuzz, timer.TYPE_ONE_SHOT);

  // Keep timer alive until it fires
  _pendingTimers.push(timer);
}
_Timer.prototype = {
  QueryInterface: function(iid) {
    if (iid.Equals(Components.interfaces.nsITimerCallback) ||
        iid.Equals(Components.interfaces.nsISupports))
      return this;

    throw Components.results.NS_ERROR_NO_INTERFACE;
  },

  notify: function(timer) {
    _pendingTimers.splice(_pendingTimers.indexOf(timer), 1);

    // The current nsITimer implementation can undershoot, but even if it
    // couldn't, paranoia is probably a virtue here given the potential for
    // random orange on tinderboxen.
    var end = Date.now();
    var elapsed = end - this._start;
    if (elapsed >= this._delay) {
      try {
        this._func.call(null);
      } catch (e) {
        do_throw("exception thrown from do_timeout callback: " + e);
      }
      return;
    }

    // Timer undershot, retry with a little overshoot to try to avoid more
    // undershoots.
    var newDelay = this._delay - elapsed;
    do_timeout(newDelay, this._func);
  }
};

function _do_main() {
  if (_quit)
    return;

  _dump("TEST-INFO | (xpcshell/head.js) | running event loop\n");

  var thr = Components.classes["@mozilla.org/thread-manager;1"]
                      .getService().currentThread;

  while (!_quit)
    thr.processNextEvent(true);

  while (thr.hasPendingEvents())
    thr.processNextEvent(true);
}

function _do_quit() {
  _dump("TEST-INFO | (xpcshell/head.js) | exiting test\n");

  _quit = true;
}

function _dump_exception_stack(stack) {
  stack.split("\n").forEach(function(frame) {
    if (!frame)
      return;
    // frame is of the form "fname(args)@file:line"
    let frame_regexp = new RegExp("(.*)\\(.*\\)@(.*):(\\d*)", "g");
    let parts = frame_regexp.exec(frame);
    dump("JS frame :: " + parts[2] + " :: " + (parts[1] ? parts[1] : "anonymous") + " :: line " + parts[3] + "\n");
  });
}

function _execute_test() {
  // Map resource://test/ to the current working directory.
  let (ios = Components.classes["@mozilla.org/network/io-service;1"]
             .getService(Components.interfaces.nsIIOService)) {
    let protocolHandler =
      ios.getProtocolHandler("resource")
         .QueryInterface(Components.interfaces.nsIResProtocolHandler);
    let curDirURI = ios.newFileURI(do_get_cwd());
    protocolHandler.setSubstitution("test", curDirURI);
  }

  // _HEAD_FILES is dynamically defined by <runxpcshelltests.py>.
  _load_files(_HEAD_FILES);
  // _TEST_FILE is dynamically defined by <runxpcshelltests.py>.
  _load_files(_TEST_FILE);

  try {
    do_test_pending();
    run_test();
    do_test_finished();
    _do_main();
  } catch (e) {
    _passed = false;
    // do_check failures are already logged and set _quit to true and throw
    // NS_ERROR_ABORT. If both of those are true it is likely this exception
    // has already been logged so there is no need to log it again. It's
    // possible that this will mask an NS_ERROR_ABORT that happens after a
    // do_check failure though.
    if (!_quit || e != Components.results.NS_ERROR_ABORT) {
      _dump("TEST-UNEXPECTED-FAIL | (xpcshell/head.js) | " + e);
      if (e.stack) {
        _dump(" - See following stack:\n");
        _dump_exception_stack(e.stack);
      }
      else {
        _dump("\n");
      }
    }
  }

  // _TAIL_FILES is dynamically defined by <runxpcshelltests.py>.
  _load_files(_TAIL_FILES);

  // Execute all of our cleanup functions.
  var func;
  while ((func = _cleanupFunctions.pop()))
    func();

  if (!_passed)
    return;

  var truePassedChecks = _passedChecks - _falsePassedChecks;
  if (truePassedChecks > 0) {
    _dump("TEST-PASS | (xpcshell/head.js) | " + truePassedChecks + " (+ " +
            _falsePassedChecks + ") check(s) passed\n");
  } else {
    // ToDo: switch to TEST-UNEXPECTED-FAIL when all tests have been updated. (Bug 496443)
    _dump("TEST-INFO | (xpcshell/head.js) | No (+ " + _falsePassedChecks + ") checks actually run\n");
  }
}

/**
 * Loads files.
 *
 * @param aFiles Array of files to load.
 */
function _load_files(aFiles) {
  function loadTailFile(element, index, array) {
    load(element);
  }

  aFiles.forEach(loadTailFile);
}


/************** Functions to be used from the tests **************/

/**
 * Calls the given function at least the specified number of milliseconds later.
 * The callback will not undershoot the given time, but it might overshoot --
 * don't expect precision!
 *
 * @param delay : uint
 *   the number of milliseconds to delay
 * @param callback : function() : void
 *   the function to call
 */
function do_timeout(delay, func) {
  new _Timer(func, Number(delay));
}

function do_execute_soon(callback) {
  do_test_pending();
  var tm = Components.classes["@mozilla.org/thread-manager;1"]
                     .getService(Components.interfaces.nsIThreadManager);

  tm.mainThread.dispatch({
    run: function() {
      try {
        callback();
      } catch (e) {
        // do_check failures are already logged and set _quit to true and throw
        // NS_ERROR_ABORT. If both of those are true it is likely this exception
        // has already been logged so there is no need to log it again. It's
        // possible that this will mask an NS_ERROR_ABORT that happens after a
        // do_check failure though.
        if (!_quit || e != Components.results.NS_ERROR_ABORT) {
          dump("TEST-UNEXPECTED-FAIL | (xpcshell/head.js) | " + e);
          if (e.stack) {
            dump(" - See following stack:\n");
            _dump_exception_stack(e.stack);
          }
          else {
            dump("\n");
          }
          _do_quit();
        }
      }
      finally {
        do_test_finished();
      }
    }
  }, Components.interfaces.nsIThread.DISPATCH_NORMAL);
}

function do_throw(text, stack) {
  if (!stack)
    stack = Components.stack.caller;

  _passed = false;
  _dump("TEST-UNEXPECTED-FAIL | " + stack.filename + " | " + text +
         " - See following stack:\n");
  var frame = Components.stack;
  while (frame != null) {
    _dump(frame + "\n");
    frame = frame.caller;
  }

  _do_quit();
  throw Components.results.NS_ERROR_ABORT;
}

function do_check_neq(left, right, stack) {
  if (!stack)
    stack = Components.stack.caller;

  var text = left + " != " + right;
  if (left == right) {
    do_throw(text, stack);
  } else {
    ++_passedChecks;
    _dump("TEST-PASS | " + stack.filename + " | [" + stack.name + " : " +
         stack.lineNumber + "] " + text + "\n");
  }
}

function do_check_eq(left, right, stack) {
  if (!stack)
    stack = Components.stack.caller;

  var text = left + " == " + right;
  if (left != right) {
    do_throw(text, stack);
  } else {
    ++_passedChecks;
    _dump("TEST-PASS | " + stack.filename + " | [" + stack.name + " : " +
         stack.lineNumber + "] " + text + "\n");
  }
}

function do_check_true(condition, stack) {
  if (!stack)
    stack = Components.stack.caller;

  do_check_eq(condition, true, stack);
}

function do_check_false(condition, stack) {
  if (!stack)
    stack = Components.stack.caller;

  do_check_eq(condition, false, stack);
}

function do_test_pending() {
  ++_tests_pending;

  _dump("TEST-INFO | (xpcshell/head.js) | test " + _tests_pending +
         " pending\n");
}

function do_test_finished() {
  _dump("TEST-INFO | (xpcshell/head.js) | test " + _tests_pending +
         " finished\n");

  if (--_tests_pending == 0)
    _do_quit();
}

function do_get_file(path, allowNonexistent) {
  try {
    let lf = Components.classes["@mozilla.org/file/directory_service;1"]
      .getService(Components.interfaces.nsIProperties)
      .get("CurWorkD", Components.interfaces.nsILocalFile);

    let bits = path.split("/");
    for (let i = 0; i < bits.length; i++) {
      if (bits[i]) {
        if (bits[i] == "..")
          lf = lf.parent;
        else
          lf.append(bits[i]);
      }
    }

    if (!allowNonexistent && !lf.exists()) {
      // Not using do_throw(): caller will continue.
      _passed = false;
      var stack = Components.stack.caller;
      _dump("TEST-UNEXPECTED-FAIL | " + stack.filename + " | [" +
             stack.name + " : " + stack.lineNumber + "] " + lf.path +
             " does not exist\n");
    }

    return lf;
  }
  catch (ex) {
    do_throw(ex.toString(), Components.stack.caller);
  }

  return null;
}

// do_get_cwd() isn't exactly self-explanatory, so provide a helper
function do_get_cwd() {
  return do_get_file("");
}

/**
 * Loads _HTTPD_JS_PATH file, which is dynamically defined by
 * <runxpcshelltests.py>.
 */
function do_load_httpd_js() {
  load(_HTTPD_JS_PATH);
}

function do_load_manifest(path) {
  var lf = do_get_file(path);
  const nsIComponentRegistrar = Components.interfaces.nsIComponentRegistrar;
  do_check_true(Components.manager instanceof nsIComponentRegistrar);
  // Previous do_check_true() is not a test check.
  ++_falsePassedChecks;
  Components.manager.autoRegister(lf);
}

/**
 * Parse a DOM document.
 *
 * @param aPath File path to the document.
 * @param aType Content type to use in DOMParser.
 *
 * @return nsIDOMDocument from the file.
 */
function do_parse_document(aPath, aType) {
  switch (aType) {
    case "application/xhtml+xml":
    case "application/xml":
    case "text/xml":
      break;

    default:
      do_throw("type: expected application/xhtml+xml, application/xml or text/xml," +
                 " got '" + aType + "'",
               Components.stack.caller);
  }

  var lf = do_get_file(aPath);
  const C_i = Components.interfaces;
  const parserClass = "@mozilla.org/xmlextras/domparser;1";
  const streamClass = "@mozilla.org/network/file-input-stream;1";
  var stream = Components.classes[streamClass]
                         .createInstance(C_i.nsIFileInputStream);
  stream.init(lf, -1, -1, C_i.nsIFileInputStream.CLOSE_ON_EOF);
  var parser = Components.classes[parserClass]
                         .createInstance(C_i.nsIDOMParser);
  var doc = parser.parseFromStream(stream, null, lf.fileSize, aType);
  parser = null;
  stream = null;
  lf = null;
  return doc;
}

/**
 * Registers a function that will run when the test harness is done running all
 * tests.
 *
 * @param aFunction
 *        The function to be called when the test harness has finished running.
 */
function do_register_cleanup(aFunction)
{
  _cleanupFunctions.push(aFunction);
}

/**
 * Registers a directory with the profile service,
 * and return the directory as an nsILocalFile.
 *
 * @return nsILocalFile of the profile directory.
 */
function do_get_profile() {
  // Since we have a profile, we will notify profile shutdown topics at
  // the end of the current test, to ensure correct cleanup on shutdown.
  do_register_cleanup(function() {
    let obsSvc = Components.classes["@mozilla.org/observer-service;1"].
                 getService(Components.interfaces.nsIObserverService);
    obsSvc.notifyObservers(null, "profile-change-net-teardown", null);
    obsSvc.notifyObservers(null, "profile-change-teardown", null);
    obsSvc.notifyObservers(null, "profile-before-change", null);
  });

  let env = Components.classes["@mozilla.org/process/environment;1"]
                      .getService(Components.interfaces.nsIEnvironment);
  // the python harness sets this in the environment for us
  let profd = env.get("XPCSHELL_TEST_PROFILE_DIR");
  let file = Components.classes["@mozilla.org/file/local;1"]
                       .createInstance(Components.interfaces.nsILocalFile);
  file.initWithPath(profd);

  let dirSvc = Components.classes["@mozilla.org/file/directory_service;1"]
                         .getService(Components.interfaces.nsIProperties);
  let provider = {
    getFile: function(prop, persistent) {
      persistent.value = true;
      if (prop == "ProfD" || prop == "ProfLD" || prop == "ProfDS") {
        return file.clone();
      }
      throw Components.results.NS_ERROR_FAILURE;
    },
    QueryInterface: function(iid) {
      if (iid.equals(Components.interfaces.nsIDirectoryServiceProvider) ||
          iid.equals(Components.interfaces.nsISupports)) {
        return this;
      }
      throw Components.results.NS_ERROR_NO_INTERFACE;
    }
  };
  dirSvc.QueryInterface(Components.interfaces.nsIDirectoryService)
        .registerProvider(provider);
  return file.clone();
}

/**
 * This function loads head.js (this file) in the child process, so that all
 * functions defined in this file (do_throw, etc) are available to subsequent
 * sendCommand calls.  It also sets various constants used by these functions.
 *
 * (Note that you may use sendCommand without calling this function first;  you
 * simply won't have any of the functions in this file available.)
 */
function do_load_child_test_harness()
{
  // Make sure this isn't called from child process
  var runtime = Components.classes["@mozilla.org/xre/app-info;1"]
                  .getService(Components.interfaces.nsIXULRuntime);
  if (runtime.processType != 
            Components.interfaces.nsIXULRuntime.PROCESS_TYPE_DEFAULT) 
  {
    do_throw("run_test_in_child cannot be called from child!");
  }

  // Allow to be called multiple times, but only run once
  if (typeof do_load_child_test_harness.alreadyRun != "undefined")
    return;
  do_load_child_test_harness.alreadyRun = 1;
  
  function addQuotes (str)  { 
    return '"' + str + '"'; 
  }
  var quoted_head_files = _HEAD_FILES.map(addQuotes);
  var quoted_tail_files = _TAIL_FILES.map(addQuotes);

  _XPCSHELL_PROCESS = "parent";
 
  sendCommand(
        "const _HEAD_JS_PATH='" + _HEAD_JS_PATH + "'; "
      + "const _HTTPD_JS_PATH='" + _HTTPD_JS_PATH + "'; "
      + "const _HEAD_FILES=[" + quoted_head_files.join() + "];"
      + "const _TAIL_FILES=[" + quoted_tail_files.join() + "];"
      + "const _XPCSHELL_PROCESS='child';"
      + "load(_HEAD_JS_PATH);");
}

/**
 * Runs an entire xpcshell unit test in a child process (rather than in chrome,
 * which is the default).
 *
 * This function returns immediately, before the test has completed.  
 *
 * @param testFile
 *        The name of the script to run.  Path format same as load().
 * @param optionalCallback.
 *        Optional function to be called (in parent) when test on child is
 *        complete.  If provided, the function must call do_test_finished();
 */
function run_test_in_child(testFile, optionalCallback) 
{
  var callback = (typeof optionalCallback == 'undefined') ? 
                    do_test_finished : optionalCallback;

  do_load_child_test_harness();

  var testPath = do_get_file(testFile).path.replace(/\\/g, "/");
  do_test_pending();
  sendCommand("const _TEST_FILE=['" + testPath + "']; _execute_test();", 
              callback);
}

