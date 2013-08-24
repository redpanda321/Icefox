/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * Test Definition
 *
 * Most tests can use an array named TESTS that will perform most if not all of
 * the necessary checks. Each element in the array must be an object with the
 * following possible properties. Additional properties besides the ones listed
 * below can be added as needed.
 *
 * overrideCallback (optional)
 *   The function to call for the next test. This is typically called when the
 *   wizard page changes but can also be called for other events by the previous
 *   test. If this property isn't defined then the defailtCallback function will
 *   be called. If this property is defined then all other properties are
 *   optional.
 *
 * pageid (required unless overrideCallback is specified)
 *   The expected pageid for the wizard. This property is required unless the
 *   overrideCallback property is defined.
 *
 * extraStartFunction (optional)
 *   The function to call at the beginning of the defaultCallback function. If
 *   the function returns true the defaultCallback function will return early
 *   which allows waiting for a specific condition to be evaluated in the
 *   function specified in the extraStartFunction property before continuing
 *   with the test.
 *
 * extraCheckFunction (optional)
 *   The function to call to perform extra checks in the defaultCallback
 *   function.
 *
 * extraDelayedCheckFunction (optional)
 *   The function to call to perform extra checks in the delayedDefaultCallback
 *   function.
 *
 * buttonStates (optional)
 *   A javascript object representing the expected hidden and disabled attribute
 *   values for the buttons of the current wizard page. The values are checked
 *   in the delayedDefaultCallback function. For information about the structure
 *   of this object refer to the getExpectedButtonStates and checkButtonStates
 *   functions.
 *
 * buttonClick (optional)
 *   The current wizard page button to click at the end of the
 *   delayedDefaultCallback function. If the buttonClick property is defined
 *   then the extraDelayedFinishFunction property can't be specified due to race
 *   conditions in some of the tests and if both of them are specified the test
 *   will intentionally throw.
 *
 * extraDelayedFinishFunction (optional)
 *   The function to call at the end of the delayedDefaultCallback function.
 *
 * ranTest (should not be specified)
 *   When delayedDefaultCallback is called a property named ranTest is added to
 *   the current test it is possible to verify that each test in the TESTS
 *   array has ran.
 *
 * prefHasUserValue (optional)
 *   For comparing the expected value defined by this property with the return
 *   value of prefHasUserValue using gPrefToCheck for the preference name in the
 *   checkPrefHasUserValue function.
 *
 * expectedRadioGroupSelectedIndex (optional)
 *   For comparing the expected selectedIndex attribute value of the wizard's
 *   license page radiogroup selectedIndex attribute in the
 *   checkRadioGroupSelectedIndex function.
 *
 * expectedRemoteContentState (optional)
 *   For comparing the expected remotecontent state attribute value of the
 *   wizard's billboard and license pages in the checkRemoteContentState and
 *   waitForRemoteContentLoaded functions.
 *
 *
 * Test Add-ons
 *
 * All tests include the test add-ons specified in the TEST_ADDONS array and
 * the only thing that can be configured is whether the noupdate test add-on is
 * disabled (see below). The add-on names are in the format of typename_X where
 * X is a number to make the add-on ID unique and typename is one of the values
 * specified below:
 *
 * appdisabled
 *   disabled by the application due to being incompatible with the current
 *   toolkit version.
 *
 * compatible
 *   compatible with the current toolkit version and the update's toolkit
 *   version.
 *
 * noupdate
 *   the add-on is compatible with the current toolkit version and does not have
 *   an update to make it compatible with the update's toolkit version. Tests
 *   that need to have all add-ons compatible for the application update can
 *   disable this add-on by setting the gDisableNoUpdateAddon variable to true.
 *
 * updatecompatibility
 *   the add-on is compatible with the current toolkit version and has a
 *   compatibility update to make it compatible with the update's toolkit
 *   version.
 *
 * updateversion
 *   the add-on is compatible with the current toolkit version and has a version
 *   update to make it compatible with the update's toolkit version.
 *
 * userdisabled
 *   disabled by the user and compatible with the current toolkit version but
 *   not the update's toolkit version. This add-on will be disabled after its
 *   install completes.
 */

Components.utils.import("resource://gre/modules/AddonManager.jsm");

// The tests have to use the pageid instead of the pageIndex due to the
// app update wizard's access method being random.
const PAGEID_DUMMY            = "dummy";                 // Done
const PAGEID_CHECKING         = "checking";              // Done
const PAGEID_PLUGIN_UPDATES   = "pluginupdatesfound";
const PAGEID_NO_UPDATES_FOUND = "noupdatesfound";        // Done
const PAGEID_MANUAL_UPDATE    = "manualUpdate"; // Tested on license load failure
const PAGEID_INCOMPAT_CHECK   = "incompatibleCheck";     // Done
const PAGEID_FOUND_BASIC      = "updatesfoundbasic";     // Done
const PAGEID_FOUND_BILLBOARD  = "updatesfoundbillboard"; // Done
const PAGEID_LICENSE          = "license";               // Done
const PAGEID_INCOMPAT_LIST    = "incompatibleList";      // Done
const PAGEID_DOWNLOADING      = "downloading";           // Done
const PAGEID_ERRORS           = "errors";                // Done
const PAGEID_ERROR_PATCHING   = "errorpatching";         // Done
const PAGEID_FINISHED         = "finished";              // Done
const PAGEID_FINISHED_BKGRD   = "finishedBackground";    // Done
const PAGEID_INSTALLED        = "installed";             // Done

