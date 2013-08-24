/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

// Test clearing plugin data by domain using nsPrivateBrowsingService.
const testURL = "http://mochi.test:8888/browser/browser/components/privatebrowsing/test/browser/browser_privatebrowsing_clearplugindata.html";

const pluginHostIface = Ci.nsIPluginHost;
var pluginHost = Cc["@mozilla.org/plugin/host;1"].getService(Ci.nsIPluginHost);
pluginHost.QueryInterface(pluginHostIface);

var pluginTag;

function stored(needles) {
  var something = pluginHost.siteHasData(this.pluginTag, null);
  if (!needles)
    return something;

  if (!something)
    return false;

  for (var i = 0; i < needles.length; ++i) {
    if (!pluginHost.siteHasData(this.pluginTag, needles[i]))
      return false;
  }
  return true;
}

function test() {
  waitForExplicitFinish();

  var tags = pluginHost.getPluginTags();

  // Find the test plugin
  for (var i = 0; i < tags.length; i++)
  {
    if (tags[i].name == "Test Plug-in")
    {
      pluginTag = tags[i];
    }
  }

  executeSoon(do_test);
}

function setFinishedCallback(callback)
{
  let testPage = gBrowser.selectedBrowser.contentWindow.wrappedJSObject;
  testPage.testFinishedCallback = function() {
    setTimeout(function() {
      info("got finished callback");
      callback();
    }, 0);
  }
}

function do_test()
{
  // Load page to set data for the plugin.
  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.selectedBrowser.addEventListener("load", function () {
    gBrowser.selectedBrowser.removeEventListener("load", arguments.callee, true);

    setFinishedCallback(function() {
      ok(stored(["192.168.1.1","foo.com","nonexistent.foo.com","bar.com","localhost"]),
        "Data stored for sites");

      let pb = Cc["@mozilla.org/privatebrowsing;1"].
               getService(Ci.nsIPrivateBrowsingService);

      // Clear data for "foo.com" and its subdomains.
      pb.removeDataFromDomain("foo.com");
      ok(stored(["bar.com","192.168.1.1","localhost"]), "Data stored for sites");
      ok(!stored(["foo.com"]), "Data cleared for foo.com");
      ok(!stored(["bar.foo.com"]), "Data cleared for subdomains of foo.com");

      // Clear data for "bar.com" using a subdomain.
      pb.removeDataFromDomain("foo.bar.com");
      ok(!stored(["bar.com"]), "Data cleared for bar.com");

      // Clear data for "192.168.1.1".
      pb.removeDataFromDomain("192.168.1.1");
      ok(!stored(["192.168.1.1"]), "Data cleared for 192.168.1.1");

      // Clear data for "localhost".
      pb.removeDataFromDomain("localhost");
      ok(!stored(null), "All data cleared");

      gBrowser.removeCurrentTab();

      executeSoon(finish);
    });
  }, true);
  content.location = testURL;
}

