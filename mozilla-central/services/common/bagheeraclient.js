/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file contains a client API for the Bagheera data storage service.
 *
 * Information about Bagheera is available at
 * https://github.com/mozilla-metrics/bagheera
 */

"use strict";

this.EXPORTED_SYMBOLS = [
  "BagheeraClient",
  "BagheeraClientRequestResult",
];

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/commonjs/promise/core.js");
Cu.import("resource://services-common/log4moz.js");
Cu.import("resource://services-common/rest.js");
Cu.import("resource://services-common/utils.js");


/**
 * Represents the result of a Bagheera request.
 */
this.BagheeraClientRequestResult = function BagheeraClientRequestResult() {
  this.transportSuccess = false;
  this.serverSuccess = false;
  this.request = null;
}

Object.freeze(BagheeraClientRequestResult.prototype);

/**
 * Create a new Bagheera client instance.
 *
 * Each client is associated with a specific Bagheera HTTP URI endpoint.
 *
 * @param baseURI
 *        (string) The base URI of the Bagheera HTTP endpoint.
 */
this.BagheeraClient = function BagheeraClient(baseURI) {
  if (!baseURI) {
    throw new Error("baseURI argument must be defined.");
  }

  this._log = Log4Moz.repository.getLogger("Services.BagheeraClient");
  this._log.level = Log4Moz.Level["Debug"];

  this.baseURI = baseURI;

  if (!baseURI.endsWith("/")) {
    this.baseURI += "/";
  }
}

BagheeraClient.prototype = {
  /**
   * Channel load flags for all requests.
   *
   * Caching is not applicable, so we bypass and disable it. We also
   * ignore any cookies that may be present for the domain because
   * Bagheera does not utilize cookies and the release of cookies may
   * inadvertantly constitute unncessary information disclosure.
   */
  _loadFlags: Ci.nsIRequest.LOAD_BYPASS_CACHE |
              Ci.nsIRequest.INHIBIT_CACHING |
              Ci.nsIRequest.LOAD_ANONYMOUS,

  DEFAULT_TIMEOUT_MSEC: 5 * 60 * 1000, // 5 minutes.

  _RE_URI_IDENTIFIER: /^[a-zA-Z0-9_-]+$/,

  /**
   * Upload a JSON payload to the server.
   *
   * The return value is a Promise which will be resolved with a
   * BagheeraClientRequestResult when the request has finished.
   *
   * @param namespace
   *        (string) The namespace to post this data to.
   * @param id
   *        (string) The ID of the document being uploaded. This is typically
   *        a UUID in hex form.
   * @param payload
   *        (string|object) Data to upload. Can be specified as a string (which
   *        is assumed to be JSON) or an object. If an object, it will be fed into
   *        JSON.stringify() for serialization.
   * @param deleteOldID
   *        (string) Old document ID to delete as part of upload. If not
   *        specified, no old documents will be deleted as part of upload. The
   *        string value is typically a UUID in hex form.
   *
   * @return Promise<BagheeraClientRequestResult>
   */
  uploadJSON: function uploadJSON(namespace, id, payload, deleteOldID=null) {
    if (!namespace) {
      throw new Error("namespace argument must be defined.");
    }

    if (!id) {
      throw new Error("id argument must be defined.");
    }

    if (!payload) {
      throw new Error("payload argument must be defined.");
    }

    let uri = this._submitURI(namespace, id);

    let data = payload;

    if (typeof(payload) == "object") {
      data = JSON.stringify(payload);
    }

    if (typeof(data) != "string") {
      throw new Error("Unknown type for payload: " + typeof(data));
    }

    this._log.info("Uploading data to " + uri);

    let request = new RESTRequest(uri);
    request.loadFlags = this._loadFlags;
    request.timeout = this.DEFAULT_TIMEOUT_MSEC;

    if (deleteOldID) {
      request.setHeader("X-Obsolete-Document", deleteOldID);
    }

    let deferred = Promise.defer();

    data = CommonUtils.convertString(data, "uncompressed", "deflate");
    // TODO proper header per bug 807134.
    request.setHeader("Content-Type", "application/json+zlib; charset=utf-8");

    this._log.info("Request body length: " + data.length);

    let result = new BagheeraClientRequestResult();
    result.namespace = namespace;
    result.id = id;

    request.onComplete = this._onComplete.bind(this, request, deferred, result);
    request.post(data);

    return deferred.promise;
  },

  /**
   * Delete the specified document.
   *
   * @param namespace
   *        (string) Namespace from which to delete the document.
   * @param id
   *        (string) ID of document to delete.
   *
   * @return Promise<BagheeraClientRequestResult>
   */
  deleteDocument: function deleteDocument(namespace, id) {
    let uri = this._submitURI(namespace, id);

    let request = new RESTRequest(uri);
    request.loadFlags = this._loadFlags;
    request.timeout = this.DEFAULT_TIMEOUT_MSEC;

    let result = new BagheeraClientRequestResult();
    result.namespace = namespace;
    result.id = id;
    let deferred = Promise.defer();

    request.onComplete = this._onComplete.bind(this, request, deferred, result);
    request.delete();

    return deferred.promise;
  },

  _submitURI: function _submitURI(namespace, id) {
    if (!this._RE_URI_IDENTIFIER.test(namespace)) {
      throw new Error("Illegal namespace name. Must be alphanumeric + [_-]: " +
                      namespace);
    }

    if (!this._RE_URI_IDENTIFIER.test(id)) {
      throw new Error("Illegal id value. Must be alphanumeric + [_-]: " + id);
    }

    return this.baseURI + "1.0/submit/" + namespace + "/" + id;
  },

  _onComplete: function _onComplete(request, deferred, result, error) {
    result.request = request;

    if (error) {
      this._log.info("Transport failure on request: " +
                     CommonUtils.exceptionStr(error));
      result.transportSuccess = false;
      deferred.resolve(result);
      return;
    }

    result.transportSuccess = true;

    let response = request.response;

    switch (response.status) {
      case 200:
      case 201:
        result.serverSuccess = true;
        break;

      default:
        result.serverSuccess = false;

        this._log.info("Received unexpected status code: " + response.status);
        this._log.debug("Response body: " + response.body);
    }

    deferred.resolve(result);
  },
};

Object.freeze(BagheeraClient.prototype);
