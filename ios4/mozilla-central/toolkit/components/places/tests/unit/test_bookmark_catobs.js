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
 * The Original Code is Places unit test code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mark Finkle <mfinkle@mozilla.com> (Original Author)
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

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

// Get services.
let bs = Cc["@mozilla.org/browser/nav-bookmarks-service;1"].
         getService(Ci.nsINavBookmarksService);
let os = Cc["@mozilla.org/observer-service;1"].
         getService(Ci.nsIObserverService);

let gDummyCreated = false;
let gDummyAdded = false;

let observer = {
  observe: function(subject, topic, data) {
    if (topic == "dummy-observer-created")
      gDummyCreated = true;
    else if (topic == "dummy-observer-item-added")
      gDummyAdded = true;
  },

  QueryInterface: XPCOMUtils.generateQI([
    Ci.nsIObserver,
    Ci.nsISupportsWeakReference,
  ])
};

function verify() {
  do_check_true(gDummyCreated);
  do_check_true(gDummyAdded);
  do_test_finished();
}

// main
function run_test() {
  do_load_manifest("nsDummyObserver.manifest");

  os.addObserver(observer, "dummy-observer-created", true);
  os.addObserver(observer, "dummy-observer-item-added", true);

  // Add a bookmark
  bs.insertBookmark(bs.unfiledBookmarksFolder, uri("http://typed.mozilla.org"),
                    bs.DEFAULT_INDEX, "bookmark");

  do_test_pending();
  do_timeout(1000, verify);
}
