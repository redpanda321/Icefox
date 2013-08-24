// Utility functions for offline tests.
netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");

var Cc = Components.classes;
var Ci = Components.interfaces;

const kNetBase = 2152398848; // 0x804B0000
var NS_ERROR_CACHE_KEY_NOT_FOUND = kNetBase + 61;
var NS_ERROR_CACHE_KEY_WAIT_FOR_VALIDATION = kNetBase + 64;

// Reading the contents of multiple cache entries asynchronously
function OfflineCacheContents(urls) {
  this.urls = urls;
  this.contents = {};
}

OfflineCacheContents.prototype = {
QueryInterface: function(iid) {
    if (!iid.equals(Ci.nsISupports) &&
        !iid.equals(Ci.nsICacheListener)) {
      throw Cr.NS_ERROR_NO_INTERFACE;
    }
    return this;
  },
onCacheEntryAvailable: function(desc, accessGranted, status) {
    netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");

    if (!desc) {
      this.fetch(this.callback);
      return;
    }

    var stream = desc.QueryInterface(Ci.nsICacheEntryDescriptor).openInputStream(0);
    var sstream = Components.classes["@mozilla.org/scriptableinputstream;1"]
                 .createInstance(Components.interfaces.nsIScriptableInputStream);
    sstream.init(stream);
    this.contents[desc.key] = sstream.read(sstream.available());
    sstream.close();
    desc.close();
    this.fetch(this.callback);
  },

fetch: function(callback)
{
  this.callback = callback;
  if (this.urls.length == 0) {
    callback(this.contents);
    return;
  }

  var url = this.urls.shift();
  var self = this;

  var cacheSession = OfflineTest.getActiveSession();
  cacheSession.asyncOpenCacheEntry(url, Ci.nsICache.ACCESS_READ, this);
}
};

