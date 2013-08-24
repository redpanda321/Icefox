/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// This verifies that add-on update checks work

// The test extension uses an insecure update url.
Services.prefs.setBoolPref("extensions.checkUpdateSecurity", false);
// This test requires lightweight themes update to be enabled even if the app
// doesn't support lightweight themes.
Services.prefs.setBoolPref("lightweightThemes.update.enabled", true);

Components.utils.import("resource://gre/modules/LightweightThemeManager.jsm");

const PARAMS = "?%REQ_VERSION%/%ITEM_ID%/%ITEM_VERSION%/%ITEM_MAXAPPVERSION%/" +
               "%ITEM_STATUS%/%APP_ID%/%APP_VERSION%/%CURRENT_APP_VERSION%/" +
               "%APP_OS%/%APP_ABI%/%APP_LOCALE%/%UPDATE_TYPE%";

var gInstallDate;

do_load_httpd_js();
var testserver;
const profileDir = gProfD.clone();
profileDir.append("extensions");

function run_test() {
  createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "1.9.2");

  // Create and configure the HTTP server.
  testserver = new nsHttpServer();
  testserver.registerDirectory("/data/", do_get_file("data"));
  testserver.registerDirectory("/addons/", do_get_file("addons"));
  testserver.start(4444);

  var dest = profileDir.clone();
  dest.append("addon1@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon1@tests.mozilla.org",
    version: "1.0",
    updateURL: "http://localhost:4444/data/test_update.rdf",
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "1",
      maxVersion: "1"
    }],
    name: "Test Addon 1",
  }, dest);

  dest = profileDir.clone();
  dest.append("addon2@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon2@tests.mozilla.org",
    version: "1.0",
    updateURL: "http://localhost:4444/data/test_update.rdf",
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "0",
      maxVersion: "0"
    }],
    name: "Test Addon 2",
  }, dest);

  dest = profileDir.clone();
  dest.append("addon3@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon3@tests.mozilla.org",
    version: "1.0",
    updateURL: "http://localhost:4444/data/test_update.rdf",
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "5",
      maxVersion: "5"
    }],
    name: "Test Addon 3",
  }, dest);

  startupManager();

  do_test_pending();
  run_test_1();
}

function end_test() {
  testserver.stop(do_test_finished);
}

// Verify that an update is available and can be installed.
function run_test_1() {
  AddonManager.getAddonByID("addon1@tests.mozilla.org", function(a1) {
    do_check_neq(a1, null);
    do_check_eq(a1.version, "1.0");
    do_check_true(a1.applyBackgroundUpdates);
    do_check_eq(a1.releaseNotesURI, null);

    a1.applyBackgroundUpdates = true;

    prepare_test({
      "addon1@tests.mozilla.org": [
        ["onPropertyChanged", ["applyBackgroundUpdates"]]
      ]
    });
    a1.applyBackgroundUpdates = false;
    check_test_completed();

    a1.applyBackgroundUpdates = false;

    prepare_test({}, [
      "onNewInstall",
    ]);

    a1.findUpdates({
      onNoCompatibilityUpdateAvailable: function(addon) {
        do_throw("Should not have seen no compatibility update");
      },

      onUpdateAvailable: function(addon, install) {
        ensure_test_completed();

        do_check_eq(addon, a1);
        do_check_eq(install.name, addon.name);
        do_check_eq(install.version, "2.0");
        do_check_eq(install.state, AddonManager.STATE_AVAILABLE);
        do_check_eq(install.existingAddon, addon);
        do_check_eq(install.releaseNotesURI.spec, "http://example.com/updateInfo.xhtml");

        prepare_test({}, [
          "onDownloadStarted",
          "onDownloadEnded",
        ], check_test_1);
        install.install();
      },

      onNoUpdateAvailable: function(addon) {
        do_throw("Should have seen an update");
      }
    }, AddonManager.UPDATE_WHEN_USER_REQUESTED);
  });
}

