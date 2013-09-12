/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* General Update Service Tests */

function run_test() {
  do_test_pending();
  do_register_cleanup(end_test);

  // Verify write access to the custom app dir
  logTestInfo("testing write access to the application directory");
  removeUpdateDirsAndFiles();
  var testFile = getCurrentProcessDir();
  testFile.append("update_write_access_test");
  testFile.create(AUS_Ci.nsIFile.NORMAL_FILE_TYPE, 0644);
  do_check_true(testFile.exists());
  testFile.remove(false);
  do_check_false(testFile.exists());

  standardInit();

  // Check if available updates can be checked for
  logTestInfo("testing nsIApplicationUpdateService:canCheckForUpdates");
  do_check_true(gAUS.canCheckForUpdates);
  // Check if updates can be applied
  logTestInfo("testing nsIApplicationUpdateService:canApplyUpdates");
  do_check_true(gAUS.canApplyUpdates);

  do_test_finished();
}

function end_test() {
  cleanUp();
}
