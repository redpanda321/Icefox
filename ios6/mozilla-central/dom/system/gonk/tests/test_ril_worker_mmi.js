/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

subscriptLoader.loadSubScript("resource://gre/modules/ril_consts.js", this);

function run_test() {
  run_next_test();
}

function parseMMI(mmi) {
  let worker = newWorker({
    postRILMessage: function fakePostRILMessage(data) {
      // Do nothing
    },
    postMessage: function fakePostMessage(message) {
      // Do nothing
    }
  });
  return worker.RIL._parseMMI(mmi);
}

function getWorker() {
  let _postedMessage;
  let _worker = newWorker({
    postRILMessage: function fakePostRILMessage(data) {
    },
    postMessage: function fakePostMessage(message) {
      _postedMessage = message;
    },
  });

  return {
    get postedMessage() {
      return _postedMessage;
    },
    get worker() {
      return _worker;
    }
  };
}

function testSendMMI(mmi, error) {
  let workerhelper = getWorker();
  let worker = workerhelper.worker;

  do_print("worker.postMessage " + worker.postMessage);

  worker.RIL.sendMMI({mmi: mmi});

  let postedMessage = workerhelper.postedMessage;

  do_check_eq(postedMessage.rilMessageType, "sendMMI");
  do_check_eq(postedMessage.errorMsg, error);
}

add_test(function test_parseMMI_empty() {
  let mmi = parseMMI("");

  do_check_null(mmi);

  run_next_test();
});

add_test(function test_parseMMI_undefined() {
  let mmi = parseMMI();

  do_check_null(mmi);

  run_next_test();
});

add_test(function test_parseMMI_invalid() {
  let mmi = parseMMI("**");

  do_check_null(mmi);

  run_next_test();
});

add_test(function test_parseMMI_dial_string() {
  let mmi = parseMMI("12345");

  do_check_null(mmi);

  run_next_test();
});

add_test(function test_parseMMI_USSD() {
  let mmi = parseMMI("*123#");

  do_check_eq(mmi.fullMMI, "*123#");
  do_check_eq(mmi.procedure, MMI_PROCEDURE_ACTIVATION);
  do_check_eq(mmi.serviceCode, "123");
  do_check_eq(mmi.sia, undefined);
  do_check_eq(mmi.sib, undefined);
  do_check_eq(mmi.sic, undefined);
  do_check_eq(mmi.pwd, undefined);
  do_check_eq(mmi.dialNumber, "");

  run_next_test();
});

add_test(function test_parseMMI_sia() {
  let mmi = parseMMI("*123*1#");

  do_check_eq(mmi.fullMMI, "*123*1#");
  do_check_eq(mmi.procedure, MMI_PROCEDURE_ACTIVATION);
  do_check_eq(mmi.serviceCode, "123");
  do_check_eq(mmi.sia, "1");
  do_check_eq(mmi.sib, undefined);
  do_check_eq(mmi.sic, undefined);
  do_check_eq(mmi.pwd, undefined);
  do_check_eq(mmi.dialNumber, "");

  run_next_test();
});

add_test(function test_parseMMI_sib() {
  let mmi = parseMMI("*123**1#");

  do_check_eq(mmi.fullMMI, "*123**1#");
  do_check_eq(mmi.procedure, MMI_PROCEDURE_ACTIVATION);
  do_check_eq(mmi.serviceCode, "123");
  do_check_eq(mmi.sia, "");
  do_check_eq(mmi.sib, "1");
  do_check_eq(mmi.sic, undefined);
  do_check_eq(mmi.pwd, undefined);
  do_check_eq(mmi.dialNumber, "");

  run_next_test();
});

add_test(function test_parseMMI_sic() {
  let mmi = parseMMI("*123***1#");

  do_check_eq(mmi.fullMMI, "*123***1#");
  do_check_eq(mmi.procedure, MMI_PROCEDURE_ACTIVATION);
  do_check_eq(mmi.serviceCode, "123");
  do_check_eq(mmi.sia, "");
  do_check_eq(mmi.sib, "");
  do_check_eq(mmi.sic, "1");
  do_check_eq(mmi.pwd, undefined);
  do_check_eq(mmi.dialNumber, "");

  run_next_test();
});

