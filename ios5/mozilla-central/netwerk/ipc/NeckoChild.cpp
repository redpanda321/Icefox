/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHttp.h"
#include "mozilla/net/NeckoChild.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/net/HttpChannelChild.h"
#include "mozilla/net/CookieServiceChild.h"
#include "mozilla/net/WyciwygChannelChild.h"
#include "mozilla/net/FTPChannelChild.h"
#include "mozilla/net/WebSocketChannelChild.h"

namespace mozilla {
namespace net {

PNeckoChild *gNeckoChild = nsnull;

// C++ file contents
NeckoChild::NeckoChild()
{
}

NeckoChild::~NeckoChild()
{
}

void NeckoChild::InitNeckoChild()
{
  NS_ABORT_IF_FALSE(IsNeckoChild(), "InitNeckoChild called by non-child!");

  if (!gNeckoChild) {
    mozilla::dom::ContentChild * cpc = 
      mozilla::dom::ContentChild::GetSingleton();
    NS_ASSERTION(cpc, "Content Protocol is NULL!");
    gNeckoChild = cpc->SendPNeckoConstructor(); 
    NS_ASSERTION(gNeckoChild, "PNecko Protocol init failed!");
  }
}

// Note: not actually called; has some lifespan as child process, so
// automatically destroyed at exit.  
void NeckoChild::DestroyNeckoChild()
{
  NS_ABORT_IF_FALSE(IsNeckoChild(), "DestroyNeckoChild called by non-child!");
  static bool alreadyDestroyed = false;
  NS_ABORT_IF_FALSE(!alreadyDestroyed, "DestroyNeckoChild already called!");

  if (!alreadyDestroyed) {
    Send__delete__(gNeckoChild); 
    gNeckoChild = nsnull;
    alreadyDestroyed = true;
  }
}

PHttpChannelChild* 
NeckoChild::AllocPHttpChannel(PBrowserChild* browser)
{
  // This constructor is only used when PHttpChannel is constructed by
  // the parent process, e.g. during a redirect.  (Normally HttpChannelChild is
  // created by nsHttpHandler::NewProxiedChannel(), and then creates the
  // PHttpChannel in HttpChannelChild::AsyncOpen().)

  // No need to store PBrowser. It is only needed by the parent.
  HttpChannelChild* httpChannel = new HttpChannelChild();
  httpChannel->AddIPDLReference();
  return httpChannel;
}

bool 
NeckoChild::DeallocPHttpChannel(PHttpChannelChild* channel)
{
  NS_ABORT_IF_FALSE(IsNeckoChild(), "DeallocPHttpChannel called by non-child!");

  HttpChannelChild* child = static_cast<HttpChannelChild*>(channel);
  child->ReleaseIPDLReference();
  return true;
}

PFTPChannelChild*
NeckoChild::AllocPFTPChannel()
{
  // We don't allocate here: see FTPChannelChild::AsyncOpen()
  NS_RUNTIMEABORT("AllocPFTPChannel should not be called");
  return nsnull;
}

bool
NeckoChild::DeallocPFTPChannel(PFTPChannelChild* channel)
{
  NS_ABORT_IF_FALSE(IsNeckoChild(), "DeallocPFTPChannel called by non-child!");

  FTPChannelChild* child = static_cast<FTPChannelChild*>(channel);
  child->ReleaseIPDLReference();
  return true;
}

PCookieServiceChild* 
NeckoChild::AllocPCookieService()
{
  // We don't allocate here: see nsCookieService::GetSingleton()
  NS_NOTREACHED("AllocPCookieService should not be called");
  return nsnull;
}

bool 
NeckoChild::DeallocPCookieService(PCookieServiceChild* cs)
{
  NS_ASSERTION(IsNeckoChild(), "DeallocPCookieService called by non-child!");

  CookieServiceChild *p = static_cast<CookieServiceChild*>(cs);
  p->Release();
  return true;
}

PWyciwygChannelChild*
NeckoChild::AllocPWyciwygChannel()
{
  WyciwygChannelChild *p = new WyciwygChannelChild();
  p->AddIPDLReference();
  return p;
}

bool
NeckoChild::DeallocPWyciwygChannel(PWyciwygChannelChild* channel)
{
  NS_ABORT_IF_FALSE(IsNeckoChild(), "DeallocPWyciwygChannel called by non-child!");

  WyciwygChannelChild *p = static_cast<WyciwygChannelChild*>(channel);
  p->ReleaseIPDLReference();
  return true;
}

PWebSocketChild*
NeckoChild::AllocPWebSocket(PBrowserChild* browser)
{
  NS_NOTREACHED("AllocPWebSocket should not be called");
  return nsnull;
}

bool
NeckoChild::DeallocPWebSocket(PWebSocketChild* child)
{
  WebSocketChannelChild* p = static_cast<WebSocketChannelChild*>(child);
  p->ReleaseIPDLReference();
  return true;
}

}} // mozilla::net

