/* vim:set ts=2 sw=2 sts=2 et: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests that the network panel works.

const TEST_URI = "http://example.com/browser/browser/devtools/webconsole/test/test-console.html";
const TEST_IMG = "http://example.com/browser/browser/devtools/webconsole/test/test-image.png";
const TEST_ENCODING_ISO_8859_1 = "http://example.com/browser/browser/devtools/webconsole/test/test-encoding-ISO-8859-1.html";

let testDriver;

function test() {
  addTab(TEST_URI);
  browser.addEventListener("load", function onLoad() {
    browser.removeEventListener("load", onLoad, true);
    openConsole(null, testNetworkPanel);
  }, true);
}

function testNetworkPanel() {
  testDriver = testGen();
  testDriver.next();
}

function checkIsVisible(aPanel, aList) {
  for (let id in aList) {
    let node = aPanel.document.getElementById(id);
    let isVisible = aList[id];
    is(node.style.display, (isVisible ? "block" : "none"), id + " isVisible=" + isVisible);
  }
}

function checkNodeContent(aPanel, aId, aContent) {
  let node = aPanel.document.getElementById(aId);
  if (node == null) {
    ok(false, "Tried to access node " + aId + " that doesn't exist!");
  }
  else if (node.textContent.indexOf(aContent) != -1) {
    ok(true, "checking content of " + aId);
  }
  else {
    ok(false, "Got false value for " + aId + ": " + node.textContent + " doesn't have " + aContent);
  }
}

function checkNodeKeyValue(aPanel, aId, aKey, aValue) {
  let node = aPanel.document.getElementById(aId);

  let headers = node.querySelectorAll("th");
  for (let i = 0; i < headers.length; i++) {
    if (headers[i].textContent == (aKey + ":")) {
      is(headers[i].nextElementSibling.textContent, aValue,
         "checking content of " + aId + " for key " + aKey);
      return;
    }
  }

  ok(false, "content check failed for " + aId + ", key " + aKey);
}

function testGen() {
  let filterBox = HUDService.getHudByWindow(content).filterBox;

  let tempScope  = {};
  Cu.import("resource:///modules/WebConsoleUtils.jsm", tempScope);
  let l10n = tempScope.WebConsoleUtils.l10n;
  tempScope = null;

  let httpActivity = {
    meta: {
      stages: [],
      discardRequestBody: true,
      discardResponseBody: true,
    },
    log: {
      entries: [{
        startedDateTime: (new Date()).toISOString(),
        request: {
          url: "http://www.testpage.com",
          method: "GET",
          cookies: [],
          headers: [
            { name: "foo", value: "bar" },
          ],
        },
        response: {
          headers: [],
          content: {},
        },
        timings: {},
      }],
    },
  };

  let entry = httpActivity.log.entries[0];

  let networkPanel = HUDService.openNetworkPanel(filterBox, httpActivity);

  is(filterBox._netPanel, networkPanel,
     "Network panel stored on the anchor object");

  networkPanel.panel.addEventListener("load", function onLoad() {
    networkPanel.panel.removeEventListener("load", onLoad, true);
    testDriver.next();
  }, true);

  yield;

  info("test 1");

  checkIsVisible(networkPanel, {
    requestCookie: false,
    requestFormData: false,
    requestBody: false,
    responseContainer: false,
    responseBody: false,
    responseNoBody: false,
    responseImage: false,
    responseImageCached: false
  });

  checkNodeContent(networkPanel, "header", "http://www.testpage.com");
  checkNodeContent(networkPanel, "header", "GET");
  checkNodeKeyValue(networkPanel, "requestHeadersContent", "foo", "bar");

  // Test request body.
  info("test 2: request body");
  httpActivity.meta.discardRequestBody = false;
  entry.request.postData = { text: "hello world" };
  networkPanel.update();

  checkIsVisible(networkPanel, {
    requestBody: true,
    requestFormData: false,
    requestCookie: false,
    responseContainer: false,
    responseBody: false,
    responseNoBody: false,
    responseImage: false,
    responseImageCached: false
  });
  checkNodeContent(networkPanel, "requestBodyContent", "hello world");

  // Test response header.
  info("test 3: response header");
  entry.timings.wait = 10;
  entry.response.httpVersion = "HTTP/3.14";
  entry.response.status = 999;
  entry.response.statusText = "earthquake win";
  entry.response.content.mimeType = "text/html";
  entry.response.headers.push(
    { name: "Content-Type", value: "text/html" },
    { name: "leaveHouses", value: "true" }
  );

  networkPanel.update();

  checkIsVisible(networkPanel, {
    requestBody: true,
    requestFormData: false,
    requestCookie: false,
    responseContainer: true,
    responseBody: false,
    responseNoBody: false,
    responseImage: false,
    responseImageCached: false
  });

  checkNodeContent(networkPanel, "header", "HTTP/3.14 999 earthquake win");
  checkNodeKeyValue(networkPanel, "responseHeadersContent", "leaveHouses", "true");
  checkNodeContent(networkPanel, "responseHeadersInfo", "10ms");

  info("test 4");

  httpActivity.meta.discardResponseBody = false;
  entry.timings.receive = 2;
  networkPanel.update();

  checkIsVisible(networkPanel, {
    requestBody: true,
    requestCookie: false,
    requestFormData: false,
    responseContainer: true,
    responseBody: false,
    responseNoBody: false,
    responseImage: false,
    responseImageCached: false
  });

  info("test 5");

  httpActivity.meta.stages.push("REQUEST_STOP", "TRANSACTION_CLOSE");
  networkPanel.update();

  checkNodeContent(networkPanel, "responseNoBodyInfo", "2ms");
  checkIsVisible(networkPanel, {
    requestBody: true,
    requestCookie: false,
    responseContainer: true,
    responseBody: false,
    responseNoBody: true,
    responseImage: false,
    responseImageCached: false
  });

  networkPanel.panel.hidePopup();

  // Second run: Test for cookies and response body.
  info("test 6: cookies and response body");
  entry.request.cookies.push(
    { name: "foo", value: "bar" },
    { name: "hello", value: "world" }
  );
  entry.response.content.text = "get out here";

  networkPanel = HUDService.openNetworkPanel(filterBox, httpActivity);
  is(filterBox._netPanel, networkPanel,
     "Network panel stored on httpActivity object");

  networkPanel.panel.addEventListener("load", function onLoad() {
    networkPanel.panel.removeEventListener("load", onLoad, true);
    testDriver.next();
  }, true);

  yield;

  checkIsVisible(networkPanel, {
    requestBody: true,
    requestFormData: false,
    requestCookie: true,
    responseContainer: true,
    responseBody: true,
    responseNoBody: false,
    responseImage: false,
    responseImageCached: false
  });

  checkNodeKeyValue(networkPanel, "requestCookieContent", "foo", "bar");
  checkNodeKeyValue(networkPanel, "requestCookieContent", "hello", "world");
  checkNodeContent(networkPanel, "responseBodyContent", "get out here");
  checkNodeContent(networkPanel, "responseBodyInfo", "2ms");

  networkPanel.panel.hidePopup();

  // Check image request.
  info("test 7: image request");
  entry.response.headers[1].value = "image/png";
  entry.response.content.mimeType = "image/png";
  entry.request.url = TEST_IMG;

  networkPanel = HUDService.openNetworkPanel(filterBox, httpActivity);
  networkPanel.panel.addEventListener("load", function onLoad() {
    networkPanel.panel.removeEventListener("load", onLoad, true);
    testDriver.next();
  }, true);

  yield;

  checkIsVisible(networkPanel, {
    requestBody: true,
    requestFormData: false,
    requestCookie: true,
    responseContainer: true,
    responseBody: false,
    responseNoBody: false,
    responseImage: true,
    responseImageCached: false
  });

  let imgNode = networkPanel.document.getElementById("responseImageNode");
  is(imgNode.getAttribute("src"), TEST_IMG, "Displayed image is correct");

  function checkImageResponseInfo() {
    checkNodeContent(networkPanel, "responseImageInfo", "2ms");
    checkNodeContent(networkPanel, "responseImageInfo", "16x16px");
  }

  // Check if the image is loaded already.
  if (imgNode.width == 0) {
    imgNode.addEventListener("load", function onLoad() {
      imgNode.removeEventListener("load", onLoad, false);
      checkImageResponseInfo();
      networkPanel.panel.hidePopup();
      testDriver.next();
    }, false);
    // Wait until the image is loaded.
    yield;
  }
  else {
    checkImageResponseInfo();
    networkPanel.panel.hidePopup();
  }

  // Check cached image request.
  info("test 8: cached image request");
  entry.response.httpVersion = "HTTP/1.1";
  entry.response.status = 304;
  entry.response.statusText = "Not Modified";

  networkPanel = HUDService.openNetworkPanel(filterBox, httpActivity);
  networkPanel.panel.addEventListener("load", function onLoad() {
    networkPanel.panel.removeEventListener("load", onLoad, true);
    testDriver.next();
  }, true);

  yield;

  checkIsVisible(networkPanel, {
    requestBody: true,
    requestFormData: false,
    requestCookie: true,
    responseContainer: true,
    responseBody: false,
    responseNoBody: false,
    responseImage: false,
    responseImageCached: true
  });

  let imgNode = networkPanel.document.getElementById("responseImageCachedNode");
  is(imgNode.getAttribute("src"), TEST_IMG, "Displayed image is correct");

  networkPanel.panel.hidePopup();

  // Test sent form data.
  info("test 9: sent form data");
  entry.request.postData.text = [
    "Content-Type:      application/x-www-form-urlencoded",
    "Content-Length: 59",
    "name=rob&age=20"
  ].join("\n");

  networkPanel = HUDService.openNetworkPanel(filterBox, httpActivity);
  networkPanel.panel.addEventListener("load", function onLoad() {
    networkPanel.panel.removeEventListener("load", onLoad, true);
    testDriver.next();
  }, true);

  yield;

  checkIsVisible(networkPanel, {
    requestBody: false,
    requestFormData: true,
    requestCookie: true,
    responseContainer: true,
    responseBody: false,
    responseNoBody: false,
    responseImage: false,
    responseImageCached: true
  });

  checkNodeKeyValue(networkPanel, "requestFormDataContent", "name", "rob");
  checkNodeKeyValue(networkPanel, "requestFormDataContent", "age", "20");
  networkPanel.panel.hidePopup();

  // Test no space after Content-Type:
  info("test 10: no space after Content-Type header in post data");
  entry.request.postData.text = "Content-Type:application/x-www-form-urlencoded\n";

  networkPanel = HUDService.openNetworkPanel(filterBox, httpActivity);
  networkPanel.panel.addEventListener("load", function onLoad() {
    networkPanel.panel.removeEventListener("load", onLoad, true);
    testDriver.next();
  }, true);

  yield;

  checkIsVisible(networkPanel, {
    requestBody: false,
    requestFormData: true,
    requestCookie: true,
    responseContainer: true,
    responseBody: false,
    responseNoBody: false,
    responseImage: false,
    responseImageCached: true
  });

  networkPanel.panel.hidePopup();

  // Test cached data.

  info("test 11: cached data");

  entry.request.url = TEST_ENCODING_ISO_8859_1;
  entry.response.headers[1].value = "application/json";
  entry.response.content.mimeType = "application/json";
  entry.response.content.text = "my cached data is here!";

  networkPanel = HUDService.openNetworkPanel(filterBox, httpActivity);
  networkPanel.panel.addEventListener("load", function onLoad() {
    networkPanel.panel.removeEventListener("load", onLoad, true);
    testDriver.next();
  }, true);

  yield;

  checkIsVisible(networkPanel, {
    requestBody: false,
    requestFormData: true,
    requestCookie: true,
    responseContainer: true,
    responseBody: false,
    responseBodyCached: true,
    responseNoBody: false,
    responseImage: false,
    responseImageCached: false
  });

  checkNodeContent(networkPanel, "responseBodyCachedContent",
                   "my cached data is here!");

  networkPanel.panel.hidePopup();

  // Test a response with a content type that can't be displayed in the
  // NetworkPanel.
  info("test 12: unknown content type");
  entry.response.headers[1].value = "application/x-shockwave-flash";
  entry.response.content.mimeType = "application/x-shockwave-flash";

  networkPanel = HUDService.openNetworkPanel(filterBox, httpActivity);
  networkPanel.panel.addEventListener("load", function onLoad() {
    networkPanel.panel.removeEventListener("load", onLoad, true);
    testDriver.next();
  }, true);

  yield;

  checkIsVisible(networkPanel, {
    requestBody: false,
    requestFormData: true,
    requestCookie: true,
    responseContainer: true,
    responseBody: false,
    responseBodyCached: false,
    responseBodyUnknownType: true,
    responseNoBody: false,
    responseImage: false,
    responseImageCached: false
  });

  let responseString =
    l10n.getFormatStr("NetworkPanel.responseBodyUnableToDisplay.content",
                      ["application/x-shockwave-flash"]);
  checkNodeContent(networkPanel, "responseBodyUnknownTypeContent", responseString);
  networkPanel.panel.hidePopup();

  /*

  // This test disabled. See bug 603620.

  // Test if the NetworkPanel figures out the content type based on an URL as
  // well.
  delete httpActivity.response.header["Content-Type"];
  httpActivity.url = "http://www.test.com/someCrazyFile.swf?done=right&ending=txt";

  networkPanel = HUDService.openNetworkPanel(filterBox, httpActivity);
  networkPanel.isDoneCallback = function NP_doneCallback() {
    networkPanel.isDoneCallback = null;
    testDriver.next();
  }

  yield;

  checkIsVisible(networkPanel, {
    requestBody: false,
    requestFormData: true,
    requestCookie: true,
    responseContainer: true,
    responseBody: false,
    responseBodyCached: false,
    responseBodyUnknownType: true,
    responseNoBody: false,
    responseImage: false,
    responseImageCached: false
  });

  // Systems without Flash installed will return an empty string here. Ignore.
  if (networkPanel.document.getElementById("responseBodyUnknownTypeContent").textContent !== "")
    checkNodeContent(networkPanel, "responseBodyUnknownTypeContent", responseString);
  else
    ok(true, "Flash not installed");

  networkPanel.panel.hidePopup(); */

  // All done!
  testDriver = null;
  executeSoon(finishTest);

  yield;
}
