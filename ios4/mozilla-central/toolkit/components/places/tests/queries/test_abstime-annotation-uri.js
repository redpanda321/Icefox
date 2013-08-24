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
 * The Original Code is Places Test Code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Clint Talbert <ctalbert@mozilla.com>
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

const DAY_MSEC = 86400000;
const MIN_MSEC = 60000;
const HOUR_MSEC = 3600000;
// Jan 6 2008 at 8am is our begin edge of the query
var beginTimeDate = new Date(2008, 0, 6, 8, 0, 0, 0);
// Jan 15 2008 at 9:30pm is our ending edge of the query
var endTimeDate = new Date(2008, 0, 15, 21, 30, 0, 0);

// These as millisecond values
var beginTime = beginTimeDate.getTime();
var endTime = endTimeDate.getTime();

// Some range dates inside our query - mult by 1000 to convert to PRTIME
var jan7_800 = (beginTime + DAY_MSEC) * 1000;
var jan6_815 = (beginTime + (MIN_MSEC * 15)) * 1000;
var jan11_800 = (beginTime + (DAY_MSEC * 5)) * 1000;
var jan14_2130 = (endTime - DAY_MSEC) * 1000;
var jan15_2045 = (endTime - (MIN_MSEC * 45)) * 1000;
var jan12_1730 = (endTime - (DAY_MSEC * 3) - (HOUR_MSEC*4)) * 1000;

// Dates outside our query - mult by 1000 to convert to PRTIME
var jan6_700 = (beginTime - HOUR_MSEC) * 1000;
var jan5_800 = (beginTime - DAY_MSEC) * 1000;
var dec27_800 = (beginTime - (DAY_MSEC * 10)) * 1000;
var jan15_2145 = (endTime + (MIN_MSEC * 15)) * 1000;
var jan16_2130 = (endTime + (DAY_MSEC)) * 1000;
var jan25_2130 = (endTime + (DAY_MSEC * 10)) * 1000;

// So that we can easily use these too, convert them to PRTIME
beginTime *= 1000;
endTime *= 1000;

/**
 * Array of objects to build our test database
 */
var goodAnnoName = "moz-test-places/testing123";
var val = "test";
var badAnnoName = "text/foo";

// The test data for our database, note that the ordering of the results that
// will be returned by the query (the isInQuery: true objects) is IMPORTANT.
// see compareArrayToResult in head_queries.js for more info.
var testData = [

  // Test flat domain with annotation
  {isInQuery: true, isVisit: true, isDetails: true, isPageAnnotation: true,
   uri: "http://foo.com/", annoName: goodAnnoName, annoVal: val,
   lastVisit: jan14_2130, title: "moz"},

  // Test begin edge of time
  {isInQuery: true, isVisit: true, isDetails: true, title: "moz mozilla",
   uri: "http://foo.com/begin.html", lastVisit: beginTime},

  // Test end edge of time
  {isInQuery: true, isVisit: true, isDetails: true, title: "moz mozilla",
   uri: "http://foo.com/end.html", lastVisit: endTime},

  // Test uri included with isRedirect=true, different transtype
  {isInQuery: true, isVisit: true, isDetails: true, title: "moz",
   isRedirect: true, uri: "http://foo.com/redirect", lastVisit: jan11_800,
   transType: PlacesUtils.history.TRANSITION_LINK},

  // Test leading time edge with tag string is included
  {isInQuery: true, isVisit: true, isDetails: true, title: "taggariffic",
   uri: "http://foo.com/tagging/test.html", lastVisit: beginTime, isTag: true,
   tagArray: ["moz"] },

  // Begin the invalid queries: 
  // Test www. style URI is not included, with an annotation
  {isInQuery: false, isVisit: true, isDetails: true, isPageAnnotation: true,
   uri: "http://www.foo.com/yiihah", annoName: goodAnnoName, annoVal: val,
   lastVisit: jan7_800, title: "moz"},

   // Test subdomain not inclued at the leading time edge 
   {isInQuery: false, isVisit: true, isDetails: true,
    uri: "http://mail.foo.com/yiihah", title: "moz", lastVisit: jan6_815},

  // Test https protocol
  {isInQuery: false, isVisit: true, isDetails: true, title: "moz",
   uri: "https://foo.com/", lastVisit: jan15_2045},

   // Test ftp protocol
   {isInQuery: false, isVisit: true, isDetails: true,
    uri: "ftp://foo.com/ftp", lastVisit: jan12_1730,
    title: "hugelongconfmozlagurationofwordswithasearchtermsinit whoo-hoo"},

  // Test too early
  {isInQuery: false, isVisit:true, isDetails: true, title: "moz",
   uri: "http://foo.com/tooearly.php", lastVisit: jan6_700},

  // Test Bad Annotation
  {isInQuery: false, isVisit:true, isDetails: true, isPageAnnotation: true,
   title: "moz", uri: "http://foo.com/badanno.htm", lastVisit: jan12_1730,
   annoName: badAnnoName, annoVal: val},
  
  // Test afterward, one to update
  {isInQuery: false, isVisit:true, isDetails: true, title: "changeme",
   uri: "http://foo.com/changeme1.htm", lastVisit: jan12_1730},

  // Test invalid title
  {isInQuery: false, isVisit:true, isDetails: true, title: "changeme2",
   uri: "http://foo.com/changeme2.htm", lastVisit: jan7_800},

  // Test changing the lastVisit
  {isInQuery: false, isVisit:true, isDetails: true, title: "moz",
   uri: "http://foo.com/changeme3.htm", lastVisit: dec27_800}];

