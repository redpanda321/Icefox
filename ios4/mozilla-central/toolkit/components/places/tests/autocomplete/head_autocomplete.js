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
 * The Original Code is Places Test Code.
 *
 * The Initial Developer of the Original Code is
 * Edward Lee <edward.lee@engineering.uiuc.edu>.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Drew Willcoxon <adw@mozilla.com>
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

const Ci = Components.interfaces;
const Cc = Components.classes;
const Cr = Components.results;
const Cu = Components.utils;

Cu.import("resource://gre/modules/Services.jsm");

// Import common head.
let (commonFile = do_get_file("../head_common.js", false)) {
  let uri = Services.io.newFileURI(commonFile);
  Services.scriptloader.loadSubScript(uri.spec, this);
}

// Put any other stuff relative to this test folder below.


/**
 * Header file for autocomplete testcases that create a set of pages with uris,
 * titles, tags and tests that a given search term matches certain pages.
 */

let current_test = 0;

function AutoCompleteInput(aSearches) {
  this.searches = aSearches;
}
AutoCompleteInput.prototype = {
  timeout: 10,
  textValue: "",
  searches: null,
  searchParam: "",
  popupOpen: false,
  minResultsForPopup: 0,
  invalidate: function() {},
  disableAutoComplete: false,
  completeDefaultIndex: false,
  get popup() { return this; },
  onSearchBegin: function() {},
  onSearchComplete: function() {},
  setSelectedIndex: function() {},
  get searchCount() { return this.searches.length; },
  getSearchAt: function(aIndex) this.searches[aIndex],
  QueryInterface: XPCOMUtils.generateQI([
    Ci.nsIAutoCompleteInput,
    Ci.nsIAutoCompletePopup,
  ])
};

function toURI(aSpec) {
  return uri(aSpec);
}

let appendTags = true;
// Helper to turn off tag matching in results
function ignoreTags()
{
  print("Ignoring tags from results");
  appendTags = false;
}

function ensure_results(aSearch, aExpected)
{
  let controller = Cc["@mozilla.org/autocomplete/controller;1"].
                   getService(Ci.nsIAutoCompleteController);

  // Make an AutoCompleteInput that uses our searches
  // and confirms results on search complete
  let input = new AutoCompleteInput(["history"]);

  controller.input = input;

  if (typeof kSearchParam == "string")
    input.searchParam = kSearchParam;

  let numSearchesStarted = 0;
  input.onSearchBegin = function() {
    numSearchesStarted++;
    do_check_eq(numSearchesStarted, 1);
  };

  input.onSearchComplete = function() {
    do_check_eq(numSearchesStarted, 1);
    aExpected = aExpected.slice();

    // Check to see the expected uris and titles match up (in any order)
    for (let i = 0; i < controller.matchCount; i++) {
      let value = controller.getValueAt(i);
      let comment = controller.getCommentAt(i);

      print("Looking for '" + value + "', '" + comment + "' in expected results...");
      let j;
      for (j = 0; j < aExpected.length; j++) {
        // Skip processed expected results
        if (aExpected[j] == undefined)
          continue;

        let [uri, title, tags] = gPages[aExpected[j]];

        // Load the real uri and titles and tags if necessary
        uri = toURI(kURIs[uri]).spec;
        title = kTitles[title];
        if (tags && appendTags)
          title += " \u2013 " + tags.map(function(aTag) kTitles[aTag]);
        print("Checking against expected '" + uri + "', '" + title + "'...");

        // Got a match on both uri and title?
        if (uri == value && title == comment) {
          print("Got it at index " + j + "!!");
          // Make it undefined so we don't process it again
          aExpected[j] = undefined;
          break;
        }
      }

      // We didn't hit the break, so we must have not found it
      if (j == aExpected.length)
        do_throw("Didn't find the current result ('" + value + "', '" + comment + "') in expected: " + aExpected);
    }

    // Make sure we have the right number of results
    print("Expecting " + aExpected.length + " results; got " +
          controller.matchCount + " results");
    do_check_eq(controller.matchCount, aExpected.length);

    // If we expect results, make sure we got matches
    do_check_eq(controller.searchStatus, aExpected.length ?
                Ci.nsIAutoCompleteController.STATUS_COMPLETE_MATCH :
                Ci.nsIAutoCompleteController.STATUS_COMPLETE_NO_MATCH);

    // Fetch the next test if we have more
    if (++current_test < gTests.length)
      run_test();

    do_test_finished();
  };

  print("Searching for.. '" + aSearch + "'");
  controller.startSearch(aSearch);
}

