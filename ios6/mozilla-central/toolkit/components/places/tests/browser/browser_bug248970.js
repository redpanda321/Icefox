/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This test performs checks on the history testing area as outlined
// https://wiki.mozilla.org/Firefox3.1/PrivateBrowsing/TestPlan#History
// http://developer.mozilla.org/en/Using_the_Places_history_service

let visitedURIs = [
  "http://www.test-link.com/",
  "http://www.test-typed.com/",
  "http://www.test-bookmark.com/",
  "http://www.test-redirect-permanent.com/",
  "http://www.test-redirect-temporary.com/",
  "http://www.test-embed.com/",
  "http://www.test-framed.com/",
  "http://www.test-download.com/"
];

function test() {
  waitForExplicitFinish();

  let windowsToClose = [];
  let windowCount = 0;
  let placeItemsCount = 0;

  registerCleanupFunction(function() {
    windowsToClose.forEach(function(win) {
      win.close();
    });
  });

  function testOnWindow(aIsPrivate, aCallback) {
    whenNewWindowLoaded(aIsPrivate, function(win) {
      windowsToClose.push(win);
      checkPlaces(win, aIsPrivate, aCallback);
    });
  }

  function checkPlaces(aWindow, aIsPrivate, aCallback) {
    // Updates the place items count
    placeItemsCount = getPlacesItemsCount(aWindow);
    // History items should be retrievable by query
    checkHistoryItems(aWindow);

    // Create Bookmark
    let bookmarkTitle = "title " + windowCount;
    let bookmarkKeyword = "keyword " + windowCount;
    let bookmarkUri = NetUtil.newURI("http://test-a-" + windowCount + ".com/");
    createBookmark(aWindow, bookmarkUri, bookmarkTitle, bookmarkKeyword);
    placeItemsCount++;
    windowCount++;
    ok(aWindow.PlacesUtils.bookmarks.isBookmarked(bookmarkUri),
       "Bookmark should be bookmarked, data should be retrievable");
    is(bookmarkKeyword, aWindow.PlacesUtils.bookmarks.getKeywordForURI(bookmarkUri),
       "Check bookmark uri keyword");
    is(getPlacesItemsCount(aWindow), placeItemsCount,
       "Check the new bookmark items count");
    is(isBookmarkAltered(aWindow), false, "Check if bookmark has been visited");

    aCallback();
  }

  clearHistory(function() {
    // Updates the place items count
    placeItemsCount = getPlacesItemsCount(window);
    // History database should be empty
    is(PlacesUtils.history.hasHistoryEntries, false,
       "History database should be empty");
    // Create a handful of history items with various visit types
    fillHistoryVisitedURI(window);
    placeItemsCount += 7;
    // History database should have entries
    is(PlacesUtils.history.hasHistoryEntries, true,
       "History database should have entries");
    // We added 7 new items to history.
    is(getPlacesItemsCount(window), placeItemsCount,
       "Check the total items count");
    // Test on windows.
    testOnWindow(false, function() {
      testOnWindow(true, function() {
        testOnWindow(false, finish);
      });
    });
  });
}

function whenNewWindowLoaded(aIsPrivate, aCallback) {
  let win = OpenBrowserWindow({private: aIsPrivate});
  win.addEventListener("load", function onLoad() {
    win.removeEventListener("load", onLoad, false);
    aCallback(win);
  }, false);
}

function clearHistory(aCallback) {
  Services.obs.addObserver(function observer(aSubject, aTopic, aData) {
    Services.obs.removeObserver(observer, PlacesUtils.TOPIC_EXPIRATION_FINISHED);
    aCallback();
  }, PlacesUtils.TOPIC_EXPIRATION_FINISHED, false);
  PlacesUtils.bhistory.removeAllPages();
}

/**
 * Function performs a really simple query on our places entries,
 * and makes sure that the number of entries equal num_places_entries.
 */
function getPlacesItemsCount(aWin){
  // Get bookmarks count
  let options = aWin.PlacesUtils.history.getNewQueryOptions();
  options.includeHidden = true;
  options.queryType = Ci.nsINavHistoryQueryOptions.QUERY_TYPE_BOOKMARKS;
  let root = aWin.PlacesUtils.history.executeQuery(
    aWin.PlacesUtils.history.getNewQuery(), options).root;
  root.containerOpen = true;
  let cc = root.childCount;
  root.containerOpen = false;

  // Get history item count
  options.queryType = Ci.nsINavHistoryQueryOptions.QUERY_TYPE_HISTORY;
  let root = aWin.PlacesUtils.history.executeQuery(
    aWin.PlacesUtils.history.getNewQuery(), options).root;
  root.containerOpen = true;
  cc += root.childCount;
  root.containerOpen = false;

  return cc;
}

function addVisit(aWin, aURI, aType) {
  aWin.PlacesUtils.history.addVisit(
    NetUtil.newURI(aURI), Date.now() * 1000, null, aType, false, 0);
}

