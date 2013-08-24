/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is the Update Service.
#
# The Initial Developer of the Original Code is Ben Goodger.
# Portions created by the Initial Developer are Copyright (C) 2004
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#  Ben Goodger <ben@mozilla.org> (Original Author)
#  Darin Fisher <darin@meer.net>
#  Ben Turner <bent.mozilla@gmail.com>
#  Jeff Walden <jwalden+code@mit.edu>
#  Alexander J. Vincent <ajvincent@gmail.com>
#  Dão Gottwald <dao@mozilla.com>
#  Robert Strong <robert.bugzilla@gmail.com>
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK ***** */
*/
Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");
Components.utils.import("resource://gre/modules/FileUtils.jsm");
Components.utils.import("resource://gre/modules/AddonManager.jsm");
Components.utils.import("resource://gre/modules/Services.jsm");

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;

const PREF_APP_UPDATE_AUTO                = "app.update.auto";
const PREF_APP_UPDATE_BACKGROUND_INTERVAL = "app.update.download.backgroundInterval";
const PREF_APP_UPDATE_CERTS_BRANCH        = "app.update.certs.";
const PREF_APP_UPDATE_CHANNEL             = "app.update.channel";
const PREF_APP_UPDATE_ENABLED             = "app.update.enabled";
const PREF_APP_UPDATE_IDLETIME            = "app.update.idletime";
const PREF_APP_UPDATE_INCOMPATIBLE_MODE   = "app.update.incompatible.mode";
const PREF_APP_UPDATE_INTERVAL            = "app.update.interval";
const PREF_APP_UPDATE_LOG                 = "app.update.log";
const PREF_APP_UPDATE_MODE                = "app.update.mode";
const PREF_APP_UPDATE_NEVER_BRANCH        = "app.update.never.";
const PREF_APP_UPDATE_POSTUPDATE          = "app.update.postupdate";
const PREF_APP_UPDATE_PROMPTWAITTIME      = "app.update.promptWaitTime";
const PREF_APP_UPDATE_SHOW_INSTALLED_UI   = "app.update.showInstalledUI";
const PREF_APP_UPDATE_SILENT              = "app.update.silent";
const PREF_APP_UPDATE_URL                 = "app.update.url";
const PREF_APP_UPDATE_URL_DETAILS         = "app.update.url.details";
const PREF_APP_UPDATE_URL_OVERRIDE        = "app.update.url.override";

const PREF_PARTNER_BRANCH                 = "app.partner.";
const PREF_APP_DISTRIBUTION               = "distribution.id";
const PREF_APP_DISTRIBUTION_VERSION       = "distribution.version";

const URI_UPDATE_PROMPT_DIALOG  = "chrome://mozapps/content/update/updates.xul";
const URI_UPDATE_HISTORY_DIALOG = "chrome://mozapps/content/update/history.xul";
const URI_BRAND_PROPERTIES      = "chrome://branding/locale/brand.properties";
const URI_UPDATES_PROPERTIES    = "chrome://mozapps/locale/update/updates.properties";
const URI_UPDATE_NS             = "http://www.mozilla.org/2005/app-update";

const CATEGORY_UPDATE_TIMER               = "update-timer";

const KEY_APPDIR          = "XCurProcD";
const KEY_GRED            = "GreD";
#ifdef XP_WIN
#ifndef WINCE
const KEY_UPDROOT         = "UpdRootD";
#endif
#endif

const DIR_UPDATES         = "updates";
const FILE_UPDATE_STATUS  = "update.status";
const FILE_UPDATE_VERSION = "update.version";
const FILE_UPDATE_ARCHIVE = "update.mar";
const FILE_UPDATE_LOG     = "update.log"
const FILE_UPDATES_DB     = "updates.xml";
const FILE_UPDATE_ACTIVE  = "active-update.xml";
const FILE_PERMS_TEST     = "update.test";
const FILE_LAST_LOG       = "last-update.log";
const FILE_BACKUP_LOG     = "backup-update.log";
const FILE_UPDATE_LOCALE  = "update.locale";

const STATE_NONE            = "null";
const STATE_DOWNLOADING     = "downloading";
const STATE_PENDING         = "pending";
const STATE_APPLYING        = "applying";
const STATE_SUCCEEDED       = "succeeded";
const STATE_DOWNLOAD_FAILED = "download-failed";
const STATE_FAILED          = "failed";

// From updater/errors.h:
const WRITE_ERROR        = 7;
const ELEVATION_CANCELED = 9;

const DOWNLOAD_CHUNK_SIZE           = 300000; // bytes
const DOWNLOAD_BACKGROUND_INTERVAL  = 600;    // seconds
const DOWNLOAD_FOREGROUND_INTERVAL  = 0;

const UPDATE_WINDOW_NAME      = "Update:Wizard";

var gLocale     = null;

XPCOMUtils.defineLazyGetter(this, "gLogEnabled", function aus_gLogEnabled() {
  return getPref("getBoolPref", PREF_APP_UPDATE_LOG, false);
});

XPCOMUtils.defineLazyGetter(this, "gUpdateBundle", function aus_gUpdateBundle() {
  return Services.strings.createBundle(URI_UPDATES_PROPERTIES);
});

// shared code for suppressing bad cert dialogs
XPCOMUtils.defineLazyGetter(this, "gCertUtils", function aus_gCertUtils() {
  let temp = { };
  Components.utils.import("resource://gre/modules/CertUtils.jsm", temp);
  return temp;
});

XPCOMUtils.defineLazyGetter(this, "gABI", function aus_gABI() {
  let abi = null;
  try {
    abi = Services.appinfo.XPCOMABI;
  }
  catch (e) {
    LOG("gABI - XPCOM ABI unknown: updates are not possible.");
  }
#ifdef XP_MACOSX
  // Mac universal build should report a different ABI than either macppc
  // or mactel.
  let macutils = Cc["@mozilla.org/xpcom/mac-utils;1"].
                 getService(Ci.nsIMacUtils);

  if (macutils.isUniversalBinary)
    abi += "-u-" + macutils.architecturesInBinary;
#endif
  return abi;
});

XPCOMUtils.defineLazyGetter(this, "gOSVersion", function aus_gOSVersion() {
  let osVersion;
  let sysInfo = Cc["@mozilla.org/system-info;1"].
                getService(Ci.nsIPropertyBag2);
  try {
    osVersion = sysInfo.getProperty("name") + " " + sysInfo.getProperty("version");
  }
  catch (e) {
    LOG("gOSVersion - OS Version unknown: updates are not possible.");
  }

  if (osVersion) {
    try {
      osVersion += " (" + sysInfo.getProperty("secondaryLibrary") + ")";
    }
    catch (e) {
      // Not all platforms have a secondary widget library, so an error is nothing to worry about.
    }
    osVersion = encodeURIComponent(osVersion);
  }
  return osVersion;
});

XPCOMUtils.defineLazyGetter(this, "gCanApplyUpdates", function aus_gCanApplyUpdates() {
  try {
    const NORMAL_FILE_TYPE = Ci.nsILocalFile.NORMAL_FILE_TYPE;
    var updateTestFile = getUpdateFile([FILE_PERMS_TEST]);
    LOG("gCanApplyUpdates - testing write access " + updateTestFile.path);
    if (updateTestFile.exists())
      updateTestFile.remove(false);
    updateTestFile.create(NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);
    updateTestFile.remove(false);
#ifdef XP_WIN
#ifndef WINCE
    var sysInfo = Cc["@mozilla.org/system-info;1"].
                  getService(Ci.nsIPropertyBag2);

    // Example windowsVersion:  Windows XP == 5.1
    var windowsVersion = sysInfo.getProperty("version");
    LOG("gCanApplyUpdates - windowsVersion = " + windowsVersion);

  /**
#    For Vista, updates can be performed to a location requiring admin
#    privileges by requesting elevation via the UAC prompt when launching
#    updater.exe if the appDir is under the Program Files directory
#    (e.g. C:\Program Files\) and UAC is turned on and  we can elevate
#    (e.g. user has a split token).
#
#    Note: this does note attempt to handle the case where UAC is turned on
#    and the installation directory is in a restricted location that
#    requires admin privileges to update other than Program Files.
   */
    var userCanElevate = false;

    if (parseFloat(windowsVersion) >= 6) {
      try {
        var fileLocator = Cc["@mozilla.org/file/directory_service;1"].
                          getService(Ci.nsIProperties);
        // KEY_UPDROOT will fail and throw an exception if
        // appDir is not under the Program Files, so we rely on that
        var dir = fileLocator.get(KEY_UPDROOT, Ci.nsIFile);
        // appDir is under Program Files, so check if the user can elevate
        userCanElevate = Services.appinfo.QueryInterface(Ci.nsIWinAppHelper).
                         userCanElevate;
        LOG("gCanApplyUpdates - on Vista, userCanElevate: " + userCanElevate);
      }
      catch (ex) {
        // When the installation directory is not under Program Files,
        // fall through to checking if write access to the 
        // installation directory is available.
        LOG("gCanApplyUpdates - on Vista, appDir is not under Program Files");
      }
    }

    /**
#      On Windows, we no longer store the update under the app dir if the app
#      dir is under C:\Program Files.
#
#      If we are on Windows (including Vista, if we can't elevate) we need to
#      to check that we can create and remove files from the actual app 
#      directory (like C:\Program Files\Mozilla Firefox).  If we can't
#      (because this user is not an adminstrator, for example) canUpdate()
#      should return false.
#
#      For Vista, we perform this check to enable updating the  application
#      when the user has write access to the installation directory under the
#      following scenarios:
#      1) the installation directory is not under Program Files
#         (e.g. C:\Program Files)
#      2) UAC is turned off
#      3) UAC is turned on and the user is not an admin 
#         (e.g. the user does not have a split token)
#      4) UAC is turned on and the user is already elevated, so they can't be
#         elevated again
     */
    if (!userCanElevate) {
      // if we're unable to create the test file this will throw an exception.
      var appDirTestFile = FileUtils.getFile(KEY_APPDIR, [FILE_PERMS_TEST]);
      LOG("gCanApplyUpdates - testing write access " + appDirTestFile.path);
      if (appDirTestFile.exists())
        appDirTestFile.remove(false)
      appDirTestFile.create(NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);
      appDirTestFile.remove(false);
    }
#endif //WINCE
#endif //XP_WIN
  }
  catch (e) {
     LOG("gCanApplyUpdates - unable to apply updates. Exception: " + e);
    // No write privileges to install directory
    return false;
  }

  LOG("gCanApplyUpdates - able to apply updates");
  return true;
});

XPCOMUtils.defineLazyGetter(this, "gCanCheckForUpdates", function aus_gCanCheckForUpdates() {
  // If the administrator has locked the app update functionality
  // OFF - this is not just a user setting, so disable the manual
  // UI too.
  var enabled = getPref("getBoolPref", PREF_APP_UPDATE_ENABLED, true);
  if (!enabled && Services.prefs.prefIsLocked(PREF_APP_UPDATE_ENABLED)) {
    LOG("gCanCheckForUpdates - unable to automatically check for updates, " +
        "disabled by pref");
    return false;
  }

  // If we don't know the binary platform we're updating, we can't update.
  if (!gABI) {
    LOG("gCanCheckForUpdates - unable to check for updates, unknown ABI");
    return false;
  }

  // If we don't know the OS version we're updating, we can't update.
  if (!gOSVersion) {
    LOG("gCanCheckForUpdates - unable to check for updates, unknown OS " +
        "version");
    return false;
  }

  LOG("gCanCheckForUpdates - able to check for updates");
  return true;
});

/**
 * Logs a string to the error console.
 * @param   string
 *          The string to write to the error console.
 */
function LOG(string) {
  if (gLogEnabled) {
    dump("*** AUS:SVC " + string + "\n");
    Services.console.logStringMessage("AUS:SVC " + string);
  }
}

/**
#  Gets a preference value, handling the case where there is no default.
#  @param   func
#           The name of the preference function to call, on nsIPrefBranch
#  @param   preference
#           The name of the preference
#  @param   defaultValue
#           The default value to return in the event the preference has
#           no setting
#  @returns The value of the preference, or undefined if there was no
#           user or default value.
 */
function getPref(func, preference, defaultValue) {
  try {
    return Services.prefs[func](preference);
  }
  catch (e) {
  }
  return defaultValue;
}

