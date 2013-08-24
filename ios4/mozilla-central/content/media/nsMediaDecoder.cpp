/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
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
 * The Original Code is Mozilla code.
 *
 * The Initial Developer of the Original Code is the Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Chris Double <chris.double@double.co.nz>
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

#include "nsMediaDecoder.h"
#include "nsMediaStream.h"

#include "prlog.h"
#include "prmem.h"
#include "nsIFrame.h"
#include "nsIDocument.h"
#include "nsThreadUtils.h"
#include "nsIDOMHTMLMediaElement.h"
#include "nsNetUtil.h"
#include "nsHTMLMediaElement.h"
#include "nsAutoLock.h"
#include "nsIRenderingContext.h"
#include "gfxContext.h"
#include "gfxImageSurface.h"
#include "nsPresContext.h"
#include "nsDOMError.h"
#include "nsDisplayList.h"
#ifdef MOZ_SVG
#include "nsSVGEffects.h"
#endif

#if defined(XP_MACOSX)
#include "gfxQuartzImageSurface.h"
#endif

// Number of milliseconds between progress events as defined by spec
#define PROGRESS_MS 350

// Number of milliseconds of no data before a stall event is fired as defined by spec
#define STALL_MS 3000

nsMediaDecoder::nsMediaDecoder() :
  mElement(0),
  mRGBWidth(-1),
  mRGBHeight(-1),
  mProgressTime(),
  mDataTime(),
  mVideoUpdateLock(nsnull),
  mPixelAspectRatio(1.0),
  mFrameBufferLength(0),
  mPinnedForSeek(PR_FALSE),
  mSizeChanged(PR_FALSE),
  mShuttingDown(PR_FALSE)
{
  MOZ_COUNT_CTOR(nsMediaDecoder);
}

nsMediaDecoder::~nsMediaDecoder()
{
  if (mVideoUpdateLock) {
    PR_DestroyLock(mVideoUpdateLock);
    mVideoUpdateLock = nsnull;
  }
  MOZ_COUNT_DTOR(nsMediaDecoder);
}

PRBool nsMediaDecoder::Init(nsHTMLMediaElement* aElement)
{
  mElement = aElement;
  mVideoUpdateLock = PR_NewLock();

  return mVideoUpdateLock != nsnull;
}

void nsMediaDecoder::Shutdown()
{
  StopProgress();
  mElement = nsnull;
}

nsHTMLMediaElement* nsMediaDecoder::GetMediaElement()
{
  return mElement;
}

nsresult nsMediaDecoder::RequestFrameBufferLength(PRUint32 aLength)
{
  if (aLength < FRAMEBUFFER_LENGTH_MIN || aLength > FRAMEBUFFER_LENGTH_MAX) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  mFrameBufferLength = aLength;
  return NS_OK;
}


static PRInt32 ConditionDimension(float aValue, PRInt32 aDefault)
{
  // This will exclude NaNs and infinities
  if (aValue >= 1.0 && aValue <= 10000.0)
    return PRInt32(NS_round(aValue));
  return aDefault;
}

void nsMediaDecoder::Invalidate()
{
  if (!mElement)
    return;

  nsIFrame* frame = mElement->GetPrimaryFrame();

  {
    nsAutoLock lock(mVideoUpdateLock);
    if (mSizeChanged) {
      nsIntSize scaledSize(mRGBWidth, mRGBHeight);
      // Apply the aspect ratio to produce the intrinsic size we report
      // to the element.
      if (mPixelAspectRatio > 1.0) {
        // Increase the intrinsic width
        scaledSize.width =
          ConditionDimension(mPixelAspectRatio*scaledSize.width, scaledSize.width);
      } else {
        // Increase the intrinsic height
        scaledSize.height =
          ConditionDimension(scaledSize.height/mPixelAspectRatio, scaledSize.height);
      }
      mElement->UpdateMediaSize(scaledSize);

      mSizeChanged = PR_FALSE;
      if (frame) {
        nsPresContext* presContext = frame->PresContext();
        nsIPresShell *presShell = presContext->PresShell();
        presShell->FrameNeedsReflow(frame,
                                    nsIPresShell::eStyleChange,
                                    NS_FRAME_IS_DIRTY);
      }
    }
  }

  if (frame) {
    nsRect contentRect = frame->GetContentRect() - frame->GetPosition();
    // Only the layer needs to be updated here
    frame->InvalidateLayer(contentRect, nsDisplayItem::TYPE_VIDEO);
  }

#ifdef MOZ_SVG
  nsSVGEffects::InvalidateDirectRenderingObservers(mElement);
#endif
}

