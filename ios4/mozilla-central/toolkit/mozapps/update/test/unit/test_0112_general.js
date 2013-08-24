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
 * The Original Code is the Application Update Service.
 *
 * The Initial Developer of the Original Code is
 * Robert Strong <robert.bugzilla@gmail.com>.
 *
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Mozilla Foundation <http://www.mozilla.org/>. All Rights Reserved.
 *
 * Contributor(s):
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
 * ***** END LICENSE BLOCK *****
 */

/* General Partial MAR File Patch Apply Failure Test */

var gTestFiles = [
{
  fileName         : "1_exe1.exe",
  destinationDir   : "mar_test/1/",
  originalContents : null,
  compareContents  : null,
  originalFile     : "data/aus-0111_general_ref_image.png",
  compareFile      : "data/aus-0111_general_ref_image.png",
  originalPerms    : 0755,
  comparePerms     : null
}, {
  fileName         : "1_1_image1.png",
  destinationDir   : "mar_test/1/1_1/",
  originalContents : null,
  compareContents  : null,
  originalFile     : "data/aus-0110_general_ref_image.png",
  compareFile      : "data/aus-0110_general_ref_image.png",
  originalPerms    : 0644,
  comparePerms     : null
}, {
  fileName         : "1_1_text1",
  destinationDir   : "mar_test/1/1_1/",
  originalContents : "ShouldNotBeDeleted\n",
  compareContents  : "ShouldNotBeDeleted\n",
  originalFile     : null,
  compareFile      : null,
  originalPerms    : 0644,
  comparePerms     : null
}, {
  fileName         : "1_1_text2",
  destinationDir   : "mar_test/1/1_1/",
  originalContents : "ShouldNotBeDeleted\n",
  compareContents  : "ShouldNotBeDeleted\n",
  originalFile     : null,
  compareFile      : null,
  originalPerms    : 0644,
  comparePerms     : null
}, {
  fileName         : "2_1_text1",
  destinationDir   : "mar_test/2/2_1/",
  originalContents : "ShouldNotBeDeleted\n",
  compareContents  : "ShouldNotBeDeleted\n",
  originalFile     : null,
  compareFile      : null,
  originalPerms    : 0644,
  comparePerms     : null
}];

function run_test() {
  var testFile;
  // The directory the updates will be applied to is the current working
  // directory and not dist/bin.
  var testDir = do_get_file("mar_test", true);
  // The mar files were created with all files in a subdirectory named
  // mar_test... clear it out of the way if it exists and then create it.
  try {
    removeDirRecursive(testDir);
  }
  catch (e) {
    dump("Unable to remove directory\npath: " + testDir.path +
         "\nException: " + e + "\n");
  }
  dump("Testing: successful removal of the directory used to apply the mar file\n");
  do_check_false(testDir.exists());

  // Create the files to test the partial mar's ability to rollback to the
  // original files.
  for (var i = 0; i < gTestFiles.length; i++) {
    var f = gTestFiles[i];
    if (f.originalFile || f.originalContents) {
      testDir = do_get_file(f.destinationDir, true);
      if (!testDir.exists())
        testDir.create(AUS_Ci.nsIFile.DIRECTORY_TYPE, PERMS_DIRECTORY);

      if (f.originalFile) {
        testFile = do_get_file(f.originalFile);
        testFile.copyTo(testDir, f.fileName);
        testFile = do_get_file(f.destinationDir + f.fileName);
      }
      else {
        testFile = do_get_file(f.destinationDir + f.fileName, true);
        writeFile(testFile, f.originalContents);
      }

      // Skip these tests on Windows (includes WinCE) and OS/2 since their
      // implementaions of chmod doesn't really set permissions.
      if (!IS_WIN && !IS_OS2 && f.originalPerms) {
        testFile.permissions = f.originalPerms;
        // Store the actual permissions on the file for reference later after
        // setting the permissions.
        if (!f.comparePerms)
          f.comparePerms = testFile.permissions;
      }
    }
  }

  var binDir = getGREDir();

  // The updater binary file
  var updater = binDir.clone();
  updater.append("updater.app");
  if (!updater.exists()) {
    updater = binDir.clone();
    updater.append("updater.exe");
    if (!updater.exists()) {
      updater = binDir.clone();
      updater.append("updater");
      if (!updater.exists()) {
        do_throw("Unable to find updater binary!");
      }
    }
  }

  // Use a directory outside of dist/bin to lessen the garbage in dist/bin
  var updatesDir = do_get_file("0112_complete_mar", true);
  try {
    // Mac OS X intermittently fails when removing the dir where the updater
    // binary was launched.
    removeDirRecursive(updatesDir);
  }
  catch (e) {
    dump("Unable to remove directory\npath: " + updatesDir.path +
         "\nException: " + e + "\n");
  }

  updatesDir.create(AUS_Ci.nsIFile.DIRECTORY_TYPE, PERMS_DIRECTORY);
  var mar = do_get_file("data/aus-0111_general.mar");
  mar.copyTo(updatesDir, FILE_UPDATE_ARCHIVE);

  // apply the partial mar and check the innards of the files
  var exitValue = runUpdate(updatesDir, updater);
  dump("Testing: updater binary process exitValue for success when applying " +
       "a partial mar\n");
  do_check_eq(exitValue, 0);

  dump("Testing: update.status should be set to STATE_FAILED\n");
  testFile = updatesDir.clone();
  testFile.append(FILE_UPDATE_STATUS);
  // The update status format for a failure is failed: # where # is the error
  // code for the failure.
  do_check_eq(readFile(testFile).split(": ")[0], STATE_FAILED);

  dump("Testing: files should not be modified or deleted when an update " +
       "fails including retention of file permissions\n");
  for (i = 0; i < gTestFiles.length; i++) {
    f = gTestFiles[i];
    testFile = do_get_file(f.destinationDir + f.fileName, true);
    dump("Testing: " + testFile.path + "\n");
    if (f.compareFile || f.compareContents) {
      do_check_true(testFile.exists());

      // Skip these tests on Windows (includes WinCE) and OS/2 since their
      // implementaions of chmod doesn't really set permissions.
      if (!IS_WIN && !IS_OS2 && f.comparePerms) {
        // Check the original permssions are retained on the file.
        if (f.originalPerms)
          dump("original permissions: " + f.originalPerms.toString(8) + "\n");
        dump("compare permissions : " + f.comparePerms.toString(8) + "\n");
        dump("updated permissions : " + testFile.permissions.toString(8) + "\n");
        do_check_eq(testFile.permissions & 0xfff, f.comparePerms & 0xfff);
      }

      if (f.compareFile) {
        do_check_eq(readFileBytes(testFile),
                    readFileBytes(do_get_file(f.compareFile)));
      }
      else {
        do_check_eq(readFileBytes(testFile), f.compareContents);
      }
    }
    else {
      do_check_false(testFile.exists());
    }
  }

  cleanUp();
}
