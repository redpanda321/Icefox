/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// install.rdf size, icon.png size, subfile.txt size
const ADDON_SIZE = 635 + 15 + 26;

// This verifies the functionality of getResourceURI
// There are two cases - with a filename it returns an nsIFileURL to the filename
// and with no parameters, it returns an nsIFileURL to the root of the addon

function run_test() {
  do_test_pending();
  createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "1");

  startupManager();

  AddonManager.getInstallForFile(do_get_addon("test_getresource"), function(aInstall) {
    do_check_true(aInstall.addon.hasResource("install.rdf"));
    do_check_eq(aInstall.addon.getResourceURI().spec, aInstall.sourceURI.spec);

    do_check_true(aInstall.addon.hasResource("icon.png"));
    do_check_eq(aInstall.addon.getResourceURI("icon.png").spec,
                "jar:" + aInstall.sourceURI.spec + "!/icon.png");

    do_check_false(aInstall.addon.hasResource("missing.txt"));

    do_check_true(aInstall.addon.hasResource("subdir/subfile.txt"));
    do_check_eq(aInstall.addon.getResourceURI("subdir/subfile.txt").spec,
                "jar:" + aInstall.sourceURI.spec + "!/subdir/subfile.txt");

    do_check_false(aInstall.addon.hasResource("subdir/missing.txt"));

    do_check_eq(aInstall.addon.size, ADDON_SIZE);

    completeAllInstalls([aInstall], function() {
      restartManager();
      AddonManager.getAddonByID("addon1@tests.mozilla.org", function(a1) {
        do_check_neq(a1, null);

        let addonDir = gProfD.clone();
        addonDir.append("extensions");
        addonDir.append("addon1@tests.mozilla.org");

        let uri = a1.getResourceURI();
        do_check_true(uri instanceof AM_Ci.nsIFileURL);
        do_check_eq(uri.file.path, addonDir.path);

        let file = addonDir.clone();
        file.append("install.rdf");
        do_check_true(a1.hasResource("install.rdf"));
        uri = a1.getResourceURI("install.rdf")
        do_check_true(uri instanceof AM_Ci.nsIFileURL);
        do_check_eq(uri.file.path, file.path);

        file = addonDir.clone();
        file.append("icon.png");
        do_check_true(a1.hasResource("icon.png"));
        uri = a1.getResourceURI("icon.png")
        do_check_true(uri instanceof AM_Ci.nsIFileURL);
        do_check_eq(uri.file.path, file.path);

        do_check_false(a1.hasResource("missing.txt"));

        file = addonDir.clone();
        file.append("subdir");
        file.append("subfile.txt");
        do_check_true(a1.hasResource("subdir/subfile.txt"));
        uri = a1.getResourceURI("subdir/subfile.txt")
        do_check_true(uri instanceof AM_Ci.nsIFileURL);
        do_check_eq(uri.file.path, file.path);

        do_check_false(a1.hasResource("subdir/missing.txt"));

        do_check_eq(a1.size, ADDON_SIZE);

        a1.uninstall();

        restartManager();

        AddonManager.getAddonByID("addon1@tests.mozilla.org", function(newa1) {
          do_check_eq(newa1, null);

          do_test_finished();
        });
      });
    });
  });
}
