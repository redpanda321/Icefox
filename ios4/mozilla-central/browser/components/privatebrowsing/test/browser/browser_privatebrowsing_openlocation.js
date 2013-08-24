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

// This test makes sure that Open Location dialog is usable inside the private browsing
// mode without leaving any trace of the URLs visited.

function test() {
  // initialization
  gPrefService.setBoolPref("browser.privatebrowsing.keep_current_session", true);
  let pb = Cc["@mozilla.org/privatebrowsing;1"].
           getService(Ci.nsIPrivateBrowsingService);
  waitForExplicitFinish();

  function openLocation(url, autofilled, callback) {
    function observer(aSubject, aTopic, aData) {
      switch (aTopic) {
        case "domwindowopened":
          let dialog = aSubject.QueryInterface(Ci.nsIDOMWindow);
          dialog.addEventListener("load", function () {
            dialog.removeEventListener("load", arguments.callee, false);

            let browser = gBrowser.selectedBrowser;
            browser.addEventListener("load", function() {
              browser.removeEventListener("load", arguments.callee, true);

              is(browser.currentURI.spec, url,
                 "The correct URL should be loaded via the open location dialog");
              executeSoon(callback);
            }, true);

            SimpleTest.waitForFocus(function() {
              let input = dialog.document.getElementById("dialog.input");
              is(input.value, autofilled, "The input field should be correctly auto-filled");
              input.focus();
              for (let i = 0; i < url.length; ++i)
                EventUtils.synthesizeKey(url[i], {}, dialog);
              EventUtils.synthesizeKey("VK_RETURN", {}, dialog);
            }, dialog);
          }, false);
          break;

        case "domwindowclosed":
          Services.ww.unregisterNotification(arguments.callee);
          break;
      }
    }

    Services.ww.registerNotification(observer);
    gPrefService.setIntPref("general.open_location.last_window_choice", 0);
    openDialog("chrome://browser/content/openLocation.xul", "_blank",
               "chrome,titlebar", window);
  }


  if (gPrefService.prefHasUserValue("general.open_location.last_url"))
    gPrefService.clearUserPref("general.open_location.last_url");

  openLocation("http://example.com/", "", function() {
    openLocation("http://example.org/", "http://example.com/", function() {
      // enter private browsing mode
      pb.privateBrowsingEnabled = true;
      openLocation("about:logo", "", function() {
        openLocation("about:buildconfig", "about:logo", function() {
          // exit private browsing mode
          pb.privateBrowsingEnabled = false;
          openLocation("about:blank", "http://example.org/", function() {
            gPrefService.clearUserPref("general.open_location.last_url");
            if (gPrefService.prefHasUserValue("general.open_location.last_window_choice"))
              gPrefService.clearUserPref("general.open_location.last_window_choice");
            gPrefService.clearUserPref("browser.privatebrowsing.keep_current_session");
            finish();
          });
        });
      });
    });
  });
}
