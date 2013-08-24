do_load_httpd_js();

var httpserver = null;
var geolocation = null;
var success = false;
var watchId = -1;

function terminate(succ) {
      success = succ;
      geolocation.clearWatch(watchID);
    }

function successCallback(pos){ terminate(true); }
function errorCallback(pos) { terminate(false); } 

var observer = {
    QueryInterface: function(iid) {
	if (iid.equals(Components.interfaces.nsISupports) ||
	    iid.equals(Components.interfaces.nsIObserver))
	    return this;
	throw Components.results.NS_ERROR_NO_INTERFACE;
    },

    observe: function(subject, topic, data) {
        if (data == "shutdown") {
            do_check_true(1);
            this._numProviders--;
            if (!this._numProviders) {
                httpserver.stop(function() {
                        do_check_true(success);
                        do_test_finished();
                    });
            }
        }
        else if (data == "starting") {
            do_check_true(1);
            this._numProviders++;
        }
    },

    _numProviders: 0,
};

function geoHandler(metadata, response)
{
    var georesponse = {
        status: "OK",
        location: {
            lat: 42,
            lng: 42,
        },
        accuracy: 42,
    };
  var position = JSON.stringify(georesponse);
  response.setStatusLine("1.0", 200, "OK");
  response.setHeader("Cache-Control", "no-cache", false);
  response.setHeader("Content-Type", "aplication/x-javascript", false);
  response.write(position);
}

function run_test()
{
    // only kill this test when shutdown is called on the provider.
    do_test_pending();
  
    httpserver = new nsHttpServer();
    httpserver.registerPathHandler("/geo", geoHandler);
    httpserver.start(4444);
  
    var prefs = Cc["@mozilla.org/preferences-service;1"].getService(Ci.nsIPrefBranch);
    prefs.setCharPref("geo.wifi.uri", "http://localhost:4444/geo");

    var obs = Cc["@mozilla.org/observer-service;1"].getService();
    obs = obs.QueryInterface(Ci.nsIObserverService);
    obs.addObserver(observer, "geolocation-device-events", false); 

    geolocation = Cc["@mozilla.org/geolocation;1"].getService(Ci.nsIDOMGeoGeolocation);
    watchID = geolocation.watchPosition(successCallback, errorCallback);
}

