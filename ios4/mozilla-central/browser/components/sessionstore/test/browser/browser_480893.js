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
 * The Initial Developer of the Original Code is
 * Michael Kohler <michaelkohler@live.com>.
 * Portions created by the Initial Developer are Copyright (C) 2009
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

function test() {
  /** Test for Bug 480893 **/

  waitForExplicitFinish();

  // Test that starting a new session loads a blank page if Firefox is
  // configured to display a blank page at startup (browser.startup.page = 0)
  gPrefService.setIntPref("browser.startup.page", 0);
  let tab = gBrowser.addTab("about:sessionrestore");
  gBrowser.selectedTab = tab;
  let browser = tab.linkedBrowser;
  browser.addEventListener("load", function(aEvent) {
    browser.removeEventListener("load", arguments.callee, true);
    let doc = browser.contentDocument;

    // click on the "Start New Session" button after about:sessionrestore is loaded
    doc.getElementById("errorCancel").click();
    browser.addEventListener("load", function(aEvent) {
      browser.removeEventListener("load", arguments.callee, true);
      let doc = browser.contentDocument;

      is(doc.URL, "about:blank", "loaded page is about:blank");

      // Test that starting a new session loads the homepage (set to http://mochi.test:8888)
      // if Firefox is configured to display a homepage at startup (browser.startup.page = 1)
      let homepage = "http://mochi.test:8888/";
      gPrefService.setCharPref("browser.startup.homepage", homepage);
      gPrefService.setIntPref("browser.startup.page", 1);
      gBrowser.loadURI("about:sessionrestore");
      browser.addEventListener("load", function(aEvent) {
        browser.removeEventListener("load", arguments.callee, true);
        let doc = browser.contentDocument;

        // click on the "Start New Session" button after about:sessionrestore is loaded
        doc.getElementById("errorCancel").click();
        browser.addEventListener("load", function(aEvent) {
          browser.removeEventListener("load", arguments.callee, true);
          let doc = browser.contentDocument;

          is(doc.URL, homepage, "loaded page is the homepage");

          // close tab, restore default values and finish the test
          gBrowser.removeTab(tab);
          // we need this if-statement because if there is no user set value, 
          // clearUserPref throws a uncatched exception and finish is not called
          if (gPrefService.prefHasUserValue("browser.startup.page"))
            gPrefService.clearUserPref("browser.startup.page");
          gPrefService.clearUserPref("browser.startup.homepage");
          finish();
        }, true);
      }, true);
    }, true);
  }, true);
}
