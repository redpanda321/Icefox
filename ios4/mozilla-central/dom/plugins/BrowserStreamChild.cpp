/* -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 8 -*- */
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
 * The Original Code is Mozilla Plugin App.
 *
 * The Initial Developer of the Original Code is
 *   Benjamin Smedberg <benjamin@smedbergs.us>
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "BrowserStreamChild.h"
#include "PluginInstanceChild.h"
#include "StreamNotifyChild.h"

namespace mozilla {
namespace plugins {

BrowserStreamChild::BrowserStreamChild(PluginInstanceChild* instance,
                                       const nsCString& url,
                                       const uint32_t& length,
                                       const uint32_t& lastmodified,
                                       StreamNotifyChild* notifyData,
                                       const nsCString& headers,
                                       const nsCString& mimeType,
                                       const bool& seekable,
                                       NPError* rv,
                                       uint16_t* stype)
  : mInstance(instance)
  , mStreamStatus(kStreamOpen)
  , mDestroyPending(NOT_DESTROYED)
  , mNotifyPending(false)
  , mInstanceDying(false)
  , mState(CONSTRUCTING)
  , mURL(url)
  , mHeaders(headers)
  , mStreamNotify(notifyData)
  , mDeliveryTracker(this)
{
  PLUGIN_LOG_DEBUG(("%s (%s, %i, %i, %p, %s, %s)", FULLFUNCTION,
                    url.get(), length, lastmodified, (void*) notifyData,
                    headers.get(), mimeType.get()));

  AssertPluginThread();

  memset(&mStream, 0, sizeof(mStream));
  mStream.ndata = static_cast<AStream*>(this);
  mStream.url = NullableStringGet(mURL);
  mStream.end = length;
  mStream.lastmodified = lastmodified;
  mStream.headers = NullableStringGet(mHeaders);
  if (notifyData)
    mStream.notifyData = notifyData->mClosure;
}

NPError
BrowserStreamChild::StreamConstructed(
            const nsCString& mimeType,
            const bool& seekable,
            uint16_t* stype)
{
  NPError rv = NPERR_NO_ERROR;

  *stype = NP_NORMAL;
  rv = mInstance->mPluginIface->newstream(
    &mInstance->mData, const_cast<char*>(NullableStringGet(mimeType)),
    &mStream, seekable, stype);
  if (rv != NPERR_NO_ERROR) {
    mState = DELETING;
    mStreamNotify = NULL;
  }
  else {
    mState = ALIVE;

    if (mStreamNotify)
      mStreamNotify->SetAssociatedStream(this);
  }

  return rv;
}

BrowserStreamChild::~BrowserStreamChild()
{
  NS_ASSERTION(!mStreamNotify, "Should have nulled it by now!");
}

bool
BrowserStreamChild::RecvWrite(const int32_t& offset,
                              const Buffer& data,
                              const uint32_t& newlength)
{
  PLUGIN_LOG_DEBUG_FUNCTION;

  AssertPluginThread();

  if (ALIVE != mState)
    NS_RUNTIMEABORT("Unexpected state: received data after NPP_DestroyStream?");

  if (kStreamOpen != mStreamStatus)
    return true;

  mStream.end = newlength;

  NS_ASSERTION(data.Length() > 0, "Empty data");

  PendingData* newdata = mPendingData.AppendElement();
  newdata->offset = offset;
  newdata->data = data;
  newdata->curpos = 0;

  EnsureDeliveryPending();

  return true;
}

bool
BrowserStreamChild::AnswerNPP_StreamAsFile(const nsCString& fname)
{
  PLUGIN_LOG_DEBUG(("%s (fname=%s)", FULLFUNCTION, fname.get()));

  AssertPluginThread();

  if (ALIVE != mState)
    NS_RUNTIMEABORT("Unexpected state: received file after NPP_DestroyStream?");

  if (kStreamOpen != mStreamStatus)
    return true;

  mInstance->mPluginIface->asfile(&mInstance->mData, &mStream,
                                  fname.get());
  return true;
}

bool
BrowserStreamChild::RecvNPP_DestroyStream(const NPReason& reason)
{
  PLUGIN_LOG_DEBUG_METHOD;

  if (ALIVE != mState)
    NS_RUNTIMEABORT("Unexpected state: recevied NPP_DestroyStream twice?");

  mState = DYING;
  mDestroyPending = DESTROY_PENDING;
  if (NPRES_DONE != reason)
    mStreamStatus = reason;

  EnsureDeliveryPending();
  return true;
}

bool
BrowserStreamChild::Recv__delete__()
{
  AssertPluginThread();

  if (DELETING != mState)
    NS_RUNTIMEABORT("Bad state, not DELETING");

  return true;
}

NPError
BrowserStreamChild::NPN_RequestRead(NPByteRange* aRangeList)
{
  PLUGIN_LOG_DEBUG_FUNCTION;

  AssertPluginThread();

  if (ALIVE != mState || kStreamOpen != mStreamStatus)
    return NPERR_GENERIC_ERROR;

  IPCByteRanges ranges;
  for (; aRangeList; aRangeList = aRangeList->next) {
    IPCByteRange br = {aRangeList->offset, aRangeList->length};
    ranges.push_back(br);
  }

  NPError result;
  CallNPN_RequestRead(ranges, &result);
  return result;
}

void
BrowserStreamChild::NPN_DestroyStream(NPReason reason)
{
  mStreamStatus = reason;
  if (ALIVE == mState)
    SendNPN_DestroyStream(reason);

  EnsureDeliveryPending();
}

void
BrowserStreamChild::EnsureDeliveryPending()
{
  MessageLoop::current()->PostTask(FROM_HERE,
    mDeliveryTracker.NewRunnableMethod(&BrowserStreamChild::Deliver));
}

void
BrowserStreamChild::Deliver()
{
  while (kStreamOpen == mStreamStatus && mPendingData.Length()) {
    if (DeliverPendingData() && kStreamOpen == mStreamStatus) {
      SetSuspendedTimer();
      return;
    }
  }
  ClearSuspendedTimer();

  NS_ASSERTION(kStreamOpen != mStreamStatus || 0 == mPendingData.Length(),
               "Exit out of the data-delivery loop with pending data");
  mPendingData.Clear();

  if (DESTROY_PENDING == mDestroyPending) {
    mDestroyPending = DESTROYED;
    if (mState != DYING)
      NS_RUNTIMEABORT("mDestroyPending but state not DYING");

    NS_ASSERTION(NPRES_DONE != mStreamStatus, "Success status set too early!");
    if (kStreamOpen == mStreamStatus)
      mStreamStatus = NPRES_DONE;

    (void) mInstance->mPluginIface
      ->destroystream(&mInstance->mData, &mStream, mStreamStatus);
  }
  if (DESTROYED == mDestroyPending && mNotifyPending) {
    NS_ASSERTION(mStreamNotify, "mDestroyPending but no mStreamNotify?");
      
    mNotifyPending = false;
    mStreamNotify->NPP_URLNotify(mStreamStatus);
    delete mStreamNotify;
    mStreamNotify = NULL;
  }
  if (DYING == mState && DESTROYED == mDestroyPending
      && !mStreamNotify && !mInstanceDying) {
    SendStreamDestroyed();
    mState = DELETING;
  }
}

bool
BrowserStreamChild::DeliverPendingData()
{
  if (mState != ALIVE && mState != DYING)
    NS_RUNTIMEABORT("Unexpected state");

  NS_ASSERTION(mPendingData.Length(), "Called from Deliver with empty pending");

  while (mPendingData[0].curpos < static_cast<int32_t>(mPendingData[0].data.Length())) {
    int32_t r = mInstance->mPluginIface->writeready(&mInstance->mData, &mStream);
    if (kStreamOpen != mStreamStatus)
      return false;
    if (0 == r) // plugin wants to suspend delivery
      return true;

    r = mInstance->mPluginIface->write(
      &mInstance->mData, &mStream,
      mPendingData[0].offset + mPendingData[0].curpos, // offset
      mPendingData[0].data.Length() - mPendingData[0].curpos, // length
      const_cast<char*>(mPendingData[0].data.BeginReading() + mPendingData[0].curpos));
    if (kStreamOpen != mStreamStatus)
      return false;
    if (0 == r)
      return true;
    if (r < 0) { // error condition
      NPN_DestroyStream(NPRES_NETWORK_ERR);
      return false;
    }
    mPendingData[0].curpos += r;
  }
  mPendingData.RemoveElementAt(0);
  return false;
}

void
BrowserStreamChild::SetSuspendedTimer()
{
  if (mSuspendedTimer.IsRunning())
    return;
  mSuspendedTimer.Start(
    base::TimeDelta::FromMilliseconds(100), // 100ms copied from Mozilla plugin host
    this, &BrowserStreamChild::Deliver);
}

void
BrowserStreamChild::ClearSuspendedTimer()
{
  mSuspendedTimer.Stop();
}

} /* namespace plugins */
} /* namespace mozilla */
