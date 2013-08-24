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
    test_historymode_retention("remember", undefined),
    test_historymode_retention("dontremember", "remember"),
    test_historymode_retention("custom", "dontremember"),
    // custom without any micro-prefs changed won't retain
    test_historymode_retention("remember", "dontremember"),
    test_historymode_retention("custom", "remember"),
    // custom without any micro-prefs changed won't retain
    test_historymode_retention("remember", "remember"),

    // reset all preferences to their default values once we're done
    reset_preferences
  ]);
}
