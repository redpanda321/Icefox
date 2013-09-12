/* vim:set ts=2 sw=2 sts=2 et: */
/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Make sure that the debugger is updated with the correct scripts when moving
 * back and forward in the tab.
 */

const TAB_URL = EXAMPLE_URL + "browser_dbg_script-switching.html";
var gPane = null;
var gTab = null;
var gDebuggee = null;
var gDebugger = null;
var gScripts = null;

function test()
{
  debug_tab_pane(TAB_URL, function(aTab, aDebuggee, aPane) {
    gTab = aTab;
    gDebuggee = aDebuggee;
    gPane = aPane;
    gDebugger = gPane.panelWin;

    testInitialLoad();
  });
}

function testInitialLoad() {
  gDebugger.DebuggerController.activeThread.addOneTimeListener("framesadded", function() {
    executeSoon(function() {
      validateFirstPage();
      testLocationChange();
    });
  });

  gDebuggee.firstCall();
}

function testLocationChange()
{
  gDebugger.DebuggerController.activeThread.resume(function() {
    gDebugger.DebuggerController.client.addListener("tabNavigated", function onTabNavigated(aEvent, aPacket) {
      dump("tabNavigated state " + aPacket.state + "\n");
      if (aPacket.state == "start") {
        return;
      }

      gDebugger.DebuggerController.client.removeListener("tabNavigated", onTabNavigated);

      ok(true, "tabNavigated event was fired.");
      info("Still attached to the tab.");

      gDebugger.addEventListener("Debugger:AfterScriptsAdded", function _onEvent(aEvent) {
        gDebugger.removeEventListener(aEvent.type, _onEvent);

        executeSoon(function() {
          validateSecondPage();
          testBack();
        });
      });
    });
    content.location = STACK_URL;
  });
}

function testBack()
{
  gDebugger.DebuggerController.client.addListener("tabNavigated", function onTabNavigated(aEvent, aPacket) {
    dump("tabNavigated state " + aPacket.state + "\n");
    if (aPacket.state == "start") {
      return;
    }

    gDebugger.DebuggerController.client.removeListener("tabNavigated", onTabNavigated);

    ok(true, "tabNavigated event was fired after going back.");
    info("Still attached to the tab.");

    gDebugger.addEventListener("Debugger:AfterScriptsAdded", function _onEvent(aEvent) {
      gDebugger.removeEventListener(aEvent.type, _onEvent);

      executeSoon(function() {
        validateFirstPage();
        testForward();
      });
    });
  });

  info("Going back.");
  content.history.back();
}

function testForward()
{
  gDebugger.DebuggerController.client.addListener("tabNavigated", function onTabNavigated(aEvent, aPacket) {
    dump("tabNavigated state " + aPacket.state + "\n");
    if (aPacket.state == "start") {
      return;
    }

    gDebugger.DebuggerController.client.removeListener("tabNavigated", onTabNavigated);

    ok(true, "tabNavigated event was fired after going forward.");
    info("Still attached to the tab.");

    gDebugger.addEventListener("Debugger:AfterScriptsAdded", function _onEvent(aEvent) {
      gDebugger.removeEventListener(aEvent.type, _onEvent);

      executeSoon(function() {
        validateSecondPage();
        closeDebuggerAndFinish();
      });
    });
  });

  info("Going forward.");
  content.history.forward();
}

function validateFirstPage() {
  gScripts = gDebugger.DebuggerView.Sources._container;

  is(gScripts.itemCount, 2, "Found the expected number of scripts.");

  let label1 = "test-script-switching-01.js";
  let label2 = "test-script-switching-02.js";

  ok(gDebugger.DebuggerView.Sources.containsLabel(label1),
     "Found the first script label.");
  ok(gDebugger.DebuggerView.Sources.containsLabel(label2),
     "Found the second script label.");
}

function validateSecondPage() {
  gScripts = gDebugger.DebuggerView.Sources._container;

  is(gScripts.itemCount, 1, "Found the expected number of scripts.");

  ok(gDebugger.DebuggerView.Sources.containsLabel("browser_dbg_stack.html"),
     "Found the single script label.");
}

registerCleanupFunction(function() {
  removeTab(gTab);
  gPane = null;
  gTab = null;
  gDebuggee = null;
  gDebugger = null;
  gScripts = null;
});
