/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

let tempScope = {};
Components.utils.import("resource:///modules/devtools/Target.jsm", tempScope);
let TargetFactory = tempScope.TargetFactory;
Components.utils.import("resource://gre/modules/devtools/Console.jsm", tempScope);
let console = tempScope.console;
Components.utils.import("resource://gre/modules/commonjs/promise/core.js", tempScope);
let Promise = tempScope.Promise;

/**
 * Open a new tab at a URL and call a callback on load
 */
function addTab(aURL, aCallback)
{
  waitForExplicitFinish();

  gBrowser.selectedTab = gBrowser.addTab();
  if (aURL != null) {
    content.location = aURL;
  }

  let deferred = Promise.defer();

  let tab = gBrowser.selectedTab;
  let target = TargetFactory.forTab(gBrowser.selectedTab);
  let browser = gBrowser.getBrowserForTab(tab);

  function onTabLoad() {
    browser.removeEventListener("load", onTabLoad, true);

    if (aCallback != null) {
      aCallback(browser, tab, browser.contentDocument);
    }

    deferred.resolve({ browser: browser, tab: tab, target: target });
  }

  browser.addEventListener("load", onTabLoad, true);
  return deferred.promise;
}

registerCleanupFunction(function tearDown() {
  while (gBrowser.tabs.length > 1) {
    gBrowser.removeCurrentTab();
  }
});
