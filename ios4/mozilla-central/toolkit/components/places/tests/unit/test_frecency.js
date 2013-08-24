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
 * The Original Code is Bug 378079 unit test code.
 *
 * The Initial Developer of the Original Code is POTI Inc.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Matt Crocker <matt@songbirdnest.com>
 *   Seth Spitzer <sspitzer@mozilla.org>
 *   Edward Lee <edward.lee@engineering.uiuc.edu>
 *   Kyle Huey <me@kylehuey.com>
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
 * Test for bug 406358 to make sure frecency works for empty input/search, but
 * this also tests for non-empty inputs as well. Because the interactions among
 * *DIFFERENT* visit counts and visit dates is not well defined, this test
 * holds one of the two values constant when modifying the other.
 *
 * Also test bug 419068 to make sure tagged pages don't necessarily have to be
 * first in the results.
 *
 * Also test bug 426166 to make sure that the results of autocomplete searches
 * are stable.  Note that failures of this test will be intermittent by nature
 * since we are testing to make sure that the unstable sort algorithm used
 * by SQLite is not changing the order of the results on us.
 */

function AutoCompleteInput(aSearches) {
  this.searches = aSearches;
}
AutoCompleteInput.prototype = {
  constructor: AutoCompleteInput,

  searches: null,

  minResultsForPopup: 0,
  timeout: 10,
  searchParam: "",
  textValue: "",
  disableAutoComplete: false,
  completeDefaultIndex: false,

  get searchCount() {
    return this.searches.length;
  },

  getSearchAt: function(aIndex) {
    return this.searches[aIndex];
  },

  onSearchBegin: function() {},
  onSearchComplete: function() {},

  popupOpen: false,

  popup: {
    setSelectedIndex: function(aIndex) {},
    invalidate: function() {},

    // nsISupports implementation
    QueryInterface: function(iid) {
      if (iid.equals(Ci.nsISupports) ||
          iid.equals(Ci.nsIAutoCompletePopup))
        return this;

      throw Components.results.NS_ERROR_NO_INTERFACE;
    }
  },

  // nsISupports implementation
  QueryInterface: function(iid) {
    if (iid.equals(Ci.nsISupports) ||
        iid.equals(Ci.nsIAutoCompleteInput))
      return this;

    throw Components.results.NS_ERROR_NO_INTERFACE;
  }
}

function ensure_results(uris, searchTerm)
{
  var controller = Components.classes["@mozilla.org/autocomplete/controller;1"].
                   getService(Components.interfaces.nsIAutoCompleteController);

  // Make an AutoCompleteInput that uses our searches
  // and confirms results on search complete
  var input = new AutoCompleteInput(["history"]);

  controller.input = input;

  var numSearchesStarted = 0;
  input.onSearchBegin = function() {
    numSearchesStarted++;
    do_check_eq(numSearchesStarted, 1);
  };

  input.onSearchComplete = function() {
    do_check_eq(numSearchesStarted, 1);
    do_check_eq(controller.searchStatus,
                Ci.nsIAutoCompleteController.STATUS_COMPLETE_MATCH);
    do_check_eq(controller.matchCount, uris.length);
    for (var i=0; i<controller.matchCount; i++) {
      do_check_eq(controller.getValueAt(i), uris[i].spec);
    }

    next_test();
  };

  controller.startSearch(searchTerm);
}

// Get history service
try {
  var histsvc = Cc["@mozilla.org/browser/nav-history-service;1"].
                getService(Ci.nsINavHistoryService);
  var bhist = histsvc.QueryInterface(Ci.nsIBrowserHistory);
  var tagssvc = Cc["@mozilla.org/browser/tagging-service;1"].
                getService(Ci.nsITaggingService);
  var bmksvc = Cc["@mozilla.org/browser/nav-bookmarks-service;1"].
                getService(Ci.nsINavBookmarksService);
} catch(ex) {
  do_throw("Could not get history service\n");
} 

function setCountDate(aURI, aCount, aDate)
{
  // We need visits so that frecency can be computed over multiple visits
  for (let i = 0; i < aCount; i++)
    histsvc.addVisit(aURI, aDate, null, histsvc.TRANSITION_TYPED, false, 0);
}

function setBookmark(aURI)
{
  bmksvc.insertBookmark(bmksvc.bookmarksMenuFolder, aURI, -1, "bleh");
}

function tagURI(aURI, aTags) {
  bmksvc.insertBookmark(bmksvc.unfiledBookmarksFolder, aURI,
                        bmksvc.DEFAULT_INDEX, "bleh");
  tagssvc.tagURI(aURI, aTags);
}

