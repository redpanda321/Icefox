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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Google Inc.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Darin Fisher <darin@meer.net>
 *  Dietrich Ayala <dietrich@mozilla.com>
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

// Get history service
try {
  var histsvc = Cc["@mozilla.org/browser/nav-history-service;1"].getService(Ci.nsINavHistoryService);
} catch(ex) {
  do_throw("Could not get history service\n");
} 

// Get global history service
try {
  var bhist = Cc["@mozilla.org/browser/global-history;2"].getService(Ci.nsIBrowserHistory);
} catch(ex) {
  do_throw("Could not get history service\n");
} 

// adds a test URI visit to the database, and checks for a valid place ID
function add_visit(aURI, aDate) {
  var date = aDate || Date.now() * 1000;
  var placeID = histsvc.addVisit(aURI,
                                 date,
                                 null, // no referrer
                                 histsvc.TRANSITION_TYPED, // user typed in URL bar
                                 false, // not redirect
                                 0);
  do_check_true(placeID > 0);
  return placeID;
}

var resultObserver = {
  insertedNode: null,
  nodeInserted: function(parent, node, newIndex) {
    this.insertedNode = node;
  },
  removedNode: null,
  nodeRemoved: function(parent, node, oldIndex) {
    this.removedNode = node;
  },

  newTitle: "",
  nodeChangedByTitle: null,
  nodeTitleChanged: function(node, newTitle) {
    this.nodeChangedByTitle = node;
    this.newTitle = newTitle;
  },

  newAccessCount: 0,
  newTime: 0,
  nodeChangedByHistoryDetails: null,
  nodeHistoryDetailsChanged: function(node,
                                         updatedVisitDate,
                                         updatedVisitCount) {
    this.nodeChangedByHistoryDetails = node
    this.newTime = updatedVisitDate;
    this.newAccessCount = updatedVisitCount;
  },

  replacedNode: null,
  nodeReplaced: function(parent, oldNode, newNode, index) {
    this.replacedNode = node;
  },
  movedNode: null,
  nodeMoved: function(node, oldParent, oldIndex, newParent, newIndex) {
    this.movedNode = node;
  },
  openedContainer: null,
  containerOpened: function(node) {
    this.openedContainer = node;
  },
  closedContainer: null,
  containerClosed: function(node) {
    this.closedContainer = node;
  },
  containerStateChanged: function (node, oldState, newState) {},
  invalidatedContainer: null,
  invalidateContainer: function(node) {    
    this.invalidatedContainer = node;
  },
  sortingMode: null,
  sortingChanged: function(sortingMode) {
    this.sortingMode = sortingMode;
  },
  result: null,
  reset: function() {
    this.insertedNode = null;
    this.removedNode = null;
    this.nodeChangedByTitle = null;
    this.nodeChangedByHistoryDetails = null;
    this.replacedNode = null;
    this.movedNode = null;
    this.openedContainer = null;
    this.closedContainer = null;
    this.invalidatedContainer = null;
    this.sortingMode = null;
  }
};

