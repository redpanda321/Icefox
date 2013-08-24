/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Places test code.
 *
 * The Initial Developer of the Original Code is Mozilla Corp.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Drew Willcoxon <adw@mozilla.com> (Original Author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/**
 * Tests the bookmarks-restore-* nsIObserver notifications after restoring
 * bookmarks from JSON and HTML.  See bug 470314.
 */

// The topics and data passed to nsIObserver.observe() on bookmarks restore
const NSIOBSERVER_TOPIC_BEGIN    = "bookmarks-restore-begin";
const NSIOBSERVER_TOPIC_SUCCESS  = "bookmarks-restore-success";
const NSIOBSERVER_TOPIC_FAILED   = "bookmarks-restore-failed";
const NSIOBSERVER_DATA_JSON      = "json";
const NSIOBSERVER_DATA_HTML      = "html";
const NSIOBSERVER_DATA_HTML_INIT = "html-initial";

// Bookmarks are added for these URIs
var uris = [
  "http://example.com/1",
  "http://example.com/2",
  "http://example.com/3",
  "http://example.com/4",
  "http://example.com/5",
];

// Add tests here.  Each is an object with these properties:
//   desc:       description printed before test is run
//   currTopic:  the next expected topic that should be observed for the test;
//               set to NSIOBSERVER_TOPIC_BEGIN to begin
//   finalTopic: the last expected topic that should be observed for the test,
//               which then causes the next test to be run
//   data:       the data passed to nsIObserver.observe() corresponding to the
//               test
//   file:       the nsILocalFile that the test creates
//   folderId:   for HTML restore into a folder, the folder ID to restore into;
//               otherwise, set it to null
//   run:        a method that actually runs the test
var tests = [
  {
    desc:       "JSON restore: normal restore should succeed",
    currTopic:  NSIOBSERVER_TOPIC_BEGIN,
    finalTopic: NSIOBSERVER_TOPIC_SUCCESS,
    data:       NSIOBSERVER_DATA_JSON,
    folderId:   null,
    run:        function () {
      this.file = createFile("bookmarks-test_restoreNotification.json");
      addBookmarks();
      PlacesUtils.backups.saveBookmarksToJSONFile(this.file);
      remove_all_bookmarks();
      try {
        PlacesUtils.restoreBookmarksFromJSONFile(this.file);
      }
      catch (e) {
        do_throw("  Restore should not have failed");
      }
    }
  },

  {
    desc:       "JSON restore: empty file should succeed",
    currTopic:  NSIOBSERVER_TOPIC_BEGIN,
    finalTopic: NSIOBSERVER_TOPIC_SUCCESS,
    data:       NSIOBSERVER_DATA_JSON,
    folderId:   null,
    run:        function () {
      this.file = createFile("bookmarks-test_restoreNotification.json");
      try {
        PlacesUtils.restoreBookmarksFromJSONFile(this.file);
      }
      catch (e) {
        do_throw("  Restore should not have failed");
      }
    }
  },

  {
    desc:       "JSON restore: nonexistent file should fail",
    currTopic:  NSIOBSERVER_TOPIC_BEGIN,
    finalTopic: NSIOBSERVER_TOPIC_FAILED,
    data:       NSIOBSERVER_DATA_JSON,
    folderId:   null,
    run:        function () {
      this.file = Services.dirsvc.get("ProfD", Ci.nsILocalFile);
      this.file.append("this file doesn't exist because nobody created it");
      try {
        PlacesUtils.restoreBookmarksFromJSONFile(this.file);
        do_throw("  Restore should have failed");
      }
      catch (e) {}
    }
  },

  {
    desc:       "HTML restore: normal restore should succeed",
    currTopic:  NSIOBSERVER_TOPIC_BEGIN,
    finalTopic: NSIOBSERVER_TOPIC_SUCCESS,
    data:       NSIOBSERVER_DATA_HTML,
    folderId:   null,
    run:        function () {
      this.file = createFile("bookmarks-test_restoreNotification.html");
      addBookmarks();
      importer.exportHTMLToFile(this.file);
      remove_all_bookmarks();
      try {
        importer.importHTMLFromFile(this.file, false);
      }
      catch (e) {
        do_throw("  Restore should not have failed");
      }
    }
  },

  {
    desc:       "HTML restore: empty file should succeed",
    currTopic:  NSIOBSERVER_TOPIC_BEGIN,
    finalTopic: NSIOBSERVER_TOPIC_SUCCESS,
    data:       NSIOBSERVER_DATA_HTML,
    folderId:   null,
    run:        function () {
      this.file = createFile("bookmarks-test_restoreNotification.init.html");
      try {
        importer.importHTMLFromFile(this.file, false);
      }
      catch (e) {
        do_throw("  Restore should not have failed");
      }
    }
  },

  {
    desc:       "HTML restore: nonexistent file should fail",
    currTopic:  NSIOBSERVER_TOPIC_BEGIN,
    finalTopic: NSIOBSERVER_TOPIC_FAILED,
    data:       NSIOBSERVER_DATA_HTML,
    folderId:   null,
    run:        function () {
      this.file = Services.dirsvc.get("ProfD", Ci.nsILocalFile);
      this.file.append("this file doesn't exist because nobody created it");
      try {
        importer.importHTMLFromFile(this.file, false);
        do_throw("  Restore should have failed");
      }
      catch (e) {}
    }
  },

  {
    desc:       "HTML initial restore: normal restore should succeed",
    currTopic:  NSIOBSERVER_TOPIC_BEGIN,
    finalTopic: NSIOBSERVER_TOPIC_SUCCESS,
    data:       NSIOBSERVER_DATA_HTML_INIT,
    folderId:   null,
    run:        function () {
      this.file = createFile("bookmarks-test_restoreNotification.init.html");
      addBookmarks();
      importer.exportHTMLToFile(this.file);
      remove_all_bookmarks();
      try {
        importer.importHTMLFromFile(this.file, true);
      }
      catch (e) {
        do_throw("  Restore should not have failed");
      }
    }
  },

  {
    desc:       "HTML initial restore: empty file should succeed",
    currTopic:  NSIOBSERVER_TOPIC_BEGIN,
    finalTopic: NSIOBSERVER_TOPIC_SUCCESS,
    data:       NSIOBSERVER_DATA_HTML_INIT,
    folderId:   null,
    run:        function () {
      this.file = createFile("bookmarks-test_restoreNotification.init.html");
      try {
        importer.importHTMLFromFile(this.file, true);
      }
      catch (e) {
        do_throw("  Restore should not have failed");
      }
    }
  },

  {
    desc:       "HTML initial restore: nonexistent file should fail",
    currTopic:  NSIOBSERVER_TOPIC_BEGIN,
    finalTopic: NSIOBSERVER_TOPIC_FAILED,
    data:       NSIOBSERVER_DATA_HTML_INIT,
    folderId:   null,
    run:        function () {
      this.file = Services.dirsvc.get("ProfD", Ci.nsILocalFile);
      this.file.append("this file doesn't exist because nobody created it");
      try {
        importer.importHTMLFromFile(this.file, true);
        do_throw("  Restore should have failed");
      }
      catch (e) {}
    }
  },

  {
    desc:       "HTML restore into folder: normal restore should succeed",
    currTopic:  NSIOBSERVER_TOPIC_BEGIN,
    finalTopic: NSIOBSERVER_TOPIC_SUCCESS,
    data:       NSIOBSERVER_DATA_HTML,
    run:        function () {
      this.file = createFile("bookmarks-test_restoreNotification.html");
      addBookmarks();
      importer.exportHTMLToFile(this.file);
      remove_all_bookmarks();
      this.folderId = bmsvc.createFolder(bmsvc.unfiledBookmarksFolder,
                                         "test folder",
                                         bmsvc.DEFAULT_INDEX);
      print("  Sanity check: createFolder() should have succeeded");
      do_check_true(this.folderId > 0);
      try {
        importer.importHTMLFromFileToFolder(this.file, this.folderId, false);
      }
      catch (e) {
        do_throw("  Restore should not have failed");
      }
    }
  },

  {
    desc:       "HTML restore into folder: empty file should succeed",
    currTopic:  NSIOBSERVER_TOPIC_BEGIN,
    finalTopic: NSIOBSERVER_TOPIC_SUCCESS,
    data:       NSIOBSERVER_DATA_HTML,
    run:        function () {
      this.file = createFile("bookmarks-test_restoreNotification.init.html");
      this.folderId = bmsvc.createFolder(bmsvc.unfiledBookmarksFolder,
                                         "test folder",
                                         bmsvc.DEFAULT_INDEX);
      print("  Sanity check: createFolder() should have succeeded");
      do_check_true(this.folderId > 0);
      try {
        importer.importHTMLFromFileToFolder(this.file, this.folderId, false);
      }
      catch (e) {
        do_throw("  Restore should not have failed");
      }
    }
  },

  {
    desc:       "HTML restore into folder: nonexistent file should fail",
    currTopic:  NSIOBSERVER_TOPIC_BEGIN,
    finalTopic: NSIOBSERVER_TOPIC_FAILED,
    data:       NSIOBSERVER_DATA_HTML,
    run:        function () {
      this.file = Services.dirsvc.get("ProfD", Ci.nsILocalFile);
      this.file.append("this file doesn't exist because nobody created it");
      this.folderId = bmsvc.createFolder(bmsvc.unfiledBookmarksFolder,
                                         "test folder",
                                         bmsvc.DEFAULT_INDEX);
      print("  Sanity check: createFolder() should have succeeded");
      do_check_true(this.folderId > 0);
      try {
        importer.importHTMLFromFileToFolder(this.file, this.folderId, false);
        do_throw("  Restore should have failed");
      }
      catch (e) {}
    }
  }
];

// nsIObserver that observes bookmarks-restore-begin.
var beginObserver = {
  observe: function _beginObserver(aSubject, aTopic, aData) {
    var test = tests[currTestIndex];

    print("  Observed " + aTopic);
    print("  Topic for current test should be what is expected");
    do_check_eq(aTopic, test.currTopic);

    print("  Data for current test should be what is expected");
    do_check_eq(aData, test.data);

    // Update current expected topic to the next expected one.
    test.currTopic = test.finalTopic;
  }
};

// nsIObserver that observes bookmarks-restore-success/failed.  This starts
// the next test.
var successAndFailedObserver = {
  observe: function _successAndFailedObserver(aSubject, aTopic, aData) {
    var test = tests[currTestIndex];

    print("  Observed " + aTopic);
    print("  Topic for current test should be what is expected");
    do_check_eq(aTopic, test.currTopic);

    print("  Data for current test should be what is expected");
    do_check_eq(aData, test.data);

    // On restore failed, file may not exist, so wrap in try-catch.
    try {
      test.file.remove(false);
    }
    catch (exc) {}

    // Make sure folder ID is what is expected.  For importing HTML into a
    // folder, this will be an integer, otherwise null.
    if (aSubject) {
      do_check_eq(aSubject.QueryInterface(Ci.nsISupportsPRInt64).data,
                  test.folderId);
    }
    else
      do_check_eq(test.folderId, null);

    remove_all_bookmarks();
    doNextTest();
  }
};

// Index of the currently running test.  See doNextTest().
var currTestIndex = -1;

var bmsvc = Cc["@mozilla.org/browser/nav-bookmarks-service;1"].
            getService(Ci.nsINavBookmarksService);

var obssvc = Cc["@mozilla.org/observer-service;1"].
             getService(Ci.nsIObserverService);

var importer = Cc["@mozilla.org/browser/places/import-export-service;1"].
               getService(Ci.nsIPlacesImportExportService);

///////////////////////////////////////////////////////////////////////////////

/**
 * Adds some bookmarks for the URIs in |uris|.
 */
function addBookmarks() {
  uris.forEach(function (u) bmsvc.insertBookmark(bmsvc.bookmarksMenuFolder,
                                                 uri(u),
                                                 bmsvc.DEFAULT_INDEX,
                                                 u));
  checkBookmarksExist();
}

/**
 * Checks that all of the bookmarks created for |uris| exist.  It works by
 * creating one query per URI and then ORing all the queries.  The number of
 * results returned should be uris.length.
 */
function checkBookmarksExist() {
  var hs = Cc["@mozilla.org/browser/nav-history-service;1"].
           getService(Ci.nsINavHistoryService);
  var queries = uris.map(function (u) {
    var q = hs.getNewQuery();
    q.uri = uri(u);
    return q;
  });
  var options = hs.getNewQueryOptions();
  options.queryType = options.QUERY_TYPE_BOOKMARKS;
  var root = hs.executeQueries(queries, uris.length, options).root;
  root.containerOpen = true;
  do_check_eq(root.childCount, uris.length);
  root.containerOpen = false;
}

/**
 * Creates an nsILocalFile in the profile directory.
 *
 * @param  aBasename
 *         e.g., "foo.txt" in the path /some/long/path/foo.txt
 * @return The nsILocalFile
 */
function createFile(aBasename) {
  var file = Services.dirsvc.get("ProfD", Ci.nsILocalFile);
  file.append(aBasename);
  if (file.exists())
    file.remove(false);
  file.create(file.NORMAL_FILE_TYPE, 0666);
  if (!file.exists())
    do_throw("Couldn't create file: " + aBasename);
  return file;
}

/**
 * Runs the next test or if all tests have been run, finishes.
 */
function doNextTest() {
  currTestIndex++;
  if (currTestIndex >= tests.length) {
    obssvc.removeObserver(beginObserver, NSIOBSERVER_TOPIC_BEGIN);
    obssvc.removeObserver(successAndFailedObserver, NSIOBSERVER_TOPIC_SUCCESS);
    obssvc.removeObserver(successAndFailedObserver, NSIOBSERVER_TOPIC_FAILED);
    do_test_finished();
  }
  else {
    var test = tests[currTestIndex];
    print("Running test: " + test.desc);
    test.run();
  }
}

///////////////////////////////////////////////////////////////////////////////

function run_test() {
  do_test_pending();
  obssvc.addObserver(beginObserver, NSIOBSERVER_TOPIC_BEGIN, false);
  obssvc.addObserver(successAndFailedObserver, NSIOBSERVER_TOPIC_SUCCESS, false);
  obssvc.addObserver(successAndFailedObserver, NSIOBSERVER_TOPIC_FAILED, false);
  doNextTest();
}