function check_test_1(install) {
  ensure_test_completed();
  do_check_eq(install.state, AddonManager.STATE_DOWNLOADED);
  run_test_2();
}

// Continue installing the update.
function run_test_2() {
  prepare_test({
    "addon1@tests.mozilla.org": [
      "onInstalling"
    ]
  }, [
    "onInstallStarted",
    "onInstallEnded",
  ], check_test_2);
}

function check_test_2() {
  ensure_test_completed();

  AddonManager.getAddonByID("addon1@tests.mozilla.org", function(olda1) {
    do_check_neq(olda1, null);
    do_check_eq(olda1.version, "1.0");
    do_check_true(isExtensionInAddonsList(profileDir, olda1.id));

    shutdownManager();

    do_check_false(isExtensionInAddonsList(profileDir, olda1.id));

    startupManager();

    do_check_true(isExtensionInAddonsList(profileDir, olda1.id));

    AddonManager.getAddonByID("addon1@tests.mozilla.org", function(a1) {
      do_check_neq(a1, null);
      do_check_eq(a1.version, "2.0");
      do_check_true(isExtensionInAddonsList(profileDir, a1.id));
      do_check_false(a1.applyBackgroundUpdates);
      do_check_eq(a1.releaseNotesURI.spec, "http://example.com/updateInfo.xhtml");

      a1.uninstall();
      restartManager();

      run_test_3();
    });
  });
}


// Check that an update check finds compatibility updates and applies them
function run_test_3() {
  AddonManager.getAddonByID("addon2@tests.mozilla.org", function(a2) {
    do_check_neq(a2, null);
    do_check_false(a2.isActive);
    do_check_false(a2.isCompatible);
    do_check_true(a2.appDisabled);
    do_check_true(a2.isCompatibleWith("0"));

    a2.findUpdates({
      onCompatibilityUpdateAvailable: function(addon) {
        do_check_true(a2.isCompatible);
        do_check_false(a2.appDisabled);
        do_check_false(a2.isActive);
      },

      onUpdateAvailable: function(addon, install) {
        do_throw("Should not have seen an available update");
      },

      onNoUpdateAvailable: function(addon) {
        do_check_eq(addon, a2);
        restartManager();
        check_test_3();
      }
    }, AddonManager.UPDATE_WHEN_USER_REQUESTED);
  });
}

function check_test_3() {
  AddonManager.getAddonByID("addon2@tests.mozilla.org", function(a2) {
    do_check_neq(a2, null);
    do_check_true(a2.isActive);
    do_check_true(a2.isCompatible);
    do_check_false(a2.appDisabled);
    a2.uninstall();

    run_test_4();
  });
}

// Checks that we see no compatibility information when there is none.
function run_test_4() {
  AddonManager.getAddonByID("addon3@tests.mozilla.org", function(a3) {
    do_check_neq(a3, null);
    do_check_false(a3.isActive);
    do_check_false(a3.isCompatible);
    do_check_true(a3.appDisabled);
    do_check_true(a3.isCompatibleWith("5"));
    do_check_false(a3.isCompatibleWith("2"));

    a3.findUpdates({
      sawUpdate: false,
      onCompatibilityUpdateAvailable: function(addon) {
        do_throw("Should not have seen compatibility information");
      },

      onNoCompatibilityUpdateAvailable: function(addon) {
        this.sawUpdate = true;
      },

      onUpdateAvailable: function(addon, install) {
        do_throw("Should not have seen an available update");
      },

      onNoUpdateAvailable: function(addon) {
        do_check_true(this.sawUpdate);
        run_test_5();
      }
    }, AddonManager.UPDATE_WHEN_USER_REQUESTED);
  });
}

