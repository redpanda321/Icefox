/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const TAB_STATE_NEEDS_RESTORE = 1;
const TAB_STATE_RESTORING = 2;

let ss = Cc["@mozilla.org/browser/sessionstore;1"].getService(Ci.nsISessionStore);

// Some tests here assume that all restored tabs are loaded without waiting for
// the user to bring them to the foreground. We ensure this by resetting the
// related preference (see the "firefox.js" defaults file for details).
Services.prefs.setBoolPref("browser.sessionstore.restore_on_demand", false);
registerCleanupFunction(function () {
  Services.prefs.clearUserPref("browser.sessionstore.restore_on_demand");
});

// This kicks off the search service used on about:home and allows the
// session restore tests to be run standalone without triggering errors.
Cc["@mozilla.org/browser/clh;1"].getService(Ci.nsIBrowserHandler).defaultArgs;

function provideWindow(aCallback, aURL, aFeatures) {
  function callbackSoon(aWindow) {
    executeSoon(function executeCallbackSoon() {
      aCallback(aWindow);
    });
  }

  let win = openDialog(getBrowserURL(), "", aFeatures || "chrome,all,dialog=no", aURL);
  whenWindowLoaded(win, function onWindowLoaded(aWin) {
    if (!aURL) {
      info("Loaded a blank window.");
      callbackSoon(aWin);
      return;
    }

    aWin.gBrowser.selectedBrowser.addEventListener("load", function selectedBrowserLoadListener() {
      aWin.gBrowser.selectedBrowser.removeEventListener("load", selectedBrowserLoadListener, true);
      callbackSoon(aWin);
    }, true);
  });
}

// This assumes that tests will at least have some state/entries
function waitForBrowserState(aState, aSetStateCallback) {
  let windows = [window];
  let tabsRestored = 0;
  let expectedTabsRestored = 0;
  let expectedWindows = aState.windows.length;
  let windowsOpen = 1;
  let listening = false;
  let windowObserving = false;
  let restoreHiddenTabs = Services.prefs.getBoolPref(
                          "browser.sessionstore.restore_hidden_tabs");

  aState.windows.forEach(function (winState) {
    winState.tabs.forEach(function (tabState) {
      if (restoreHiddenTabs || !tabState.hidden)
        expectedTabsRestored++;
    });
  });

  // There must be only hidden tabs and restoreHiddenTabs = false. We still
  // expect one of them to be restored because it gets shown automatically.
  if (!expectedTabsRestored)
    expectedTabsRestored = 1;

  function onSSTabRestored(aEvent) {
    if (++tabsRestored == expectedTabsRestored) {
      // Remove the event listener from each window
      windows.forEach(function(win) {
        win.gBrowser.tabContainer.removeEventListener("SSTabRestored", onSSTabRestored, true);
      });
      listening = false;
      info("running " + aSetStateCallback.name);
      executeSoon(aSetStateCallback);
    }
  }

  // Used to add our listener to further windows so we can catch SSTabRestored
  // coming from them when creating a multi-window state.
  function windowObserver(aSubject, aTopic, aData) {
    if (aTopic == "domwindowopened") {
      let newWindow = aSubject.QueryInterface(Ci.nsIDOMWindow);
      newWindow.addEventListener("load", function() {
        newWindow.removeEventListener("load", arguments.callee, false);

        if (++windowsOpen == expectedWindows) {
          Services.ww.unregisterNotification(windowObserver);
          windowObserving = false;
        }

        // Track this window so we can remove the progress listener later
        windows.push(newWindow);
        // Add the progress listener
        newWindow.gBrowser.tabContainer.addEventListener("SSTabRestored", onSSTabRestored, true);
      }, false);
    }
  }

  // We only want to register the notification if we expect more than 1 window
  if (expectedWindows > 1) {
    registerCleanupFunction(function() {
      if (windowObserving) {
        Services.ww.unregisterNotification(windowObserver);
      }
    });
    windowObserving = true;
    Services.ww.registerNotification(windowObserver);
  }

  registerCleanupFunction(function() {
    if (listening) {
      windows.forEach(function(win) {
        win.gBrowser.tabContainer.removeEventListener("SSTabRestored", onSSTabRestored, true);
      });
    }
  });
  // Add the event listener for this window as well.
  listening = true;
  gBrowser.tabContainer.addEventListener("SSTabRestored", onSSTabRestored, true);

  // Finally, call setBrowserState
  ss.setBrowserState(JSON.stringify(aState));
}

