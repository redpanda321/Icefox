/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   John Bandhauer <jband@netscape.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

/* Windows only app to show a modal debug dialog - launched by nsDebug.cpp */
#include <windows.h>
#include <stdlib.h>
#ifdef _MSC_VER
#include <strsafe.h>
#endif
#ifdef __MINGW32__
/* MingW currently does not implement a wide version of the
   startup routines.  Workaround is to implement something like
   it ourselves.  See bug 472063 */
#include <stdio.h>
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int);

#undef __argc
#undef __wargv

static int __argc;
static wchar_t** __wargv;

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
        LPSTR lpszCommandLine, int nCmdShow)
{
  LPWSTR commandLine = GetCommandLineW();

  /* parse for __argc and __wargv for compatibility, since mingw
   * doesn't claim to support it :(
   */
  __wargv = CommandLineToArgvW(commandLine, &__argc);
  if (!__wargv)
    return 127;

  /* need to strip off any leading whitespace plus the first argument
   * (the executable itself) to match what should be passed to wWinMain
   */
  while ((*commandLine <= L' ') && *commandLine) {
    ++commandLine;
  }
  if (*commandLine == L'"') {
    ++commandLine;
    while ((*commandLine != L'"') && *commandLine) {
      ++commandLine;
    }
    if (*commandLine) {
      ++commandLine;
    }
  } else {
    while (*commandLine > L' ') {
      ++commandLine;
    }
  }
  while ((*commandLine <= L' ') && *commandLine) {
    ++commandLine;
  }

  int result = wWinMain(hInstance, hPrevInstance, commandLine, nCmdShow);
  LocalFree(__wargv);
  return result;
}
#endif /* __MINGW32__ */


int WINAPI
wWinMain(HINSTANCE  hInstance, HINSTANCE  hPrevInstance,
         LPWSTR  lpszCmdLine, int  nCmdShow)
{
    /* support for auto answering based on words in the assertion.
     * the assertion message is sent as a series of arguements (words) to the commandline.
     * set a "word" to 0xffffffff to let the word not affect this code.
     * set a "word" to 0xfffffffe to show the dialog.
     * set a "word" to 0x5 to ignore (program should continue).
     * set a "word" to 0x4 to retry (should fall into debugger).
     * set a "word" to 0x3 to abort (die).
     */
    DWORD regType;
    DWORD regValue = -1;
    DWORD regLength = sizeof regValue;
    HKEY hkeyCU, hkeyLM;
    RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\mozilla.org\\windbgdlg", 0, KEY_READ, &hkeyCU);
    RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\mozilla.org\\windbgdlg", 0, KEY_READ, &hkeyLM);
    int argc =0;
    for (int i = __argc - 1; regValue == (DWORD)-1 && i; --i) {
        bool ok = false;
        if (hkeyCU)
            ok = RegQueryValueExW(hkeyCU, __wargv[i], 0, &regType, (LPBYTE)&regValue, &regLength) == ERROR_SUCCESS;
        if (!ok && hkeyLM)
            ok = RegQueryValueExW(hkeyLM, __wargv[i], 0, &regType, (LPBYTE)&regValue, &regLength) == ERROR_SUCCESS;
        if (!ok)
            regValue = -1;
    }
    if (hkeyCU)
        RegCloseKey(hkeyCU);
    if (hkeyLM)
        RegCloseKey(hkeyLM);
    if (regValue != (DWORD)-1 && regValue != (DWORD)-2)
        return regValue;
    static const int size = 4096;
    static WCHAR msg[size];

#ifdef _MSC_VER
    StringCchPrintfW(msg,
#else
    snwprintf(msg,
#endif
              size,
              L"%s\n\nClick Abort to exit the Application.\n"
              L"Click Retry to Debug the Application.\n"
              L"Click Ignore to continue running the Application.",
              lpszCmdLine);
    msg[size - 1] = L'\0';
    return MessageBoxW(NULL, msg, L"NSGlue_Assertion",
                       MB_ICONSTOP | MB_SYSTEMMODAL |
                       MB_ABORTRETRYIGNORE | MB_DEFBUTTON3);
}
