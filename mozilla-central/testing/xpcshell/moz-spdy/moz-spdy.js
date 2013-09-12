/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var spdy = require('../node-spdy/lib/spdy.js');
var fs = require('fs');
var url = require('url');
var crypto = require('crypto');

function getHttpContent(path) {
  var content = '<!doctype html>' +
                '<html>' +
                '<head><title>HOORAY!</title></head>' +
                '<body>You Win! (by requesting' + path + ')</body>' +
                '</html>';
  return content;
}

function getHugeContent(size) {
  var content = '';

  for (var i = 0; i < size; i++) {
    content += '0';
  }

  return content;
}

/* This takes care of responding to the multiplexed request for us */
var Multiplex = function() {};

Multiplex.prototype = {
  mp1res: null,
  mp2res: null,
  buf: null,
  mp1start: 0,
  mp2start: 0,

  checkReady: function() {
    if (this.mp1res != null && this.mp2res != null) {
      this.buf = getHugeContent(30*1024);
      this.mp1start = 0;
      this.mp2start = 0;
      this.send(this.mp1res, 0);
      setTimeout(function() { this.send(this.mp2res, 0); }.bind(this), 5);
    }
  },

  send: function(res, start) {
    var end = start + 1024;
    if (end > this.buf.length)
      end = this.buf.length;
    var content = this.buf.substring(start, end);
    if (end < this.buf.length) {
      res.write(content);
      setTimeout(function() { this.send(res, end); }.bind(this), 10);
    } else {
      res.end(content);
    }
  },
};

var m = new Multiplex();

var post_hash = null;
function receivePostData(chunk) {
  post_hash.update(chunk.toString());
}

function finishPost(res, content) {
  var md5 = post_hash.digest('hex');
  res.setHeader('X-Calculated-MD5', md5);
  res.writeHead(200);
  res.end(content);
}

function handleRequest(req, res) {
  var u = url.parse(req.url);
  var content = getHttpContent(u.pathname);

  if (req.streamID) {
    res.setHeader('X-Connection-Spdy', 'yes');
    res.setHeader('X-Spdy-StreamId', '' + req.streamID);
  } else {
    res.setHeader('X-Connection-Spdy', 'no');
  }

  if (u.pathname == '/exit') {
    res.setHeader('Content-Type', 'text/plain');
    res.writeHead(200);
    res.end('ok');
    process.exit();
  } else if (u.pathname == '/multiplex1' && req.streamID) {
    res.setHeader('Content-Type', 'text/plain');
    res.writeHead(200);
    m.mp1res = res;
    m.checkReady();
    return;
  } else if (u.pathname == '/multiplex2' && req.streamID) {
    res.setHeader('Content-Type', 'text/plain');
    res.writeHead(200);
    m.mp2res = res;
    m.checkReady();
    return;
  } else if (u.pathname == "/header") {
    var val = req.headers["x-test-header"];
    if (val) {
      res.setHeader("X-Received-Test-Header", val);
    }
  } else if (u.pathname == "/big") {
    content = getHugeContent(128 * 1024);
    var hash = crypto.createHash('md5');
    hash.update(content);
    var md5 = hash.digest('hex');
    res.setHeader("X-Expected-MD5", md5);
  } else if (u.pathname == "/post") {
    if (req.method != "POST") {
      res.writeHead(405);
      res.end('Unexpected method: ' + req.method);
      return;
    }

    post_hash = crypto.createHash('md5');
    req.on('data', receivePostData);
    req.on('end', function () { finishPost(res, content); });

    return;
  }

  res.setHeader('Content-Type', 'text/html');
  res.writeHead(200);
  res.end(content);
}

// Set up the SSL certs for our server
var options = {
  key: fs.readFileSync(__dirname + '/spdy-key.pem'),
  cert: fs.readFileSync(__dirname + '/spdy-cert.pem'),
  ca: fs.readFileSync(__dirname + '/spdy-ca.pem'),
};

spdy.createServer(options, handleRequest).listen(4443);
console.log('SPDY server listening on port 4443');

// Set up to exit when the user finishes our stdin
process.stdin.resume();
process.stdin.on('end', function () {
  process.exit();
});
