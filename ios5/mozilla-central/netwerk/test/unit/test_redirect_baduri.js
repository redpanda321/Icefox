do_load_httpd_js();

/*
 * Test whether we fail bad URIs in HTTP redirect as CORRUPTED_CONTENT.
 */

var httpServer = null;

var BadRedirectPath = "/BadRedirect"; 
var BadRedirectURI = "http://localhost:4444" + BadRedirectPath;

function make_channel(url, callback, ctx) {
  var ios = Cc["@mozilla.org/network/io-service;1"].
            getService(Ci.nsIIOService);
  return ios.newChannel(url, "", null);
}

function BadRedirectHandler(metadata, response)
{
  response.setStatusLine(metadata.httpVersion, 301, "Moved");
  // '>' in URI will fail to parse: we should not render response
  response.setHeader("Location", 'http://localhost:4444>BadRedirect', false);
}

function checkFailed(request, buffer)
{
  do_check_eq(request.status, Components.results.NS_ERROR_CORRUPTED_CONTENT);

  httpServer.stop(do_test_finished);
}

function run_test()
{
  httpServer = new nsHttpServer();
  httpServer.registerPathHandler(BadRedirectPath, BadRedirectHandler);
  httpServer.start(4444);

  var chan = make_channel(BadRedirectURI);
  chan.asyncOpen(new ChannelListener(checkFailed, null, CL_EXPECT_FAILURE), 
                 null);
  do_test_pending();
}
