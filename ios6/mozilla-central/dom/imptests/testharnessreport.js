/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

var W3CTest = {
  /**
   * Dictionary mapping a test URL to either the string "all", which means that
   * all tests in this file are expected to fail, or a dictionary mapping test
   * names to either the boolean |true|, or the string "debug". The former
   * means that this test is expected to fail in all builds, and the latter
   * that it is only expected to fail in debug builds.
   */
  "expectedFailures": {},

  /**
   * If set to true, we will dump the test failures to the console.
   */
  "dumpFailures": false,

  /**
   * If dumpFailures is true, this holds a structure like necessary for
   * expectedFailures, for ease of updating the expectations.
   */
  "failures": {},

  /**
   * List of test results, needed by TestRunner to update the UI.
   */
  "tests": [],

  /**
   * Number of unlogged passes, to stop buildbot from truncating the log.
   * We will print a message every MAX_COLLAPSED_MESSAGES passes.
   */
  "collapsedMessages": 0,
  "MAX_COLLAPSED_MESSAGES": 100,

  /**
   * Reference to the TestRunner object in the parent frame.
   */
  "runner": parent === this ? null : parent.TestRunner || parent.wrappedJSObject.TestRunner,

  /**
   * Prefixes for the error logging. Indexed first by int(todo) and second by
   * int(result).
   */
  "prefixes": [
    ["TEST-UNEXPECTED-FAIL", "TEST-PASS"],
    ["TEST-KNOWN-FAIL", "TEST-UNEXPECTED-PASS"]
  ],

  /**
   * Returns the URL of the current test, relative to the root W3C tests
   * directory. Used as a key into the expectedFailures dictionary.
   */
  "getURL": function() {
    return this.runner.currentTestURL.substring("/tests/dom/imptests/".length);
  },

  /**
   * Lets the test runner know about a test result.
   */
  "_log": function(test) {
    var msg = this.prefixes[+test.todo][+test.result] + " | ";
    if (this.runner.currentTestURL) {
      msg += this.runner.currentTestURL;
    }
    msg += " | " + test.message;
    this.runner[(test.result === !test.todo) ? "log" : "error"](msg);
  },

  "_logCollapsedMessages": function() {
    if (this.collapsedMessages) {
      this._log({
        "result": true,
        "todo": false,
        "message": "Elided " + this.collapsedMessages + " passes or known failures."
      });
    }
    this.collapsedMessages = 0;
  },

  /**
   * Maybe logs a result, eliding up to MAX_COLLAPSED_MESSAGES consecutive
   * passes.
   */
  "_maybeLog": function(test) {
    var success = (test.result === !test.todo);
    if (success && ++this.collapsedMessages < this.MAX_COLLAPSED_MESSAGES) {
      return;
    }
    this._logCollapsedMessages();
    this._log(test);
  },

  /**
   * Reports a test result. The argument is an object with the following
   * properties:
   *
   * o message (string): message to be reported
   * o result (boolean): whether this test failed
   * o todo (boolean): whether this test is expected to fail
   */
  "report": function(test) {
    this.tests.push(test);
    this._maybeLog(test);
  },

  /**
   * Returns true if this test is expected to fail, and false otherwise.
   */
  "_todo": function(test) {
    if (this.expectedFailures === "all") {
      return true;
    }
    var value = this.expectedFailures[test.name];
    return value === true || (value === "debug" && !!SpecialPowers.isDebugBuild);
  },

  /**
   * Callback function for testharness.js. Called when one test in a file
   * finishes.
   */
  "result": function(test) {
    var url = this.getURL();
    this.report({
      "message": test.name + (test.message ? "; " + test.message : ""),
      "result": test.status === test.PASS,
      "todo": this._todo(test)
    });
    if (this.dumpFailures && test.status !== test.PASS) {
      this.failures[test.name] = true;
    }
  },

  /**
   * Callback function for testharness.js. Called when the entire test file
   * finishes.
   */
  "finish": function(tests, status) {
    var url = this.getURL();
    this.report({
      "message": "Finished test, status " + status.status,
      "result": status.status === status.OK,
      "todo":
        url in this.expectedFailures &&
        this.expectedFailures[url] === "error"
    });

    this._logCollapsedMessages();

    if (this.dumpFailures) {
      dump("@@@ @@@ Failures\n");
      dump(url + "@@@" + JSON.stringify(this.failures) + "\n");
    }
    this.runner.testFinished(this.tests);
  },

  /**
   * Log an unexpected failure. Intended to be used from harness code, not
   * from tests.
   */
  "logFailure": function(message) {
    this.report({
      "message": message,
      "result": false,
      "todo": false
    });
  },

  /**
   * Timeout the current test. Intended to be used from harness code, not
   * from tests.
   */
  "timeout": function() {
    this.logFailure("Test runner timed us out.");
    timeout();
  }
};
(function() {
  try {
    if (!W3CTest.runner) {
      return;
    }
    // Get expected fails.  If there aren't any, there will be a 404, which is
    // fine.  Anything else is unexpected.
    var request = new XMLHttpRequest();
    request.open("GET", "/tests/dom/imptests/failures/" + W3CTest.getURL() + ".json", false);
    request.send();
    if (request.status === 200) {
      W3CTest.expectedFailures = JSON.parse(request.responseText);
    } else if (request.status !== 404) {
      W3CTest.logFailure("Request status was " + request.status);
    }

    add_result_callback(W3CTest.result.bind(W3CTest));
    add_completion_callback(W3CTest.finish.bind(W3CTest));
    setup({
      "output": false,
      "explicit_timeout": true
    });
  } catch (e) {
    W3CTest.logFailure("Unexpected exception: " + e);
  }
})();
