/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_WebSocketChannelChild_h
#define mozilla_net_WebSocketChannelChild_h

#include "mozilla/net/PWebSocketChild.h"
#include "mozilla/net/ChannelEventQueue.h"
#include "mozilla/net/BaseWebSocketChannel.h"
#include "nsCOMPtr.h"
#include "nsString.h"

namespace mozilla {
namespace net {

class WebSocketChannelChild : public BaseWebSocketChannel,
                              public PWebSocketChild
{
 public:
  WebSocketChannelChild(bool aSecure);
  ~WebSocketChannelChild();

  NS_DECL_ISUPPORTS

  // nsIWebSocketChannel methods BaseWebSocketChannel didn't implement for us
  //
  NS_IMETHOD AsyncOpen(nsIURI *aURI, const nsACString &aOrigin,
                       nsIWebSocketListener *aListener, nsISupports *aContext);
  NS_IMETHOD Close(PRUint16 code, const nsACString & reason);
  NS_IMETHOD SendMsg(const nsACString &aMsg);
  NS_IMETHOD SendBinaryMsg(const nsACString &aMsg);
  NS_IMETHOD SendBinaryStream(nsIInputStream *aStream, PRUint32 aLength);
  NS_IMETHOD GetSecurityInfo(nsISupports **aSecurityInfo);

  void AddIPDLReference();
  void ReleaseIPDLReference();

 private:
  bool RecvOnStart(const nsCString& aProtocol, const nsCString& aExtensions);
  bool RecvOnStop(const nsresult& aStatusCode);
  bool RecvOnMessageAvailable(const nsCString& aMsg);
  bool RecvOnBinaryMessageAvailable(const nsCString& aMsg);
  bool RecvOnAcknowledge(const PRUint32& aSize);
  bool RecvOnServerClose(const PRUint16& aCode, const nsCString &aReason);
  bool RecvAsyncOpenFailed();

  void OnStart(const nsCString& aProtocol, const nsCString& aExtensions);
  void OnStop(const nsresult& aStatusCode);
  void OnMessageAvailable(const nsCString& aMsg);
  void OnBinaryMessageAvailable(const nsCString& aMsg);
  void OnAcknowledge(const PRUint32& aSize);
  void OnServerClose(const PRUint16& aCode, const nsCString& aReason);
  void AsyncOpenFailed();  

  ChannelEventQueue mEventQ;
  bool mIPCOpen;

  friend class StartEvent;
  friend class StopEvent;
  friend class MessageEvent;
  friend class AcknowledgeEvent;
  friend class ServerCloseEvent;
  friend class AsyncOpenFailedEvent;
};

} // namespace net
} // namespace mozilla

#endif // mozilla_net_WebSocketChannelChild_h
