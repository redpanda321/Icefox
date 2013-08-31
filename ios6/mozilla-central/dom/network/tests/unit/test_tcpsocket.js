/**
 * Test TCPSocket.js by creating an XPCOM-style server socket, then sending
 * data in both directions and making sure each side receives their data
 * correctly and with the proper events.
 *
 * This test is derived from netwerk/test/unit/test_socks.js, except we don't
 * involve a subprocess.
 *
 * Future work:
 * - SSL.  see https://bugzilla.mozilla.org/show_bug.cgi?id=466524
 *             https://bugzilla.mozilla.org/show_bug.cgi?id=662180
 *   Alternatively, mochitests could be used.
 * - Testing overflow logic.
 *
 **/

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;
const Cu = Components.utils;
const CC = Components.Constructor;

/**
 *
 * Constants
 *
 */

// Some binary data to send.
const DATA_ARRAY = [0, 255, 254, 0, 1, 2, 3, 0, 255, 255, 254, 0],
      TYPED_DATA_ARRAY = new Uint8Array(DATA_ARRAY),
      HELLO_WORLD = "hlo wrld. ",
      BIG_ARRAY = new Array(65539),
      BIG_ARRAY_2 = new Array(65539);

for (var i_big = 0; i_big < BIG_ARRAY.length; i_big++) {
  BIG_ARRAY[i_big] = Math.floor(Math.random() * 256);
  BIG_ARRAY_2[i_big] = Math.floor(Math.random() * 256);
}

const BIG_TYPED_ARRAY = new Uint8Array(BIG_ARRAY),
      BIG_TYPED_ARRAY_2 = new Uint8Array(BIG_ARRAY_2);
      
const ServerSocket = CC("@mozilla.org/network/server-socket;1",
                        "nsIServerSocket",
                        "init"),
      InputStreamPump = CC("@mozilla.org/network/input-stream-pump;1",
                           "nsIInputStreamPump",
                           "init"),
      BinaryInputStream = CC("@mozilla.org/binaryinputstream;1",
                             "nsIBinaryInputStream",
                             "setInputStream"),
      BinaryOutputStream = CC("@mozilla.org/binaryoutputstream;1",
                              "nsIBinaryOutputStream",
                              "setOutputStream"),
      TCPSocket = new (CC("@mozilla.org/tcp-socket;1",
                     "nsIDOMTCPSocket"))();

const gInChild = Cc["@mozilla.org/xre/app-info;1"].getService(Ci.nsIXULRuntime)
                  .processType != Ci.nsIXULRuntime.PROCESS_TYPE_DEFAULT;

Cu.import("resource://gre/modules/Services.jsm");

/**
 *
 * Helper functions
 *
 */

/**
 * Spin up a listening socket and associate at most one live, accepted socket
 * with ourselves.
 */
function TestServer() {
  this.listener = ServerSocket(-1, true, -1);
  do_print('server: listening on', this.listener.port);
  this.listener.asyncListen(this);

  this.binaryInput = null;
  this.input = null;
  this.binaryOutput = null;
  this.output = null;

  this.onaccept = null;
  this.ondata = null;
  this.onclose = null;
}

TestServer.prototype = {
  onSocketAccepted: function(socket, trans) {
    if (this.input)
      do_throw("More than one live connection!?");

    do_print('server: got client connection');
    this.input = trans.openInputStream(0, 0, 0);
    this.binaryInput = new BinaryInputStream(this.input);
    this.output = trans.openOutputStream(0, 0, 0);
    this.binaryOutput = new BinaryOutputStream(this.output);

    new InputStreamPump(this.input, -1, -1, 0, 0, false).asyncRead(this, null);

    if (this.onaccept)
      this.onaccept();
    else
      do_throw("Received unexpected connection!");
  },

  onStopListening: function(socket) {
  },

  onDataAvailable: function(request, context, inputStream, offset, count) {
    var readData = this.binaryInput.readByteArray(count);
    if (this.ondata) {
      try {
        this.ondata(readData);
      } catch(ex) {
        // re-throw if this is from do_throw
        if (ex === Cr.NS_ERROR_ABORT)
          throw ex;
        // log if there was a test problem
        do_print('Caught exception: ' + ex + '\n' + ex.stack);
        do_throw('test is broken; bad ondata handler; see above');
      }
    } else {
      do_throw('Received ' + count + ' bytes of unexpected data!');
    }
  },

  onStartRequest: function(request, context) {
  },

  onStopRequest: function(request, context, status) {
    if (this.onclose)
      this.onclose();
    else
      do_throw("Received unexpected close!");
  },

  close: function() {
    this.binaryInput.close();
    this.binaryOutput.close();
  },

  /**
   * Forget about the socket we knew about before.
   */
  reset: function() {
    this.binaryInput = null;
    this.input = null;
    this.binaryOutput = null;
    this.output = null;
  },
};

