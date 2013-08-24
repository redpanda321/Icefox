/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org unit test code.
 *
 * The Initial Developer of the Original Code is.
 *  Ben Bucksch <ben.bucksch beonex.com> of Beonex <http://business.beonex.com>
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/**
 * This tests the automatic login to the proxy with password,
 * if the password is stored and the browser is restarted.
 *
 * <copied from="test_authentication.js"/>
 */

do_load_httpd_js();

const FLAG_RETURN_FALSE   = 1 << 0;
const FLAG_WRONG_PASSWORD = 1 << 1;
const FLAG_PREVIOUS_FAILED = 1 << 2;

function AuthPrompt2(proxyFlags, hostFlags) {
  this.proxyCred.flags = proxyFlags;
  this.hostCred.flags = hostFlags;
}
AuthPrompt2.prototype = {
  proxyCred : { user: "proxy", pass: "guest",
      realmExpected: "intern", flags : 0 },
  hostCred : { user: "host", pass: "guest",
      realmExpected: "extern", flags : 0 },

  QueryInterface: function authprompt2_qi(iid) {
    if (iid.equals(Ci.nsISupports) ||
        iid.equals(Ci.nsIAuthPrompt2))
      return this;
    throw Cr.NS_ERROR_NO_INTERFACE;
  },

  promptAuth:
    function ap2_promptAuth(channel, encryptionLevel, authInfo)
  {
    try {

      // never HOST and PROXY set at the same time in prompt
      do_check_eq((authInfo.flags & Ci.nsIAuthInformation.AUTH_HOST) != 0,
          (authInfo.flags & Ci.nsIAuthInformation.AUTH_PROXY) == 0);

      var isProxy = (authInfo.flags & Ci.nsIAuthInformation.AUTH_PROXY) != 0;
      var cred = isProxy ? this.proxyCred : this.hostCred;

      dump("with flags: " +
        ((cred.flags & FLAG_WRONG_PASSWORD) !=0 ? "wrong password" : "")+" "+
        ((cred.flags & FLAG_PREVIOUS_FAILED) !=0 ? "previous failed" : "")+" "+
        ((cred.flags & FLAG_RETURN_FALSE) !=0 ? "return false" : "")  + "\n");

      // PROXY properly set by necko (checked using realm)
      do_check_eq(cred.realmExpected, authInfo.realm);

      // PREVIOUS_FAILED properly set by necko
      do_check_eq((cred.flags & FLAG_PREVIOUS_FAILED) != 0,
          (authInfo.flags & Ci.nsIAuthInformation.PREVIOUS_FAILED) != 0);

      if (cred.flags & FLAG_RETURN_FALSE)
      {
        cred.flags |= FLAG_PREVIOUS_FAILED;
        cred.flags &= ~FLAG_RETURN_FALSE;
        return false;
      }

      authInfo.username = cred.user;
      if (cred.flags & FLAG_WRONG_PASSWORD) {
        authInfo.password = cred.pass + ".wrong";
        cred.flags |= FLAG_PREVIOUS_FAILED;
        // Now clear the flag to avoid an infinite loop
        cred.flags &= ~FLAG_WRONG_PASSWORD;
      } else {
        authInfo.password = cred.pass;
        cred.flags &= ~FLAG_PREVIOUS_FAILED;
      }
      return true;

    } catch (e) { do_throw(e); }
  },

  asyncPromptAuth:
    function ap2_async(channel, callback, context, encryptionLevel, authInfo)
  {
    try {
      var me = this;
      var allOverAndDead = false;
      runLater(function() {
        try {
          if (allOverAndDead)
            throw "already canceled";
          var ret = me.promptAuth(channel, encryptionLevel, authInfo);
          if (!ret)
            callback.onAuthCancelled(context, true);
          else
            callback.onAuthAvailable(context, authInfo);
          allOverAndDead = true;
        } catch (e) { do_throw(e); }
      });
      return new Cancelable(function() {
          if (allOverAndDead)
            throw "can't cancel, already ran";
          callback.onAuthAvailable(context, authInfo);
          allOverAndDead = true;
        });
    } catch (e) { do_throw(e); }
  }
};

