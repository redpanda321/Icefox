/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 10000;

SpecialPowers.setBoolPref("dom.sms.enabled", true);
SpecialPowers.addPermission("sms", true, document);

let sms = window.navigator.mozSms;
let destNumber = "5551117777";
let msgText = "Mozilla Firefox OS!";
let gotSmsOnsent = false;
let gotReqOnsuccess = false;

function verifyInitialState() {
  log("Verifying initial state.");
  ok(sms, "mozSms");
  sendSms();
}

function sendSms() {
  let smsId = 0;

  log("Sending an SMS.");
  sms.onsent = function(event) {
    log("Received 'onsent' smsmanager event.");
    gotSmsOnsent = true;
    let sentSms = event.message;
    ok(sentSms, "outgoing sms");
    ok(sentSms.id, "sms id");
    smsId = sentSms.id;
    log("Sent SMS (id: " + smsId + ").");
    is(sentSms.body, msgText, "msg body");
    is(sentSms.delivery, "sent", "delivery");
    is(sentSms.deliveryStatus, "pending", "deliveryStatus");
    is(sentSms.read, true, "read");
    is(sentSms.receiver, destNumber, "receiver");
    is(sentSms.sender, null, "sender");
    is(sentSms.messageClass, "normal", "messageClass");
    ok(sentSms.timestamp instanceof Date, "timestamp is istanceof date");

    if (gotSmsOnsent && gotReqOnsuccess) { verifySmsExists(smsId); }
  };

  let requestRet = sms.send(destNumber, msgText);
  ok(requestRet, "smsrequest obj returned");

  requestRet.onsuccess = function(event) {
    log("Received 'onsuccess' smsrequest event.");
    gotReqOnsuccess = true;
    if(event.target.result){
      if (gotSmsOnsent && gotReqOnsuccess) { verifySmsExists(smsId); }
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

function verifySmsExists(smsId) {
  log("Getting SMS (id: " + smsId + ").");
  let requestRet = sms.getMessage(smsId);
  ok(requestRet, "smsrequest obj returned");

  requestRet.onsuccess = function(event) {
    log("Received 'onsuccess' smsrequest event.");
    ok(event.target.result, "smsrequest event.target.result");
    let foundSms = event.target.result;
    is(foundSms.id, smsId, "found SMS id matches");
    is(foundSms.body, msgText, "found SMS msg text matches");
    log("Got SMS (id: " + foundSms.id + ") as expected.");
    deleteSms(smsId);
  };

  requestRet.onerror = function(event) {
    log("Received 'onerror' smsrequest event.");
    ok(event.target.error, "domerror obj");
    is(event.target.error.name, "NotFoundError", "error returned");
    log("Could not get SMS (id: " + smsId + ") but should have.");
    ok(false,"SMS was not found");
    cleanUp();
  };
}

function deleteSms(smsId){
  log("Deleting SMS (id: " + smsId + ") using sms id parameter.");
  let requestRet = sms.delete(smsId);
  ok(requestRet,"smsrequest obj returned");

  requestRet.onsuccess = function(event) {
    log("Received 'onsuccess' smsrequest event.");
    if(event.target.result){
      verifySmsDeleted(smsId);
    } else {
      log("smsrequest returned false for sms.delete");
      ok(false,"SMS delete failed");
      cleanUp();
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

function verifySmsDeleted(smsId) {
  log("Getting SMS (id: " + smsId + ").");
  let requestRet = sms.getMessage(smsId);
  ok(requestRet, "smsrequest obj returned");

  requestRet.onsuccess = function(event) {
    log("Received 'onsuccess' smsrequest event.");
    ok(event.target.result, "smsrequest event.target.result");
    let foundSms = event.target.result;
    is(foundSms.id, smsId, "found SMS id matches");
    is(foundSms.body, msgText, "found SMS msg text matches");
    log("Got SMS (id: " + foundSms.id + ") but should not have.");
    ok(false, "SMS was not deleted");
    cleanUp();
  };

  requestRet.onerror = function(event) {
    log("Received 'onerror' smsrequest event.");
    ok(event.target.error, "domerror obj");
    is(event.target.error.name, "NotFoundError", "error returned");
    log("Could not get SMS (id: " + smsId + ") as expected.");
    cleanUp();
  };
}

function cleanUp() {
  sms.onsent = null;
  SpecialPowers.removePermission("sms", document);
  SpecialPowers.setBoolPref("dom.sms.enabled", false);
  finish();
}

// Start the test
verifyInitialState();
