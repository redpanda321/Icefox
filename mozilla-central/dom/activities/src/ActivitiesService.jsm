/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict"

const Cu = Components.utils;
const Cc = Components.classes;
const Ci = Components.interfaces;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/IndexedDBHelper.jsm");

XPCOMUtils.defineLazyServiceGetter(this, "ppmm",
                                   "@mozilla.org/parentprocessmessagemanager;1",
                                   "nsIMessageBroadcaster");

this.EXPORTED_SYMBOLS = [];

let idbGlobal = this;

function debug(aMsg) {
  //dump("-- ActivitiesService.jsm " + Date.now() + " " + aMsg + "\n");
}

const DB_NAME    = "activities";
const DB_VERSION = 1;
const STORE_NAME = "activities";

function ActivitiesDb() {

}

ActivitiesDb.prototype = {
  __proto__: IndexedDBHelper.prototype,

  init: function actdb_init() {
    let idbManager = Cc["@mozilla.org/dom/indexeddb/manager;1"]
                       .getService(Ci.nsIIndexedDatabaseManager);
    idbManager.initWindowless(idbGlobal);
    this.initDBHelper(DB_NAME, DB_VERSION, STORE_NAME, idbGlobal);
  },

  /**
   * Create the initial database schema.
   *
   * The schema of records stored is as follows:
   *
   * {
   *  id:                  String
   *  manifest:            String
   *  name:                String
   *  title:               String
   *  icon:                String
   *  description:         jsval
   * }
   */
  upgradeSchema: function actdb_upgradeSchema(aTransaction, aDb, aOldVersion, aNewVersion) {
    debug("Upgrade schema " + aOldVersion + " -> " + aNewVersion);
    let objectStore = aDb.createObjectStore(STORE_NAME, { keyPath: "id" });

    // indexes
    objectStore.createIndex("name", "name", { unique: false });
    objectStore.createIndex("manifest", "manifest", { unique: false });

    debug("Created object stores and indexes");
  },

  // unique ids made of (uri, action)
  createId: function actdb_createId(aObject) {
    let converter = Cc["@mozilla.org/intl/scriptableunicodeconverter"]
                      .createInstance(Ci.nsIScriptableUnicodeConverter);
    converter.charset = "UTF-8";

    let hasher = Cc["@mozilla.org/security/hash;1"]
                   .createInstance(Ci.nsICryptoHash);
    hasher.init(hasher.SHA1);

    // add uri and action to the hash
    ["manifest", "name"].forEach(function(aProp) {
      let data = converter.convertToByteArray(aObject[aProp], {});
      hasher.update(data, data.length);
    });

    return hasher.finish(true);
  },

  // Add all the activities carried in the |aObjects| array.
  add: function actdb_add(aObjects, aSuccess, aError) {
    this.newTxn("readwrite", function (txn, store) {
      aObjects.forEach(function (aObject) {
        let object = {
          manifest: aObject.manifest,
          name: aObject.name,
          title: aObject.title || "",
          icon: aObject.icon || "",
          description: aObject.description
        };
        object.id = this.createId(object);
        debug("Going to add " + JSON.stringify(object));
        store.put(object);
      }, this);
    }.bind(this), aSuccess, aError);
  },

  // Remove all the activities carried in the |aObjects| array.
  remove: function actdb_remove(aObjects) {
    this.newTxn("readwrite", function (txn, store) {
      aObjects.forEach(function (aObject) {
        let object = {
          manifest: aObject.manifest,
          name: aObject.name
        };
        debug("Going to remove " + JSON.stringify(object));
        store.delete(this.createId(object));
      }, this);
    }.bind(this), function() {}, function() {});
  },

  find: function actdb_find(aObject, aSuccess, aError, aMatch) {
    debug("Looking for " + aObject.options.name);

    this.newTxn("readonly", function (txn, store) {
      let index = store.index("name");
      let request = index.mozGetAll(aObject.options.name);
      request.onsuccess = function findSuccess(aEvent) {
        debug("Request successful. Record count: " + aEvent.target.result.length);
        if (!txn.result) {
          txn.result = {
            name: aObject.options.name,
            options: []
          };
        }

        aEvent.target.result.forEach(function(result) {
          if (!aMatch(result))
            return;

          txn.result.options.push({
            manifest: result.manifest,
            title: result.title,
            icon: result.icon,
            description: result.description
          });
        });
      }
    }.bind(this), aSuccess, aError);
  }
}

