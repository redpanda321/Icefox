/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 10000;

SpecialPowers.addPermission("telephony", true, document);

let telephony = window.navigator.mozTelephony;
let outNumber = "5555551111";
let outgoingCall;

function dial() {
  log("Make an outgoing call.");
  outgoingCall = telephony.dial(outNumber);

  outgoingCall.onalerting = function onalerting(event) {
    log("Received 'alerting' call event.");
    answer();
  };  
}

function answer() {
  log("Answering the outgoing call.");

  outgoingCall.onconnected = function onconnectedOut(event) {
    log("Received 'connected' call event for the original outgoing call.");
    // just some code to keep call active for awhile
    callStartTime = Date.now();
    waitFor(cleanUp,function() {
      callDuration = Date.now() - callStartTime;
      log("Waiting while call is active, call duration (ms): " + callDuration);
      return(callDuration >= 2000);
    });
  };
  runEmulatorCmd("gsm accept " + outNumber);
}

function cleanUp(){
  outgoingCall.hangUp();
  ok("passed");
  finish();
}

dial();