/**
 * Convert a string containing binary values to hex.
 */
function binaryToHex(input) {
  var result = "";
  for (var i = 0; i < input.length; ++i) {
    var hex = input.charCodeAt(i).toString(16);
    if (hex.length == 1)
      hex = "0" + hex;
    result += hex;
  }
  return result;
}

/**
#  Gets the specified directory at the specified hierarchy under the
#  update root directory and creates it if it doesn't exist.
#  @param   pathArray
#           An array of path components to locate beneath the directory
#           specified by |key|
#  @return  nsIFile object for the location specified.
 */
function getUpdateDirCreate(pathArray) {
#ifdef XP_WIN
#ifndef WINCE
  try {
    let dir = FileUtils.getDir(KEY_UPDROOT, pathArray, true);
    return dir;
  } catch (e) {
  }
#endif
#endif
  return FileUtils.getDir(KEY_APPDIR, pathArray, true);
}

/**
 * Gets the file at the specified hierarchy under the update root directory.
 * @param   pathArray
 *          An array of path components to locate beneath the directory
 *          specified by |key|. The last item in this array must be the
 *          leaf name of a file.
 * @return  nsIFile object for the file specified. The file is NOT created
 *          if it does not exist, however all required directories along
 *          the way are.
 */
function getUpdateFile(pathArray) {
  var file = getUpdateDirCreate(pathArray.slice(0, -1));
  file.append(pathArray[pathArray.length - 1]);
  return file;
}

/**
 * Returns human readable status text from the updates.properties bundle
 * based on an error code
 * @param   code
 *          The error code to look up human readable status text for
 * @param   defaultCode
 *          The default code to look up should human readable status text
 *          not exist for |code|
 * @returns A human readable status text string
 */
function getStatusTextFromCode(code, defaultCode) {
  var reason;
  try {
    reason = gUpdateBundle.GetStringFromName("check_error-" + code);
    LOG("getStatusTextFromCode - transfer error: " + reason + ", code: " +
        code);
  }
  catch (e) {
    // Use the default reason
    reason = gUpdateBundle.GetStringFromName("check_error-" + defaultCode);
    LOG("getStatusTextFromCode - transfer error: " + reason +
        ", default code: " + defaultCode);
  }
  return reason;
}

/**
 * Get the Active Updates directory
 * @returns The active updates directory, as a nsIFile object
 */
function getUpdatesDir() {
  // Right now, we only support downloading one patch at a time, so we always
  // use the same target directory.
  return getUpdateDirCreate([DIR_UPDATES, "0"]);
}

/**
 * Reads the update state from the update.status file in the specified
 * directory.
 * @param   dir
 *          The dir to look for an update.status file in
 * @returns The status value of the update.
 */
function readStatusFile(dir) {
  var statusFile = dir.clone();
  statusFile.append(FILE_UPDATE_STATUS);
  var status = readStringFromFile(statusFile) || STATE_NONE;
  LOG("readStatusFile - status: " + status + ", path: " + statusFile.path);
  return status;
}

/**
 * Writes the current update operation/state to a file in the patch
 * directory, indicating to the patching system that operations need
 * to be performed.
 * @param   dir
 *          The patch directory where the update.status file should be
 *          written.
 * @param   state
 *          The state value to write.
 */
function writeStatusFile(dir, state) {
  var statusFile = dir.clone();
  statusFile.append(FILE_UPDATE_STATUS);
  writeStringToFile(statusFile, state);
}

/**
#  Writes the update's application version to a file in the patch directory. If
#  the update doesn't provide application version information via the
#  appVersion attribute the string "null" will be written to the file.
#  This value is compared during startup (in nsUpdateDriver.cpp) to determine if
#  the update should be applied. Note that this won't provide protection from
#  downgrade of the application for the nightly user case where the application
#  version doesn't change.
#  @param   dir
#           The patch directory where the update.version file should be
#           written.
#  @param   version
#           The version value to write. Will be the string "null" when the
#           update doesn't provide the appVersion attribute in the update xml.
 */
function writeVersionFile(dir, version) {
  var versionFile = dir.clone();
  versionFile.append(FILE_UPDATE_VERSION);
  writeStringToFile(versionFile, version);
}

/**
 * Removes the contents of the Updates Directory
 */
function cleanUpUpdatesDir() {
  // Bail out if we don't have appropriate permissions
  try {
    var updateDir = getUpdatesDir();
  }
  catch (e) {
    return;
  }

  var e = updateDir.directoryEntries;
  while (e.hasMoreElements()) {
    var f = e.getNext().QueryInterface(Ci.nsIFile);
    // Preserve the last update log file for debugging purposes
    if (f.leafName == FILE_UPDATE_LOG) {
      try {
        var dir = f.parent.parent;
        var logFile = dir.clone();
        logFile.append(FILE_LAST_LOG);
        if (logFile.exists()) {
          try {
            logFile.moveTo(dir, FILE_BACKUP_LOG);
          }
          catch (e) {
            LOG("cleanUpUpdatesDir - failed to rename file " + logFile.path +
                " to " + FILE_BACKUP_LOG);
          }
        }
        f.moveTo(dir, FILE_LAST_LOG);
        continue;
      }
      catch (e) {
        LOG("cleanUpUpdatesDir - failed to move file " + f.path + " to " +
            dir.path + " and rename it to " + FILE_LAST_LOG);
      }
    }
    // Now, recursively remove this file.  The recusive removal is really
    // only needed on Mac OSX because this directory will contain a copy of
    // updater.app, which is itself a directory.
    try {
      f.remove(true);
    }
    catch (e) {
      LOG("cleanUpUpdatesDir - failed to remove file " + f.path);
    }
  }
}

/**
 * Clean up updates list and the updates directory.
 */
function cleanupActiveUpdate() {
  // Move the update from the Active Update list into the Past Updates list.
  var um = Cc["@mozilla.org/updates/update-manager;1"].
           getService(Ci.nsIUpdateManager);
  um.activeUpdate = null;
  um.saveUpdates();

  // Now trash the updates directory, since we're done with it
  cleanUpUpdatesDir();
}

/**
 * Gets the locale from the update.locale file for replacing %LOCALE% in the
 * update url. The update.locale file can be located in the application
 * directory or the GRE directory with preference given to it being located in
 * the application directory.
 */
function getLocale() {
  if (gLocale)
    return gLocale;

  var localeFile = FileUtils.getFile(KEY_APPDIR, [FILE_UPDATE_LOCALE]);
  if (!localeFile.exists())
    localeFile = FileUtils.getFile(KEY_GRED, [FILE_UPDATE_LOCALE]);

  if (!localeFile.exists())
    throw Components.Exception(FILE_UPDATE_LOCALE + " file doesn't exist in " +
                               "either the " + KEY_APPDIR + " or " + KEY_GRED +
                               " directories", Cr.NS_ERROR_FILE_NOT_FOUND);

  gLocale = readStringFromFile(localeFile);
  LOG("getLocale - getting locale from file: " + localeFile.path +
      ", locale: " + gLocale);
  return gLocale;
}

/**
 * Read the update channel from defaults only.  We do this to ensure that
 * the channel is tightly coupled with the application and does not apply
 * to other instances of the application that may use the same profile.
 */
function getUpdateChannel() {
  var channel = "default";
  var prefName;
  var prefValue;

  try {
    channel = Services.prefs.getDefaultBranch(null).
              getCharPref(PREF_APP_UPDATE_CHANNEL);
  } catch (e) {
    // use default when pref not found
  }

  try {
    var partners = Services.prefs.getChildList(PREF_PARTNER_BRANCH);
    if (partners.length) {
      channel += "-cck";
      partners.sort();

      for each (prefName in partners) {
        prefValue = Services.prefs.getCharPref(prefName);
        channel += "-" + prefValue;
      }
    }
  }
  catch (e) {
    Components.utils.reportError(e);
  }

  return channel;
}

/* Get the distribution pref values, from defaults only */
function getDistributionPrefValue(aPrefName) {
  var prefValue = "default";

  try {
    prefValue = Services.prefs.getDefaultBranch(null).getCharPref(aPrefName);
  } catch (e) {
    // use default when pref not found
  }

  return prefValue;
}

/**
 * An enumeration of items in a JS array.
 * @constructor
 */
function ArrayEnumerator(aItems) {
  this._index = 0;
  if (aItems) {
    for (var i = 0; i < aItems.length; ++i) {
      if (!aItems[i])
        aItems.splice(i, 1);
    }
  }
  this._contents = aItems;
}

ArrayEnumerator.prototype = {
  _index: 0,
  _contents: [],

  hasMoreElements: function ArrayEnumerator_hasMoreElements() {
    return this._index < this._contents.length;
  },

  getNext: function ArrayEnumerator_getNext() {
    return this._contents[this._index++];
  }
};

/**
 * Writes a string of text to a file.  A newline will be appended to the data
 * written to the file.  This function only works with ASCII text.
 */
function writeStringToFile(file, text) {
  var fos = FileUtils.openSafeFileOutputStream(file)
  text += "\n";
  fos.write(text, text.length);
  FileUtils.closeSafeFileOutputStream(fos);
}

/**
 * Reads a string of text from a file.  A trailing newline will be removed
 * before the result is returned.  This function only works with ASCII text.
 */
function readStringFromFile(file) {
  if (!file.exists()) {
    LOG("readStringFromFile - file doesn't exist: " + file.path);
    return null;
  }
  var fis = Cc["@mozilla.org/network/file-input-stream;1"].
            createInstance(Ci.nsIFileInputStream);
  fis.init(file, FileUtils.MODE_RDONLY, FileUtils.PERMS_FILE, 0);
  var sis = Cc["@mozilla.org/scriptableinputstream;1"].
            createInstance(Ci.nsIScriptableInputStream);
  sis.init(fis);
  var text = sis.read(sis.available());
  sis.close();
  if (text[text.length - 1] == "\n")
    text = text.slice(0, -1);
  return text;
}

/**
 * Update Patch
 * @param   patch
 *          A <patch> element to initialize this object with
 * @throws if patch has a size of 0
 * @constructor
 */
function UpdatePatch(patch) {
  this._properties = {};
  for (var i = 0; i < patch.attributes.length; ++i) {
    var attr = patch.attributes.item(i);
    attr.QueryInterface(Ci.nsIDOMAttr);
    switch (attr.name) {
    case "selected":
      this.selected = attr.value == "true";
      break;
    case "size":
      if (0 == parseInt(attr.value)) {
        LOG("UpdatePatch:init - 0-sized patch!");
        throw Cr.NS_ERROR_ILLEGAL_VALUE;
      }
      // fall through
    default:
      this[attr.name] = attr.value;
      break;
    };
  }
}
UpdatePatch.prototype = {
  /**
   * See nsIUpdateService.idl
   */
  serialize: function UpdatePatch_serialize(updates) {
    var patch = updates.createElementNS(URI_UPDATE_NS, "patch");
    patch.setAttribute("type", this.type);
    patch.setAttribute("URL", this.URL);
    patch.setAttribute("hashFunction", this.hashFunction);
    patch.setAttribute("hashValue", this.hashValue);
    patch.setAttribute("size", this.size);
    patch.setAttribute("selected", this.selected);
    patch.setAttribute("state", this.state);

    for (var p in this._properties) {
      if (this._properties[p].present)
        patch.setAttribute(p, this._properties[p].data);
    }

    return patch;
  },

  /**
   * A hash of custom properties
   */
  _properties: null,

  /**
   * See nsIWritablePropertyBag.idl
   */
  setProperty: function UpdatePatch_setProperty(name, value) {
    this._properties[name] = { data: value, present: true };
  },

  /**
   * See nsIWritablePropertyBag.idl
   */
  deleteProperty: function UpdatePatch_deleteProperty(name) {
    if (name in this._properties)
      this._properties[name].present = false;
    else
      throw Cr.NS_ERROR_FAILURE;
  },

  /**
   * See nsIPropertyBag.idl
   */
  get enumerator() {
    var properties = [];
    for (var p in this._properties)
      properties.push(this._properties[p].data);
    return new ArrayEnumerator(properties);
  },

  /**
   * See nsIPropertyBag.idl
   */
  getProperty: function UpdatePatch_getProperty(name) {
    if (name in this._properties &&
        this._properties[name].present)
      return this._properties[name].data;
    throw Cr.NS_ERROR_FAILURE;
  },

  /**
   * Returns whether or not the update.status file for this patch exists at the
   * appropriate location.
   */
  get statusFileExists() {
    var statusFile = getUpdatesDir();
    statusFile.append(FILE_UPDATE_STATUS);
    return statusFile.exists();
  },

  /**
   * See nsIUpdateService.idl
   */
  get state() {
    if (this._properties.state)
      return this._properties.state;
    return STATE_NONE;
  },
  set state(val) {
    this._properties.state = val;
  },

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIUpdatePatch,
                                         Ci.nsIPropertyBag,
                                         Ci.nsIWritablePropertyBag])
};

