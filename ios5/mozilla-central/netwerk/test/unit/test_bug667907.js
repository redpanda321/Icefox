do_load_httpd_js();

var httpserver = null;
var simplePath = "/simple";
var normalPath = "/normal";
var httpbody = "<html></html>";
var uri1 = "http://localhost:4444" + simplePath;
var uri2 = "http://localhost:4444" + normalPath;

function make_channel(url) {
  var ios = Cc["@mozilla.org/network/io-service;1"].
            getService(Ci.nsIIOService);
  return ios.newChannel(url, "", null);
}

var listener_proto = {
  QueryInterface: function(iid) {
    if (iid.equals(Components.interfaces.nsIStreamListener) ||
        iid.equals(Components.interfaces.nsIRequestObserver) ||
        iid.equals(Components.interfaces.nsISupports))
      return this;
    throw Components.results.NS_ERROR_NO_INTERFACE;
  },

  onStartRequest: function(request, context) {
    do_check_eq(request.QueryInterface(Ci.nsIChannel).contentType,
		this.contentType);
    request.cancel(Cr.NS_BINDING_ABORTED);
  },

  onDataAvailable: function(request, context, stream, offset, count) {
    do_throw("Unexpected onDataAvailable");
  },

  onStopRequest: function(request, context, status) {
    do_check_eq(status, Cr.NS_BINDING_ABORTED);
    this.termination_func();
  }  
};

function listener(contentType, termination_func) {
  this.contentType = contentType;
  this.termination_func = termination_func;
}
listener.prototype = listener_proto;

function run_test()
{
  httpserver = new nsHttpServer();
  httpserver.registerPathHandler(simplePath, simpleHandler);
  httpserver.registerPathHandler(normalPath, normalHandler);
  httpserver.start(4444);
  
  var channel = make_channel(uri1);
  channel.asyncOpen(new listener("text/plain", function() {
	run_test2();
      }), null);

  do_test_pending();
}

function run_test2()
{
  var channel = make_channel(uri2);
  channel.asyncOpen(new listener("text/html", function() {
	httpserver.stop(do_test_finished);
      }), null);
}

function simpleHandler(metadata, response)
{
  response.seizePower();
  response.bodyOutputStream.write(httpbody, httpbody.length);
  response.finish();
}

function normalHandler(metadata, response)
{
  response.bodyOutputStream.write(httpbody, httpbody.length);
  response.finish();
}