// Doesn't assume that the tab needs to be closed in a cleanup function.
// If that's the case, the test author should handle that in the test.
function waitForTabState(aTab, aState, aCallback) {
  let listening = true;

  function onSSTabRestored() {
    aTab.removeEventListener("SSTabRestored", onSSTabRestored, false);
    listening = false;
    aCallback();
  }

  aTab.addEventListener("SSTabRestored", onSSTabRestored, false);

  registerCleanupFunction(function() {
    if (listening) {
      aTab.removeEventListener("SSTabRestored", onSSTabRestored, false);
    }
  });
  ss.setTabState(aTab, JSON.stringify(aState));
}

// waitForSaveState waits for a state write but not necessarily for the state to
// turn dirty.
function waitForSaveState(aSaveStateCallback) {
  let observing = false;
  let topic = "sessionstore-state-write";

  let sessionSaveTimeout = 1000 +
    Services.prefs.getIntPref("browser.sessionstore.interval");

  function removeObserver() {
    if (!observing)
      return;
    Services.obs.removeObserver(observer, topic, false);
    observing = false;
  }

  let timeout = setTimeout(function () {
    removeObserver();
    aSaveStateCallback();
  }, sessionSaveTimeout);

  function observer(aSubject, aTopic, aData) {
    removeObserver();
    timeout = clearTimeout(timeout);
    executeSoon(aSaveStateCallback);
  }

  registerCleanupFunction(function() {
    removeObserver();
    if (timeout) {
      clearTimeout(timeout);
    }
  });

  observing = true;
  Services.obs.addObserver(observer, topic, false);
};

function whenBrowserLoaded(aBrowser, aCallback) {
  aBrowser.addEventListener("load", function onLoad() {
    aBrowser.removeEventListener("load", onLoad, true);
    executeSoon(aCallback);
  }, true);
}

function whenWindowLoaded(aWindow, aCallback) {
  aWindow.addEventListener("load", function windowLoadListener() {
    aWindow.removeEventListener("load", windowLoadListener, false);
    executeSoon(function executeWhenWindowLoaded() {
      aCallback(aWindow);
    });
  }, false);
}

var gUniqueCounter = 0;
function r() {
  return Date.now() + "-" + (++gUniqueCounter);
}

function BrowserWindowIterator() {
  let windowsEnum = Services.wm.getEnumerator("navigator:browser");
  while (windowsEnum.hasMoreElements()) {
    let currentWindow = windowsEnum.getNext();
    if (!currentWindow.closed) {
      yield currentWindow;
    }
  }
}

let gProgressListener = {
  _callback: null,
  _checkRestoreState: true,

  setCallback: function gProgressListener_setCallback(aCallback, aCheckRestoreState = true) {
    if (!this._callback) {
      window.gBrowser.addTabsProgressListener(this);
    }
    this._callback = aCallback;
    this._checkRestoreState = aCheckRestoreState;
  },

  unsetCallback: function gProgressListener_unsetCallback() {
    if (this._callback) {
      this._callback = null;
      window.gBrowser.removeTabsProgressListener(this);
    }
  },

  onStateChange:
  function gProgressListener_onStateChange(aBrowser, aWebProgress, aRequest,
                                           aStateFlags, aStatus) {
    if ((!this._checkRestoreState ||
         aBrowser.__SS_restoreState == TAB_STATE_RESTORING) &&
        aStateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
        aStateFlags & Ci.nsIWebProgressListener.STATE_IS_NETWORK &&
        aStateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW) {
      let args = [aBrowser].concat(this._countTabs());
      this._callback.apply(this, args);
    }
  },

  _countTabs: function gProgressListener_countTabs() {
    let needsRestore = 0, isRestoring = 0, wasRestored = 0;

    for (let win in BrowserWindowIterator()) {
      for (let i = 0; i < win.gBrowser.tabs.length; i++) {
        let browser = win.gBrowser.tabs[i].linkedBrowser;
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
};

registerCleanupFunction(function () {
  gProgressListener.unsetCallback();
});

// Close everything but our primary window. We can't use waitForFocus()
// because apparently it's buggy. See bug 599253.
function closeAllButPrimaryWindow() {
  for (let win in BrowserWindowIterator()) {
    if (win != window) {
      win.close();
    }
  }
}

function whenNewWindowLoaded(aIsPrivate, aCallback) {
  let win = OpenBrowserWindow({private: aIsPrivate});
  win.addEventListener("load", function onLoad() {
    win.removeEventListener("load", onLoad, false);
    aCallback(win);
  }, false);
}
