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
 * The Original Code is Mozmill Test Code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Aakash Desai <adesai@mozilla.com>
 *   Henrik Skupin <hskupin@mozilla.com>
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

// Include necessary modules
var RELATIVE_ROOT = '../../shared-modules';
var MODULE_REQUIRES = ['PrefsAPI', 'TabbedBrowsingAPI'];

const gDelay = 0;
const gTimeout = 5000;

const homepage = 'http://www.mozilla.org/';

var setupModule = function(module) {
  module.controller = mozmill.getBrowserController();

  TabbedBrowsingAPI.closeAllTabs(controller);
}

var teardownModule = function(module) {
  PrefsAPI.preferences.clearUserPref("browser.startup.homepage");
}

/**
 * Set homepage to current page
 */
var testSetHomePage = function() {
  // Go to the Mozilla.org website and verify the correct page has loaded
  controller.open(homepage);
  controller.waitForPageLoad();

  var link = new elementslib.Link(controller.tabs.activeTab, "Mozilla");
  controller.assertNode(link);

  // Call Prefs Dialog and set Home Page
  PrefsAPI.openPreferencesDialog(prefDialogHomePageCallback);
}

/**
 * Test the home button
 */
var testHomeButton = function()
{
  // Open another page before going to the home page
  controller.open('http://www.yahoo.com/');
  controller.waitForPageLoad();

  // Go to the saved home page and verify it's the correct page
  controller.click(new elementslib.ID(controller.window.document, "home-button"));
  controller.waitForPageLoad();

  // Verify location bar with the saved home page
  var locationBar = new elementslib.ID(controller.window.document, "urlbar");
  controller.assertValue(locationBar, homepage);
}

/**
 * Set the current page as home page
 *
 * @param {MozMillController} controller
 *        MozMillController of the window to operate on
 */
var prefDialogHomePageCallback = function(controller) {
  var prefDialog = new PrefsAPI.preferencesDialog(controller);
  prefDialog.paneId = 'paneMain';

  // Set Home Page to the current page
  var useCurrent = new elementslib.ID(controller.window.document, "useCurrent");
  controller.click(useCurrent);

  prefDialog.close(true);
}

/**
 * Map test functions to litmus tests
 */
// testSetHomePage.meta = {litmusids : [7964]};
// testHomeButton.meta = {litmusids : [8031]};
