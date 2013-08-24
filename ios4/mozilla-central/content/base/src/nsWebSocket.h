/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Wellington Fernando de Macedo.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *    Wellington Fernando de Macedo <wfernandom2004@gmail.com> (original author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifndef nsWebSocket_h__
#define nsWebSocket_h__

#include "nsISupportsUtils.h"
#include "nsIWebSocket.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsIJSNativeInitializer.h"
#include "nsIPrincipal.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIDOMEventListener.h"
#include "nsDOMEventTargetWrapperCache.h"
#include "nsAutoPtr.h"
#include "nsIProxiedProtocolHandler.h"

#define DEFAULT_WS_SCHEME_PORT  80
#define DEFAULT_WSS_SCHEME_PORT 443

#define NS_WEBSOCKET_CID                            \
 { /* 7ca25214-98dc-40a6-bc1f-41ddbe41f46c */       \
  0x7ca25214, 0x98dc, 0x40a6,                       \
 {0xbc, 0x1f, 0x41, 0xdd, 0xbe, 0x41, 0xf4, 0x6c} }

#define NS_WEBSOCKET_CONTRACTID "@mozilla.org/websocket;1"

class nsWSNetAddressComparator;
class nsWebSocketEstablishedConnection;
class nsWSCloseEvent;

class nsWebSocket: public nsDOMEventTargetWrapperCache,
                   public nsIWebSocket,
                   public nsIJSNativeInitializer
{
friend class nsWSNetAddressComparator;
friend class nsWebSocketEstablishedConnection;
friend class nsWSCloseEvent;

public:
  nsWebSocket();
  virtual ~nsWebSocket();
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsWebSocket,
                                           nsDOMEventTargetWrapperCache)
  NS_DECL_NSIWEBSOCKET

  // nsIJSNativeInitializer
  NS_IMETHOD Initialize(nsISupports* aOwner, JSContext* aContext,
                        JSObject* aObject, PRUint32 aArgc, jsval* aArgv);

  // nsIDOMEventTarget
  NS_IMETHOD AddEventListener(const nsAString& aType,
                              nsIDOMEventListener* aListener,
                              PRBool aUseCapture);
  NS_IMETHOD RemoveEventListener(const nsAString& aType,
                                 nsIDOMEventListener* aListener,
                                 PRBool aUseCapture);

  // nsIDOMNSEventTarget
  NS_IMETHOD AddEventListener(const nsAString& aType,
                              nsIDOMEventListener *aListener,
                              PRBool aUseCapture,
                              PRBool aWantsUntrusted,
                              PRUint8 optional_argc);

  static void ReleaseGlobals();

protected:
  nsresult ParseURL(const nsString& aURL);
  nsresult SetProtocol(const nsString& aProtocol);
  nsresult EstablishConnection();

  nsresult CreateAndDispatchSimpleEvent(const nsString& aName);
  nsresult CreateAndDispatchMessageEvent(nsCString *aData);
  nsresult CreateAndDispatchCloseEvent(PRBool aWasClean);

  // called from mConnection accordingly to the situation
  void SetReadyState(PRUint16 aNewReadyState);

  // if there are "strong event listeners" (see comment in nsWebSocket.cpp) or
  // outgoing not sent messages then this method keeps the object alive
  // when js doesn't have strong references to it.
  void UpdateMustKeepAlive();
  // ATTENTION, when calling this method the object can be released
  // (and possibly collected).
  void DontKeepAliveAnyMore();

  nsRefPtr<nsDOMEventListenerWrapper> mOnOpenListener;
  nsRefPtr<nsDOMEventListenerWrapper> mOnErrorListener;
  nsRefPtr<nsDOMEventListenerWrapper> mOnMessageListener;
  nsRefPtr<nsDOMEventListenerWrapper> mOnCloseListener;

  // related to the WebSocket constructor steps
  nsString mOriginalURL;
  PRPackedBool mSecure; // if true it is using SSL and the wss scheme,
                        // otherwise it is using the ws scheme with no SSL

  PRPackedBool mKeepingAlive;
  PRPackedBool mCheckMustKeepAlive;
  PRPackedBool mTriggeredCloseEvent;

  nsCString mAsciiHost;  // hostname
  PRUint32  mPort;
  nsCString mResource; // [filepath[?query]]
  nsCString mOrigin;
  nsCOMPtr<nsIURI> mURI;
  nsCString mProtocol;

  PRUint16 mReadyState;

  nsCOMPtr<nsIPrincipal> mPrincipal;

  nsRefPtr<nsWebSocketEstablishedConnection> mConnection;
  PRUint32 mOutgoingBufferedAmount; // actually, we get this value from
                                    // mConnection when we are connected,
                                    // but we need this one after disconnecting.

private:
  nsWebSocket(const nsWebSocket& x);   // prevent bad usage
  nsWebSocket& operator=(const nsWebSocket& x);
};

#define NS_WSPROTOCOLHANDLER_CONTRACTID \
    NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX "ws"

#define NS_WSSPROTOCOLHANDLER_CONTRACTID \
    NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX "wss"

#define NS_WSPROTOCOLHANDLER_CID                     \
{ /* a4e6aa3b-b6db-4809-aa11-e292e074cbc4 */         \
    0xa4e6aa3b,                                      \
    0xb6db,                                          \
    0x4809,                                          \
    {0xaa, 0x11, 0xe2, 0x92, 0xe0, 0x74, 0xcb, 0xc4} \
}

#define NS_WSSPROTOCOLHANDLER_CID                    \
{ /* c6531804-b5c8-4a53-80bf-e339b82d3161 */         \
    0xc6531804,                                      \
    0xb5c8,                                          \
    0x4a53,                                          \
    {0x80, 0xbf, 0xe3, 0x39, 0xb8, 0x2d, 0x31, 0x61} \
}

class nsWSProtocolHandler: public nsIProxiedProtocolHandler
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPROTOCOLHANDLER
  NS_DECL_NSIPROXIEDPROTOCOLHANDLER

  nsWSProtocolHandler() {};
};

class nsWSSProtocolHandler: public nsWSProtocolHandler
{
public:
  NS_IMETHOD GetScheme(nsACString & aScheme);
  NS_IMETHOD GetDefaultPort(PRInt32 *aDefaultPort);
  nsWSSProtocolHandler() {};
};

#endif
