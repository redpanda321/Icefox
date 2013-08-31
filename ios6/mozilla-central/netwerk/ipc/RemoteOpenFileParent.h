/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_RemoteOpenFileParent_h
#define mozilla_net_RemoteOpenFileParent_h

#include "mozilla/net/PRemoteOpenFileParent.h"
#include "mozilla/net/NeckoCommon.h"
#include "nsIFileURL.h"

namespace mozilla {
namespace net {

class RemoteOpenFileParent : public PRemoteOpenFileParent
{
public:
  RemoteOpenFileParent(nsIFileURL* aURI);

 ~RemoteOpenFileParent();

  virtual bool RecvAsyncOpenFile();

private:
  nsCOMPtr<nsIFileURL> mURI;

#if !defined(XP_WIN) && !defined(MOZ_WIDGET_COCOA)
  int mFd;
#endif
};

} // namespace net
} // namespace mozilla

#endif // mozilla_net_RemoteOpenFileParent_h
