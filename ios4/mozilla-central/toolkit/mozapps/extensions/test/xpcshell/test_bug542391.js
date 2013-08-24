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
 *
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Dave Townsend <dtownsend@oxymoronical.com>
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
 * the terms of any one of the MPL, the GPL or the LGPL
 *
 * ***** END LICENSE BLOCK *****
 */

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;

const URI_EXTENSION_UPDATE_DIALOG     = "chrome://mozapps/content/extensions/update.xul";
const PREF_EM_DISABLED_ADDONS_LIST    = "extensions.disabledAddons";
const PREF_EM_SHOW_MISMATCH_UI        = "extensions.showMismatchUI";

const profileDir = gProfD.clone();
profileDir.append("extensions");

var gInstallUpdate = false;

// This will be called to show the compatiiblity update dialog.
var WindowWatcher = {
  expected: false,
  arguments: null,

  openWindow: function(parent, url, name, features, arguments) {
    do_check_eq(url, URI_EXTENSION_UPDATE_DIALOG);
    do_check_true(this.expected);
    this.expected = false;
    this.arguments = arguments.QueryInterface(AM_Ci.nsIVariant);

    if (!gInstallUpdate)
      return;

    // Simulate installing an update while in the dialog
    var installed = false;
    installAllFiles([do_get_addon("test_bug542391_3_2")], function() {
      installed = true;
    });

    // The dialog is meant to be opened modally and the install operation can be
    // asynchronous, so we must spin an event loop (like the modal window does)
    // until the install is complete
    let thr = AM_Cc["@mozilla.org/thread-manager;1"].
              getService(AM_Ci.nsIThreadManager).
              mainThread;

    while (!installed)
      thr.processNextEvent(false);
  },

  QueryInterface: function(iid) {
    if (iid.equals(Ci.nsIWindowWatcher)
     || iid.equals(Ci.nsISupports))
      return this;

    throw Cr.NS_ERROR_NO_INTERFACE;
  }
}

var WindowWatcherFactory = {
  createInstance: function createInstance(outer, iid) {
    if (outer != null)
      throw Components.results.NS_ERROR_NO_AGGREGATION;
    return WindowWatcher.QueryInterface(iid);
  }
};

var registrar = Components.manager.QueryInterface(Components.interfaces.nsIComponentRegistrar);
registrar.registerFactory(Components.ID("{1dfeb90a-2193-45d5-9cb8-864928b2af55}"),
                          "Fake Window Watcher",
                          "@mozilla.org/embedcomp/window-watcher;1", WindowWatcherFactory);

function check_state_v1([a1, a2, a3, a4, a5]) {
  do_check_neq(a1, null);
  do_check_false(a1.appDisabled);
  do_check_false(a1.userDisabled);

  do_check_neq(a2, null);
  do_check_false(a2.appDisabled);
  do_check_true(a2.userDisabled);

  do_check_neq(a3, null);
  do_check_false(a3.appDisabled);
  do_check_false(a3.userDisabled);
  do_check_eq(a3.version, "1.0");

  do_check_neq(a4, null);
  do_check_false(a4.appDisabled);
  do_check_true(a4.userDisabled);

  do_check_neq(a5, null);
  do_check_false(a5.appDisabled);
  do_check_false(a5.userDisabled);
}

function check_state_v2([a1, a2, a3, a4, a5]) {
  do_check_neq(a1, null);
  do_check_true(a1.appDisabled);
  do_check_false(a1.userDisabled);

  do_check_neq(a2, null);
  do_check_false(a2.appDisabled);
  do_check_true(a2.userDisabled);

  do_check_neq(a3, null);
  do_check_false(a3.appDisabled);
  do_check_false(a3.userDisabled);
  do_check_eq(a3.version, "1.0");

  do_check_neq(a4, null);
  do_check_false(a4.appDisabled);
  do_check_true(a4.userDisabled);

  do_check_neq(a5, null);
  do_check_false(a5.appDisabled);
  do_check_false(a5.userDisabled);
}

