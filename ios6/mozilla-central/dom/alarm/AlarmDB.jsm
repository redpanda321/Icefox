/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

this.EXPORTED_SYMBOLS = ["AlarmDB"];

/* static functions */
const DEBUG = false;

function debug(aStr) {
  if (DEBUG)
    dump("AlarmDB: " + aStr + "\n");
}

const { classes: Cc, interfaces: Ci, utils: Cu, results: Cr } = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/IndexedDBHelper.jsm");

const ALARMDB_NAME    = "alarms";
const ALARMDB_VERSION = 1;
const ALARMSTORE_NAME = "alarms";

this.AlarmDB = function AlarmDB(aGlobal) {
  debug("AlarmDB()");
  this._global = aGlobal;
}

AlarmDB.prototype = {
  __proto__: IndexedDBHelper.prototype,

  init: function init(aGlobal) {
    debug("init()");

    this.initDBHelper(ALARMDB_NAME, ALARMDB_VERSION, ALARMSTORE_NAME, aGlobal);
  },

  upgradeSchema: function upgradeSchema(aTransaction, aDb, aOldVersion, aNewVersion) {
    debug("upgradeSchema()");

    let objectStore = aDb.createObjectStore(ALARMSTORE_NAME, { keyPath: "id", autoIncrement: true });

    objectStore.createIndex("date",           "date",           { unique: false });
    objectStore.createIndex("ignoreTimezone", "ignoreTimezone", { unique: false });
    objectStore.createIndex("timezoneOffset", "timezoneOffset", { unique: false });
    objectStore.createIndex("data",           "data",           { unique: false });
    objectStore.createIndex("pageURL",        "pageURL",        { unique: false });
    objectStore.createIndex("manifestURL",    "manifestURL",    { unique: false });

    debug("Created object stores and indexes");
  },

  /**
   * @param aAlarm
   *        The record to be added.
   * @param aSuccessCb
   *        Callback function to invoke with result ID.
   * @param aErrorCb [optional]
   *        Callback function to invoke when there was an error.
   */
  add: function add(aAlarm, aSuccessCb, aErrorCb) {
    debug("add()");

    this.newTxn(
      "readwrite", 
      function txnCb(aTxn, aStore) {
        debug("Going to add " + JSON.stringify(aAlarm));
        aStore.put(aAlarm).onsuccess = function setTxnResult(aEvent) {
          aTxn.result = aEvent.target.result;
          debug("Request successful. New record ID: " + aTxn.result);
        };
      },
      aSuccessCb, 
      aErrorCb
    );
  },

  /**
   * @param aId
   *        The ID of record to be removed.
   * @param aManifestURL
   *        The manifest URL of the app that alarm belongs to.
   *        If null, directly remove the ID record; otherwise,
   *        need to check if the alarm belongs to this app.
   * @param aSuccessCb
   *        Callback function to invoke with result.
   * @param aErrorCb [optional]
   *        Callback function to invoke when there was an error.
   */
  remove: function remove(aId, aManifestURL, aSuccessCb, aErrorCb) {
    debug("remove()");

    this.newTxn(
      "readwrite", 
      function txnCb(aTxn, aStore) {
        debug("Going to remove " + aId);

        // Look up the existing record and compare the manifestURL
        // to see if the alarm to be removed belongs to this app.
        aStore.get(aId).onsuccess = function doRemove(aEvent) {
          let alarm = aEvent.target.result;

          if (!alarm) {
            debug("Alarm doesn't exist. No need to remove it.");
            return;
          }

          if (aManifestURL && aManifestURL != alarm.manifestURL) {
            debug("Cannot remove the alarm added by other apps.");
            return;
          }

          aStore.delete(aId);
        };
      }, 
      aSuccessCb, 
      aErrorCb
    );
  },

  /**
   * @param aManifestURL
   *        The manifest URL of the app that alarms belong to.
   *        If null, directly return all alarms; otherwise,
   *        only return the alarms that belong to this app.
   * @param aSuccessCb
   *        Callback function to invoke with result array.
   * @param aErrorCb [optional]
   *        Callback function to invoke when there was an error.
   */
  getAll: function getAll(aManifestURL, aSuccessCb, aErrorCb) {
    debug("getAll()");

    this.newTxn(
      "readonly", 
      function txnCb(aTxn, aStore) {
        if (!aTxn.result)
          aTxn.result = [];

        aStore.mozGetAll().onsuccess = function setTxnResult(aEvent) {
          aEvent.target.result.forEach(function addAlarm(aAlarm) {
            if (!aManifestURL || aManifestURL == aAlarm.manifestURL)
              aTxn.result.push(aAlarm);
          });

          debug("Request successful. Record count: " + aTxn.result.length);
        };
      }, 
      aSuccessCb, 
      aErrorCb
    );
  }
};