function Cancelable(onCancelFunc) {
  this.onCancelFunc = onCancelFunc;
}
Cancelable.prototype = {
  QueryInterface: function cancelable_qi(iid) {
    if (iid.equals(Ci.nsISupports) ||
        iid.equals(Ci.nsICancelable))
      return this;
    throw Cr.NS_ERROR_NO_INTERFACE;
  },
  cancel: function cancel() {
    try {
      this.onCancelFunc();
    } catch (e) { do_throw(e); }
  }
};

function runLater(func)
{
  var timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  timer.initWithCallback(func, 0, Ci.nsITimer.TYPE_ONE_SHOT);
}

function Requestor(proxyFlags, hostFlags) {
  this.proxyFlags = proxyFlags;
  this.hostFlags = hostFlags;
}
Requestor.prototype = {
  QueryInterface: function requestor_qi(iid) {
    if (iid.equals(Ci.nsISupports) ||
        iid.equals(Ci.nsIInterfaceRequestor))
      return this;
    throw Cr.NS_ERROR_NO_INTERFACE;
  },

  getInterface: function requestor_gi(iid) {
    if (iid.equals(Ci.nsIAuthPrompt)) {
      dump("authprompt1 not implemented\n");
      throw Cr.NS_ERROR_NO_INTERFACE;
    }
    if (iid.equals(Ci.nsIAuthPrompt2)) {
      try {
        // Allow the prompt to store state by caching it here
        if (!this.prompt2)
          this.prompt2 = new AuthPrompt2(this.proxyFlags, this.hostFlags);
        return this.prompt2;
      } catch (e) { do_throw(e); }
    }
    throw Cr.NS_ERROR_NO_INTERFACE;
  },

  prompt2: null
};

var listener = {
  expectedCode: -1, // uninitialized

  onStartRequest: function test_onStartR(request, ctx) {
    try {
      if (!Components.isSuccessCode(request.status))
        do_throw("Channel should have a success code!");

      if (!(request instanceof Ci.nsIHttpChannel))
        do_throw("Expecting an HTTP channel");

      do_check_eq(this.expectedCode, request.responseStatus);
      // If we expect 200, the request should have succeeded
      do_check_eq(this.expectedCode == 200, request.requestSucceeded);

    } catch (e) {
      do_throw("Unexpected exception: " + e);
    }

    throw Cr.NS_ERROR_ABORT;
  },

  onDataAvailable: function test_ODA() {
    do_throw("Should not get any data!");
  },

  onStopRequest: function test_onStopR(request, ctx, status) {
    do_check_eq(status, Cr.NS_ERROR_ABORT);

    if (current_test < (tests.length - 1)) {
      // First, need to clear the auth cache
      Cc["@mozilla.org/network/http-auth-manager;1"]
            .getService(Ci.nsIHttpAuthManager)
            .clearAll();

      current_test++;
      tests[current_test]();
    } else {
      do_test_pending();
      httpserv.stop(do_test_finished);
    }

    do_test_finished();
  }
};

function makeChan(url) {
  if (!url)
    url = "http://somesite/";
  var ios = Cc["@mozilla.org/network/io-service;1"]
                      .getService(Ci.nsIIOService);
  var chan = ios.newChannel(url, null, null)
                .QueryInterface(Ci.nsIHttpChannel);

  return chan;
}

var current_test = 0;
var httpserv = null;

function run_test() {
  httpserv = new nsHttpServer();
  httpserv.registerPathHandler("/", proxyAuthHandler);
  httpserv.identity.add("http", "somesite", 80);
  httpserv.start(4444);

  const prefs = Cc["@mozilla.org/preferences-service;1"]
                         .getService(Ci.nsIPrefBranch);
  prefs.setCharPref("network.proxy.http", "localhost");
  prefs.setIntPref("network.proxy.http_port", 4444);
  prefs.setCharPref("network.proxy.no_proxies_on", "");
  prefs.setIntPref("network.proxy.type", 1);

  tests[current_test]();
}

function test_proxy_returnfalse() {
  dump("\ntest: proxy returnfalse\n");
  var chan = makeChan();
  chan.notificationCallbacks = new Requestor(FLAG_RETURN_FALSE, 0);
  listener.expectedCode = 407; // Proxy Unauthorized
  chan.asyncOpen(listener, null);

  do_test_pending();
}

