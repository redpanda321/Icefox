/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 10000;

SpecialPowers.setBoolPref("dom.sms.enabled", true);
SpecialPowers.addPermission("sms", true, document);

let sms = window.navigator.mozSms;
let myNumber = "15555215554";
let myNumberFormats = ["15555215554", "+15555215554"];
let inText = "Incoming SMS message. Mozilla Firefox OS!";
let remoteNumber = "5559997777";
let remoteNumberFormats = ["5559997777", "+15559997777"];
let outText = "Outgoing SMS message. Mozilla Firefox OS!";
let gotSmsOnsent = false;
let gotReqOnsuccess = false;
let inSmsid = 0;
let outSmsid = 0;
let inSmsTimeStamp;
let outSmsTimeStamp;

function verifyInitialState() {
  log("Verifying initial state.");
  ok(sms, "mozSms");
  simulateIncomingSms();  
}

function isIn(aVal, aArray, aMsg) {
  ok(aArray.indexOf(aVal) >= 0, aMsg);
}

function simulateIncomingSms() {
  log("Simulating incoming SMS.");

  sms.onreceived = function onreceived(event) {
    log("Received 'onreceived' smsmanager event.");
    let incomingSms = event.message;
    ok(incomingSms, "incoming sms");
    ok(incomingSms.id, "sms id");
    inSmsId = incomingSms.id;
    log("Received SMS (id: " + inSmsId + ").");
    is(incomingSms.body, inText, "msg body");
    is(incomingSms.delivery, "received", "delivery");
    is(incomingSms.deliveryStatus, "success", "deliveryStatus");
    is(incomingSms.read, false, "read");
    is(incomingSms.receiver, null, "receiver");
    isIn(incomingSms.sender, remoteNumberFormats, "sender");
    is(incomingSms.messageClass, "normal", "messageClass");
    ok(incomingSms.timestamp instanceof Date, "timestamp is instanceof date");
    inSmsTimeStamp = incomingSms.timestamp;
    sendSms();
  };
  // Simulate incoming sms sent from remoteNumber to our emulator
  runEmulatorCmd("sms send " + remoteNumber + " " + inText, function(result) {
    is(result[0], "OK", "emulator output");
  });
}

function sendSms() {
  log("Sending an SMS.");
  sms.onsent = function(event) {
    log("Received 'onsent' smsmanager event.");
    gotSmsOnsent = true;
    let sentSms = event.message;
    ok(sentSms, "outgoing sms");
    ok(sentSms.id, "sms id");
    outSmsId = sentSms.id;
    log("Sent SMS (id: " + outSmsId + ").");
    is(sentSms.body, outText, "msg body");
    is(sentSms.delivery, "sent", "delivery");
    is(sentSms.deliveryStatus, "pending", "deliveryStatus");
    is(sentSms.read, true, "read");
    isIn(sentSms.receiver, remoteNumberFormats, "receiver");
    is(sentSms.sender, null, "sender");
    is(sentSms.messageClass, "normal", "messageClass");
    ok(sentSms.timestamp instanceof Date, "timestamp is instanceof date");  
    outSmsTimeStamp = sentSms.timestamp;

    if (gotSmsOnsent && gotReqOnsuccess) { getReceivedSms(); }
  };

  let requestRet = sms.send(remoteNumber, outText);
  ok(requestRet, "smsrequest obj returned");

  requestRet.onsuccess = function(event) {
    log("Received 'onsuccess' smsrequest event.");
    gotReqOnsuccess = true;
    if(event.target.result){
      if (gotSmsOnsent && gotReqOnsuccess) { getReceivedSms(); }
    } else {
      log("smsrequest returned false for sms.send");
      ok(false,"SMS send failed");
      cleanUp();
    }
  };

  requestRet.onerror = function(event) {
    log("Received 'onerror' smsrequest event.");
    ok(event.target.error, "domerror obj");
    ok(false, "sms.send request returned unexpected error: "
        + event.target.error.name );
    cleanUp();
  };
}