add_test(function test_parseMMI_sia_sib() {
  let mmi = parseMMI("*123*1*1#");

  do_check_eq(mmi.fullMMI, "*123*1*1#");
  do_check_eq(mmi.procedure, MMI_PROCEDURE_ACTIVATION);
  do_check_eq(mmi.serviceCode, "123");
  do_check_eq(mmi.sia, "1");
  do_check_eq(mmi.sib, "1");
  do_check_eq(mmi.sic, undefined);
  do_check_eq(mmi.pwd, undefined);
  do_check_eq(mmi.dialNumber, "");

  run_next_test();
});

add_test(function test_parseMMI_sia_sic() {
  let mmi = parseMMI("*123*1**1#");

  do_check_eq(mmi.fullMMI, "*123*1**1#");
  do_check_eq(mmi.procedure, MMI_PROCEDURE_ACTIVATION);
  do_check_eq(mmi.serviceCode, "123");
  do_check_eq(mmi.sia, "1");
  do_check_eq(mmi.sib, "");
  do_check_eq(mmi.sic, "1");
  do_check_eq(mmi.pwd, undefined);
  do_check_eq(mmi.dialNumber, "");

  run_next_test();
});

add_test(function test_parseMMI_sib_sic() {
  let mmi = parseMMI("*123**1*1#");

  do_check_eq(mmi.fullMMI, "*123**1*1#");
  do_check_eq(mmi.procedure, MMI_PROCEDURE_ACTIVATION);
  do_check_eq(mmi.serviceCode, "123");
  do_check_eq(mmi.sia, "");
  do_check_eq(mmi.sib, "1");
  do_check_eq(mmi.sic, "1");
  do_check_eq(mmi.pwd, undefined);
  do_check_eq(mmi.dialNumber, "");

  run_next_test();
});

add_test(function test_parseMMI_pwd() {
  let mmi = parseMMI("*123****1#");

  do_check_eq(mmi.fullMMI, "*123****1#");
  do_check_eq(mmi.procedure, MMI_PROCEDURE_ACTIVATION);
  do_check_eq(mmi.serviceCode, "123");
  do_check_eq(mmi.sia, "");
  do_check_eq(mmi.sib, "");
  do_check_eq(mmi.sic, "");
  do_check_eq(mmi.pwd, "1");
  do_check_eq(mmi.dialNumber, "");

  run_next_test();
});

add_test(function test_parseMMI_dial_number() {
  let mmi = parseMMI("*123#345");

  do_check_eq(mmi.fullMMI, "*123#");
  do_check_eq(mmi.procedure, MMI_PROCEDURE_ACTIVATION);
  do_check_eq(mmi.serviceCode, "123");
  do_check_eq(mmi.sia, undefined);
  do_check_eq(mmi.sib, undefined);
  do_check_eq(mmi.sic, undefined);
  do_check_eq(mmi.pwd, undefined);
  do_check_eq(mmi.dialNumber, "345");

  run_next_test();
});


/**
 * MMI procedures tests
 */

add_test(function test_parseMMI_activation() {
  let mmi = parseMMI("*00*12*34*56#");

  do_check_eq(mmi.fullMMI, "*00*12*34*56#");
  do_check_eq(mmi.procedure, MMI_PROCEDURE_ACTIVATION);
  do_check_eq(mmi.serviceCode, "00");
  do_check_eq(mmi.sia, "12");
  do_check_eq(mmi.sib, "34");
  do_check_eq(mmi.sic, "56");
  do_check_eq(mmi.pwd, undefined);
  do_check_eq(mmi.dialNumber, "");

  run_next_test();
});

add_test(function test_parseMMI_deactivation() {
  let mmi = parseMMI("#00*12*34*56#");

  do_check_eq(mmi.fullMMI, "#00*12*34*56#");
  do_check_eq(mmi.procedure, MMI_PROCEDURE_DEACTIVATION);
  do_check_eq(mmi.serviceCode, "00");
  do_check_eq(mmi.sia, "12");
  do_check_eq(mmi.sib, "34");
  do_check_eq(mmi.sic, "56");
  do_check_eq(mmi.pwd, undefined);
  do_check_eq(mmi.dialNumber, "");

  run_next_test();
});

