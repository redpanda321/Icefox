/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

function test() {
  let loader = Cc["@mozilla.org/moz/jssubscript-loader;1"].
               getService(Ci.mozIJSSubScriptLoader);
  let rootDir = getRootDirectory(gTestPath);
  let jar = getJar(rootDir);
  if (jar) {
    let tmpdir = extractJarToTmp(jar);
    rootDir = "file://" + tmpdir.path + '/';
  }
  loader.loadSubScript(rootDir + "privacypane_tests.js", this);

  run_test_subset([
    test_custom_retention("acceptCookies", "remember"),
    test_custom_retention("acceptCookies", "custom"),
    test_custom_retention("acceptThirdParty", "remember"),
    test_custom_retention("acceptThirdParty", "custom"),
    test_custom_retention("keepCookiesUntil", "remember", 1),
    test_custom_retention("keepCookiesUntil", "custom", 2),
    test_custom_retention("keepCookiesUntil", "custom", 0),
    test_custom_retention("alwaysClear", "remember"),
    test_custom_retention("alwaysClear", "custom"),
    test_historymode_retention("remember", "remember"),

    // reset all preferences to their default values once we're done
    reset_preferences
  ]);
}
