/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * Test applying an update by applying an update in the background and
 * launching an application
 */

/**
 * This test is identical to test_0201_app_launch_apply_update.js, except
 * that it locks the application directory when the test is launched to
 * check if the alternate updated directory logic works correctly.
 */

/**
 * The MAR file used for this test should not contain a version 2 update
 * manifest file (e.g. updatev2.manifest).
 */

const TEST_ID = "0202";

// Backup the updater.ini and use a custom one to prevent the updater from
// launching a post update executable.
const FILE_UPDATER_INI_BAK = "updater.ini.bak";

// Number of milliseconds for each do_timeout call.
const CHECK_TIMEOUT_MILLI = 1000;

// Maximum number of milliseconds the process that is launched can run before
// the test will try to kill it.
const APP_TIMER_TIMEOUT = 15000;

let gAppTimer;
let gProcess;
let gActiveUpdate;

// Override getUpdatesRootDir on Mac because we need to apply the update
// inside the bundle directory.
function symlinkUpdateFilesIntoBundleDirectory() {
  if (!shouldAdjustPathsOnMac()) {
    return;
  }
  // Symlink active-update.xml and updates/ inside the dist/bin directory
  // to point to the bundle directory.
  // This is necessary because in order to test the code which actually ships
  // with Firefox, we need to perform the update inside the bundle directory,
  // whereas xpcshell runs from dist/bin/, and the updater service code looks
  // at the current process directory to find things like these two files.

  Components.utils.import("resource://gre/modules/ctypes.jsm");
  let libc = ctypes.open("/usr/lib/libc.dylib");
  // We need these two low level APIs because their functionality is not
  // provided in nsIFile APIs.
  let symlink = libc.declare("symlink", ctypes.default_abi, ctypes.int,
                             ctypes.char.ptr, ctypes.char.ptr);
  let unlink = libc.declare("unlink", ctypes.default_abi, ctypes.int,
                            ctypes.char.ptr);

  // Symlink active-update.xml
  let dest = getAppDir();
  dest.append("active-update.xml");
  if (!dest.exists()) {
    dest.create(dest.NORMAL_FILE_TYPE, 0644);
  }
  do_check_true(dest.exists());
  let source = getUpdatesRootDir();
  source.append("active-update.xml");
  unlink(source.path);
  let ret = symlink(dest.path, source.path);
  do_check_eq(ret, 0);
  do_check_true(source.exists());

  // Symlink updates/
  let dest2 = getAppDir();
  dest2.append("updates");
  if (dest2.exists()) {
    dest2.remove(true);
  }
  dest2.create(dest.DIRECTORY_TYPE, 0755);
  do_check_true(dest2.exists());
  let source2 = getUpdatesRootDir();
  source2.append("updates");
  if (source2.exists()) {
    source2.remove(true);
  }
  ret = symlink(dest2.path, source2.path);
  do_check_eq(ret, 0);
  do_check_true(source2.exists());

  // Cleanup the symlinks when the test is finished.
  do_register_cleanup(function() {
    let ret = unlink(source.path);
    do_check_false(source.exists());
    let ret = unlink(source2.path);
    do_check_false(source2.exists());
  });

  // Now, make sure that getUpdatesRootDir returns the application bundle
  // directory, to make the various stuff in the test framework to work
  // correctly.
  getUpdatesRootDir = getAppDir;
}

function run_test() {
  do_test_pending();
  do_register_cleanup(end_test);

  removeUpdateDirsAndFiles();

  symlinkUpdateFilesIntoBundleDirectory();
  if (IS_WIN) {
    adjustPathsOnWindows();
  }

  if (!gAppBinPath) {
    do_throw("Main application binary not found... expected: " +
             APP_BIN_NAME + APP_BIN_SUFFIX);
    return;
  }

  // Don't attempt to show a prompt when the update is finished.
  Services.prefs.setBoolPref(PREF_APP_UPDATE_SILENT, true);

  let channel = Services.prefs.getCharPref(PREF_APP_UPDATE_CHANNEL);
  let patches = getLocalPatchString(null, null, null, null, null, "true",
                                    STATE_PENDING);
  let updates = getLocalUpdateString(patches, null, null, null, null, null,
                                     null, null, null, null, null, null,
                                     null, "true", channel);
  writeUpdatesToXMLFile(getLocalUpdatesXMLString(updates), true);

  // Read the application.ini and use its application version
  let processDir = getAppDir();
  lockDirectory(processDir);
  let file = processDir.clone();
  file.append("application.ini");
  let ini = AUS_Cc["@mozilla.org/xpcom/ini-parser-factory;1"].
            getService(AUS_Ci.nsIINIParserFactory).
            createINIParser(file);
  let version = ini.getString("App", "Version");
  writeVersionFile(version);
  writeStatusFile(STATE_PENDING);

  // Remove the old updated directory which might be left over from previous tests.
  let oldUpdatedDir = processDir.clone();
  oldUpdatedDir.append(UPDATED_DIR_SUFFIX.replace("/", ""));
  if (oldUpdatedDir.exists()) {
    oldUpdatedDir.remove(true);
  }

  // This is the directory where the update files will be located
  let updateTestDir = getUpdateTestDir();
  try {
    removeDirRecursive(updateTestDir);
  }
  catch (e) {
    logTestInfo("unable to remove directory - path: " + updateTestDir.path +
                ", exception: " + e);
  }

  let updatesPatchDir = getUpdatesDir();
  updatesPatchDir.append("0");
  let mar = do_get_file("data/simple.mar");
  mar.copyTo(updatesPatchDir, FILE_UPDATE_ARCHIVE);

  reloadUpdateManagerData();
  gActiveUpdate = gUpdateManager.activeUpdate;
  do_check_true(!!gActiveUpdate);

  let updateSettingsIni = processDir.clone();
  updateSettingsIni.append(UPDATE_SETTINGS_INI_FILE);
  writeFile(updateSettingsIni, UPDATE_SETTINGS_CONTENTS);

  // Initiate a background update.
  AUS_Cc["@mozilla.org/updates/update-processor;1"].
    createInstance(AUS_Ci.nsIUpdateProcessor).
    processUpdate(gActiveUpdate);

  checkUpdateApplied();
}

