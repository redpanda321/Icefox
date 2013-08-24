/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test various GCLI commands

let imported = {};
Components.utils.import("resource:///modules/HUDService.jsm", imported);

const TEST_URI = "data:text/html;charset=utf-8,gcli-commands";

function test() {
  DeveloperToolbarTest.test(TEST_URI, function(browser, tab) {
    testEcho();
    testConsole(tab);

    imported = undefined;
    finish();
  });
}

function testEcho() {
  DeveloperToolbarTest.exec({
    typed: "echo message",
    args: { message: "message" },
    outputMatch: /^message$/,
  });
}

function testConsole(tab) {
  DeveloperToolbarTest.exec({
    typed: "console open",
    args: {},
    blankOutput: true,
  });

  let hud = imported.HUDService.getHudByWindow(content);
  ok(hud.hudId in imported.HUDService.hudReferences, "console open");

  hud.jsterm.execute("pprint(window)");

  /*
  // The web console is async and we can't force it with hud._flushMessageQueue
  // So we are skipping the test for output until we have an event to wait on
  let labels = hud.jsterm.outputNode.querySelectorAll(".webconsole-msg-output");
  ok(labels.length > 0, "output for pprint(window)");
  */

  DeveloperToolbarTest.exec({
    typed: "console clear",
    args: {},
    blankOutput: true,
  });

  let labels = hud.jsterm.outputNode.querySelectorAll(".webconsole-msg-output");
  is(labels.length, 0, "no output in console");

  DeveloperToolbarTest.exec({
    typed: "console close",
    args: {},
    blankOutput: true,
  });

  ok(!(hud.hudId in imported.HUDService.hudReferences), "console closed");
}
