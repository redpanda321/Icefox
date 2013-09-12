/* vim: set ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

function test()
{
  waitForExplicitFinish();

  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.selectedBrowser.addEventListener("load", function onLoad() {
    gBrowser.selectedBrowser.removeEventListener("load", onLoad, true);
    openScratchpad(runTests);
  }, true);

  content.location = "data:text/html,<title>foobarBug636725</title>" +
    "<p>test inspect() in Scratchpad";
}

function runTests()
{
  let sp = gScratchpadWindow.Scratchpad;
  let doc = gScratchpadWindow.document;

  let methodsAndItems = {
    "sp-menu-newscratchpad": "openScratchpad",
    "sp-menu-open": "openFile",
    "sp-menu-save": "saveFile",
    "sp-menu-saveas": "saveFileAs",
    "sp-text-run": "run",
    "sp-text-inspect": "inspect",
    "sp-text-display": "display",
    "sp-text-resetContext": "resetContext",
    "sp-menu-content": "setContentContext",
    "sp-menu-browser": "setBrowserContext",
  };

  let lastMethodCalled = null;
  sp.__noSuchMethod__ = function(aMethodName) {
    lastMethodCalled = aMethodName;
  };

  for (let id in methodsAndItems) {
    lastMethodCalled = null;

    let methodName = methodsAndItems[id];
    let oldMethod = sp[methodName];
    ok(oldMethod, "found method " + methodName + " in Scratchpad object");

    delete sp[methodName];

    let menu = doc.getElementById(id);
    ok(menu, "found menuitem #" + id);

    try {
      menu.doCommand();
    }
    catch (ex) {
      ok(false, "exception thrown while executing the command of menuitem #" + id);
    }

    ok(lastMethodCalled == methodName,
       "method " + methodName + " invoked by the associated menuitem");

    sp[methodName] = oldMethod;
  }

  delete sp.__noSuchMethod__;

  finish();
}
