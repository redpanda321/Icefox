/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

let stateBackup = ss.getBrowserState();

const TAB_STATE_NEEDS_RESTORE = 1;
const TAB_STATE_RESTORING = 2;

function test() {
  /** Test for Bug 586068 - Cascade page loads when restoring **/
  waitForExplicitFinish();
  // This test does a lot of window opening / closing and waiting for loads.
  // In order to prevent timeouts, we'll extend the default that mochitest uses.
  requestLongerTimeout(4);
  runNextTest();
}

// test_reloadCascade, test_reloadReload are generated tests that are run out
// of cycle (since they depend on current state). They're listed in [tests] here
// so that it is obvious when they run in respect to the other tests.
let tests = [test_cascade, test_select, test_multiWindowState,
             test_setWindowStateNoOverwrite, test_setWindowStateOverwrite,
             test_setBrowserStateInterrupted, test_reload,
             /* test_reloadReload, */ test_reloadCascadeSetup,
             /* test_reloadCascade, */ test_apptabs_only,
             test_restore_apptabs_ondemand];
function runNextTest() {
  // Reset the pref
  try {
    Services.prefs.clearUserPref("browser.sessionstore.restore_on_demand");
    Services.prefs.clearUserPref("browser.sessionstore.restore_pinned_tabs_on_demand");
  } catch (e) {}

  // set an empty state & run the next test, or finish
  if (tests.length) {
    // Enumerate windows and close everything but our primary window. We can't
    // use waitForFocus() because apparently it's buggy. See bug 599253.
    var windowsEnum = Services.wm.getEnumerator("navigator:browser");
    while (windowsEnum.hasMoreElements()) {
      var currentWindow = windowsEnum.getNext();
      if (currentWindow != window) {
        currentWindow.close();
      }
    }

    ss.setBrowserState(JSON.stringify({ windows: [{ tabs: [{ url: 'about:blank' }] }] }));
    let currentTest = tests.shift();
    info("running " + currentTest.name);
    executeSoon(currentTest);
  }
  else {
    ss.setBrowserState(stateBackup);
    executeSoon(finish);
  }
}