function test_proxy_wrongpw() {
  dump("\ntest: proxy wrongpw\n");
  var chan = makeChan();
  chan.notificationCallbacks = new Requestor(FLAG_WRONG_PASSWORD, 0);
  listener.expectedCode = 200; // Eventually OK
  chan.asyncOpen(listener, null);
  do_test_pending();
}

function test_all_ok() {
  dump("\ntest: all ok\n");
  var chan = makeChan();
  chan.notificationCallbacks = new Requestor(0, 0);
  listener.expectedCode = 200; // OK
  chan.asyncOpen(listener, null);
  do_test_pending();
}

function test_host_returnfalse() {
  dump("\ntest: host returnfalse\n");
  var chan = makeChan();
  chan.notificationCallbacks = new Requestor(0, FLAG_RETURN_FALSE);
  listener.expectedCode = 401; // Host Unauthorized
  chan.asyncOpen(listener, null);

  do_test_pending();
}

function test_host_wrongpw() {
  dump("\ntest: host wrongpw\n");
  var chan = makeChan();
  chan.notificationCallbacks = new Requestor(0, FLAG_WRONG_PASSWORD);
  listener.expectedCode = 200; // Eventually OK
  chan.asyncOpen(listener, null);
  do_test_pending();
}

function test_proxy_wrongpw_host_wrongpw() {
  dump("\ntest: proxy wrongpw, host wrongpw\n");
  var chan = makeChan();
  chan.notificationCallbacks =
      new Requestor(FLAG_WRONG_PASSWORD, FLAG_WRONG_PASSWORD);
  listener.expectedCode = 200; // OK
  chan.asyncOpen(listener, null);
  do_test_pending();
}

function test_proxy_wrongpw_host_returnfalse() {
  dump("\ntest: proxy wrongpw, host return false\n");
  var chan = makeChan();
  chan.notificationCallbacks =
      new Requestor(FLAG_WRONG_PASSWORD, FLAG_RETURN_FALSE);
  listener.expectedCode = 401; // Host Unauthorized
  chan.asyncOpen(listener, null);
  do_test_pending();
}

var tests = [test_proxy_returnfalse, test_proxy_wrongpw, test_all_ok,
        test_host_returnfalse, test_host_wrongpw,
        test_proxy_wrongpw_host_wrongpw, test_proxy_wrongpw_host_returnfalse];


// PATH HANDLERS

// Proxy
function proxyAuthHandler(metadata, response) {
  try {
    var realm = "intern";
    // btoa("proxy:guest"), but that function is not available here
    var expectedHeader = "Basic cHJveHk6Z3Vlc3Q=";

    var body;
    if (metadata.hasHeader("Proxy-Authorization") &&
        metadata.getHeader("Proxy-Authorization") == expectedHeader)
    {
      dump("proxy password ok\n");
      response.setHeader("Proxy-Authenticate",
          'Basic realm="' + realm + '"', false);

      hostAuthHandler(metadata, response);
    }
    else
    {
      dump("proxy password required\n");
      response.setStatusLine(metadata.httpVersion, 407,
          "Unauthorized by HTTP proxy");
      response.setHeader("Proxy-Authenticate",
          'Basic realm="' + realm + '"', false);
      body = "failed";
      response.bodyOutputStream.write(body, body.length);
    }
  } catch (e) { do_throw(e); }
}

// Host /auth
function hostAuthHandler(metadata, response) {
  try {
    var realm = "extern";
    // btoa("host:guest"), but that function is not available here
    var expectedHeader = "Basic aG9zdDpndWVzdA==";

    var body;
    if (metadata.hasHeader("Authorization") &&
        metadata.getHeader("Authorization") == expectedHeader)
    {
      dump("host password ok\n");
      response.setStatusLine(metadata.httpVersion, 200,
          "OK, authorized for host");
      response.setHeader("WWW-Authenticate",
          'Basic realm="' + realm + '"', false);
      body = "success";
    }
    else
    {
      dump("host password required\n");
      response.setStatusLine(metadata.httpVersion, 401,
          "Unauthorized by HTTP server host");
      response.setHeader("WWW-Authenticate",
          'Basic realm="' + realm + '"', false);
      body = "failed";
    }
    response.bodyOutputStream.write(body, body.length);
  } catch (e) { do_throw(e); }
}