function end_test() {
  // Remove the files added by the update.
  let updateTestDir = getUpdateTestDir();
  try {
    logTestInfo("removing update test directory " + updateTestDir.path);
    removeDirRecursive(updateTestDir);
  }
  catch (e) {
    logTestInfo("unable to remove directory - path: " + updateTestDir.path +
                ", exception: " + e);
  }

  if (IS_UNIX) {
    // This will delete the launch script if it exists.
    getLaunchScript();
    if (IS_MACOSX) {
      // This will delete the version script and version file if they exist.
      getVersionScriptAndFile();
    }
  }

  cleanUp();
}

function shouldAdjustPathsOnMac() {
  // When running xpcshell tests locally, xpcshell and firefox-bin do not live
  // in the same directory.
  let dir = getCurrentProcessDir();
  return (IS_MACOSX && dir.leafName != "MacOS");
}

/**
 * This function copies the entire process directory over to a new one which we
 * can write to, so that we can test under Windows which holds locks on opened
 * files.
 */
function adjustPathsOnWindows() {
  // We copy the entire GRE directory into another location so that xpcshell
  // running doesn't prevent the updater from moving stuff around.
  let tmpDir = do_get_profile();
  tmpDir.append("ExecutableDir.tmp");
  tmpDir.createUnique(tmpDir.DIRECTORY_TYPE, 0755);
  let procDir = getCurrentProcessDir();
  procDir.copyTo(tmpDir, "bin");
  let newDir = tmpDir.clone();
  newDir.append("bin");
  gWindowsBinDir = newDir;
  logTestInfo("Using this new bin directory: " + gWindowsBinDir.path);
  // Note that this directory will be deleted as part of the xpcshell teardown,
  // so we don't need to remove it explicitly.

  // We need to make NS_GRE_DIR point to the new bindir, since
  // nsUpdateProcessor::ProcessUpdate uses NS_GRE_DIR to construct the
  // destination path name which would be passed to updater.exe.
  let dirProvider = {
    getFile: function DP_getFile(prop, persistent) {
      persistent.value = true;
      if (prop == NS_GRE_DIR)
        return getAppDir();
      return null;
    },
    QueryInterface: function(iid) {
      if (iid.equals(AUS_Ci.nsIDirectoryServiceProvider) ||
          iid.equals(AUS_Ci.nsISupports))
        return this;
      throw AUS_Cr.NS_ERROR_NO_INTERFACE;
    }
  };
  let ds = Services.dirsvc.QueryInterface(AUS_Ci.nsIDirectoryService);
  ds.QueryInterface(AUS_Ci.nsIProperties).undefine(NS_GRE_DIR);
  ds.registerProvider(dirProvider);
  do_register_cleanup(function() {
    ds.unregisterProvider(dirProvider);
  });
}

/**
 * Gets the directory where the update adds / removes the files contained in the
 * update.
 *
 * @return  nsIFile for the directory where the update adds / removes the files
 *          contained in the update mar.
 */
function getUpdateTestDir() {
  let updateTestDir = getAppDir();
  if (IS_MACOSX) {
    updateTestDir = updateTestDir.parent.parent;
  }
  updateTestDir.append("update_test");
  return updateTestDir;
}

/**
 * Checks if the update has failed being applied in the background.
 */
function checkUpdateApplied() {
  // Don't proceed until the update has failed, and reset to pending.
  if (gUpdateManager.activeUpdate.state != STATE_PENDING) {
    do_timeout(CHECK_TIMEOUT_MILLI, checkUpdateApplied);
    return;
  }

  // Don't proceed until the update status is pending.
  let status = readStatusFile();
  do_check_eq(status, STATE_PENDING);

  unlockDirectory(getAppDir());

  removeCallbackCopy();
}