function check_state_v3([a1, a2, a3, a4, a5]) {
  do_check_neq(a1, null);
  do_check_true(a1.appDisabled);
  do_check_false(a1.userDisabled);

  do_check_neq(a2, null);
  do_check_true(a2.appDisabled);
  do_check_true(a2.userDisabled);

  do_check_neq(a3, null);
  do_check_true(a3.appDisabled);
  do_check_false(a3.userDisabled);
  do_check_eq(a3.version, "1.0");

  do_check_neq(a4, null);
  do_check_false(a4.appDisabled);
  do_check_true(a4.userDisabled);

  do_check_neq(a5, null);
  do_check_false(a5.appDisabled);
  do_check_false(a5.userDisabled);
}

function check_state_v3_2([a1, a2, a3, a4, a5]) {
  do_check_neq(a1, null);
  do_check_true(a1.appDisabled);
  do_check_false(a1.userDisabled);

  do_check_neq(a2, null);
  do_check_true(a2.appDisabled);
  do_check_true(a2.userDisabled);

  do_check_neq(a3, null);
  do_check_false(a3.appDisabled);
  do_check_false(a3.userDisabled);
  do_check_eq(a3.version, "2.0");

  do_check_neq(a4, null);
  do_check_false(a4.appDisabled);
  do_check_true(a4.userDisabled);

  do_check_neq(a5, null);
  do_check_false(a5.appDisabled);
  do_check_false(a5.userDisabled);
}

// Install all the test add-ons, disable two of them and "upgrade" the app to
// version 2 which will appDisable one.
function run_test() {
  do_test_pending();
  createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "1");

  Services.prefs.setBoolPref(PREF_EM_SHOW_MISMATCH_UI, true);

  // Add an extension to the profile to make sure the dialog doesn't show up
  // on new profiles
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
  }, dest);

  startupManager();

  dest.remove(true);

  installAllFiles([do_get_addon("test_bug542391_1"),
                   do_get_addon("test_bug542391_2"),
                   do_get_addon("test_bug542391_3_1"),
                   do_get_addon("test_bug542391_4"),
                   do_get_addon("test_bug542391_5")], function() {

    restartManager();

    AddonManager.getAddonsByIDs(["bug542391_2@tests.mozilla.org",
                                 "bug542391_4@tests.mozilla.org"],
                                 function([a2, a4]) {
      a2.userDisabled = true;
      a4.userDisabled = true;
      restartManager();

      AddonManager.getAddonsByIDs(["bug542391_1@tests.mozilla.org",
                                   "bug542391_2@tests.mozilla.org",
                                   "bug542391_3@tests.mozilla.org",
                                   "bug542391_4@tests.mozilla.org",
                                   "bug542391_5@tests.mozilla.org"],
                                   function(addons) {
        check_state_v1(addons);

        WindowWatcher.expected = true;
        restartManager("2");
        do_check_false(WindowWatcher.expected);

        AddonManager.getAddonsByIDs(["bug542391_1@tests.mozilla.org",
                                     "bug542391_2@tests.mozilla.org",
                                     "bug542391_3@tests.mozilla.org",
                                     "bug542391_4@tests.mozilla.org",
                                     "bug542391_5@tests.mozilla.org"],
                                     function(addons) {
          check_state_v2(addons);

          run_test_1();
        });
      });
    });
  });
}

// Upgrade to version 3 which will appDisable two more add-ons. Check that the
// 3 already disabled add-ons were passed to the mismatch dialog.
function run_test_1() {
  WindowWatcher.expected = true;
  restartManager("3");
  do_check_false(WindowWatcher.expected);

  AddonManager.getAddonsByIDs(["bug542391_1@tests.mozilla.org",
                               "bug542391_2@tests.mozilla.org",
                               "bug542391_3@tests.mozilla.org",
                               "bug542391_4@tests.mozilla.org",
                               "bug542391_5@tests.mozilla.org"],
                               function(addons) {
    check_state_v3(addons);

    do_check_eq(WindowWatcher.arguments.length, 3);
    do_check_true(WindowWatcher.arguments.indexOf("bug542391_1@tests.mozilla.org") >= 0);
    do_check_true(WindowWatcher.arguments.indexOf("bug542391_2@tests.mozilla.org") >= 0);
    do_check_true(WindowWatcher.arguments.indexOf("bug542391_4@tests.mozilla.org") >= 0);

    run_test_2();
  });
}

