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

// This test checks the browser.privatebrowsing.autostart preference.

function do_test() {
  // initialization
  var prefsService = Cc["@mozilla.org/preferences-service;1"].
                     getService(Ci.nsIPrefBranch);
  prefsService.setBoolPref("browser.privatebrowsing.autostart", true);
  do_check_true(prefsService.getBoolPref("browser.privatebrowsing.autostart"));

  var pb = Cc[PRIVATEBROWSING_CONTRACT_ID].
           getService(Ci.nsIPrivateBrowsingService).
           QueryInterface(Ci.nsIObserver);

  // private browsing not auto-started yet
  do_check_false(pb.autoStarted);

  // simulate startup to make the PB service read the prefs
  pb.observe(null, "profile-after-change", "");

  // the private mode should be entered automatically
  do_check_true(pb.privateBrowsingEnabled);

  // private browsing is auto-started
  do_check_true(pb.autoStarted);

  // leave private browsing mode
  pb.privateBrowsingEnabled = false;

  // private browsing not auto-started
  do_check_false(pb.autoStarted);

  // enter private browsing mode again
  pb.privateBrowsingEnabled = true;

  // private browsing is auto-started
  do_check_true(pb.autoStarted);
}