// Checks that compatibility info for future apps are detected but don't make
// the item compatibile.
function run_test_5() {
  AddonManager.getAddonByID("addon3@tests.mozilla.org", function(a3) {
    do_check_neq(a3, null);
    do_check_false(a3.isActive);
    do_check_false(a3.isCompatible);
    do_check_true(a3.appDisabled);
    do_check_true(a3.isCompatibleWith("5"));
    do_check_false(a3.isCompatibleWith("2"));

    a3.findUpdates({
      sawUpdate: false,
      onCompatibilityUpdateAvailable: function(addon) {
        do_check_false(a3.isCompatible);
        do_check_true(a3.appDisabled);
        do_check_false(a3.isActive);
        this.sawUpdate = true;
      },

      onNoCompatibilityUpdateAvailable: function(addon) {
        do_throw("Should have seen some compatibility information");
      },

      onUpdateAvailable: function(addon, install) {
        do_throw("Should not have seen an available update");
      },

      onNoUpdateAvailable: function(addon) {
        do_check_true(this.sawUpdate);
        restartManager();
        check_test_5();
      }
    }, AddonManager.UPDATE_WHEN_USER_REQUESTED, "3.0");
  });
}

function check_test_5() {
  AddonManager.getAddonByID("addon3@tests.mozilla.org", function(a3) {
    do_check_neq(a3, null);
    do_check_false(a3.isActive);
    do_check_false(a3.isCompatible);
    do_check_true(a3.appDisabled);

    a3.uninstall();
    restartManager();

    run_test_6();
  });
}

// Test that background update checks work
function run_test_6() {
  var dest = profileDir.clone();
  dest.append("addon1@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon1@tests.mozilla.org",
    version: "1.0",
    updateURL: "http://localhost:4444/data/test_update.rdf",
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "1",
      maxVersion: "1"
    }],
    name: "Test Addon 1",
  }, dest);
  restartManager();

  prepare_test({}, [
    "onNewInstall",
    "onDownloadStarted",
    "onDownloadEnded"
  ], continue_test_6);

  // Fake a timer event to cause a background update and wait for the magic to
  // happen
  gInternalManager.notify(null);
}

function continue_test_6(install) {
  do_check_neq(install.existingAddon, null);
  do_check_eq(install.existingAddon.id, "addon1@tests.mozilla.org");

  prepare_test({
    "addon1@tests.mozilla.org": [
      "onInstalling"
    ]
  }, [
    "onInstallStarted",
    "onInstallEnded",
  ], check_test_6);
}

function check_test_6(install) {
  do_check_eq(install.existingAddon.pendingUpgrade.install, install);

  restartManager();
  AddonManager.getAddonByID("addon1@tests.mozilla.org", function(a1) {
    do_check_neq(a1, null);
    do_check_eq(a1.version, "2.0");
    do_check_eq(a1.releaseNotesURI.spec, "http://example.com/updateInfo.xhtml");
    a1.uninstall();
    restartManager();

    run_test_7();
  });
}

