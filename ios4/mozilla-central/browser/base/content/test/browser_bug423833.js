/* Tests for proper behaviour of "Show this frame" context menu options */

// Two frames, one with text content, the other an error page
var invalidPage = 'http://127.0.0.1:55555/';
var validPage = 'http://example.com/';
var testPage = 'data:text/html,<frameset cols="400,400"><frame src="' + validPage + '"><frame src="' + invalidPage + '"></frameset>';

// Store the tab and window created in tests 2 and 3 respectively
var test2tab;
var test3window;

// We use setInterval instead of setTimeout to avoid race conditions on error doc loads
var intervalID;

function test() {
  waitForExplicitFinish();

  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.selectedBrowser.addEventListener("load", test1Setup, true);
  content.location = testPage;
}

function test1Setup() {
  if (content.frames.length < 2 ||
      content.frames[1].location != invalidPage)
    // The error frame hasn't loaded yet
    return;

  gBrowser.selectedBrowser.removeEventListener("load", test1Setup, true);

  var badFrame = content.frames[1];
  document.popupNode = badFrame.document.firstChild;

  var contentAreaContextMenu = document.getElementById("contentAreaContextMenu");
  var contextMenu = new nsContextMenu(contentAreaContextMenu, gBrowser);

  // We'd like to use another load listener here, but error pages don't fire load events
  contextMenu.showOnlyThisFrame();
  intervalID = setInterval(testShowOnlyThisFrame, 3000);
}

function testShowOnlyThisFrame() {
  if (content.location.href == testPage)
    // This is a stale event from the original page loading
    return;

  // We should now have loaded the error page frame content directly
  // in the tab, make sure the URL is right.
  clearInterval(intervalID);

  is(content.location.href, invalidPage, "Should navigate to page url, not about:neterror");

  // Go back to the frames page
  gBrowser.addEventListener("load", test2Setup, true);
  content.location = testPage;
}

function test2Setup() {
  if (content.frames.length < 2 ||
      content.frames[1].location != invalidPage)
    // The error frame hasn't loaded yet
    return;

  gBrowser.removeEventListener("load", test2Setup, true);

  // Now let's do the whole thing again, but this time for "Open frame in new tab"
  var badFrame = content.frames[1];

  document.popupNode = badFrame.document.firstChild;

  var contentAreaContextMenu = document.getElementById("contentAreaContextMenu");
  var contextMenu = new nsContextMenu(contentAreaContextMenu, gBrowser);

  test2tab = contextMenu.openFrameInTab();
  ok(test2tab instanceof XULElement, "openFrameInTab() should return an element (non-null)");
  is(test2tab.tagName, "tab", "openFrameInTab() should return a *tab* element");

  gBrowser.selectedTab = test2tab;

  intervalID = setInterval(testOpenFrameInTab, 3000);
}

function testOpenFrameInTab() {
  if (gBrowser.contentDocument.location.href == "about:blank")
    // Wait another cycle
    return;

  clearInterval(intervalID);

  // We should now have the error page in a new, active tab.
  is(gBrowser.contentDocument.location.href, invalidPage, "New tab should have page url, not about:neterror");

  // Clear up the new tab, and punt to test 3
  gBrowser.removeCurrentTab();

  test3Setup();
}

function test3Setup() {
  // One more time, for "Open frame in new window"
  var badFrame = content.frames[1];
  document.popupNode = badFrame.document.firstChild;

  var contentAreaContextMenu = document.getElementById("contentAreaContextMenu");
  var contextMenu = new nsContextMenu(contentAreaContextMenu, gBrowser);

  test3window = contextMenu.openFrame();
  ok(test3window instanceof Window, "openFrame() should return a window (non-null) ");

  intervalID = setInterval(testOpenFrame, 3000);
}

function testOpenFrame() {
  if (test3window.content.location.href == "about:blank")
    // Wait another cycle
    return;

  clearInterval(intervalID);

  is(test3window.content.location.href, invalidPage, "New window should have page url, not about:neterror");

  test3window.close();
  cleanup();
}

function cleanup() {
  gBrowser.removeCurrentTab();
  finish();
}