const UPDATE_WINDOW_NAME = "Update:Wizard";

const URL_HOST   = "http://example.com/";
const URL_PATH   = "chrome/toolkit/mozapps/update/test/chrome";
const URL_UPDATE = URL_HOST + URL_PATH + "/update.sjs";

const URI_UPDATE_PROMPT_DIALOG  = "chrome://mozapps/content/update/updates.xul";

const CRC_ERROR = 4;

const ADDON_ID_SUFFIX = "@appupdatetest.mozilla.org";
const ADDON_PREP_DIR = "appupdateprep";
// Preference for storing add-ons that are disabled by the tests to prevent them
// from interefering with the tests.
const PREF_DISABLEDADDONS = "app.update.test.disabledAddons";
const TEST_ADDONS = [ "appdisabled_1", "appdisabled_2",
                      "compatible_1", "compatible_2",
                      "noupdate_1", "noupdate_2",
                      "updatecompatibility_1", "updatecompatibility_2",
                      "updateversion_1", "updateversion_2",
                      "userdisabled_1", "userdisabled_2" ];

const DEBUG_DUMP = false;

const TEST_TIMEOUT = 30000; // 30 seconds
var gTimeoutTimer;

// The following vars are for restoring previous preference values (if present)
// when the test finishes.
var gAppUpdateChannel; // app.update.channel (default prefbranch)
var gAppUpdateEnabled; // app.update.enabled
var gAppUpdateURL;     // app.update.url.override
var gExtUpdateURL;     // extensions.update.url

var gTestCounter = -1;
var gWin;
var gDocElem;
var gPrefToCheck;
var gDisableNoUpdateAddon = false;

#include ../shared.js

function debugDump(msg) {
  if (DEBUG_DUMP) {
    dump("*** " + msg + "\n");
  }
}

/**
 * The current test in TESTS array.
 */
__defineGetter__("gTest", function() {
  return TESTS[gTestCounter];
});

/**
 * The current test's callback. This will either return the callback defined in
 * the test's overrideCallback property or defaultCallback if the
 * overrideCallback property is undefined.
 */
__defineGetter__("gCallback", function() {
  return gTest.overrideCallback ? gTest.overrideCallback
                                : defaultCallback;
});

/**
 * The remotecontent element for the current page if one exists or null if a
 * remotecontent element doesn't exist.
 */
__defineGetter__("gRemoteContent", function() {
  switch (gTest.pageid) {
    case PAGEID_FOUND_BILLBOARD:
      return gWin.document.getElementById("updateMoreInfoContent");
    case PAGEID_LICENSE:
      return gWin.document.getElementById("licenseContent");
  }
  return null;
});

/**
 * The state for the remotecontent element if one exists or null if a
 * remotecontent element doesn't exist.
 */
__defineGetter__("gRemoteContentState", function() {
  if (gRemoteContent) {
    return gRemoteContent.getAttribute("state");
  }
  return null;
});

/**
 * The radiogroup for the license page.
 */
__defineGetter__("gAcceptDeclineLicense", function() {
  return gWin.document.getElementById("acceptDeclineLicense");
});

/**
 * The listbox for the incompatibleList page.
 */
__defineGetter__("gIncompatibleListbox", function() {
  return gWin.document.getElementById("incompatibleListbox");
});

/**
 * Default test run function that can be used by most tests.
 */
function runTestDefault() {
  debugDump("Entering runTestDefault");

  if (!("@mozilla.org/zipwriter;1" in AUS_Cc)) {
    ok(false, "nsIZipWriter is required to run these tests");
    return;
  }

  SimpleTest.waitForExplicitFinish();

  Services.ww.registerNotification(gWindowObserver);

  setupPrefs();
  removeUpdateDirsAndFiles();
  reloadUpdateManagerData();
  setupAddons(runTest);
}

/**
 * Default test finish function that can be used by most tests.
 */
function finishTestDefault() {
  debugDump("Entering finishTestDefault");

  gDocElem.removeEventListener("pageshow", onPageShowDefault, false);

  if (gTimeoutTimer) {
    gTimeoutTimer.cancel();
    gTimeoutTimer = null;
  }

  verifyTestsRan();

  Services.ww.unregisterNotification(gWindowObserver);

  resetPrefs();
  removeUpdateDirsAndFiles();
  reloadUpdateManagerData();
  SimpleTest.finish();
}

/**
 * nsITimerCallback for the timeout timer to cleanly finish a test if the Update
 * Window doesn't close for a test. This allows the next test to run properly if
 * a previous test fails.
 *
 * @param  aTimer
 *         The nsITimer that fired.
 */