function fillHistoryVisitedURI(aWin) {
  aWin.PlacesUtils.history.runInBatchMode({
    runBatched: function (aUserData) {
      addVisit(aWin, visitedURIs[0], PlacesUtils.history.TRANSITION_LINK);
      addVisit(aWin, visitedURIs[1], PlacesUtils.history.TRANSITION_TYPED);
      addVisit(aWin, visitedURIs[2], PlacesUtils.history.TRANSITION_BOOKMARK);
      addVisit(aWin, visitedURIs[3], PlacesUtils.history.TRANSITION_REDIRECT_PERMANENT);
      addVisit(aWin, visitedURIs[4], PlacesUtils.history.TRANSITION_REDIRECT_TEMPORARY);
      addVisit(aWin, visitedURIs[5], PlacesUtils.history.TRANSITION_EMBED);
      addVisit(aWin, visitedURIs[6], PlacesUtils.history.TRANSITION_FRAMED_LINK);
      addVisit(aWin, visitedURIs[7], PlacesUtils.history.TRANSITION_DOWNLOAD);
    }
  }, null);
}

function checkHistoryItems(aWin) {
  visitedURIs.forEach(function (visitedUri) {
    ok(aWin.PlacesUtils.bhistory.isVisited(NetUtil.newURI(visitedUri)), "");
    if (/embed/.test(visitedUri)) {
      is(!!pageInDatabase(visitedUri), false, "Check if URI is in database");
    } else {
      ok(!!pageInDatabase(visitedUri), "Check if URI is in database");
    }
  });
}

/**
 * Checks if an address is found in the database.
 * @param aURI
 *        nsIURI or address to look for.
 * @return place id of the page or 0 if not found
 */
function pageInDatabase(aURI) {
  let url = (aURI instanceof Ci.nsIURI ? aURI.spec : aURI);
  let stmt = DBConn().createStatement(
    "SELECT id FROM moz_places WHERE url = :url"
  );
  stmt.params.url = url;
  try {
    if (!stmt.executeStep())
      return 0;
    return stmt.getInt64(0);
  } finally {
    stmt.finalize();
  }
}

/**
 * Gets the database connection.  If the Places connection is invalid it will
 * try to create a new connection.
 *
 * @param [optional] aForceNewConnection
 *        Forces creation of a new connection to the database.  When a
 *        connection is asyncClosed it cannot anymore schedule async statements,
 *        though connectionReady will keep returning true (Bug 726990).
 *
 * @return The database connection or null if unable to get one.
 */
let gDBConn;
function DBConn(aForceNewConnection) {
  if (!aForceNewConnection) {
    let db =
      PlacesUtils.history.QueryInterface(Ci.nsPIPlacesDatabase).DBConnection;
    if (db.connectionReady)
      return db;
  }

  // If the Places database connection has been closed, create a new connection.
  if (!gDBConn || aForceNewConnection) {
    let file = Services.dirsvc.get('ProfD', Ci.nsIFile);
    file.append("places.sqlite");
    let dbConn = gDBConn = Services.storage.openDatabase(file);

    // Be sure to cleanly close this connection.
    Services.obs.addObserver(function DBCloseCallback(aSubject, aTopic, aData) {
      Services.obs.removeObserver(DBCloseCallback, aTopic);
      dbConn.asyncClose();
    }, "profile-before-change", false);
  }

  return gDBConn.connectionReady ? gDBConn : null;
};

/**
 * Function creates a bookmark
 * @param aURI
 *        The URI for the bookmark
 * @param aTitle
 *        The title for the bookmark
 * @param aKeyword
 *        The keyword for the bookmark
 * @returns the bookmark
 */
function createBookmark(aWin, aURI, aTitle, aKeyword) {
  let bookmarkID = aWin.PlacesUtils.bookmarks.insertBookmark(
    aWin.PlacesUtils.bookmarksMenuFolderId, aURI,
    aWin.PlacesUtils.bookmarks.DEFAULT_INDEX, aTitle);
  aWin.PlacesUtils.bookmarks.setKeywordForBookmark(bookmarkID, aKeyword);
  return bookmarkID;
}

/**
 * Function attempts to check if Bookmark-A has been visited
 * during private browsing mode, function should return false
 *
 * @returns false if the accessCount has not changed
 *          true if the accessCount has changed
 */
function isBookmarkAltered(aWin){
  let options = aWin.PlacesUtils.history.getNewQueryOptions();
  options.queryType = Ci.nsINavHistoryQueryOptions.QUERY_TYPE_BOOKMARKS;
  options.maxResults = 1; // should only expect a new bookmark
  options.resultType = options.RESULT_TYPE_VISIT;

  let query = aWin.PlacesUtils.history.getNewQuery();
  query.setFolders([aWin.PlacesUtils.bookmarks.bookmarksMenuFolder], 1);

  let root = aWin.PlacesUtils.history.executeQuery(query, options).root;
  root.containerOpen = true;
  is(root.childCount, options.maxResults, "Check new bookmarks results");
  let node = root.getChild(0);
  root.containerOpen = false;

  return (node.accessCount != 0);
}
