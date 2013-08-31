/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=40: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_telephony_telephonycall_h__
#define mozilla_dom_telephony_telephonycall_h__

#include "TelephonyCommon.h"

#include "nsIDOMTelephonyCall.h"
#include "nsIRadioInterfaceLayer.h"

class nsPIDOMWindow;

BEGIN_TELEPHONY_NAMESPACE

class TelephonyCall : public nsDOMEventTargetHelper,
                      public nsIDOMTelephonyCall
{
  nsRefPtr<Telephony> mTelephony;

  nsString mNumber;
  nsString mState;
  nsCOMPtr<nsIDOMDOMError> mError;

  uint32_t mCallIndex;
  uint16_t mCallState;
  bool mLive;
  bool mOutgoing;

public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMTELEPHONYCALL
  NS_FORWARD_NSIDOMEVENTTARGET(nsDOMEventTargetHelper::)
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(TelephonyCall,
                                           nsDOMEventTargetHelper)

  static already_AddRefed<TelephonyCall>
  Create(Telephony* aTelephony, const nsAString& aNumber, uint16_t aCallState,
         uint32_t aCallIndex = kOutgoingPlaceholderCallIndex);

  nsIDOMEventTarget*
  ToIDOMEventTarget() const
  {
    return static_cast<nsDOMEventTargetHelper*>(
             const_cast<TelephonyCall*>(this));
  }

  nsISupports*
  ToISupports() const
  {
    return ToIDOMEventTarget();
  }

  void
  ChangeState(uint16_t aCallState)
  {
    ChangeStateInternal(aCallState, true);
  }

  uint32_t
  CallIndex() const
  {
    return mCallIndex;
  }

  void
  UpdateCallIndex(uint32_t aCallIndex)
  {
    NS_ASSERTION(mCallIndex == kOutgoingPlaceholderCallIndex,
                 "Call index should not be set!");
    mCallIndex = aCallIndex;
  }

  uint16_t
  CallState() const
  {
    return mCallState;
  }

  bool
  IsOutgoing() const
  {
    return mOutgoing;
  }

  void
  NotifyError(const nsAString& aError);

private:
  TelephonyCall()
  : mCallIndex(kOutgoingPlaceholderCallIndex),
    mCallState(nsIRadioInterfaceLayer::CALL_STATE_UNKNOWN), mLive(false), mOutgoing(false)
  { }

  ~TelephonyCall()
  { }

  void
  ChangeStateInternal(uint16_t aCallState, bool aFireEvents);
};

END_TELEPHONY_NAMESPACE

#endif // mozilla_dom_telephony_telephonycall_h__