function finishTestTimeout(aTimer) {
  gTimeoutTimer = null;
  ok(false, "Test timed out. Maximum time allowed is " + (TEST_TIMEOUT / 1000) +
     " seconds");
  gWin.close();
}

/**
 * Default callback for the wizard's documentElement pageshow listener. This
 * will return early for event's where the originalTarget's nodeName is not
 * wizardpage.
 */
function onPageShowDefault(aEvent) {
  // Return early if the event's original target isn't for a wizardpage element.
  // This check is necessary due to the remotecontent element firing pageshow.
  if (aEvent.originalTarget.nodeName != "wizardpage") {
    debugDump("onPageShowDefault - only handles events with an " +
              "originalTarget nodeName of |wizardpage|. " +
              "aEvent.originalTarget.nodeName = " +
              aEvent.originalTarget.nodeName + "... returning early");
    return;
  }

  gTestCounter++;
  gCallback(aEvent);
}

/**
 * Default callback that can be used by most tests.
 */
function defaultCallback(aEvent) {
  debugDump("Entering defaultCallback - TESTS[" + gTestCounter + "], " +
            "pageid: " + gTest.pageid + ", " +
            "aEvent.originalTarget.nodeName: " + aEvent.originalTarget.nodeName);

  if (gTest && gTest.extraStartFunction) {
    debugDump("defaultCallback - calling extraStartFunction " +
              gTest.extraStartFunction.name);
    if (gTest.extraStartFunction(aEvent)) {
      debugDump("defaultCallback - extraStartFunction early return");
      return;
    }
  }

  is(gDocElem.currentPage.pageid, gTest.pageid,
     "Checking currentPage.pageid equals " + gTest.pageid + " in pageshow");

  // Perform extra checks if specified by the test
  if (gTest.extraCheckFunction) {
    debugDump("delayedCallback - calling extraCheckFunction " +
              gTest.extraCheckFunction.name);
    gTest.extraCheckFunction();
  }

  // The wizard page buttons' disabled and hidden attributes are set after the
  // pageshow event so use executeSoon to allow them to be set so their disabled
  // and hidden attribute values can be checked.
  SimpleTest.executeSoon(delayedDefaultCallback);
}

/**
 * Delayed default callback called using executeSoon in defaultCallback which
 * allows the wizard page buttons' disabled and hidden attributes to be set
 * before checking their values.
 */
function delayedDefaultCallback() {
  debugDump("Entering delayedDefaultCallback - TESTS[" + gTestCounter + "], " +
            "pageid: " + gTest.pageid);

  // Verify the pageid hasn't changed after executeSoon was called.
  is(gDocElem.currentPage.pageid, gTest.pageid,
     "Checking currentPage.pageid equals " + gTest.pageid + " after " +
     "executeSoon");

  checkButtonStates();

  // Perform delayed extra checks if specified by the test
  if (gTest.extraDelayedCheckFunction) {
    debugDump("delayedDefaultCallback - calling extraDelayedCheckFunction " +
              gTest.extraDelayedCheckFunction.name);
    gTest.extraDelayedCheckFunction();
  }

  // Used to verify that this test has been performed
  gTest.ranTest = true;

  if (gTest.buttonClick) {
    debugDump("delayedDefaultCallback - clicking " + gTest.buttonClick +
              " button");
    if(gTest.extraDelayedFinishFunction) {
      throw("Tests cannot have a buttonClick and an extraDelayedFinishFunction property");
    }
    gDocElem.getButton(gTest.buttonClick).click();
  }
  else if (gTest.extraDelayedFinishFunction) {
    debugDump("delayedDefaultCallback - calling extraDelayedFinishFunction " +
              gTest.extraDelayedFinishFunction.name);
    gTest.extraDelayedFinishFunction();
  }
}

/**
 * Checks the wizard page buttons' disabled and hidden attributes values are
 * correct. If an expected button id is not specified then the expected disabled
 * and hidden attribute value is true.
 */
function checkButtonStates() {
  debugDump("Entering checkButtonStates - TESTS[" + gTestCounter + "], " +
            "pageid: " + gTest.pageid);

  const buttonNames = ["extra1", "extra2", "back", "next", "finish", "cancel"];
  let buttonStates = getExpectedButtonStates();
  buttonNames.forEach(function(aButtonName) {
    let button = gDocElem.getButton(aButtonName);
    let hasHidden = aButtonName in buttonStates &&
                    "hidden" in buttonStates[aButtonName];
    let hidden = hasHidden ? buttonStates[aButtonName].hidden : true;
    let hasDisabled = aButtonName in buttonStates &&
                      "disabled" in buttonStates[aButtonName];
    let disabled = hasDisabled ? buttonStates[aButtonName].disabled : true;
    is(button.hidden, hidden, "Checking " + aButtonName + " button " +
       "hidden attribute value equals " + (hidden ? "true" : "false"));
    is(button.disabled, disabled, "Checking " + aButtonName + " button " +
       "disabled attribute value equals " + (disabled ? "true" : "false"));
  });
}