// Test that background update checks work for lightweight themes
function run_test_7() {
  LightweightThemeManager.currentTheme = {
    id: "1",
    version: "1",
    name: "Test LW Theme",
    description: "A test theme",
    author: "Mozilla",
    homepageURL: "http://localhost:4444/data/index.html",
    headerURL: "http://localhost:4444/data/header.png",
    footerURL: "http://localhost:4444/data/footer.png",
    previewURL: "http://localhost:4444/data/preview.png",
    iconURL: "http://localhost:4444/data/icon.png",
    updateURL: "http://localhost:4444/data/lwtheme.js"
  };

  // XXX The lightweight theme manager strips non-https updateURLs so hack it
  // back in.
  let themes = JSON.parse(Services.prefs.getCharPref("lightweightThemes.usedThemes"));
  do_check_eq(themes.length, 1);
  themes[0].updateURL = "http://localhost:4444/data/lwtheme.js";
  Services.prefs.setCharPref("lightweightThemes.usedThemes", JSON.stringify(themes));

  testserver.registerPathHandler("/data/lwtheme.js", function(request, response) {
    response.write(JSON.stringify({
      id: "1",
      version: "2",
      name: "Updated Theme",
      description: "A test theme",
      author: "Mozilla",
      homepageURL: "http://localhost:4444/data/index2.html",
      headerURL: "http://localhost:4444/data/header.png",
      footerURL: "http://localhost:4444/data/footer.png",
      previewURL: "http://localhost:4444/data/preview.png",
      iconURL: "http://localhost:4444/data/icon2.png",
      updateURL: "http://localhost:4444/data/lwtheme.js"
    }));
  });

  AddonManager.getAddonByID("1@personas.mozilla.org", function(p1) {
    do_check_neq(p1, null);
    do_check_eq(p1.version, "1");
    do_check_eq(p1.name, "Test LW Theme");
    do_check_true(p1.isActive);
    do_check_eq(p1.installDate.getTime(), p1.updateDate.getTime());

    // 5 seconds leeway seems like a lot, but tests can run slow and really if
    // this is within 5 seconds it is fine. If it is going to be wrong then it
    // is likely to be hours out at least
    do_check_true((Date.now() - p1.installDate.getTime()) < 5000);

    gInstallDate = p1.installDate.getTime();

    prepare_test({
      "1@personas.mozilla.org": [
        ["onInstalling", false],
        "onInstalled"
      ]
    }, [
      "onExternalInstall"
    ], check_test_7);
  
    // Fake a timer event to cause a background update and wait for the magic to
    // happen
    gInternalManager.notify(null);
  });
}

function check_test_7() {
  AddonManager.getAddonByID("1@personas.mozilla.org", function(p1) {
    do_check_neq(p1, null);
    do_check_eq(p1.version, "2");
    do_check_eq(p1.name, "Updated Theme");
    do_check_eq(p1.installDate.getTime(), gInstallDate);
    do_check_true(p1.installDate.getTime() < p1.updateDate.getTime());

    // 5 seconds leeway seems like a lot, but tests can run slow and really if
    // this is within 5 seconds it is fine. If it is going to be wrong then it
    // is likely to be hours out at least
    do_check_true((Date.now() - p1.updateDate.getTime()) < 5000);

    gInstallDate = p1.installDate.getTime();

    run_test_8();
  });
}