// Get history services
var histsvc = Cc["@mozilla.org/browser/nav-history-service;1"].
              getService(Ci.nsINavHistoryService);
var bhist = histsvc.QueryInterface(Ci.nsIBrowserHistory);
var bmsvc = Cc["@mozilla.org/browser/nav-bookmarks-service;1"].
            getService(Ci.nsINavBookmarksService);
var tagsvc = Cc["@mozilla.org/browser/tagging-service;1"].
             getService(Ci.nsITaggingService);
var iosvc = Cc["@mozilla.org/network/io-service;1"].
            getService(Ci.nsIIOService);
var prefs = Cc["@mozilla.org/preferences-service;1"].
            getService(Ci.nsIPrefBranch);
var lmsvc = Cc["@mozilla.org/browser/livemark-service;2"].
            getService(Ci.nsILivemarkService);

// Some date not too long ago
let gDate = new Date(Date.now() - 1000 * 60 * 60) * 1000;
// Store the page info for each uri
let gPages = [];


/**
 * Adds a livemark container with a single child, and creates various properties
 * for it depending on the parameters passed in.
 *
 * @param aContainerSiteURI
 *        An index into kURIs that holds the string for the URI of site of the
 *        livemark container we are to add.
 * @param aContainerFeedURI
 *        An index into kURIs that holds the string for the URI of feed of the
 *        livemark container we are to add.
 * @param aContainerTitle
 *        An index into kTitles that holds the string for the title we are to
 *        associate with the livemark container.
 * @param aChildURI
 *        An index into kURIs that holds the string for the URI of single
 *        livemark child we are to add.
 * @param aChildTitle
 *        An index into kTitles that holds the string for the title we are to
 *        associate with the single livemark child.
 * @param aTransitionType [optional]
 *        The transition type to use when adding the visit.  The default is
 *        nsINavHistoryService::TRANSITION_LINK.
 * @param aNoChildVisit [optional]
 *        If true, no visit is added for the child's URI.  If false or
 *        undefined, a visit is added.
 */
function addLivemark(aContainerSiteURI, aContainerFeedURI, aContainerTitle,
                     aChildURI, aChildTitle, aTransitionType, aNoChildVisit)
{
  // Add a page entry for the child uri
  gPages[aChildURI] = [aChildURI, aChildTitle, null];

  let out = [aChildURI, aChildTitle];
  out.push("\nchild uri=" + kURIs[aChildURI]);
  out.push("\nchild title=" + kTitles[aChildTitle]);

  // Create the container
  let containerSiteURI = toURI(kURIs[aContainerSiteURI]);
  let containerFeedURI = toURI(kURIs[aContainerFeedURI]);
  let containerTitle = kTitles[aContainerTitle];
  let containerId = lmsvc.createLivemarkFolderOnly(bmsvc.unfiledBookmarksFolder,
                                                   containerTitle,
                                                   containerSiteURI,
                                                   containerFeedURI,
                                                   bmsvc.DEFAULT_INDEX);
  // Insert the child
  let childURI = toURI(kURIs[aChildURI]);
  let childTitle = kTitles[aChildTitle];
  bmsvc.insertBookmark(containerId, childURI, bmsvc.DEFAULT_INDEX, childTitle);

  // Add a visit to the child if we need to
  if (!aNoChildVisit) {
    let tt = aTransitionType || TRANSITION_LINK;
    let isRedirect = tt == TRANSITION_REDIRECT_PERMANENT ||
                     tt == TRANSITION_REDIRECT_TEMPORARY;
    histsvc.addVisit(childURI, gDate, null, tt, isRedirect, 0);
    out.push("\nwith visit");
  }

  print("\nAdding livemark: " + out.join(", "));
}

