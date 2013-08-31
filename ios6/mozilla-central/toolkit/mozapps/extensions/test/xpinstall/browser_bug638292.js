// ----------------------------------------------------------------------------
// Test whether an InstallTrigger.enabled is working
function test() {
  waitForExplicitFinish();

  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.selectedBrowser.addEventListener("load", function() {
    gBrowser.selectedBrowser.removeEventListener("load", arguments.callee, true);
    waitForFocus(page_loaded, gBrowser.contentWindow);
  }, true);
  gBrowser.loadURI(TESTROOT + "bug638292.html");
}

function check_load(aCallback) {
  gBrowser.addEventListener("load", function(aEvent) {
    if (!gBrowser.browsers[2] ||
        aEvent.target != gBrowser.browsers[2].contentDocument) {
      // SeaMonkey tabbrowser needs to deal with additional loads.
      if (navigator.userAgent.match(/ SeaMonkey\//))
        info("Ignoring unrelated load on SeaMonkey. (Expected 2-3 times.)");
      else
        ok(false, "Ignoring unrelated load on Firefox. (Should never happen!)");
      return;
    }

    gBrowser.removeEventListener("load", arguments.callee, true);

    // Let the load handler complete
    executeSoon(function() {
      var doc = gBrowser.browsers[2].contentDocument;
      is(doc.getElementById("enabled").textContent, "true", "installTrigger should have been enabled");

      // Focus the old tab
      gBrowser.selectedTab = gBrowser.tabs[1];
      waitForFocus(function() {
        // Close the new tab
        gBrowser.removeTab(gBrowser.tabs[2]);
        aCallback();
      }, gBrowser.contentWindow);
    });
  }, true);
}

function page_loaded() {
  var doc = gBrowser.contentDocument;
  info("Clicking link 1");
  EventUtils.synthesizeMouseAtCenter(doc.getElementById("link1"), { }, gBrowser.contentWindow);

  check_load(function() {
    info("Clicking link 2");
    EventUtils.synthesizeMouseAtCenter(doc.getElementById("link2"), { }, gBrowser.contentWindow);

    check_load(function() {
      info("Clicking link 3");
      EventUtils.synthesizeMouseAtCenter(doc.getElementById("link3"), { button: 1 }, gBrowser.contentWindow);

      check_load(function() {
        gBrowser.removeCurrentTab();
        finish();
      });
    });
  });
}