static void ProgressCallback(nsITimer* aTimer, void* aClosure)
{
  nsMediaDecoder* decoder = static_cast<nsMediaDecoder*>(aClosure);
  decoder->Progress(PR_TRUE);
}

void nsMediaDecoder::Progress(PRBool aTimer)
{
  if (!mElement)
    return;

  TimeStamp now = TimeStamp::Now();

  if (!aTimer) {
    mDataTime = now;
  }

  // If PROGRESS_MS has passed since the last progress event fired and more
  // data has arrived since then, fire another progress event.
  if ((mProgressTime.IsNull() ||
       now - mProgressTime >= TimeDuration::FromMilliseconds(PROGRESS_MS)) &&
      !mDataTime.IsNull() &&
      now - mDataTime <= TimeDuration::FromMilliseconds(PROGRESS_MS)) {
    mElement->DispatchAsyncProgressEvent(NS_LITERAL_STRING("progress"));
    mProgressTime = now;
  }

  if (!mDataTime.IsNull() &&
      now - mDataTime >= TimeDuration::FromMilliseconds(STALL_MS)) {
    mElement->DownloadStalled();
    // Null it out
    mDataTime = TimeStamp();
  }
}

nsresult nsMediaDecoder::StartProgress()
{
  if (mProgressTimer)
    return NS_OK;

  mProgressTimer = do_CreateInstance("@mozilla.org/timer;1");
  return mProgressTimer->InitWithFuncCallback(ProgressCallback,
                                              this,
                                              PROGRESS_MS,
                                              nsITimer::TYPE_REPEATING_SLACK);
}

nsresult nsMediaDecoder::StopProgress()
{
  if (!mProgressTimer)
    return NS_OK;

  nsresult rv = mProgressTimer->Cancel();
  mProgressTimer = nsnull;

  return rv;
}

void nsMediaDecoder::SetVideoData(const gfxIntSize& aSize,
                                  float aPixelAspectRatio,
                                  Image* aImage)
{
  nsAutoLock lock(mVideoUpdateLock);

  if (mRGBWidth != aSize.width || mRGBHeight != aSize.height ||
      mPixelAspectRatio != aPixelAspectRatio) {
    mRGBWidth = aSize.width;
    mRGBHeight = aSize.height;
    mPixelAspectRatio = aPixelAspectRatio;
    mSizeChanged = PR_TRUE;
  }
  if (mImageContainer && aImage) {
    mImageContainer->SetCurrentImage(aImage);
  }
}

void nsMediaDecoder::PinForSeek()
{
  nsMediaStream* stream = GetCurrentStream();
  if (!stream || mPinnedForSeek) {
    return;
  }
  mPinnedForSeek = PR_TRUE;
  stream->Pin();
}

void nsMediaDecoder::UnpinForSeek()
{
  nsMediaStream* stream = GetCurrentStream();
  if (!stream || !mPinnedForSeek) {
    return;
  }
  mPinnedForSeek = PR_FALSE;
  stream->Unpin();
}

// Number of bytes to add to the download size when we're computing
// when the download will finish --- a safety margin in case bandwidth
// or other conditions are worse than expected
static const PRInt32 gDownloadSizeSafetyMargin = 1000000;

PRBool nsMediaDecoder::CanPlayThrough()
{
  Statistics stats = GetStatistics();
  if (!stats.mDownloadRateReliable || !stats.mPlaybackRateReliable) {
    return PR_FALSE;
  }
  PRInt64 bytesToDownload = stats.mTotalBytes - stats.mDownloadPosition;
  PRInt64 bytesToPlayback = stats.mTotalBytes - stats.mPlaybackPosition;
  double timeToDownload =
    (bytesToDownload + gDownloadSizeSafetyMargin)/stats.mDownloadRate;
  double timeToPlay = bytesToPlayback/stats.mPlaybackRate;
  return timeToDownload <= timeToPlay;
}