/**
 * Returns the expected disabled and hidden attribute values for the buttons of
 * the current wizard page.
 */
function getExpectedButtonStates() {
  // Allow individual tests to override the expected button states.
  if (gTest.buttonStates) {
    return gTest.buttonStates;
  }

  switch (gTest.pageid) {
    case PAGEID_CHECKING:
    case PAGEID_INCOMPAT_CHECK:
      return { cancel: { disabled: false, hidden: false } };
    case PAGEID_FOUND_BASIC:
    case PAGEID_FOUND_BILLBOARD:
      if (gTest.neverButton) {
        return { extra1: { disabled: false, hidden: false },
                 extra2: { disabled: false, hidden: false },
                 next  : { disabled: false, hidden: false } }
      }
      return { extra1: { disabled: false, hidden: false },
               next  : { disabled: false, hidden: false } };
    case PAGEID_LICENSE:
      if (gRemoteContentState != "loaded" ||
          gAcceptDeclineLicense.selectedIndex != 0) {
        return { extra1: { disabled: false, hidden: false },
                 next  : { disabled: true, hidden: false } };
      }
      return { extra1: { disabled: false, hidden: false },
               next  : { disabled: false, hidden: false } };
    case PAGEID_INCOMPAT_LIST:
      return { extra1: { disabled: false, hidden: false },
               next  : { disabled: false, hidden: false } };
    case PAGEID_DOWNLOADING:
      return { extra1: { disabled: false, hidden: false } };
    case PAGEID_NO_UPDATES_FOUND:
    case PAGEID_MANUAL_UPDATE:
    case PAGEID_ERRORS:
    case PAGEID_INSTALLED:
      return { finish: { disabled: false, hidden: false } };
    case PAGEID_ERROR_PATCHING:
      return { next  : { disabled: false, hidden: false } };
    case PAGEID_FINISHED:
    case PAGEID_FINISHED_BKGRD:
      return { extra1: { disabled: false, hidden: false },
               finish: { disabled: false, hidden: false } };
  }
  return null;
}

/**
 * Adds a load event listener to the current remotecontent element.
 */
function addRemoteContentLoadListener() {
  debugDump("Entering addRemoteContentLoadListener - TESTS[" + gTestCounter +
            "], pageid: " + gTest.pageid);

  gRemoteContent.addEventListener("load", remoteContentLoadListener, false);
}

/**
 * The nsIDOMEventListener for a remotecontent load event.
 */
function remoteContentLoadListener(aEvent) {
  // Return early if the event's original target's nodeName isn't remotecontent.
  if (aEvent.originalTarget.nodeName != "remotecontent") {
    debugDump("remoteContentLoadListener - only handles events with an " +
              "originalTarget nodeName of |remotecontent|. " +
              "aEvent.originalTarget.nodeName = " +
              aEvent.originalTarget.nodeName);
    return;
  }

  gTestCounter++;
  gCallback(aEvent);
}

/**
 * Waits until a remotecontent element to finish loading which is determined
 * by the current test's expectedRemoteContentState property and then removes
 * the event listener.
 *
 * Note: tests that use this function should not test the state of the
 *      remotecontent since this will check the expected state.
 *
 * @return false if the remotecontent has loaded and its state is the state
 *         specified in the current test's expectedRemoteContentState
 *         property... otherwise true.
 */
function waitForRemoteContentLoaded(aEvent) {
  // Return early until the remotecontent has loaded with the state that is
  // expected or isn't the event's originalTarget.
  if (gRemoteContentState != gTest.expectedRemoteContentState ||
      !aEvent.originalTarget.isSameNode(gRemoteContent)) {
    debugDump("waitForRemoteContentLoaded - returning early\n" +
              "gRemoteContentState: " + gRemoteContentState + "\n" +
              "expectedRemoteContentState: " +
              gTest.expectedRemoteContentState + "\n" +
              "aEvent.originalTarget.nodeName: " +
              aEvent.originalTarget.nodeName);
    return true;
  }

  gRemoteContent.removeEventListener("load", remoteContentLoadListener, false);
  return false;
}

/**
 * Compares the value of the remotecontent state attribute with the value
 * specified in the test's expectedRemoteContentState property.
 */
function checkRemoteContentState() {
  is(gRemoteContentState, gTest.expectedRemoteContentState, "Checking remote " +
     "content state equals " + gTest.expectedRemoteContentState + " - pageid " +
     gTest.pageid);
}

/**
 * Adds a select event listener to the license radiogroup element and clicks
 * the radio element specified in the current test's radioClick property.
 */
function addRadioGroupSelectListenerAndClick() {
  debugDump("Entering addRadioGroupSelectListenerAndClick - TESTS[" +
            gTestCounter + "], pageid: " + gTest.pageid);

  gAcceptDeclineLicense.addEventListener("select", radioGroupSelectListener,
                                         false);
  gWin.document.getElementById(gTest.radioClick).click();
}

/**
 * The nsIDOMEventListener for the license radiogroup select event.
 */
