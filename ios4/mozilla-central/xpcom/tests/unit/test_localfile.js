/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et: */
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
 * The Original Code is XPCOM unit tests.
 *
 * The Initial Developer of the Original Code is
 * Jeff Walden <jwalden+code@mit.edu>.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
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
 * ***** END LICENSE BLOCK ***** */

const Cr = Components.results;
const CC = Components.Constructor;
const Ci = Components.interfaces;

const MAX_TIME_DIFFERENCE = 2500;
const MILLIS_PER_DAY      = 1000 * 60 * 60 * 24;

var LocalFile = CC("@mozilla.org/file/local;1", "nsILocalFile", "initWithPath");

function run_test()
{
  test_toplevel_parent_is_null();
  test_normalize_crash_if_media_missing();
  test_file_modification_time();
  test_directory_modification_time();
}

function test_toplevel_parent_is_null()
{
  try
  {
    var lf = new LocalFile("C:\\");

    // not required by API, but a property on which the implementation of
    // parent == null relies for correctness
    do_check_true(lf.path.length == 2);

    do_check_true(lf.parent === null);
  }
  catch (e)
  {
    // not Windows
    do_check_eq(e.result, Cr.NS_ERROR_FILE_UNRECOGNIZED_PATH);
  }
}

function test_normalize_crash_if_media_missing()
{
  const a="a".charCodeAt(0);
  const z="z".charCodeAt(0);
  for (var i = a; i <= z; ++i)
  {
    try
    {
      LocalFile(String.fromCharCode(i)+":.\\test").normalize();
    }
    catch (e)
    {
    }
  }
}

// Tests that changing a file's modification time is possible   
function test_file_modification_time()
{
  var file = do_get_profile();
  file.append("testfile");

  // Should never happen but get rid of it anyway
  if (file.exists())
    file.remove(true);

  var now = Date.now();
  file.create(Ci.nsIFile.NORMAL_FILE_TYPE, 0644);
  do_check_true(file.exists());

  // Modification time may be out by up to 2 seconds on FAT filesystems. Test
  // with a bit of leeway, close enough probably means it is correct.
  var diff = Math.abs(file.lastModifiedTime - now);
  do_check_true(diff < MAX_TIME_DIFFERENCE);

  var yesterday = now - MILLIS_PER_DAY;
  file.lastModifiedTime = yesterday;

  diff = Math.abs(file.lastModifiedTime - yesterday);
  do_check_true(diff < MAX_TIME_DIFFERENCE);

  var tomorrow = now - MILLIS_PER_DAY;
  file.lastModifiedTime = tomorrow;

  diff = Math.abs(file.lastModifiedTime - tomorrow);
  do_check_true(diff < MAX_TIME_DIFFERENCE);

  file.remove(true);
}

// Tests that changing a directory's modification time is possible   
function test_directory_modification_time()
{
  var dir = do_get_profile();
  dir.append("testdir");

  // Should never happen but get rid of it anyway
  if (dir.exists())
    dir.remove(true);

  var now = Date.now();
  dir.create(Ci.nsIFile.DIRECTORY_TYPE, 0755);
  do_check_true(dir.exists());

  // Modification time may be out by up to 2 seconds on FAT filesystems. Test
  // with a bit of leeway, close enough probably means it is correct.
  var diff = Math.abs(dir.lastModifiedTime - now);
  do_check_true(diff < MAX_TIME_DIFFERENCE);

  var yesterday = now - MILLIS_PER_DAY;
  dir.lastModifiedTime = yesterday;

  diff = Math.abs(dir.lastModifiedTime - yesterday);
  do_check_true(diff < MAX_TIME_DIFFERENCE);

  var tomorrow = now - MILLIS_PER_DAY;
  dir.lastModifiedTime = tomorrow;

  diff = Math.abs(dir.lastModifiedTime - tomorrow);
  do_check_true(diff < MAX_TIME_DIFFERENCE);

  dir.remove(true);
}