/**
 * Update
 * Implements nsIUpdate
 * @param   update
 *          An <update> element to initialize this object with
 * @throws if the update contains no patches
 * @constructor
 */
function Update(update) {
  this._properties = {};
  this._patches = [];
  this.isCompleteUpdate = false;
  this.showPrompt = false;
  this.showSurvey = false;
  this.showNeverForVersion = false;
  this.channel = "default"

  // Null <update>, assume this is a message container and do no
  // further initialization
  if (!update)
    return;

  const ELEMENT_NODE = Ci.nsIDOMNode.ELEMENT_NODE;
  for (var i = 0; i < update.childNodes.length; ++i) {
    var patchElement = update.childNodes.item(i);
    if (patchElement.nodeType != ELEMENT_NODE ||
        patchElement.localName != "patch")
      continue;

    patchElement.QueryInterface(Ci.nsIDOMElement);
    try {
      var patch = new UpdatePatch(patchElement);
    } catch (e) {
      continue;
    }
    this._patches.push(patch);
  }

  if (0 == this._patches.length)
    throw Cr.NS_ERROR_ILLEGAL_VALUE;

  for (var i = 0; i < update.attributes.length; ++i) {
    var attr = update.attributes.item(i);
    attr.QueryInterface(Ci.nsIDOMAttr);
    if (attr.value == "undefined")
      continue;
    else if (attr.name == "detailsURL")
      this._detailsURL = attr.value;
    else if (attr.name == "extensionVersion") {
      // Prevent extensionVersion from replacing appVersion if appVersion is
      // present in the update xml.
      if (!this.appVersion)
        this.appVersion = attr.value;
    }
    else if (attr.name == "installDate" && attr.value)
      this.installDate = parseInt(attr.value);
    else if (attr.name == "isCompleteUpdate")
      this.isCompleteUpdate = attr.value == "true";
    else if (attr.name == "isSecurityUpdate")
      this.isSecurityUpdate = attr.value == "true";
    else if (attr.name == "showNeverForVersion")
      this.showNeverForVersion = attr.value == "true";
    else if (attr.name == "showPrompt")
      this.showPrompt = attr.value == "true";
    else if (attr.name == "showSurvey")
      this.showSurvey = attr.value == "true";
    else if (attr.name == "version") {
      // Prevent version from replacing displayVersion if displayVersion is
      // present in the update xml.
      if (!this.displayVersion)
        this.displayVersion = attr.value;
    }
    else {
      this[attr.name] = attr.value;

      switch (attr.name) {
      case "appVersion":
      case "billboardURL":
      case "buildID":
      case "channel":
      case "displayVersion":
      case "licenseURL":
      case "name":
      case "platformVersion":
      case "previousAppVersion":
      case "serviceURL":
      case "statusText":
      case "type":
        break;
      default:
        // Save custom attributes when serializing to the local xml file but
        // don't use this method for the expected attributes which are already
        // handled in serialize.
        this.setProperty(attr.name, attr.value);
        break;
      };
    }
  }

  // Set the initial value with the current time when it doesn't already have a
  // value or the value is already set to 0 (bug 316328).
  if (!this.installDate && this.installDate != 0)
    this.installDate = (new Date()).getTime();

  // The Update Name is either the string provided by the <update> element, or
  // the string: "<App Name> <Update App Version>"
  var name = "";
  if (update.hasAttribute("name"))
    name = update.getAttribute("name");
  else {
    var brandBundle = Services.strings.createBundle(URI_BRAND_PROPERTIES);
    var appName = brandBundle.GetStringFromName("brandShortName");
    name = gUpdateBundle.formatStringFromName("updateName",
                                              [appName, this.displayVersion], 2);
  }
  this.name = name;
}
Update.prototype = {
  /**
   * See nsIUpdateService.idl
   */
  get patchCount() {
    return this._patches.length;
  },

  /**
   * See nsIUpdateService.idl
   */
  getPatchAt: function Update_getPatchAt(index) {
    return this._patches[index];
  },

  /**
   * See nsIUpdateService.idl
   *
   * We use a copy of the state cached on this object in |_state| only when
   * there is no selected patch, i.e. in the case when we could not load
   * |.activeUpdate| from the update manager for some reason but still have
   * the update.status file to work with.
   */
  _state: "",
  set state(state) {
    if (this.selectedPatch)
      this.selectedPatch.state = state;
    this._state = state;
    return state;
  },
  get state() {
    if (this.selectedPatch)
      return this.selectedPatch.state;
    return this._state;
  },

  /**
   * See nsIUpdateService.idl
   */
  errorCode: 0,

  /**
   * See nsIUpdateService.idl
   */
  get selectedPatch() {
    for (var i = 0; i < this.patchCount; ++i) {
      if (this._patches[i].selected)
        return this._patches[i];
    }
    return null;
  },

  /**
   * See nsIUpdateService.idl
   */
  get detailsURL() {
    if (!this._detailsURL) {
      try {
        // Try using a default details URL supplied by the distribution
        // if the update XML does not supply one.
        return Services.urlFormatter.formatURLPref(PREF_APP_UPDATE_URL_DETAILS);
      }
      catch (e) {
      }
    }
    return this._detailsURL || "";
  },

  /**
   * See nsIUpdateService.idl
   */
  serialize: function Update_serialize(updates) {
    var update = updates.createElementNS(URI_UPDATE_NS, "update");
    update.setAttribute("appVersion", this.appVersion);
    update.setAttribute("buildID", this.buildID);
    update.setAttribute("channel", this.channel);
    update.setAttribute("displayVersion", this.displayVersion);
    // for backwards compatibility in case the user downgrades
    update.setAttribute("extensionVersion", this.appVersion);
    update.setAttribute("installDate", this.installDate);
    update.setAttribute("isCompleteUpdate", this.isCompleteUpdate);
    update.setAttribute("name", this.name);
    update.setAttribute("serviceURL", this.serviceURL);
    update.setAttribute("showNeverForVersion", this.showNeverForVersion);
    update.setAttribute("showPrompt", this.showPrompt);
    update.setAttribute("showSurvey", this.showSurvey);
    update.setAttribute("type", this.type);
    // for backwards compatibility in case the user downgrades

    // Optional attributes
    update.setAttribute("version", this.displayVersion);
    if (this.billboardURL)
      update.setAttribute("billboardURL", this.billboardURL);
    if (this.detailsURL)
      update.setAttribute("detailsURL", this.detailsURL);
    if (this.licenseURL)
      update.setAttribute("licenseURL", this.licenseURL);
    if (this.platformVersion)
      update.setAttribute("platformVersion", this.platformVersion);
    if (this.previousAppVersion)
      update.setAttribute("previousAppVersion", this.previousAppVersion);
    if (this.statusText)
      update.setAttribute("statusText", this.statusText);
    updates.documentElement.appendChild(update);

    for (var p in this._properties) {
      if (this._properties[p].present)
        update.setAttribute(p, this._properties[p].data);
    }

    for (var i = 0; i < this.patchCount; ++i)
      update.appendChild(this.getPatchAt(i).serialize(updates));

    return update;
  },

  /**
   * A hash of custom properties
   */
  _properties: null,

  /**
   * See nsIWritablePropertyBag.idl
   */
  setProperty: function Update_setProperty(name, value) {
    this._properties[name] = { data: value, present: true };
  },

  /**
   * See nsIWritablePropertyBag.idl
   */
  deleteProperty: function Update_deleteProperty(name) {
    if (name in this._properties)
      this._properties[name].present = false;
    else
      throw Cr.NS_ERROR_FAILURE;
  },

  /**
   * See nsIPropertyBag.idl
   */
  get enumerator() {
    var properties = [];
    for (var p in this._properties)
      properties.push(this._properties[p].data);
    return new ArrayEnumerator(properties);
  },

  /**
   * See nsIPropertyBag.idl
   */
  getProperty: function Update_getProperty(name) {
    if (name in this._properties && this._properties[name].present)
      return this._properties[name].data;
    throw Cr.NS_ERROR_FAILURE;
  },

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIUpdate,
                                         Ci.nsIPropertyBag,
                                         Ci.nsIWritablePropertyBag])
};

const UpdateServiceFactory = {
  _instance: null,
  createInstance: function (outer, iid) {
    if (outer != null)
      throw Components.results.NS_ERROR_NO_AGGREGATION;
    return this._instance == null ? this._instance = new UpdateService() :
                                    this._instance;
  }
};

/**
 * UpdateService
 * A Service for managing the discovery and installation of software updates.
 * @constructor
 */
function UpdateService() {
  Services.obs.addObserver(this, "xpcom-shutdown", false);
}

