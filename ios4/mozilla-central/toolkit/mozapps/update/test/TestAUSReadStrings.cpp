/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * The Initial Developer of the Original Code is Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Robert Strong <robert.bugzilla@gmail.com>
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

/**
 * This binary tests the updater's ReadStrings ini parser and should run in a
 * directory with a Unicode character to test bug 473417.
 */
#ifdef XP_WIN
  #include <windows.h>
  #define NS_main wmain
  #define NS_tstrrchr wcsrchr
  #define NS_tsnprintf _snwprintf
  #define NS_T(str) L ## str
  #define PATH_SEPARATOR_CHAR L'\\'
#else
  #include <unistd.h>
  #define NS_main main
  #define NS_tstrrchr strrchr
  #define NS_tsnprintf snprintf
  #define NS_T(str) str
#ifdef XP_OS2
  #define PATH_SEPARATOR_CHAR '\\'
#else
  #define PATH_SEPARATOR_CHAR '/'
#endif
#endif

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "updater/resource.h"
#include "updater/progressui.h"
#include "../../readstrings/readstrings.h"
#include "../../readstrings/errors.h"

#ifndef MAXPATHLEN
# ifdef PATH_MAX
#  define MAXPATHLEN PATH_MAX
# elif defined(MAX_PATH)
#  define MAXPATHLEN MAX_PATH
# elif defined(_MAX_PATH)
#  define MAXPATHLEN _MAX_PATH
# elif defined(CCHMAXPATH)
#  define MAXPATHLEN CCHMAXPATH
# else
#  define MAXPATHLEN 1024
# endif
#endif

#define TEST_NAME "Updater ReadStrings"

static int gFailCount = 0;

/**
 * Prints the given failure message and arguments using printf, prepending
 * "TEST-UNEXPECTED-FAIL " for the benefit of the test harness and
 * appending "\n" to eliminate having to type it at each call site.
 */
void fail(const char* msg, ...)
{
  va_list ap;

  printf("TEST-UNEXPECTED-FAIL | ");

  va_start(ap, msg);
  vprintf(msg, ap);
  va_end(ap);

  putchar('\n');
  ++gFailCount;
}

/**
 * Prints the given string prepending "TEST-PASS | " for the benefit of
 * the test harness and with "\n" at the end, to be used at the end of a
 * successful test function.
 */
void passed(const char* test)
{
  printf("TEST-PASS | %s\n", test);
}

int NS_main(int argc, NS_tchar **argv)
{
  printf("Running TestAUSReadStrings tests\n");

  int rv = 0;
  int retval;
  NS_tchar inifile[MAXPATHLEN];
  StringTable testStrings;

  NS_tchar *slash = NS_tstrrchr(argv[0], PATH_SEPARATOR_CHAR);
  if (!slash) {
    fail("%s | unable to find platform specific path separator (check 1)", TEST_NAME);
    return 20;
  }

  *(++slash) = '\0';
  // Test success when the ini file exists with both Title and Info in the
  // Strings section and the values for Title and Info.
  NS_tsnprintf(inifile, sizeof(inifile), NS_T("%sTestAUSReadStrings1.ini"), argv[0]);
  retval = ReadStrings(inifile, &testStrings);
  if (retval == OK) {
    if (strcmp(testStrings.title, "Title Test - \xD0\x98\xD1\x81\xD0\xBF\xD1\x8B" \
                                  "\xD1\x82\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5 " \
                                  "\xCE\x94\xCE\xBF\xCE\xBA\xCE\xB9\xCE\xBC\xCE\xAE " \
                                  "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88 " \
                                  "\xE6\xB8\xAC\xE8\xA9\xA6 " \
                                  "\xE6\xB5\x8B\xE8\xAF\x95") != 0) {
      rv = 21;
      fail("%s | Title ini value incorrect (check 3)", TEST_NAME);
    }

    if (strcmp(testStrings.info, "Info Test - \xD0\x98\xD1\x81\xD0\xBF\xD1\x8B" \
                                 "\xD1\x82\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5 " \
                                 "\xCE\x94\xCE\xBF\xCE\xBA\xCE\xB9\xCE\xBC\xCE\xAE " \
                                 "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88 " \
                                 "\xE6\xB8\xAC\xE8\xA9\xA6 " \
                                 "\xE6\xB5\x8B\xE8\xAF\x95\xE2\x80\xA6") != 0) {
      rv = 22;
      fail("%s | Info ini value incorrect (check 4)", TEST_NAME);
    }
  }
  else {
    fail("%s | ReadStrings returned %i (check 2)", TEST_NAME, retval);
    rv = 23;
  }

  // Test failure when the ini file exists without Title and with Info in the
  // Strings section.
  NS_tsnprintf(inifile, sizeof(inifile), NS_T("%sTestAUSReadStrings2.ini"), argv[0]);
  retval = ReadStrings(inifile, &testStrings);
  if (retval != PARSE_ERROR) {
    rv = 24;
    fail("%s | ReadStrings returned %i (check 5)", TEST_NAME, retval);
  }

  // Test failure when the ini file exists with Title and without Info in the
  // Strings section.
  NS_tsnprintf(inifile, sizeof(inifile), NS_T("%sTestAUSReadStrings3.ini"), argv[0]);
  retval = ReadStrings(inifile, &testStrings);
  if (retval != PARSE_ERROR) {
    rv = 25;
    fail("%s | ReadStrings returned %i (check 6)", TEST_NAME, retval);
  }

  // Test failure when the ini file doesn't exist
  NS_tsnprintf(inifile, sizeof(inifile), NS_T("%sTestAUSReadStringsBogus.ini"), argv[0]);
  retval = ReadStrings(inifile, &testStrings);
  if (retval != READ_ERROR) {
    rv = 26;
    fail("%s | ini file doesn't exist (check 7)", TEST_NAME);
  }

  // Test reading a non-default section name
  NS_tsnprintf(inifile, sizeof(inifile), NS_T("%sTestAUSReadStrings3.ini"), argv[0]);
  retval = ReadStrings(inifile, "Title\0", 1, &testStrings.title, "BogusSection2");
  if (retval == OK) {
    if (strcmp(testStrings.title, "Bogus Title") != 0) {
      rv = 27;
      fail("%s | Title ini value incorrect (check 9)", TEST_NAME);
    }
  }
  else {
    fail("%s | ReadStrings returned %i (check 8)", TEST_NAME, retval);
    rv = 28;
  }


  if (rv == 0) {
    printf("TEST-PASS | %s | all checks passed\n", TEST_NAME);
  } else {
    fail("%s | %i out of 9 checks failed", TEST_NAME, gFailCount);
  }

  return rv;
}
