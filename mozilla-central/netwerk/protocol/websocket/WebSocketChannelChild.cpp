/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebSocketLog.h"
#include "mozilla/dom/TabChild.h"
#include "mozilla/net/NeckoChild.h"
#include "WebSocketChannelChild.h"
#include "nsITabChild.h"
#include "nsILoadContext.h"
#include "nsNetUtil.h"
#include "mozilla/ipc/InputStreamUtils.h"
#include "mozilla/ipc/URIUtils.h"

using namespace mozilla::ipc;

namespace mozilla {
namespace net {

NS_IMPL_ADDREF(WebSocketChannelChild)

NS_IMETHODIMP_(nsrefcnt) WebSocketChannelChild::Release()
{
  NS_PRECONDITION(0 != mRefCnt, "dup release");
  NS_ASSERT_OWNINGTHREAD(WebSocketChannelChild);
  --mRefCnt;
  NS_LOG_RELEASE(this, mRefCnt, "WebSocketChannelChild");

  if (mRefCnt == 1 && mIPCOpen) {
    SendDeleteSelf();
    return mRefCnt;
  }

  if (mRefCnt == 0) {
    mRefCnt = 1; /* stabilize */
    delete this;
    return 0;
  }
  return mRefCnt;
}

NS_INTERFACE_MAP_BEGIN(WebSocketChannelChild)
  NS_INTERFACE_MAP_ENTRY(nsIWebSocketChannel)
  NS_INTERFACE_MAP_ENTRY(nsIProtocolHandler)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIWebSocketChannel)
NS_INTERFACE_MAP_END

WebSocketChannelChild::WebSocketChannelChild(bool aSecure)
: mEventQ(static_cast<nsIWebSocketChannel*>(this))
, mIPCOpen(false)
{
  LOG(("WebSocketChannelChild::WebSocketChannelChild() %p\n", this));
  BaseWebSocketChannel::mEncrypted = aSecure;
}

WebSocketChannelChild::~WebSocketChannelChild()
{
  LOG(("WebSocketChannelChild::~WebSocketChannelChild() %p\n", this));
}

void
WebSocketChannelChild::AddIPDLReference()
{
  NS_ABORT_IF_FALSE(!mIPCOpen, "Attempt to retain more than one IPDL reference");
  mIPCOpen = true;
  AddRef();
}

void
WebSocketChannelChild::ReleaseIPDLReference()
{
  NS_ABORT_IF_FALSE(mIPCOpen, "Attempt to release nonexistent IPDL reference");
  mIPCOpen = false;
  Release();
}

class StartEvent : public ChannelEvent
{
 public:
  StartEvent(WebSocketChannelChild* aChild,
             const nsCString& aProtocol,
             const nsCString& aExtensions)
  : mChild(aChild)
  , mProtocol(aProtocol)
  , mExtensions(aExtensions)
  {}

  void Run()
  {
    mChild->OnStart(mProtocol, mExtensions);
  }
 private:
  WebSocketChannelChild* mChild;
  nsCString mProtocol;
  nsCString mExtensions;
};

bool
WebSocketChannelChild::RecvOnStart(const nsCString& aProtocol,
                                   const nsCString& aExtensions)
{
  if (mEventQ.ShouldEnqueue()) {
    mEventQ.Enqueue(new StartEvent(this, aProtocol, aExtensions));
  } else {
    OnStart(aProtocol, aExtensions);
  }
  return true;
}

void
WebSocketChannelChild::OnStart(const nsCString& aProtocol,
                               const nsCString& aExtensions)
{
  LOG(("WebSocketChannelChild::RecvOnStart() %p\n", this));
  SetProtocol(aProtocol);
  mNegotiatedExtensions = aExtensions;

  if (mListener) {
    AutoEventEnqueuer ensureSerialDispatch(mEventQ);;
    mListener->OnStart(mContext);
  }
}

class StopEvent : public ChannelEvent
{
 public:
  StopEvent(WebSocketChannelChild* aChild,
            const nsresult& aStatusCode)
  : mChild(aChild)
  , mStatusCode(aStatusCode)
  {}

