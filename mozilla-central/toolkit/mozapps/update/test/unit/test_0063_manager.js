/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* General Update Manager Tests */

function run_test() {
  do_test_pending();
  do_register_cleanup(end_test);

  logTestInfo("testing removing an active update for a channel that is not" +
              "valid due to switching channels (bug 486275)");
  removeUpdateDirsAndFiles();

  var patches, updates, update;

  patches = getLocalPatchString(null, null, null, null, null, null,
                                STATE_DOWNLOADING);
  updates = getLocalUpdateString(patches, null, null, "version 1.0", "1.0");
  writeUpdatesToXMLFile(getLocalUpdatesXMLString(updates), true);
  writeStatusFile(STATE_DOWNLOADING);

  patches = getLocalPatchString(null, null, null, null, null, null,
                                STATE_FAILED);
  updates = getLocalUpdateString(patches, null, "Existing", "version 3.0",
                                 "3.0", "3.0", null, null, null, null, null,
                                 getString("patchApplyFailure"));
  writeUpdatesToXMLFile(getLocalUpdatesXMLString(updates), false);

  setUpdateChannel("original_channel");

  standardInit();

  do_check_eq(gUpdateManager.updateCount, 1);
  update = gUpdateManager.getUpdateAt(0);
  do_check_eq(update.name, "Existing");

  do_check_eq(gUpdateManager.activeUpdate, null);
  // Verify that the active-update.xml file has had the update from the old
  // channel removed.
  file = getUpdatesXMLFile(true);
  logTestInfo("verifying contents of " + FILE_UPDATE_ACTIVE);
  do_check_eq(readFile(file), getLocalUpdatesXMLString(""));

  do_test_finished();
}

function end_test() {
  cleanUp();
}
