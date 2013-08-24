/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

let testURL = "data:text/plain,nothing but plain text";
let testTag = "581253_tag";
let starButton = document.getElementById("star-button");
let timerID = -1;

function test() {
  registerCleanupFunction(function() {
    PlacesUtils.bookmarks.removeFolderChildren(PlacesUtils.unfiledBookmarksFolderId);
    if (timerID > 0) {
      clearTimeout(timerID);
    }
  });
  waitForExplicitFinish();

  let tab = gBrowser.selectedTab = gBrowser.addTab();
  tab.linkedBrowser.addEventListener("load", (function(event) {
    tab.linkedBrowser.removeEventListener("load", arguments.callee, true);

    let uri = makeURI(testURL);
    let bmTxn =
      new PlacesCreateBookmarkTransaction(uri,
                                          PlacesUtils.unfiledBookmarksFolderId,
                                          -1, "", null, []);
    PlacesUtils.transactionManager.doTransaction(bmTxn);

    ok(PlacesUtils.bookmarks.isBookmarked(uri), "the test url is bookmarked");
    waitForStarChange(true, onStarred);
  }), true);

  content.location = testURL;
}

function waitForStarChange(aValue, aCallback) {
  if (PlacesStarButton._pendingStmt || starButton.hasAttribute("starred") != aValue) {
    info("Waiting for star button change.");
    info("pendingStmt: " + (!!PlacesStarButton._pendingStmt) + ", hasAttribute: " + starButton.hasAttribute("starred") + ", tracked uri: " + PlacesStarButton._uri.spec);
    timerID = setTimeout(arguments.callee, 50, aValue, aCallback);
    return;
  }
  timerID = -1;
  aCallback();
}

function onStarred() {
  ok(starButton.getAttribute("starred") == "true",
     "star button indicates that the page is bookmarked");

  let uri = makeURI(testURL);
  let tagTxn = new PlacesTagURITransaction(uri, [testTag]);
  PlacesUtils.transactionManager.doTransaction(tagTxn);

  StarUI.panel.addEventListener("popupshown", onPanelShown, false);
  starButton.click();
}

function onPanelShown(aEvent) {
  if (aEvent.target == StarUI.panel) {
    StarUI.panel.removeEventListener("popupshown", arguments.callee, false);
    let tagsField = document.getElementById("editBMPanel_tagsField");
    ok(tagsField.value == testTag, "tags field value was set");
    tagsField.focus();

    StarUI.panel.addEventListener("popuphidden", onPanelHidden, false);
    let removeButton = document.getElementById("editBookmarkPanelRemoveButton");
    removeButton.click();
  }
}

/**
 * Clears history invoking callback when done.
 */
function waitForClearHistory(aCallback)
{
  let observer = {
    observe: function(aSubject, aTopic, aData)
    {
      Services.obs.removeObserver(this, PlacesUtils.TOPIC_EXPIRATION_FINISHED);
      aCallback(aSubject, aTopic, aData);
    }
  };
  Services.obs.addObserver(observer, PlacesUtils.TOPIC_EXPIRATION_FINISHED, false);
  PlacesUtils.bhistory.removeAllPages();
}

function onPanelHidden(aEvent) {
  if (aEvent.target == StarUI.panel) {
    StarUI.panel.removeEventListener("popuphidden", arguments.callee, false);

    executeSoon(function() {
      ok(!PlacesUtils.bookmarks.isBookmarked(makeURI(testURL)),
         "the bookmark for the test url has been removed");
      ok(!starButton.hasAttribute("starred"),
         "star button indicates that the bookmark has been removed");
      gBrowser.removeCurrentTab();
      waitForClearHistory(finish);
    });
  }
}