add_test(function test_parseMMI_interrogation() {
  let mmi = parseMMI("*#00*12*34*56#");

  do_check_eq(mmi.fullMMI, "*#00*12*34*56#");
  do_check_eq(mmi.procedure, MMI_PROCEDURE_INTERROGATION);
  do_check_eq(mmi.serviceCode, "00");
  do_check_eq(mmi.sia, "12");
  do_check_eq(mmi.sib, "34");
  do_check_eq(mmi.sic, "56");
  do_check_eq(mmi.pwd, undefined);
  do_check_eq(mmi.dialNumber, "");

  run_next_test();
});

add_test(function test_parseMMI_registration() {
  let mmi = parseMMI("**00*12*34*56#");

  do_check_eq(mmi.fullMMI, "**00*12*34*56#");
  do_check_eq(mmi.procedure, MMI_PROCEDURE_REGISTRATION);
  do_check_eq(mmi.serviceCode, "00");
  do_check_eq(mmi.sia, "12");
  do_check_eq(mmi.sib, "34");
  do_check_eq(mmi.sic, "56");
  do_check_eq(mmi.pwd, undefined);
  do_check_eq(mmi.dialNumber, "");

  run_next_test();
});

add_test(function test_parseMMI_erasure() {
  let mmi = parseMMI("##00*12*34*56#");

  do_check_eq(mmi.fullMMI, "##00*12*34*56#");
  do_check_eq(mmi.procedure, MMI_PROCEDURE_ERASURE);
  do_check_eq(mmi.serviceCode, "00");
  do_check_eq(mmi.sia, "12");
  do_check_eq(mmi.sib, "34");
  do_check_eq(mmi.sic, "56");
  do_check_eq(mmi.pwd, undefined);
  do_check_eq(mmi.dialNumber, "");

  run_next_test();
});

/**
 * sendMMI tests.
 */

add_test(function test_sendMMI_empty() {
  testSendMMI("", "NO_VALID_MMI_STRING");

  run_next_test();
});

add_test(function test_sendMMI_undefined() {
  testSendMMI({}, "NO_VALID_MMI_STRING");

  run_next_test();
});

add_test(function test_sendMMI_invalid() {
  testSendMMI("**", "NO_VALID_MMI_STRING");

  run_next_test();
});

add_test(function test_sendMMI_dial_string() {
  testSendMMI("123", "NO_VALID_MMI_STRING");

  run_next_test();
});

function setCallForwardSuccess(mmi) {
  let workerhelper = getWorker();
  let worker = workerhelper.worker;

  worker.RIL.setCallForward = function fakeSetCallForward(options) {
    worker.RIL[REQUEST_SET_CALL_FORWARD](0, {
      rilRequestError: ERROR_SUCCESS
    });
  };

  worker.RIL.sendMMI({mmi: mmi});

  let postedMessage = workerhelper.postedMessage;

  do_check_eq(postedMessage.errorMsg, GECKO_ERROR_SUCCESS);
  do_check_true(postedMessage.success);
}

add_test(function test_sendMMI_call_forwarding_activation() {
  setCallForwardSuccess("*21*12345*99*10#");

  run_next_test();
});

add_test(function test_sendMMI_call_forwarding_deactivation() {
  setCallForwardSuccess("#21*12345*99*10#");

  run_next_test();
});

