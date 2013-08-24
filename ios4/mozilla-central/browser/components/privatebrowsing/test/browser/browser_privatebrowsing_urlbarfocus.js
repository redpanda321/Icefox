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
 * Portions created by the Initial Developer are Copyright (C) 2009
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

// This test makes sure that the URL bar is focused when entering the private browsing mode.

function test() {
  // initialization
  let pb = Cc["@mozilla.org/privatebrowsing;1"].
           getService(Ci.nsIPrivateBrowsingService);

  const TEST_URL = "data:text/plain,test";
  gBrowser.selectedTab = gBrowser.addTab();
  let browser = gBrowser.selectedBrowser;
  browser.addEventListener("load", function() {
    browser.removeEventListener("load", arguments.callee, true);

    // ensure that the URL bar is not focused initially
    browser.focus();
    isnot(document.commandDispatcher.focusedElement, gURLBar.inputField,
      "URL Bar should not be focused before entering the private browsing mode");
    // ensure that the URL bar is not empty initially
    isnot(gURLBar.value, "", "URL Bar should no longer be empty after leaving the private browsing mode");

    // enter private browsing mode
    pb.privateBrowsingEnabled = true;
    browser = gBrowser.selectedBrowser;
    browser.addEventListener("load", function() {
      // setTimeout is needed here because the onload handler of about:privatebrowsing sets the focus
      setTimeout(function() {
        // ensure that the URL bar is focused inside the private browsing mode
        is(document.commandDispatcher.focusedElement, gURLBar.inputField,
          "URL Bar should be focused inside the private browsing mode");
        // ensure that the URL bar is emptied inside the private browsing mode
        is(gURLBar.value, "", "URL Bar should be empty inside the private browsing mode");

        // leave private browsing mode
        pb.privateBrowsingEnabled = false;
        browser = gBrowser.selectedBrowser;
        browser.addEventListener("load", function() {
          // ensure that the URL bar is no longer focused after leaving the private browsing mode
          isnot(document.commandDispatcher.focusedElement, gURLBar.inputField,
            "URL Bar should no longer be focused after leaving the private browsing mode");
          // ensure that the URL bar is no longer empty after leaving the private browsing mode
          isnot(gURLBar.value, "", "URL Bar should no longer be empty after leaving the private browsing mode");

          gBrowser.removeCurrentTab();
          finish();
        }, true);
      }, 0);
    }, true);
  }, true);
  content.location = TEST_URL;

  waitForExplicitFinish();
}
