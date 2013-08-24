/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

function run_test() {
  var cm = Cc["@mozilla.org/cookiemanager;1"].getService(Ci.nsICookieManager2);
  var prefs = Cc["@mozilla.org/preferences-service;1"].getService(Ci.nsIPrefBranch);
  var pb = null;
  try {
    pb = Cc["@mozilla.org/privatebrowsing;1"].getService(Ci.nsIPrivateBrowsingService);
  } catch (e) {}

  prefs.setBoolPref("browser.privatebrowsing.keep_current_session", true);

  // accept all cookies and clear the table
  prefs.setIntPref("network.cookie.lifetimePolicy", 0);
  cm.removeAll();

  // saturate the cookie table
  addCookies(0, 5000);

  // check how many cookies we have
  var count = getCookieCount();
  do_check_neq(count, 0);

  // if private browsing is available
  if (pb) {
    // enter private browsing mode
    pb.privateBrowsingEnabled = true;

    // check that we have zero cookies
    do_check_eq(getCookieCount(), 0);

    // saturate the cookie table again
    addCookies(5000, 5000);

    // check we have the same number of cookies
    do_check_eq(getCookieCount(), count);

    // remove them all
    cm.removeAll();
    do_check_eq(getCookieCount(), 0);

    // leave private browsing mode
    pb.privateBrowsingEnabled = false;
  }

  // make sure our cookies are back
  do_check_eq(getCookieCount(), count);

  // set a few more, to trigger a purge
  addCookies(10000, 1000);

  // check we have the same number of cookies
  var count = getCookieCount();
  do_check_eq(getCookieCount(), count);

  // remove them all
  cm.removeAll();
  do_check_eq(getCookieCount(), 0);

  prefs.clearUserPref("browser.privatebrowsing.keep_current_session");
}

function getCookieCount() {
  var count = 0;
  var cm = Cc["@mozilla.org/cookiemanager;1"].getService(Ci.nsICookieManager2);
  var enumerator = cm.enumerator;
  while (enumerator.hasMoreElements()) {
    if (!(enumerator.getNext() instanceof Ci.nsICookie2))
      throw new Error("not a cookie");
    ++count;
  }
  return count;
}

function addCookies(start, count) {
  var cm = Cc["@mozilla.org/cookiemanager;1"].getService(Ci.nsICookieManager2);
  var expiry = (Date.now() + 1000) * 1000;
  for (var i = start; i < start + count; ++i)
    cm.add(i + ".bar", "", "foo", "bar", false, false, true, expiry);
}

