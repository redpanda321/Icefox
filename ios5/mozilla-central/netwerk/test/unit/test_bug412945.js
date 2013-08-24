do_load_httpd_js();

var httpserv;

function TestListener() {
}

TestListener.prototype.onStartRequest = function(request, context) {
}

TestListener.prototype.onStopRequest = function(request, context, status) {
  httpserv.stop(do_test_finished);
}

function run_test() {
  httpserv = new nsHttpServer();

  httpserv.registerPathHandler("/bug412945", bug412945);

  httpserv.start(4444);

  // make request
  var channel =
      Components.classes["@mozilla.org/network/io-service;1"].
      getService(Components.interfaces.nsIIOService).
      newChannel("http://localhost:4444/bug412945", null, null);

  channel.QueryInterface(Components.interfaces.nsIHttpChannel);
  channel.requestMethod = "post";
  channel.asyncOpen(new TestListener(), null);

  do_test_pending();
}

function bug412945(metadata, response) {
  if (!metadata.hasHeader("Content-Length") ||
      metadata.getHeader("Content-Length") != "0")
  {
    do_throw("Content-Length header not found!");
  }
}
