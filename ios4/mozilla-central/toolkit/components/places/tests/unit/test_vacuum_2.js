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
 * The Original Code is Places unit test code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Marco Bonardo <mak77@bonardo.net> (Original Author)
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

const LAST_VACUUM_PREF = "last_vacuum";
const VACUUM_THRESHOLD = 0.1;
const PLACES_VACUUM_STARTING_TOPIC = "places-vacuum-starting";

function getDBVacuumRatio() {
  let hs = Cc["@mozilla.org/browser/nav-history-service;1"].
           getService(Ci.nsINavHistoryService);
  let dbConn = hs.QueryInterface(Ci.nsPIPlacesDatabase).DBConnection;
  let freelistStmt = dbConn.createStatement("PRAGMA freelist_count");
  freelistStmt.executeStep();
  let freelistCount = freelistStmt.row.freelist_count;
  freelistStmt.finalize();
  let pageCountStmt = dbConn.createStatement("PRAGMA page_count");
  pageCountStmt.executeStep();
  let pageCount = pageCountStmt.row.page_count;
  pageCountStmt.finalize();

  let ratio = (freelistCount / pageCount);
  return ratio;
}

/* Only high ratio tests.  Stop at first vacuum. */
var gTests = [
  {
    desc: "High ratio, last vacuum today",
    ratio: "high",
    elapsedDays: 0,
    vacuum: false
  },
  {
    desc: "High ratio, last vacuum two months ago",
    ratio: "high",
    elapsedDays: 61,
    vacuum: true
  },
];

function run_test() {
  let sparseDB = do_get_file("places.sparse.sqlite");
  sparseDB.copyTo(do_get_profile(), "places.sqlite");

  let os = Cc["@mozilla.org/observer-service;1"].
           getService(Ci.nsIObserverService);
  let observer = {
    vacuum: false,
    observe: function(aSubject, aTopic, aData) {
      if (aTopic == PLACES_VACUUM_STARTING_TOPIC) {
        this.vacuum = true;
      }
    }
  }
  os.addObserver(observer, PLACES_VACUUM_STARTING_TOPIC, false);

  let prefs = Cc["@mozilla.org/preferences-service;1"].
              getService(Ci.nsIPrefService).
              getBranch("places.");

  while (gTests.length) {
    observer.vacuum = false;
    let test = gTests.shift();
    print("PLACES TEST: " + test.desc);
    let ratio = getDBVacuumRatio();
    if (test.ratio == "high") {
      do_check_true(getDBVacuumRatio() > VACUUM_THRESHOLD);
    }
    else if (test.ratio == "low") {
      do_throw("This test only supports high ratio cases");
    }
    print("current ratio is " + getDBVacuumRatio());
    prefs.setIntPref(LAST_VACUUM_PREF, ((Date.now() / 1000) - (test.elapsedDays * 24 * 60 * 60)));
    os.notifyObservers(null, "idle-daily", null);
    let testFunc = test.vacuum ? do_check_true : do_check_false;
    testFunc(observer.vacuum);
  }

  os.removeObserver(observer, PLACES_VACUUM_STARTING_TOPIC);
}
