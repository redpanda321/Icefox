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
 * The Original Code is Places Test Code.
 *
 * The Initial Developer of the Original Code is
 * Edward Lee <edward.lee@engineering.uiuc.edu>.
 * Portions created by the Initial Developer are Copyright (C) 2008
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

/**
 * Test bug 418257 by making sure tags are returned with the title as part of
 * the "comment" if there are tags even if we didn't match in the tags. They
 * are separated from the title by a endash.
 */

// Define some shared uris and titles (each page needs its own uri)
let kURIs = [
  "http://page1",
  "http://page2",
  "http://page3",
  "http://page4",
];
let kTitles = [
  "tag1",
  "tag2",
  "tag3",
];

// Add pages with varying number of tags
addPageBook(0, 0, 0, [0]);
addPageBook(1, 0, 0, [0,1]);
addPageBook(2, 0, 0, [0,2]);
addPageBook(3, 0, 0, [0,1,2]);

// Provide for each test: description; search terms; array of gPages indices of
// pages that should match; optional function to be run before the test
let gTests = [
  ["0: Make sure tags come back in the title when matching tags",
   "page1 tag", [0]],
  ["1: Check tags in title for page2",
   "page2 tag", [1]],
  ["2: Make sure tags appear even when not matching the tag",
   "page3", [2]],
  ["3: Multiple tags come in commas for page4",
   "page4", [3]],
  ["4: Extra test just to make sure we match the title",
   "tag2", [1,3]],
];
