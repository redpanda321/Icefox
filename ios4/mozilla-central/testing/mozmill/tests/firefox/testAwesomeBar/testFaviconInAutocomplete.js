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
 * The Original Code is MozMill Test code.
 *
 * The Initial Developer of the Original Code is the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Tracy Walker <twalker@mozilla.com>
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
 * ***** END LICENSE BLOCK *****/

// Include necessary modules
var RELATIVE_ROOT = '../../shared-modules';
var MODULE_REQUIRES = ['PlacesAPI', 'PrefsAPI', 'ToolbarAPI'];

const gTimeout = 5000;
const gDelay = 200;

const testSite = {url : 'http://www.google.com/', string: 'google'};

var setupModule = function(module)
{
  module.controller = mozmill.getBrowserController();
  module.locationBar =  new ToolbarAPI.locationBar(controller);

  // Clear complete history so we don't get interference from previous entries
  try {
    PlacesAPI.historyService.removeAllPages();
  } catch (ex) {}
}

/**
 * Check Favicon in autocomplete list
 *
 */
var testFaviconInAutoComplete = function()
{
  // Use preferences dialog to select "When Using the location bar suggest:" "History"
  PrefsAPI.openPreferencesDialog(prefDialogSuggestsCallback);

  // Open the test page
  locationBar.loadURL(testSite.url);
  controller.waitForPageLoad();

  // Get the location bar favicon element URL.
  var locationBarFaviconUrl = locationBar.getElement({type:"favicon"}).getNode().getAttribute('src');

  // wait for 4 seconds to work around Firefox LAZY ADD of items to the db
  controller.sleep(4000);

  // Focus the locationbar, delete any contents there
  locationBar.clear();

  // Type in each letter of the test string to allow the autocomplete to populate with results.
  for each (letter in testSite.string) {
    locationBar.type(letter);
    controller.sleep(gDelay);
  }

  // defines the path to the first auto-complete result
  var richlistItem = locationBar.autoCompleteResults.getResult(0);

  // Ensure the autocomplete list is open
  controller.waitForEval('subject.isOpened == true', 3000, 100, locationBar.autoCompleteResults);

  // Get url for the autocomplete favicon for the matched entry
  var listFaviconUrl = richlistItem.getNode().boxObject.firstChild.childNodes[0].getAttribute('src');

  // Check that both favicons have the same URL
  controller.assertJS("subject.isSameFavicon == true",
                      {isSameFavicon: richlistItem.getNode().image.indexOf(locationBarFaviconUrl) != -1});
}

/**
 * Set suggests in the location bar to "History"
 *
 * @param {MozMillController} controller
 *        MozMillController of the window to operate on
 */
var prefDialogSuggestsCallback = function(controller)
{
  var prefDialog = new PrefsAPI.preferencesDialog(controller);
  prefDialog.paneId = 'panePrivacy';

  var suggests = new elementslib.ID(controller.window.document, "locationBarSuggestion");
  controller.waitForElement(suggests);
  controller.select(suggests, null, null, 1);
  controller.sleep(gDelay);

  prefDialog.close(true);
}
