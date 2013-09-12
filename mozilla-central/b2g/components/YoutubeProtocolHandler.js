/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

XPCOMUtils.defineLazyGetter(this, "cpmm", function() {
  return Cc["@mozilla.org/childprocessmessagemanager;1"]
           .getService(Ci.nsIMessageSender);
});

// Splits parameters in a query string.
function extractParameters(aQuery) {
  let params = aQuery.split("&");
  let res = {};
  params.forEach(function(aParam) {
    let obj = aParam.split("=");
    res[obj[0]] = decodeURIComponent(obj[1]);
  });
  return res;
}

function YoutubeProtocolHandler() {
}

YoutubeProtocolHandler.prototype = {
  classID: Components.ID("{c3f1b945-7e71-49c8-95c7-5ae9cc9e2bad}"),
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIProtocolHandler]),

  scheme: "vnd.youtube",
  defaultPort: -1,
  protocolFlags: Ci.nsIProtocolHandler.URI_NORELATIVE |
                 Ci.nsIProtocolHandler.URI_NOAUTH |
                 Ci.nsIProtocolHandler.URI_LOADABLE_BY_ANYONE,

  // Sample URL:
  // vnd.youtube:iNuKL2Gy_QM?vndapp=youtube_mobile&vndclient=mv-google&vndel=watch&vnddnc=1
  newURI: function yt_phNewURI(aSpec, aOriginCharset, aBaseURI) {
    let uri = Cc["@mozilla.org/network/standard-url;1"]
              .createInstance(Ci.nsIStandardURL);
    let id = aSpec.substring(this.scheme.length + 1);
    id = id.substring(0, id.indexOf('?'));
    uri.init(Ci.nsIStandardURL.URLTYPE_STANDARD, -1, this.scheme + "://dummy_host/" + id, aOriginCharset,
             aBaseURI);
    return uri.QueryInterface(Ci.nsIURI);
  },

  newChannel: function yt_phNewChannel(aURI) {
    // Get a list of streams for this video id.
    let infoURI = "http://www.youtube.com/get_video_info?&video_id=" +
                  aURI.path.substring(1);

    let xhr = Cc["@mozilla.org/xmlextras/xmlhttprequest;1"]
                .createInstance(Ci.nsIXMLHttpRequest);
    xhr.open("GET", infoURI, true);
    xhr.addEventListener("load", function() {
      // Youtube sends the response as a double wrapped url answer:
      // we first extract the url_encoded_fmt_stream_map parameter,
      // and from each comma-separated entry in this value, we extract
      // other parameters (url and type).
      let key = "url_encoded_fmt_stream_map=";
      let pos = xhr.responseText.indexOf(key);
      if (pos == -1) {
        return;
      }
      let streams = decodeURIComponent(xhr.responseText
                                       .substring(pos + key.length)).split(",");
      let uri;
      let mimeType;

      // itag is an undocumented value which maps to resolution and mimetype
      // see https://en.wikipedia.org/wiki/YouTube#Quality_and_codecs
      // Ordered from least to most preferred
      let recognizedItags = [
        "17", // 144p 3GP
        "36", // 240p 3GP
        "43", // 360p WebM
#ifdef MOZ_WIDGET_GONK
        "18", // 360p H.264
#endif
      ];

      let bestItag = -1;

      let extras = { }

      streams.forEach(function(aStream) {
        let params = extractParameters(aStream);
        let url = params["url"];
        let type = params["type"] ? params["type"].split(";")[0] : null;
        let itag = params["itag"];

        let index;
        if (url && type && ((index = recognizedItags.indexOf(itag)) != -1) &&
            index > bestItag) {
          uri = url + '&signature=' + (params["sig"] ? params['sig'] : '');
          mimeType = type;
          bestItag = index;
        }
        for (let param in params) {
          if (["thumbnail_url", "length_seconds", "title"].indexOf(param) != -1) {
            extras[param] = decodeURIComponent(params[param]);
          }
        }
      });

      if (uri && mimeType) {
        cpmm.sendAsyncMessage("content-handler", {
          url: uri,
          type: mimeType,
          extras: extras
        });
      }
    });
    xhr.send(null);

    throw Components.results.NS_ERROR_ILLEGAL_VALUE;
  },

  allowPort: function yt_phAllowPort(aPort, aScheme) {
    return false;
  }
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([YoutubeProtocolHandler]);
