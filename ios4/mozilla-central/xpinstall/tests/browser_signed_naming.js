// Load in the test harness
var scriptLoader = Components.classes["@mozilla.org/moz/jssubscript-loader;1"]
                             .getService(Components.interfaces.mozIJSSubScriptLoader);
scriptLoader.loadSubScript("chrome://mochikit/content/browser/xpinstall/tests/harness.js", this);

// ----------------------------------------------------------------------------
// Tests that the correct signer is presented for combinations of O and CN present.
// The signed files have (when present) O=Mozilla Testing, CN=Object Signer
// This verifies bug 372980
function test() {
  Harness.installConfirmCallback = confirm_install;
  Harness.installEndedCallback = check_xpi_install;
  Harness.installsCompletedCallback = finish_test;
  Harness.setup();

  var pm = Services.perms;
  pm.add(makeURI("http://example.com/"), "install", pm.ALLOW_ACTION);

  var triggers = encodeURIComponent(JSON.stringify({
    "Signed XPI (O and CN)": TESTROOT + "signed.xpi",
    "Signed XPI (CN)": TESTROOT + "signed-no-o.xpi",
    "Signed XPI (O)": TESTROOT + "signed-no-cn.xpi",
  }));
  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.loadURI(TESTROOT + "installtrigger.html?" + triggers);
}

function confirm_install(window) {
  items = window.document.getElementById("itemList").childNodes;
  is(items.length, 3, "Should be 3 items listed in the confirmation dialog");
  is(items[0].name, "Signed XPI (O and CN)", "Should have seen the name from the trigger list");
  is(items[0].url, TESTROOT + "signed.xpi", "Should have listed the correct url for the item");
  is(items[0].cert, "(Object Signer)", "Should have seen the signer");
  is(items[0].signed, "true", "Should have listed the item as signed");
  is(items[1].name, "Signed XPI (CN)", "Should have seen the name from the trigger list");
  is(items[1].url, TESTROOT + "signed-no-o.xpi", "Should have listed the correct url for the item");
  is(items[1].cert, "(Object Signer)", "Should have seen the signer");
  is(items[1].signed, "true", "Should have listed the item as signed");
  is(items[2].name, "Signed XPI (O)", "Should have seen the name from the trigger list");
  is(items[2].url, TESTROOT + "signed-no-cn.xpi", "Should have listed the correct url for the item");
  is(items[2].cert, "(Mozilla Testing)", "Should have seen the signer");
  is(items[2].signed, "true", "Should have listed the item as signed");
  return true;
}

function check_xpi_install(addon, status) {
  is(status, 0, "Installs should succeed");
}

function finish_test() {
  var em = Components.classes["@mozilla.org/extensions/manager;1"]
                     .getService(Components.interfaces.nsIExtensionManager);
  em.cancelInstallItem("signed-xpi@tests.mozilla.org");
  em.cancelInstallItem("signed-xpi-no-o@tests.mozilla.org");
  em.cancelInstallItem("signed-xpi-no-cn@tests.mozilla.org");

  Services.perms.remove("example.com", "install");

  gBrowser.removeCurrentTab();
  Harness.finish();
}
