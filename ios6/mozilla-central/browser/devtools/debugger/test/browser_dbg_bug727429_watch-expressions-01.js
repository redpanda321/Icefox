/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Bug 727429: test the debugger watch expressions.
 */

const TAB_URL = EXAMPLE_URL + "browser_dbg_watch-expressions.html";

let gPane = null;
let gTab = null;
let gDebuggee = null;
let gDebugger = null;
let gWatch = null;

function test()
{
  debug_tab_pane(TAB_URL, function(aTab, aDebuggee, aPane) {
    gTab = aTab;
    gDebuggee = aDebuggee;
    gPane = aPane;
    gDebugger = gPane.panelWin;
    gWatch = gDebugger.DebuggerView.WatchExpressions;

    gDebugger.DebuggerView.togglePanes({ visible: true, animated: false });

    executeSoon(function() {
      performTest();
    });
  });

  function performTest()
  {
    is(gWatch.getExpressions().length, 0,
      "There should initially be no watch expressions");

    addAndCheckExpressions(1, 0, "a");
    addAndCheckExpressions(2, 0, "b");
    addAndCheckExpressions(3, 0, "c");

    removeAndCheckExpression(2, 1, "a");
    removeAndCheckExpression(1, 0, "a");


    addAndCheckExpressions(2, 0, "", true);
    gDebugger.editor.focus();
    is(gWatch.getExpressions().length, 1,
      "Empty watch expressions are automatically removed");

    addAndCheckExpressions(2, 0, "a", true);
    gDebugger.editor.focus();
    is(gWatch.getExpressions().length, 1,
      "Duplicate watch expressions are automatically removed");

    addAndCheckExpressions(2, 0, "a\t", true);
    addAndCheckExpressions(2, 0, "a\r", true);
    addAndCheckExpressions(2, 0, "a\n", true);
    gDebugger.editor.focus();
    is(gWatch.getExpressions().length, 1,
      "Duplicate watch expressions are automatically removed");

    addAndCheckExpressions(2, 0, "\ta", true);
    addAndCheckExpressions(2, 0, "\ra", true);
    addAndCheckExpressions(2, 0, "\na", true);
    gDebugger.editor.focus();
    is(gWatch.getExpressions().length, 1,
      "Duplicate watch expressions are automatically removed");


    addAndCheckCustomExpression(2, 0, "bazΩΩka");
    addAndCheckCustomExpression(3, 0, "bambøøcha");


    EventUtils.sendMouseEvent({ type: "click" },
      gWatch.getItemAtIndex(0).target.closeNode,
      gDebugger);

    is(gWatch.getExpressions().length, 2,
      "Watch expressions are removed when the close button is pressed");
    is(gWatch.getExpressions()[0], "bazΩΩka",
      "The expression at index " + 0 + " should be correct (1)");
    is(gWatch.getExpressions()[1], "a",
      "The expression at index " + 1 + " should be correct (2)");


    EventUtils.sendMouseEvent({ type: "click" },
      gWatch.getItemAtIndex(0).target.closeNode,
      gDebugger);

    is(gWatch.getExpressions().length, 1,
      "Watch expressions are removed when the close button is pressed");
    is(gWatch.getExpressions()[0], "a",
      "The expression at index " + 0 + " should be correct (3)");


    EventUtils.sendMouseEvent({ type: "click" },
      gWatch.getItemAtIndex(0).target.closeNode,
      gDebugger);

    is(gWatch.getExpressions().length, 0,
      "Watch expressions are removed when the close button is pressed");


    EventUtils.sendMouseEvent({ type: "click" },
      gWatch._container._parent,
      gDebugger);

    is(gWatch.getExpressions().length, 1,
      "Watch expressions are added when the view container is pressed");


    closeDebuggerAndFinish();
  }

  function addAndCheckCustomExpression(total, index, string, noBlur) {
    addAndCheckExpressions(total, index, "", true);

    for (let i = 0; i < string.length; i++) {
      EventUtils.sendChar(string[i]);
    }

    gDebugger.editor.focus();

    let id = gWatch.getItemAtIndex(index).attachment.id;
    let element = gDebugger.document.getElementById("expression-" + id);

    is(gWatch.getItemAtIndex(index).attachment.initialExpression, "",
      "The initial expression at index " + index + " should be correct (1)");
    is(gWatch.getItemForElement(element).attachment.initialExpression, "",
      "The initial expression at index " + index + " should be correct (2)");

    is(gWatch.getItemAtIndex(index).attachment.expression, string,
      "The expression at index " + index + " should be correct (1)");
    is(gWatch.getItemForElement(element).attachment.expression, string,
      "The expression at index " + index + " should be correct (2)");

    is(gWatch.getExpression(index), string,
      "The expression at index " + index + " should be correct (3)");
    is(gWatch.getExpressions()[index], string,
      "The expression at index " + index + " should be correct (4)");
  }

  function addAndCheckExpressions(total, index, string, noBlur) {
    gWatch.addExpression(string);

    is(gWatch.getExpressions().length, total,
      "There should be " + total + " watch expressions available (1)");
    is(gWatch.totalItems, total,
      "There should be " + total + " watch expressions available (2)");

    ok(gWatch.getItemAtIndex(index),
      "The expression at index " + index + " should be available");
    ok(gWatch.getItemAtIndex(index).attachment.id,
      "The expression at index " + index + " should have an id");
    is(gWatch.getItemAtIndex(index).attachment.initialExpression, string,
      "The expression at index " + index + " should have an initial expression");

    let id = gWatch.getItemAtIndex(index).attachment.id;
    let element = gDebugger.document.getElementById("expression-" + id);

    ok(element,
      "Three should be a new expression item in the view");
    ok(gWatch.getItemForElement(element),
      "The watch expression item should be accessible");
    is(gWatch.getItemForElement(element), gWatch.getItemAtIndex(index),
      "The correct watch expression item was accessed");

    ok(gWatch.getItemAtIndex(index) instanceof gDebugger.MenuItem,
      "The correct watch expression element was accessed (1)");
    ok(gWatch._container.getItemAtIndex(index) instanceof XULElement,
      "The correct watch expression element was accessed (2)");
    is(element, gWatch._container.getItemAtIndex(index),
      "The correct watch expression element was accessed (3)");

    is(element.arrowNode.hidden, false,
      "The arrow node should be visible");
    is(element.closeNode.hidden, false,
      "The close button should be visible");
    is(element.inputNode.getAttribute("focused"), "true",
      "The textbox input should be focused");

    is(gWatch._variables.scrollTop, 0,
      "The variables view should be scrolled to top");

    is(gWatch._cache[0], gWatch.getItemAtIndex(index),
      "The correct watch expression was added to the cache (1)");
    is(gWatch._cache[0], gWatch.getItemForElement(element),
      "The correct watch expression was added to the cache (2)");

    if (!noBlur) {
      gDebugger.editor.focus();

      is(gWatch.getItemAtIndex(index).attachment.initialExpression, string,
        "The initial expression at index " + index + " should be correct (1)");
      is(gWatch.getItemForElement(element).attachment.initialExpression, string,
        "The initial expression at index " + index + " should be correct (2)");

      is(gWatch.getItemAtIndex(index).attachment.expression, string,
        "The expression at index " + index + " should be correct (1)");
      is(gWatch.getItemForElement(element).attachment.expression, string,
        "The expression at index " + index + " should be correct (2)");

      is(gWatch.getExpression(index), string,
        "The expression at index " + index + " should be correct (3)");
      is(gWatch.getExpressions()[index], string,
        "The expression at index " + index + " should be correct (4)");
    }
  }

  function removeAndCheckExpression(total, index, string) {
    gWatch.removeExpressionAt(index);

    is(gWatch.getExpressions().length, total,
      "There should be " + total + " watch expressions available (1)");
    is(gWatch.totalItems, total,
      "There should be " + total + " watch expressions available (2)");

    ok(gWatch.getItemAtIndex(index),
      "The expression at index " + index + " should still be available");
    ok(gWatch.getItemAtIndex(index).attachment.id,
      "The expression at index " + index + " should still have an id");
    is(gWatch.getItemAtIndex(index).attachment.initialExpression, string,
      "The expression at index " + index + " should still have an initial expression");

    let id = gWatch.getItemAtIndex(index).attachment.id;
    let element = gDebugger.document.getElementById("expression-" + id);

    is(gWatch.getItemAtIndex(index).attachment.initialExpression, string,
      "The initial expression at index " + index + " should be correct (1)");
    is(gWatch.getItemForElement(element).attachment.initialExpression, string,
      "The initial expression at index " + index + " should be correct (2)");

    is(gWatch.getItemAtIndex(index).attachment.expression, string,
      "The expression at index " + index + " should be correct (1)");
    is(gWatch.getItemForElement(element).attachment.expression, string,
      "The expression at index " + index + " should be correct (2)");

    is(gWatch.getExpression(index), string,
      "The expression at index " + index + " should be correct (3)");
    is(gWatch.getExpressions()[index], string,
      "The expression at index " + index + " should be correct (4)");
  }

  registerCleanupFunction(function() {
    removeTab(gTab);
    gPane = null;
    gTab = null;
    gDebuggee = null;
    gDebugger = null;
    gWatch = null;
  });
}