var uri1 = uri("http://site.tld/1");
var uri2 = uri("http://site.tld/2");
var uri3 = uri("http://aaaaaaaaaa/1");
var uri4 = uri("http://aaaaaaaaaa/2");

// d1 is younger (should show up higher) than d2 (PRTime is in usecs not msec)
// Make sure the dates fall into different frecency buckets
var d1 = new Date(Date.now() - 1000 * 60 * 60) * 1000;
var d2 = new Date(Date.now() - 1000 * 60 * 60 * 24 * 10) * 1000;
// c1 is larger (should show up higher) than c2
var c1 = 10;
var c2 = 1;

var tests = [
// test things without a search term
function() {
  print("Test 0: same count, different date");
  setCountDate(uri1, c1, d1);
  setCountDate(uri2, c1, d2);
  tagURI(uri1, ["site"]);
  ensure_results([uri1, uri2], "");
},
function() {
  print("Test 1: same count, different date");
  setCountDate(uri1, c1, d2);
  setCountDate(uri2, c1, d1);
  tagURI(uri1, ["site"]);
  ensure_results([uri2, uri1], "");
},
function() {
  print("Test 2: different count, same date");
  setCountDate(uri1, c1, d1);
  setCountDate(uri2, c2, d1);
  tagURI(uri1, ["site"]);
  ensure_results([uri1, uri2], "");
},
function() {
  print("Test 3: different count, same date");
  setCountDate(uri1, c2, d1);
  setCountDate(uri2, c1, d1);
  tagURI(uri1, ["site"]);
  ensure_results([uri2, uri1], "");
},

// test things with a search term
function() {
  print("Test 4: same count, different date");
  setCountDate(uri1, c1, d1);
  setCountDate(uri2, c1, d2);
  tagURI(uri1, ["site"]);
  ensure_results([uri1, uri2], "site");
},
function() {
  print("Test 5: same count, different date");
  setCountDate(uri1, c1, d2);
  setCountDate(uri2, c1, d1);
  tagURI(uri1, ["site"]);
  ensure_results([uri2, uri1], "site");
},
function() {
  print("Test 6: different count, same date");
  setCountDate(uri1, c1, d1);
  setCountDate(uri2, c2, d1);
  tagURI(uri1, ["site"]);
  ensure_results([uri1, uri2], "site");
},
function() {
  print("Test 7: different count, same date");
  setCountDate(uri1, c2, d1);
  setCountDate(uri2, c1, d1);
  tagURI(uri1, ["site"]);
  ensure_results([uri2, uri1], "site");
},
// There are multiple tests for 8, hence the multiple functions
// Bug 426166 section
function() {
  print("Test 8.1: same count, same date");  
  setBookmark(uri3);
  setBookmark(uri4);
  ensure_results([uri4, uri3], "a");
},
function() {
  print("Test 8.1: same count, same date");  
  setBookmark(uri3);
  setBookmark(uri4);
  ensure_results([uri4, uri3], "aa");
},
function() {
  print("Test 8.2: same count, same date");
  setBookmark(uri3);
  setBookmark(uri4);
  ensure_results([uri4, uri3], "aaa");
},
function() {
  print("Test 8.3: same count, same date");
  setBookmark(uri3);
  setBookmark(uri4);
  ensure_results([uri4, uri3], "aaaa");
},
function() {
  print("Test 8.4: same count, same date");
  setBookmark(uri3);
  setBookmark(uri4);
  ensure_results([uri4, uri3], "aaa");
},
function() {
  print("Test 8.5: same count, same date");
  setBookmark(uri3);
  setBookmark(uri4);
  ensure_results([uri4, uri3], "aa");
},
function() {
  print("Test 8.6: same count, same date");
  setBookmark(uri3);
  setBookmark(uri4);
  ensure_results([uri4, uri3], "a");
}
];

/**
 * Test adaptive autocomplete
 */
function run_test() {
  // always search in history + bookmarks, no matter what the default is
  var prefs = Cc["@mozilla.org/preferences-service;1"].
              getService(Ci.nsIPrefBranch);
  prefs.setIntPref("browser.urlbar.search.sources", 3);
  prefs.setIntPref("browser.urlbar.default.behavior", 0);

  do_test_pending();
  next_test();
}

function next_test() {
  if (tests.length) {
    remove_all_bookmarks();
    let test = tests.shift();
    waitForClearHistory(test);
  }
  else
    do_test_finished();
}
