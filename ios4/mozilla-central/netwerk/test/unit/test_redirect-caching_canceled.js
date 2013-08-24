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

const responseBody = "response body";

function redirectHandler(metadata, response)
{
  response.setStatusLine(metadata.httpVersion, 301, "Moved");
  response.setHeader("Location", "http://localhost:4444/content", false);
  response.setHeader("Cache-control", "max-age=1000", false);
  return;
}

function contentHandler(metadata, response)
{
  response.setHeader("Content-Type", "text/plain");
  response.bodyOutputStream.write(responseBody, responseBody.length);
}

function firstTimeThrough(request, buffer)
{
  do_check_eq(buffer, responseBody);
  var chan = make_channel(randomURI);
  chan.asyncOpen(new ChannelListener(secondTimeThrough, null), null);
}

function secondTimeThrough(request, buffer)
{
  do_check_eq(buffer, responseBody);
  var chan = make_channel(randomURI);
  chan.loadFlags |= Ci.nsIRequest.LOAD_FROM_CACHE;
  chan.notificationCallbacks = new ChannelEventSink(ES_ABORT_REDIRECT);
  chan.asyncOpen(new ChannelListener(finish_test, null, CL_EXPECT_FAILURE), null);
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
  httpServer.registerPathHandler("/content", contentHandler);
  httpServer.start(4444);

  var chan = make_channel(randomURI);
  chan.asyncOpen(new ChannelListener(firstTimeThrough, null), null);
  do_test_pending();
}
