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
 * The Initial Developer of the Original Code is
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Dietrich Ayala <dietrich@mozilla.com>
 *   Marco Bonardo <mak77@bonardo.net>
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

/*
Tests the performance of opening the history menu.
*/

/*********************** begin header **********************/
const TEST_IDENTIFIER = "ui-perf-test";
const TEST_SUITE = "places";

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;

var wm = Cc["@mozilla.org/appshell/window-mediator;1"].
         getService(Ci.nsIWindowMediator);
var win = wm.getMostRecentWindow("navigator:browser");

var ios = Cc["@mozilla.org/network/io-service;1"].
          getService(Ci.nsIIOService);
var hs = Cc["@mozilla.org/browser/nav-history-service;1"].
         getService(Ci.nsINavHistoryService);
var bs = Cc["@mozilla.org/browser/nav-bookmarks-service;1"].
         getService(Ci.nsINavBookmarksService);

var historyMenu = document.getElementById("history-menu");
var historyPopup = document.getElementById("goPopup");

function add_visit(aURI, aDate) {
  var visitId = hs.addVisit(aURI,
                            aDate,
                            null, // no referrer
                            hs.TRANSITION_TYPED, // user typed in URL bar
                            false, // not redirect
                            0);
  return visitId;
}

function add_bookmark(aURI) {
  var bId = bs.insertBookmark(bs.unfiledBookmarksFolder, aURI,
                              bs.DEFAULT_INDEX, "bookmark/" + aURI.spec);
  return bId;
}

function make_test_report(testName, result, units) {
  return [TEST_IDENTIFIER, TEST_SUITE, testName, result, units||"ms"].join(":");
}

// Each test is an obj w/ a name property and run method
var ptests = [];

/*********************** end header **********************/

const TEST_REPEAT_COUNT = 6;

// test duration of history menu opening
ptests.push({
  name: "open_history_menu",
  times: [],
  run: function() {
    var self = this;
    var start = Date.now();
    historyPopup.addEventListener("popupshown", function() {
      historyPopup.removeEventListener("popupshown", arguments.callee, true);
      executeSoon(function() {
        var duration = Date.now() - start;
        historyPopup.hidePopup();
        historyMenu.open = false;
        self.times.push(duration);
        if (self.times.length == TEST_REPEAT_COUNT)
          self.finish();
        else
          self.run();
      });
    }, true);
    historyMenu.open = true;
    historyPopup.openPopup();
  },
  finish: function() {
    processTestResult(this);
    setTimeout(runNextTest, 0);
  }
});

function processTestResult(aTest) {
  aTest.times.sort();  // sort the scores
  aTest.times.pop();   // remove worst
  aTest.times.shift(); // remove best
  var totalDuration = aTest.times.reduce(function(time, total){ return time + total; });
  var avgDuration = totalDuration/aTest.times.length;
  var report = make_test_report(aTest.name, avgDuration);
  ok(true, report);
}

function test() {
  // Skip test on Mac due to native menus.
  if (navigator.platform.toLowerCase().indexOf("mac") != -1)
    return;

  waitForExplicitFinish();

  // kick off tests
  setTimeout(runNextTest, 0);
}

function runNextTest() {
  if (ptests.length > 0)
    ptests.shift().run();
  else
    finish();
}
