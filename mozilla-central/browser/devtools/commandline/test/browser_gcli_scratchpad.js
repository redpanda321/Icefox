/*
 * Copyright 2012, Mozilla Foundation and contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// define(function(require, exports, module) {

// <INJECTED SOURCE:START>

// THIS FILE IS GENERATED FROM SOURCE IN THE GCLI PROJECT
// DO NOT EDIT IT DIRECTLY

var exports = {};

const TEST_URI = "data:text/html;charset=utf-8,<p id='gcli-input'>gcli-testScratchpad.js</p>";

function test() {
  var tests = Object.keys(exports);
  // Push setup to the top and shutdown to the bottom
  tests.sort(function(t1, t2) {
    if (t1 == "setup" || t2 == "shutdown") return -1;
    if (t2 == "setup" || t1 == "shutdown") return 1;
    return 0;
  });
  info("Running tests: " + tests.join(", "))
  tests = tests.map(function(test) { return exports[test]; });
  DeveloperToolbarTest.test(TEST_URI, tests, true);
}

// <INJECTED SOURCE:END>


// var assert = require('test/assert');

var origScratchpad;

exports.setup = function(options) {
  if (options.display) {
    origScratchpad = options.display.inputter.scratchpad;
    options.display.inputter.scratchpad = stubScratchpad;
  }
};

exports.shutdown = function(options) {
  if (options.display) {
    options.display.inputter.scratchpad = origScratchpad;
  }
};

var stubScratchpad = {
  shouldActivate: function(ev) {
    return true;
  },
  activatedCount: 0,
  linkText: 'scratchpad.linkText'
};
stubScratchpad.activate = function(value) {
  stubScratchpad.activatedCount++;
  return true;
};


exports.testActivate = function(options) {
  if (!options.display) {
    assert.log('No display. Skipping scratchpad tests');
    return;
  }

  var ev = {};
  stubScratchpad.activatedCount = 0;
  options.display.inputter.onKeyUp(ev);
  assert.is(1, stubScratchpad.activatedCount, 'scratchpad is activated');
};


// });