/**
 * Adds a page, and creates various properties for it depending on the
 * parameters passed in.  This function will also add one visit, unless
 * aNoVisit is true.
 *
 * @param aURI
 *        An index into kURIs that holds the string for the URI we are to add a
 *        page for.
 * @param aTitle
 *        An index into kTitles that holds the string for the title we are to
 *        associate with the specified URI.
 * @param aBook [optional]
 *        An index into kTitles that holds the string for the title we are to
 *        associate with the bookmark.  If this is undefined, no bookmark is
 *        created.
 * @param aTags [optional]
 *        An array of indexes into kTitles that hold the strings for the tags we
 *        are to associate with the URI.  If this is undefined (or aBook is), no
 *        tags are added.
 * @param aKey [optional]
 *        A string to associate as the keyword for this bookmark.  aBook must be
 *        a valid index into kTitles for this to be checked and used.
 * @param aTransitionType [optional]
 *        The transition type to use when adding the visit.  The default is
 *        nsINavHistoryService::TRANSITION_LINK.
 * @param aNoVisit [optional]
 *        If true, no visit is added for the URI.  If false or undefined, a
 *        visit is added.
 */
function addPageBook(aURI, aTitle, aBook, aTags, aKey, aTransitionType, aNoVisit)
{
  // Add a page entry for the current uri
  gPages[aURI] = [aURI, aBook != undefined ? aBook : aTitle, aTags];

  let uri = toURI(kURIs[aURI]);
  let title = kTitles[aTitle];

  let out = [aURI, aTitle, aBook, aTags, aKey];
  out.push("\nuri=" + kURIs[aURI]);
  out.push("\ntitle=" + title);

  // Add the page and a visit if we need to
  if (!aNoVisit) {
    let tt = aTransitionType || TRANSITION_LINK;
    let isRedirect = tt == TRANSITION_REDIRECT_PERMANENT ||
                     tt == TRANSITION_REDIRECT_TEMPORARY;
    histsvc.addVisit(uri, gDate, null, tt, isRedirect, 0);
    setPageTitle(uri, title);
    out.push("\nwith visit");
  }

  // Add a bookmark if we need to
  if (aBook != undefined) {
    let book = kTitles[aBook];
    let bmid = bmsvc.insertBookmark(bmsvc.unfiledBookmarksFolder, uri,
      bmsvc.DEFAULT_INDEX, book);
    out.push("\nbook=" + book);

    // Add a keyword to the bookmark if we need to
    if (aKey != undefined)
      bmsvc.setKeywordForBookmark(bmid, aKey);

    // Add tags if we need to
    if (aTags != undefined && aTags.length > 0) {
      // Convert each tag index into the title
      let tags = aTags.map(function(aTag) kTitles[aTag]);
      tagsvc.tagURI(uri, tags);
      out.push("\ntags=" + tags);
    }
  }

  print("\nAdding page/book/tag: " + out.join(", "));
}

function run_test() {
  print("\n");
  // always search in history + bookmarks, no matter what the default is
  prefs.setIntPref("browser.urlbar.search.sources", 3);
  prefs.setIntPref("browser.urlbar.default.behavior", 0);

  // Search is asynchronous, so don't let the test finish immediately
  do_test_pending();

  // Load the test and print a description then run the test
  let [description, search, expected, func] = gTests[current_test];
  print(description);

  // By default assume we want to match tags
  appendTags = true;

  // Do an extra function if necessary
  if (func)
    func();

  ensure_results(search, expected);
}

// Utility function to remove history pages
function removePages(aURIs)
{
  for each (let uri in aURIs)
    histsvc.removePage(toURI(kURIs[uri]));
}

// Utility function to mark pages as typed
function markTyped(aURIs)
{
  for each (let uri in aURIs)
    histsvc.addVisit(toURI(kURIs[uri]), Date.now() * 1000, null,
      histsvc.TRANSITION_TYPED, false, 0);
}

