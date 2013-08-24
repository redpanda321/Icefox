const CC = Components.Constructor;
const BinaryInputStream = CC("@mozilla.org/binaryinputstream;1",
                             "nsIBinaryInputStream",
                             "setInputStream");

function utf8decode(s) {
  return decodeURIComponent(escape(s));
}

function utf8encode(s) {
  return unescape(encodeURIComponent(s));
}

function handleRequest(request, response)
{
  var bodyStream = new BinaryInputStream(request.bodyInputStream);
  var bodyBytes = [];
  var result = [];
  while ((bodyAvail = bodyStream.available()) > 0)
    Array.prototype.push.apply(bodyBytes, bodyStream.readByteArray(bodyAvail));

  if (request.method == "POST") {
    var requestBody = String.fromCharCode.apply(null, bodyBytes);

    var contentTypeParams = {};
    request.getHeader("Content-Type").split(/\s*\;\s*/).forEach(function(s) {
      if (s.indexOf('=') >= 0) {
        let [name, value] = s.split('=');
        contentTypeParams[name] = value;
      }
      else {
        contentTypeParams[''] = s;
      }
    });

    if (contentTypeParams[''] == "multipart/form-data" &&
        request.queryString == "") {
      requestBody.split("--" + contentTypeParams.boundary).slice(1, -1).forEach(function (s) {

        let headers = {};
        let headerEnd = s.indexOf("\r\n\r\n");
        s.substr(2, headerEnd-2).split("\r\n").forEach(function(s) {
          // We're assuming UTF8 for now
          let [name, value] = s.split(': ');
          headers[name] = utf8decode(value);
        });
	let body = s.substring(headerEnd + 4, s.length - 2);
	if (!headers["Content-Type"] || headers["Content-Type"] == "text/plain") {
          // We're assuming UTF8 for now
	  body = utf8decode(body);
	}
	result.push({ headers: headers, body: body});
      });
    }
    if (contentTypeParams[''] == "text/plain" &&
        request.queryString == "plain") {
      requestBody.split("\r\n").slice(0, -1).forEach(function (s) {
        let index = s.indexOf("=");
        result.push({ name: s.substr(0, index),
                      value: s.substr(index + 1) });
      });
    }
    if (contentTypeParams[''] == "application/x-www-form-urlencoded" &&
        request.queryString == "url") {
      requestBody.split("&").forEach(function (s) {
        let index = s.indexOf("=");
        result.push({ name: unescape(s.substr(0, index)),
                      value: unescape(s.substr(index + 1)) });
      });
    }
  }
  else if (request.method == "GET") {
    request.queryString.split("&").forEach(function (s) {
      let index = s.indexOf("=");
      result.push({ name: unescape(s.substr(0, index)),
                    value: unescape(s.substr(index + 1)) });
    });
  }

  // Send response body
  response.setHeader("Content-Type", "text/plain; charset=utf-8", false);
  response.write(utf8encode(JSON.stringify(result)));
}