// Downgrade to version 2 which will remove appDisable from two add-ons and
// should pass all 4 previously disabled add-ons.
function run_test_2() {
  WindowWatcher.expected = true;
  restartManager("2");
  do_check_false(WindowWatcher.expected);

  AddonManager.getAddonsByIDs(["bug542391_1@tests.mozilla.org",
                               "bug542391_2@tests.mozilla.org",
                               "bug542391_3@tests.mozilla.org",
                               "bug542391_4@tests.mozilla.org",
                               "bug542391_5@tests.mozilla.org"],
                               function(addons) {
    check_state_v2(addons);

    do_check_eq(WindowWatcher.arguments.length, 4);
    do_check_true(WindowWatcher.arguments.indexOf("bug542391_1@tests.mozilla.org") >= 0);
    do_check_true(WindowWatcher.arguments.indexOf("bug542391_2@tests.mozilla.org") >= 0);
    do_check_true(WindowWatcher.arguments.indexOf("bug542391_3@tests.mozilla.org") >= 0);
    do_check_true(WindowWatcher.arguments.indexOf("bug542391_4@tests.mozilla.org") >= 0);

    run_test_3();
  });
}

// Upgrade to version 3 which will appDisable two more add-ons.
function run_test_3() {
  Services.prefs.setBoolPref(PREF_EM_SHOW_MISMATCH_UI, false);

  restartManager("3");

  AddonManager.getAddonsByIDs(["bug542391_1@tests.mozilla.org",
                               "bug542391_2@tests.mozilla.org",
                               "bug542391_3@tests.mozilla.org",
                               "bug542391_4@tests.mozilla.org",
                               "bug542391_5@tests.mozilla.org"],
                               function(addons) {
    check_state_v3(addons);

    var disabled = [];
    try {
      disabled = Services.prefs.getCharPref(PREF_EM_DISABLED_ADDONS_LIST).split(",");
    }
    catch (e) {}
    do_check_eq(disabled.length, 2);
    do_check_true(disabled.indexOf("bug542391_2@tests.mozilla.org") >= 0);
    do_check_true(disabled.indexOf("bug542391_3@tests.mozilla.org") >= 0);
    Services.prefs.clearUserPref(PREF_EM_DISABLED_ADDONS_LIST);

    run_test_4();
  });
}

// Downgrade to version 2 which will remove appDisable from two add-ons.
function run_test_4() {
  restartManager("2");

  AddonManager.getAddonsByIDs(["bug542391_1@tests.mozilla.org",
                               "bug542391_2@tests.mozilla.org",
                               "bug542391_3@tests.mozilla.org",
                               "bug542391_4@tests.mozilla.org",
                               "bug542391_5@tests.mozilla.org"],
                               function(addons) {
    check_state_v2(addons);

    var disabled = [];
    try {
      disabled = Services.prefs.getCharPref(PREF_EM_DISABLED_ADDONS_LIST).split(",");
    }
    catch (e) {}
    do_check_eq(disabled.length, 0);

    run_test_5();
  });
}

// Upgrade to version 3 which will appDisable two more add-ons. Check that when
// the upgrade dialog updates an add-on no restart is necessary
function run_test_5() {
  Services.prefs.setBoolPref(PREF_EM_SHOW_MISMATCH_UI, true);
  gInstallUpdate = true;

  WindowWatcher.expected = true;
  restartManager("3");
  do_check_false(WindowWatcher.expected);

  AddonManager.getAddonsByIDs(["bug542391_1@tests.mozilla.org",
                               "bug542391_2@tests.mozilla.org",
                               "bug542391_3@tests.mozilla.org",
                               "bug542391_4@tests.mozilla.org",
                               "bug542391_5@tests.mozilla.org"],
                               function(addons) {
    check_state_v3_2(addons);

    do_check_eq(WindowWatcher.arguments.length, 3);
    do_check_true(WindowWatcher.arguments.indexOf("bug542391_1@tests.mozilla.org") >= 0);
    do_check_true(WindowWatcher.arguments.indexOf("bug542391_2@tests.mozilla.org") >= 0);
    do_check_true(WindowWatcher.arguments.indexOf("bug542391_4@tests.mozilla.org") >= 0);

    finish_test();
  });
}

function finish_test() {
  do_test_finished();
}
