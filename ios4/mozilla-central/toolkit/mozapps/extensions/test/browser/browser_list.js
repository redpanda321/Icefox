/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Tests the recent updates pane

var gProvider;
var gManagerWindow;
var gCategoryUtilities;

var gApp = document.getElementById("bundle_brand").getString("brandShortName");
var gVersion = Services.appinfo.version;
var gBlocklistURL = Services.urlFormatter.formatURLPref("extensions.blocklist.detailsURL");
var gDate = new Date(2010, 7, 16);

function test() {
  waitForExplicitFinish();

  gProvider = new MockProvider();

  gProvider.createAddons([{
    id: "addon1@tests.mozilla.org",
    name: "Test add-on",
    version: "1.0",
    description: "A test add-on",
    longDescription: " A longer description",
    updateDate: gDate
  }, {
    id: "addon2@tests.mozilla.org",
    name: "Test add-on 2",
    version: "2.0",
    longDescription: " A longer description",
    _userDisabled: true,
    isActive: false,
  }, {
    id: "addon3@tests.mozilla.org",
    name: "Test add-on 3",
    longDescription: " A longer description",
    isActive: false,
    isCompatible: false,
    appDisabled: true,
    permissions: AddonManager.PERM_CAN_ENABLE |
                 AddonManager.PERM_CAN_DISABLE |
                 AddonManager.PERM_CAN_UPGRADE
  }, {
    id: "addon4@tests.mozilla.org",
    name: "Test add-on 4",
    _userDisabled: true,
    isActive: false,
    blocklistState: Ci.nsIBlocklistService.STATE_SOFTBLOCKED
  }, {
    id: "addon5@tests.mozilla.org",
    name: "Test add-on 5",
    isActive: false,
    blocklistState: Ci.nsIBlocklistService.STATE_BLOCKED,
    appDisabled: true
  }, {
    id: "addon6@tests.mozilla.org",
    name: "Test add-on 6",
    operationsRequiringRestart: AddonManager.OP_NEEDS_RESTART_NONE
  }]);

  open_manager(null, function(aWindow) {
    gManagerWindow = aWindow;
    gCategoryUtilities = new CategoryUtilities(gManagerWindow);
    run_next_test();
  });
}

function end_test() {
  close_manager(gManagerWindow, function() {
    finish();
  });
}

function get_node(parent, anonid) {
  return parent.ownerDocument.getAnonymousElementByAttribute(parent, "anonid", anonid);
}

function get_class_node(parent, cls) {
  return parent.ownerDocument.getAnonymousElementByAttribute(parent, "class", cls);
}