function radioGroupSelectListener(aEvent) {
  // Return early if the event's original target's nodeName isn't radiogroup.
  if (aEvent.originalTarget.nodeName != "radiogroup") {
    debugDump("remoteContentLoadListener - only handles events with an " +
              "originalTarget nodeName of |radiogroup|. " +
              "aEvent.originalTarget.nodeName = " +
              aEvent.originalTarget.nodeName);
    return;
  }

  gAcceptDeclineLicense.removeEventListener("select", radioGroupSelectListener,
                                            false);
  gTestCounter++;
  gCallback(aEvent);
}

/**
 * Compares the value of the License radiogroup's selectedIndex attribute with
 * the value specified in the test's expectedRadioGroupSelectedIndex property.
 */
function checkRadioGroupSelectedIndex() {
  is(gAcceptDeclineLicense.selectedIndex, gTest.expectedRadioGroupSelectedIndex,
     "Checking license radiogroup selectedIndex equals " +
     gTest.expectedRadioGroupSelectedIndex);
}

/**
 * Checks that only incompatible add-ons (e.g. noupdate_X add-ons) that don't
 * have an update are listed in the add-ons incompatible list.
 */
function checkIncompatbleList() {
  for (let i = 0; i < gIncompatibleListbox.itemCount; i++) {
    let label = gIncompatibleListbox.getItemAtIndex(i).label;
    // Use indexOf since locales can change the text displayed
    ok(label.indexOf("noupdate") != -1, "Checking that only incompatible " + 
       "add-ons that don't have an update are listed in the incompatible list");
  }
}

/**
 * Compares the return value of prefHasUserValue for the preference specified in
 * gPrefToCheck with the value passed in the aPrefHasValue parameter or the
 * value specified in the current test's prefHasUserValue property if
 * aPrefHasValue is undefined.
 *
 * @param  aPrefHasValue (optional)
 *         The expected value returned from prefHasUserValue for the preference
 *         specified in gPrefToCheck. If aPrefHasValue is undefined the value
 *         of the current test's prefHasUserValue property will be used.
 */
function checkPrefHasUserValue(aPrefHasValue) {
  let prefHasUserValue = aPrefHasValue === undefined ? gTest.prefHasUserValue
                                                     : aPrefHasValue;
  is(Services.prefs.prefHasUserValue(gPrefToCheck), prefHasUserValue,
     "Checking prefHasUserValue for preference " + gPrefToCheck + " equals " +
     (prefHasUserValue ? "true" : "false"));
}

/**
 * Gets the update version info for the update url parameters to send to
 * update.sjs.
 *
 * @param  aAppVersion (optional)
 *         The application version for the update snippet. If not specified the
 *         current application version will be used.
 * @param  aPlatformVersion (optional)
 *         The platform version for the update snippet. If not specified the
 *         current platform version will be used.
 * @return The url parameters for the application and platform version to send
 *         to update.sjs.
 */
function getVersionParams(aAppVersion, aPlatformVersion) {
  let appInfo = Services.appinfo;
  return "&appVersion=" + (aAppVersion ? aAppVersion : appInfo.version) +
         "&platformVersion=" + (aPlatformVersion ? aPlatformVersion
                                                 : appInfo.platformVersion);
}

/**
 * Gets an application version that is greater than the current application
 * version. The version is created by taking the first sequence from the current
 * application version and adding 1 to it.
 *
 * @return A version string greater than the current application version string.
 */
function getNewerAppVersion() {
  let appVersion = Services.appinfo.version.split(".")[0];
  appVersion++;
  return appVersion;
}

/**
 * Gets a platform version that is greater than the current platform version.
 * The version is created by taking the first sequence from the current platform
 * version and adding 1 to it.
 *
 * @return A version string greater than the current platform version string.
 */
function getNewerPlatformVersion() {
  let platformVersion = Services.appinfo.platformVersion.split(".")[0];
  platformVersion++;
  return platformVersion;
}

/**
 * Verifies that all tests ran.
 */
function verifyTestsRan() {
  debugDump("Entering verifyTestsRan");

  // Return early if there are no tests defined.
  if (!TESTS) {
    return;
  }

  gTestCounter = -1;
  for (let i = 0; i < TESTS.length; ++i) {
    gTestCounter++;
    let test = TESTS[i];
    let msg = "Checking if TESTS[" + i + "] test was performed... " +
              "callback function name = " + gCallback.name + ", " +
              "pageid = " + test.pageid;
    ok(test.ranTest, msg);
  }
}

/**
 * Sets the most common preferences used by tests to values used by the tests
 * and saves some of the preference's original values if present so they can be
 * set back to the original values when each test has finished.
 */
