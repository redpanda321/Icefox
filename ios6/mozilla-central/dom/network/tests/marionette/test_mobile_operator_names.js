/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 60000;

SpecialPowers.addPermission("mobileconnection", true, document);

const OPERATOR_HOME = 0;
const OPERATOR_ROAMING = 1;

let connection = navigator.mozMobileConnection;
ok(connection instanceof MozMobileConnection,
   "connection is instanceof " + connection.constructor);

let voice = connection.voice;
ok(voice, "voice connection valid");

let network = voice.network;
ok(network, "voice network info valid");

let emulatorCmdPendingCount = 0;
function sendEmulatorCommand(cmd, callback) {
  emulatorCmdPendingCount++;
  runEmulatorCmd(cmd, function (result) {
    emulatorCmdPendingCount--;

    is(result[result.length - 1], "OK");

    callback(result);
  });
}

function setEmulatorOperatorNames(which, longName, shortName, callback) {
  let cmd = "operator set " + which + " " + longName + "," + shortName;
  sendEmulatorCommand(cmd, function (result) {
    let re = new RegExp("^" + longName + "," + shortName + ",");
    ok(result[which].match(re), "Long/short name should be changed.");

    if (callback) {
      window.setTimeout(callback, 0);
    }
  });
}

function setEmulatorRoaming(roaming, callback) {
  let cmd = "gsm voice " + (roaming ? "roaming" : "home");
  sendEmulatorCommand(cmd, function (result) {
    is(result[0], "OK");

    if (callback) {
      window.setTimeout(callback, 0);
    }
  });
}

function checkValidMccMnc() {
  is(network.mcc, 310, "network.mcc");
  is(network.mnc, 260, "network.mnc");
}

function waitForVoiceChange(callback) {
  connection.addEventListener("voicechange", function onvoicechange() {
    connection.removeEventListener("voicechange", onvoicechange);
    callback();
  });
}

function doTestMobileOperatorNames(longName, shortName, callback) {
  log("Testing '" + longName + "', '" + shortName + "':");

  checkValidMccMnc();

  waitForVoiceChange(function () {
    is(network.longName, longName, "network.longName");
    is(network.shortName, shortName, "network.shortName");

    checkValidMccMnc();

    window.setTimeout(callback, 0);
  });

  setEmulatorOperatorNames(OPERATOR_HOME, longName, shortName);
}

function testMobileOperatorNames() {
  doTestMobileOperatorNames("Mozilla", "B2G", function () {
    doTestMobileOperatorNames("Mozilla", "", function () {
      doTestMobileOperatorNames("", "B2G", function () {
        doTestMobileOperatorNames("", "", function () {
          doTestMobileOperatorNames("Android", "Android", testRoamingCheck);
        });
      });
    });
  });
}

// See bug 797972 - B2G RIL: False roaming situation
//
// Steps to test:
// 1. set roaming operator names
// 2. set emulator roaming
// 3. wait for onvoicechange event and test passing conditions
// 4. set emulator roaming back to false
// 5. wait for onvoicechange event again and callback
function doTestRoamingCheck(longName, shortName, callback) {
  log("Testing roaming check '" + longName + "', '" + shortName + "':");

  setEmulatorOperatorNames(OPERATOR_ROAMING, longName, shortName,
                           window.setTimeout.bind(window, function () {
      let done = false;
      function resetRoaming() {
        if (!done) {
          window.setTimeout(resetRoaming, 100);
          return;
        }

        waitForVoiceChange(callback);
        setEmulatorRoaming(false);
      }

      waitForVoiceChange(function () {
        is(network.longName, longName, "network.longName");
        is(network.shortName, shortName, "network.shortName");
        is(voice.roaming, false, "voice.roaming");

        resetRoaming();
      });

      setEmulatorRoaming(true, function () {
        done = true;
      });
    }, 3000) // window.setTimeout.bind
  ); // setEmulatorOperatorNames
}

function testRoamingCheck() {
  // If Either long name or short name of current registered operator matches
  // SPN("Android"), then the `roaming` attribute should be set to false.
  doTestRoamingCheck("Android", "Android", function () {
    doTestRoamingCheck("Android", "android", function () {
      doTestRoamingCheck("Android", "Xxx", function () {
        doTestRoamingCheck("android", "Android", function () {
          doTestRoamingCheck("android", "android", function () {
            doTestRoamingCheck("android", "Xxx", function () {
              doTestRoamingCheck("Xxx", "Android", function () {
                doTestRoamingCheck("Xxx", "android", function () {
                  setEmulatorOperatorNames(OPERATOR_ROAMING, "TelKila", "TelKila",
                                           window.setTimeout.bind(window, cleanUp, 3000));
                });
              });
            });
          });
        });
      });
    });
  });
}

function cleanUp() {
  if (emulatorCmdPendingCount > 0) {
    setTimeout(cleanUp, 100);
    return;
  }

  SpecialPowers.removePermission("mobileconnection", document);
  finish();
}

waitFor(testMobileOperatorNames, function () {
  return voice.connected;
});
