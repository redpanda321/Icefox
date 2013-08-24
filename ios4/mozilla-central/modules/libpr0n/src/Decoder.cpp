
/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010.
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

#include "Decoder.h"

namespace mozilla {
namespace imagelib {

Decoder::Decoder()
  : mFrameCount(0)
  , mInitialized(false)
  , mSizeDecode(false)
  , mInFrame(false)
{
}

Decoder::~Decoder()
{
  NS_WARN_IF_FALSE(!mInFrame, "Shutting down decoder mid-frame!");
  mInitialized = false;
}

/*
 * Common implementation of the decoder interface.
 */

nsresult
Decoder::Init(RasterImage* aImage, imgIDecoderObserver* aObserver)
{
  // We should always have an image
  NS_ABORT_IF_FALSE(aImage, "Can't initialize decoder without an image!");

  // No re-initializing
  NS_ABORT_IF_FALSE(mImage == nsnull, "Can't re-initialize a decoder!");

  // Save our paremeters
  mImage = aImage;
  mObserver = aObserver;

  // Implementation-specific initialization
  nsresult rv = InitInternal();
  mInitialized = true;
  return rv;
}

nsresult
Decoder::Write(const char* aBuffer, PRUint32 aCount)
{
  // Pass the data along to the implementation
  return WriteInternal(aBuffer, aCount);
}

nsresult
Decoder::Finish()
{
  // Implementation-specific finalization
  return FinishInternal();
}

void
Decoder::FlushInvalidations()
{
  // If we've got an empty invalidation rect, we have nothing to do
  if (mInvalidRect.IsEmpty())
    return;

  // Tell the image that it's been updated
  mImage->FrameUpdated(mFrameCount - 1, mInvalidRect);

  // Fire OnDataAvailable
  if (mObserver) {
    PRBool isCurrentFrame = mImage->GetCurrentFrameIndex() == (mFrameCount - 1);
    mObserver->OnDataAvailable(nsnull, isCurrentFrame, &mInvalidRect);
  }

  // Clear the invalidation rectangle
  mInvalidRect.Empty();
}

/*
 * Hook stubs. Override these as necessary in decoder implementations.
 */

nsresult Decoder::InitInternal() {return NS_OK; }
nsresult Decoder::WriteInternal(const char* aBuffer, PRUint32 aCount) {return NS_OK; }
nsresult Decoder::FinishInternal() {return NS_OK; }

/*
 * Progress Notifications
 */

void
Decoder::PostSize(PRInt32 aWidth, PRInt32 aHeight)
{
  // Validate
  NS_ABORT_IF_FALSE(aWidth >= 0, "Width can't be negative!");
  NS_ABORT_IF_FALSE(aHeight >= 0, "Height can't be negative!");

  // Tell the image
  mImage->SetSize(aWidth, aHeight);

  // Notify the observer
  if (mObserver)
    mObserver->OnStartContainer(nsnull, mImage);
}

void
Decoder::PostFrameStart()
{
  // We shouldn't already be mid-frame
  NS_ABORT_IF_FALSE(!mInFrame, "Starting new frame but not done with old one!");

  // We should take care of any invalidation region when wrapping up the
  // previous frame
  NS_ABORT_IF_FALSE(mInvalidRect.IsEmpty(),
                    "Start image frame with non-empty invalidation region!");

  // Update our state to reflect the new frame
  mFrameCount++;
  mInFrame = true;

  // Decoder implementations should only call this method if they successfully
  // appended the frame to the image. So mFrameCount should always match that
  // reported by the Image.
  NS_ABORT_IF_FALSE(mFrameCount == mImage->GetNumFrames(),
                    "Decoder frame count doesn't match image's!");

  // Fire notification
  if (mObserver)
    mObserver->OnStartFrame(nsnull, mFrameCount - 1); // frame # is zero-indexed
}

void
Decoder::PostFrameStop()
{
  // We should be mid-frame
  NS_ABORT_IF_FALSE(mInFrame, "Stopping frame when we didn't start one!");

  // Update our state
  mInFrame = false;

  // Flush any invalidations before we finish the frame
  FlushInvalidations();

  // Fire notification
  if (mObserver)
    mObserver->OnStopFrame(nsnull, mFrameCount - 1); // frame # is zero-indexed
}

void
Decoder::PostInvalidation(nsIntRect& aRect)
{
  // We should be mid-frame
  NS_ABORT_IF_FALSE(mInFrame, "Can't invalidate when not mid-frame!");

  // Account for the new region
  mInvalidRect.UnionRect(mInvalidRect, aRect);
}

} // namespace imagelib
} // namespace mozilla