UpdateService.prototype = {
  /**
   * The downloader we are using to download updates. There is only ever one of
   * these.
   */
  _downloader: null,

  /**
   * Incompatible add-on count.
   */
  _incompatAddonsCount: 0,

  /**
   * Handle Observer Service notifications
   * @param   subject
   *          The subject of the notification
   * @param   topic
   *          The notification name
   * @param   data
   *          Additional data
   */
  observe: function AUS_observe(subject, topic, data) {
    switch (topic) {
    case "post-update-processing":
      // Clean up any extant updates
      this._postUpdateProcessing();
      break;
    case "xpcom-shutdown":
      Services.obs.removeObserver(this, "xpcom-shutdown");

      // Prevent leaking the downloader (bug 454964)
      this._downloader = null;
      break;
    }
  },

  /**
   * The following needs to happen during the post-update-processing
   * notification from nsUpdateServiceStub.js:
   * 1. post update processing
   * 2. resume of a download that was in progress during a previous session
   * 3. start of a complete update download after the failure to apply a partial
   *    update
   */

  /**
   * Perform post-processing on updates lingering in the updates directory
   * from a previous browser session - either report install failures (and
   * optionally attempt to fetch a different version if appropriate) or
   * notify the user of install success.
   */
  _postUpdateProcessing: function AUS__postUpdateProcessing() {
    if (!this.canCheckForUpdates || !this.canApplyUpdates) {
      LOG("UpdateService:_postUpdateProcessing - unable to check for or apply " +
          "updates... returning early");
      return;
    }

    var status = readStatusFile(getUpdatesDir());
    // STATE_NONE status means that the update.status file is present but a
    // background download error occurred.
    if (status == STATE_NONE) {
      LOG("UpdateService:_postUpdateProcessing - no status, no update");
      cleanupActiveUpdate();
      return;
    }

    var um = Cc["@mozilla.org/updates/update-manager;1"].
             getService(Ci.nsIUpdateManager);
    var update = um.activeUpdate;

    if (status == STATE_DOWNLOADING) {
      LOG("UpdateService:_postUpdateProcessing - patch found in downloading " +
          "state");
      if (update && update.state != STATE_SUCCEEDED) {
        // Resume download
        var status = this.downloadUpdate(update, true);
        if (status == STATE_NONE)
          cleanupActiveUpdate();
      }
      return;
    }

    if (!update)
      update = new Update(null);

    var prompter = Cc["@mozilla.org/updates/update-prompt;1"].
                   createInstance(Ci.nsIUpdatePrompt);

    update.state = status;
    if (status == STATE_SUCCEEDED) {
      update.statusText = gUpdateBundle.GetStringFromName("installSuccess");

      // Update the patch's metadata.
      um.activeUpdate = update;
      Services.prefs.setBoolPref(PREF_APP_UPDATE_POSTUPDATE, true);
      prompter.showUpdateInstalled();

      // Done with this update. Clean it up.
      cleanupActiveUpdate();
    }
    else {
      // If we hit an error, then the error code will be included in the
      // status string following a colon.  If we had an I/O error, then we
      // assume that the patch is not invalid, and we restage the patch so
      // that it can be attempted again the next time we restart.
      var ary = status.split(": ");
      update.state = ary[0];
      if (update.state == STATE_FAILED && ary[1]) {
        update.errorCode = ary[1];
        if (update.errorCode == WRITE_ERROR) {
          prompter.showUpdateError(update);
          writeStatusFile(getUpdatesDir(), update.state = STATE_PENDING);
          writeVersionFile(getUpdatesDir(), update.appVersion);
          return;
        }
        else if (update.errorCode == ELEVATION_CANCELED) {
          writeStatusFile(getUpdatesDir(), update.state = STATE_PENDING);
          writeVersionFile(getUpdatesDir(), update.appVersion);
          return;
        }
      }

      // Something went wrong with the patch application process.
      cleanupActiveUpdate();

      update.statusText = gUpdateBundle.GetStringFromName("patchApplyFailure");
      var oldType = update.selectedPatch ? update.selectedPatch.type
                                         : "complete";
      if (update.selectedPatch && oldType == "partial" && update.patchCount == 2) {
        // Partial patch application failed, try downloading the complete
        // update in the background instead.
        LOG("UpdateService:_postUpdateProcessing - install of partial patch " +
            "failed, downloading complete patch");
        var status = this.downloadUpdate(update, true);
        if (status == STATE_NONE)
          cleanupActiveUpdate();
      }
      else {
        LOG("UpdateService:_postUpdateProcessing - install of complete or " +
            "only one patch offered failed... showing error.");
      }
      update.QueryInterface(Ci.nsIWritablePropertyBag);
      update.setProperty("patchingFailed", oldType);
      prompter.showUpdateError(update);
    }
  },

  /**
   * Notified when a timer fires
   * @param   timer
   *          The timer that fired
   */
  notify: function AUS_notify(timer) {
    // If a download is in progress or the patch has been staged do nothing.
    if (this.isDownloading || this._downloader && this._downloader.patchIsStaged)
      return;

    var self = this;
    var listener = {
      /**
       * See nsIUpdateService.idl
       */
      onProgress: function AUS_notify_onProgress(request, position, totalSize) {
      },

      /**
       * See nsIUpdateService.idl
       */
      onCheckComplete: function AUS_notify_onCheckComplete(request, updates,
                                                           updateCount) {
        self._selectAndInstallUpdate(updates);
      },

      /**
       * See nsIUpdateService.idl
       */
      onError: function AUS_notify_onError(request, update) {
        LOG("UpdateService:notify:listener - error during background update: " +
            update.statusText);
      },
    }
    this.backgroundChecker.checkForUpdates(listener, false);
  },

  /**
   * Determine the update from the specified updates that should be offered.
   * If both valid major and minor updates are available the minor update will
   * be offered.
   * @param   updates
   *          An array of available nsIUpdate items
   * @returns The nsIUpdate to offer.
   */
  selectUpdate: function AUS_selectUpdate(updates) {
    if (updates.length == 0)
      return null;

    // Choose the newest of the available minor and major updates.
    var majorUpdate = null;
    var minorUpdate = null;
    var vc = Services.vc;

    updates.forEach(function(aUpdate) {
      // Ignore updates for older versions of the application and updates for
      // the same version of the application with the same build ID.
      if (vc.compare(aUpdate.appVersion, Services.appinfo.version) < 0 ||
          vc.compare(aUpdate.appVersion, Services.appinfo.version) == 0 &&
          aUpdate.buildID == Services.appinfo.appBuildID) {
        LOG("Checker:selectUpdate - skipping update because the update's " +
            "application version is less than the current application version");
        return;
      }

      // Skip the update if the user responded with "never" to this update's
      // application version and the update specifies showNeverForVersion
      // (see bug 350636).
      let neverPrefName = PREF_APP_UPDATE_NEVER_BRANCH + aUpdate.appVersion;
      if (aUpdate.showNeverForVersion &&
          getPref("getBoolPref", neverPrefName, false)) {
        LOG("Checker:selectUpdate - skipping update because the " +
            "preference " + neverPrefName + " is true");
        return;
      }

      switch (aUpdate.type) {
        case "major":
          if (!majorUpdate)
            majorUpdate = aUpdate;
          else if (vc.compare(majorUpdate.appVersion, aUpdate.appVersion) <= 0)
            majorUpdate = aUpdate;
          break;
        case "minor":
          if (!minorUpdate)
            minorUpdate = aUpdate;
          else if (vc.compare(minorUpdate.appVersion, aUpdate.appVersion) <= 0)
            minorUpdate = aUpdate;
          break;
        default:
          LOG("Checker:selectUpdate - skipping unknown update type: " +
              aUpdate.type);
          break;
      }
    });

    return minorUpdate || majorUpdate;
  },

  /**
   * Reference to the currently selected update for when add-on compatibility
   * is checked.
   */
  _update: null,

  /**
   * Determine which of the specified updates should be installed and begin the
   * download/installation process or notify the user about the update.
   * @param   updates
   *          An array of available updates
   */
  _selectAndInstallUpdate: function AUS__selectAndInstallUpdate(updates) {
    // Return early if there's an active update. The user is already aware and
    // is downloading or performed some user action to prevent notification.
    var um = Cc["@mozilla.org/updates/update-manager;1"].
             getService(Ci.nsIUpdateManager);
    if (um.activeUpdate)
      return;

    var update = this.selectUpdate(updates, updates.length);
    if (!update)
      return;

    var updateEnabled = getPref("getBoolPref", PREF_APP_UPDATE_ENABLED, true);
    if (!updateEnabled) {
      LOG("Checker:_selectAndInstallUpdate - not prompting because update is " +
          "disabled");
      return;
    }

    if (!gCanApplyUpdates) {
      LOG("Checker:_selectAndInstallUpdate - the user is unable to apply " +
          "updates... prompting");
      this._showPrompt(update);
      return;
    }

    /**
#      From this point on there are two possible outcomes:
#      1. download and install the update automatically
#      2. notify the user about the availability of an update
#
#      Notes:
#      a) if the app.update.auto preference is false then automatic download and
#         install is disabled and the user will always be notified.
#      b) Mode is determined by the value of the app.update.mode preference.
#
#      The outcome is determined as follows:
#
#      Update Type      Mode        Incompatible Add-ons   Outcome
#      Major            all         N/A                    Notify
#      Minor            0           N/A                    Auto Install
#      Minor            1 or 2      Yes                    Notify
#      Minor            1 or 2      No                     Auto Install
     */
    if (update.showPrompt) {
      LOG("Checker:_selectAndInstallUpdate - prompting because the update " +
          "snippet specified showPrompt");
      this._showPrompt(update);
      return;
    }

    if (!getPref("getBoolPref", PREF_APP_UPDATE_AUTO, true)) {
      LOG("Checker:_selectAndInstallUpdate - prompting because silent " +
          "install is disabled");
      this._showPrompt(update);
      return;
    }

    if (getPref("getIntPref", PREF_APP_UPDATE_MODE, 1) == 0) {
      // Do not prompt regardless of add-on incompatibilities
      LOG("UpdateService:_selectAndInstallUpdate - no need to show prompt, " +
          "just download the update");
      var status = this.downloadUpdate(update, true);
      if (status == STATE_NONE)
        cleanupActiveUpdate();
      return;
    }

    // Only check add-on compatibility when the version changes.
    if (update.appVersion &&
        Services.vc.compare(update.appVersion, Services.appinfo.version) != 0) {
      this._update = update;
      this._checkAddonCompatibility();
    }
    else {
      LOG("UpdateService:_selectAndInstallUpdate - no need to show prompt, " +
          "just download the update");
      var status = this.downloadUpdate(update, true);
      if (status == STATE_NONE)
        cleanupActiveUpdate();
    }
  },

  _showPrompt: function AUS__showPrompt(update) {
    var prompter = Cc["@mozilla.org/updates/update-prompt;1"].
                   createInstance(Ci.nsIUpdatePrompt);
    prompter.showUpdateAvailable(update);
  },

  _checkAddonCompatibility: function AUS__checkAddonCompatibility() {
    // Get all the installed add-ons
    var self = this;
    AddonManager.getAllAddons(function(addons) {
      self._incompatibleAddons = [];
      addons.forEach(function(addon) {
        // If an add-on isn't appDisabled and isn't userDisabled then it is
        // either active now or the user expects it to be active after the
        // restart. If that is the case and the add-on is not installed by the
        // application and is not compatible with the new application version
        // then the user should be warned that the add-on will become
        // incompatible. If an addon's type equals plugin it is skipped since
        // checking plugins compatibility information isn't supported and
        // getting the scope property of a plugin breaks in some environments
        // (see bug 566787).
        if (addon.type != "plugin" &&
            !addon.appDisabled && !addon.userDisabled &&
            addon.scope != AddonManager.SCOPE_APPLICATION &&
            !addon.isCompatibleWith(self._update.appVersion,
                                    self._update.platformVersion))
          self._incompatibleAddons.push(addon);
      });

      if (self._incompatibleAddons.length > 0) {
      /**
#        PREF_APP_UPDATE_INCOMPATIBLE_MODE
#        Controls the mode in which we check for updates as follows.
#
#          PREF_APP_UPDATE_INCOMPATIBLE_MODE != 1
#          We check for VersionInfo _and_ NewerVersion updates for the
#          incompatible add-ons - i.e. if Foo 1.2 is installed and it is
#          incompatible with the update, and we find Foo 2.0 which is but has
#          not been installed, then we do NOT prompt because the user can
#          download Foo 2.0 when they restart after the update during the add-on
#          mismatch checking UI. This is the default, since it suppresses most
#          prompt dialogs.
#
#          PREF_APP_UPDATE_INCOMPATIBLE_MODE == 1
#          We check for VersionInfo updates for the incompatible add-ons - i.e.
#          if the situation above with Foo 1.2 and available update to 2.0
#          applies, we DO show the prompt since a download operation will be
#          required after the update. This is not the default and is supplied
#          only as a hidden option for those that want it.
       */
        self._updateCheckCount = self._incompatibleAddons.length;
        LOG("UpdateService:_checkAddonCompatibility - checking for " +
            "incompatible add-ons");

        self._incompatibleAddons.forEach(function(addon) {
          addon.findUpdates(this, AddonManager.UPDATE_WHEN_NEW_APP_DETECTED,
                            this._update.appVersion, this._update.platformVersion);
        }, self);
      }
      else {
        LOG("UpdateService:_checkAddonCompatibility - no need to show prompt, " +
            "just download the update");
        var status = self.downloadUpdate(self._update, true);
        if (status == STATE_NONE)
          cleanupActiveUpdate();
        self._update = null;
      }
    });
  },

  // AddonUpdateListener
  onCompatibilityUpdateAvailable: function(addon) {
    // Remove the add-on from the list of add-ons that will become incompatible
    // with the new version of the application.
    for (var i = 0; i < this._incompatibleAddons.length; ++i) {
      if (this._incompatibleAddons[i].id == addon.id) {
        LOG("UpdateService:onAddonUpdateEnded - found update for add-on ID: " +
            addon.id);
        this._incompatibleAddons.splice(i, 1);
      }
    }
  },

  onUpdateAvailable: function(addon, install) {
    if (getPref("getIntPref", PREF_APP_UPDATE_INCOMPATIBLE_MODE, 0) == 1)
      return;

    // If the new version of this add-on is blocklisted for the new application
    // then it isn't a valid update and the user should still be warned that
    // the add-on will become incompatible.
    let bs = Cc["@mozilla.org/extensions/blocklist;1"].
             getService(Ci.nsIBlocklistService);
    if (bs.isAddonBlocklisted(addon.id, install.version,
                              gUpdates.update.appVersion,
                              gUpdates.update.platformVersion))
      return;

    // Compatibility or new version updates mean the same thing here.
    this.onCompatibilityUpdateAvailable(addon);
  },

  onUpdateFinished: function(addon) {
    if (--this._updateCheckCount > 0)
      return;

    if (this._incompatibleAddons.length > 0 || !gCanApplyUpdates) {
      LOG("Checker:onUpdateEnded - prompting because there are incompatible " +
          "add-ons");
      this._showPrompt(this._update);
    }
    else {
      LOG("UpdateService:onUpdateEnded - no need to show prompt, just " +
          "download the update");
      var status = this.downloadUpdate(this._update, true);
      if (status == STATE_NONE)
        cleanupActiveUpdate();
    }
    this._update = null;
  },

  /**
   * The Checker used for background update checks.
   */
  _backgroundChecker: null,

  /**
   * See nsIUpdateService.idl
   */
  get backgroundChecker() {
    if (!this._backgroundChecker)
      this._backgroundChecker = new Checker();
    return this._backgroundChecker;
  },

  /**
   * See nsIUpdateService.idl
   */
  get canCheckForUpdates() {
    return gCanCheckForUpdates;
  },

  /**
   * See nsIUpdateService.idl
   */
  get canApplyUpdates() {
    return gCanApplyUpdates;
  },

  /**
   * See nsIUpdateService.idl
   */
  addDownloadListener: function AUS_addDownloadListener(listener) {
    if (!this._downloader) {
      LOG("UpdateService:addDownloadListener - no downloader!");
      return;
    }
    this._downloader.addDownloadListener(listener);
  },

  /**
   * See nsIUpdateService.idl
   */
  removeDownloadListener: function AUS_removeDownloadListener(listener) {
    if (!this._downloader) {
      LOG("UpdateService:removeDownloadListener - no downloader!");
      return;
    }
    this._downloader.removeDownloadListener(listener);
  },

  /**
   * See nsIUpdateService.idl
   */
  downloadUpdate: function AUS_downloadUpdate(update, background) {
    if (!update)
      throw Cr.NS_ERROR_NULL_POINTER;

    // Don't download the update if the update's version is less than the
    // current application's version or the update's version is the same as the
    // application's version and the build ID is the same as the application's
    // build ID.
    if (update.appVersion &&
        (Services.vc.compare(update.appVersion, Services.appinfo.version) < 0 ||
         update.buildID && update.buildID == Services.appinfo.appBuildID &&
         update.appVersion == Services.appinfo.version)) {
      LOG("UpdateService:downloadUpdate - canceling download of update since " +
          "it is for an earlier or same application version and build ID.\n" +
          "current application version: " + Services.appinfo.version + "\n" +
          "update application version : " + update.appVersion + "\n" +
          "current build ID: " + Services.appinfo.appBuildID + "\n" +
          "update build ID : " + update.buildID);
      cleanupActiveUpdate();
      return STATE_NONE;
    }

    if (this.isDownloading) {
      if (update.isCompleteUpdate == this._downloader.isCompleteUpdate &&
          background == this._downloader.background) {
        LOG("UpdateService:downloadUpdate - no support for downloading more " +
            "than one update at a time");
        return readStatusFile(getUpdatesDir());
      }
      this._downloader.cancel();
    }
    // Set the previous application version prior to downloading the update.
    update.previousAppVersion = Services.appinfo.version;
    this._downloader = new Downloader(background);
    return this._downloader.downloadUpdate(update);
  },

  /**
   * See nsIUpdateService.idl
   */
  pauseDownload: function AUS_pauseDownload() {
    if (this.isDownloading)
      this._downloader.cancel();
  },

  /**
   * See nsIUpdateService.idl
   */
  get isDownloading() {
    return this._downloader && this._downloader.isBusy;
  },

  // nsIClassInfo
  flags: Ci.nsIClassInfo.SINGLETON,
  implementationLanguage: Ci.nsIProgrammingLanguage.JAVASCRIPT,
  getHelperForLanguage: function(language) null,
  getInterfaces: function AUS_getInterfaces(count) {
    var interfaces = [Ci.nsIApplicationUpdateService,
                      Ci.nsITimerCallback,
                      Ci.nsIObserver];
    count.value = interfaces.length;
    return interfaces;
  },

  classID: Components.ID("{B3C290A6-3943-4B89-8BBE-C01EB7B3B311}"),
  _xpcom_factory: UpdateServiceFactory,
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIApplicationUpdateService,
                                         Ci.nsIAddonUpdateCheckListener,
                                         Ci.nsITimerCallback,
                                         Ci.nsIObserver])
};