function test_cascade() {
  Services.prefs.setBoolPref("browser.sessionstore.restore_on_demand", false);

  // We have our own progress listener for this test, which we'll attach before our state is set
  let progressListener = {
    onStateChange: function (aBrowser, aWebProgress, aRequest, aStateFlags, aStatus) {
      dump("\n\nload: " + aBrowser.currentURI.spec + "\n" + JSON.stringify(countTabs()) + "\n\n");
      if (aBrowser.__SS_restoreState == TAB_STATE_RESTORING &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW)
        test_cascade_progressCallback();
    }
  }

  let state = { windows: [{ tabs: [
    { entries: [{ url: "http://example.com" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.com" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.com" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.com" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.com" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.com" }], extData: { "uniq": r() } }
  ] }] };

  let loadCount = 0;
  // Since our progress listener is fired before the one in sessionstore, our
  // expected counts look a little weird. This is because we inspect the state
  // before sessionstore has marked the tab as finished restoring and before it
  // starts restoring the next tab
  let expectedCounts = [
    [3, 3, 0],
    [2, 3, 1],
    [1, 3, 2],
    [0, 3, 3],
    [0, 2, 4],
    [0, 1, 5]
  ];

  function test_cascade_progressCallback() {
    loadCount++;
    let counts = countTabs();
    let expected = expectedCounts[loadCount - 1];

    is(counts[0], expected[0], "test_cascade: load " + loadCount + " - # tabs that need to be restored");
    is(counts[1], expected[1], "test_cascade: load " + loadCount + " - # tabs that are restoring");
    is(counts[2], expected[2], "test_cascade: load " + loadCount + " - # tabs that has been restored");

    if (loadCount < state.windows[0].tabs.length)
      return;

    window.gBrowser.removeTabsProgressListener(progressListener);
    runNextTest();
  }

  // This progress listener will get attached before the listener in session store.
  window.gBrowser.addTabsProgressListener(progressListener);
  ss.setBrowserState(JSON.stringify(state));
}


function test_select() {
  // Set the pref to true so we know exactly how many tabs should be restoring at
  // any given time. This guarantees that a finishing load won't start another.
  Services.prefs.setBoolPref("browser.sessionstore.restore_on_demand", true);

  // We have our own progress listener for this test, which we'll attach before our state is set
  let progressListener = {
    onStateChange: function (aBrowser, aWebProgress, aRequest, aStateFlags, aStatus) {
      if (aBrowser.__SS_restoreState == TAB_STATE_RESTORING &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW)
        test_select_progressCallback(aBrowser);
    }
  }

  let state = { windows: [{ tabs: [
    { entries: [{ url: "http://example.org" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org" }], extData: { "uniq": r() } }
  ], selected: 1 }] };

  let loadCount = 0;
  // expectedCounts looks a little wierd for the test case, but it works. See
  // comment in test_cascade for an explanation
  let expectedCounts = [
    [5, 1, 0],
    [4, 1, 1],
    [3, 1, 2],
    [2, 1, 3],
    [1, 1, 4],
    [0, 1, 5]
  ];
  let tabOrder = [0, 5, 1, 4, 3, 2];

  function test_select_progressCallback(aBrowser) {
    loadCount++;

    let counts = countTabs();
    let expected = expectedCounts[loadCount - 1];

    is(counts[0], expected[0], "test_select: load " + loadCount + " - # tabs that need to be restored");
    is(counts[1], expected[1], "test_select: load " + loadCount + " - # tabs that are restoring");
    is(counts[2], expected[2], "test_select: load " + loadCount + " - # tabs that has been restored");

    if (loadCount < state.windows[0].tabs.length) {
      // double check that this tab was the right one
      let expectedData = state.windows[0].tabs[tabOrder[loadCount - 1]].extData.uniq;
      let tab;
      for (let i = 0; i < window.gBrowser.tabs.length; i++) {
        if (!tab && window.gBrowser.tabs[i].linkedBrowser == aBrowser)
          tab = window.gBrowser.tabs[i];
      }
      is(ss.getTabValue(tab, "uniq"), expectedData, "test_select: load " + loadCount + " - correct tab was restored");

      // select the next tab
      window.gBrowser.selectTabAtIndex(tabOrder[loadCount]);
      return;
    }

    window.gBrowser.removeTabsProgressListener(progressListener);
    runNextTest();
  }

  window.gBrowser.addTabsProgressListener(progressListener);
  ss.setBrowserState(JSON.stringify(state));
}


function test_multiWindowState() {
  Services.prefs.setBoolPref("browser.sessionstore.restore_on_demand", false);

  // We have our own progress listener for this test, which we'll attach before our state is set
  let progressListener = {
    onStateChange: function (aBrowser, aWebProgress, aRequest, aStateFlags, aStatus) {
      // We only care about load events when the tab still has
      // __SS_restoreState == TAB_STATE_RESTORING on it.
      // Since our listener is attached before the sessionstore one, this works out.
      if (aBrowser.__SS_restoreState == TAB_STATE_RESTORING &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW)
        test_multiWindowState_progressCallback(aBrowser);
    }
  }

  // The first window will be put into the already open window and the second
  // window will be opened with _openWindowWithState, which is the source of the problem.
  let state = { windows: [
    {
      tabs: [
        { entries: [{ url: "http://example.org#0" }], extData: { "uniq": r() } }
      ],
      selected: 1
    },
    {
      tabs: [
        { entries: [{ url: "http://example.com#1" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.com#2" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.com#3" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.com#4" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.com#5" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.com#6" }], extData: { "uniq": r() } }
      ],
      selected: 4
    }
  ] };
  let numTabs = state.windows[0].tabs.length + state.windows[1].tabs.length;

  let loadCount = 0;
  function test_multiWindowState_progressCallback(aBrowser) {
    loadCount++;

    if (loadCount < numTabs)
      return;

    // We don't actually care about load order in this test, just that they all
    // do load.
    is(loadCount, numTabs, "test_multiWindowState: all tabs were restored");
    let count = countTabs();
    is(count[0], 0,
       "test_multiWindowState: there are no tabs left needing restore");

    // Remove the progress listener from this window, it will be removed from
    // theWin when that window is closed (in setBrowserState).
    window.gBrowser.removeTabsProgressListener(progressListener);
    runNextTest();
  }

  // We also want to catch the 2nd window, so we need to observe domwindowopened
  function windowObserver(aSubject, aTopic, aData) {
    let theWin = aSubject.QueryInterface(Ci.nsIDOMWindow);
    if (aTopic == "domwindowopened") {
      theWin.addEventListener("load", function() {
        theWin.removeEventListener("load", arguments.callee, false);

        Services.ww.unregisterNotification(windowObserver);
        theWin.gBrowser.addTabsProgressListener(progressListener);
      }, false);
    }
  }
  Services.ww.registerNotification(windowObserver);

  window.gBrowser.addTabsProgressListener(progressListener);
  ss.setBrowserState(JSON.stringify(state));
}


function test_setWindowStateNoOverwrite() {
  Services.prefs.setBoolPref("browser.sessionstore.restore_on_demand", false);

  // We have our own progress listener for this test, which we'll attach before our state is set
  let progressListener = {
    onStateChange: function (aBrowser, aWebProgress, aRequest, aStateFlags, aStatus) {
      // We only care about load events when the tab still has
      // __SS_restoreState == TAB_STATE_RESTORING on it.
      // Since our listener is attached before the sessionstore one, this works out.
      if (aBrowser.__SS_restoreState == TAB_STATE_RESTORING &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW)
        test_setWindowStateNoOverwrite_progressCallback(aBrowser);
    }
  }

  // We'll use 2 states so that we can make sure calling setWindowState doesn't
  // wipe out currently restoring data.
  let state1 = { windows: [{ tabs: [
    { entries: [{ url: "http://example.com#1" }] },
    { entries: [{ url: "http://example.com#2" }] },
    { entries: [{ url: "http://example.com#3" }] },
    { entries: [{ url: "http://example.com#4" }] },
    { entries: [{ url: "http://example.com#5" }] },
  ] }] };
  let state2 = { windows: [{ tabs: [
    { entries: [{ url: "http://example.org#1" }] },
    { entries: [{ url: "http://example.org#2" }] },
    { entries: [{ url: "http://example.org#3" }] },
    { entries: [{ url: "http://example.org#4" }] },
    { entries: [{ url: "http://example.org#5" }] }
  ] }] };

  let numTabs = state1.windows[0].tabs.length + state2.windows[0].tabs.length;

  let loadCount = 0;
  function test_setWindowStateNoOverwrite_progressCallback(aBrowser) {
    loadCount++;

    // When loadCount == 2, we'll also restore state2 into the window
    if (loadCount == 2)
      ss.setWindowState(window, JSON.stringify(state2), false);

    if (loadCount < numTabs)
      return;

    // We don't actually care about load order in this test, just that they all
    // do load.
    is(loadCount, numTabs, "test_setWindowStateNoOverwrite: all tabs were restored");
    // window.__SS_tabsToRestore isn't decremented until after the progress
    // listener is called. Since we get in here before that, we still expect
    // the count to be 1.
    is(window.__SS_tabsToRestore, 1,
       "test_setWindowStateNoOverwrite: window doesn't think there are more tabs to restore");
    let count = countTabs();
    is(count[0], 0,
       "test_setWindowStateNoOverwrite: there are no tabs left needing restore");

    // Remove the progress listener from this window, it will be removed from
    // theWin when that window is closed (in setBrowserState).
    window.gBrowser.removeTabsProgressListener(progressListener);

    runNextTest();
  }

  window.gBrowser.addTabsProgressListener(progressListener);
  ss.setWindowState(window, JSON.stringify(state1), true);
}


function test_setWindowStateOverwrite() {
  Services.prefs.setBoolPref("browser.sessionstore.restore_on_demand", false);

  // We have our own progress listener for this test, which we'll attach before our state is set
  let progressListener = {
    onStateChange: function (aBrowser, aWebProgress, aRequest, aStateFlags, aStatus) {
      // We only care about load events when the tab still has
      // __SS_restoreState == TAB_STATE_RESTORING on it.
      // Since our listener is attached before the sessionstore one, this works out.
      if (aBrowser.__SS_restoreState == TAB_STATE_RESTORING &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW)
        test_setWindowStateOverwrite_progressCallback(aBrowser);
    }
  }

  // We'll use 2 states so that we can make sure calling setWindowState doesn't
  // wipe out currently restoring data.
  let state1 = { windows: [{ tabs: [
    { entries: [{ url: "http://example.com#1" }] },
    { entries: [{ url: "http://example.com#2" }] },
    { entries: [{ url: "http://example.com#3" }] },
    { entries: [{ url: "http://example.com#4" }] },
    { entries: [{ url: "http://example.com#5" }] },
  ] }] };
  let state2 = { windows: [{ tabs: [
    { entries: [{ url: "http://example.org#1" }] },
    { entries: [{ url: "http://example.org#2" }] },
    { entries: [{ url: "http://example.org#3" }] },
    { entries: [{ url: "http://example.org#4" }] },
    { entries: [{ url: "http://example.org#5" }] }
  ] }] };

  let numTabs = 2 + state2.windows[0].tabs.length;

  let loadCount = 0;
  function test_setWindowStateOverwrite_progressCallback(aBrowser) {
    loadCount++;

    // When loadCount == 2, we'll also restore state2 into the window
    if (loadCount == 2)
      ss.setWindowState(window, JSON.stringify(state2), true);

    if (loadCount < numTabs)
      return;

    // We don't actually care about load order in this test, just that they all
    // do load.
    is(loadCount, numTabs, "test_setWindowStateOverwrite: all tabs were restored");
    // window.__SS_tabsToRestore isn't decremented until after the progress
    // listener is called. Since we get in here before that, we still expect
    // the count to be 1.
    is(window.__SS_tabsToRestore, 1,
       "test_setWindowStateOverwrite: window doesn't think there are more tabs to restore");
    let count = countTabs();
    is(count[0], 0,
       "test_setWindowStateOverwrite: there are no tabs left needing restore");

    // Remove the progress listener from this window, it will be removed from
    // theWin when that window is closed (in setBrowserState).
    window.gBrowser.removeTabsProgressListener(progressListener);

    runNextTest();
  }

  window.gBrowser.addTabsProgressListener(progressListener);
  ss.setWindowState(window, JSON.stringify(state1), true);
}


function test_setBrowserStateInterrupted() {
  Services.prefs.setBoolPref("browser.sessionstore.restore_on_demand", false);

  // We have our own progress listener for this test, which we'll attach before our state is set
  let progressListener = {
    onStateChange: function (aBrowser, aWebProgress, aRequest, aStateFlags, aStatus) {
      // We only care about load events when the tab still has
      // __SS_restoreState == TAB_STATE_RESTORING on it.
      // Since our listener is attached before the sessionstore one, this works out.
      if (aBrowser.__SS_restoreState == TAB_STATE_RESTORING &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW)
        test_setBrowserStateInterrupted_progressCallback(aBrowser);
    }
  }

  // The first state will be loaded using setBrowserState, followed by the 2nd
  // state also being loaded using setBrowserState, interrupting the first restore.
  let state1 = { windows: [
    {
      tabs: [
        { entries: [{ url: "http://example.org#1" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.org#2" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.org#3" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.org#4" }], extData: { "uniq": r() } }
      ],
      selected: 1
    },
    {
      tabs: [
        { entries: [{ url: "http://example.com#1" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.com#2" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.com#3" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.com#4" }], extData: { "uniq": r() } },
      ],
      selected: 3
    }
  ] };
  let state2 = { windows: [
    {
      tabs: [
        { entries: [{ url: "http://example.org#5" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.org#6" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.org#7" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.org#8" }], extData: { "uniq": r() } }
      ],
      selected: 3
    },
    {
      tabs: [
        { entries: [{ url: "http://example.com#5" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.com#6" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.com#7" }], extData: { "uniq": r() } },
        { entries: [{ url: "http://example.com#8" }], extData: { "uniq": r() } },
      ],
      selected: 1
    }
  ] };

  // interruptedAfter will be set after the selected tab from each window have loaded.
  let interruptedAfter = 0;
  let loadedWindow1 = false;
  let loadedWindow2 = false;
  let numTabs = state2.windows[0].tabs.length + state2.windows[1].tabs.length;

  let loadCount = 0;
  function test_setBrowserStateInterrupted_progressCallback(aBrowser) {
    loadCount++;

    if (aBrowser.currentURI.spec == state1.windows[0].tabs[2].entries[0].url)
      loadedWindow1 = true;
    if (aBrowser.currentURI.spec == state1.windows[1].tabs[0].entries[0].url)
      loadedWindow2 = true;

    if (!interruptedAfter && loadedWindow1 && loadedWindow2) {
      interruptedAfter = loadCount;
      ss.setBrowserState(JSON.stringify(state2));
      return;
    }

    if (loadCount < numTabs + interruptedAfter)
      return;

    // We don't actually care about load order in this test, just that they all
    // do load.
    is(loadCount, numTabs + interruptedAfter,
       "test_setBrowserStateInterrupted: all tabs were restored");
    let count = countTabs();
    is(count[0], 0,
       "test_setBrowserStateInterrupted: there are no tabs left needing restore");

    // Remove the progress listener from this window, it will be removed from
    // theWin when that window is closed (in setBrowserState).
    window.gBrowser.removeTabsProgressListener(progressListener);
    Services.ww.unregisterNotification(windowObserver);
    runNextTest();
  }

  // We also want to catch the extra windows (there should be 2), so we need to observe domwindowopened
  function windowObserver(aSubject, aTopic, aData) {
    let theWin = aSubject.QueryInterface(Ci.nsIDOMWindow);
    if (aTopic == "domwindowopened") {
      theWin.addEventListener("load", function() {
        theWin.removeEventListener("load", arguments.callee, false);

        Services.ww.unregisterNotification(windowObserver);
        theWin.gBrowser.addTabsProgressListener(progressListener);
      }, false);
    }
  }
  Services.ww.registerNotification(windowObserver);

  window.gBrowser.addTabsProgressListener(progressListener);
  ss.setBrowserState(JSON.stringify(state1));
}


function test_reload() {
  // Set the pref to true so we know exactly how many tabs should be restoring at
  // any given time. This guarantees that a finishing load won't start another.
  Services.prefs.setBoolPref("browser.sessionstore.restore_on_demand", true);

  // We have our own progress listener for this test, which we'll attach before our state is set
  let progressListener = {
    onStateChange: function (aBrowser, aWebProgress, aRequest, aStateFlags, aStatus) {
      if (aBrowser.__SS_restoreState == TAB_STATE_RESTORING &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW)
        test_reload_progressCallback(aBrowser);
    }
  }

  let state = { windows: [{ tabs: [
    { entries: [{ url: "http://example.org/#1" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#2" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#3" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#4" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#5" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#6" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#7" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#8" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#9" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#10" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#11" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#12" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#13" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#14" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#15" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#16" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#17" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#18" }], extData: { "uniq": r() } }
  ], selected: 1 }] };

  let loadCount = 0;
  function test_reload_progressCallback(aBrowser) {
    loadCount++;

    is(aBrowser.currentURI.spec, state.windows[0].tabs[loadCount - 1].entries[0].url,
       "test_reload: load " + loadCount + " - browser loaded correct url");

    if (loadCount <= state.windows[0].tabs.length) {
      // double check that this tab was the right one
      let expectedData = state.windows[0].tabs[loadCount - 1].extData.uniq;
      let tab;
      for (let i = 0; i < window.gBrowser.tabs.length; i++) {
        if (!tab && window.gBrowser.tabs[i].linkedBrowser == aBrowser)
          tab = window.gBrowser.tabs[i];
      }
      is(ss.getTabValue(tab, "uniq"), expectedData,
         "test_reload: load " + loadCount + " - correct tab was restored");

      if (loadCount == state.windows[0].tabs.length) {
        window.gBrowser.removeTabsProgressListener(progressListener);
        executeSoon(function() {
          _test_reloadAfter("test_reloadReload", state, runNextTest);
        });
      }
      else {
        // reload the next tab
        window.gBrowser.reloadTab(window.gBrowser.tabs[loadCount]);
      }
    }

  }

  window.gBrowser.addTabsProgressListener(progressListener);
  ss.setBrowserState(JSON.stringify(state));
}


// This doesn't actually test anything, just does a cascaded restore with default
// settings. This really just sets up to test that reloads work.
function test_reloadCascadeSetup() {
  Services.prefs.setBoolPref("browser.sessionstore.restore_on_demand", false);

  // We have our own progress listener for this test, which we'll attach before our state is set
  let progressListener = {
    onStateChange: function (aBrowser, aWebProgress, aRequest, aStateFlags, aStatus) {
      if (aBrowser.__SS_restoreState == TAB_STATE_RESTORING &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW)
        test_cascadeReloadSetup_progressCallback();
    }
  }

  let state = { windows: [{ tabs: [
    { entries: [{ url: "http://example.com/#1" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.com/#2" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.com/#3" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.com/#4" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.com/#5" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.com/#6" }], extData: { "uniq": r() } }
  ] }] };

  let loadCount = 0;
  function test_cascadeReloadSetup_progressCallback() {
    loadCount++;
    if (loadCount < state.windows[0].tabs.length)
      return;

    window.gBrowser.removeTabsProgressListener(progressListener);
    executeSoon(function() {
      _test_reloadAfter("test_reloadCascade", state, runNextTest);
    });
  }

  // This progress listener will get attached before the listener in session store.
  window.gBrowser.addTabsProgressListener(progressListener);
  ss.setBrowserState(JSON.stringify(state));
}


// This is a generic function that will attempt to reload each test. We do this
// a couple times, so make it utilitarian.
// This test expects that aState contains a single window and that each tab has
// a unique extData value eg. { "uniq": value }.
function _test_reloadAfter(aTestName, aState, aCallback) {
  info("starting " + aTestName);
  let progressListener = {
    onStateChange: function (aBrowser, aWebProgress, aRequest, aStateFlags, aStatus) {
      if (aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW)
        test_reloadAfter_progressCallback(aBrowser);
    }
  }

  // Simulate a left mouse button click with no modifiers, which is what
  // Command-R, or clicking reload does.
  let fakeEvent = {
    button: 0,
    metaKey: false,
    altKey: false,
    ctrlKey: false,
    shiftKey: false,
  }

  let loadCount = 0;
  function test_reloadAfter_progressCallback(aBrowser) {
    loadCount++;

    if (loadCount <= aState.windows[0].tabs.length) {
      // double check that this tab was the right one
      let expectedData = aState.windows[0].tabs[loadCount - 1].extData.uniq;
      let tab;
      for (let i = 0; i < window.gBrowser.tabs.length; i++) {
        if (!tab && window.gBrowser.tabs[i].linkedBrowser == aBrowser)
          tab = window.gBrowser.tabs[i];
      }
      is(ss.getTabValue(tab, "uniq"), expectedData,
         aTestName + ": load " + loadCount + " - correct tab was reloaded");

      if (loadCount == aState.windows[0].tabs.length) {
        window.gBrowser.removeTabsProgressListener(progressListener);
        aCallback();
      }
      else {
        // reload the next tab
        window.gBrowser.selectTabAtIndex(loadCount);
        BrowserReloadOrDuplicate(fakeEvent);
      }
    }
  }

  window.gBrowser.addTabsProgressListener(progressListener);
  BrowserReloadOrDuplicate(fakeEvent);
}


// This test ensures that app tabs are restored regardless of restore_on_demand
function test_apptabs_only() {
  // Set the pref to true so we know exactly how many tabs should be restoring at
  // any given time. This guarantees that a finishing load won't start another.
  Services.prefs.setBoolPref("browser.sessionstore.restore_on_demand", true);

  // We have our own progress listener for this test, which we'll attach before our state is set
  let progressListener = {
    onStateChange: function (aBrowser, aWebProgress, aRequest, aStateFlags, aStatus) {
      if (aBrowser.__SS_restoreState == TAB_STATE_RESTORING &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW)
        test_apptabs_only_progressCallback(aBrowser);
    }
  }

  let state = { windows: [{ tabs: [
    { entries: [{ url: "http://example.org/#1" }], extData: { "uniq": r() }, pinned: true },
    { entries: [{ url: "http://example.org/#2" }], extData: { "uniq": r() }, pinned: true },
    { entries: [{ url: "http://example.org/#3" }], extData: { "uniq": r() }, pinned: true },
    { entries: [{ url: "http://example.org/#4" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#5" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#6" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#7" }], extData: { "uniq": r() } },
  ], selected: 5 }] };

  let loadCount = 0;
  function test_apptabs_only_progressCallback(aBrowser) {
    loadCount++;

    // We'll make sure that the loads we get come from pinned tabs or the
    // the selected tab.

    // get the tab
    let tab;
    for (let i = 0; i < window.gBrowser.tabs.length; i++) {
      if (!tab && window.gBrowser.tabs[i].linkedBrowser == aBrowser)
        tab = window.gBrowser.tabs[i];
    }

    ok(tab.pinned || gBrowser.selectedTab == tab,
       "test_apptabs_only: load came from pinned or selected tab");

    // We should get 4 loads: 3 app tabs + 1 normal selected tab
    if (loadCount < 4)
      return;

    window.gBrowser.removeTabsProgressListener(progressListener);
    runNextTest();
  }

  window.gBrowser.addTabsProgressListener(progressListener);
  ss.setBrowserState(JSON.stringify(state));
}


// This test ensures that app tabs are not restored when restore_pinned_tabs_on_demand is set
function test_restore_apptabs_ondemand() {
  Services.prefs.setBoolPref("browser.sessionstore.restore_on_demand", true);
  Services.prefs.setBoolPref("browser.sessionstore.restore_pinned_tabs_on_demand", true);

  // We have our own progress listener for this test, which we'll attach before our state is set
  let progressListener = {
    onStateChange: function (aBrowser, aWebProgress, aRequest, aStateFlags, aStatus) {
      if (aBrowser.__SS_restoreState == TAB_STATE_RESTORING &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK &&
          aStateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW)
        test_restore_apptabs_ondemand_progressCallback(aBrowser);
    }
  }

  let state = { windows: [{ tabs: [
    { entries: [{ url: "http://example.org/#1" }], extData: { "uniq": r() }, pinned: true },
    { entries: [{ url: "http://example.org/#2" }], extData: { "uniq": r() }, pinned: true },
    { entries: [{ url: "http://example.org/#3" }], extData: { "uniq": r() }, pinned: true },
    { entries: [{ url: "http://example.org/#4" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#5" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#6" }], extData: { "uniq": r() } },
    { entries: [{ url: "http://example.org/#7" }], extData: { "uniq": r() } },
  ], selected: 5 }] };

  let loadCount = 0;
  let nextTestTimer;
  function test_restore_apptabs_ondemand_progressCallback(aBrowser) {
    loadCount++;

    // get the tab
    let tab;
    for (let i = 0; i < window.gBrowser.tabs.length; i++) {
      if (!tab && window.gBrowser.tabs[i].linkedBrowser == aBrowser)
        tab = window.gBrowser.tabs[i];
    }

    // Check that the load only comes from the selected tab.
    ok(gBrowser.selectedTab == tab,
       "test_restore_apptabs_ondemand: load came from selected tab");

    // We should get only 1 load: the selected tab
    if (loadCount == 1) {
      nextTestTimer = setTimeout(nextTest, 1000);
      return;
    }
    else if (loadCount > 1) {
      clearTimeout(nextTestTimer);
    }

    function nextTest() {
      window.gBrowser.removeTabsProgressListener(progressListener);
      runNextTest();
    }
    nextTest();
  }

  window.gBrowser.addTabsProgressListener(progressListener);
  ss.setBrowserState(JSON.stringify(state));
}


function countTabs() {
  let needsRestore = 0,
      isRestoring = 0,
      wasRestored = 0;

  let windowsEnum = Services.wm.getEnumerator("navigator:browser");

  while (windowsEnum.hasMoreElements()) {
    let window = windowsEnum.getNext();
    if (window.closed)
      continue;

    for (let i = 0; i < window.gBrowser.tabs.length; i++) {
      let browser = window.gBrowser.tabs[i].linkedBrowser;
      if (browser.__SS_restoreState == TAB_STATE_RESTORING)
        isRestoring++;
      else if (browser.__SS_restoreState == TAB_STATE_NEEDS_RESTORE)
        needsRestore++;
      else
        wasRestored++;
    }
  }
  return [needsRestore, isRestoring, wasRestored];
}