// Verify the parameter escaping in update urls.
function run_test_8() {
  var dest = profileDir.clone();
  dest.append("addon1@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon1@tests.mozilla.org",
    version: "5.0",
    updateURL: "http://localhost:4444/data/param_test.rdf" + PARAMS,
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "1",
      maxVersion: "2"
    }],
    name: "Test Addon 1",
  }, dest);

  dest = profileDir.clone();
  dest.append("addon2@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon2@tests.mozilla.org",
    version: "67.0.5b1",
    updateURL: "http://localhost:4444/data/param_test.rdf" + PARAMS,
    targetApplications: [{
      id: "toolkit@mozilla.org",
      minVersion: "0",
      maxVersion: "3"
    }],
    name: "Test Addon 2",
  }, dest);

  dest = profileDir.clone();
  dest.append("addon3@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon3@tests.mozilla.org",
    version: "1.3+",
    updateURL: "http://localhost:4444/data/param_test.rdf" + PARAMS,
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "0",
      maxVersion: "0"
    }, {
      id: "toolkit@mozilla.org",
      minVersion: "0",
      maxVersion: "3"
    }],
    name: "Test Addon 3",
  }, dest);

  dest = profileDir.clone();
  dest.append("addon4@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon4@tests.mozilla.org",
    version: "0.5ab6",
    updateURL: "http://localhost:4444/data/param_test.rdf" + PARAMS,
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "1",
      maxVersion: "5"
    }],
    name: "Test Addon 4",
  }, dest);

  dest = profileDir.clone();
  dest.append("addon5@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon5@tests.mozilla.org",
    version: "1.0",
    updateURL: "http://localhost:4444/data/param_test.rdf" + PARAMS,
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "1",
      maxVersion: "1"
    }],
    name: "Test Addon 5",
  }, dest);

  dest = profileDir.clone();
  dest.append("addon6@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon6@tests.mozilla.org",
    version: "1.0",
    updateURL: "http://localhost:4444/data/param_test.rdf" + PARAMS,
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "1",
      maxVersion: "1"
    }],
    name: "Test Addon 6",
  }, dest);

  restartManager();

  AddonManager.getAddonByID("addon2@tests.mozilla.org", function(a2) {
    a2.userDisabled = true;
    restartManager();

    testserver.registerPathHandler("/data/param_test.rdf", function(request, response) {
      do_check_neq(request.queryString, "");
      let [req_version, item_id, item_version,
           item_maxappversion, item_status,
           app_id, app_version, current_app_version,
           app_os, app_abi, app_locale, update_type] =
           [decodeURIComponent(a) for each (a in request.queryString.split("/"))];

      do_check_eq(req_version, "2");

      switch(item_id) {
      case "addon1@tests.mozilla.org":
        do_check_eq(item_version, "5.0");
        do_check_eq(item_maxappversion, "2");
        do_check_eq(item_status, "userEnabled");
        do_check_eq(app_version, "1");
        do_check_eq(update_type, "97");
        break;
      case "addon2@tests.mozilla.org":
        do_check_eq(item_version, "67.0.5b1");
        do_check_eq(item_maxappversion, "3");
        do_check_eq(item_status, "userDisabled");
        do_check_eq(app_version, "1");
        do_check_eq(update_type, "49");
        break;
      case "addon3@tests.mozilla.org":
        do_check_eq(item_version, "1.3+");
        do_check_eq(item_maxappversion, "0");
        do_check_eq(item_status, "userEnabled,incompatible");
        do_check_eq(app_version, "1");
        do_check_eq(update_type, "112");
        break;
      case "addon4@tests.mozilla.org":
        do_check_eq(item_version, "0.5ab6");
        do_check_eq(item_maxappversion, "5");
        do_check_eq(item_status, "userEnabled");
        do_check_eq(app_version, "2");
        do_check_eq(update_type, "98");
        break;
      case "addon5@tests.mozilla.org":
        do_check_eq(item_version, "1.0");
        do_check_eq(item_maxappversion, "1");
        do_check_eq(item_status, "userEnabled");
        do_check_eq(app_version, "1");
        do_check_eq(update_type, "35");
        break;
      case "addon6@tests.mozilla.org":
        do_check_eq(item_version, "1.0");
        do_check_eq(item_maxappversion, "1");
        do_check_eq(item_status, "userEnabled");
        do_check_eq(app_version, "1");
        do_check_eq(update_type, "99");
        break;
      default:
        do_throw("Update request for unexpected add-on " + item_id);
      }

      do_check_eq(app_id, "xpcshell@tests.mozilla.org");
      do_check_eq(current_app_version, "1");
      do_check_eq(app_os, "XPCShell");
      do_check_eq(app_abi, "noarch-spidermonkey");
      do_check_eq(app_locale, "en-US");

      request.setStatusLine(null, 500, "Server Error");
    });

    AddonManager.getAddonsByIDs(["addon1@tests.mozilla.org",
                                 "addon2@tests.mozilla.org",
                                 "addon3@tests.mozilla.org",
                                 "addon4@tests.mozilla.org",
                                 "addon5@tests.mozilla.org",
                                 "addon6@tests.mozilla.org"],
                                 function([a1, a2, a3, a4, a5, a6]) {
      let count = 6;

      function run_next_test() {
        a1.uninstall();
        a2.uninstall();
        a3.uninstall();
        a4.uninstall();
        a5.uninstall();
        a6.uninstall();

        restartManager();
        run_test_9();
      }

      let compatListener = {
        onUpdateFinished: function(addon, error) {
          if (--count == 0)
            run_next_test();
        }
      };

      let updateListener = {
        onUpdateAvailable: function(addon, update) {
          // Dummy so the update checker knows we care about new versions
        },

        onUpdateFinished: function(addon, error) {
          if (--count == 0)
            run_next_test();
        }
      };

      a1.findUpdates(updateListener, AddonManager.UPDATE_WHEN_USER_REQUESTED);
      a2.findUpdates(compatListener, AddonManager.UPDATE_WHEN_ADDON_INSTALLED);
      a3.findUpdates(updateListener, AddonManager.UPDATE_WHEN_PERIODIC_UPDATE);
      a4.findUpdates(updateListener, AddonManager.UPDATE_WHEN_NEW_APP_DETECTED, "2");
      a5.findUpdates(compatListener, AddonManager.UPDATE_WHEN_NEW_APP_INSTALLED);
      a6.findUpdates(updateListener, AddonManager.UPDATE_WHEN_NEW_APP_INSTALLED);
    });
  });
}