/**
 * A service to manage active and past updates.
 * @constructor
 */
function UpdateManager() {
  // Ensure the Active Update file is loaded
  var updates = this._loadXMLFileIntoArray(getUpdateFile(
                  [FILE_UPDATE_ACTIVE]));
  if (updates.length > 0)
    this._activeUpdate = updates[0];
}
UpdateManager.prototype = {
  /**
   * All previously downloaded and installed updates, as an array of nsIUpdate
   * objects.
   */
  _updates: null,

  /**
   * The current actively downloading/installing update, as a nsIUpdate object.
   */
  _activeUpdate: null,

  /**
   * Handle Observer Service notifications
   * @param   subject
   *          The subject of the notification
   * @param   topic
   *          The notification name
   * @param   data
   *          Additional data
   */
  observe: function UM_observe(subject, topic, data) {
    // Hack to be able to run and cleanup tests by reloading the update data.
    if (topic == "um-reload-update-data") {
      this._updates = this._loadXMLFileIntoArray(getUpdateFile(
                        [FILE_UPDATES_DB]));
      this._activeUpdate = null;
      var updates = this._loadXMLFileIntoArray(getUpdateFile(
                      [FILE_UPDATE_ACTIVE]));
      if (updates.length > 0)
        this._activeUpdate = updates[0];
    }
  },

  /**
   * Loads an updates.xml formatted file into an array of nsIUpdate items.
   * @param   file
   *          A nsIFile for the updates.xml file
   * @returns The array of nsIUpdate items held in the file.
   */
  _loadXMLFileIntoArray: function UM__loadXMLFileIntoArray(file) {
    if (!file.exists()) {
      LOG("UpdateManager:_loadXMLFileIntoArray: XML file does not exist");
      return [];
    }

    var result = [];
    var fileStream = Cc["@mozilla.org/network/file-input-stream;1"].
                     createInstance(Ci.nsIFileInputStream);
    fileStream.init(file, FileUtils.MODE_RDONLY, FileUtils.PERMS_FILE, 0);
    try {
      var parser = Cc["@mozilla.org/xmlextras/domparser;1"].
                   createInstance(Ci.nsIDOMParser);
      var doc = parser.parseFromStream(fileStream, "UTF-8",
                                       fileStream.available(), "text/xml");

      const ELEMENT_NODE = Ci.nsIDOMNode.ELEMENT_NODE;
      var updateCount = doc.documentElement.childNodes.length;
      for (var i = 0; i < updateCount; ++i) {
        var updateElement = doc.documentElement.childNodes.item(i);
        if (updateElement.nodeType != ELEMENT_NODE ||
            updateElement.localName != "update")
          continue;

        updateElement.QueryInterface(Ci.nsIDOMElement);
        try {
          var update = new Update(updateElement);
        } catch (e) {
          LOG("UpdateManager:_loadXMLFileIntoArray - invalid update");
          continue;
        }
        result.push(update);
      }
    }
    catch (e) {
      LOG("UpdateManager:_loadXMLFileIntoArray - error constructing update " +
          "list. Exception: " + e);
    }
    fileStream.close();
    return result;
  },

  /**
   * Load the update manager, initializing state from state files.
   */
  _ensureUpdates: function UM__ensureUpdates() {
    if (!this._updates) {
      this._updates = this._loadXMLFileIntoArray(getUpdateFile(
                        [FILE_UPDATES_DB]));
      var activeUpdates = this._loadXMLFileIntoArray(getUpdateFile(
                            [FILE_UPDATE_ACTIVE]));
      if (activeUpdates.length > 0)
        this._activeUpdate = activeUpdates[0];
    }
  },

  /**
   * See nsIUpdateService.idl
   */
  getUpdateAt: function UM_getUpdateAt(index) {
    this._ensureUpdates();
    return this._updates[index];
  },

  /**
   * See nsIUpdateService.idl
   */
  get updateCount() {
    this._ensureUpdates();
    return this._updates.length;
  },

  /**
   * See nsIUpdateService.idl
   */
  get activeUpdate() {
    if (this._activeUpdate &&
        this._activeUpdate.channel != getUpdateChannel()) {
      // User switched channels, clear out any old active updates and remove
      // partial downloads
      this._activeUpdate = null;
      this.saveUpdates();

      // Destroy the updates directory, since we're done with it.
      cleanUpUpdatesDir();
    }
    return this._activeUpdate;
  },
  set activeUpdate(activeUpdate) {
    this._addUpdate(activeUpdate);
    this._activeUpdate = activeUpdate;
    if (!activeUpdate) {
      // If |activeUpdate| is null, we have updated both lists - the active list
      // and the history list, so we want to write both files.
      this.saveUpdates();
    }
    else
      this._writeUpdatesToXMLFile([this._activeUpdate],
                                  getUpdateFile([FILE_UPDATE_ACTIVE]));
    return activeUpdate;
  },

  /**
   * Add an update to the Updates list. If the item already exists in the list,
   * replace the existing value with the new value.
   * @param   update
   *          The nsIUpdate object to add.
   */
  _addUpdate: function UM__addUpdate(update) {
    if (!update)
      return;
    this._ensureUpdates();
    if (this._updates) {
      for (var i = 0; i < this._updates.length; ++i) {
        if (this._updates[i] &&
            this._updates[i].appVersion == update.appVersion &&
            this._updates[i].buildID == update.buildID) {
          // Replace the existing entry with the new value, updating
          // all metadata.
          this._updates[i] = update;
          return;
        }
      }
    }
    // Otherwise add it to the front of the list.
    this._updates.unshift(update);
  },

  /**
   * Serializes an array of updates to an XML file
   * @param   updates
   *          An array of nsIUpdate objects
   * @param   file
   *          The nsIFile object to serialize to
   */
  _writeUpdatesToXMLFile: function UM__writeUpdatesToXMLFile(updates, file) {
    var fos = Cc["@mozilla.org/network/safe-file-output-stream;1"].
              createInstance(Ci.nsIFileOutputStream);
    var modeFlags = FileUtils.MODE_WRONLY | FileUtils.MODE_CREATE |
                    FileUtils.MODE_TRUNCATE;
    if (!file.exists())
      file.create(Ci.nsILocalFile.NORMAL_FILE_TYPE, FileUtils.PERMS_FILE);
    fos.init(file, modeFlags, FileUtils.PERMS_FILE, 0);

    try {
      var parser = Cc["@mozilla.org/xmlextras/domparser;1"].
                   createInstance(Ci.nsIDOMParser);
      const EMPTY_UPDATES_DOCUMENT = "<?xml version=\"1.0\"?><updates xmlns=\"http://www.mozilla.org/2005/app-update\"></updates>";
      var doc = parser.parseFromString(EMPTY_UPDATES_DOCUMENT, "text/xml");

      for (var i = 0; i < updates.length; ++i) {
        if (updates[i])
          doc.documentElement.appendChild(updates[i].serialize(doc));
      }

      var serializer = Cc["@mozilla.org/xmlextras/xmlserializer;1"].
                       createInstance(Ci.nsIDOMSerializer);
      serializer.serializeToStream(doc.documentElement, fos, null);
    }
    catch (e) {
    }

    FileUtils.closeSafeFileOutputStream(fos);
  },

  /**
   * See nsIUpdateService.idl
   */
  saveUpdates: function UM_saveUpdates() {
    this._writeUpdatesToXMLFile([this._activeUpdate],
                                getUpdateFile([FILE_UPDATE_ACTIVE]));
    if (this._activeUpdate)
      this._addUpdate(this._activeUpdate);

    this._ensureUpdates();
    // Don't write updates that have a temporary state to the updates.xml file.
    if (this._updates) {
      let updates = this._updates.slice();
      for (let i = updates.length - 1; i >= 0; --i) {
        let state = updates[i].state;
        if (state == STATE_NONE || state == STATE_DOWNLOADING ||
            state == STATE_PENDING) {
          updates.splice(i, 1);
        }
      }

      this._writeUpdatesToXMLFile(updates.slice(0, 10),
                                  getUpdateFile([FILE_UPDATES_DB]));
    }
  },

  classID: Components.ID("{093C2356-4843-4C65-8709-D7DBCBBE7DFB}"),
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIUpdateManager, Ci.nsIObserver])
};

/**
 * Checker
 * Checks for new Updates
 * @constructor
 */