function getReceivedSms() {
  log("Getting the received SMS message (id: " + inSmsId + ").");

  let requestRet = sms.getMessage(inSmsId);
  ok(requestRet, "smsrequest obj returned");

  requestRet.onsuccess = function(event) {
    log("Received 'onsuccess' smsrequest event.");
    ok(event.target.result, "smsrequest event.target.result");
    let foundSms = event.target.result;
    is(foundSms.id, inSmsId, "SMS id matches");
    log("Got SMS (id: " + foundSms.id + ").");
    is(foundSms.body, inText, "SMS msg text matches");
    is(foundSms.delivery, "received", "delivery");
    is(foundSms.deliveryStatus, "success", "deliveryStatus");
    is(foundSms.read, false, "read");
    isIn(foundSms.receiver, myNumberFormats, "receiver");
    isIn(foundSms.sender, remoteNumberFormats, "sender");
    is(foundSms.messageClass, "normal", "messageClass");
    ok(foundSms.timestamp instanceof Date, "timestamp is instanceof date");
    is(foundSms.timestamp.getTime(), inSmsTimeStamp.getTime(), "timestamp matches");
    getSentSms();
  };

  requestRet.onerror = function(event) {
    log("Received 'onerror' smsrequest event.");
    ok(event.target.error, "domerror obj");
    is(event.target.error.name, "NotFoundError", "error returned");
    log("Could not get SMS (id: " + inSmsId + ") but should have.");
    ok(false,"Could not get received SMS");
    cleanUp();
  };
}

function getSentSms() {
  log("Getting the sent SMS message (id: " + outSmsId + ").");
  let requestRet = sms.getMessage(outSmsId);
  ok(requestRet, "smsrequest obj returned");

  requestRet.onsuccess = function(event) {
    log("Received 'onsuccess' smsrequest event.");
    ok(event.target.result, "smsrequest event.target.result");
    let foundSms = event.target.result;
    is(foundSms.id, outSmsId, "SMS id matches");
    log("Got SMS (id: " + foundSms.id + ").");
    is(foundSms.body, outText, "SMS msg text matches");
    is(foundSms.delivery, "sent", "delivery");
    is(foundSms.deliveryStatus, "pending", "deliveryStatus");
    is(foundSms.read, true, "read");
    isIn(foundSms.receiver, remoteNumberFormats, "receiver");
    isIn(foundSms.sender, myNumberFormats, "sender");
    is(foundSms.messageClass, "normal", "messageClass");
    ok(foundSms.timestamp instanceof Date, "timestamp is instanceof date");
    is(foundSms.timestamp.getTime(), outSmsTimeStamp.getTime(), "timestamp matches");
    deleteMsgs();
  };

  requestRet.onerror = function(event) {
    log("Received 'onerror' smsrequest event.");
    ok(event.target.error, "domerror obj");
    is(event.target.error.name, "NotFoundError", "error returned");
    log("Could not get SMS (id: " + outSmsId + ") but should have.");
    ok(false,"Could not get sent SMS");
    cleanUp();
  };
}

function deleteMsgs() {
  log("Deleting SMS (id: " + inSmsId + ").");
  let requestRet = sms.delete(inSmsId);
  ok(requestRet,"smsrequest obj returned");

  requestRet.onsuccess = function(event) {
    log("Received 'onsuccess' smsrequest event.");
    if(event.target.result){
      log("Deleting SMS (id: " + outSmsId + ").");
      let nextReqRet = sms.delete(outSmsId);
      ok(nextReqRet,"smsrequest obj returned");

      nextReqRet.onsuccess = function(event) {
        log("Received 'onsuccess' smsrequest event.");
        if(event.target.result) {
          cleanUp();
        } else {
          log("smsrequest returned false for sms.delete");
          ok(false,"SMS delete failed");
        }
      };
    } else {
      log("smsrequest returned false for sms.delete");
      ok(false,"SMS delete failed");
    }
  };

  requestRet.onerror = function(event) {
    log("Received 'onerror' smsrequest event.");
    ok(event.target.error, "domerror obj");
    ok(false, "sms.delete request returned unexpected error: "
        + event.target.error.name );
    cleanUp();
  };
}

function cleanUp() {
  sms.onreceived = null;
  SpecialPowers.removePermission("sms", document);
  SpecialPowers.setBoolPref("dom.sms.enabled", false);
  finish();
}

// Start the test
verifyInitialState();
