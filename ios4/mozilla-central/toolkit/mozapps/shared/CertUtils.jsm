#if 0
/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is the Update Service.
 *
 * The Initial Developer of the Original Code is Ben Goodger.
 * Portions created by the Initial Developer are Copyright (C) 2004
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Darin Fisher <darin@meer.net>
 *  Daniel Veditz <dveditz@mozilla.com>
 *  Jesper Kristensen <mail@jesperkristensen.dk>
 *  Robert Strong <robert.bugzilla@gmail.com>
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
#endif
EXPORTED_SYMBOLS = [ "BadCertHandler", "checkCert" ];

const Ce = Components.Exception;
const Ci = Components.interfaces;
const Cr = Components.results;
const Cu = Components.utils;

/**
 * Checks if the connection must be HTTPS and if so, only allows built-in
 * certificates and validates application specified certificate attribute
 * values.
 * See bug 340198 and bug 544442.
 *
 * @param  aChannel
 *         The nsIChannel that will have its certificate checked.
 * @param  aCerts
 *         An array of JS objects with names / values corresponding to the
 *         channel's expected certificate's attribute names / values. This can
 *         be null or an empty array. If it isn't null the the scheme for the
 *         channel's originalURI must be https.
 * @throws NS_ERROR_UNEXPECTED if a certificate is expected and the URI scheme
 *         is not https.
 *         NS_ERROR_ILLEGAL_VALUE if a certificate attribute name from the
 *         cert param does not exist or the value for a certificate attribute
 *         from the aCerts  param is different than the expected value.
 *         NS_ERROR_ABORT if the certificate issuer is not built-in.
 */
function checkCert(aChannel, aCerts) {
  if (!aChannel.originalURI.schemeIs("https")) {
    // Require https if there are certificate values to verify
    if (aCerts) {
      throw new Ce("SSL is required and URI scheme is not https.",
                   Cr.NS_ERROR_UNEXPECTED);
    }
    return;
  }

  var cert =
      aChannel.securityInfo.QueryInterface(Ci.nsISSLStatusProvider).
      SSLStatus.QueryInterface(Ci.nsISSLStatus).serverCert;

  if (aCerts) {
    for (var i = 0; i < aCerts.length; ++i) {
      var error = false;
      var certAttrs = aCerts[i];
      for (var name in certAttrs) {
        if (!(name in cert)) {
          error = true;
          Cu.reportError("Expected attribute '" + name + "' not present in " +
                         "certificate.");
          break;
        }
        if (cert[name] != certAttrs[name]) {
          error = true;
          Cu.reportError("Expected certificate attribute '" + name + "' " +
                         "value incorrect, expected: '" + certAttrs[name] +
                         "', got: '" + cert[name] + "'.");
          break;
        }
      }

      if (!error)
        break;
    }

    if (error) {
      const certCheckErr = "Certificate checks failed. See previous errors " +
                           "for details.";
      Cu.reportError(certCheckErr);
      throw new Ce(certCheckErr, Cr.NS_ERROR_ILLEGAL_VALUE);
    }
  }


  var issuerCert = cert;
  while (issuerCert.issuer && !issuerCert.issuer.equals(issuerCert))
    issuerCert = issuerCert.issuer;

  const certNotBuiltInErr = "Certificate issuer is not built-in.";
  if (!issuerCert)
    throw new Ce(certNotBuiltInErr, Cr.NS_ERROR_ABORT);

  issuerCert = issuerCert.QueryInterface(Ci.nsIX509Cert3);
  var tokenNames = issuerCert.getAllTokenNames({});

  if (!tokenNames || !tokenNames.some(isBuiltinToken))
    throw new Ce(certNotBuiltInErr, Cr.NS_ERROR_ABORT);
}

function isBuiltinToken(tokenName) {
  return tokenName == "Builtin Object Token";
}

/**
 * This class implements nsIBadCertListener.  Its job is to prevent "bad cert"
 * security dialogs from being shown to the user.  It is better to simply fail
 * if the certificate is bad. See bug 304286.
 */
function BadCertHandler(aAllowNonBuiltInCerts) {
  this.allowNonBuiltInCerts = aAllowNonBuiltInCerts;
}
BadCertHandler.prototype = {

  // nsIChannelEventSink
  asyncOnChannelRedirect: function(oldChannel, newChannel, flags, callback) {
    if (this.allowNonBuiltInCerts) {
      callback.onRedirectVerifyCallback(Components.results.NS_OK);
      return;
    }

    // make sure the certificate of the old channel checks out before we follow
    // a redirect from it.  See bug 340198.
    // Don't call checkCert for internal redirects. See bug 569648.
    if (!(flags & Ci.nsIChannelEventSink.REDIRECT_INTERNAL))
      checkCert(oldChannel);
    
    callback.onRedirectVerifyCallback(Components.results.NS_OK);
  },

  // Suppress any certificate errors
  notifyCertProblem: function(socketInfo, status, targetSite) {
    return true;
  },

  // Suppress any ssl errors
  notifySSLError: function(socketInfo, error, targetSite) {
    return true;
  },

  // nsIInterfaceRequestor
  getInterface: function(iid) {
    return this.QueryInterface(iid);
  },

  // nsISupports
  QueryInterface: function(iid) {
    if (!iid.equals(Ci.nsIChannelEventSink) &&
        !iid.equals(Ci.nsIBadCertListener2) &&
        !iid.equals(Ci.nsISSLErrorListener) &&
        !iid.equals(Ci.nsIInterfaceRequestor) &&
        !iid.equals(Ci.nsISupports))
      throw Cr.NS_ERROR_NO_INTERFACE;
    return this;
  }
};