var OfflineTest = {

_slaveWindow: null,

// The window where test results should be sent.
_masterWindow: null,

// Array of all PUT overrides on the server
_pathOverrides: [],

// SJSs whom state was changed to be reverted on teardown
_SJSsStated: [],

setupChild: function()
{
  if (window.parent.OfflineTest.hasSlave()) {
    return false;
  }

  this._slaveWindow = null;
  this._masterWindow = window.top;

  return true;
},

// Setup the tests.  This will reload the current page in a new window
// if necessary.
setup: function()
{
  netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");

  if (!window.opener || !window.opener.OfflineTest ||
      !window.opener.OfflineTest._isMaster) {
    // Offline applications must be toplevel windows and have the
    // offline-app permission.  Because we were loaded without the
    // offline-app permission and (probably) in an iframe, we need to
    // enable the pref and spawn a new window to perform the actual
    // tests.  It will use this window to report successes and
    // failures.
    var pm = Cc["@mozilla.org/permissionmanager;1"]
      .getService(Ci.nsIPermissionManager);
    var uri = Cc["@mozilla.org/network/io-service;1"]
      .getService(Ci.nsIIOService)
      .newURI(window.location.href, null, null);
    if (pm.testPermission(uri, "offline-app") != 0) {
      dump("Previous test failed to clear offline-app permission!  Expect failures.\n");
    }
    pm.add(uri, "offline-app", Ci.nsIPermissionManager.ALLOW_ACTION);

    // Tests must run as toplevel windows.  Open a slave window to run
    // the test.
    this._isMaster = true;
    this._slaveWindow = window.open(window.location, "offlinetest");

    this._slaveWindow._OfflineSlaveWindow = true;

    return false;
  }

  this._masterWindow = window.opener;

  return true;
},

teardown: function()
{
  // Remove the offline-app permission we gave ourselves.

  netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");

  var pm = Cc["@mozilla.org/permissionmanager;1"]
           .getService(Ci.nsIPermissionManager);
  var uri = Cc["@mozilla.org/network/io-service;1"]
            .getService(Ci.nsIIOService)
            .newURI(window.location.href, null, null);
  pm.remove(uri.host, "offline-app");

  // Clear all overrides on the server
  for (override in this._pathOverrides)
    this.deleteData(this._pathOverrides[override]);
  for (statedSJS in this._SJSsStated)
    this.setSJSState(this._SJSsStated[statedSJS], "");

  this.clear();
},

finish: function()
{
  SimpleTest.finish();

  if (this._masterWindow) {
    this._masterWindow.OfflineTest.finish();
    window.close();
  }
},

hasSlave: function()
{
  return (this._slaveWindow != null);
},

//
// Mochitest wrappers - These forward tests to the proper mochitest window.
//
ok: function(condition, name, diag)
{
  return this._masterWindow.SimpleTest.ok(condition, name, diag);
},

is: function(a, b, name)
{
  return this._masterWindow.SimpleTest.is(a, b, name);
},

isnot: function(a, b, name)
{
  return this._masterWindow.SimpleTest.isnot(a, b, name);
},

todo: function(a, name)
{
  return this._masterWindow.SimpleTest.todo(a, name);
},

clear: function()
{
  // XXX: maybe we should just wipe out the entire disk cache.
  var applicationCache = this.getActiveCache();
  if (applicationCache) {
    applicationCache.discard();
  }
},

failEvent: function(e)
{
  OfflineTest.ok(false, "Unexpected event: " + e.type);
},

// The offline API as specified has no way to watch the load of a resource
// added with applicationCache.mozAdd().
waitForAdd: function(url, onFinished) {
  // Check every half second for ten seconds.
  var numChecks = 20;
  var waitFunc = function() {
    var cacheSession = OfflineTest.getActiveSession();
    var entry;
    try {
      var entry = cacheSession.openCacheEntry(url, Ci.nsICache.ACCESS_READ, false);
    } catch (e) {
    }

    if (entry) {
      entry.close();
      onFinished();
      return;
    }

    if (--numChecks == 0) {
      onFinished();
      return;
    }

    setTimeout(OfflineTest.priv(waitFunc), 500);
  }

  setTimeout(this.priv(waitFunc), 500);
},

getManifestUrl: function()
{
  return window.top.document.documentElement.getAttribute("manifest");
},

getActiveCache: function()
{
  // Note that this is the current active cache in the cache stack, not the
  // one associated with this window.
  var serv = Cc["@mozilla.org/network/application-cache-service;1"]
             .getService(Ci.nsIApplicationCacheService);
  return serv.getActiveCache(this.getManifestUrl());
},

getActiveSession: function()
{
  var cache = this.getActiveCache();
  if (!cache) return null;

  var cacheService = Cc["@mozilla.org/network/cache-service;1"]
                     .getService(Ci.nsICacheService);
  return cacheService.createSession(cache.clientID,
                                    Ci.nsICache.STORE_OFFLINE,
                                    true);
},

priv: function(func)
{
  var self = this;
  return function() {
    netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");
    func(arguments);
  }
},

checkCustomCache: function(group, url, expectEntry)
{
  var serv = Cc["@mozilla.org/network/application-cache-service;1"]
             .getService(Ci.nsIApplicationCacheService);
  var cache = serv.getActiveCache(group);
  var cacheSession = null;
  if (cache) {
    var cacheService = Cc["@mozilla.org/network/cache-service;1"]
                       .getService(Ci.nsICacheService);
    cacheSession = cacheService.createSession(cache.clientID,
                                      Ci.nsICache.STORE_OFFLINE,
                                      true);
  }

  this._checkCache(cacheSession, url, expectEntry);
},

checkCache: function(url, expectEntry)
{
  var cacheSession = this.getActiveSession();
  this._checkCache(cacheSession, url, expectEntry);
},

_checkCache: function(cacheSession, url, expectEntry)
{
  if (!cacheSession) {
    if (expectEntry) {
      this.ok(false, url + " should exist in the offline cache");
    } else {
      this.ok(true, url + " should not exist in the offline cache");
    }
    return;
  }

  try {
    var entry = cacheSession.openCacheEntry(url, Ci.nsICache.ACCESS_READ, false);
    if (expectEntry) {
      this.ok(true, url + " should exist in the offline cache");
    } else {
      this.ok(false, url + " should not exist in the offline cache");
    }
    entry.close();
  } catch (e) {
    if (e.result == NS_ERROR_CACHE_KEY_NOT_FOUND) {
      if (expectEntry) {
        this.ok(false, url + " should exist in the offline cache");
      } else {
        this.ok(true, url + " should not exist in the offline cache");
      }
    } else if (e.result == NS_ERROR_CACHE_KEY_WAIT_FOR_VALIDATION) {
      // There was a cache key that we couldn't access yet, that's good enough.
      if (expectEntry) {
        this.ok(!mustBeValid, url + " should exist in the offline cache");
      } else {
        this.ok(mustBeValid, url + " should not exist in the offline cache");
      }
    } else {
      throw e;
    }
  }
},

setSJSState: function(sjsPath, stateQuery)
{
  var client = new XMLHttpRequest();
  client.open("GET", sjsPath + "?state=" + stateQuery, false);

  netscape.security.PrivilegeManager.enablePrivilege("UniversalXPConnect");
  var appcachechannel = client.channel.QueryInterface(Ci.nsIApplicationCacheChannel);
  appcachechannel.chooseApplicationCache = false;
  appcachechannel.inheritApplicationCache = false;
  appcachechannel.applicationCache = null;

  client.send();

  if (stateQuery == "")
    delete this._SJSsStated[sjsPath];
  else
    this._SJSsStated.push(sjsPath);
}

};