function setupPrefs() {
  gAppUpdateChannel = gDefaultPrefBranch.getCharPref(PREF_APP_UPDATE_CHANNEL);
  setUpdateChannel();

  if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_URL_OVERRIDE)) {
    gAppUpdateURL = Services.prefs.setIntPref(PREF_APP_UPDATE_URL_OVERRIDE);
  }

  if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_ENABLED)) {
    gAppUpdateEnabled = Services.prefs.getBoolPref(PREF_APP_UPDATE_ENABLED);
  }
  Services.prefs.setBoolPref(PREF_APP_UPDATE_ENABLED, true)

  if (Services.prefs.prefHasUserValue(PREF_EXTENSIONS_UPDATE_URL)) {
    gExtUpdateURL = Services.prefs.getCharPref(PREF_EXTENSIONS_UPDATE_URL);
  }
  let extUpdateUrl = URL_UPDATE + "?addonID=%ITEM_ID%&platformVersion=" +
                     getNewerPlatformVersion();
  Services.prefs.setCharPref(PREF_EXTENSIONS_UPDATE_URL, extUpdateUrl);
  debugDump("extensions.update.url: " + extUpdateUrl);

  Services.prefs.setIntPref(PREF_APP_UPDATE_IDLETIME, 0);
}

/**
 * Resets the most common preferences used by tests to their original values.
 */
function resetPrefs() {
  if (gAppUpdateURL !== undefined) {
    Services.prefs.setCharPref(PREF_APP_UPDATE_URL_OVERRIDE, gAppUpdateURL);
  }
  else if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_URL_OVERRIDE)) {
    Services.prefs.clearUserPref(PREF_APP_UPDATE_URL_OVERRIDE);
  }

  if (gAppUpdateChannel !== undefined) {
    setUpdateChannel(gAppUpdateChannel);
  }

  if (gAppUpdateEnabled !== undefined) {
    Services.prefs.setBoolPref(PREF_APP_UPDATE_ENABLED, gAppUpdateEnabled);
  }
  else if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_ENABLED)) {
    Services.prefs.clearUserPref(PREF_APP_UPDATE_ENABLED);
  }

  if (gExtUpdateURL !== undefined) {
    Services.prefs.setCharPref(PREF_EXTENSIONS_UPDATE_URL, gExtUpdateURL);
  }
  else if (Services.prefs.prefHasUserValue(PREF_EXTENSIONS_UPDATE_URL)) {
    Services.prefs.clearUserPref(PREF_EXTENSIONS_UPDATE_URL);
  }

  if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_IDLETIME)) {
    Services.prefs.clearUserPref(PREF_APP_UPDATE_IDLETIME);
  }

  if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_URL_DETAILS)) {
    Services.prefs.clearUserPref(PREF_APP_UPDATE_URL_DETAILS);
  }

  if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_SHOW_INSTALLED_UI)) {
    Services.prefs.clearUserPref(PREF_APP_UPDATE_SHOW_INSTALLED_UI);
  }

  if (Services.prefs.prefHasUserValue(PREF_APP_UPDATE_LOG)) {
    Services.prefs.clearUserPref(PREF_APP_UPDATE_LOG);
  }

  try {
    Services.prefs.deleteBranch(PREF_APP_UPDATE_NEVER_BRANCH);
  }
  catch(e) {
  }
}

/**
 * Disables pre-existing add-ons so they don't interfere with the tests,
 * installs the test add-ons, sets the noupdate test add-ons' userDisabled value
 * for the test, and calls the callback specified in the aCallback parameter. If
 * the app.update.test.disabledAddons has a user value then setting the noupdate
 * test add-ons' userDisabled value for the test is the only thing that is done.
 *
 * @param  aCallback
 *         A callback to call after all operations have completed.
 */
