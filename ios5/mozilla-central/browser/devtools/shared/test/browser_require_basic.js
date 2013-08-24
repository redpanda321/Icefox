/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests that source URLs in the Web Console can be clicked to display the
// standard View Source window.

let [ define, require ] = (function() {
  let tempScope = {};
  Components.utils.import("resource:///modules/devtools/Require.jsm", tempScope);
  return [ tempScope.define, tempScope.require ];
})();

function test() {
  addTab("about:blank", function() {
    info("Starting Require Tests");
    setup();

    testWorking();
    testDomains();
    testLeakage();
    testMultiImport();
    testRecursive();
    testUncompilable();
    testFirebug();

    shutdown();
  });
}

function setup() {
  define('gclitest/requirable', [ 'require', 'exports', 'module' ], function(require, exports, module) {
    exports.thing1 = 'thing1';
    exports.thing2 = 2;

    let status = 'initial';
    exports.setStatus = function(aStatus) { status = aStatus; };
    exports.getStatus = function() { return status; };
  });

  define('gclitest/unrequirable', [ 'require', 'exports', 'module' ], function(require, exports, module) {
    null.throwNPE();
  });

  define('gclitest/recurse', [ 'require', 'exports', 'module', 'gclitest/recurse' ], function(require, exports, module) {
    require('gclitest/recurse');
  });

  define('gclitest/firebug', [ 'gclitest/requirable' ], function(requirable) {
    return { requirable: requirable, fb: true };
  });
}

function shutdown() {
  delete define.modules['gclitest/requirable'];
  delete define.globalDomain.modules['gclitest/requirable'];
  delete define.modules['gclitest/unrequirable'];
  delete define.globalDomain.modules['gclitest/unrequirable'];
  delete define.modules['gclitest/recurse'];
  delete define.globalDomain.modules['gclitest/recurse'];
  delete define.modules['gclitest/firebug'];
  delete define.globalDomain.modules['gclitest/firebug'];

  define = undefined;
  require = undefined;

  finish();
}

function testWorking() {
  // There are lots of requirement tests that we could be doing here
  // The fact that we can get anything at all working is a testament to
  // require doing what it should - we don't need to test the
  let requireable = require('gclitest/requirable');
  is('thing1', requireable.thing1, 'thing1 was required');
  is(2, requireable.thing2, 'thing2 was required');
  is(requireable.thing3, undefined, 'thing3 was not required');
}

function testDomains() {
  let requireable = require('gclitest/requirable');
  is(requireable.status, undefined, 'requirable has no status');
  requireable.setStatus(null);
  is(null, requireable.getStatus(), 'requirable.getStatus changed to null');
  is(requireable.status, undefined, 'requirable still has no status');
  requireable.setStatus('42');
  is('42', requireable.getStatus(), 'requirable.getStatus changed to 42');
  is(requireable.status, undefined, 'requirable *still* has no status');

  let domain = new define.Domain();
  let requireable2 = domain.require('gclitest/requirable');
  is(requireable2.status, undefined, 'requirable2 has no status');
  is('initial', requireable2.getStatus(), 'requirable2.getStatus is initial');
  requireable2.setStatus(999);
  is(999, requireable2.getStatus(), 'requirable2.getStatus changed to 999');
  is(requireable2.status, undefined, 'requirable2 still has no status');

  is('42', requireable.getStatus(), 'status 42');
  ok(requireable.status === undefined, 'requirable has no status (as expected)');

  delete domain.modules['gclitest/requirable'];
}

function testLeakage() {
  let requireable = require('gclitest/requirable');
  is(requireable.setup, null, 'leakage of setup');
  is(requireable.shutdown, null, 'leakage of shutdown');
  is(requireable.testWorking, null, 'leakage of testWorking');
}

function testMultiImport() {
  let r1 = require('gclitest/requirable');
  let r2 = require('gclitest/requirable');
  is(r1, r2, 'double require was strict equal');
}

function testUncompilable() {
  // It's not totally clear how a module loader should perform with unusable
  // modules, however at least it should go into a flat spin ...
  // GCLI mini_require reports an error as it should
  try {
    let unrequireable = require('gclitest/unrequirable');
    fail();
  }
  catch (ex) {
    // an exception is expected
  }
}

function testRecursive() {
  // See Bug 658583
  // require('gclitest/recurse');
  // Also see the comments in the testRecursive() function
}

function testFirebug() {
  let requirable = require('gclitest/requirable');
  let firebug = require('gclitest/firebug');
  ok(firebug.fb, 'firebug.fb is true');
  is(requirable, firebug.requirable, 'requirable pass-through');
}