// Check that the list appears to have displayed correctly and trigger some
// changes
add_test(function() {
  gCategoryUtilities.openType("extension", function() {
    let list = gManagerWindow.document.getElementById("addon-list");
    is(list.childNodes.length, 6, "Should be six add-ons installed");

    info("Addon 1");
    let addon = list.firstChild;
    is(get_node(addon, "name").value, "Test add-on", "Name should be correct");
    is_element_visible(get_node(addon, "version"), "Version should be visible");
    is(get_node(addon, "version").value, "1.0", "Version should be correct");
    is_element_visible(get_node(addon, "description"), "Description should be visible");
    is(get_node(addon, "description").value, "A test add-on", "Description should be correct");
    is_element_hidden(get_node(addon, "creator"), "Creator should be hidden");
    is_element_hidden(get_class_node(addon, "disabled-postfix"), "Disabled postfix should be hidden");
    is_element_hidden(get_class_node(addon, "update-postfix"), "Update postfix should be hidden");
    is(Date.parse(get_node(addon, "date-updated").value), gDate.getTime(), "Update date should be correct");

    is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
    is_element_hidden(get_node(addon, "enable-btn"), "Enable button should be hidden");
    is_element_visible(get_node(addon, "disable-btn"), "Disable button should be visible");
    is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

    is_element_hidden(get_node(addon, "warning"), "Warning message should be hidden");
    is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
    is_element_hidden(get_node(addon, "error"), "Error message should be hidden");
    is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
    is_element_hidden(get_node(addon, "pending"), "Pending message should be hidden");

    info("Disabling");
    EventUtils.synthesizeMouse(get_node(addon, "disable-btn"), 2, 2, {}, gManagerWindow);
    is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
    is_element_visible(get_node(addon, "enable-btn"), "Enable button should be visible");
    is_element_hidden(get_node(addon, "disable-btn"), "Disable button should be hidden");
    is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

    is_element_hidden(get_node(addon, "warning"), "Warning message should be visible");
    is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
    is_element_hidden(get_node(addon, "error"), "Error message should be hidden");
    is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
    is_element_visible(get_node(addon, "pending"), "Pending message should be visible");
    is(get_node(addon, "pending").textContent, "Test add-on will be disabled after you restart " + gApp + ".", "Pending message should be correct");

    info("Addon 2");
    addon = addon.nextSibling;
    is(get_node(addon, "name").value, "Test add-on 2", "Name should be correct");
    is_element_visible(get_node(addon, "version"), "Version should be visible");
    is(get_node(addon, "version").value, "2.0", "Version should be correct");
    is_element_hidden(get_node(addon, "description"), "Description should be hidden");
    is_element_hidden(get_node(addon, "creator"), "Creator should be hidden");
    is_element_visible(get_class_node(addon, "disabled-postfix"), "Disabled postfix should be visible");
    is_element_hidden(get_class_node(addon, "update-postfix"), "Update postfix should be hidden");
    is(get_node(addon, "date-updated").value, "Unknown", "Date should be correct");

    is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
    is_element_visible(get_node(addon, "enable-btn"), "Enable button should be visible");
    is_element_hidden(get_node(addon, "disable-btn"), "Disable button should be hidden");
    is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

    is_element_hidden(get_node(addon, "warning"), "Warning message should be hidden");
    is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
    is_element_hidden(get_node(addon, "error"), "Error message should be hidden");
    is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
    is_element_hidden(get_node(addon, "pending"), "Pending message should be hidden");

    info("Enabling");
    EventUtils.synthesizeMouse(get_node(addon, "enable-btn"), 2, 2, {}, gManagerWindow);
    is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
    is_element_hidden(get_node(addon, "enable-btn"), "Enable button should be hidden");
    is_element_visible(get_node(addon, "disable-btn"), "Disable button should be visible");
    is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

    is_element_hidden(get_node(addon, "warning"), "Warning message should be hidden");
    is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
    is_element_hidden(get_node(addon, "error"), "Error message should be hidden");
    is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
    is_element_visible(get_node(addon, "pending"), "Pending message should be visible");
    is(get_node(addon, "pending").textContent, "Test add-on 2 will be enabled after you restart " + gApp + ".", "Pending message should be correct");

    info("Addon 3");
    addon = addon.nextSibling;
    is(get_node(addon, "name").value, "Test add-on 3", "Name should be correct");
    is_element_hidden(get_node(addon, "version"), "Version should be hidden");

    is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
    is_element_hidden(get_node(addon, "enable-btn"), "Enable button should be hidden");
    is_element_hidden(get_node(addon, "disable-btn"), "Disable button should be hidden");
    is_element_hidden(get_node(addon, "remove-btn"), "Remove button should be hidden");

    is_element_visible(get_node(addon, "warning"), "Warning message should be visible");
    is(get_node(addon, "warning").textContent, "Test add-on 3 is incompatible with " + gApp + " " + gVersion + ".", "Warning message should be correct");
    is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
    is_element_hidden(get_node(addon, "error"), "Error message should be hidden");
    is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
    is_element_hidden(get_node(addon, "pending"), "Pending message should be hidden");

    info("Addon 4");
    addon = addon.nextSibling;
    is(get_node(addon, "name").value, "Test add-on 4", "Name should be correct");

    is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
    is_element_visible(get_node(addon, "enable-btn"), "Enable button should be visible");
    is_element_hidden(get_node(addon, "disable-btn"), "Disable button should be hidden");
    is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

    is_element_visible(get_node(addon, "warning"), "Warning message should be visible");
    is(get_node(addon, "warning").textContent, "Test add-on 4 is known to cause security or stability issues.", "Warning message should be correct");
    is_element_visible(get_node(addon, "warning-link"), "Warning link should be visible");
    is(get_node(addon, "warning-link").value, "More Information", "Warning link text should be correct");
    is(get_node(addon, "warning-link").href, gBlocklistURL, "Warning link should be correct");
    is_element_hidden(get_node(addon, "error"), "Error message should be hidden");
    is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
    is_element_hidden(get_node(addon, "pending"), "Pending message should be hidden");

    info("Enabling");
    EventUtils.synthesizeMouse(get_node(addon, "enable-btn"), 2, 2, {}, gManagerWindow);
    is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
    is_element_hidden(get_node(addon, "enable-btn"), "Enable button should be hidden");
    is_element_visible(get_node(addon, "disable-btn"), "Disable button should be visible");
    is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

    is_element_hidden(get_node(addon, "warning"), "Warning message should be hidden");
    is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
    is_element_hidden(get_node(addon, "error"), "Error message should be hidden");
    is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
    is_element_visible(get_node(addon, "pending"), "Pending message should be visible");
    is(get_node(addon, "pending").textContent, "Test add-on 4 will be enabled after you restart " + gApp + ".", "Pending message should be correct");

    info("Addon 5");
    addon = addon.nextSibling;
    is(get_node(addon, "name").value, "Test add-on 5", "Name should be correct");

    is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
    is_element_hidden(get_node(addon, "enable-btn"), "Enable button should be hidden");
    is_element_hidden(get_node(addon, "disable-btn"), "Disable button should be hidden");
    is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

    is_element_hidden(get_node(addon, "warning"), "Warning message should be hidden");
    is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
    is_element_visible(get_node(addon, "error"), "Error message should be visible");
    is(get_node(addon, "error").textContent, "Test add-on 5 has been disabled due to security or stability issues.", "Error message should be correct");
    is_element_visible(get_node(addon, "error-link"), "Error link should be visible");
    is(get_node(addon, "error-link").value, "More Information", "Error link text should be correct");
    is(get_node(addon, "error-link").href, gBlocklistURL, "Error link should be correct");
    is_element_hidden(get_node(addon, "pending"), "Pending message should be hidden");

    info("Addon 6");
    addon = addon.nextSibling;
    is(get_node(addon, "name").value, "Test add-on 6", "Name should be correct");
    is_element_hidden(get_class_node(addon, "disabled-postfix"), "Disabled postfix should be hidden");

    is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
    is_element_hidden(get_node(addon, "enable-btn"), "Enable button should be hidden");
    is_element_visible(get_node(addon, "disable-btn"), "Disable button should be visible");
    is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

    is_element_hidden(get_node(addon, "warning"), "Warning message should be hidden");
    is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
    is_element_hidden(get_node(addon, "error"), "Error message should be visible");
    is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
    is_element_hidden(get_node(addon, "pending"), "Pending message should be hidden");

    info("Disabling");
    EventUtils.synthesizeMouse(get_node(addon, "disable-btn"), 2, 2, {}, gManagerWindow);
    is_element_visible(get_class_node(addon, "disabled-postfix"), "Disabled postfix should be visible");

    is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
    is_element_visible(get_node(addon, "enable-btn"), "Enable button should be visible");
    is_element_hidden(get_node(addon, "disable-btn"), "Disable button should be hidden");
    is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

    is_element_hidden(get_node(addon, "warning"), "Warning message should be hidden");
    is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
    is_element_hidden(get_node(addon, "error"), "Error message should be visible");
    is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
    is_element_hidden(get_node(addon, "pending"), "Pending message should be hidden");

    run_next_test();
  });
});