function makeSuccessCase(name) {
  return function() {
    do_print('got expected: ' + name);
    run_next_test();
  };
}

function makeJointSuccess(names) {
  let funcs = {}, successCount = 0;
  names.forEach(function(name) {
    funcs[name] = function() {
      do_print('got expected: ' + name);
      if (++successCount === names.length)
        run_next_test();
    };
  });
  return funcs;
}

function makeFailureCase(name) {
  return function() {
    let argstr;
    if (arguments.length) {
      argstr = '(args: ' +
        Array.map(arguments, function(x) { return x + ""; }).join(" ") + ')';
    }
    else {
      argstr = '(no arguments)';
    }
    do_throw('got unexpected: ' + name + ' ' + argstr);
  };
}

function makeExpectData(name, expectedData, fromEvent, callback) {
  let dataBuffer = fromEvent ? null : [], done = false;
  return function(receivedData) {
    if (fromEvent) {
      receivedData = receivedData.data;
      if (dataBuffer) {
        let newBuffer = new Uint8Array(dataBuffer.length + receivedData.length);
        newBuffer.set(dataBuffer, 0);
        newBuffer.set(receivedData, dataBuffer.length);
        dataBuffer = newBuffer;
      }
      else {
        dataBuffer = receivedData;
      }
    }
    else {
      dataBuffer = dataBuffer.concat(receivedData);
    }
    do_print(name + ' received ' + receivedData.length + ' bytes');

    if (done)
      do_throw(name + ' Received data event when already done!');

    if (dataBuffer.length >= expectedData.length) {
      // check the bytes are equivalent
      for (let i = 0; i < expectedData.length; i++) {
        if (dataBuffer[i] !== expectedData[i]) {
          do_throw(name + ' Received mismatched character at position ' + i);
        }
      }
      if (dataBuffer.length > expectedData.length)
        do_throw(name + ' Received ' + dataBuffer.length + ' bytes but only expected ' +
                 expectedData.length + ' bytes.');

      done = true;
      if (callback) {
        callback();
      } else {
        run_next_test();
      }
    }
  };
}

var server = null, sock = null, failure_drain = null;

/**
 *
 * Test functions
 *
 */

/**
 * Connect the socket to the server. This test is added as the first
 * test, and is also added after every test which results in the socket
 * being closed.
 */

function connectSock() {
  server.reset();
  var yayFuncs = makeJointSuccess(['serveropen', 'clientopen']);

  sock = TCPSocket.open(
    '127.0.0.1', server.listener.port,
    { binaryType: 'arraybuffer' });

  sock.onopen = yayFuncs.clientopen;
  sock.ondrain = null;
  sock.ondata = makeFailureCase('data');
  sock.onerror = makeFailureCase('error');
  sock.onclose = makeFailureCase('close');

  server.onaccept = yayFuncs.serveropen;
  server.ondata = makeFailureCase('serverdata');
  server.onclose = makeFailureCase('serverclose');
}

/**
 * Test that sending a small amount of data works, and that buffering
 * does not take place for this small amount of data.
 */

function sendData() {
  server.ondata = makeExpectData('serverdata', DATA_ARRAY);
  if (!sock.send(TYPED_DATA_ARRAY)) {
    do_throw("send should not have buffered such a small amount of data");
  }
}

/**
 * Test that sending a large amount of data works, that buffering
 * takes place (send returns true), and that ondrain is called once
 * the data has been sent.
 */

function sendBig() {
  var yays = makeJointSuccess(['serverdata', 'clientdrain']),
      amount = 0;
      
  server.ondata = function (data) {
    amount += data.length;
    if (amount === BIG_TYPED_ARRAY.length) {
      yays.serverdata();      
    }
  };
  sock.ondrain = function(evt) {
    if (sock.bufferedAmount) {
      do_throw("sock.bufferedAmount was > 0 in ondrain");
    }
    yays.clientdrain(evt);
  }
  if (sock.send(BIG_TYPED_ARRAY)) {
    do_throw("expected sock.send to return false on large buffer send");
  }
}

/**
 * Test that data sent from the server correctly fires the ondata
 * callback on the client side.
 */

function receiveData() {
  server.ondata = makeFailureCase('serverdata');
  sock.ondata = makeExpectData('data', DATA_ARRAY, true);

  server.binaryOutput.writeByteArray(DATA_ARRAY, DATA_ARRAY.length);
}

/**
 * Test that when the server closes the connection, the onclose callback
 * is fired on the client side.
 */

function serverCloses() {
  // we don't really care about the server's close event, but we do want to
  // make sure it happened for sequencing purposes.
  var yayFuncs = makeJointSuccess(['clientclose', 'serverclose']);
  sock.ondata = makeFailureCase('data');
  sock.onclose = yayFuncs.clientclose;
  server.onclose = yayFuncs.serverclose;

  server.close();
}

/**
 * Test that when the client closes the connection, the onclose callback
 * is fired on the server side.
 */

