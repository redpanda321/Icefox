do_load_httpd_js();

var httpServer = null;
// Need to randomize, because apparently no one clears our cache
var randomPath = "/redirect/" + Math.random();
var randomURI = "http://localhost:4444" + randomPath;

function make_channel(url, callback, ctx) {
  var ios = Cc["@mozilla.org/network/io-service;1"].
            getService(Ci.nsIIOService);
  return ios.newChannel(url, "", null);
}

function redirectHandler(metadata, response)
{
  response.setStatusLine(metadata.httpVersion, 301, "Moved");
  response.setHeader("Location", "httpx://localhost:4444/content", false);
  response.setHeader("Cache-Control", "no-cache", false);
}

function finish_test(request, buffer)
{
  do_check_eq(buffer, "");
  httpServer.stop(do_test_finished);
}

function run_test()
{
  httpServer = new nsHttpServer();
  httpServer.registerPathHandler(randomPath, redirectHandler);
  httpServer.start(4444);

  var chan = make_channel(randomURI);
  chan.asyncOpen(new ChannelListener(finish_test), null);
  do_test_pending();
}
