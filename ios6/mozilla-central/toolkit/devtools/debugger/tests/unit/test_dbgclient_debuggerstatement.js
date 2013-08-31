/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

Cu.import("resource://gre/modules/devtools/dbg-server.jsm");
Cu.import("resource://gre/modules/devtools/dbg-client.jsm");

var gClient;
var gDebuggee;

function run_test()
{
  DebuggerServer.addActors("resource://test/testactors.js");

  // Allow incoming connections.
  DebuggerServer.init(function () { return true; });
  gDebuggee = testGlobal("test-1");
  DebuggerServer.addTestGlobal(gDebuggee);

  let transport = DebuggerServer.connectPipe();
  gClient = new DebuggerClient(transport);
  gClient.connect(function(aType, aTraits) {
    getTestGlobalContext(gClient, "test-1", function(aContext) {
      test_attach(aContext);
    });
  });
  do_test_pending();
}

function test_attach(aContext)
{
  gClient.attachThread(aContext.actor, function(aResponse, aThreadClient) {
    do_check_eq(aThreadClient.state, "paused");
    do_check_eq(aThreadClient.actor, aContext.actor);
    aThreadClient.resume(function() {
      do_check_eq(aThreadClient.state, "attached");
      test_debugger_statement(aThreadClient);
    });
  });
}

function test_debugger_statement(aThreadClient)
{
  aThreadClient.addListener("paused", function(aEvent, aPacket) {
    do_check_eq(aThreadClient.state, "paused");
    // Reach around the protocol to check that the debuggee is in the state
    // we expect.
    do_check_true(gDebuggee.a);
    do_check_false(gDebuggee.b);

    let xpcInspector = Cc["@mozilla.org/jsinspector;1"].getService(Ci.nsIJSInspector);
    do_check_eq(xpcInspector.eventLoopNestLevel, 1);

    aThreadClient.resume(function() {
      cleanup();
    });
  });

  Cu.evalInSandbox("var a = true; var b = false; debugger; var b = true;", gDebuggee);

  // Now make sure that we've run the code after the debugger statement...
  do_check_true(gDebuggee.b);
}

function cleanup()
{
  gClient.addListener("closed", function(aEvent) {
    do_test_finished();
  });

  try {
    let xpcInspector = Cc["@mozilla.org/jsinspector;1"].getService(Ci.nsIJSInspector);
    do_check_eq(xpcInspector.eventLoopNestLevel, 0);
  } catch(e) {
    dump(e);
  }

  gClient.close();
}