add_test(function test_sendMMI_call_forwarding_interrogation() {
  let workerhelper = getWorker();
  let worker = workerhelper.worker;

  worker.Buf.readUint32 = function fakeReadUint32() {
    return worker.Buf.int32Array.pop();
  };

  worker.Buf.readString = function fakeReadString() {
    return "+34666222333";
  };

  worker.RIL.queryCallForwardStatus = function fakeQueryCallForward(options) {
    worker.Buf.int32Array = [
      0,   // rules.timeSeconds
      145, // rules.toa
      49,  // rules.serviceClass
      Ci.nsIDOMMozMobileCFInfo.CALL_FORWARD_REASON_UNCONDITIONAL, // rules.reason
      1,   // rules.active
      1    // rulesLength
    ];
    worker.RIL[REQUEST_QUERY_CALL_FORWARD_STATUS](1, {
      rilRequestError: ERROR_SUCCESS
    });
  };

  worker.RIL.sendMMI({mmi: "*#21#"});

  let postedMessage = workerhelper.postedMessage;

  do_check_eq(postedMessage.errorMsg, GECKO_ERROR_SUCCESS);
  do_check_true(postedMessage.success);
  do_check_true(Array.isArray(postedMessage.rules));
  do_check_eq(postedMessage.rules.length, 1);
  do_check_true(postedMessage.rules[0].active);
  do_check_eq(postedMessage.rules[0].reason,
              Ci.nsIDOMMozMobileCFInfo.CALL_FORWARD_REASON_UNCONDITIONAL);
  do_check_eq(postedMessage.rules[0].number, "+34666222333");
  run_next_test();
});

add_test(function test_sendMMI_call_forwarding_interrogation_no_rules() {
  let workerhelper = getWorker();
  let worker = workerhelper.worker;

  worker.Buf.readUint32 = function fakeReadUint32() {
    return 0;
  };

  worker.RIL.queryCallForwardStatus = function fakeQueryCallForward(options) {
    worker.RIL[REQUEST_QUERY_CALL_FORWARD_STATUS](1, {
      rilRequestError: ERROR_SUCCESS
    });
  };

  worker.RIL.sendMMI({mmi: "*#21#"});

  let postedMessage = workerhelper.postedMessage;

  do_check_eq(postedMessage.errorMsg,
              "Invalid rule length while querying call forwarding status.");
  do_check_false(postedMessage.success);

  run_next_test();
});


add_test(function test_sendMMI_call_forwarding_registration() {
  setCallForwardSuccess("**21*12345*99*10#");

  run_next_test();
});

add_test(function test_sendMMI_call_forwarding_erasure() {
  setCallForwardSuccess("##21*12345*99#");

  run_next_test();
});

add_test(function test_sendMMI_call_forwarding_CFB() {
  setCallForwardSuccess("*67*12345*99*10#");

  run_next_test();
});

add_test(function test_sendMMI_call_forwarding_CFNRy() {
  setCallForwardSuccess("*61*12345*99*10#");

  run_next_test();
});

add_test(function test_sendMMI_call_forwarding_CFNRc() {
  setCallForwardSuccess("*62*12345*99*10#");

  run_next_test();
});

add_test(function test_sendMMI_call_forwarding_CFAll() {
  setCallForwardSuccess("*004*12345*99*10#");

  run_next_test();
});

add_test(function test_sendMMI_call_forwarding_CFAllConditional() {
  setCallForwardSuccess("*002*12345*99*10#");

  run_next_test();
});

add_test(function test_sendMMI_change_PIN() {
  let workerhelper = getWorker();
  let worker = workerhelper.worker;

  worker.RIL.changeICCPIN = function fakeChangeICCPIN(options) {
    worker.RIL[REQUEST_ENTER_SIM_PIN](0, {
      rilRequestError: ERROR_SUCCESS
    });
  }

  worker.RIL.sendMMI({mmi: "**04*1234*4567*4567#"});

  let postedMessage = workerhelper.postedMessage;

  do_check_eq (postedMessage.errorMsg, GECKO_ERROR_SUCCESS);
  do_check_true(postedMessage.success);

  run_next_test();
});

add_test(function test_sendMMI_change_PIN_no_new_PIN() {
  testSendMMI("**04*1234**4567#", "MISSING_SUPPLEMENTARY_INFORMATION");

  run_next_test();
});

add_test(function test_sendMMI_change_PIN_no_old_PIN() {
  testSendMMI("**04**1234*4567#", "MISSING_SUPPLEMENTARY_INFORMATION");

  run_next_test();
});

add_test(function test_sendMMI_change_PIN_wrong_procedure() {
  testSendMMI("*04*1234*4567*4567#", "WRONG_MMI_PROCEDURE");

  run_next_test();
});

