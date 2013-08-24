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
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Justin Dolske <dolske@mozilla.com> (original authors)
 *  Paul O’Shannessy <paul@oshannessy.com> (original authors)
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


const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");
Components.utils.import("resource://gre/modules/Services.jsm");

const DB_VERSION = 3;
const DAY_IN_MS  = 86400000; // 1 day in milliseconds

function FormHistory() {
    this.init();
}

FormHistory.prototype = {
    classID          : Components.ID("{0c1bb408-71a2-403f-854a-3a0659829ded}"),
    QueryInterface   : XPCOMUtils.generateQI([Ci.nsIFormHistory2,
                                              Ci.nsIObserver,
                                              Ci.nsIFrameMessageListener,
                                              ]),

    debug          : true,
    enabled        : true,
    saveHttpsForms : true,

    // The current database schema.
    dbSchema : {
        tables : {
            moz_formhistory: {
                "id"        : "INTEGER PRIMARY KEY",
                "fieldname" : "TEXT NOT NULL",
                "value"     : "TEXT NOT NULL",
                "timesUsed" : "INTEGER",
                "firstUsed" : "INTEGER",
                "lastUsed"  : "INTEGER",
                "guid"      : "TEXT"
            },
        },
        indices : {
            moz_formhistory_index : {
                table   : "moz_formhistory",
                columns : ["fieldname"]
            },
            moz_formhistory_lastused_index : {
                table   : "moz_formhistory",
                columns : ["lastUsed"]
            },
            moz_formhistory_guid_index : {
                table   : "moz_formhistory",
                columns : ["guid"]
            },
        }
    },
    dbConnection : null,  // The database connection
    dbStmts      : null,  // Database statements for memoization
    dbFile       : null,

    _uuidService: null,
    get uuidService() {
        if (!this._uuidService)
            this._uuidService = Cc["@mozilla.org/uuid-generator;1"].
                                getService(Ci.nsIUUIDGenerator);
        return this._uuidService;
    },

    // Private Browsing Service
    // If the service is not available, null will be returned.
    _privBrowsingSvc : undefined,
    get privBrowsingSvc() {
        if (this._privBrowsingSvc == undefined) {
            if ("@mozilla.org/privatebrowsing;1" in Cc)
                this._privBrowsingSvc = Cc["@mozilla.org/privatebrowsing;1"].
                                        getService(Ci.nsIPrivateBrowsingService);
            else
                this._privBrowsingSvc = null;
        }
        return this._privBrowsingSvc;
    },


    log : function (message) {
        if (!this.debug)
            return;
        dump("FormHistory: " + message + "\n");
        Services.console.logStringMessage("FormHistory: " + message);
    },


    init : function() {
        let self = this;

        Services.prefs.addObserver("browser.formfill.", this, false);

        this.updatePrefs();

        this.dbStmts = {};

        this.messageManager = Cc["@mozilla.org/globalmessagemanager;1"].
                              getService(Ci.nsIChromeFrameMessageManager);
        this.messageManager.loadFrameScript("chrome://satchel/content/formSubmitListener.js", true);
        this.messageManager.addMessageListener("FormHistory:FormSubmitEntries", this);

        // Add observers
        Services.obs.addObserver(function() { self.expireOldEntries() }, "idle-daily", false);
        Services.obs.addObserver(function() { self.expireOldEntries() }, "formhistory-expire-now", false);

        try {
            this.dbFile = Services.dirsvc.get("ProfD", Ci.nsIFile).clone();
            this.dbFile.append("formhistory.sqlite");
            this.log("Opening database at " + this.dbFile.path);

            this.dbInit();
        } catch (e) {
            this.log("Initialization failed: " + e);
            // If dbInit fails...
            if (e.result == Cr.NS_ERROR_FILE_CORRUPTED) {
                this.dbCleanup(true);
                this.dbInit();
            } else {
                throw "Initialization failed";
            }
        }
    },


    /* ---- message listener ---- */


    receiveMessage: function receiveMessage(message) {
        // Open a transaction so multiple adds happen in one commit
        this.dbConnection.beginTransaction();

        try {
            let entries = message.json;
            for (let i = 0; i < entries.length; i++) {
                this.addEntry(entries[i].name, entries[i].value);
            }
        } finally {
            // Don't need it to be atomic if there was an error.  Commit what
            // we managed to put in the table.
            this.dbConnection.commitTransaction();
        }
    },


    /* ---- nsIFormHistory2 interfaces ---- */


    get hasEntries() {
        return (this.countAllEntries() > 0);
    },


    addEntry : function (name, value) {
        if (!this.enabled ||
            this.privBrowsingSvc && this.privBrowsingSvc.privateBrowsingEnabled)
            return;

        this.log("addEntry for " + name + "=" + value);

        let now = Date.now() * 1000; // microseconds

        let [id, guid] = this.getExistingEntryID(name, value);
        let stmt;

        if (id != -1) {
            // Update existing entry
            let query = "UPDATE moz_formhistory SET timesUsed = timesUsed + 1, lastUsed = :lastUsed WHERE id = :id";
            let params = {
                            lastUsed : now,
                            id       : id
                         };

            try {
                stmt = this.dbCreateStatement(query, params);
                stmt.execute();
                this.sendStringNotification("modifyEntry", name, value, guid);
            } catch (e) {
                this.log("addEntry (modify) failed: " + e);
                throw e;
            } finally {
                stmt.reset();
            }

        } else {
            // Add new entry
            guid = this.generateGUID();

            let query = "INSERT INTO moz_formhistory (fieldname, value, timesUsed, firstUsed, lastUsed, guid) " +
                            "VALUES (:fieldname, :value, :timesUsed, :firstUsed, :lastUsed, :guid)";
            let params = {
                            fieldname : name,
                            value     : value,
                            timesUsed : 1,
                            firstUsed : now,
                            lastUsed  : now,
                            guid      : guid
                        };

            try {
                stmt = this.dbCreateStatement(query, params);
                stmt.execute();
                this.sendStringNotification("addEntry", name, value, guid);
            } catch (e) {
                this.log("addEntry (create) failed: " + e);
                throw e;
            } finally {
                stmt.reset();
            }
        }
    },


    removeEntry : function (name, value) {
        this.log("removeEntry for " + name + "=" + value);

        let [id, guid] = this.getExistingEntryID(name, value);
        this.sendStringNotification("before-removeEntry", name, value, guid);

        let stmt;
        let query = "DELETE FROM moz_formhistory WHERE id = :id";
        let params = { id : id };

        try {
            stmt = this.dbCreateStatement(query, params);
            stmt.execute();
            this.sendStringNotification("removeEntry", name, value, guid);
        } catch (e) {
            this.log("removeEntry failed: " + e);
            throw e;
        } finally {
            stmt.reset();
        }
    },


    removeEntriesForName : function (name) {
        this.log("removeEntriesForName with name=" + name);

        this.sendStringNotification("before-removeEntriesForName", name);

        let stmt;
        let query = "DELETE FROM moz_formhistory WHERE fieldname = :fieldname";
        let params = { fieldname : name };

        try {
            stmt = this.dbCreateStatement(query, params);
            stmt.execute();
            this.sendStringNotification("removeEntriesForName", name);
        } catch (e) {
            this.log("removeEntriesForName failed: " + e);
            throw e;
        } finally {
            stmt.reset();
        }
    },


    removeAllEntries : function () {
        this.log("removeAllEntries");

        this.sendNotification("before-removeAllEntries", null);

        let stmt;
        let query = "DELETE FROM moz_formhistory";

        try {
            stmt = this.dbCreateStatement(query);
            stmt.execute();
            this.sendNotification("removeAllEntries", null);
        } catch (e) {
            this.log("removeEntriesForName failed: " + e);
            throw e;
        } finally {
            stmt.reset();
        }

        // privacy cleanup, if there's an old mork formhistory around, just delete it
        let oldFile = Services.dirsvc.get("ProfD", Ci.nsIFile);
        oldFile.append("formhistory.dat");
        if (oldFile.exists())
            oldFile.remove(false);
    },


    nameExists : function (name) {
        this.log("nameExists for name=" + name);
        let stmt;
        let query = "SELECT COUNT(1) AS numEntries FROM moz_formhistory WHERE fieldname = :fieldname";
        let params = { fieldname : name };
        try {
            stmt = this.dbCreateStatement(query, params);
            stmt.executeStep();
            return (stmt.row.numEntries > 0);
        } catch (e) {
            this.log("nameExists failed: " + e);
            throw e;
        } finally {
            stmt.reset();
        }
    },

    entryExists : function (name, value) {
        this.log("entryExists for " + name + "=" + value);
        let [id, guid] = this.getExistingEntryID(name, value);
        this.log("entryExists: id=" + id);
        return (id != -1);
    },

    removeEntriesByTimeframe : function (beginTime, endTime) {
        this.log("removeEntriesByTimeframe for " + beginTime + " to " + endTime);

        this.sendIntNotification("before-removeEntriesByTimeframe", beginTime, endTime);

        let stmt;
        let query = "DELETE FROM moz_formhistory WHERE firstUsed >= :beginTime AND firstUsed <= :endTime";
        let params = {
                        beginTime : beginTime,
                        endTime   : endTime
                     };
        try {
            stmt = this.dbCreateStatement(query, params);
            stmt.executeStep();
            this.sendIntNotification("removeEntriesByTimeframe", beginTime, endTime);
        } catch (e) {
            this.log("removeEntriesByTimeframe failed: " + e);
            throw e;
        } finally {
            stmt.reset();
        }

    },


    get DBConnection() {
        return this.dbConnection;
    },


    /* ---- nsIObserver interface ---- */


    observe : function (subject, topic, data) {
        if (topic == "nsPref:changed")
            this.updatePrefs();
        else
            this.log("Oops! Unexpected notification: " + topic);
    },


    /* ---- helpers ---- */


    generateGUID : function() {
        // string like: "{f60d9eac-9421-4abc-8491-8e8322b063d4}"
        let uuid = this.uuidService.generateUUID().toString();
        let raw = ""; // A string with the low bytes set to random values
        let bytes = 0;
        for (let i = 1; bytes < 12 ; i+= 2) {
            // Skip dashes
            if (uuid[i] == "-")
                i++;
            let hexVal = parseInt(uuid[i] + uuid[i + 1], 16);
            raw += String.fromCharCode(hexVal);
            bytes++;
        }
        return btoa(raw);
    },


    sendStringNotification : function (changeType, str1, str2, str3) {
        function wrapit(str) {
            let wrapper = Cc["@mozilla.org/supports-string;1"].
                          createInstance(Ci.nsISupportsString);
            wrapper.data = str;
            return wrapper;
        }

        let strData;
        if (arguments.length == 2) {
            // Just 1 string, no need to put it in an array
            strData = wrapit(str1);
        } else {
            // 3 strings, put them in an array.
            strData = Cc["@mozilla.org/array;1"].
                      createInstance(Ci.nsIMutableArray);
            strData.appendElement(wrapit(str1), false);
            strData.appendElement(wrapit(str2), false);
            strData.appendElement(wrapit(str3), false);
        }
        this.sendNotification(changeType, strData);
    },


    sendIntNotification : function (changeType, int1, int2) {
        function wrapit(int) {
            let wrapper = Cc["@mozilla.org/supports-PRInt64;1"].
                          createInstance(Ci.nsISupportsPRInt64);
            wrapper.data = int;
            return wrapper;
        }

        let intData;
        if (arguments.length == 2) {
            // Just 1 int, no need for an array
            intData = wrapit(int1);
        } else {
            // 2 ints, put them in an array.
            intData = Cc["@mozilla.org/array;1"].
                      createInstance(Ci.nsIMutableArray);
            intData.appendElement(wrapit(int1), false);
            intData.appendElement(wrapit(int2), false);
        }
        this.sendNotification(changeType, intData);
    },


    sendNotification : function (changeType, data) {
        Services.obs.notifyObservers(data, "satchel-storage-changed", changeType);
    },


    getExistingEntryID : function (name, value) {
        let id = -1, guid = null;
        let stmt;
        let query = "SELECT id, guid FROM moz_formhistory WHERE fieldname = :fieldname AND value = :value";
        let params = {
                        fieldname : name,
                        value     : value
                     };
        try {
            stmt = this.dbCreateStatement(query, params);
            if (stmt.executeStep()) {
                id   = stmt.row.id;
                guid = stmt.row.guid;
            }
        } catch (e) {
            this.log("getExistingEntryID failed: " + e);
            throw e;
        } finally {
            stmt.reset();
        }

        return [id, guid];
    },


    countAllEntries : function () {
        let query = "SELECT COUNT(1) AS numEntries FROM moz_formhistory";

        let stmt, numEntries;
        try {
            stmt = this.dbCreateStatement(query, null);
            stmt.executeStep();
            numEntries = stmt.row.numEntries;
        } catch (e) {
            this.log("countAllEntries failed: " + e);
            throw e;
        } finally {
            stmt.reset();
        }

        this.log("countAllEntries: counted entries: " + numEntries);
        return numEntries;
    },


    expireOldEntries : function () {
        this.log("expireOldEntries");

        // Determine how many days of history we're supposed to keep.
        let expireDays = 180;
        try {
            expireDays = Services.prefs.getIntPref("browser.formfill.expire_days");
        } catch (e) { /* ignore */ }

        let expireTime = Date.now() - expireDays * DAY_IN_MS;
        expireTime *= 1000; // switch to microseconds

        this.sendIntNotification("before-expireOldEntries", expireTime);

        let beginningCount = this.countAllEntries();

        // Purge the form history...
        let stmt;
        let query = "DELETE FROM moz_formhistory WHERE lastUsed <= :expireTime";
        let params = { expireTime : expireTime };

        try {
            stmt = this.dbCreateStatement(query, params);
            stmt.execute();
        } catch (e) {
            this.log("expireOldEntries failed: " + e);
            throw e;
        } finally {
            stmt.reset();
        }

        let endingCount = this.countAllEntries();

        // If we expired a large batch of entries, shrink the DB to reclaim wasted
        // space. This is expected to happen when entries predating timestamps
        // (added in the v.1 schema) expire in mass, 180 days after the DB was
        // upgraded -- entries not used since then expire all at once.
        if (beginningCount - endingCount > 500)
            this.dbConnection.executeSimpleSQL("VACUUM");

        this.sendIntNotification("expireOldEntries", expireTime);
    },


    updatePrefs : function () {
        this.debug          = Services.prefs.getBoolPref("browser.formfill.debug");
        this.enabled        = Services.prefs.getBoolPref("browser.formfill.enable");
        this.saveHttpsForms = Services.prefs.getBoolPref("browser.formfill.saveHttpsForms");
    },

//**************************************************************************//
    // Database Creation & Access

    /*
     * dbCreateStatement
     *
     * Creates a statement, wraps it, and then does parameter replacement
     * Will use memoization so that statements can be reused.
     */
    dbCreateStatement : function (query, params) {
        let stmt = this.dbStmts[query];
        // Memoize the statements
        if (!stmt) {
            this.log("Creating new statement for query: " + query);
            stmt = this.dbConnection.createStatement(query);
            this.dbStmts[query] = stmt;
        }
        // Replace parameters, must be done 1 at a time
        if (params)
            for (let i in params)
                stmt.params[i] = params[i];
        return stmt;
    },


    /*
     * dbInit
     *
     * Attempts to initialize the database. This creates the file if it doesn't
     * exist, performs any migrations, etc.
     */
    dbInit : function () {
        this.log("Initializing Database");

        let storage = Cc["@mozilla.org/storage/service;1"].
                      getService(Ci.mozIStorageService);
        this.dbConnection = storage.openDatabase(this.dbFile);
        let version = this.dbConnection.schemaVersion;

        // Note: Firefox 3 didn't set a schema value, so it started from 0.
        // So we can't depend on a simple version == 0 check
        if (version == 0 && !this.dbConnection.tableExists("moz_formhistory"))
            this.dbCreate();
        else if (version != DB_VERSION)
            this.dbMigrate(version);
    },


    dbCreate: function () {
        this.log("Creating DB -- tables");
        for (let name in this.dbSchema.tables) {
            let table = this.dbSchema.tables[name];
            let tSQL = [[col, table[col]].join(" ") for (col in table)].join(", ");
            this.dbConnection.createTable(name, tSQL);
        }

        this.log("Creating DB -- indices");
        for (let name in this.dbSchema.indices) {
            let index = this.dbSchema.indices[name];
            let statement = "CREATE INDEX IF NOT EXISTS " + name + " ON " + index.table +
                            "(" + index.columns.join(", ") + ")";
            this.dbConnection.executeSimpleSQL(statement);
        }

        this.dbConnection.schemaVersion = DB_VERSION;
    },


    dbMigrate : function (oldVersion) {
        this.log("Attempting to migrate from version " + oldVersion);

        if (oldVersion > DB_VERSION) {
            this.log("Downgrading to version " + DB_VERSION);
            // User's DB is newer. Sanity check that our expected columns are
            // present, and if so mark the lower version and merrily continue
            // on. If the columns are borked, something is wrong so blow away
            // the DB and start from scratch. [Future incompatible upgrades
            // should swtich to a different table or file.]

            if (!this.dbAreExpectedColumnsPresent())
                throw Components.Exception("DB is missing expected columns",
                                           Cr.NS_ERROR_FILE_CORRUPTED);

            // Change the stored version to the current version. If the user
            // runs the newer code again, it will see the lower version number
            // and re-upgrade (to fixup any entries the old code added).
            this.dbConnection.schemaVersion = DB_VERSION;
            return;
        }

        // Upgrade to newer version...

        this.dbConnection.beginTransaction();

        try {
            for (let v = oldVersion + 1; v <= DB_VERSION; v++) {
                this.log("Upgrading to version " + v + "...");
                let migrateFunction = "dbMigrateToVersion" + v;
                this[migrateFunction]();
            }
        } catch (e) {
            this.log("Migration failed: "  + e);
            this.dbConnection.rollbackTransaction();
            throw e;
        }

        this.dbConnection.schemaVersion = DB_VERSION;
        this.dbConnection.commitTransaction();
        this.log("DB migration completed.");
    },


    /*
     * dbMigrateToVersion1
     *
     * Updates the DB schema to v1 (bug 463154).
     * Adds firstUsed, lastUsed, timesUsed columns.
     */
    dbMigrateToVersion1 : function () {
        // Check to see if the new columns already exist (could be a v1 DB that
        // was downgraded to v0). If they exist, we don't need to add them.
        let query;
        ["timesUsed", "firstUsed", "lastUsed"].forEach(function(column) {
            if (!this.dbColumnExists(column)) {
                query = "ALTER TABLE moz_formhistory ADD COLUMN " + column + " INTEGER";
                this.dbConnection.executeSimpleSQL(query);
            }
        }, this);

        // Set the default values for the new columns.
        //
        // Note that we set the timestamps to 24 hours in the past. We want a
        // timestamp that's recent (so that "keep form history for 90 days"
        // doesn't expire things surprisingly soon), but not so recent that
        // "forget the last hour of stuff" deletes all freshly migrated data.
        let stmt;
        query = "UPDATE moz_formhistory " +
                "SET timesUsed = 1, firstUsed = :time, lastUsed = :time " +
                "WHERE timesUsed isnull OR firstUsed isnull or lastUsed isnull";
        let params = { time: (Date.now() - DAY_IN_MS) * 1000 }
        try {
            stmt = this.dbCreateStatement(query, params);
            stmt.execute();
        } catch (e) {
            this.log("Failed setting timestamps: " + e);
            throw e;
        } finally {
            stmt.reset();
        }
    },


    /*
     * dbMigrateToVersion2
     *
     * Updates the DB schema to v2 (bug 243136).
     * Adds lastUsed index, removes moz_dummy_table
     */
    dbMigrateToVersion2 : function () {
        let query = "DROP TABLE IF EXISTS moz_dummy_table";
        this.dbConnection.executeSimpleSQL(query);

        query = "CREATE INDEX IF NOT EXISTS moz_formhistory_lastused_index ON moz_formhistory (lastUsed)";
        this.dbConnection.executeSimpleSQL(query);
    },


    /*
     * dbMigrateToVersion3
     *
     * Updates the DB schema to v3 (bug 506402).
     * Adds guid column and index.
     */
    dbMigrateToVersion3 : function () {
        // Check to see if GUID column already exists, add if needed
        let query;
        if (!this.dbColumnExists("guid")) {
            query = "ALTER TABLE moz_formhistory ADD COLUMN guid TEXT";
            this.dbConnection.executeSimpleSQL(query);

            query = "CREATE INDEX IF NOT EXISTS moz_formhistory_guid_index ON moz_formhistory (guid)";
            this.dbConnection.executeSimpleSQL(query);
        }

        // Get a list of IDs for existing logins
        let ids = [];
        query = "SELECT id FROM moz_formhistory WHERE guid isnull";
        let stmt;
        try {
            stmt = this.dbCreateStatement(query);
            while (stmt.executeStep())
                ids.push(stmt.row.id);
        } catch (e) {
            this.log("Failed getting IDs: " + e);
            throw e;
        } finally {
            stmt.reset();
        }

        // Generate a GUID for each login and update the DB.
        query = "UPDATE moz_formhistory SET guid = :guid WHERE id = :id";
        for each (let id in ids) {
            let params = {
                id   : id,
                guid : this.generateGUID()
            };

            try {
                stmt = this.dbCreateStatement(query, params);
                stmt.execute();
            } catch (e) {
                this.log("Failed setting GUID: " + e);
                throw e;
            } finally {
                stmt.reset();
            }
        }
    },


    /*
     * dbAreExpectedColumnsPresent
     *
     * Sanity check to ensure that the columns this version of the code expects
     * are present in the DB we're using.
     */
    dbAreExpectedColumnsPresent : function () {
        for (let name in this.dbSchema.tables) {
            let table = this.dbSchema.tables[name];
            let query = "SELECT " +
                        [col for (col in table)].join(", ") +
                        " FROM " + name;
            try {
                let stmt = this.dbConnection.createStatement(query);
                // (no need to execute statement, if it compiled we're good)
                stmt.finalize();
            } catch (e) {
                return false;
            }
        }

        this.log("verified that expected columns are present in DB.");
        return true;
    },


    /*
     * dbColumnExists
     *
     * Checks to see if the named column already exists.
     */
    dbColumnExists : function (columnName) {
        let query = "SELECT " + columnName + " FROM moz_formhistory";
        try {
            let stmt = this.dbConnection.createStatement(query);
            // (no need to execute statement, if it compiled we're good)
            stmt.finalize();
            return true;
        } catch (e) {
            return false;
        }
    },


    /*
     * dbCleanup
     *
     * Called when database creation fails. Finalizes database statements,
     * closes the database connection, deletes the database file.
     */
    dbCleanup : function (backup) {
        this.log("Cleaning up DB file - close & remove & backup=" + backup)

        // Create backup file
        if (backup) {
            let storage = Cc["@mozilla.org/storage/service;1"].
                          getService(Ci.mozIStorageService);

            let backupFile = this.dbFile.leafName + ".corrupt";
            storage.backupDatabaseFile(this.dbFile, backupFile);
        }

        // Finalize all statements to free memory, avoid errors later
        for each (let stmt in this.dbStmts)
            stmt.finalize();
        this.dbStmts = [];

        // Close the connection, ignore 'already closed' error
        try { this.dbConnection.close() } catch(e) {}
        this.dbFile.remove(false);
    }
};

let component = [FormHistory];
var NSGetFactory = XPCOMUtils.generateNSGetFactory(component);
