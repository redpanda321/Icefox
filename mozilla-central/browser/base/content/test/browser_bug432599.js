function invokeUsingCtrlD(phase) {
  switch (phase) {
  case 1:
    EventUtils.synthesizeKey("d", { accelKey: true });
    break;
  case 2:
  case 4:
    EventUtils.synthesizeKey("VK_ESCAPE", {});
    break;
  case 3:
    EventUtils.synthesizeKey("d", { accelKey: true });
    EventUtils.synthesizeKey("d", { accelKey: true });
    break;
  }
}

function invokeUsingStarButton(phase) {
  switch (phase) {
  case 1:
    EventUtils.sendMouseEvent({ type: "click" }, "star-button");
    break;
  case 2:
  case 4:
    EventUtils.synthesizeKey("VK_ESCAPE", {});
    break;
  case 3:
    EventUtils.synthesizeMouse(document.getElementById("star-button"),
                               1, 1, { clickCount: 2 });
    break;
  }
}

var testURL = "data:text/plain,Content";
var bookmarkId;

function add_bookmark(aURI, aTitle) {
  return PlacesUtils.bookmarks.insertBookmark(PlacesUtils.unfiledBookmarksFolderId,
                                              aURI, PlacesUtils.bookmarks.DEFAULT_INDEX,
                                              aTitle);
}

// test bug 432599
function test() {
  waitForExplicitFinish();

  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.selectedBrowser.addEventListener("load", function () {
    gBrowser.selectedBrowser.removeEventListener("load", arguments.callee, true);
    waitForStarChange(false, initTest);
  }, true);

  content.location = testURL;
}

function initTest() {
  // First, bookmark the page.
  bookmarkId = add_bookmark(makeURI(testURL), "Bug 432599 Test");

  checkBookmarksPanel(invokers[currentInvoker], 1);
}

function waitForStarChange(aValue, aCallback) {
  let starButton = document.getElementById("star-button");
  if (PlacesStarButton._pendingStmt || starButton.hasAttribute("starred") != aValue) {
    info("Waiting for star button change.");
    setTimeout(arguments.callee, 50, aValue, aCallback);
    return;
  }
  aCallback();
}

let invokers = [invokeUsingStarButton, invokeUsingCtrlD];
let currentInvoker = 0;

let initialValue;
let initialRemoveHidden;

let popupElement = document.getElementById("editBookmarkPanel");
let titleElement = document.getElementById("editBookmarkPanelTitle");
let removeElement = document.getElementById("editBookmarkPanelRemoveButton");

function checkBookmarksPanel(invoker, phase)
{
  let onPopupShown = function(aEvent) {
    if (aEvent.originalTarget == popupElement) {
      popupElement.removeEventListener("popupshown", arguments.callee, false);
      checkBookmarksPanel(invoker, phase + 1);
    }
  };
  let onPopupHidden = function(aEvent) {
    if (aEvent.originalTarget == popupElement) {
      popupElement.removeEventListener("popuphidden", arguments.callee, false);
      if (phase < 4) {
        checkBookmarksPanel(invoker, phase + 1);
      } else {
        ++currentInvoker;
        if (currentInvoker < invokers.length) {
          checkBookmarksPanel(invokers[currentInvoker], 1);
        } else {
          gBrowser.removeCurrentTab();
          PlacesUtils.bookmarks.removeItem(bookmarkId);
          executeSoon(finish);
        }
      }
    }
  };

  switch (phase) {
  case 1:
  case 3:
    popupElement.addEventListener("popupshown", onPopupShown, false);
    break;
  case 2:
    popupElement.addEventListener("popuphidden", onPopupHidden, false);
    initialValue = titleElement.value;
    initialRemoveHidden = removeElement.hidden;
    break;
  case 4:
    popupElement.addEventListener("popuphidden", onPopupHidden, false);
    is(titleElement.value, initialValue, "The bookmark panel's title should be the same");
    is(removeElement.hidden, initialRemoveHidden, "The bookmark panel's visibility should not change");
    break;
  }
  invoker(phase);
}
