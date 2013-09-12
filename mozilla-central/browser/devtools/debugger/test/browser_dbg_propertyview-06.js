/* vim:set ts=2 sw=2 sts=2 et: */
/*
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */
var gPane = null;
var gTab = null;
var gDebuggee = null;
var gDebugger = null;

function test() {
  debug_tab_pane(STACK_URL, function(aTab, aDebuggee, aPane) {
    gTab = aTab;
    gDebuggee = aDebuggee;
    gPane = aPane;
    gDebugger = gPane.panelWin;

    testSimpleCall();
  });
}

function testSimpleCall() {
  gDebugger.DebuggerController.activeThread.addOneTimeListener("framesadded", function() {
    Services.tm.currentThread.dispatch({ run: function() {

      let globalScope = gDebugger.DebuggerView.Variables.addScope("Test-Global");
      let localScope = gDebugger.DebuggerView.Variables.addScope("Test-Local");

      let windowVar = globalScope.addVar("window");
      let documentVar = globalScope.addVar("document");
      let localVar0 = localScope.addVar("localVariable");
      let localVar1 = localScope.addVar("localVar1");
      let localVar2 = localScope.addVar("localVar2");
      let localVar3 = localScope.addVar("localVar3");
      let localVar4 = localScope.addVar("localVar4");
      let localVar5 = localScope.addVar("localVar5");

      localVar0._setGrip(42);
      localVar1._setGrip(true);
      localVar2._setGrip("nasu");

      localVar3._setGrip({ "type": "undefined" });
      localVar4._setGrip({ "type": "null" });
      localVar5._setGrip({ "type": "object", "class": "Object" });

      localVar5.addProperties({ "someProp0": { "value": 42, "enumerable": true },
                                "someProp1": { "value": true , "enumerable": true},
                                "someProp2": { "value": "nasu", "enumerable": true},
                                "someProp3": { "value": { "type": "undefined" }, "enumerable": true},
                                "someProp4": { "value": { "type": "null" }, "enumerable": true },
                                "someProp5": {
                                  "value": { "type": "object", "class": "Object" },
                                  "enumerable": true
                                }
                              });

      localVar5.get("someProp5").addProperties({ "someProp0": { "value": 42, "enumerable": true },
                                                 "someProp1": { "value": true, "enumerable": true },
                                                 "someProp2": { "value": "nasu", "enumerable": true },
                                                 "someProp3": { "value": { "type": "undefined" }, "enumerable": true },
                                                 "someProp4": { "value": { "type": "null" }, "enumerable": true },
                                                 "someAccessor": { "get": { "type": "object", "class": "Function" },
                                                                   "set": { "type": "undefined" },
                                                                   "enumerable": true } });

      windowVar._setGrip({ "type": "object", "class": "Window" });
      windowVar.addProperties({ "helloWorld": { "value": "hello world" } });

      documentVar._setGrip({ "type": "object", "class": "HTMLDocument" });
      documentVar.addProperties({ "onload": { "value": { "type": "null" } },
                                  "onunload": { "value": { "type": "null" } },
                                  "onfocus": { "value": { "type": "null" } },
                                  "onblur": { "value": { "type": "null" } },
                                  "onclick": { "value": { "type": "null" } },
                                  "onkeypress": { "value": { "type": "null" } } });


      ok(windowVar, "The windowVar hasn't been created correctly.");
      ok(documentVar, "The documentVar hasn't been created correctly.");
      ok(localVar0, "The localVar0 hasn't been created correctly.");
      ok(localVar1, "The localVar1 hasn't been created correctly.");
      ok(localVar2, "The localVar2 hasn't been created correctly.");
      ok(localVar3, "The localVar3 hasn't been created correctly.");
      ok(localVar4, "The localVar4 hasn't been created correctly.");
      ok(localVar5, "The localVar5 hasn't been created correctly.");


      for each (let elt in globalScope.target.querySelector(".nonenum").childNodes) {
        info("globalScope :: " + { id: elt.id, className: elt.className }.toSource());
      }
      is(globalScope.target.querySelector(".nonenum").childNodes.length, 2,
        "The globalScope doesn't contain all the created variable elements.");

      for each (let elt in localScope.target.querySelector(".nonenum").childNodes) {
        info("localScope :: " + { id: elt.id, className: elt.className }.toSource());
      }
      is(localScope.target.querySelector(".nonenum").childNodes.length, 6,
        "The localScope doesn't contain all the created variable elements.");


      is(localVar5.target.querySelector(".details").childNodes.length, 6,
        "The localVar5 doesn't contain all the created properties.");

      is(localVar5.get("someProp5").target.querySelector(".details").childNodes.length, 6,
        "The localVar5.someProp5 doesn't contain all the created properties.");


      is(windowVar.target.querySelector(".value").getAttribute("value"), "[object Window]",
        "The grip information for the windowVar wasn't set correctly.");

      is(documentVar.target.querySelector(".value").getAttribute("value"), "[object HTMLDocument]",
        "The grip information for the documentVar wasn't set correctly.");

      is(localVar0.target.querySelector(".value").getAttribute("value"), "42",
        "The grip information for the localVar0 wasn't set correctly.");

      is(localVar1.target.querySelector(".value").getAttribute("value"), "true",
        "The grip information for the localVar1 wasn't set correctly.");

      is(localVar2.target.querySelector(".value").getAttribute("value"), "\"nasu\"",
        "The grip information for the localVar2 wasn't set correctly.");

      is(localVar3.target.querySelector(".value").getAttribute("value"), "undefined",
        "The grip information for the localVar3 wasn't set correctly.");

      is(localVar4.target.querySelector(".value").getAttribute("value"), "null",
        "The grip information for the localVar4 wasn't set correctly.");

      is(localVar5.target.querySelector(".value").getAttribute("value"), "[object Object]",
        "The grip information for the localVar5 wasn't set correctly.");

      gDebugger.DebuggerController.activeThread.resume(function() {
        closeDebuggerAndFinish();
      });
    }}, 0);
  });

  gDebuggee.simpleCall();
}

registerCleanupFunction(function() {
  removeTab(gTab);
  gPane = null;
  gTab = null;
  gDebuggee = null;
  gDebugger = null;
});
