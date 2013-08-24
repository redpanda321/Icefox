/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 et lcs=trail\:.,tab\:>~ :
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
 * The Original Code is Firefox Browser Test Code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ted Mielczarek <ted.mielczarek@gmail.com> (Original Author)
 *   Marco Bonardo <mak77@bonardo.net>
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

const Ci = Components.interfaces;
const Cc = Components.classes;

const kTestString = "  hello hello  \n  world\nworld  ";

var gTests = [

  { desc: "Urlbar strips newlines and surrounding whitespace",
    element: gURLBar,
    expected: kTestString.replace(/\s*\n\s*/g,'')
  },

  { desc: "Searchbar replaces newlines with spaces",
    element: document.getElementById('searchbar'),
    expected: kTestString.replace('\n',' ','g')
  },

];

// Test for bug 23485 and bug 321000.
// Urlbar should strip newlines,
// search bar should replace newlines with spaces.
function test() {
  waitForExplicitFinish();

  let cbHelper = Cc["@mozilla.org/widget/clipboardhelper;1"].
                 getService(Ci.nsIClipboardHelper);

  // Put a multi-line string in the clipboard.
  // Setting the clipboard value is an async OS operation, so we need to poll
  // the clipboard for valid data before going on.
  waitForClipboard(kTestString, function() { cbHelper.copyString(kTestString); },
                   next_test, finish);
}

function next_test() {
  if (gTests.length)
    test_paste(gTests.shift());
  else
    finish();
}

function test_paste(aCurrentTest) {
  var element = aCurrentTest.element;

  // Register input listener.
  var inputListener = {
    test: aCurrentTest,
    handleEvent: function(event) {
      var element = event.target;
      element.removeEventListener("input", this, false);

      is(element.value, this.test.expected, this.test.desc);

      // Clear the field and go to next test.
      element.value = "";
      setTimeout(next_test, 0);
    }
  }
  element.addEventListener("input", inputListener, false);

  // Focus the window.
  window.focus();
  gBrowser.selectedBrowser.focus();

  // Focus the element and wait for focus event.
  info("About to focus " + element.id);
  element.addEventListener("focus", function() {
    element.removeEventListener("focus", arguments.callee, false);
    executeSoon(function() {
      // Pasting is async because the Accel+V codepath ends up going through
      // DocumentViewerImpl::FireClipboardEvent.
      info("Pasting into " + element.id);
      EventUtils.synthesizeKey("v", { accelKey: true });
    });
  }, false);
  element.focus();
}
