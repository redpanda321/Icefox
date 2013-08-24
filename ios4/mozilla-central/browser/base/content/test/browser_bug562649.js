function test() {
  const URI = "data:text/plain,bug562649";
  browserDOMWindow.openURI(makeURI(URI),
                           null,
                           Ci.nsIBrowserDOMWindow.OPEN_NEWTAB,
                           Ci.nsIBrowserDOMWindow.OPEN_EXTERNAL);

  ok(XULBrowserWindow.isBusy, "window is busy loading a page");
  is(gBrowser.userTypedValue, URI, "userTypedValue matches test URI");
  is(gURLBar.value, URI, "location bar value matches test URI");

  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.removeCurrentTab();
  is(gBrowser.userTypedValue, URI, "userTypedValue matches test URI after switching tabs");
  is(gURLBar.value, URI, "location bar value matches test URI after switching tabs");

  waitForExplicitFinish();
  gBrowser.selectedBrowser.addEventListener("load", function () {
    is(gBrowser.userTypedValue, null, "userTypedValue is null as the page has loaded");
    is(gURLBar.value, URI, "location bar value matches test URI as the page has loaded");
    gBrowser.removeCurrentTab();
    finish();
  }, true);
}