function setupAddons(aCallback) {
  debugDump("Entering setupAddons");

  // Sets the appropriate userDisabled value for the noupdate test add-ons based
  // on the value of gDisableNoUpdateAddon and calls the callback specified in
  // setupAddons aCallback parameter.
  function setNoUpdateAddonsDisabledState() {
    AddonManager.getAllAddons(function(aAddons) {
      aAddons.forEach(function(aAddon) {
        if (aAddon.name.indexOf("noupdate") != 0)
          return;

        if (gDisableNoUpdateAddon) {
          if (!aAddon.userDisabled) {
            aAddon.userDisabled = true;
          }
        }
        else {
          if (aAddon.userDisabled) {
            aAddon.userDisabled = false;
          }
        }
      });
      aCallback();
    });
  }

  // If the app.update.test.disabledAddons preference exists the pre-existing
  // add-ons have already been disabled so they don't interfere with the tests,
  // the test add-ons have already been installed, and the only thing that needs
  // to be done is setting the appropriate userDisabled value for the noupdate
  // test add-ons.
  if (Services.prefs.prefHasUserValue(PREF_DISABLEDADDONS)) {
    setNoUpdateAddonsDisabledState();
    return;
  }

  // Disable all pre-existing enabled addons so they don't interfere with the
  // tests.
  AddonManager.getAllAddons(function(aAddons) {
    let disabledAddons = [];
    aAddons.forEach(function(aAddon) {
      // If an addon's type equals plugin it is skipped since
      // checking plugins compatibility information isn't supported at this
      // time (also see bug 566787). Also, SCOPE_APPLICATION add-ons are
      // excluded by app update so there is no reason to disable them.
      if (aAddon.type != "plugin" && !aAddon.appDisabled &&
          !aAddon.userDisabled &&
          aAddon.scope != AddonManager.SCOPE_APPLICATION) {
        disabledAddons.push(aAddon);
        aAddon.userDisabled = true;
      }
    });
    // If there are no pre-existing add-ons the preference value will be an
    // empty string.
    Services.prefs.setCharPref(PREF_DISABLEDADDONS, disabledAddons.join(" "));

    // Install the test add-ons.
    let xpiFiles = getTestAddonXPIFiles();
    let xpiCount = xpiFiles.length;
    let installs = [];
    xpiFiles.forEach(function(aFile) {
      AddonManager.getInstallForFile(aFile, function(aInstall) {
        if (!aInstall) {
          throw "No AddonInstall created for " + aFile.path;
        }

        installs.push(aInstall);

        if (--xpiCount == 0) {
          let installCount = installs.length;
          function installCompleted(aInstall) {
            aInstall.removeListener(listener);

            if (getAddonTestType(aInstall.addon.name) == "userdisabled") {
              aInstall.addon.userDisabled = true;
            }
            if (--installCount == 0) {
              setNoUpdateAddonsDisabledState();
            }
          }

          let listener = {
            onDownloadFailed: installCompleted,
            onDownloadCancelled: installCompleted,
            onInstallFailed: installCompleted,
            onInstallCancelled: installCompleted,
            onInstallEnded: installCompleted
          };

          installs.forEach(function(aInstall) {
            aInstall.addListener(listener);
            aInstall.install();
          });
        }
      });
    });
  });
}

/**
 * Uninstalls the test add-ons, enables add-ons that were disabled when the
 * test started, and calls the callback specified in the aCallback parameter.
 *
 * @param  aCallback
 *         A callback to call after all operations have completed.
 */
function resetAddons(aCallback) {
  debugDump("Entering resetAddons");
  // If test_9999_cleanup.xul is ran by itself then the test add-ons will not
  // have been installed and any pre-existing add-ons will not have been
  // disabled so return early.
  if (!Services.prefs.prefHasUserValue(PREF_DISABLEDADDONS)) {
    debugDump("resetAddons - preference " + PREF_DISABLEDADDONS + " doesn't " +
              "exist... returning early");
    aCallback();
    return;
  }

  // Uninstall the test add-ons.
  let count = TEST_ADDONS.length;
  function uninstallCompleted(aAddon) {
    if (--count == 0) {
      AddonManager.removeAddonListener(listener);

      // Enable the pre-existing add-ons that were disabled so they wouldn't
      // interfere with the tests.
      let disabledAddons = Services.prefs.getCharPref(PREF_DISABLEDADDONS).split(" ");
      Services.prefs.clearUserPref(PREF_DISABLEDADDONS);
      AddonManager.getAllAddons(function(aAddons) {
        aAddons.forEach(function(aAddon) {
          if (disabledAddons.indexOf(aAddon.id)) {
            aAddon.userDisabled = false;
          }
        });
        aCallback();
      });
    }
  }

  let listener = {
    onUninstalled: uninstallCompleted
  };

  AddonManager.addAddonListener(listener);
  TEST_ADDONS.forEach(function(aName) {
    AddonManager.getAddonByID(aName + ADDON_ID_SUFFIX, function(aAddon) {
      aAddon.uninstall();
    });
  });
}

/**
 * Helper function to get the string before the '_' character in an add-on's
 * name or id which is used to determine the add-on test type used by the tests.
 *
 * @param  aName
 *         The test add-on's name or id.
 * @return The string before the '_' character in the string passed in the aName
 *         parameter.
 */
function getAddonTestType(aName) {
  return aName.split("_")[0];
}

/**
 * Helper function to create add-on xpi files for the default test add-ons.
 *
 * @return An array with each member being an nsILocalFile for an add-on XPI
 *         file.
 */
function getTestAddonXPIFiles() {
  let addonPrepDir = Services.dirsvc.get(NS_APP_USER_PROFILE_50_DIR,
                                         AUS_Ci.nsILocalFile);
  addonPrepDir.append(ADDON_PREP_DIR);

  let bootstrap = addonPrepDir.clone();
  bootstrap.append("bootstrap.js");
  // If a previous test has already created bootstrap.js don't create it again.
  if (!bootstrap.exists()) {
    let bootstrapContents = "function install(data, reason){ }\n" +
                            "function startup(data, reason){ }\n" +
                            "function shutdown(data, reason){ }\n" +
                            "function uninstall(data, reason){ }\n";
    writeFile(bootstrap, bootstrapContents);
  }

  let installRDF = addonPrepDir.clone();
  installRDF.append("install.rdf");

  let xpiFiles = [];
  TEST_ADDONS.forEach(function(aName) {
    let xpiFile = addonPrepDir.clone();
    xpiFile.append(aName + ".xpi");

    if (installRDF.exists())
      installRDF.remove(false);
    writeFile(installRDF, getInstallRDFString(aName));
    gZipW.open(xpiFile, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE);
    gZipW.addEntryFile(installRDF.leafName,
                       AUS_Ci.nsIZipWriter.COMPRESSION_DEFAULT, installRDF,
                       false);
    gZipW.addEntryFile(bootstrap.leafName,
                       AUS_Ci.nsIZipWriter.COMPRESSION_DEFAULT, bootstrap,
                       false);
    gZipW.close();
    xpiFiles.push(xpiFile);
  });

  return xpiFiles;
}

