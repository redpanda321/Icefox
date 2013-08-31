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

const TEST_URI = "data:text/html;charset=utf-8,<p id='gcli-input'>gcli-testSplit.js</p>";

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

// var mockCommands = require('gclitest/mockCommands');
var Requisition = require('gcli/cli').Requisition;
var canon = require('gcli/canon');

exports.setup = function() {
  mockCommands.setup();
};

exports.shutdown = function() {
  mockCommands.shutdown();
};

exports.testSplitSimple = function() {
  var args;
  var requ = new Requisition();

  args = requ._tokenize('s');
  requ._split(args);
  assert.is(0, args.length);
  assert.is('s', requ.commandAssignment.arg.text);
};

exports.testFlatCommand = function() {
  var args;
  var requ = new Requisition();

  args = requ._tokenize('tsv');
  requ._split(args);
  assert.is(0, args.length);
  assert.is('tsv', requ.commandAssignment.value.name);

  args = requ._tokenize('tsv a b');
  requ._split(args);
  assert.is('tsv', requ.commandAssignment.value.name);
  assert.is(2, args.length);
  assert.is('a', args[0].text);
  assert.is('b', args[1].text);
};

exports.testJavascript = function() {
  if (!canon.getCommand('{')) {
    assert.log('Skipping testJavascript because { is not registered');
    return;
  }

  var args;
  var requ = new Requisition();

  args = requ._tokenize('{');
  requ._split(args);
  assert.is(1, args.length);
  assert.is('', args[0].text);
  assert.is('', requ.commandAssignment.arg.text);
  assert.is('{', requ.commandAssignment.value.name);
};

// BUG 663081 - add tests for sub commands

// });