function Checker() {
}
Checker.prototype = {
  /**
   * The XMLHttpRequest object that performs the connection.
   */
  _request  : null,

  /**
   * The nsIUpdateCheckListener callback
   */
  _callback : null,

  /**
   * The URL of the update service XML file to connect to that contains details
   * about available updates.
   */
  getUpdateURL: function UC_getUpdateURL(force) {
    this._forced = force;

    // Use the override URL if specified.
    var url = getPref("getCharPref", PREF_APP_UPDATE_URL_OVERRIDE, null);

    // Otherwise, construct the update URL from component parts.
    if (!url) {
      try {
        url = Services.prefs.getDefaultBranch(null).
              getCharPref(PREF_APP_UPDATE_URL);
      } catch (e) {
      }
    }

    if (!url || url == "") {
      LOG("Checker:getUpdateURL - update URL not defined");
      return null;
    }

    url = url.replace(/%PRODUCT%/g, Services.appinfo.name);
    url = url.replace(/%VERSION%/g, Services.appinfo.version);
    url = url.replace(/%BUILD_ID%/g, Services.appinfo.appBuildID);
    url = url.replace(/%BUILD_TARGET%/g, Services.appinfo.OS + "_" + gABI);
    url = url.replace(/%OS_VERSION%/g, gOSVersion);
    if (/%LOCALE%/.test(url))
      url = url.replace(/%LOCALE%/g, getLocale());
    url = url.replace(/%CHANNEL%/g, getUpdateChannel());
    url = url.replace(/%PLATFORM_VERSION%/g, Services.appinfo.platformVersion);
    url = url.replace(/%DISTRIBUTION%/g,
                      getDistributionPrefValue(PREF_APP_DISTRIBUTION));
    url = url.replace(/%DISTRIBUTION_VERSION%/g,
                      getDistributionPrefValue(PREF_APP_DISTRIBUTION_VERSION));
    url = url.replace(/\+/g, "%2B");

    if (force)
      url += (url.indexOf("?") != -1 ? "&" : "?") + "force=1";

    LOG("Checker:getUpdateURL - update URL: " + url);
    return url;
  },

  /**
   * See nsIUpdateService.idl
   */
  checkForUpdates: function UC_checkForUpdates(listener, force) {
    if (!listener)
      throw Cr.NS_ERROR_NULL_POINTER;

    var url = this.getUpdateURL(force);
    if (!url || (!this.enabled && !force))
      return;

    this._request = Cc["@mozilla.org/xmlextras/xmlhttprequest;1"].
                    createInstance(Ci.nsIXMLHttpRequest);
    this._request.open("GET", url, true);
    this._request.channel.notificationCallbacks = new gCertUtils.BadCertHandler();
    this._request.overrideMimeType("text/xml");
    this._request.setRequestHeader("Cache-Control", "no-cache");

    var self = this;
    this._request.onerror     = function(event) { self.onError(event);    };
    this._request.onload      = function(event) { self.onLoad(event);     };
    this._request.onprogress  = function(event) { self.onProgress(event); };

    LOG("Checker:checkForUpdates - sending request to: " + url);
    this._request.send(null);

    this._callback = listener;
  },

  /**
   * When progress associated with the XMLHttpRequest is received.
   * @param   event
   *          The nsIDOMLSProgressEvent for the load.
   */
  onProgress: function UC_onProgress(event) {
    LOG("Checker:onProgress - " + event.position + "/" + event.totalSize);
    this._callback.onProgress(event.target, event.position, event.totalSize);
  },

  /**
   * Returns an array of nsIUpdate objects discovered by the update check.
   * @throws if the XML document element node name is not updates.
   */
  get _updates() {
    var updatesElement = this._request.responseXML.documentElement;
    if (!updatesElement) {
      LOG("Checker:_updates get - empty updates document?!");
      return [];
    }

    if (updatesElement.nodeName != "updates") {
      LOG("Checker:updates get - unexpected node name!");
      throw new Error("Unexpected node name, expected: updates, got: " +
                      updatesElement.nodeName);
    }

    const ELEMENT_NODE = Ci.nsIDOMNode.ELEMENT_NODE;
    var updates = [];
    for (var i = 0; i < updatesElement.childNodes.length; ++i) {
      var updateElement = updatesElement.childNodes.item(i);
      if (updateElement.nodeType != ELEMENT_NODE ||
          updateElement.localName != "update")
        continue;

      updateElement.QueryInterface(Ci.nsIDOMElement);
      try {
        var update = new Update(updateElement);
      } catch (e) {
        LOG("Checker:updates get - invalid <update/>, ignoring...");
        continue;
      }
      update.serviceURL = this.getUpdateURL(this._forced);
      update.channel = getUpdateChannel();
      updates.push(update);
    }

    return updates;
  },

  /**
   * Returns the status code for the XMLHttpRequest
   */
  _getChannelStatus: function UC__getChannelStatus(request) {
    var status = 0;
    try {
      status = request.status;
    }
    catch (e) {
    }

    if (status == 0)
      status = request.channel.QueryInterface(Ci.nsIRequest).status;
    return status;
  },

  /**
   * The XMLHttpRequest succeeded and the document was loaded.
   * @param   event
   *          The nsIDOMEvent for the load
   */
  onLoad: function UC_onLoad(event) {
    LOG("Checker:onLoad - request completed downloading document");

    var prefs = Services.prefs;
    var certs = null;
    if (!prefs.prefHasUserValue(PREF_APP_UPDATE_URL_OVERRIDE) &&
        prefs.getBranch(PREF_APP_UPDATE_CERTS_BRANCH).getChildList("").length) {
      certs = [];
      let counter = 1;
      while (true) {
        let prefBranchCert = prefs.getBranch(PREF_APP_UPDATE_CERTS_BRANCH +
                                             counter + ".");
        let prefCertAttrs = prefBranchCert.getChildList("");
        if (prefCertAttrs.length == 0)
          break;

        let certAttrs = {};
        for each (let prefCertAttr in prefCertAttrs)
          certAttrs[prefCertAttr] = prefBranchCert.getCharPref(prefCertAttr);

        certs.push(certAttrs);
        counter++;
      }
    }

    var certAttrCheckFailed = false;
    var status;
    try {
      try {
        gCertUtils.checkCert(this._request.channel, certs);
      }
      catch (e) {
        Components.utils.reportError(e);
        if (e.result != Cr.NS_ERROR_ILLEGAL_VALUE)
          throw e;

        certAttrCheckFailed = true;
      }

      // Analyze the resulting DOM and determine the set of updates. If the
      // certificate attribute check failed treat it as no updates found until
      // Bug 583408 is fixed.
      var updates = certAttrCheckFailed ? [] : this._updates;

      LOG("Checker:onLoad - number of updates available: " + updates.length);

      // Tell the Update Service about the updates
      this._callback.onCheckComplete(event.target, updates, updates.length);
    }
    catch (e) {
      LOG("Checker:onLoad - there was a problem with the update service URL " +
          "specified, either the XML file was malformed or it does not exist " +
          "at the location specified. Exception: " + e);
      var request = event.target;
      var status = this._getChannelStatus(request);
      LOG("Checker:onLoad - request.status: " + status);
      var update = new Update(null);
      update.statusText = getStatusTextFromCode(status, 404);
      this._callback.onError(request, update);
    }

    this._request = null;
  },

  /**
   * There was an error of some kind during the XMLHttpRequest
   * @param   event
   *          The nsIDOMEvent for the load
   */
  onError: function UC_onError(event) {
    var request = event.target;
    var status = this._getChannelStatus(request);
    LOG("Checker:onError - request.status: " + status);

    // If we can't find an error string specific to this status code,
    // just use the 200 message from above, which means everything
    // "looks" fine but there was probably an XML error or a bogus file.
    var update = new Update(null);
    update.statusText = getStatusTextFromCode(status, 200);
    this._callback.onError(request, update);

    this._request = null;
  },

  /**
   * Whether or not we are allowed to do update checking.
   */
  _enabled: true,
  get enabled() {
    return getPref("getBoolPref", PREF_APP_UPDATE_ENABLED, true) &&
           gCanCheckForUpdates && this._enabled;
  },

  /**
   * See nsIUpdateService.idl
   */
  stopChecking: function UC_stopChecking(duration) {
    // Always stop the current check
    if (this._request)
      this._request.abort();

    switch (duration) {
    case Ci.nsIUpdateChecker.CURRENT_SESSION:
      this._enabled = false;
      break;
    case Ci.nsIUpdateChecker.ANY_CHECKS:
      this._enabled = false;
      Services.prefs.setBoolPref(PREF_APP_UPDATE_ENABLED, this._enabled);
      break;
    }
  },

  classID: Components.ID("{898CDC9B-E43F-422F-9CC4-2F6291B415A3}"),
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIUpdateChecker])
};

/**
 * Manages the download of updates
 * @param   background
 *          Whether or not this downloader is operating in background
 *          update mode.
 * @constructor
 */