add_test(function test_sendMMI_change_PIN_new_PIN_mismatch() {
  testSendMMI("**04*4567*1234*4567#", "NEW_PIN_MISMATCH");

  run_next_test();
});

add_test(function test_sendMMI_change_PIN2() {
  let workerhelper = getWorker();
  let worker = workerhelper.worker;

  worker.RIL.changeICCPIN2 = function fakeChangeICCPIN2(options){
    worker.RIL[REQUEST_ENTER_SIM_PIN2](0, {
      rilRequestError: ERROR_SUCCESS
    });
  }

  worker.RIL.sendMMI({mmi: "**042*1234*4567*4567#"});

  let postedMessage = workerhelper.postedMessage;

  do_check_eq (postedMessage.errorMsg, GECKO_ERROR_SUCCESS);
  do_check_true(postedMessage.success);

  run_next_test();
});

add_test(function test_sendMMI_change_PIN2_no_new_PIN2() {
  testSendMMI("**042*1234**4567#", "MISSING_SUPPLEMENTARY_INFORMATION");

  run_next_test();
});

add_test(function test_sendMMI_change_PIN2_no_old_PIN2() {
  testSendMMI("**042**1234*4567#", "MISSING_SUPPLEMENTARY_INFORMATION");

  run_next_test();
});

add_test(function test_sendMMI_change_PIN2_wrong_procedure() {
  testSendMMI("*042*1234*4567*4567#", "WRONG_MMI_PROCEDURE");

  run_next_test();
});

add_test(function test_sendMMI_change_PIN2_new_PIN2_mismatch() {
  testSendMMI("**042*4567*1234*4567#", "NEW_PIN_MISMATCH");

  run_next_test();
});

add_test(function test_sendMMI_unblock_PIN() {
  let workerhelper = getWorker();
  let worker = workerhelper.worker;

  worker.RIL.enterICCPUK = function fakeEnterICCPUK(options){
    worker.RIL[REQUEST_ENTER_SIM_PUK](0, {
      rilRequestError: ERROR_SUCCESS
    });
  }

  worker.RIL.sendMMI({mmi: "**05*1234*4567*4567#"});

  let postedMessage = workerhelper.postedMessage;

  do_check_eq (postedMessage.errorMsg, GECKO_ERROR_SUCCESS);
  do_check_true(postedMessage.success);

  run_next_test();
});

add_test(function test_sendMMI_unblock_PIN_no_new_PIN() {
  testSendMMI("**05*1234**4567#", "MISSING_SUPPLEMENTARY_INFORMATION");

  run_next_test();
});

add_test(function test_sendMMI_unblock_PIN_no_PUK() {
  testSendMMI("**05**1234*4567#", "MISSING_SUPPLEMENTARY_INFORMATION");

  run_next_test();
});

add_test(function test_sendMMI_unblock_PIN_wrong_procedure() {
  testSendMMI("*05*1234*4567*4567#", "WRONG_MMI_PROCEDURE");

  run_next_test();
});

add_test(function test_sendMMI_unblock_PIN_new_PIN_mismatch() {
  testSendMMI("**05*4567*1234*4567#", "NEW_PIN_MISMATCH");

  run_next_test();
});

add_test(function test_sendMMI_unblock_PIN2() {
  let workerhelper = getWorker();
  let worker = workerhelper.worker;

  worker.RIL.enterICCPUK2 = function fakeEnterICCPUK2(options){
    worker.RIL[REQUEST_ENTER_SIM_PUK2](0, {
      rilRequestError: ERROR_SUCCESS
    });
  }

  worker.RIL.sendMMI({mmi: "**052*1234*4567*4567#"});

  let postedMessage = workerhelper.postedMessage;

  do_check_eq (postedMessage.errorMsg, GECKO_ERROR_SUCCESS);
  do_check_true(postedMessage.success);

  run_next_test();
});

add_test(function test_sendMMI_unblock_PIN2_no_new_PIN2() {
  testSendMMI("**052*1234**4567#", "MISSING_SUPPLEMENTARY_INFORMATION");

  run_next_test();
});

