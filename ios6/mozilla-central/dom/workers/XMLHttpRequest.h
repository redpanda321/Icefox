/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_workers_xmlhttprequest_h__
#define mozilla_dom_workers_xmlhttprequest_h__

#include "mozilla/dom/workers/bindings/XMLHttpRequestEventTarget.h"
#include "mozilla/dom/workers/bindings/WorkerFeature.h"

// Need this for XMLHttpRequestResponseType.
#include "mozilla/dom/XMLHttpRequestBinding.h"

#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/TypedArray.h"

BEGIN_WORKERS_NAMESPACE

class Proxy;
class XMLHttpRequestUpload;
class WorkerPrivate;

class XMLHttpRequest : public XMLHttpRequestEventTarget,
                       public WorkerFeature
{
public:
  struct StateData
  {
    nsString mResponseText;
    uint32_t mStatus;
    nsString mStatusText;
    uint16_t mReadyState;
    jsval mResponse;
    nsresult mResponseTextResult;
    nsresult mStatusResult;
    nsresult mResponseResult;

    StateData()
    : mStatus(0), mReadyState(0), mResponse(JSVAL_VOID),
      mResponseTextResult(NS_OK), mStatusResult(NS_OK),
      mResponseResult(NS_OK)
    { }
  };

private:
  JSObject* mJSObject;
  XMLHttpRequestUpload* mUpload;
  WorkerPrivate* mWorkerPrivate;
  nsRefPtr<Proxy> mProxy;
  XMLHttpRequestResponseType mResponseType;
  StateData mStateData;

  uint32_t mTimeout;

  bool mJSObjectRooted;
  bool mMultipart;
  bool mBackgroundRequest;
  bool mWithCredentials;
  bool mCanceled;

  bool mMozAnon;
  bool mMozSystem;

protected:
  XMLHttpRequest(JSContext* aCx, WorkerPrivate* aWorkerPrivate);
  virtual ~XMLHttpRequest();

public:
  virtual void
  _trace(JSTracer* aTrc) MOZ_OVERRIDE;

  virtual void
  _finalize(JSFreeOp* aFop) MOZ_OVERRIDE;

  static XMLHttpRequest*
  Constructor(JSContext* aCx, JSObject* aGlobal,
              const MozXMLHttpRequestParametersWorkers& aParams,
              ErrorResult& aRv);

  static XMLHttpRequest*
  Constructor(JSContext* aCx, JSObject* aGlobal,
              const nsAString& ignored, ErrorResult& aRv)
  {
    // Pretend like someone passed null, so we can pick up the default values
    MozXMLHttpRequestParametersWorkers params;
    if (!params.Init(aCx, nullptr, JS::NullValue())) {
      aRv.Throw(NS_ERROR_UNEXPECTED);
      return nullptr;
    }

    return Constructor(aCx, aGlobal, params, aRv);
  }

  void
  Unpin();

  bool
  Notify(JSContext* aCx, Status aStatus) MOZ_OVERRIDE;

#define IMPL_GETTER_AND_SETTER(_type)                                          \
  JSObject*                                                                    \
  GetOn##_type(JSContext* /* unused */, ErrorResult& aRv)                      \
  {                                                                            \
    return GetEventListener(NS_LITERAL_STRING(#_type), aRv);                   \
  }                                                                            \
                                                                               \
  void                                                                         \
  SetOn##_type(JSContext* /* unused */, JSObject* aListener, ErrorResult& aRv) \
  {                                                                            \
    SetEventListener(NS_LITERAL_STRING(#_type), aListener, aRv);               \
  }

  IMPL_GETTER_AND_SETTER(readystatechange)

#undef IMPL_GETTER_AND_SETTER

  uint16_t
  ReadyState() const
  {
    return mStateData.mReadyState;
  }

  void
  Open(const nsAString& aMethod, const nsAString& aUrl, bool aAsync,
       const Optional<nsAString>& aUser, const Optional<nsAString>& aPassword,
       ErrorResult& aRv);

  void
  SetRequestHeader(const nsAString& aHeader, const nsAString& aValue,
                   ErrorResult& aRv);

  uint32_t
  Timeout() const
  {
    return mTimeout;
  }

  void
  SetTimeout(uint32_t aTimeout, ErrorResult& aRv);

  bool
  WithCredentials() const
  {
    return mWithCredentials;
  }

  void
  SetWithCredentials(bool aWithCredentials, ErrorResult& aRv);

  bool
  Multipart() const
  {
    return mMultipart;
  }

  void
  SetMultipart(bool aMultipart, ErrorResult& aRv);

  bool
  MozBackgroundRequest() const
  {
    return mBackgroundRequest;
  }

  void
  SetMozBackgroundRequest(bool aBackgroundRequest, ErrorResult& aRv);

  XMLHttpRequestUpload*
  GetUpload(ErrorResult& aRv);

  void
  Send(ErrorResult& aRv);

  void
  Send(const nsAString& aBody, ErrorResult& aRv);

  void
  Send(JSObject* aBody, ErrorResult& aRv);

  void
  Send(JSObject& aBody, ErrorResult& aRv)
  {
    Send(&aBody, aRv);
  }

  void
  Send(ArrayBuffer& aBody, ErrorResult& aRv) {
    return Send(aBody.Obj(), aRv);
  }

  void
  Send(ArrayBufferView& aBody, ErrorResult& aRv) {
    return Send(aBody.Obj(), aRv);
  }

  void
  SendAsBinary(const nsAString& aBody, ErrorResult& aRv);

  void
  Abort(ErrorResult& aRv);

  uint16_t
  GetStatus(ErrorResult& aRv) const
  {
    aRv = mStateData.mStatusResult;
    return mStateData.mStatus;
  }

  void
  GetStatusText(nsAString& aStatusText) const
  {
    aStatusText = mStateData.mStatusText;
  }

  void
  GetResponseHeader(const nsAString& aHeader, nsAString& aResponseHeader,
                    ErrorResult& aRv);

  void
  GetAllResponseHeaders(nsAString& aResponseHeaders, ErrorResult& aRv);

  void
  OverrideMimeType(const nsAString& aMimeType, ErrorResult& aRv);

  XMLHttpRequestResponseType
  ResponseType() const
  {
    return mResponseType;
  }

  void
  SetResponseType(XMLHttpRequestResponseType aResponseType, ErrorResult& aRv);

  jsval
  GetResponse(JSContext* /* unused */, ErrorResult& aRv);

  void
  GetResponseText(nsAString& aResponseText, ErrorResult& aRv);

  JSObject*
  GetResponseXML() const
  {
    return NULL;
  }

  JSObject*
  GetChannel() const
  {
    return NULL;
  }

  JS::Value
  GetInterface(JSContext* cx, JSObject* aIID, ErrorResult& aRv)
  {
    aRv.Throw(NS_ERROR_FAILURE);
    return JSVAL_NULL;
  }

  XMLHttpRequestUpload*
  GetUploadObjectNoCreate() const
  {
    return mUpload;
  }

  void
  UpdateState(const StateData& aStateData)
  {
    mStateData = aStateData;
  }

  void
  NullResponseText()
  {
    mStateData.mResponseText.SetIsVoid(true);
    mStateData.mResponse = JSVAL_NULL;
  }

  bool MozAnon() const
  {
    return mMozAnon;
  }

  bool MozSystem() const
  {
    return mMozSystem;
  }

private:
  enum ReleaseType { Default, XHRIsGoingAway, WorkerIsGoingAway };

  void
  ReleaseProxy(ReleaseType aType = Default);

  void
  MaybePin(ErrorResult& aRv);

  void
  MaybeDispatchPrematureAbortEvents(ErrorResult& aRv);

  void
  DispatchPrematureAbortEvent(JSObject* aTarget, uint8_t aEventType,
                              bool aUploadTarget, ErrorResult& aRv);

  bool
  SendInProgress() const
  {
    return mJSObjectRooted;
  }

  void
  SendInternal(const nsAString& aStringBody,
               JSAutoStructuredCloneBuffer& aBody,
               nsTArray<nsCOMPtr<nsISupports> >& aClonedObjects,
               ErrorResult& aRv);
};

END_WORKERS_NAMESPACE

#endif // mozilla_dom_workers_xmlhttprequest_h__