// Tests that if an install.rdf claims compatibility then the add-on will be
// seen as compatible regardless of what the update.rdf says.
function run_test_9() {
  var dest = profileDir.clone();
  dest.append("addon4@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon4@tests.mozilla.org",
    version: "5.0",
    updateURL: "http://localhost:4444/data/test_update.rdf",
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "0",
      maxVersion: "1"
    }],
    name: "Test Addon 1",
  }, dest);

  restartManager();

  AddonManager.getAddonByID("addon4@tests.mozilla.org", function(a4) {
    do_check_true(a4.isActive);
    do_check_true(a4.isCompatible);

    run_test_10();
  });
}

// Tests that a normal update check won't decrease a targetApplication's
// maxVersion.
function run_test_10() {
  AddonManager.getAddonByID("addon4@tests.mozilla.org", function(a4) {
    a4.findUpdates({
      onUpdateFinished: function(addon) {
        do_check_true(addon.isCompatible);

        run_test_11();
      }
    }, AddonManager.UPDATE_WHEN_PERIODIC_UPDATE);
  });
}

// Tests that an update check for a new application will decrease a
// targetApplication's maxVersion.
function run_test_11() {
  AddonManager.getAddonByID("addon4@tests.mozilla.org", function(a4) {
    a4.findUpdates({
      onUpdateFinished: function(addon) {
        do_check_false(addon.isCompatible);

        run_test_12();
      }
    }, AddonManager.UPDATE_WHEN_NEW_APP_INSTALLED);
  });
}

// Check that the decreased maxVersion applied and disables the add-on
function run_test_12() {
  restartManager();

  AddonManager.getAddonByID("addon4@tests.mozilla.org", function(a4) {
    do_check_false(a4.isActive);
    do_check_false(a4.isCompatible);

    a4.uninstall();
    restartManager();

    run_test_13();
  });
}

// Tests that no compatibility update is passed to the listener when there is
// compatibility info for the current version of the app but not for the
// version of the app that the caller requested an update check for.
function run_test_13() {
  // Not initially compatible but the update check will make it compatible
  dest = profileDir.clone();
  dest.append("addon7@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon7@tests.mozilla.org",
    version: "1.0",
    updateURL: "http://localhost:4444/data/test_update.rdf",
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "0",
      maxVersion: "0"
    }],
    name: "Test Addon 7",
  }, dest);
  restartManager();

  AddonManager.getAddonByID("addon7@tests.mozilla.org", function(a7) {
    do_check_neq(a7, null);
    do_check_false(a7.isActive);
    do_check_false(a7.isCompatible);
    do_check_true(a7.appDisabled);
    do_check_true(a7.isCompatibleWith("0"));

    a7.findUpdates({
      sawUpdate: false,
      onCompatibilityUpdateAvailable: function(addon) {
        do_throw("Should have not have seen compatibility information");
      },

      onUpdateAvailable: function(addon, install) {
        do_throw("Should not have seen an available update");
      },

      onUpdateFinished: function(addon) {
        do_check_true(addon.isCompatible);
        restartManager();
        check_test_13();
      }
    }, AddonManager.UPDATE_WHEN_NEW_APP_DETECTED, "3.0");
  });
}

