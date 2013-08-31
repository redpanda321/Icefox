/* vim:set ts=2 sw=2 sts=2 et: */
/* ***** BEGIN LICENSE BLOCK *****
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 *
 * Contributor(s):
 *  Mihai Șucan <mihai.sucan@gmail.com>
 *
 * ***** END LICENSE BLOCK ***** */

const TEST_URI = "http://example.com/browser/browser/devtools/webconsole/test/test-bug-599725-response-headers.sjs";

function performTest(lastFinishedRequest, aConsole)
{
  ok(lastFinishedRequest, "page load was logged");

  let headers = null;

  function readHeader(aName)
  {
    for (let header of headers) {
      if (header.name == aName) {
        return header.value;
      }
    }
    return null;
  }

  aConsole.webConsoleClient.getResponseHeaders(lastFinishedRequest.actor,
    function (aResponse) {
      headers = aResponse.headers;
      ok(headers, "we have the response headers");
      ok(!readHeader("Content-Type"), "we do not have the Content-Type header");
      isnot(readHeader("Content-Length"), 60, "Content-Length != 60");
      executeSoon(finishTest);
    });

  HUDService.lastFinishedRequestCallback = null;
}

function test()
{
  addTab(TEST_URI);

  browser.addEventListener("load", function onLoad() {
    browser.removeEventListener("load", onLoad, true);
    openConsole(null, function() {
      HUDService.lastFinishedRequestCallback = performTest;
      content.location.reload();
    });
  }, true);
}
