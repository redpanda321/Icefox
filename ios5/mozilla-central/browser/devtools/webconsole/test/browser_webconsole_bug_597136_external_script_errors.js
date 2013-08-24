/* vim:set ts=2 sw=2 sts=2 et: */
/* ***** BEGIN LICENSE BLOCK *****
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 *
 * Contributor(s):
 *  Patrick Walton <pcwalton@mozilla.com>
 *
 * ***** END LICENSE BLOCK ***** */

const TEST_URI = "http://example.com/browser/browser/devtools/" +
                 "webconsole/test/test-bug-597136-external-script-" +
                 "errors.html";

function test() {
  addTab(TEST_URI);
  browser.addEventListener("load", tabLoaded, true);
}

function tabLoaded(aEvent) {
  browser.removeEventListener("load", tabLoaded, true);
  openConsole();

  let button = content.document.querySelector("button");
  let outputNode = HUDService.getHudByWindow(content).outputNode;

  expectUncaughtException();
  EventUtils.sendMouseEvent({ type: "click" }, button, content);

  waitForSuccess({
    name: "external script error message",
    validatorFn: function()
    {
      return outputNode.textContent.indexOf("bogus is not defined") > -1;
    },
    successFn: finishTest,
    failureFn: finishTest,
  });
}