add_test(function test_sendMMI_unblock_PIN2_no_PUK2() {
  testSendMMI("**052**1234*4567#", "MISSING_SUPPLEMENTARY_INFORMATION");

  run_next_test();
});

add_test(function test_sendMMI_unblock_PIN2_wrong_procedure() {
  testSendMMI("*052*1234*4567*4567#", "WRONG_MMI_PROCEDURE");

  run_next_test();
});

add_test(function test_sendMMI_unblock_PIN2_new_PIN_mismatch() {
  testSendMMI("**052*4567*1234*4567#", "NEW_PIN_MISMATCH");

  run_next_test();
});

add_test(function test_sendMMI_get_IMEI() {
  let workerhelper = getWorker();
  let worker = workerhelper.worker;
  let mmiOptions;

  worker.RIL.getIMEI = function getIMEI(options){
    mmiOptions = options;
    worker.RIL[REQUEST_SEND_USSD](0, {
      rilRequestError: ERROR_SUCCESS,
    });
  }

  worker.RIL.sendMMI({mmi: "*#06#"});

  let postedMessage = workerhelper.postedMessage;

  do_check_true(mmiOptions.mmi);
  do_check_eq (postedMessage.errorMsg, GECKO_ERROR_SUCCESS);
  do_check_true(postedMessage.success);

  run_next_test();
});

add_test(function test_sendMMI_get_IMEI_error() {
  let workerhelper = getWorker();
  let worker = workerhelper.worker;
  let mmiOptions;

  worker.RIL.getIMEI = function getIMEI(options){
    mmiOptions = options;
    worker.RIL[REQUEST_SEND_USSD](0, {
      rilRequestError: ERROR_RADIO_NOT_AVAILABLE,
    });
  }

  worker.RIL.sendMMI({mmi: "*#06#"});

  let postedMessage = workerhelper.postedMessage;

  do_check_true(mmiOptions.mmi);
  do_check_eq (postedMessage.errorMsg, GECKO_ERROR_RADIO_NOT_AVAILABLE);
  do_check_false(postedMessage.success);

  run_next_test();
});

add_test(function test_sendMMI_call_barring() {
  testSendMMI("*33#", "CALL_BARRING_NOT_SUPPORTED_VIA_MMI");

  run_next_test();
});

add_test(function test_sendMMI_call_waiting() {
  testSendMMI("*43#", "CALL_WAITING_NOT_SUPPORTED_VIA_MMI");

  run_next_test();
});

add_test(function test_sendMMI_USSD() {
  let workerhelper = getWorker();
  let worker = workerhelper.worker;
  let ussdOptions;

  worker.RIL.sendUSSD = function fakeSendUSSD(options){
    ussdOptions = options;
    worker.RIL[REQUEST_SEND_USSD](0, {
      rilRequestError: ERROR_SUCCESS
    });
  }

  worker.RIL.sendMMI({mmi: "*123#"});

  let postedMessage = workerhelper.postedMessage;

  do_check_eq(ussdOptions.ussd, "*123#");
  do_check_eq (postedMessage.errorMsg, GECKO_ERROR_SUCCESS);
  do_check_true(postedMessage.success);
  do_check_true(worker.RIL._ussdSession);

  run_next_test();
});

add_test(function test_sendMMI_USSD_error() {
  let workerhelper = getWorker();
  let worker = workerhelper.worker;
  let ussdOptions;

  worker.RIL.sendUSSD = function fakeSendUSSD(options){
    ussdOptions = options;
    worker.RIL[REQUEST_SEND_USSD](0, {
      rilRequestError: ERROR_GENERIC_FAILURE
    });
  }

  worker.RIL.sendMMI({mmi: "*123#"});

  let postedMessage = workerhelper.postedMessage;

  do_check_eq(ussdOptions.ussd, "*123#");
  do_check_eq (postedMessage.errorMsg, GECKO_ERROR_GENERIC_FAILURE);
  do_check_false(postedMessage.success);
  do_check_false(worker.RIL._ussdSession);

  run_next_test();
});
