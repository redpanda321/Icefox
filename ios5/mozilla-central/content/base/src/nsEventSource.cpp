/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Util.h"

#include "nsEventSource.h"
#include "nsNetUtil.h"
#include "nsMimeTypes.h"
#include "nsDOMMessageEvent.h"
#include "nsIJSContextStack.h"
#include "nsIPromptFactory.h"
#include "nsIWindowWatcher.h"
#include "nsPresContext.h"
#include "nsContentPolicyUtils.h"
#include "nsIStringBundle.h"
#include "nsIConsoleService.h"
#include "nsIObserverService.h"
#include "nsIScriptObjectPrincipal.h"
#include "jsdbgapi.h"
#include "nsJSUtils.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIScriptError.h"
#include "nsICharsetConverterManager.h"
#include "nsIChannelPolicy.h"
#include "nsIContentSecurityPolicy.h"
#include "nsContentUtils.h"
#include "mozilla/Preferences.h"
#include "xpcpublic.h"
#include "nsCrossSiteListenerProxy.h"
#include "nsWrapperCacheInlines.h"
#include "nsDOMEventTargetHelper.h"
#include "mozilla/Attributes.h"

using namespace mozilla;

#define REPLACEMENT_CHAR     (PRUnichar)0xFFFD
#define BOM_CHAR             (PRUnichar)0xFEFF
#define SPACE_CHAR           (PRUnichar)0x0020
#define CR_CHAR              (PRUnichar)0x000D
#define LF_CHAR              (PRUnichar)0x000A
#define COLON_CHAR           (PRUnichar)0x003A

#define DEFAULT_BUFFER_SIZE 4096

// Reconnection time related values in milliseconds. The default one is equal
// to the default value of the pref dom.server-events.default-reconnection-time
#define MIN_RECONNECTION_TIME_VALUE       500
#define DEFAULT_RECONNECTION_TIME_VALUE   5000
#define MAX_RECONNECTION_TIME_VALUE       PR_IntervalToMilliseconds(DELAY_INTERVAL_LIMIT)

nsEventSource::nsEventSource() :
  mStatus(PARSE_STATE_OFF),
  mFrozen(false),
  mErrorLoadOnRedirect(false),
  mGoingToDispatchAllMessages(false),
  mWithCredentials(false),
  mWaitingForOnStopRequest(false),
  mLastConvertionResult(NS_OK),
  mReadyState(nsIEventSource::CONNECTING),
  mScriptLine(0),
  mInnerWindowID(0)
{
}

nsEventSource::~nsEventSource()
{
  Close();
}

//-----------------------------------------------------------------------------
// nsEventSource::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_CYCLE_COLLECTION_CLASS(nsEventSource)

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(nsEventSource)
  bool isBlack = tmp->IsBlack();
  if (isBlack || tmp->mWaitingForOnStopRequest) {
    if (tmp->mListenerManager) {
      tmp->mListenerManager->UnmarkGrayJSListeners();
      NS_UNMARK_LISTENER_WRAPPER(Open)
      NS_UNMARK_LISTENER_WRAPPER(Message)
      NS_UNMARK_LISTENER_WRAPPER(Error)
    }
    if (!isBlack && tmp->PreservingWrapper()) {
      xpc_UnmarkGrayObject(tmp->GetWrapperPreserveColor());
    }
    return true;
  }
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(nsEventSource)
  return tmp->IsBlack();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(nsEventSource)
  return tmp->IsBlack();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(nsEventSource,
                                               nsDOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsEventSource,
                                                  nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mSrc)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mNotificationCallbacks)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mLoadGroup)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mChannelEventSink)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mHttpChannel)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mTimer)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mOnOpenListener)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mOnMessageListener)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mOnErrorListener)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mUnicodeDecoder)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(nsEventSource, nsDOMEventTargetHelper)
  tmp->Close();
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mOnOpenListener)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mOnMessageListener)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mOnErrorListener)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

