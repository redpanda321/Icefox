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
 * The Original Code is sessionstore test code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * Paul O’Shannessy <paul@oshannessy.com>
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

function browserWindowsCount() {
  let count = 0;
  let e = Services.wm.getEnumerator("navigator:browser");
  while (e.hasMoreElements()) {
    if (!e.getNext().closed)
      ++count;
  }
  return count;
}

function test() {
  /** Test for Bug 514751 (Wallpaper) **/
  is(browserWindowsCount(), 1, "Only one browser window should be open initially");

  let ss = Cc["@mozilla.org/browser/sessionstore;1"].
           getService(Ci.nsISessionStore);

  waitForExplicitFinish();

  let state = {
    windows: [{
      tabs: [{
        entries: [
          { url: "http://www.mozilla.org/projects/minefield/", title: "Minefield Start Page" },
          {}
        ]
      }]
    }]
  };

  var theWin = openDialog(location, "", "chrome,all,dialog=no");
  theWin.addEventListener("load", function () {
    executeSoon(function () {
      var gotError = false;
      try {
        ss.setWindowState(theWin, JSON.stringify(state), true);
      } catch (e) {
        if (/NS_ERROR_MALFORMED_URI/.test(e))
          gotError = true;
      }
      ok(!gotError, "Didn't get a malformed URI error.");
      theWin.close();
      is(browserWindowsCount(), 1, "Only one browser window should be open eventually");
      finish();
    });
  }, false);
}

