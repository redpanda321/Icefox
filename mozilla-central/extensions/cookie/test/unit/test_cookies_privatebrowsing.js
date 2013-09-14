/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Test private browsing mode.

let test_generator = do_run_test();

function run_test() {
  do_test_pending();
  do_run_generator(test_generator);
}

function finish_test() {
  do_execute_soon(function() {
    test_generator.close();
    do_test_finished();
  });
}

function make_channel(url) {
  var ios = Cc["@mozilla.org/network/io-service;1"].getService(Ci.nsIIOService);
  var chan = ios.newChannel(url, null, null).QueryInterface(Ci.nsIHttpChannel);
  return chan;
}

function do_run_test() {
  // Set up a profile.
  let profile = do_get_profile();

  // Make sure the private browsing service is available.
  try {
    Services.pb;
  } catch (e) {
    finish_test();
    return;
  }

  // Tell the private browsing service to not attempt to restore window state.
  Services.prefs.setBoolPref("browser.privatebrowsing.keep_current_session",
    true);

  // Test with cookies enabled.
  Services.prefs.setIntPref("network.cookie.cookieBehavior", 0);

  // Create URIs pointing to foo.com and bar.com.
  let uri1 = NetUtil.newURI("http://foo.com/foo.html");
  let uri2 = NetUtil.newURI("http://bar.com/bar.html");

  // Set a cookie for host 1.
  Services.cookies.setCookieString(uri1, null, "oh=hai; max-age=1000", null);
  do_check_eq(Services.cookiemgr.countCookiesFromHost(uri1.host), 1);

  // Enter private browsing mode, set a cookie for host 2, and check the counts.
  var chan1 = make_channel(uri1.spec);
  chan1.QueryInterface(Ci.nsIPrivateBrowsingChannel);
  chan1.setPrivate(true);

  var chan2 = make_channel(uri2.spec);
  chan2.QueryInterface(Ci.nsIPrivateBrowsingChannel);
  chan2.setPrivate(true);

  Services.cookies.setCookieString(uri2, null, "oh=hai; max-age=1000", chan2);
  do_check_eq(Services.cookiemgr.getCookieString(uri1, chan1), null);
  do_check_eq(Services.cookiemgr.getCookieString(uri2, chan2), "oh=hai");

  // Remove cookies and check counts.
  Services.obs.notifyObservers(null, "last-pb-context-exited", null);
  do_check_eq(Services.cookiemgr.getCookieString(uri1, chan1), null);
  do_check_eq(Services.cookiemgr.getCookieString(uri2, chan2), null);

  Services.cookies.setCookieString(uri2, null, "oh=hai; max-age=1000", chan2);
  do_check_eq(Services.cookiemgr.getCookieString(uri2, chan2), "oh=hai");

  // Leave private browsing mode and check counts.
  Services.obs.notifyObservers(null, "last-pb-context-exited", null);
  do_check_eq(Services.cookiemgr.countCookiesFromHost(uri1.host), 1);
  do_check_eq(Services.cookiemgr.countCookiesFromHost(uri2.host), 0);

  // Fake a profile change.
  do_close_profile(test_generator);
  yield;
  do_load_profile();

  // Check that the right cookie persisted.
  do_check_eq(Services.cookiemgr.countCookiesFromHost(uri1.host), 1);
  do_check_eq(Services.cookiemgr.countCookiesFromHost(uri2.host), 0);

  // Enter private browsing mode, set a cookie for host 2, and check the counts.
  do_check_eq(Services.cookiemgr.getCookieString(uri1, chan1), null);
  do_check_eq(Services.cookiemgr.getCookieString(uri2, chan2), null);
  Services.cookies.setCookieString(uri2, null, "oh=hai; max-age=1000", chan2);
  do_check_eq(Services.cookiemgr.getCookieString(uri2, chan2), "oh=hai");

  // Fake a profile change.
  do_close_profile(test_generator);
  yield;
  do_load_profile();

  // We're still in private browsing mode, but should have a new session.
  // Check counts.
  do_check_eq(Services.cookiemgr.getCookieString(uri1, chan1), null);
  do_check_eq(Services.cookiemgr.getCookieString(uri2, chan2), null);

  // Leave private browsing mode and check counts.
  Services.obs.notifyObservers(null, "last-pb-context-exited", null);
  do_check_eq(Services.cookiemgr.countCookiesFromHost(uri1.host), 1);
  do_check_eq(Services.cookiemgr.countCookiesFromHost(uri2.host), 0);

  // Enter private browsing mode.

  // Fake a profile change, but wait for async read completion.
  do_close_profile(test_generator);
  yield;
  do_load_profile(test_generator);
  yield;

  // We're still in private browsing mode, but should have a new session.
  // Check counts.
  do_check_eq(Services.cookiemgr.getCookieString(uri1, chan1), null);
  do_check_eq(Services.cookiemgr.getCookieString(uri2, chan2), null);

  // Leave private browsing mode and check counts.
  Services.obs.notifyObservers(null, "last-pb-context-exited", null);
  do_check_eq(Services.cookiemgr.countCookiesFromHost(uri1.host), 1);
  do_check_eq(Services.cookiemgr.countCookiesFromHost(uri2.host), 0);

  finish_test();
}