DOMCI_DATA(EventSource, nsEventSource)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(nsEventSource)
  NS_INTERFACE_MAP_ENTRY(nsIEventSource)
  NS_INTERFACE_MAP_ENTRY(nsIJSNativeInitializer)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsIRequestObserver)
  NS_INTERFACE_MAP_ENTRY(nsIStreamListener)
  NS_INTERFACE_MAP_ENTRY(nsIChannelEventSink)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(EventSource)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(nsEventSource, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(nsEventSource, nsDOMEventTargetHelper)

void
nsEventSource::DisconnectFromOwner()
{
  nsDOMEventTargetHelper::DisconnectFromOwner();
  NS_DISCONNECT_EVENT_HANDLER(Open)
  NS_DISCONNECT_EVENT_HANDLER(Message)
  NS_DISCONNECT_EVENT_HANDLER(Error)
  Close();
}

//-----------------------------------------------------------------------------
// nsEventSource::nsIEventSource
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsEventSource::GetUrl(nsAString& aURL)
{
  aURL = mOriginalURL;
  return NS_OK;
}

NS_IMETHODIMP
nsEventSource::GetReadyState(PRInt32 *aReadyState)
{
  NS_ENSURE_ARG_POINTER(aReadyState);
  *aReadyState = mReadyState;
  return NS_OK;
}

NS_IMETHODIMP
nsEventSource::GetWithCredentials(bool *aWithCredentials)
{
  NS_ENSURE_ARG_POINTER(aWithCredentials);
  *aWithCredentials = mWithCredentials;
  return NS_OK;
}

#define NS_EVENTSRC_IMPL_DOMEVENTLISTENER(_eventlistenername, _eventlistener)  \
  NS_IMETHODIMP                                                                \
  nsEventSource::GetOn##_eventlistenername(nsIDOMEventListener * *aListener)   \
  {                                                                            \
    return GetInnerEventListener(_eventlistener, aListener);                   \
  }                                                                            \
                                                                               \
  NS_IMETHODIMP                                                                \
  nsEventSource::SetOn##_eventlistenername(nsIDOMEventListener * aListener)    \
  {                                                                            \
    return RemoveAddEventListener(NS_LITERAL_STRING(#_eventlistenername),      \
                                  _eventlistener, aListener);                  \
  }

NS_EVENTSRC_IMPL_DOMEVENTLISTENER(open, mOnOpenListener)
NS_EVENTSRC_IMPL_DOMEVENTLISTENER(error, mOnErrorListener)
NS_EVENTSRC_IMPL_DOMEVENTLISTENER(message, mOnMessageListener)

NS_IMETHODIMP
nsEventSource::Close()
{
  if (mReadyState == nsIEventSource::CLOSED) {
    return NS_OK;
  }

  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (os) {
    os->RemoveObserver(this, DOM_WINDOW_DESTROYED_TOPIC);
    os->RemoveObserver(this, DOM_WINDOW_FROZEN_TOPIC);
    os->RemoveObserver(this, DOM_WINDOW_THAWED_TOPIC);
  }

  if (mTimer) {
    mTimer->Cancel();
    mTimer = nsnull;
  }

  ResetConnection();

  ClearFields();

  while (mMessagesToDispatch.GetSize() != 0) {
    delete static_cast<Message*>(mMessagesToDispatch.PopFront());
  }

  mSrc = nsnull;
  mFrozen = false;

  mUnicodeDecoder = nsnull;

  mReadyState = nsIEventSource::CLOSED;

  return NS_OK;
}

/**
 * This Init method should only be called by C++ consumers.
 */
NS_IMETHODIMP
nsEventSource::Init(nsIPrincipal* aPrincipal,
                    nsIScriptContext* aScriptContext,
                    nsPIDOMWindow* aOwnerWindow,
                    const nsAString& aURL,
                    bool aWithCredentials)
{
  NS_ENSURE_ARG(aPrincipal);

  if (mReadyState != nsIEventSource::CONNECTING || !PrefEnabled()) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  mPrincipal = aPrincipal;
  mWithCredentials = aWithCredentials;
  if (aOwnerWindow) {
    BindToOwner(aOwnerWindow->IsOuterWindow() ?
      aOwnerWindow->GetCurrentInnerWindow() : aOwnerWindow);
  } else {
    BindToOwner(aOwnerWindow);
  }

  nsCOMPtr<nsIJSContextStack> stack =
    do_GetService("@mozilla.org/js/xpc/ContextStack;1");
  JSContext* cx = nsnull;
  if (stack && NS_SUCCEEDED(stack->Peek(&cx)) && cx) {
    const char *filename;
    if (nsJSUtils::GetCallingLocation(cx, &filename, &mScriptLine)) {
      mScriptFile.AssignASCII(filename);
    }

    mInnerWindowID = nsJSUtils::GetCurrentlyRunningCodeInnerWindowID(cx);
  }

  // Get the load group for the page. When requesting we'll add ourselves to it.
  // This way any pending requests will be automatically aborted if the user
  // leaves the page.
  nsresult rv;
  nsIScriptContext* sc = GetContextForEventHandlers(&rv);
  if (sc) {
    nsCOMPtr<nsIDocument> doc =
      nsContentUtils::GetDocumentFromScriptContext(sc);
    if (doc) {
      mLoadGroup = doc->GetDocumentLoadGroup();
    }
  }

  // get the src
  nsCOMPtr<nsIURI> baseURI;
  rv = GetBaseURI(getter_AddRefs(baseURI));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> srcURI;
  rv = NS_NewURI(getter_AddRefs(srcURI), aURL, nsnull, baseURI);
  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_SYNTAX_ERR);

  // we observe when the window freezes and thaws
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  NS_ENSURE_STATE(os);

  rv = os->AddObserver(this, DOM_WINDOW_DESTROYED_TOPIC, true);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = os->AddObserver(this, DOM_WINDOW_FROZEN_TOPIC, true);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = os->AddObserver(this, DOM_WINDOW_THAWED_TOPIC, true);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString origin;
  rv = nsContentUtils::GetUTFOrigin(srcURI, origin);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString spec;
  rv = srcURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  mOriginalURL = NS_ConvertUTF8toUTF16(spec);
  mSrc = srcURI;
  mOrigin = origin;

  mReconnectionTime =
    Preferences::GetInt("dom.server-events.default-reconnection-time",
                        DEFAULT_RECONNECTION_TIME_VALUE);

  nsCOMPtr<nsICharsetConverterManager> convManager =
    do_GetService(NS_CHARSETCONVERTERMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = convManager->GetUnicodeDecoder("UTF-8", getter_AddRefs(mUnicodeDecoder));
  NS_ENSURE_SUCCESS(rv, rv);
  mUnicodeDecoder->SetInputErrorBehavior(nsIUnicodeDecoder::kOnError_Recover);

  // the constructor should throw a SYNTAX_ERROR only if it fails resolving the
  // url parameter, so we don't care about the InitChannelAndRequestEventSource
  // result.
  InitChannelAndRequestEventSource();

  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsEventSource::nsIJSNativeInitializer methods:
//-----------------------------------------------------------------------------

/**
 * This Initialize method is called from XPConnect via nsIJSNativeInitializer.
 * It is used for constructing our nsEventSource from javascript. It expects a
 * URL string parameter. Also, initializes the principal, the script context
 * and the window owner.
 */
NS_IMETHODIMP
nsEventSource::Initialize(nsISupports* aOwner,
                          JSContext* aContext,
                          JSObject* aObject,
                          PRUint32 aArgc,
                          jsval* aArgv)
{
  if (mReadyState != nsIEventSource::CONNECTING || !PrefEnabled() ||
      aArgc < 1) {
    return NS_ERROR_FAILURE;
  }

  JSAutoRequest ar(aContext);

  JSString* jsstr = JS_ValueToString(aContext, aArgv[0]);
  if (!jsstr) {
    return NS_ERROR_DOM_SYNTAX_ERR;
  }

  JS::Anchor<JSString *> deleteProtector(jsstr);
  size_t length;
  const jschar *chars = JS_GetStringCharsAndLength(aContext, jsstr, &length);
  if (!chars) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  nsAutoString urlParam;

  urlParam.Assign(chars, length);

  nsCOMPtr<nsPIDOMWindow> ownerWindow = do_QueryInterface(aOwner);
  NS_ENSURE_STATE(ownerWindow);

  nsCOMPtr<nsIScriptGlobalObject> sgo = do_QueryInterface(aOwner);
  NS_ENSURE_STATE(sgo);
  nsCOMPtr<nsIScriptContext> scriptContext = sgo->GetContext();
  NS_ENSURE_STATE(scriptContext);

  nsCOMPtr<nsIScriptObjectPrincipal> scriptPrincipal =
    do_QueryInterface(aOwner);
  NS_ENSURE_STATE(scriptPrincipal);
  nsCOMPtr<nsIPrincipal> principal = scriptPrincipal->GetPrincipal();
  NS_ENSURE_STATE(principal);

  bool withCredentialsParam = false;
  if (aArgc >= 2) {
    NS_ENSURE_TRUE(!JSVAL_IS_PRIMITIVE(aArgv[1]), NS_ERROR_INVALID_ARG);

    JSObject *obj = JSVAL_TO_OBJECT(aArgv[1]);
    NS_ASSERTION(obj, "obj shouldn't be null!!");

    JSBool hasProperty = JS_FALSE;
    NS_ENSURE_TRUE(JS_HasProperty(aContext, obj, "withCredentials",
                                  &hasProperty), NS_ERROR_FAILURE);

    if (hasProperty) {
      jsval withCredentialsVal;
      NS_ENSURE_TRUE(JS_GetProperty(aContext, obj, "withCredentials",
                                    &withCredentialsVal), NS_ERROR_FAILURE);

      JSBool withCredentials = JS_FALSE;
      NS_ENSURE_TRUE(JS_ValueToBoolean(aContext, withCredentialsVal,
                                       &withCredentials), NS_ERROR_FAILURE);
      withCredentialsParam = !!withCredentials;
    }
  }

  return Init(principal, scriptContext, ownerWindow,
              urlParam, withCredentialsParam);
}

//-----------------------------------------------------------------------------
// nsEventSource::nsIObserver
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsEventSource::Observe(nsISupports* aSubject,
                       const char* aTopic,
                       const PRUnichar* aData)
{
  if (mReadyState == nsIEventSource::CLOSED) {
    return NS_OK;
  }

  nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(aSubject);
  if (!GetOwner() || window != GetOwner()) {
    return NS_OK;
  }

  DebugOnly<nsresult> rv;
  if (strcmp(aTopic, DOM_WINDOW_FROZEN_TOPIC) == 0) {
    rv = Freeze();
    NS_ASSERTION(rv, "Freeze() failed");
  } else if (strcmp(aTopic, DOM_WINDOW_THAWED_TOPIC) == 0) {
    rv = Thaw();
    NS_ASSERTION(rv, "Thaw() failed");
  } else if (strcmp(aTopic, DOM_WINDOW_DESTROYED_TOPIC) == 0) {
    Close();
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsEventSource::nsIStreamListener
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsEventSource::OnStartRequest(nsIRequest *aRequest,
                              nsISupports *ctxt)
{
  nsresult rv = CheckHealthOfRequestCallback(aRequest);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aRequest, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  bool requestSucceeded;
  rv = httpChannel->GetRequestSucceeded(&requestSucceeded);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString contentType;
  rv = httpChannel->GetContentType(contentType);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!requestSucceeded || !contentType.EqualsLiteral(TEXT_EVENT_STREAM)) {
    DispatchFailConnection();
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsIPrincipal> principal = mPrincipal;
  if (nsContentUtils::IsSystemPrincipal(principal)) {
    // Don't give this channel the system principal.
    principal = do_CreateInstance("@mozilla.org/nullprincipal;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  rv = httpChannel->SetOwner(principal);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIRunnable> event =
    NS_NewRunnableMethod(this, &nsEventSource::AnnounceConnection);
  NS_ENSURE_STATE(event);

  rv = NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);

  mStatus = PARSE_STATE_BEGIN_OF_STREAM;

  return NS_OK;
}

// this method parses the characters as they become available instead of
// buffering them.
NS_METHOD
nsEventSource::StreamReaderFunc(nsIInputStream *aInputStream,
                                void *aClosure,
                                const char *aFromRawSegment,
                                PRUint32 aToOffset,
                                PRUint32 aCount,
                                PRUint32 *aWriteCount)
{
  nsEventSource* thisObject = static_cast<nsEventSource*>(aClosure);
  if (!thisObject || !aWriteCount) {
    NS_WARNING("nsEventSource cannot read from stream: no aClosure or aWriteCount");
    return NS_ERROR_FAILURE;
  }

  *aWriteCount = 0;

  PRInt32 srcCount, outCount;
  PRUnichar out[2];
  nsresult rv;

  const char *p = aFromRawSegment,
             *end = aFromRawSegment + aCount;

  do {
    srcCount = aCount - (p - aFromRawSegment);
    outCount = 2;

    thisObject->mLastConvertionResult =
      thisObject->mUnicodeDecoder->Convert(p, &srcCount, out, &outCount);

    if (thisObject->mLastConvertionResult == NS_ERROR_ILLEGAL_INPUT) {
      // There's an illegal byte in the input. It's now the responsibility
      // of this calling code to output a U+FFFD REPLACEMENT CHARACTER, advance
      // over the bad byte and reset the decoder.
      rv = thisObject->ParseCharacter(REPLACEMENT_CHAR);
      NS_ENSURE_SUCCESS(rv, rv);
      p = p + srcCount + 1;
      thisObject->mUnicodeDecoder->Reset();
    } else {
      for (PRInt32 i = 0; i < outCount; ++i) {
        rv = thisObject->ParseCharacter(out[i]);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      p = p + srcCount;
    }
  } while (p < end &&
           thisObject->mLastConvertionResult != NS_PARTIAL_MORE_INPUT &&
           thisObject->mLastConvertionResult != NS_OK);

  // check if the last byte was a bad one and
  // clear the state since it was handled above.
  if (thisObject->mLastConvertionResult == NS_ERROR_ILLEGAL_INPUT) {
    thisObject->mLastConvertionResult = NS_OK;
  }

  *aWriteCount = aCount;
  return NS_OK;
}

NS_IMETHODIMP
nsEventSource::OnDataAvailable(nsIRequest *aRequest,
                               nsISupports *aContext,
                               nsIInputStream *aInputStream,
                               PRUint32 aOffset,
                               PRUint32 aCount)
{
  NS_ENSURE_ARG_POINTER(aInputStream);

  nsresult rv = CheckHealthOfRequestCallback(aRequest);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 totalRead;
  return aInputStream->ReadSegments(nsEventSource::StreamReaderFunc, this,
                                    aCount, &totalRead);
}

NS_IMETHODIMP
nsEventSource::OnStopRequest(nsIRequest *aRequest,
                             nsISupports *aContext,
                             nsresult aStatusCode)
{
  mWaitingForOnStopRequest = false;

  if (mReadyState == nsIEventSource::CLOSED) {
    return NS_ERROR_ABORT;
  }

  if (NS_FAILED(aStatusCode)) {
    DispatchFailConnection();
    return aStatusCode;
  }

  nsresult rv;
  nsresult healthOfRequestResult = CheckHealthOfRequestCallback(aRequest);
  if (NS_SUCCEEDED(healthOfRequestResult)) {
    // check if we had an incomplete UTF8 char at the end of the stream
    if (mLastConvertionResult == NS_PARTIAL_MORE_INPUT) {
      rv = ParseCharacter(REPLACEMENT_CHAR);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // once we reach the end of the stream we must
    // dispatch the current event
    switch (mStatus)
    {
      case PARSE_STATE_CR_CHAR:
      case PARSE_STATE_COMMENT:
      case PARSE_STATE_FIELD_NAME:
      case PARSE_STATE_FIRST_CHAR_OF_FIELD_VALUE:
      case PARSE_STATE_FIELD_VALUE:
      case PARSE_STATE_BEGIN_OF_LINE:
        rv = SetFieldAndClear();
        NS_ENSURE_SUCCESS(rv, rv);

        rv = DispatchCurrentMessageEvent();  // there is an empty line (CRCR)
        NS_ENSURE_SUCCESS(rv, rv);

        break;

      // Just for not getting warnings when compiling
      case PARSE_STATE_OFF:
      case PARSE_STATE_BEGIN_OF_STREAM:
      case PARSE_STATE_BOM_WAS_READ:
        break;
    }
  }

  nsCOMPtr<nsIRunnable> event =
    NS_NewRunnableMethod(this, &nsEventSource::ReestablishConnection);
  NS_ENSURE_STATE(event);

  rv = NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);

  return healthOfRequestResult;
}

/**
 * Simple helper class that just forwards the redirect callback back
 * to the nsEventSource.
 */
class AsyncVerifyRedirectCallbackFwr MOZ_FINAL : public nsIAsyncVerifyRedirectCallback
{
public:
  AsyncVerifyRedirectCallbackFwr(nsEventSource* aEventsource)
    : mEventSource(aEventsource)
  {
  }

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(AsyncVerifyRedirectCallbackFwr)

  // nsIAsyncVerifyRedirectCallback implementation
  NS_IMETHOD OnRedirectVerifyCallback(nsresult aResult)
  {
    nsresult rv = mEventSource->OnRedirectVerifyCallback(aResult);
    if (NS_FAILED(rv)) {
      mEventSource->mErrorLoadOnRedirect = true;
      mEventSource->DispatchFailConnection();
    }

    return NS_OK;
  }

private:
  nsRefPtr<nsEventSource> mEventSource;
};

NS_IMPL_CYCLE_COLLECTION_CLASS(AsyncVerifyRedirectCallbackFwr)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(AsyncVerifyRedirectCallbackFwr)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR_AMBIGUOUS(mEventSource, nsIEventSource)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(AsyncVerifyRedirectCallbackFwr)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mEventSource)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(AsyncVerifyRedirectCallbackFwr)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsIAsyncVerifyRedirectCallback)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(AsyncVerifyRedirectCallbackFwr)
NS_IMPL_CYCLE_COLLECTING_RELEASE(AsyncVerifyRedirectCallbackFwr)

//-----------------------------------------------------------------------------
// nsEventSource::nsIChannelEventSink
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsEventSource::AsyncOnChannelRedirect(nsIChannel *aOldChannel,
                                      nsIChannel *aNewChannel,
                                      PRUint32    aFlags,
                                      nsIAsyncVerifyRedirectCallback *aCallback)
{
  nsCOMPtr<nsIRequest> aOldRequest = do_QueryInterface(aOldChannel);
  NS_PRECONDITION(aOldRequest, "Redirect from a null request?");

  nsresult rv = CheckHealthOfRequestCallback(aOldRequest);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_PRECONDITION(aNewChannel, "Redirect without a channel?");

  nsCOMPtr<nsIURI> newURI;
  rv = NS_GetFinalChannelURI(aNewChannel, getter_AddRefs(newURI));
  NS_ENSURE_SUCCESS(rv, rv);

  if (!CheckCanRequestSrc(newURI)) {
    DispatchFailConnection();
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  // Prepare to receive callback
  mRedirectFlags = aFlags;
  mRedirectCallback = aCallback;
  mNewRedirectChannel = aNewChannel;

  if (mChannelEventSink) {
    nsRefPtr<AsyncVerifyRedirectCallbackFwr> fwd =
      new AsyncVerifyRedirectCallbackFwr(this);

    rv = mChannelEventSink->AsyncOnChannelRedirect(aOldChannel,
                                                   aNewChannel,
                                                   aFlags, fwd);
    if (NS_FAILED(rv)) {
      mRedirectCallback = nsnull;
      mNewRedirectChannel = nsnull;
      mErrorLoadOnRedirect = true;
      DispatchFailConnection();
    }
    return rv;
  }
  OnRedirectVerifyCallback(NS_OK);
  return NS_OK;
}

nsresult
nsEventSource::OnRedirectVerifyCallback(nsresult aResult)
{
  NS_ABORT_IF_FALSE(mRedirectCallback, "mRedirectCallback not set in callback");
  NS_ABORT_IF_FALSE(mNewRedirectChannel,
                    "mNewRedirectChannel not set in callback");

  NS_ENSURE_SUCCESS(aResult, aResult);

  // update our channel

  mHttpChannel = do_QueryInterface(mNewRedirectChannel);
  NS_ENSURE_STATE(mHttpChannel);

  nsresult rv = SetupHttpChannel();
  NS_ENSURE_SUCCESS(rv, rv);

  if ((mRedirectFlags & nsIChannelEventSink::REDIRECT_PERMANENT) != 0) {
    rv = NS_GetFinalChannelURI(mHttpChannel, getter_AddRefs(mSrc));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  mNewRedirectChannel = nsnull;

  mRedirectCallback->OnRedirectVerifyCallback(aResult);
  mRedirectCallback = nsnull;

  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsEventSource::nsIInterfaceRequestor
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsEventSource::GetInterface(const nsIID & aIID,
                            void **aResult)
{
  // Make sure to return ourselves for the channel event sink interface,
  // no matter what.  We can forward these to mNotificationCallbacks
  // if it wants to get notifications for them.  But we
  // need to see these notifications for proper functioning.
  if (aIID.Equals(NS_GET_IID(nsIChannelEventSink))) {
    mChannelEventSink = do_GetInterface(mNotificationCallbacks);
    *aResult = static_cast<nsIChannelEventSink*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }

  // Now give mNotificationCallbacks (if non-null) a chance to return the
  // desired interface.
  if (mNotificationCallbacks) {
    nsresult rv = mNotificationCallbacks->GetInterface(aIID, aResult);
    if (NS_SUCCEEDED(rv)) {
      NS_ASSERTION(*aResult, "Lying nsIInterfaceRequestor implementation!");
      return rv;
    }
  }

  if (aIID.Equals(NS_GET_IID(nsIAuthPrompt)) ||
      aIID.Equals(NS_GET_IID(nsIAuthPrompt2))) {
    nsresult rv = CheckInnerWindowCorrectness();
    NS_ENSURE_SUCCESS(rv, NS_ERROR_UNEXPECTED);

    nsCOMPtr<nsIPromptFactory> wwatch =
      do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    // Get the an auth prompter for our window so that the parenting
    // of the dialogs works as it should when using tabs.

    nsCOMPtr<nsIDOMWindow> window;
    if (GetOwner()) {
      window = GetOwner()->GetOuterWindow();
    }

    return wwatch->GetPrompt(window, aIID, aResult);
  }

  return QueryInterface(aIID, aResult);
}

// static
bool
nsEventSource::PrefEnabled()
{
  return Preferences::GetBool("dom.server-events.enabled", false);
}

nsresult
nsEventSource::GetBaseURI(nsIURI **aBaseURI)
{
  NS_ENSURE_ARG_POINTER(aBaseURI);

  *aBaseURI = nsnull;

  nsCOMPtr<nsIURI> baseURI;

  // first we try from document->GetBaseURI()
  nsresult rv;
  nsIScriptContext* sc = GetContextForEventHandlers(&rv);
  nsCOMPtr<nsIDocument> doc =
    nsContentUtils::GetDocumentFromScriptContext(sc);
  if (doc) {
    baseURI = doc->GetBaseURI();
  }

  // otherwise we get from the doc's principal
  if (!baseURI) {
    rv = mPrincipal->GetURI(getter_AddRefs(baseURI));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  NS_ENSURE_STATE(baseURI);

  baseURI.forget(aBaseURI);
  return NS_OK;
}

nsresult
nsEventSource::SetupHttpChannel()
{
  mHttpChannel->SetRequestMethod(NS_LITERAL_CSTRING("GET"));

  /* set the http request headers */

  mHttpChannel->SetRequestHeader(NS_LITERAL_CSTRING("Accept"),
    NS_LITERAL_CSTRING(TEXT_EVENT_STREAM), false);

  // LOAD_BYPASS_CACHE already adds the Cache-Control: no-cache header

  if (!mLastEventID.IsEmpty()) {
    mHttpChannel->SetRequestHeader(NS_LITERAL_CSTRING("Last-Event-ID"),
      NS_ConvertUTF16toUTF8(mLastEventID), false);
  }

  nsCOMPtr<nsIURI> codebase;
  nsresult rv = GetBaseURI(getter_AddRefs(codebase));
  if (NS_SUCCEEDED(rv)) {
    rv = mHttpChannel->SetReferrer(codebase);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult
nsEventSource::InitChannelAndRequestEventSource()
{
  if (mReadyState == nsIEventSource::CLOSED) {
    return NS_ERROR_ABORT;
  }

  // eventsource validation

  if (!CheckCanRequestSrc()) {
    DispatchFailConnection();
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  nsLoadFlags loadFlags;
  loadFlags = nsIRequest::LOAD_BACKGROUND | nsIRequest::LOAD_BYPASS_CACHE;

  // get Content Security Policy from principal to pass into channel
  nsCOMPtr<nsIChannelPolicy> channelPolicy;
  nsCOMPtr<nsIContentSecurityPolicy> csp;
  nsresult rv = mPrincipal->GetCsp(getter_AddRefs(csp));
  NS_ENSURE_SUCCESS(rv, rv);
  if (csp) {
    channelPolicy = do_CreateInstance("@mozilla.org/nschannelpolicy;1");
    channelPolicy->SetContentSecurityPolicy(csp);
    channelPolicy->SetLoadType(nsIContentPolicy::TYPE_DATAREQUEST);
  }

  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewChannel(getter_AddRefs(channel), mSrc, nsnull, mLoadGroup,
                     nsnull, loadFlags, channelPolicy);
  NS_ENSURE_SUCCESS(rv, rv);

  mHttpChannel = do_QueryInterface(channel);
  NS_ENSURE_TRUE(mHttpChannel, NS_ERROR_NO_INTERFACE);

  rv = SetupHttpChannel();
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIInterfaceRequestor> notificationCallbacks;
  mHttpChannel->GetNotificationCallbacks(getter_AddRefs(notificationCallbacks));
  if (notificationCallbacks != this) {
    mNotificationCallbacks = notificationCallbacks;
    mHttpChannel->SetNotificationCallbacks(this);
  }

  nsCOMPtr<nsIStreamListener> listener =
    new nsCORSListenerProxy(this, mPrincipal, mHttpChannel,
                            mWithCredentials, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Start reading from the channel
  rv = mHttpChannel->AsyncOpen(listener, nsnull);
  if (NS_SUCCEEDED(rv)) {
    mWaitingForOnStopRequest = true;
  }
  return rv;
}

void
nsEventSource::AnnounceConnection()
{
  if (mReadyState == nsIEventSource::CLOSED) {
    return;
  }

  if (mReadyState != nsIEventSource::CONNECTING) {
    NS_WARNING("Unexpected mReadyState!!!");
    return;
  }

  // When a user agent is to announce the connection, the user agent must set
  // the readyState attribute to OPEN and queue a task to fire a simple event
  // named open at the EventSource object.

  mReadyState = nsIEventSource::OPEN;

  nsresult rv = CheckInnerWindowCorrectness();
  if (NS_FAILED(rv)) {
    return;
  }

  nsCOMPtr<nsIDOMEvent> event;
  rv = NS_NewDOMEvent(getter_AddRefs(event), nsnull, nsnull);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to create the open event!!!");
    return;
  }

  // it doesn't bubble, and it isn't cancelable
  rv = event->InitEvent(NS_LITERAL_STRING("open"), false, false);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to init the open event!!!");
    return;
  }

  event->SetTrusted(true);

  rv = DispatchDOMEvent(nsnull, event, nsnull, nsnull);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to dispatch the open event!!!");
    return;
  }
}

nsresult
nsEventSource::ResetConnection()
{
  if (mHttpChannel) {
    mHttpChannel->Cancel(NS_ERROR_ABORT);
  }

  if (mUnicodeDecoder) {
    mUnicodeDecoder->Reset();
  }
  mLastConvertionResult = NS_OK;

  mHttpChannel = nsnull;
  mNotificationCallbacks = nsnull;
  mChannelEventSink = nsnull;
  mStatus = PARSE_STATE_OFF;
  mRedirectCallback = nsnull;
  mNewRedirectChannel = nsnull;

  mReadyState = nsIEventSource::CONNECTING;

  return NS_OK;
}

void
nsEventSource::ReestablishConnection()
{
  if (mReadyState == nsIEventSource::CLOSED) {
    return;
  }

  if (mReadyState != nsIEventSource::OPEN) {
    NS_WARNING("Unexpected mReadyState!!!");
    return;
  }

  nsresult rv = ResetConnection();
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to reset the connection!!!");
    return;
  }

  rv = CheckInnerWindowCorrectness();
  if (NS_FAILED(rv)) {
    return;
  }

  nsCOMPtr<nsIDOMEvent> event;
  rv = NS_NewDOMEvent(getter_AddRefs(event), nsnull, nsnull);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to create the error event!!!");
    return;
  }

  // it doesn't bubble, and it isn't cancelable
  rv = event->InitEvent(NS_LITERAL_STRING("error"), false, false);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to init the error event!!!");
    return;
  }

  event->SetTrusted(true);

  rv = DispatchDOMEvent(nsnull, event, nsnull, nsnull);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to dispatch the error event!!!");
    return;
  }

  rv = SetReconnectionTimeout();
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to set the timeout for reestablishing the connection!!!");
    return;
  }
}

nsresult
nsEventSource::SetReconnectionTimeout()
{
  if (mReadyState == nsIEventSource::CLOSED) {
    return NS_ERROR_ABORT;
  }

  // the timer will be used whenever the requests are going finished.
  if (!mTimer) {
    mTimer = do_CreateInstance("@mozilla.org/timer;1");
    NS_ENSURE_STATE(mTimer);
  }

  nsresult rv = mTimer->InitWithFuncCallback(TimerCallback, this,
                                             mReconnectionTime,
                                             nsITimer::TYPE_ONE_SHOT);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
nsEventSource::PrintErrorOnConsole(const char *aBundleURI,
                                   const PRUnichar *aError,
                                   const PRUnichar **aFormatStrings,
                                   PRUint32 aFormatStringsLen)
{
  nsCOMPtr<nsIStringBundleService> bundleService =
    mozilla::services::GetStringBundleService();
  NS_ENSURE_STATE(bundleService);

  nsCOMPtr<nsIStringBundle> strBundle;
  nsresult rv =
    bundleService->CreateBundle(aBundleURI, getter_AddRefs(strBundle));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIConsoleService> console(
    do_GetService(NS_CONSOLESERVICE_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIScriptError> errObj(
    do_CreateInstance(NS_SCRIPTERROR_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  // Localize the error message
  nsXPIDLString message;
  if (aFormatStrings) {
    rv = strBundle->FormatStringFromName(aError, aFormatStrings,
                                         aFormatStringsLen,
                                         getter_Copies(message));
  } else {
    rv = strBundle->GetStringFromName(aError, getter_Copies(message));
  }
  NS_ENSURE_SUCCESS(rv, rv);

  rv = errObj->InitWithWindowID(message.get(),
                                mScriptFile.get(),
                                nsnull,
                                mScriptLine, 0,
                                nsIScriptError::errorFlag,
                                "Event Source", mInnerWindowID);
  NS_ENSURE_SUCCESS(rv, rv);

  // print the error message directly to the JS console
  rv = console->LogMessage(errObj);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
nsEventSource::ConsoleError()
{
  nsCAutoString targetSpec;
  nsresult rv = mSrc->GetSpec(targetSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ConvertUTF8toUTF16 specUTF16(targetSpec);
  const PRUnichar *formatStrings[] = { specUTF16.get() };

  if (mReadyState == nsIEventSource::CONNECTING) {
    rv = PrintErrorOnConsole("chrome://global/locale/appstrings.properties",
                             NS_LITERAL_STRING("connectionFailure").get(),
                             formatStrings, ArrayLength(formatStrings));
  } else {
    rv = PrintErrorOnConsole("chrome://global/locale/appstrings.properties",
                             NS_LITERAL_STRING("netInterrupt").get(),
                             formatStrings, ArrayLength(formatStrings));
  }
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
nsEventSource::DispatchFailConnection()
{
  nsCOMPtr<nsIRunnable> event =
    NS_NewRunnableMethod(this, &nsEventSource::FailConnection);
  NS_ENSURE_STATE(event);

  return NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
}

void
nsEventSource::FailConnection()
{
  if (mReadyState == nsIEventSource::CLOSED) {
    return;
  }

  nsresult rv = ConsoleError();
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to print to the console error");
  }

  // When a user agent is to fail the connection, the user agent must set the
  // readyState attribute to CLOSED and queue a task to fire a simple event
  // named error at the EventSource  object.

  Close(); // it sets mReadyState to CLOSED

  rv = CheckInnerWindowCorrectness();
  if (NS_FAILED(rv)) {
    return;
  }

  nsCOMPtr<nsIDOMEvent> event;
  rv = NS_NewDOMEvent(getter_AddRefs(event), nsnull, nsnull);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to create the error event!!!");
    return;
  }

  // it doesn't bubble, and it isn't cancelable
  rv = event->InitEvent(NS_LITERAL_STRING("error"), false, false);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to init the error event!!!");
    return;
  }

  event->SetTrusted(true);

  rv = DispatchDOMEvent(nsnull, event, nsnull, nsnull);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to dispatch the error event!!!");
    return;
  }
}

bool
nsEventSource::CheckCanRequestSrc(nsIURI* aSrc)
{
  if (mReadyState == nsIEventSource::CLOSED) {
    return false;
  }

  bool isValidURI = false;
  bool isValidContentLoadPolicy = false;
  bool isValidProtocol = false;

  nsCOMPtr<nsIURI> srcToTest = aSrc ? aSrc : mSrc.get();
  NS_ENSURE_TRUE(srcToTest, false);

  PRUint32 aCheckURIFlags =
    nsIScriptSecurityManager::DISALLOW_INHERIT_PRINCIPAL |
    nsIScriptSecurityManager::DISALLOW_SCRIPT;

  nsresult rv = nsContentUtils::GetSecurityManager()->
    CheckLoadURIWithPrincipal(mPrincipal,
                              srcToTest,
                              aCheckURIFlags);
  isValidURI = NS_SUCCEEDED(rv);

  // After the security manager, the content-policy check

  nsIScriptContext* sc = GetContextForEventHandlers(&rv);
  nsCOMPtr<nsIDocument> doc =
    nsContentUtils::GetDocumentFromScriptContext(sc);

  // mScriptContext should be initialized because of GetBaseURI() above.
  // Still need to consider the case that doc is nsnull however.
  rv = CheckInnerWindowCorrectness();
  NS_ENSURE_SUCCESS(rv, false);
  PRInt16 shouldLoad = nsIContentPolicy::ACCEPT;
  rv = NS_CheckContentLoadPolicy(nsIContentPolicy::TYPE_DATAREQUEST,
                                 srcToTest,
                                 mPrincipal,
                                 doc,
                                 NS_LITERAL_CSTRING(TEXT_EVENT_STREAM),
                                 nsnull,    // extra
                                 &shouldLoad,
                                 nsContentUtils::GetContentPolicy(),
                                 nsContentUtils::GetSecurityManager());
  isValidContentLoadPolicy = NS_SUCCEEDED(rv) && NS_CP_ACCEPTED(shouldLoad);

  nsCAutoString targetURIScheme;
  rv = srcToTest->GetScheme(targetURIScheme);
  if (NS_SUCCEEDED(rv)) {
    // We only have the http support for now
    isValidProtocol = targetURIScheme.EqualsLiteral("http") ||
                      targetURIScheme.EqualsLiteral("https");
  }

  return isValidURI && isValidContentLoadPolicy && isValidProtocol;
}

// static
void
nsEventSource::TimerCallback(nsITimer* aTimer, void* aClosure)
{
  nsRefPtr<nsEventSource> thisObject = static_cast<nsEventSource*>(aClosure);

  if (thisObject->mReadyState == nsIEventSource::CLOSED) {
    return;
  }

  NS_PRECONDITION(!thisObject->mHttpChannel,
                  "the channel hasn't been cancelled!!");

  if (!thisObject->mFrozen) {
    nsresult rv = thisObject->InitChannelAndRequestEventSource();
    if (NS_FAILED(rv)) {
      NS_WARNING("thisObject->InitChannelAndRequestEventSource() failed");
      return;
    }
  }
}

nsresult
nsEventSource::Thaw()
{
  if (mReadyState == nsIEventSource::CLOSED || !mFrozen) {
    return NS_OK;
  }

  NS_ASSERTION(!mHttpChannel, "the connection hasn't been closed!!!");

  mFrozen = false;
  nsresult rv;
  if (!mGoingToDispatchAllMessages && mMessagesToDispatch.GetSize() > 0) {
    nsCOMPtr<nsIRunnable> event =
      NS_NewRunnableMethod(this, &nsEventSource::DispatchAllMessageEvents);
    NS_ENSURE_STATE(event);

    mGoingToDispatchAllMessages = true;

    rv = NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = InitChannelAndRequestEventSource();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult
nsEventSource::Freeze()
{
  if (mReadyState == nsIEventSource::CLOSED || mFrozen) {
    return NS_OK;
  }

  NS_ASSERTION(!mHttpChannel, "the connection hasn't been closed!!!");
  mFrozen = true;
  return NS_OK;
}

nsresult
nsEventSource::DispatchCurrentMessageEvent()
{
  nsAutoPtr<Message> message(new Message());
  *message = mCurrentMessage;

  ClearFields();

  if (message->mData.IsEmpty()) {
    return NS_OK;
  }

  // removes the trailing LF from mData
  NS_ASSERTION(message->mData.CharAt(message->mData.Length() - 1) == LF_CHAR,
               "Invalid trailing character! LF was expected instead.");
  message->mData.SetLength(message->mData.Length() - 1);

  if (message->mEventName.IsEmpty()) {
    message->mEventName.AssignLiteral("message");
  }

  if (message->mLastEventID.IsEmpty() && !mLastEventID.IsEmpty()) {
    message->mLastEventID.Assign(mLastEventID);
  }

  PRInt32 sizeBefore = mMessagesToDispatch.GetSize();
  mMessagesToDispatch.Push(message.forget());
  NS_ENSURE_TRUE(mMessagesToDispatch.GetSize() == sizeBefore + 1,
                 NS_ERROR_OUT_OF_MEMORY);


  if (!mGoingToDispatchAllMessages) {
    nsCOMPtr<nsIRunnable> event =
      NS_NewRunnableMethod(this, &nsEventSource::DispatchAllMessageEvents);
    NS_ENSURE_STATE(event);

    mGoingToDispatchAllMessages = true;

    return NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
  }

  return NS_OK;
}

void
nsEventSource::DispatchAllMessageEvents()
{
  if (mReadyState == nsIEventSource::CLOSED || mFrozen) {
    return;
  }

  mGoingToDispatchAllMessages = false;

  nsresult rv = CheckInnerWindowCorrectness();
  if (NS_FAILED(rv)) {
    return;
  }

  // Let's play get the JSContext
  nsCOMPtr<nsIScriptGlobalObject> sgo = do_QueryInterface(GetOwner());
  NS_ENSURE_TRUE(sgo,);

  nsIScriptContext* scriptContext = sgo->GetContext();
  NS_ENSURE_TRUE(scriptContext,);

  JSContext* cx = scriptContext->GetNativeContext();
  NS_ENSURE_TRUE(cx,);

  while (mMessagesToDispatch.GetSize() > 0) {
    nsAutoPtr<Message>
      message(static_cast<Message*>(mMessagesToDispatch.PopFront()));

    // Now we can turn our string into a jsval
    jsval jsData;
    {
      JSString* jsString;
      JSAutoRequest ar(cx);
      jsString = JS_NewUCStringCopyN(cx,
                                     message->mData.get(),
                                     message->mData.Length());
      NS_ENSURE_TRUE(jsString,);

      jsData = STRING_TO_JSVAL(jsString);
    }

    // create an event that uses the MessageEvent interface,
    // which does not bubble, is not cancelable, and has no default action

    nsCOMPtr<nsIDOMEvent> event;
    rv = NS_NewDOMMessageEvent(getter_AddRefs(event), nsnull, nsnull);
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to create the message event!!!");
      return;
    }

    nsCOMPtr<nsIDOMMessageEvent> messageEvent = do_QueryInterface(event);
    rv = messageEvent->InitMessageEvent(message->mEventName,
                                        false, false,
                                        jsData,
                                        mOrigin,
                                        message->mLastEventID, nsnull);
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to init the message event!!!");
      return;
    }

    messageEvent->SetTrusted(true);

    rv = DispatchDOMEvent(nsnull, event, nsnull, nsnull);
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to dispatch the message event!!!");
      return;
    }
  }
}

nsresult
nsEventSource::ClearFields()
{
  // mLastEventID and mReconnectionTime must be cached

  mCurrentMessage.mEventName.Truncate();
  mCurrentMessage.mLastEventID.Truncate();
  mCurrentMessage.mData.Truncate();

  mLastFieldName.Truncate();
  mLastFieldValue.Truncate();

  return NS_OK;
}

nsresult
nsEventSource::SetFieldAndClear()
{
  if (mLastFieldName.IsEmpty()) {
    mLastFieldValue.Truncate();
    return NS_OK;
  }

  PRUnichar first_char;
  first_char = mLastFieldName.CharAt(0);

  switch (first_char)  // with no case folding performed
  {
    case PRUnichar('d'):
      if (mLastFieldName.EqualsLiteral("data")) {
        // If the field name is "data" append the field value to the data
        // buffer, then append a single U+000A LINE FEED (LF) character
        // to the data buffer.
        mCurrentMessage.mData.Append(mLastFieldValue);
        mCurrentMessage.mData.Append(LF_CHAR);
      }
      break;

    case PRUnichar('e'):
      if (mLastFieldName.EqualsLiteral("event")) {
        mCurrentMessage.mEventName.Assign(mLastFieldValue);
      }
      break;

    case PRUnichar('i'):
      if (mLastFieldName.EqualsLiteral("id")) {
        mCurrentMessage.mLastEventID.Assign(mLastFieldValue);
        mLastEventID.Assign(mLastFieldValue);
      }
      break;

    case PRUnichar('r'):
      if (mLastFieldName.EqualsLiteral("retry")) {
        PRUint32 newValue=0;
        PRUint32 i = 0;  // we must ensure that there are only digits
        bool assign = true;
        for (i = 0; i < mLastFieldValue.Length(); ++i) {
          if (mLastFieldValue.CharAt(i) < (PRUnichar)'0' ||
              mLastFieldValue.CharAt(i) > (PRUnichar)'9') {
            assign = false;
            break;
          }
          newValue = newValue*10 +
                     (((PRUint32)mLastFieldValue.CharAt(i))-
                       ((PRUint32)((PRUnichar)'0')));
        }

        if (assign) {
          if (newValue < MIN_RECONNECTION_TIME_VALUE) {
            mReconnectionTime = MIN_RECONNECTION_TIME_VALUE;
          } else if (newValue > MAX_RECONNECTION_TIME_VALUE) {
            mReconnectionTime = MAX_RECONNECTION_TIME_VALUE;
          } else {
            mReconnectionTime = newValue;
          }
        }
        break;
      }
      break;
  }

  mLastFieldName.Truncate();
  mLastFieldValue.Truncate();

  return NS_OK;
}

nsresult
nsEventSource::CheckHealthOfRequestCallback(nsIRequest *aRequestCallback)
{
  // check if we have been closed or if the request has been canceled
  // or if we have been frozen
  if (mReadyState == nsIEventSource::CLOSED || !mHttpChannel ||
      mFrozen || mErrorLoadOnRedirect) {
    return NS_ERROR_ABORT;
  }

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aRequestCallback);
  NS_ENSURE_STATE(httpChannel);

  if (httpChannel != mHttpChannel) {
    NS_WARNING("wrong channel from request callback");
    return NS_ERROR_ABORT;
  }

  return NS_OK;
}

nsresult
nsEventSource::ParseCharacter(PRUnichar aChr)
{
  nsresult rv;

  if (mReadyState == nsIEventSource::CLOSED) {
    return NS_ERROR_ABORT;
  }

  switch (mStatus)
  {
    case PARSE_STATE_OFF:
      NS_ERROR("Invalid state");
      return NS_ERROR_FAILURE;
      break;

    case PARSE_STATE_BEGIN_OF_STREAM:
      if (aChr == BOM_CHAR) {
        mStatus = PARSE_STATE_BOM_WAS_READ;  // ignore it
      } else if (aChr == CR_CHAR) {
        mStatus = PARSE_STATE_CR_CHAR;
      } else if (aChr == LF_CHAR) {
        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      } else if (aChr == COLON_CHAR) {
        mStatus = PARSE_STATE_COMMENT;
      } else {
        mLastFieldName += aChr;
        mStatus = PARSE_STATE_FIELD_NAME;
      }

      break;

    case PARSE_STATE_BOM_WAS_READ:
      if (aChr == CR_CHAR) {
        mStatus = PARSE_STATE_CR_CHAR;
      } else if (aChr == LF_CHAR) {
        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      } else if (aChr == COLON_CHAR) {
        mStatus = PARSE_STATE_COMMENT;
      } else {
        mLastFieldName += aChr;
        mStatus = PARSE_STATE_FIELD_NAME;
      }
      break;

    case PARSE_STATE_CR_CHAR:
      if (aChr == CR_CHAR) {
        rv = DispatchCurrentMessageEvent();  // there is an empty line (CRCR)
        NS_ENSURE_SUCCESS(rv, rv);
      } else if (aChr == LF_CHAR) {
        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      } else if (aChr == COLON_CHAR) {
        mStatus = PARSE_STATE_COMMENT;
      } else {
        mLastFieldName += aChr;
        mStatus = PARSE_STATE_FIELD_NAME;
      }

      break;

    case PARSE_STATE_COMMENT:
      if (aChr == CR_CHAR) {
        mStatus = PARSE_STATE_CR_CHAR;
      } else if (aChr == LF_CHAR) {
        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      }

      break;

    case PARSE_STATE_FIELD_NAME:
      if (aChr == CR_CHAR) {
        rv = SetFieldAndClear();
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_CR_CHAR;
      } else if (aChr == LF_CHAR) {
        rv = SetFieldAndClear();
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      } else if (aChr == COLON_CHAR) {
        mStatus = PARSE_STATE_FIRST_CHAR_OF_FIELD_VALUE;
      } else {
        mLastFieldName += aChr;
      }

      break;

    case PARSE_STATE_FIRST_CHAR_OF_FIELD_VALUE:
      if (aChr == CR_CHAR) {
        rv = SetFieldAndClear();
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_CR_CHAR;
      } else if (aChr == LF_CHAR) {
        rv = SetFieldAndClear();
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      } else if (aChr == SPACE_CHAR) {
        mStatus = PARSE_STATE_FIELD_VALUE;
      } else {
        mLastFieldValue += aChr;
        mStatus = PARSE_STATE_FIELD_VALUE;
      }

      break;

    case PARSE_STATE_FIELD_VALUE:
      if (aChr == CR_CHAR) {
        rv = SetFieldAndClear();
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_CR_CHAR;
      } else if (aChr == LF_CHAR) {
        rv = SetFieldAndClear();
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      } else {
        mLastFieldValue += aChr;
      }

      break;

    case PARSE_STATE_BEGIN_OF_LINE:
      if (aChr == CR_CHAR) {
        rv = DispatchCurrentMessageEvent();  // there is an empty line
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_CR_CHAR;
      } else if (aChr == LF_CHAR) {
        rv = DispatchCurrentMessageEvent();  // there is an empty line
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      } else if (aChr == COLON_CHAR) {
        mStatus = PARSE_STATE_COMMENT;
      } else {
        mLastFieldName += aChr;
        mStatus = PARSE_STATE_FIELD_NAME;
      }

      break;
  }

  return NS_OK;
}
