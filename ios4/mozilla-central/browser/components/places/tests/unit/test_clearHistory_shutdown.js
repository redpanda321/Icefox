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
 * The Original Code is Places Unit Test code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Marco Bonardo <mak77@bonardo.net>
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
 * Tests that requesting clear history at shutdown will really clear history.
 */

const URIS = [
  "http://a.example1.com/"
, "http://b.example1.com/"
, "http://b.example2.com/"
, "http://c.example3.com/"
];

const TOPIC_CONNECTION_CLOSED = "places-connection-closed";

let EXPECTED_NOTIFICATIONS = [
  "places-shutdown"
, "places-will-close-connection"
, "places-connection-closing"
, "places-sync-finished"
, "places-expiration-finished"
, "places-sync-finished"
, "places-connection-closed"
];

const UNEXPECTED_NOTIFICATIONS = [
  "xpcom-shutdown"
];

const URL = "ftp://localhost/clearHistoryOnShutdown/";

let notificationIndex = 0;

let notificationsObserver = {
  observe: function observe(aSubject, aTopic, aData) {
    print("Received notification: " + aTopic);

    // Note that some of these notifications could arrive multiple times, for
    // example in case of sync, we allow that.
    if (EXPECTED_NOTIFICATIONS[notificationIndex] != aTopic)
      notificationIndex++;
    do_check_eq(EXPECTED_NOTIFICATIONS[notificationIndex], aTopic);

    if (aTopic != TOPIC_CONNECTION_CLOSED)
      return;

    getDistinctNotifications().forEach(
      function (topic) Services.obs.removeObserver(notificationsObserver, topic)
    );

    print("Looking for uncleared stuff.");

    let stmt = DBConn().createStatement(
      "SELECT id FROM moz_places WHERE url = :page_url "
    );

    try {
      URIS.forEach(function(aUrl) {
        stmt.params.page_url = aUrl;
        do_check_false(stmt.executeStep());
        stmt.reset();
      });
    } finally {
      stmt.finalize();
    }

    // Check cache.
    do_check_false(cacheExists(URL));

    do_test_finished();
  }
}

let timeInMicroseconds = Date.now() * 1000;

function run_test() {
  do_test_pending();

  print("Initialize browserglue before Places");
  Cc["@mozilla.org/browser/browserglue;1"].getService(Ci.nsIBrowserGlue);

  Services.prefs.setBoolPref("privacy.clearOnShutdown.cache", true);
  Services.prefs.setBoolPref("privacy.clearOnShutdown.cookies", true);
  Services.prefs.setBoolPref("privacy.clearOnShutdown.offlineApps", true);
  Services.prefs.setBoolPref("privacy.clearOnShutdown.history", true);
  Services.prefs.setBoolPref("privacy.clearOnShutdown.downloads", true);
  Services.prefs.setBoolPref("privacy.clearOnShutdown.cookies", true);
  Services.prefs.setBoolPref("privacy.clearOnShutdown.formData", true);
  Services.prefs.setBoolPref("privacy.clearOnShutdown.passwords", true);
  Services.prefs.setBoolPref("privacy.clearOnShutdown.sessions", true);
  Services.prefs.setBoolPref("privacy.clearOnShutdown.siteSettings", true);

  Services.prefs.setBoolPref("privacy.sanitize.sanitizeOnShutdown", true);

  print("Add visits.");
  URIS.forEach(function(aUrl) {
    PlacesUtils.history.addVisit(uri(aUrl), timeInMicroseconds++, null,
                                 PlacesUtils.history.TRANSITION_TYPED,
                                 false, 0);
  });
  print("Add cache.");
  storeCache(URL, "testData");

  print("Simulate and wait shutdown.");
  getDistinctNotifications().forEach(
    function (topic)
      Services.obs.addObserver(notificationsObserver, topic, false)
  );

  shutdownPlaces();
}

function getDistinctNotifications() {
  let ar = EXPECTED_NOTIFICATIONS.concat(UNEXPECTED_NOTIFICATIONS);
  return [ar[i] for (i in ar) if (ar.slice(0, i).indexOf(ar[i]) == -1)];
}

function storeCache(aURL, aContent) {
  let cache = Cc["@mozilla.org/network/cache-service;1"].
              getService(Ci.nsICacheService);
  let session = cache.createSession("FTP", Ci.nsICache.STORE_ANYWHERE,
                                    Ci.nsICache.STREAM_BASED);
  let cacheEntry =
    session.openCacheEntry(aURL, Ci.nsICache.ACCESS_READ_WRITE, false);

  cacheEntry.setMetaDataElement("servertype", "0");
  var oStream = cacheEntry.openOutputStream(0);

  var written = oStream.write(aContent, aContent.length);
  if (written != aContent.length) {
    do_throw("oStream.write has not written all data!\n" +
             "  Expected: " + written  + "\n" +
             "  Actual: " + aContent.length + "\n");
  }
  oStream.close();
  cacheEntry.close();
}

function cacheExists(aURL) {
  let cache = Cc["@mozilla.org/network/cache-service;1"].
              getService(Ci.nsICacheService);
  let session = cache.createSession("FTP", Ci.nsICache.STORE_ANYWHERE,
                                    Ci.nsICache.STREAM_BASED);
  try {
    let cacheEntry =
      session.openCacheEntry(aURL, Ci.nsICache.ACCESS_READ, true);
  } catch (e) {
    if (e.result == Cr.NS_ERROR_CACHE_KEY_NOT_FOUND ||
        e.result == Cr.NS_ERROR_FAILURE)
      return false;
 
    // Throw the textual error description.
    do_throw(e);
  }
  cacheEntry.close();
  return true;
}