/**
 * This test will test a Query using several terms and do a bit of negative
 * testing for items that should be ignored while querying over history.
 * The Query:WHERE absoluteTime(matches) AND searchTerms AND URI
 *                 AND annotationIsNot(match) GROUP BY Domain, Day SORT BY uri,ascending
 *                 excludeITems(should be ignored)
 */
function run_test() {

  //Initialize database
  populateDB(testData);

  // Query
  var query = PlacesUtils.history.getNewQuery();
  query.beginTime = beginTime;
  query.endTime = endTime;
  query.beginTimeReference = PlacesUtils.history.TIME_RELATIVE_EPOCH;
  query.endTimeReference = PlacesUtils.history.TIME_RELATIVE_EPOCH;
  query.searchTerms = "moz";
  query.uri = uri("http://foo.com");
  query.uriIsPrefix = true;
  query.annotation = "text/foo";
  query.annotationIsNot = true;

  // Options
  var options = PlacesUtils.history.getNewQueryOptions();
  options.sortingMode = options.SORT_BY_URI_ASCENDING;
  options.resultType = options.RESULTS_AS_URI;
  // The next two options should be ignored
  // can't use this one, breaks test - bug 419779
  // options.excludeItems = true;

  // Results
  var result = PlacesUtils.history.executeQuery(query, options);
  var root = result.root;
  root.containerOpen = true;

  // Ensure the result set is correct
  compareArrayToResult(testData, root);

  // Make some changes to the result set
  // Let's add something first
  var addItem = [{isInQuery: true, isVisit: true, isDetails: true, title: "moz",
                 uri: "http://foo.com/i-am-added.html", lastVisit: jan11_800}];
  populateDB(addItem);
  LOG("Adding item foo.com/i-am-added.html");
  do_check_eq(isInResult(addItem, root), true);

  // Let's update something by title
  var change1 = [{isDetails: true, uri: "http://foo.com/changeme1",
                  lastVisit: jan12_1730, title: "moz moz mozzie"}];
  populateDB(change1);
  LOG("LiveUpdate by changing title");
  do_check_eq(isInResult(change1, root), true);

  // Let's update something by annotation
  // Updating a page by removing an annotation does not cause it to join this
  // query set.  I tend to think that it should cause that page to join this
  // query set, because this visit fits all theother specified criteria once the
  // annotation is removed. Uncommenting this will fail the test.
  // This is bug 424050 - appears to happen for both domain and URI queries
  /*var change2 = [{isPageAnnotation: true, uri: "http://foo.com/badannotaion.html",
                  annoName: "text/mozilla", annoVal: "test"}];
  populateDB(change2);
  LOG("LiveUpdate by removing annotation");
  do_check_eq(isInResult(change2, root), true);*/

  // Let's update by adding a visit in the time range for an existing URI
  var change3 = [{isDetails: true, uri: "http://foo.com/changeme3.htm",
                  title: "moz", lastVisit: jan15_2045}];
  populateDB(change3);
  LOG("LiveUpdate by adding visit within timerange");
  do_check_eq(isInResult(change3, root), true);

  // And delete something from the result set - using annotation
  // Once more, bug 424050
  /*var change4 = [{isPageAnnotation: true, uri: "http://foo.com/",
                  annoVal: "test", annoName: badAnnoName}];
  populateDB(change4);
  LOG("LiveUpdate by deleting item from set by adding annotation");
  do_check_eq(isInResult(change4, root), false);*/

  // Delete something by changing the title
  var change5 = [{isDetails: true, uri: "http://foo.com/end.html", title: "deleted"}];
  populateDB(change5);
  LOG("LiveUpdate by deleting item by changing title");
  do_check_eq(isInResult(change5, root), false);

  // Update some in batch mode
  // Adds http://foo.com/changeme2 to the result set and removes foo.com/begin.html
  var updateBatch = {
    runBatched: function (aUserData) {
      var batchChange = [{isDetails: true, uri: "http://foo.com/changeme2",
                          title: "moz", lastVisit: jan7_800},
                         {isPageAnnotation: true, uri: "http://foo.com/begin.html",
                          annoName: badAnnoName, annoVal: val}];
      populateDB(batchChange);
    }
  };

  PlacesUtils.history.runInBatchMode(updateBatch, null);
  LOG("LiveUpdate by updating title in batch mode");
  do_check_eq(isInResult({uri: "http://foo.com/changeme2"}, root), true);

  LOG("LiveUpdate by deleting item by setting annotation in batch mode");
  do_check_eq(isInResult({uri: "http:/foo.com/begin.html"}, root), false);

  root.containerOpen = false;
}
