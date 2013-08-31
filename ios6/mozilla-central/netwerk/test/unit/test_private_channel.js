//
// Private channel test
//

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;
const Cr = Components.results;

Cu.import("resource://testing-common/httpd.js");

var httpserver = new HttpServer();
var testpath = "/simple";

function run_test() {
  // Simulate a profile dir for xpcshell
  do_get_profile();

  // Start off with an empty cache
  evict_cache_entries();

  httpserver.registerPathHandler(testpath, serverHandler);
  httpserver.start(4444);

  var channel = setupChannel(testpath);

  channel.QueryInterface(Ci.nsIPrivateBrowsingChannel);
  channel.setPrivate(true);

  channel.asyncOpen(new ChannelListener(checkRequest, channel), null);

  do_test_pending();
}

function setupChannel(path) {
  var ios = Cc["@mozilla.org/network/io-service;1"].getService(Ci.nsIIOService);
  return chan = ios.newChannel("http://localhost:4444" + path, "", null)
                   .QueryInterface(Ci.nsIHttpChannel);
}

function serverHandler(metadata, response) {
  response.write("HTTP/1.0 200 OK\r\n\r\nfoobar");
  respose.finish();
}

function checkRequest(request, data, context) {
  do_check_eq(get_device_entry_count("disk"), 0);
  do_check_eq(get_device_entry_count("memory"), 1);
  httpserver.stop(do_test_finished);
}