function check_test_13() {
  AddonManager.getAddonByID("addon7@tests.mozilla.org", function(a7) {
    do_check_neq(a7, null);
    do_check_true(a7.isActive);
    do_check_true(a7.isCompatible);
    do_check_false(a7.appDisabled);

    a7.uninstall();
    restartManager();

    run_test_14();
  });
}

// Test that background update checks doesn't update an add-on that isn't
// allowed to update automatically.
function run_test_14() {
  // Have an add-on there that will be updated so we see some events from it
  var dest = profileDir.clone();
  dest.append("addon1@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon1@tests.mozilla.org",
    version: "1.0",
    updateURL: "http://localhost:4444/data/test_update.rdf",
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "1",
      maxVersion: "1"
    }],
    name: "Test Addon 1",
  }, dest);

  dest = profileDir.clone();
  dest.append("addon8@tests.mozilla.org");
  writeInstallRDFToDir({
    id: "addon8@tests.mozilla.org",
    version: "1.0",
    updateURL: "http://localhost:4444/data/test_update.rdf",
    targetApplications: [{
      id: "xpcshell@tests.mozilla.org",
      minVersion: "1",
      maxVersion: "1"
    }],
    name: "Test Addon 8",
  }, dest);
  restartManager();

  AddonManager.getAddonByID("addon8@tests.mozilla.org", function(a8) {
    a8.applyBackgroundUpdates = false;

    // The background update check will find updates for both add-ons but only
    // proceed to install one of them.
    AddonManager.addInstallListener({
      onNewInstall: function(aInstall) {
        if (aInstall.existingAddon.id != "addon1@tests.mozilla.org" &&
            aInstall.existingAddon.id != "addon8@tests.mozilla.org")
          do_throw("Saw unexpected onNewInstall for " + aInstall.existingAddon.id);
      },

      onDownloadStarted: function(aInstall) {
        do_check_eq(aInstall.existingAddon.id, "addon1@tests.mozilla.org");
      },

      onDownloadEnded: function(aInstall) {
        do_check_eq(aInstall.existingAddon.id, "addon1@tests.mozilla.org");
      },

      onDownloadFailed: function(aInstall) {
        do_throw("Should not have seen onDownloadFailed event");
      },

      onDownloadCancelled: function(aInstall) {
        do_throw("Should not have seen onDownloadCancelled event");
      },

      onInstallStarted: function(aInstall) {
        do_check_eq(aInstall.existingAddon.id, "addon1@tests.mozilla.org");
      },

      onInstallEnded: function(aInstall) {
        do_check_eq(aInstall.existingAddon.id, "addon1@tests.mozilla.org");
        check_test_14(aInstall);
      },

      onInstallFailed: function(aInstall) {
        do_throw("Should not have seen onInstallFailed event");
      },

      onInstallCancelled: function(aInstall) {
        do_throw("Should not have seen onInstallCancelled event");
      },
    });
  
    // Fake a timer event
    gInternalManager.notify(null);
  });
}

function check_test_14(install) {
  do_check_eq(install.existingAddon.pendingUpgrade.install, install);

  restartManager();
  AddonManager.getAddonsByIDs(["addon1@tests.mozilla.org",
                               "addon8@tests.mozilla.org"], function([a1, a8]) {
    do_check_neq(a1, null);
    do_check_eq(a1.version, "2.0");
    a1.uninstall();

    do_check_neq(a8, null);
    do_check_eq(a8.version, "1.0");
    a8.uninstall();

    restartManager();

    end_test();
  });
}
