/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Check basic step-in functionality.
 */

var gDebuggee;
var gClient;
var gThreadClient;

function run_test()
{
  initTestDebuggerServer();
  gDebuggee = addTestGlobal("test-stack");
  gClient = new DebuggerClient(DebuggerServer.connectPipe());
  gClient.connect(function () {
    attachTestGlobalClientAndResume(gClient,
                                    "test-stack",
                                    function (aResponse, aThreadClient) {
      gThreadClient = aThreadClient;
      test_simple_stepping();
    });
  });
  do_test_pending();
}

function test_simple_stepping()
{
  gThreadClient.addOneTimeListener("paused", function (aEvent, aPacket) {
    gThreadClient.addOneTimeListener("paused", function (aEvent, aPacket) {
      // Check the return value.
      do_check_eq(aPacket.type, "paused");
      do_check_eq(aPacket.frame.where.line, gDebuggee.line0 + 2);
      do_check_eq(aPacket.why.type, "resumeLimit");
      // Check that stepping worked.
      do_check_eq(gDebuggee.a, undefined);
      do_check_eq(gDebuggee.b, undefined);

      gThreadClient.addOneTimeListener("paused", function (aEvent, aPacket) {
        // Check the return value.
        do_check_eq(aPacket.type, "paused");
        do_check_eq(aPacket.frame.where.line, gDebuggee.line0 + 3);
        do_check_eq(aPacket.why.type, "resumeLimit");
        // Check that stepping worked.
        do_check_eq(gDebuggee.a, 1);
        do_check_eq(gDebuggee.b, undefined);

        gThreadClient.addOneTimeListener("paused", function (aEvent, aPacket) {
          // Check the return value.
          do_check_eq(aPacket.type, "paused");
          // When leaving a stack frame the line number doesn't change.
          do_check_eq(aPacket.frame.where.line, gDebuggee.line0 + 3);
          do_check_eq(aPacket.why.type, "resumeLimit");
          // Check that stepping worked.
          do_check_eq(gDebuggee.a, 1);
          do_check_eq(gDebuggee.b, 2);

          gThreadClient.resume(function () {
            finishClient(gClient);
          });
        });
        gThreadClient.stepIn();
      });
      gThreadClient.stepIn();

    });
    gThreadClient.stepIn();

  });

  gDebuggee.eval("var line0 = Error().lineNumber;\n" +
                 "debugger;\n" +   // line0 + 1
                 "var a = 1;\n" +  // line0 + 2
                 "var b = 2;\n");  // line0 + 3
}