// Check the add-ons are now in the right state
add_test(function() {
  AddonManager.getAddonsByIDs(["addon1@tests.mozilla.org",
                               "addon2@tests.mozilla.org",
                               "addon4@tests.mozilla.org",
                               "addon6@tests.mozilla.org"],
                               function([a1, a2, a4, a6]) {
    is(a1.pendingOperations, AddonManager.PENDING_DISABLE, "Add-on 1 should be pending disable");
    is(a2.pendingOperations, AddonManager.PENDING_ENABLE, "Add-on 2 should be pending enable");
    is(a4.pendingOperations, AddonManager.PENDING_ENABLE, "Add-on 4 should be pending enable");

    run_next_test();
  });
});

// Reload the list to make sure the changes are still pending and that undoing
// works
add_test(function() {
  gCategoryUtilities.openType("plugin", function() {
    gCategoryUtilities.openType("extension", function() {
      let list = gManagerWindow.document.getElementById("addon-list");
      is(list.childNodes.length, 6, "Should be six add-ons installed");

      info("Addon 1");
      let addon = list.firstChild;
      is(get_node(addon, "name").value, "Test add-on", "Name should be correct");
      is_element_visible(get_node(addon, "version"), "Version should be visible");
      is(get_node(addon, "version").value, "1.0", "Version should be correct");
      is_element_visible(get_node(addon, "description"), "Description should be visible");
      is(get_node(addon, "description").value, "A test add-on", "Description should be correct");
      is_element_hidden(get_node(addon, "creator"), "Creator should be hidden");
      is_element_hidden(get_class_node(addon, "disabled-postfix"), "Disabled postfix should be hidden");
      is_element_hidden(get_class_node(addon, "update-postfix"), "Update postfix should be hidden");
      is(Date.parse(get_node(addon, "date-updated").value), gDate.getTime(), "Update date should be correct");

      is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
      is_element_visible(get_node(addon, "enable-btn"), "Enable button should be visible");
      is_element_hidden(get_node(addon, "disable-btn"), "Disable button should be hidden");
      is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

      is_element_hidden(get_node(addon, "warning"), "Warning message should be hidden");
      is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
      is_element_hidden(get_node(addon, "error"), "Error message should be hidden");
      is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
      is_element_visible(get_node(addon, "pending"), "Pending message should be visible");
      is(get_node(addon, "pending").textContent, "Test add-on will be disabled after you restart " + gApp + ".", "Pending message should be correct");

      info("Undoing");
      EventUtils.synthesizeMouse(get_node(addon, "undo"), 2, 2, {}, gManagerWindow);
      is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
      is_element_hidden(get_node(addon, "enable-btn"), "Enable button should be hidden");
      is_element_visible(get_node(addon, "disable-btn"), "Disable button should be visible");
      is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

      is_element_hidden(get_node(addon, "warning"), "Warning message should be hidden");
      is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
      is_element_hidden(get_node(addon, "error"), "Error message should be hidden");
      is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
      is_element_hidden(get_node(addon, "pending"), "Pending message should be hidden");

      info("Addon 2");
      addon = addon.nextSibling;
      is(get_node(addon, "name").value, "Test add-on 2", "Name should be correct");
      is_element_visible(get_node(addon, "version"), "Version should be visible");
      is(get_node(addon, "version").value, "2.0", "Version should be correct");
      is_element_hidden(get_node(addon, "description"), "Description should be hidden");
      is_element_hidden(get_node(addon, "creator"), "Creator should be hidden");
      is_element_visible(get_class_node(addon, "disabled-postfix"), "Disabled postfix should be visible");
      is_element_hidden(get_class_node(addon, "update-postfix"), "Update postfix should be hidden");
      is(get_node(addon, "date-updated").value, "Unknown", "Date should be correct");

      is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
      is_element_hidden(get_node(addon, "enable-btn"), "Enable button should be hidden");
      is_element_visible(get_node(addon, "disable-btn"), "Disable button should be visible");
      is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

      is_element_hidden(get_node(addon, "warning"), "Warning message should be hidden");
      is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
      is_element_hidden(get_node(addon, "error"), "Error message should be hidden");
      is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
      is_element_visible(get_node(addon, "pending"), "Pending message should be visible");
      is(get_node(addon, "pending").textContent, "Test add-on 2 will be enabled after you restart " + gApp + ".", "Pending message should be correct");

      info("Undoing");
      EventUtils.synthesizeMouse(get_node(addon, "undo"), 2, 2, {}, gManagerWindow);
      is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
      is_element_visible(get_node(addon, "enable-btn"), "Enable button should be visible");
      is_element_hidden(get_node(addon, "disable-btn"), "Disable button should be hidden");
      is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

      is_element_hidden(get_node(addon, "warning"), "Warning message should be hidden");
      is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
      is_element_hidden(get_node(addon, "error"), "Error message should be hidden");
      is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
      is_element_hidden(get_node(addon, "pending"), "Pending message should be hidden");

      info("Addon 4");
      addon = addon.nextSibling;
      addon = addon.nextSibling;
      is(get_node(addon, "name").value, "Test add-on 4", "Name should be correct");

      is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
      is_element_hidden(get_node(addon, "enable-btn"), "Enable button should be hidden");
      is_element_visible(get_node(addon, "disable-btn"), "Disable button should be visible");
      is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

      is_element_hidden(get_node(addon, "warning"), "Warning message should be hidden");
      is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
      is_element_hidden(get_node(addon, "error"), "Error message should be hidden");
      is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
      is_element_visible(get_node(addon, "pending"), "Pending message should be visible");
      is(get_node(addon, "pending").textContent, "Test add-on 4 will be enabled after you restart " + gApp + ".", "Pending message should be correct");

      info("Undoing");
      EventUtils.synthesizeMouse(get_node(addon, "undo"), 2, 2, {}, gManagerWindow);
      is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
      is_element_visible(get_node(addon, "enable-btn"), "Enable button should be visible");
      is_element_hidden(get_node(addon, "disable-btn"), "Disable button should be hidden");
      is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

      is_element_visible(get_node(addon, "warning"), "Warning message should be visible");
      is(get_node(addon, "warning").textContent, "Test add-on 4 is known to cause security or stability issues.", "Warning message should be correct");
      is_element_visible(get_node(addon, "warning-link"), "Warning link should be visible");
      is(get_node(addon, "warning-link").value, "More Information", "Warning link text should be correct");
      is(get_node(addon, "warning-link").href, gBlocklistURL, "Warning link should be correct");
      is_element_hidden(get_node(addon, "error"), "Error message should be hidden");
      is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
      is_element_hidden(get_node(addon, "pending"), "Pending message should be hidden");

      info("Addon 6");
      addon = addon.nextSibling;
      addon = addon.nextSibling;
      is(get_node(addon, "name").value, "Test add-on 6", "Name should be correct");
      is_element_visible(get_class_node(addon, "disabled-postfix"), "Disabled postfix should be visible");

      is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
      is_element_visible(get_node(addon, "enable-btn"), "Enable button should be visible");
      is_element_hidden(get_node(addon, "disable-btn"), "Disable button should be hidden");
      is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

      is_element_hidden(get_node(addon, "warning"), "Warning message should be hidden");
      is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
      is_element_hidden(get_node(addon, "error"), "Error message should be visible");
      is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
      is_element_hidden(get_node(addon, "pending"), "Pending message should be hidden");

      info("Enabling");
      EventUtils.synthesizeMouse(get_node(addon, "enable-btn"), 2, 2, {}, gManagerWindow);
      is_element_hidden(get_class_node(addon, "disabled-postfix"), "Disabled postfix should be hidden");

      is_element_hidden(get_node(addon, "preferences-btn"), "Preferences button should be hidden");
      is_element_hidden(get_node(addon, "enable-btn"), "Enable button should be hidden");
      is_element_visible(get_node(addon, "disable-btn"), "Disable button should be visible");
      is_element_visible(get_node(addon, "remove-btn"), "Remove button should be visible");

      is_element_hidden(get_node(addon, "warning"), "Warning message should be hidden");
      is_element_hidden(get_node(addon, "warning-link"), "Warning link should be hidden");
      is_element_hidden(get_node(addon, "error"), "Error message should be visible");
      is_element_hidden(get_node(addon, "error-link"), "Error link should be hidden");
      is_element_hidden(get_node(addon, "pending"), "Pending message should be hidden");

      run_next_test();
    });
  });
});

// Check the add-ons are now in the right state
add_test(function() {
  AddonManager.getAddonsByIDs(["addon1@tests.mozilla.org",
                               "addon2@tests.mozilla.org",
                               "addon4@tests.mozilla.org"],
                               function([a1, a2, a4]) {
    is(a1.pendingOperations, 0, "Add-on 1 should not have any pending operations");
    is(a2.pendingOperations, 0, "Add-on 1 should not have any pending operations");
    is(a4.pendingOperations, 0, "Add-on 1 should not have any pending operations");

    run_next_test();
  });
});
