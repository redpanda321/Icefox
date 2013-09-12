/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/DOMRequestHelper.jsm");

const PAYMENTCONTENTHELPER_CID =
  Components.ID("{a920adc0-c36e-4fd0-8de0-aac1ac6ebbd0}");

const PAYMENT_IPC_MSG_NAMES = ["Payment:Success",
                               "Payment:Failed"];

XPCOMUtils.defineLazyServiceGetter(this, "cpmm",
                                   "@mozilla.org/childprocessmessagemanager;1",
                                   "nsIMessageSender");

function debug (s) {
  //dump("-*- PaymentContentHelper: " + s + "\n");
};

function PaymentContentHelper() {
};

PaymentContentHelper.prototype = {
  __proto__: DOMRequestIpcHelper.prototype,

  QueryInterface: XPCOMUtils.generateQI([Ci.nsIDOMNavigatorPayment,
                                         Ci.nsIDOMGlobalPropertyInitializer]),
  classID:        PAYMENTCONTENTHELPER_CID,
  classInfo:      XPCOMUtils.generateCI({
    classID: PAYMENTCONTENTHELPER_CID,
    contractID: "@mozilla.org/payment/content-helper;1",
    classDescription: "Payment Content Helper",
    flags: Ci.nsIClassInfo.DOM_OBJECT,
    interfaces: [Ci.nsIDOMNavigatorPayment]
  }),

  // nsIDOMNavigatorPayment

  pay: function pay(aJwts) {
    let request = this.createRequest();
    let requestId = this.getRequestId(request);

    let docShell = this._window.QueryInterface(Ci.nsIInterfaceRequestor)
                   .getInterface(Ci.nsIWebNavigation)
                   .QueryInterface(Ci.nsIDocShell);
    if (!docShell.isActive) {
      debug("The caller application is a background app. No request " +
            "will be sent");
      let runnable = {
        run: function run() {
          Services.DOMRequest.fireError(request, "BACKGROUND_APP");
        }
      }
      Services.tm.currentThread.dispatch(runnable,
                                         Ci.nsIThread.DISPATCH_NORMAL);
      return request;
    }

    if (!Array.isArray(aJwts)) {
      aJwts = [aJwts];
    }

    cpmm.sendAsyncMessage("Payment:Pay", {
      jwts: aJwts,
      requestId: requestId
    });
    return request;
  },

  // nsIDOMGlobalPropertyInitializer

  init: function(aWindow) {
    this._window = aWindow;
    this.initHelper(aWindow, PAYMENT_IPC_MSG_NAMES);
    return this.pay.bind(this);
  },

  // nsIFrameMessageListener

  receiveMessage: function receiveMessage(aMessage) {
    let name = aMessage.name;
    let msg = aMessage.json;
    debug("Received message '" + name + "': " + JSON.stringify(msg));
    let requestId = msg.requestId;
    let request = this.takeRequest(requestId);
    if (!request) {
      return;
    }
    switch (name) {
      case "Payment:Success":
        Services.DOMRequest.fireSuccess(request, msg.result);
        break;
      case "Payment:Failed":
        Services.DOMRequest.fireError(request, msg.errorMsg);
        break;
    }
  }
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([PaymentContentHelper]);