  void Run()
  {
    mChild->OnStop(mStatusCode);
  }
 private:
  WebSocketChannelChild* mChild;
  nsresult mStatusCode;
};

bool
WebSocketChannelChild::RecvOnStop(const nsresult& aStatusCode)
{
  if (mEventQ.ShouldEnqueue()) {
    mEventQ.Enqueue(new StopEvent(this, aStatusCode));
  } else {
    OnStop(aStatusCode);
  }
  return true;
}

void
WebSocketChannelChild::OnStop(const nsresult& aStatusCode)
{
  LOG(("WebSocketChannelChild::RecvOnStop() %p\n", this));
  if (mListener) {
    AutoEventEnqueuer ensureSerialDispatch(mEventQ);;
    mListener->OnStop(mContext, aStatusCode);
  }
}

class MessageEvent : public ChannelEvent
{
 public:
  MessageEvent(WebSocketChannelChild* aChild,
               const nsCString& aMessage,
               bool aBinary)
  : mChild(aChild)
  , mMessage(aMessage)
  , mBinary(aBinary)
  {}

  void Run()
  {
    if (!mBinary) {
      mChild->OnMessageAvailable(mMessage);
    } else {
      mChild->OnBinaryMessageAvailable(mMessage);
    }
  }
 private:
  WebSocketChannelChild* mChild;
  nsCString mMessage;
  bool mBinary;
};

bool
WebSocketChannelChild::RecvOnMessageAvailable(const nsCString& aMsg)
{
  if (mEventQ.ShouldEnqueue()) {
    mEventQ.Enqueue(new MessageEvent(this, aMsg, false));
  } else {
    OnMessageAvailable(aMsg);
  }
  return true;
}

void
WebSocketChannelChild::OnMessageAvailable(const nsCString& aMsg)
{
  LOG(("WebSocketChannelChild::RecvOnMessageAvailable() %p\n", this));
  if (mListener) {
    AutoEventEnqueuer ensureSerialDispatch(mEventQ);;
    mListener->OnMessageAvailable(mContext, aMsg);
  }
}

bool
WebSocketChannelChild::RecvOnBinaryMessageAvailable(const nsCString& aMsg)
{
  if (mEventQ.ShouldEnqueue()) {
    mEventQ.Enqueue(new MessageEvent(this, aMsg, true));
  } else {
    OnBinaryMessageAvailable(aMsg);
  }
  return true;
}

void
WebSocketChannelChild::OnBinaryMessageAvailable(const nsCString& aMsg)
{
  LOG(("WebSocketChannelChild::RecvOnBinaryMessageAvailable() %p\n", this));
  if (mListener) {
    AutoEventEnqueuer ensureSerialDispatch(mEventQ);;
    mListener->OnBinaryMessageAvailable(mContext, aMsg);
  }
}

class AcknowledgeEvent : public ChannelEvent
{
 public:
  AcknowledgeEvent(WebSocketChannelChild* aChild,
                   const uint32_t& aSize)
  : mChild(aChild)
  , mSize(aSize)
  {}

  void Run()
  {
    mChild->OnAcknowledge(mSize);
  }
 private:
  WebSocketChannelChild* mChild;
  uint32_t mSize;
};

bool
WebSocketChannelChild::RecvOnAcknowledge(const uint32_t& aSize)
{
  if (mEventQ.ShouldEnqueue()) {
    mEventQ.Enqueue(new AcknowledgeEvent(this, aSize));
  } else {
    OnAcknowledge(aSize);
  }
  return true;
}

void
WebSocketChannelChild::OnAcknowledge(const uint32_t& aSize)
{
  LOG(("WebSocketChannelChild::RecvOnAcknowledge() %p\n", this));
  if (mListener) {
    AutoEventEnqueuer ensureSerialDispatch(mEventQ);;
    mListener->OnAcknowledge(mContext, aSize);
  }
}

class ServerCloseEvent : public ChannelEvent
{
 public:
  ServerCloseEvent(WebSocketChannelChild* aChild,
                   const uint16_t aCode,
                   const nsCString &aReason)
  : mChild(aChild)
  , mCode(aCode)
  , mReason(aReason)
  {}

