/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// This verifies that app upgrades produce the expected behaviours.

// Enable loading extensions from the application scope
Services.prefs.setIntPref("extensions.enabledScopes",
                          AddonManager.SCOPE_PROFILE +
                          AddonManager.SCOPE_APPLICATION);

const profileDir = gProfD.clone();
profileDir.append("extensions");

const globalDir = Services.dirsvc.get("XCurProcD", AM_Ci.nsILocalFile);
globalDir.append("extensions");

var gGlobalExisted = globalDir.exists();
var gInstallTime = Date.now();

function run_test() {
  createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "1.9.2");

  // Will be enabled in the first version and disabled in subsequent versions
  var dest = profileDir.clone();
  dest.append("addon1@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon1@tests.mozilla.org",
    version: "1.0",
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "1",
      maxVersion: "1"
    }],
    name: "Test Addon 1",
    targetPlatforms: [
      "XPCShell",
      "WINNT_x86",
    ]
  }, dest);

  // Works in all tested versions
  dest = profileDir.clone();
  dest.append("addon2@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon2@tests.mozilla.org",
    version: "1.0",
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "1",
      maxVersion: "2"
    }],
    name: "Test Addon 2",
    targetPlatforms: [
      "XPCShell_noarch-spidermonkey"
    ]
  }, dest);

  // Will be disabled in the first version and enabled in the second.
  dest = profileDir.clone();
  dest.append("addon3@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon3@tests.mozilla.org",
    version: "1.0",
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "2",
      maxVersion: "2"
    }],
    name: "Test Addon 3",
  }, dest);

  // Will be enabled in both versions but will change version in between
  dest = globalDir.clone();
  dest.append("addon4@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon4@tests.mozilla.org",
    version: "1.0",
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "1",
      maxVersion: "1"
    }],
    name: "Test Addon 4",
  }, dest);
  dest.lastModifiedTime = gInstallTime;

  do_test_pending();

  run_test_1();
}

function end_test() {
  if (!gGlobalExisted) {
    globalDir.remove(true);
  }
  else {
    globalDir.append("addon4@tests.mozilla.org");
    globalDir.remove(true);
  }
  do_test_finished();
}

// Test that the test extensions are all installed
function run_test_1() {
  startupManager();

  AddonManager.getAddonsByIDs(["addon1@tests.mozilla.org",
                               "addon2@tests.mozilla.org",
                               "addon3@tests.mozilla.org",
                               "addon4@tests.mozilla.org"],
                               function([a1, a2, a3, a4]) {

    do_check_neq(a1, null);
    do_check_true(isExtensionInAddonsList(profileDir, a1.id));

    do_check_neq(a2, null);
    do_check_true(isExtensionInAddonsList(profileDir, a2.id));

    do_check_neq(a3, null);
    do_check_false(isExtensionInAddonsList(profileDir, a3.id));

    do_check_neq(a4, null);
    do_check_true(isExtensionInAddonsList(globalDir, a4.id));
    do_check_eq(a4.version, "1.0");

    run_test_2();
  });
}

// Test that upgrading the application disables now incompatible add-ons
function run_test_2() {
  // Upgrade the extension
  dest = globalDir.clone();
  dest.append("addon4@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon4@tests.mozilla.org",
    version: "2.0",
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "2",
      maxVersion: "2"
    }],
    name: "Test Addon 4",
  }, dest);
  dest.lastModifiedTime = gInstallTime;

  restartManager("2");
  AddonManager.getAddonsByIDs(["addon1@tests.mozilla.org",
                               "addon2@tests.mozilla.org",
                               "addon3@tests.mozilla.org",
                               "addon4@tests.mozilla.org"],
                               function([a1, a2, a3, a4]) {

    do_check_neq(a1, null);
    do_check_false(isExtensionInAddonsList(profileDir, a1.id));

    do_check_neq(a2, null);
    do_check_true(isExtensionInAddonsList(profileDir, a2.id));

    do_check_neq(a3, null);
    do_check_true(isExtensionInAddonsList(profileDir, a3.id));

    do_check_neq(a4, null);
    do_check_true(isExtensionInAddonsList(globalDir, a4.id));
    do_check_eq(a4.version, "2.0");

    run_test_3();
  });
}

// Test that nothing changes when only the build ID changes.
function run_test_3() {
  // Upgrade the extension
  dest = globalDir.clone();
  dest.append("addon4@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon4@tests.mozilla.org",
    version: "3.0",
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "3",
      maxVersion: "3"
    }],
    name: "Test Addon 4",
  }, dest);
  dest.lastModifiedTime = gInstallTime;

  // Simulates a simple Build ID change, the platform deletes extensions.ini
  // whenever the application is changed.
  var file = gProfD.clone();
  file.append("extensions.ini");
  file.remove(true);
  restartManager();

  AddonManager.getAddonsByIDs(["addon1@tests.mozilla.org",
                               "addon2@tests.mozilla.org",
                               "addon3@tests.mozilla.org",
                               "addon4@tests.mozilla.org"],
                               function([a1, a2, a3, a4]) {

    do_check_neq(a1, null);
    do_check_false(isExtensionInAddonsList(profileDir, a1.id));

    do_check_neq(a2, null);
    do_check_true(isExtensionInAddonsList(profileDir, a2.id));

    do_check_neq(a3, null);
    do_check_true(isExtensionInAddonsList(profileDir, a3.id));

    do_check_neq(a4, null);
    do_check_true(isExtensionInAddonsList(globalDir, a4.id));
    do_check_eq(a4.version, "2.0");

    end_test();
  });
}
