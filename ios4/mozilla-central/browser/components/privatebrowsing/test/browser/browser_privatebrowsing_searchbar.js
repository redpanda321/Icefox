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
 * The Original Code is Private Browsing Tests.
 *
 * The Initial Developer of the Original Code is
 * Ehsan Akhgari.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ehsan Akhgari <ehsan.akhgari@gmail.com> (Original Author)
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

// This test makes sure that the search bar is cleared when leaving the
// private browsing mode.

function test() {
  // initialization
  waitForExplicitFinish();
  gPrefService.setBoolPref("browser.privatebrowsing.keep_current_session", true);
  let pb = Cc["@mozilla.org/privatebrowsing;1"].
           getService(Ci.nsIPrivateBrowsingService);

  // fill in the search bar with something
  const kTestSearchString = "privatebrowsing";
  let searchBar = BrowserSearch.searchBar;
  searchBar.value = kTestSearchString;

  // enter private browsing mode
  pb.privateBrowsingEnabled = true;

  is(searchBar.value, kTestSearchString,
    "entering the private browsing mode should not clear the search bar");
  ok(searchBar.textbox.editor.transactionManager.numberOfUndoItems > 0,
    "entering the private browsing mode should not reset the undo list of the searchbar control");

  // Change the search bar value inside the private browsing mode
  searchBar.value = "something else";

  // leave private browsing mode
  pb.privateBrowsingEnabled = false;

  is(searchBar.value, kTestSearchString,
    "leaving the private browsing mode should restore the search bar contents");
  is(searchBar.textbox.editor.transactionManager.numberOfUndoItems, 1,
    "leaving the private browsing mode should only leave 1 item in the undo list of the searchbar control");

  // enter private browsing mode
  pb.privateBrowsingEnabled = true;

  const TEST_URL =
    "data:text/html,<head><link rel=search type='application/opensearchdescription+xml' href='http://foo.bar' title=dummy></head>";
  gBrowser.selectedTab = gBrowser.addTab(TEST_URL);
  gBrowser.selectedBrowser.addEventListener("load", function(e) {
    e.currentTarget.removeEventListener("load", arguments.callee, true);

    var browser = gBrowser.selectedBrowser;
    is(typeof browser.engines, "undefined",
       "An engine should not be discovered in private browsing mode");

    gBrowser.removeTab(gBrowser.selectedTab);
    pb.privateBrowsingEnabled = false;

    // cleanup
    gPrefService.clearUserPref("browser.privatebrowsing.keep_current_session");
    finish();
  }, true);
}