  void Run()
  {
    mChild->OnServerClose(mCode, mReason);
  }
 private:
  WebSocketChannelChild* mChild;
  uint16_t               mCode;
  nsCString              mReason;
};

bool
WebSocketChannelChild::RecvOnServerClose(const uint16_t& aCode,
                                         const nsCString& aReason)
{
  if (mEventQ.ShouldEnqueue()) {
    mEventQ.Enqueue(new ServerCloseEvent(this, aCode, aReason));
  } else {
    OnServerClose(aCode, aReason);
  }
  return true;
}

void
WebSocketChannelChild::OnServerClose(const uint16_t& aCode,
                                     const nsCString& aReason)
{
  LOG(("WebSocketChannelChild::RecvOnServerClose() %p\n", this));
  if (mListener) {
    AutoEventEnqueuer ensureSerialDispatch(mEventQ);;
    mListener->OnServerClose(mContext, aCode, aReason);
  }
}

NS_IMETHODIMP
WebSocketChannelChild::AsyncOpen(nsIURI *aURI,
                                 const nsACString &aOrigin,
                                 nsIWebSocketListener *aListener,
                                 nsISupports *aContext)
{
  LOG(("WebSocketChannelChild::AsyncOpen() %p\n", this));

  NS_ABORT_IF_FALSE(aURI && aListener && !mListener, 
                    "Invalid state for WebSocketChannelChild::AsyncOpen");

  mozilla::dom::TabChild* tabChild = nullptr;
  nsCOMPtr<nsITabChild> iTabChild;
  NS_QueryNotificationCallbacks(mCallbacks, mLoadGroup,
                                NS_GET_IID(nsITabChild),
                                getter_AddRefs(iTabChild));
  if (iTabChild) {
    tabChild = static_cast<mozilla::dom::TabChild*>(iTabChild.get());
  }

  URIParams uri;
  SerializeURI(aURI, uri);

  // Corresponding release in DeallocPWebSocket
  AddIPDLReference();

  gNeckoChild->SendPWebSocketConstructor(this, tabChild);
  if (!SendAsyncOpen(uri, nsCString(aOrigin), mProtocol, mEncrypted,
                     IPC::SerializedLoadContext(this)))
    return NS_ERROR_UNEXPECTED;

  mOriginalURI = aURI;
  mURI = mOriginalURI;
  mListener = aListener;
  mContext = aContext;
  mOrigin = aOrigin;

  return NS_OK;
}

NS_IMETHODIMP
WebSocketChannelChild::Close(uint16_t code, const nsACString & reason)
{
  LOG(("WebSocketChannelChild::Close() %p\n", this));

  if (!mIPCOpen || !SendClose(code, nsCString(reason)))
    return NS_ERROR_UNEXPECTED;
  return NS_OK;
}

NS_IMETHODIMP
WebSocketChannelChild::SendMsg(const nsACString &aMsg)
{
  LOG(("WebSocketChannelChild::SendMsg() %p\n", this));

  if (!mIPCOpen || !SendSendMsg(nsCString(aMsg)))
    return NS_ERROR_UNEXPECTED;
  return NS_OK;
}

NS_IMETHODIMP
WebSocketChannelChild::SendBinaryMsg(const nsACString &aMsg)
{
  LOG(("WebSocketChannelChild::SendBinaryMsg() %p\n", this));

  if (!mIPCOpen || !SendSendBinaryMsg(nsCString(aMsg)))
    return NS_ERROR_UNEXPECTED;
  return NS_OK;
}

NS_IMETHODIMP
WebSocketChannelChild::SendBinaryStream(nsIInputStream *aStream,
                                        uint32_t aLength)
{
  LOG(("WebSocketChannelChild::SendBinaryStream() %p\n", this));

  OptionalInputStreamParams stream;
  SerializeInputStream(aStream, stream);

  if (!mIPCOpen || !SendSendBinaryStream(stream, aLength))
    return NS_ERROR_UNEXPECTED;
  return NS_OK;
}

NS_IMETHODIMP
WebSocketChannelChild::GetSecurityInfo(nsISupports **aSecurityInfo)
{
  LOG(("WebSocketChannelChild::GetSecurityInfo() %p\n", this));
  return NS_ERROR_NOT_AVAILABLE;
}

} // namespace net
} // namespace mozilla