// main
function run_test() {

  // history query
  var options = histsvc.getNewQueryOptions();
  options.sortingMode = options.SORT_BY_DATE_DESCENDING;
  options.resultType = options.RESULTS_AS_VISIT;
  var query = histsvc.getNewQuery();
  var result = histsvc.executeQuery(query, options);
  result.addObserver(resultObserver, false);
  var root = result.root;
  root.containerOpen = true;

  // nsINavHistoryResultObserver.containerOpened
  do_check_neq(resultObserver.openedContainer, null);

  // nsINavHistoryResultObserver.nodeInserted
  // add a visit
  var testURI = uri("http://mozilla.com");
  add_visit(testURI);
  do_check_eq(testURI.spec, resultObserver.insertedNode.uri);

  // nsINavHistoryResultObserver.nodeHistoryDetailsChanged
  // adding a visit causes nodeHistoryDetailsChanged for the folder
  do_check_eq(root.uri, resultObserver.nodeChangedByHistoryDetails.uri);

  // nsINavHistoryResultObserver.itemTitleChanged for a leaf node
  bhist.addPageWithDetails(testURI, "baz", Date.now() * 1000);
  do_check_eq(resultObserver.nodeChangedByTitle.title, "baz");

  // nsINavHistoryResultObserver.nodeRemoved
  var removedURI = uri("http://google.com");
  add_visit(removedURI);
  bhist.removePage(removedURI);
  do_check_eq(removedURI.spec, resultObserver.removedNode.uri);

  // XXX nsINavHistoryResultObserver.nodeReplaced
  // NHQRN.onVisit()->NHCRN.MergeResults()->NHCRN.ReplaceChildURIAt()->NHRV.NodeReplaced()

  // nsINavHistoryResultObserver.invalidateContainer
  bhist.removePagesFromHost("mozilla.com", false);
  do_check_eq(root.uri, resultObserver.invalidatedContainer.uri);

  // nsINavHistoryResultObserver.sortingChanged
  resultObserver.invalidatedContainer = null;
  result.sortingMode = options.SORT_BY_TITLE_ASCENDING;
  do_check_eq(resultObserver.sortingMode, options.SORT_BY_TITLE_ASCENDING);
  do_check_eq(resultObserver.invalidatedContainer, result.root);

  // nsINavHistoryResultObserver.containerClosed
  root.containerOpen = false;
  do_check_eq(resultObserver.closedContainer, resultObserver.openedContainer);
  result.removeObserver(resultObserver);

  // bookmarks query
  
  // Reset the result observer.
  resultObserver.reset();

  try {
    var bmsvc = Cc["@mozilla.org/browser/nav-bookmarks-service;1"].getService(Ci.nsINavBookmarksService);
  } catch(ex) {
    do_throw("Could not get nav-bookmarks-service\n");
  }

  var options = histsvc.getNewQueryOptions();
  var query = histsvc.getNewQuery();
  query.setFolders([bmsvc.bookmarksMenuFolder], 1);
  var result = histsvc.executeQuery(query, options);
  result.addObserver(resultObserver, false);
  var root = result.root;
  root.containerOpen = true;

  // nsINavHistoryResultObserver.containerOpened
  do_check_neq(resultObserver.openedContainer, null);

  // nsINavHistoryResultObserver.nodeInserted
  // add a bookmark
  var testBookmark = bmsvc.insertBookmark(bmsvc.bookmarksMenuFolder, testURI, bmsvc.DEFAULT_INDEX, "foo");
  do_check_eq("foo", resultObserver.insertedNode.title);
  do_check_eq(testURI.spec, resultObserver.insertedNode.uri);

  // nsINavHistoryResultObserver.nodeHistoryDetailsChanged
  // adding a visit causes nodeHistoryDetailsChanged for the folder
  do_check_eq(root.uri, resultObserver.nodeChangedByHistoryDetails.uri);

  // nsINavHistoryResultObserver.nodeTitleChanged for a leaf node
  bmsvc.setItemTitle(testBookmark, "baz");
  do_check_eq(resultObserver.nodeChangedByTitle.title, "baz");
  do_check_eq(resultObserver.newTitle, "baz");

  var testBookmark2 = bmsvc.insertBookmark(bmsvc.bookmarksMenuFolder, uri("http://google.com"), bmsvc.DEFAULT_INDEX, "foo");
  bmsvc.moveItem(testBookmark2, bmsvc.bookmarksMenuFolder, 0);
  do_check_eq(resultObserver.movedNode.itemId, testBookmark2);

  // nsINavHistoryResultObserver.nodeRemoved
  bmsvc.removeItem(testBookmark2);
  do_check_eq(testBookmark2, resultObserver.removedNode.itemId);

  // XXX nsINavHistoryResultObserver.nodeReplaced
  // NHQRN.onVisit()->NHCRN.MergeResults()->NHCRN.ReplaceChildURIAt()->NHRV.NodeReplaced()

  // XXX nsINavHistoryResultObserver.invalidateContainer

  // nsINavHistoryResultObserver.sortingChanged
  resultObserver.invalidatedContainer = null;
  result.sortingMode = options.SORT_BY_TITLE_ASCENDING;
  do_check_eq(resultObserver.sortingMode, options.SORT_BY_TITLE_ASCENDING);
  do_check_eq(resultObserver.invalidatedContainer, result.root);

  // nsINavHistoryResultObserver.containerClosed
  root.containerOpen = false;
  do_check_eq(resultObserver.closedContainer, resultObserver.openedContainer);
  result.removeObserver(resultObserver);
}
