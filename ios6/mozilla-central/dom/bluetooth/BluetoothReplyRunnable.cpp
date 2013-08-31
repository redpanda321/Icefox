/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"
#include "BluetoothReplyRunnable.h"
#include "nsIDOMDOMRequest.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"

USING_BLUETOOTH_NAMESPACE

BluetoothReplyRunnable::BluetoothReplyRunnable(nsIDOMDOMRequest* aReq)
  : mDOMRequest(aReq)
{}

void
BluetoothReplyRunnable::SetReply(BluetoothReply* aReply)
{
  mReply = aReply;
}

void
BluetoothReplyRunnable::ReleaseMembers()
{
  mDOMRequest = nullptr;
}

BluetoothReplyRunnable::~BluetoothReplyRunnable()
{}

nsresult
BluetoothReplyRunnable::FireReply(const jsval& aVal)
{
  nsCOMPtr<nsIDOMRequestService> rs =
    do_GetService("@mozilla.org/dom/dom-request-service;1");
  
  if (!rs) {
    NS_WARNING("No DOMRequest Service!");
    return NS_ERROR_FAILURE;
  }
  
  
  return mReply->type() == BluetoothReply::TBluetoothReplySuccess ?
    rs->FireSuccessAsync(mDOMRequest, aVal) :
    rs->FireErrorAsync(mDOMRequest, mReply->get_BluetoothReplyError().error());
}

nsresult
BluetoothReplyRunnable::FireErrorString()
{
  nsCOMPtr<nsIDOMRequestService> rs =
    do_GetService("@mozilla.org/dom/dom-request-service;1");
  
  if (!rs) {
    NS_WARNING("No DOMRequest Service!");
    return NS_ERROR_FAILURE;
  }
  
  return rs->FireErrorAsync(mDOMRequest, mErrorString);
}

NS_IMETHODIMP
BluetoothReplyRunnable::Run()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mDOMRequest);
  MOZ_ASSERT(mReply);

  nsresult rv;

  if (mReply->type() != BluetoothReply::TBluetoothReplySuccess) {
    rv = FireReply(JSVAL_VOID);
  } else {
    jsval v; 
    if (!ParseSuccessfulReply(&v)) {
      rv = FireErrorString();
    } else {
      rv = FireReply(v);
    }
  }

  if (NS_FAILED(rv)) {
    NS_WARNING("Could not fire DOMRequest!");
  }

  ReleaseMembers();
  MOZ_ASSERT(!mDOMRequest,
             "mDOMRequest still alive! Deriving class should call "
             "BluetoothReplyRunnable::ReleaseMembers()!");

  return rv;
}

BluetoothVoidReplyRunnable::BluetoothVoidReplyRunnable(nsIDOMDOMRequest* aReq)
  : BluetoothReplyRunnable(aReq)
{}

BluetoothVoidReplyRunnable::~BluetoothVoidReplyRunnable()
{}