let Activities = {
  messages: [
    // ActivityProxy.js
    "Activity:Start",

    // ActivityRequestHandler.js
    "Activity:PostResult",
    "Activity:PostError",

    "Activities:Register",
    "Activities:Unregister",
  ],

  init: function activities_init() {
    this.messages.forEach(function(msgName) {
      ppmm.addMessageListener(msgName, this);
    }, this);

    Services.obs.addObserver(this, "xpcom-shutdown", false);

    this.db = new ActivitiesDb();
    this.db.init();
    this.callers = {};
  },

  observe: function activities_observe(aSubject, aTopic, aData) {
    this.messages.forEach(function(msgName) {
      ppmm.removeMessageListener(msgName, this);
    }, this);
    ppmm = null;

    Services.obs.removeObserver(this, "xpcom-shutdown");
  },

  /**
    * Starts an activity by doing:
    * - finds a list of matching activities.
    * - calls the UI glue to get the user choice.
    * - fire an system message of type "activity" to this app, sending the
    *   activity data as a payload.
    */
  startActivity: function activities_startActivity(aMsg) {
    debug("StartActivity: " + JSON.stringify(aMsg));

    let successCb = function successCb(aResults) {
      debug(JSON.stringify(aResults));

      // We have no matching activity registered, let's fire an error.
      if (aResults.options.length === 0) {
        Activities.callers[aMsg.id].mm.sendAsyncMessage("Activity:FireError", {
          "id": aMsg.id,
          "error": "NO_PROVIDER"
        });
        delete Activities.callers[aMsg.id];
        return;
      }

      function getActivityChoice(aChoice) {
        debug("Activity choice: " + aChoice);

        // The user has cancelled the choice, fire an error.
        if (aChoice === -1) {
          Activities.callers[aMsg.id].mm.sendAsyncMessage("Activity:FireError", {
            "id": aMsg.id,
            "error": "USER_ABORT"
          });
          delete Activities.callers[aMsg.id];
          return;
        }

        let sysmm = Cc["@mozilla.org/system-message-internal;1"]
                      .getService(Ci.nsISystemMessagesInternal);
        if (!sysmm) {
          // System message is not present, what should we do?
          return;
        }

        debug("Sending system message...");
        let result = aResults.options[aChoice];
        sysmm.sendMessage("activity", {
            "id": aMsg.id,
            "payload": aMsg.options,
            "target": result.description
          },
          Services.io.newURI(result.description.href, null, null),
          Services.io.newURI(result.manifest, null, null));

        if (!result.description.returnValue) {
          Activities.callers[aMsg.id].mm.sendAsyncMessage("Activity:FireSuccess", {
            "id": aMsg.id,
            "result": null
          });
          // No need to notify observers, since we don't want the caller
          // to be raised on the foreground that quick.
          delete Activities.callers[aMsg.id];
        }
      };

      let glue = Cc["@mozilla.org/dom/activities/ui-glue;1"]
                   .createInstance(Ci.nsIActivityUIGlue);
      glue.chooseActivity(aResults.name, aResults.options, getActivityChoice);
    };

    let errorCb = function errorCb(aError) {
      // Something unexpected happened. Should we send an error back?
      debug("Error in startActivity: " + aError + "\n");
    };

    let matchFunc = function matchFunc(aResult) {

      function matchFuncValue(aValue, aFilter) {
        // Bug 805822 - Regexp support for MozActivity

        let values = Array.isArray(aValue) ? aValue : [aValue];
        let filters = Array.isArray(aFilter) ? aFilter : [aFilter];

        // At least 1 value must match.
        let ret = false;
        values.forEach(function(value) {
          if (filters.indexOf(value) != -1) {
            ret = true;
          }
        });

        return ret;
      }

      // For any incoming property.
      for (let prop in aMsg.options.data) {

        // If this is unknown for the app, let's continue.
        if (!(prop in aResult.description.filters)) {
          continue;
        }

        // Otherwise, let's check the value against the filter.
        if (!matchFuncValue(aMsg.options.data[prop], aResult.description.filters[prop])) {
          return false;
        }
      }

      return true;
    };

    this.db.find(aMsg, successCb, errorCb, matchFunc);
  },

  receiveMessage: function activities_receiveMessage(aMessage) {
    let mm = aMessage.target;
    let msg = aMessage.json;

    let caller;
    let obsData;

    if (aMessage.name == "Activity:PostResult" ||
        aMessage.name == "Activity:PostError") {
      caller = this.callers[msg.id];
      if (!caller) {
        debug("!! caller is null for msg.id=" + msg.id);
        return;
      }
      obsData = JSON.stringify({ manifestURL: caller.manifestURL,
                                 pageURL: caller.pageURL,
                                 success: aMessage.name == "Activity:PostResult" });
    }

    switch(aMessage.name) {
      case "Activity:Start":
        this.callers[msg.id] = { mm: aMessage.target,
                                 manifestURL: msg.manifestURL,
                                 pageURL: msg.pageURL };
        this.startActivity(msg);
        break;

      case "Activity:PostResult":
        caller.mm.sendAsyncMessage("Activity:FireSuccess", msg);
        Services.obs.notifyObservers(null, "activity-done", obsData);
        delete this.callers[msg.id];
        break;
      case "Activity:PostError":
        caller.mm.sendAsyncMessage("Activity:FireError", msg);
        Services.obs.notifyObservers(null, "activity-done", obsData);
        delete this.callers[msg.id];
        break;

      case "Activities:Register":
        this.db.add(msg,
          function onSuccess(aEvent) {
            mm.sendAsyncMessage("Activities:Register:OK", null);
          },
          function onError(aEvent) {
            msg.error = "REGISTER_ERROR";
            mm.sendAsyncMessage("Activities:Register:KO", msg);
          });
        break;
      case "Activities:Unregister":
        this.db.remove(msg);
        break;
    }
  }
}

Activities.init();
