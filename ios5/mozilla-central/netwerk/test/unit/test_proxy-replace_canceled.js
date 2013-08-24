do_load_httpd_js();

var httpServer = null;

function make_channel(url, callback, ctx) {
  var ios = Cc["@mozilla.org/network/io-service;1"].
            getService(Ci.nsIIOService);
  return ios.newChannel(url, "", null);
}

const responseBody = "response body";

function contentHandler(metadata, response)
{
  response.setHeader("Content-Type", "text/plain");
  response.bodyOutputStream.write(responseBody, responseBody.length);
}

function finish_test(request, buffer)
{
  do_check_eq(buffer, "");
  httpServer.stop(do_test_finished);
}

function run_test()
{
  httpServer = new nsHttpServer();
  httpServer.registerPathHandler("/content", contentHandler);
  httpServer.start(4444);

  var prefserv = Cc["@mozilla.org/preferences-service;1"].
                 getService(Ci.nsIPrefService);
  var prefs = prefserv.getBranch("network.proxy.");
  prefs.setIntPref("type", 2);
  prefs.setCharPref("autoconfig_url", "data:text/plain," +
    "function FindProxyForURL(url, host) {return 'PROXY localhost:4444';}"
  );

  var chan = make_channel("http://localhost:4444/content");
  chan.notificationCallbacks = new ChannelEventSink(ES_ABORT_REDIRECT);
  chan.asyncOpen(new ChannelListener(finish_test, null, CL_EXPECT_FAILURE), null);
  do_test_pending();
}