function Downloader(background) {
  this.background = background;
}
Downloader.prototype = {
  /**
   * The nsIUpdatePatch that we are downloading
   */
  _patch: null,

  /**
   * The nsIUpdate that we are downloading
   */
  _update: null,

  /**
   * The nsIIncrementalDownload object handling the download
   */
  _request: null,

  /**
   * Whether or not the update being downloaded is a complete replacement of
   * the user's existing installation or a patch representing the difference
   * between the new version and the previous version.
   */
  isCompleteUpdate: null,

  /**
   * Cancels the active download.
   */
  cancel: function Downloader_cancel() {
    if (this._request && this._request instanceof Ci.nsIRequest)
      this._request.cancel(Cr.NS_BINDING_ABORTED);
  },

  /**
   * Whether or not a patch has been downloaded and staged for installation.
   */
  get patchIsStaged() {
    return readStatusFile(getUpdatesDir()) == STATE_PENDING;
  },

  /**
   * Verify the downloaded file.  We assume that the download is complete at
   * this point.
   */
  _verifyDownload: function Downloader__verifyDownload() {
    if (!this._request)
      return false;

    var destination = this._request.destination;

    // Ensure that the file size matches the expected file size.
    if (destination.fileSize != this._patch.size)
      return false;

    var fileStream = Cc["@mozilla.org/network/file-input-stream;1"].
                     createInstance(Ci.nsIFileInputStream);
    fileStream.init(destination, FileUtils.MODE_RDONLY, FileUtils.PERMS_FILE, 0);

    try {
      var hash = Cc["@mozilla.org/security/hash;1"].
                 createInstance(Ci.nsICryptoHash);
      var hashFunction = Ci.nsICryptoHash[this._patch.hashFunction.toUpperCase()];
      if (hashFunction == undefined)
        throw Cr.NS_ERROR_UNEXPECTED;
      hash.init(hashFunction);
      hash.updateFromStream(fileStream, -1);
      // NOTE: For now, we assume that the format of _patch.hashValue is hex
      // encoded binary (such as what is typically output by programs like
      // sha1sum).  In the future, this may change to base64 depending on how
      // we choose to compute these hashes.
      digest = binaryToHex(hash.finish(false));
    } catch (e) {
      LOG("Downloader:_verifyDownload - failed to compute hash of the " +
          "downloaded update archive");
      digest = "";
    }

    fileStream.close();

    return digest == this._patch.hashValue.toLowerCase();
  },

  /**
   * Select the patch to use given the current state of updateDir and the given
   * set of update patches.
   * @param   update
   *          A nsIUpdate object to select a patch from
   * @param   updateDir
   *          A nsIFile representing the update directory
   * @returns A nsIUpdatePatch object to download
   */
  _selectPatch: function Downloader__selectPatch(update, updateDir) {
    // Given an update to download, we will always try to download the patch
    // for a partial update over the patch for a full update.

    /**
     * Return the first UpdatePatch with the given type.
     * @param   type
     *          The type of the patch ("complete" or "partial")
     * @returns A nsIUpdatePatch object matching the type specified
     */
    function getPatchOfType(type) {
      for (var i = 0; i < update.patchCount; ++i) {
        var patch = update.getPatchAt(i);
        if (patch && patch.type == type)
          return patch;
      }
      return null;
    }

    // Look to see if any of the patches in the Update object has been
    // pre-selected for download, otherwise we must figure out which one
    // to select ourselves.
    var selectedPatch = update.selectedPatch;

    var state = readStatusFile(updateDir);

    // If this is a patch that we know about, then select it.  If it is a patch
    // that we do not know about, then remove it and use our default logic.
    var useComplete = false;
    if (selectedPatch) {
      LOG("Downloader:_selectPatch - found existing patch with state: " +
          state);
      switch (state) {
      case STATE_DOWNLOADING:
        LOG("Downloader:_selectPatch - resuming download");
        return selectedPatch;
      case STATE_PENDING:
        LOG("Downloader:_selectPatch - already downloaded and staged");
        return null;
      default:
        // Something went wrong when we tried to apply the previous patch.
        // Try the complete patch next time.
        if (update && selectedPatch.type == "partial") {
          useComplete = true;
        } else {
          // This is a pretty fatal error.  Just bail.
          LOG("Downloader:_selectPatch - failed to apply complete patch!");
          writeStatusFile(updateDir, STATE_NONE);
          writeVersionFile(getUpdatesDir(), null);
          return null;
        }
      }

      selectedPatch = null;
    }

    // If we were not able to discover an update from a previous download, we
    // select the best patch from the given set.
    var partialPatch = getPatchOfType("partial");
    if (!useComplete)
      selectedPatch = partialPatch;
    if (!selectedPatch) {
      if (partialPatch)
        partialPatch.selected = false;
      selectedPatch = getPatchOfType("complete");
    }

    // Restore the updateDir since we may have deleted it.
    updateDir = getUpdatesDir();

    // if update only contains a partial patch, selectedPatch == null here if
    // the partial patch has been attempted and fails and we're trying to get a
    // complete patch
    if (selectedPatch)
      selectedPatch.selected = true;

    update.isCompleteUpdate = useComplete;

    // Reset the Active Update object on the Update Manager and flush the
    // Active Update DB.
    var um = Cc["@mozilla.org/updates/update-manager;1"].
             getService(Ci.nsIUpdateManager);
    um.activeUpdate = update;

    return selectedPatch;
  },

  /**
   * Whether or not we are currently downloading something.
   */
  get isBusy() {
    return this._request != null;
  },

  /**
   * Download and stage the given update.
   * @param   update
   *          A nsIUpdate object to download a patch for. Cannot be null.
   */
  downloadUpdate: function Downloader_downloadUpdate(update) {
    if (!update)
      throw Cr.NS_ERROR_NULL_POINTER;

    var updateDir = getUpdatesDir();

    this._update = update;

    // This function may return null, which indicates that there are no patches
    // to download.
    this._patch = this._selectPatch(update, updateDir);
    if (!this._patch) {
      LOG("Downloader:downloadUpdate - no patch to download");
      return readStatusFile(updateDir);
    }
    this.isCompleteUpdate = this._patch.type == "complete";

    var patchFile = updateDir.clone();
    patchFile.append(FILE_UPDATE_ARCHIVE);

    var uri = Services.io.newURI(this._patch.URL, null, null);

    this._request = Cc["@mozilla.org/network/incremental-download;1"].
                    createInstance(Ci.nsIIncrementalDownload);

    LOG("Downloader:downloadUpdate - downloading from " + uri.spec + " to " +
        patchFile.path);
    var interval = this.background ? getPref("getIntPref",
                                             PREF_APP_UPDATE_BACKGROUND_INTERVAL,
                                             DOWNLOAD_BACKGROUND_INTERVAL)
                                   : DOWNLOAD_FOREGROUND_INTERVAL;
    this._request.init(uri, patchFile, DOWNLOAD_CHUNK_SIZE, interval);
    this._request.start(this, null);

    writeStatusFile(updateDir, STATE_DOWNLOADING);
    this._patch.QueryInterface(Ci.nsIWritablePropertyBag);
    this._patch.state = STATE_DOWNLOADING;
    var um = Cc["@mozilla.org/updates/update-manager;1"].
             getService(Ci.nsIUpdateManager);
    um.saveUpdates();
    return STATE_DOWNLOADING;
  },

  /**
   * An array of download listeners to notify when we receive
   * nsIRequestObserver or nsIProgressEventSink method calls.
   */
  _listeners: [],

  /**
   * Adds a listener to the download process
   * @param   listener
   *          A download listener, implementing nsIRequestObserver and
   *          nsIProgressEventSink
   */
  addDownloadListener: function Downloader_addDownloadListener(listener) {
    for (var i = 0; i < this._listeners.length; ++i) {
      if (this._listeners[i] == listener)
        return;
    }
    this._listeners.push(listener);
  },

  /**
   * Removes a download listener
   * @param   listener
   *          The listener to remove.
   */
  removeDownloadListener: function Downloader_removeDownloadListener(listener) {
    for (var i = 0; i < this._listeners.length; ++i) {
      if (this._listeners[i] == listener) {
        this._listeners.splice(i, 1);
        return;
      }
    }
  },

  /**
   * When the async request begins
   * @param   request
   *          The nsIRequest object for the transfer
   * @param   context
   *          Additional data
   */
  onStartRequest: function Downloader_onStartRequest(request, context) {
    if (request instanceof Ci.nsIIncrementalDownload)
      LOG("Downloader:onStartRequest - spec: " + request.URI.spec);

    var listenerCount = this._listeners.length;
    for (var i = 0; i < listenerCount; ++i)
      this._listeners[i].onStartRequest(request, context);
  },

  /**
   * When new data has been downloaded
   * @param   request
   *          The nsIRequest object for the transfer
   * @param   context
   *          Additional data
   * @param   progress
   *          The current number of bytes transferred
   * @param   maxProgress
   *          The total number of bytes that must be transferred
   */
  onProgress: function Downloader_onProgress(request, context, progress,
                                             maxProgress) {
    LOG("Downloader:onProgress - progress: " + progress + "/" + maxProgress);

    var listenerCount = this._listeners.length;
    for (var i = 0; i < listenerCount; ++i) {
      var listener = this._listeners[i];
      if (listener instanceof Ci.nsIProgressEventSink)
        listener.onProgress(request, context, progress, maxProgress);
    }
  },

  /**
   * When we have new status text
   * @param   request
   *          The nsIRequest object for the transfer
   * @param   context
   *          Additional data
   * @param   status
   *          A status code
   * @param   statusText
   *          Human readable version of |status|
   */
  onStatus: function Downloader_onStatus(request, context, status, statusText) {
    LOG("Downloader:onStatus - status: " + status + ", statusText: " +
        statusText);

    var listenerCount = this._listeners.length;
    for (var i = 0; i < listenerCount; ++i) {
      var listener = this._listeners[i];
      if (listener instanceof Ci.nsIProgressEventSink)
        listener.onStatus(request, context, status, statusText);
    }
  },

  /**
   * When data transfer ceases
   * @param   request
   *          The nsIRequest object for the transfer
   * @param   context
   *          Additional data
   * @param   status
   *          Status code containing the reason for the cessation.
   */
  onStopRequest: function  Downloader_onStopRequest(request, context, status) {
    if (request instanceof Ci.nsIIncrementalDownload)
      LOG("Downloader:onStopRequest - spec: " + request.URI.spec +
          ", status: " + status);

    var state = this._patch.state;
    var shouldShowPrompt = false;
    var deleteActiveUpdate = false;
    if (Components.isSuccessCode(status)) {
      if (this._verifyDownload()) {
        state = STATE_PENDING;

        // We only need to explicitly show the prompt if this is a backround
        // download, since otherwise some kind of UI is already visible and
        // that UI will notify.
        if (this.background)
          shouldShowPrompt = true;

        // Tell the updater.exe we're ready to apply.
        writeStatusFile(getUpdatesDir(), state);
        writeVersionFile(getUpdatesDir(), this._update.appVersion);
        this._update.installDate = (new Date()).getTime();
        this._update.statusText = gUpdateBundle.GetStringFromName("installPending");
      }
      else {
        LOG("Downloader:onStopRequest - download verification failed");
        state = STATE_DOWNLOAD_FAILED;

        // TODO: use more informative error code here
        status = Cr.NS_ERROR_UNEXPECTED;

        // Yes, this code is a string.
        const vfCode = "verification_failed";
        var message = getStatusTextFromCode(vfCode, vfCode);
        this._update.statusText = message;

        if (this._update.isCompleteUpdate)
          deleteActiveUpdate = true;

        // Destroy the updates directory, since we're done with it.
        cleanUpUpdatesDir();
      }
    }
    else if (status != Cr.NS_BINDING_ABORTED &&
             status != Cr.NS_ERROR_ABORT) {
      LOG("Downloader:onStopRequest - non-verification failure");
      // Some sort of other failure, log this in the |statusText| property
      state = STATE_DOWNLOAD_FAILED;

      // XXXben - if |request| (The Incremental Download) provided a means
      // for accessing the http channel we could do more here.

      const NS_BINDING_FAILED = 2152398849;
      this._update.statusText = getStatusTextFromCode(status,
        NS_BINDING_FAILED);

      // Destroy the updates directory, since we're done with it.
      cleanUpUpdatesDir();

      deleteActiveUpdate = true;
    }
    LOG("Downloader:onStopRequest - setting state to: " + state);
    this._patch.state = state;
    var um = Cc["@mozilla.org/updates/update-manager;1"].
             getService(Ci.nsIUpdateManager);
    if (deleteActiveUpdate) {
      this._update.installDate = (new Date()).getTime();
      um.activeUpdate = null;
    }
    else {
      if (um.activeUpdate)
        um.activeUpdate.state = state;
    }
    um.saveUpdates();

    var listenerCount = this._listeners.length;
    for (var i = 0; i < listenerCount; ++i)
      this._listeners[i].onStopRequest(request, context, status);

    this._request = null;

    if (state == STATE_DOWNLOAD_FAILED) {
      if (!this._update.isCompleteUpdate) {
        var allFailed = true;

        // If we were downloading a patch and the patch verification phase
        // failed, log this and then commence downloading the complete update.
        LOG("Downloader:onStopRequest - verification of patch failed, " +
            "downloading complete update");
        this._update.isCompleteUpdate = true;
        var status = this.downloadUpdate(this._update);

        if (status == STATE_NONE) {
          cleanupActiveUpdate();
        } else {
          allFailed = false;
        }
        // This will reset the |.state| property on this._update if a new
        // download initiates.
      }

      // if we still fail after trying a complete download, give up completely
      if (allFailed) {
        // In all other failure cases, i.e. we're S.O.L. - no more failing over
        // ...

        // If this was ever a foreground download, and now there is no UI active
        // (e.g. because the user closed the download window) and there was an
        // error, we must notify now. Otherwise we can keep the failure to
        // ourselves since the user won't be expecting it.
        try {
          this._update.QueryInterface(Ci.nsIWritablePropertyBag);
          var fgdl = this._update.getProperty("foregroundDownload");
        }
        catch (e) {
        }

        if (fgdl == "true") {
          var prompter = Cc["@mozilla.org/updates/update-prompt;1"].
                         createInstance(Ci.nsIUpdatePrompt);
          this._update.QueryInterface(Ci.nsIWritablePropertyBag);
          this._update.setProperty("downloadFailed", "true");
          prompter.showUpdateError(this._update);
        }
      }

      // Prevent leaking the update object (bug 454964)
      this._update = null;
      // the complete download succeeded or total failure was handled, so exit
      return;
    }

    // Do this after *everything* else, since it will likely cause the app
    // to shut down.
    if (shouldShowPrompt) {
      // Notify the user that an update has been downloaded and is ready for
      // installation (i.e. that they should restart the application). We do
      // not notify on failed update attempts.
      var prompter = Cc["@mozilla.org/updates/update-prompt;1"].
                     createInstance(Ci.nsIUpdatePrompt);
      prompter.showUpdateDownloaded(this._update, true);
    }
    // Prevent leaking the update object (bug 454964)
    this._update = null;
  },

  /**
   * See nsIInterfaceRequestor.idl
   */
  getInterface: function Downloader_getInterface(iid) {
    // The network request may require proxy authentication, so provide the
    // default nsIAuthPrompt if requested.
    if (iid.equals(Ci.nsIAuthPrompt)) {
      var prompt = Cc["@mozilla.org/network/default-auth-prompt;1"].
                   createInstance();
      return prompt.QueryInterface(iid);
    }
    throw Components.results.NS_NOINTERFACE;
  },

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIRequestObserver,
                                         Ci.nsIProgressEventSink,
                                         Ci.nsIInterfaceRequestor])
};