/**
 * Helper function to gets the string representation of the contents of the
 * add-on's install.rdf file.
 *
 * @param  aName
 *         The string to use for the add-on's name which is also used to
 *         construct the local-part in RFC 5322 format of the add-on's ID.
 * @return A string representation of the contents of the add-on's install.rdf
 *         file.
 */
function getInstallRDFString(aName) {
  let maxVersion = Services.appinfo.platformVersion;
  switch (getAddonTestType(aName)) {
    case "compatible":
      maxVersion = getNewerPlatformVersion();
      break;
    case "appdisabled":
      maxVersion = "0.1";
      break;
  }

  return "<?xml version=\"1.0\"?>\n" +
         "<RDF xmlns=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"\n" +
         "  xmlns:em=\"http://www.mozilla.org/2004/em-rdf#\">\n" +
         "  <Description about=\"urn:mozilla:install-manifest\">\n" +
         "    <em:id>" + aName + ADDON_ID_SUFFIX + "</em:id>\n" +
         "    <em:version>1.0</em:version>\n" +
         "    <em:bootstrap>true</em:bootstrap>\n" +
         "    <em:name>" + aName + "</em:name>\n" +
         "    <em:description>Test Description</em:description>\n" +
         "    <em:targetApplication>\n" +
         "      <Description>\n" +
         "        <em:id>toolkit@mozilla.org</em:id>\n" +
         "        <em:minVersion>undefined</em:minVersion>\n" +
         "        <em:maxVersion>" + maxVersion + "</em:maxVersion>\n" +
         "      </Description>\n" +
         "    </em:targetApplication>\n" +
         "  </Description>\n" +
         "</RDF>";
}

/**
 * Closes the update window if it is open.
 */
function closeUpdateWindow() {
  let updateWindow = getUpdateWindow();
  if (!updateWindow)
    return;

  ok(false, "Found an existing Update Window from a previous test... " +
            "attempting to close it.");
  updateWindow.close();
}

/**
 * Gets the update window.
 *
 * @return The nsIDOMWindowInternal for the Update Window if it is open and null
 *         if it isn't.
 */
function getUpdateWindow() {
  return Services.wm.getMostRecentWindow(UPDATE_WINDOW_NAME);
}

/**
 * nsIObserver for receiving window open and close notifications.
 */
var gWindowObserver = {
  loaded: false,

  observe: function WO_observe(aSubject, aTopic, aData) {
    let win = aSubject.QueryInterface(AUS_Ci.nsIDOMEventTarget);

    if (aTopic == "domwindowclosed") {
      if (win.location == URI_UPDATE_PROMPT_DIALOG) {
        // Allow tests the ability to provide their own function (it must be
        // named finishTest) for finishing the test.
        try {
          finishTest();
        }
        catch (e) {
          finishTestDefault();
        }
      }
      return;
    }

    // Defensive measure to prevent adding multiple listeners.
    if (this.loaded) {
      // This should never happen but if it does this will provide a clue for
      // diagnosing the cause.
      ok(false, "Unexpected gWindowObserver:observe - called with aTopic = " +
         aTopic + "... returning early");
      return;
    }

    win.addEventListener("load", function onLoad() {
      // Defensive measure to prevent windows we shouldn't see from breaking
      // a test.
      if (win.location != URI_UPDATE_PROMPT_DIALOG) {
        // This should never happen.
        ok(false, "Unexpected load event - win.location got: " + location +
           ", expected: " + URI_UPDATE_PROMPT_DIALOG + "... returning early");
        return;
      }

      // Defensive measure to prevent an unexpected wizard page from breaking
      // a test.
      let pageid = win.document.documentElement.currentPage.pageid;
      if (pageid != PAGEID_DUMMY) {
        // This should never happen but if it does this will provide a clue
        // for diagnosing the cause.
        ok(false, "Unexpected load event - pageid got: " + pageid +
           ", expected: " + PAGEID_DUMMY + "... returning early");
        return;
      }

      win.removeEventListener("load", onLoad, false);
      gTimeoutTimer = AUS_Cc["@mozilla.org/timer;1"].
                      createInstance(AUS_Ci.nsITimer);
      gTimeoutTimer.initWithCallback(finishTestTimeout, TEST_TIMEOUT,
                                     AUS_Ci.nsITimer.TYPE_ONE_SHOT);

      gWin = win;
      gDocElem = gWin.document.documentElement;
      gDocElem.addEventListener("pageshow", onPageShowDefault, false);
    }, false);

    this.loaded = true;
  }
};