function clientCloses() {
  // we want to make sure the server heard the close and also that the client's
  // onclose event fired for consistency.
  var yayFuncs = makeJointSuccess(['clientclose', 'serverclose']);
  server.onclose = yayFuncs.serverclose;
  sock.onclose = yayFuncs.clientclose;

  sock.close();
}

/**
 * Send a large amount of data and immediately call close
 */

function bufferedClose() {
  var yays = makeJointSuccess(['serverdata', 'clientclose', 'serverclose']);
  server.ondata = makeExpectData(
    "ondata", BIG_TYPED_ARRAY, false, yays.serverdata);
  server.onclose = yays.serverclose;
  sock.onclose = yays.clientclose;
  sock.send(BIG_TYPED_ARRAY);
  sock.close();
}

/**
 * Connect to a port we know is not listening so an error is assured,
 * and make sure that onerror and onclose are fired on the client side.
 */
 
function badConnect() {
  // There's probably nothing listening on tcp port 2.
  sock = TCPSocket.open('127.0.0.1', 2);

  sock.onopen = makeFailureCase('open');
  sock.ondata = makeFailureCase('data');
  sock.onclose = makeFailureCase('close');

  let success = makeSuccessCase('error');
  sock.onerror = function(data) {
    do_check_neq(data.data.message, '');
    do_check_neq(data.data.fileName, '');
    do_check_neq(data.data.lineNumber, 0);
    success();
  };
}

/**
 * Test that calling send with enough data to buffer causes ondrain to
 * be invoked once the data has been sent, and then test that calling send
 * and buffering again causes ondrain to be fired again.
 */

function drainTwice() {
  let yays = makeJointSuccess(
    ['ondrain', 'ondrain2',
    'ondata', 'ondata2',
    'serverclose', 'clientclose']);

  function serverSideCallback() {
    yays.ondata();
    server.ondata = makeExpectData(
      "ondata2", BIG_TYPED_ARRAY_2, false, yays.ondata2);

    sock.ondrain = yays.ondrain2;

    if (sock.send(BIG_TYPED_ARRAY_2)) {
      do_throw("sock.send(BIG_TYPED_ARRAY_2) did not return false to indicate buffering");
    }

    sock.close();
  }

  server.onclose = yays.serverclose;
  server.ondata = makeExpectData(
    "ondata", BIG_TYPED_ARRAY, false, serverSideCallback);

  sock.onclose = yays.clientclose;
  sock.ondrain = yays.ondrain;

  if (sock.send(BIG_TYPED_ARRAY)) {
    throw new Error("sock.send(BIG_TYPED_ARRAY) did not return false to indicate buffering");
  }
}

function cleanup() {
  do_print("Cleaning up");
  sock.close();
  if (!gInChild)
    Services.prefs.clearUserPref('dom.mozTCPSocket.enabled');
  run_next_test();
}

/**
 * Test that calling send with enough data to buffer twice in a row without
 * waiting for ondrain still results in ondrain being invoked at least once.
 */

function bufferTwice() {
  let yays = makeJointSuccess(
    ['ondata', 'ondrain', 'serverclose', 'clientclose']);

  let double_array = new Uint8Array(BIG_ARRAY.concat(BIG_ARRAY_2));
  server.ondata = makeExpectData(
    "ondata", double_array, false, yays.ondata);

  server.onclose = yays.serverclose;
  sock.onclose = yays.clientclose;

  sock.ondrain = function () {
    sock.close();
    yays.ondrain();
  }

  if (sock.send(BIG_TYPED_ARRAY)) {
    throw new Error("sock.send(BIG_TYPED_ARRAY) did not return false to indicate buffering");
  }
  if (sock.send(BIG_TYPED_ARRAY_2)) {
    throw new Error("sock.send(BIG_TYPED_ARRAY_2) did not return false to indicate buffering on second synchronous call to send");
  }
}

// - connect, data and events work both ways
add_test(connectSock);
add_test(sendData);
add_test(sendBig);
add_test(receiveData);
// - server closes on us
add_test(serverCloses);

// - connect, we close on the server
add_test(connectSock);
add_test(clientCloses);

// - connect, buffer, close
add_test(connectSock);
add_test(bufferedClose);

// - get an error on an attempt to connect to a non-listening port
add_test(badConnect);

// send a buffer, get a drain, send a buffer, get a drain
add_test(connectSock);
add_test(drainTwice);

// send a buffer, get a drain, send a buffer, get a drain
add_test(connectSock);
add_test(bufferTwice);

// clean up
add_test(cleanup);

function run_test() {
  if (!gInChild)
    Services.prefs.setBoolPref('dom.mozTCPSocket.enabled', true);
  
  server = new TestServer();

  run_next_test();

  do_timeout(10000, function() {
    do_throw(
      "The test should never take this long unless the system is hosed.");
  });
}