/**
 * UpdatePrompt
 * An object which can prompt the user with information about updates, request
 * action, etc. Embedding clients can override this component with one that
 * invokes a native front end.
 * @constructor
 */
function UpdatePrompt() {
}
UpdatePrompt.prototype = {
  /**
   * See nsIUpdateService.idl
   */
  checkForUpdates: function UP_checkForUpdates() {
    this._showUI(null, URI_UPDATE_PROMPT_DIALOG, null, UPDATE_WINDOW_NAME,
                 null, null);
  },

  /**
   * See nsIUpdateService.idl
   */
  showUpdateAvailable: function UP_showUpdateAvailable(update) {
    if (!this._enabled || this._getUpdateWindow())
      return;

    var stringsPrefix = "updateAvailable_" + update.type + ".";
    var title = gUpdateBundle.formatStringFromName(stringsPrefix + "title",
                                                   [update.name], 1);
    var text = gUpdateBundle.GetStringFromName(stringsPrefix + "text");
    var imageUrl = "";
    this._showUnobtrusiveUI(null, URI_UPDATE_PROMPT_DIALOG, null,
                           UPDATE_WINDOW_NAME, "updatesavailable", update,
                           title, text, imageUrl);
  },

  /**
   * See nsIUpdateService.idl
   */
  showUpdateDownloaded: function UP_showUpdateDownloaded(update, background) {
    if (background) {
      if (!this._enabled)
        return;

      var stringsPrefix = "updateDownloaded_" + update.type + ".";
      var title = gUpdateBundle.formatStringFromName(stringsPrefix + "title",
                                                     [update.name], 1);
      var text = gUpdateBundle.GetStringFromName(stringsPrefix + "text");
      var imageUrl = "";
      this._showUnobtrusiveUI(null, URI_UPDATE_PROMPT_DIALOG, null,
                              UPDATE_WINDOW_NAME, "finishedBackground", update,
                              title, text, imageUrl);
    } else {
      this._showUI(null, URI_UPDATE_PROMPT_DIALOG, null,
                   UPDATE_WINDOW_NAME, "finishedBackground", update);
    }
  },

  /**
   * See nsIUpdateService.idl
   */
  showUpdateInstalled: function UP_showUpdateInstalled() {
    if (!this._enabled || this._getUpdateWindow() ||
        !getPref("getBoolPref", PREF_APP_UPDATE_SHOW_INSTALLED_UI, false))
      return;

    var page = "installed";
    var win = this._getUpdateWindow();
    if (win) {
      if (page && "setCurrentPage" in win)
        win.setCurrentPage(page);
      win.focus();
    }
    else {
      var openFeatures = "chrome,centerscreen,dialog=no,resizable=no,titlebar,toolbar=no";
      var arg = Cc["@mozilla.org/supports-string;1"].
                createInstance(Ci.nsISupportsString);
      arg.data = page;
      Services.ww.openWindow(null, URI_UPDATE_PROMPT_DIALOG, null, openFeatures, arg);
    }
  },

  /**
   * See nsIUpdateService.idl
   */
  showUpdateError: function UP_showUpdateError(update) {
    if (!this._enabled)
      return;

    // In some cases, we want to just show a simple alert dialog:
    if (update.state == STATE_FAILED && update.errorCode == WRITE_ERROR) {
      var title = gUpdateBundle.GetStringFromName("updaterIOErrorTitle");
      var text = gUpdateBundle.formatStringFromName("updaterIOErrorMsg",
                                                    [Services.appinfo.name,
                                                     Services.appinfo.name], 2);
      Services.ww.getNewPrompter(null).alert(title, text);
    } else {
      this._showUI(null, URI_UPDATE_PROMPT_DIALOG, null, UPDATE_WINDOW_NAME,
                   "errors", update);
    }
  },

  /**
   * See nsIUpdateService.idl
   */
  showUpdateHistory: function UP_showUpdateHistory(parent) {
    this._showUI(parent, URI_UPDATE_HISTORY_DIALOG, "modal,dialog=yes",
                 "Update:History", null, null);
  },

  /**
   * Whether or not we are enabled (i.e. not in Silent mode)
   */
  get _enabled() {
    return !getPref("getBoolPref", PREF_APP_UPDATE_SILENT, false);
  },

  /**
   * Returns the update window if present.
   */
  _getUpdateWindow: function UP__getUpdateWindow() {
    return Services.wm.getMostRecentWindow(UPDATE_WINDOW_NAME);
  },

  /**
   * Initiate a less obtrusive UI, starting with a non-modal notification alert
   * @param   parent
   *          A parent window, can be null
   * @param   uri
   *          The URI string of the dialog to show
   * @param   name
   *          The Window Name of the dialog to show, in case it is already open
   *          and can merely be focused
   * @param   page
   *          The page of the wizard to be displayed, if one is already open.
   * @param   update
   *          An update to pass to the UI in the window arguments.
   *          Can be null
   * @param   title
   *          The title for the notification alert.
   * @param   text
   *          The contents of the notification alert.
   * @param   imageUrl
   *          A URL identifying the image to put in the notification alert.
   */
  _showUnobtrusiveUI: function UP__showUnobUI(parent, uri, features, name, page,
                                              update, title, text, imageUrl) {
    var observer = {
      updatePrompt: this,
      service: null,
      timer: null,
      notify: function () {
        // the user hasn't restarted yet => prompt when idle
        this.service.removeObserver(this, "quit-application");
        // If the update window is already open skip showing the UI
        if (this.updatePrompt._getUpdateWindow())
          return;
        this.updatePrompt._showUIWhenIdle(parent, uri, features, name, page, update);
      },
      observe: function (aSubject, aTopic, aData) {
        switch (aTopic) {
          case "alertclickcallback":
            this.updatePrompt._showUI(parent, uri, features, name, page, update);
            // fall thru
          case "quit-application":
            if (this.timer)
              this.timer.cancel();
            this.service.removeObserver(this, "quit-application");
            break;
        }
      }
    };

    // bug 534090 - show the UI for update available notifications when the
    // the system has been idle for at least IDLE_TIME without displaying an
    // alert notification.
    if (page == "updatesavailable") {
      var idleService = Cc["@mozilla.org/widget/idleservice;1"].
                        getService(Ci.nsIIdleService);

      const IDLE_TIME = getPref("getIntPref", PREF_APP_UPDATE_IDLETIME, 60);
      if (idleService.idleTime / 1000 >= IDLE_TIME) {
        this._showUI(parent, uri, features, name, page, update);
        return;
      }
    }

    try {
      var notifier = Cc["@mozilla.org/alerts-service;1"].
                     getService(Ci.nsIAlertsService);
      notifier.showAlertNotification(imageUrl, title, text, true, "", observer);
    }
    catch (e) {
      // Failed to retrieve alerts service, platform unsupported
      this._showUIWhenIdle(parent, uri, features, name, page, update);
      return;
    }

    observer.service = Services.obs;
    observer.service.addObserver(observer, "quit-application", false);

    // bug 534090 - show the UI when idle for update available notifications.
    if (page == "updatesavailable") {
      this._showUIWhenIdle(parent, uri, features, name, page, update);
      return;
    }

    // Give the user x seconds to react before showing the big UI
    var promptWaitTime = getPref("getIntPref", PREF_APP_UPDATE_PROMPTWAITTIME, 43200);
    observer.timer = Cc["@mozilla.org/timer;1"].
                     createInstance(Ci.nsITimer);
    observer.timer.initWithCallback(observer, promptWaitTime * 1000,
                                    observer.timer.TYPE_ONE_SHOT);
  },

  /**
   * Show the UI when the user was idle
   * @param   parent
   *          A parent window, can be null
   * @param   uri
   *          The URI string of the dialog to show
   * @param   name
   *          The Window Name of the dialog to show, in case it is already open
   *          and can merely be focused
   * @param   page
   *          The page of the wizard to be displayed, if one is already open.
   * @param   update
   *          An update to pass to the UI in the window arguments.
   *          Can be null
   */
  _showUIWhenIdle: function UP__showUIWhenIdle(parent, uri, features, name,
                                               page, update) {
    var idleService = Cc["@mozilla.org/widget/idleservice;1"].
                      getService(Ci.nsIIdleService);

    const IDLE_TIME = getPref("getIntPref", PREF_APP_UPDATE_IDLETIME, 60);
    if (idleService.idleTime / 1000 >= IDLE_TIME) {
      this._showUI(parent, uri, features, name, page, update);
    } else {
      var observer = {
        updatePrompt: this,
        observe: function (aSubject, aTopic, aData) {
          switch (aTopic) {
            case "idle":
              // If the update window is already open skip showing the UI
              if (!this.updatePrompt._getUpdateWindow())
                this.updatePrompt._showUI(parent, uri, features, name, page, update);
              // fall thru
            case "quit-application":
              idleService.removeIdleObserver(this, IDLE_TIME);
              Services.obs.removeObserver(this, "quit-application");
              break;
          }
        }
      };
      idleService.addIdleObserver(observer, IDLE_TIME);
      Services.obs.addObserver(observer, "quit-application", false);
    }
  },

  /**
   * Show the Update Checking UI
   * @param   parent
   *          A parent window, can be null
   * @param   uri
   *          The URI string of the dialog to show
   * @param   name
   *          The Window Name of the dialog to show, in case it is already open
   *          and can merely be focused
   * @param   page
   *          The page of the wizard to be displayed, if one is already open.
   * @param   update
   *          An update to pass to the UI in the window arguments.
   *          Can be null
   */
  _showUI: function UP__showUI(parent, uri, features, name, page, update) {
    var ary = null;
    if (update) {
      ary = Cc["@mozilla.org/supports-array;1"].
            createInstance(Ci.nsISupportsArray);
      ary.AppendElement(update);
    }

    var win = this._getUpdateWindow();
    if (win) {
      if (page && "setCurrentPage" in win)
        win.setCurrentPage(page);
      win.focus();
    }
    else {
      var openFeatures = "chrome,centerscreen,dialog=no,resizable=no,titlebar,toolbar=no";
      if (features)
        openFeatures += "," + features;
      Services.ww.openWindow(parent, uri, "", openFeatures, ary);
    }
  },

  classDescription: "Update Prompt",
  contractID: "@mozilla.org/updates/update-prompt;1",
  classID: Components.ID("{27ABA825-35B5-4018-9FDD-F99250A0E722}"),
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIUpdatePrompt])
};

var components = [UpdateService, Checker, UpdatePrompt, UpdateManager];
var NSGetFactory = XPCOMUtils.generateNSGetFactory(components);

#if 0
/**
 * Logs a message and stack trace to the console.
 * @param   string
 *          The string to write to the console.
 */
function STACK(string) {
  dump("*** " + string + "\n");
  stackTrace(arguments.callee.caller.arguments, -1);
}

function stackTraceFunctionFormat(aFunctionName) {
  var classDelimiter = aFunctionName.indexOf("_");
  var className = aFunctionName.substr(0, classDelimiter);
  if (!className)
    className = "<global>";
  var functionName = aFunctionName.substr(classDelimiter + 1, aFunctionName.length);
  if (!functionName)
    functionName = "<anonymous>";
  return className + "::" + functionName;
}

function stackTraceArgumentsFormat(aArguments) {
  arglist = "";
  for (var i = 0; i < aArguments.length; i++) {
    arglist += aArguments[i];
    if (i < aArguments.length - 1)
      arglist += ", ";
  }
  return arglist;
}

function stackTrace(aArguments, aMaxCount) {
  dump("=[STACKTRACE]=====================================================\n");
  dump("*** at: " + stackTraceFunctionFormat(aArguments.callee.name) + "(" +
       stackTraceArgumentsFormat(aArguments) + ")\n");
  var temp = aArguments.callee.caller;
  var count = 0;
  while (temp) {
    dump("***     " + stackTraceFunctionFormat(temp.name) + "(" +
         stackTraceArgumentsFormat(temp.arguments) + ")\n");

    temp = temp.arguments.callee.caller;
    if (aMaxCount > 0 && ++count == aMaxCount)
      break;
  }
  dump("==================================================================\n");
}

function dumpFile(file) {
  dump("*** file = " + file.path + ", exists = " + file.exists() + "\n");
}
#endif
