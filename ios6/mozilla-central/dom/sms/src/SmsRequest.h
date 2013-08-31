/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_sms_SmsRequest_h
#define mozilla_dom_sms_SmsRequest_h

#include "nsIDOMSmsRequest.h"
#include "nsISmsRequest.h"
#include "nsDOMEventTargetHelper.h"

class nsIDOMMozSmsMessage;
class nsIDOMMozSmsCursor;

namespace mozilla {
namespace dom {
namespace sms {

class SmsRequestChild;
class SmsRequestParent;
class MessageReply;
class ThreadListItem;

// We need this forwarder to avoid a QI to nsIClassInfo.
// See: https://bugzilla.mozilla.org/show_bug.cgi?id=775997#c51 
class SmsRequestForwarder : public nsISmsRequest
{
  friend class SmsRequestChild;

public:
  NS_DECL_ISUPPORTS
  NS_FORWARD_NSISMSREQUEST(mRealRequest->)

  SmsRequestForwarder(nsISmsRequest* aRealRequest) {
    mRealRequest = aRealRequest;
  }

private:
  virtual
  ~SmsRequestForwarder() {}

  nsISmsRequest* GetRealRequest() {
    return mRealRequest;
  }

  nsCOMPtr<nsISmsRequest> mRealRequest;
};

class SmsManager;

class SmsRequest : public nsDOMEventTargetHelper
                 , public nsIDOMMozSmsRequest
                 , public nsISmsRequest
{
public:
  friend class SmsCursor;

  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMDOMREQUEST
  NS_DECL_NSISMSREQUEST
  NS_DECL_NSIDOMMOZSMSREQUEST

  NS_FORWARD_NSIDOMEVENTTARGET(nsDOMEventTargetHelper::)

  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(SmsRequest,
                                                         nsDOMEventTargetHelper)

  static already_AddRefed<nsIDOMMozSmsRequest> Create(SmsManager* aManager);

  static already_AddRefed<SmsRequest> Create(SmsRequestParent* requestParent);
  void Reset();

  void SetActorDied() {
    mParentAlive = false;
  }

  void
  NotifyThreadList(const InfallibleTArray<ThreadListItem>& aItems);

private:
  SmsRequest() MOZ_DELETE;

  SmsRequest(SmsManager* aManager);
  SmsRequest(SmsRequestParent* aParent);
  ~SmsRequest();

  nsresult SendMessageReply(const MessageReply& aReply);

  /**
   * Root mResult (jsval) to prevent garbage collection.
   */
  void RootResult();

  /**
   * Unroot mResult (jsval) to allow garbage collection.
   */
  void UnrootResult();

  /**
   * Set the object in a success state with the result being aMessage.
   */
  void SetSuccess(nsIDOMMozSmsMessage* aMessage);

  /**
   * Set the object in a success state with the result being a boolean.
   */
  void SetSuccess(bool aResult);

  /**
   * Set the object in a success state with the result being a SmsCursor.
   */
  void SetSuccess(nsIDOMMozSmsCursor* aCursor);

  /**
   * Set the object in a success state with the result being the given jsval.
   */
  void SetSuccess(const jsval& aVal);

  /**
   * Set the object in an error state with the error type being aError.
   */
  void SetError(int32_t aError);

  /**
   * Set the object in a success state with the result being the nsISupports
   * object in parameter.
   * @return whether setting the object was a success
   */
  bool SetSuccessInternal(nsISupports* aObject);

  nsresult DispatchTrustedEvent(const nsAString& aEventName);

  template <class T>
  nsresult NotifySuccess(T aParam);
  nsresult NotifyError(int32_t aError);

  jsval     mResult;
  bool      mResultRooted;
  bool      mDone;
  bool      mParentAlive;
  SmsRequestParent* mParent;
  nsCOMPtr<nsIDOMDOMError> mError;
  nsCOMPtr<nsIDOMMozSmsCursor> mCursor;
};

} // namespace sms
} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_sms_SmsRequest_h
