
/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Decoder.h"
#include "nsIServiceManager.h"
#include "nsIConsoleService.h"
#include "nsIScriptError.h"

namespace mozilla {
namespace image {

Decoder::Decoder(RasterImage &aImage, imgIDecoderObserver* aObserver)
  : mImage(aImage)
  , mObserver(aObserver)
  , mDecodeFlags(0)
  , mDecodeDone(false)
  , mDataError(false)
  , mFrameCount(0)
  , mFailCode(NS_OK)
  , mInitialized(false)
  , mSizeDecode(false)
  , mInFrame(false)
  , mIsAnimated(false)
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

void
Decoder::Init()
{
  // No re-initializing
  NS_ABORT_IF_FALSE(!mInitialized, "Can't re-initialize a decoder!");

  // Fire OnStartDecode at init time to support bug 512435
  if (!IsSizeDecode() && mObserver)
      mObserver->OnStartDecode(nsnull);

  // Implementation-specific initialization
  InitInternal();
  mInitialized = true;
}

// Initializes a decoder whose aImage and aObserver is already being used by a
// parent decoder
void
Decoder::InitSharedDecoder()
{
  // No re-initializing
  NS_ABORT_IF_FALSE(!mInitialized, "Can't re-initialize a decoder!");

  // Implementation-specific initialization
  InitInternal();
  mInitialized = true;
}

void
Decoder::Write(const char* aBuffer, PRUint32 aCount)
{
  // We're strict about decoder errors
  NS_ABORT_IF_FALSE(!HasDecoderError(),
                    "Not allowed to make more decoder calls after error!");

  // If a data error occured, just ignore future data
  if (HasDataError())
    return;

  // Pass the data along to the implementation
  WriteInternal(aBuffer, aCount);
}

void
Decoder::Finish()
{
  // Implementation-specific finalization
  if (!HasError())
    FinishInternal();

  // If the implementation left us mid-frame, finish that up.
  if (mInFrame && !HasDecoderError())
    PostFrameStop();

  // If PostDecodeDone() has not been called, we need to sent teardown
  // notifications.
  if (!IsSizeDecode() && !mDecodeDone) {

    // Log data errors to the error console
    nsCOMPtr<nsIConsoleService> consoleService =
      do_GetService(NS_CONSOLESERVICE_CONTRACTID);
    nsCOMPtr<nsIScriptError> errorObject =
      do_CreateInstance(NS_SCRIPTERROR_CONTRACTID);

    if (consoleService && errorObject && !HasDecoderError()) {
      nsAutoString msg(NS_LITERAL_STRING("Image corrupt or truncated: ") +
                       NS_ConvertASCIItoUTF16(mImage.GetURIString()));

      if (NS_SUCCEEDED(errorObject->InitWithWindowID(
                         msg.get(),
                         NS_ConvertUTF8toUTF16(mImage.GetURIString()).get(),
                         nsnull, 0, 0, nsIScriptError::errorFlag,
                         "Image", mImage.InnerWindowID()
                       ))) {
        consoleService->LogMessage(errorObject);
      }
    }

    // If we only have a data error, see if things are worth salvaging
    bool salvage = !HasDecoderError() && mImage.GetNumFrames();

    // If we're salvaging, say we finished decoding
    if (salvage)
      mImage.DecodingComplete();

    // Fire teardown notifications
    if (mObserver) {
      mObserver->OnStopContainer(nsnull, &mImage);
      mObserver->OnStopDecode(nsnull, salvage ? NS_OK : NS_ERROR_FAILURE, nsnull);
    }
  }
}

void
Decoder::FinishSharedDecoder()
{
  if (!HasError()) {
    FinishInternal();
  }
}

void
Decoder::FlushInvalidations()
{
  NS_ABORT_IF_FALSE(!HasDecoderError(),
                    "Not allowed to make more decoder calls after error!");

  // If we've got an empty invalidation rect, we have nothing to do
  if (mInvalidRect.IsEmpty())
    return;

  // Tell the image that it's been updated
  mImage.FrameUpdated(mFrameCount - 1, mInvalidRect);

  // Fire OnDataAvailable
  if (mObserver) {
#ifdef XP_MACOSX
    // Bug 703231
    // Because of high quality down sampling on mac we show scan lines while decoding.
    // Bypass this problem by redrawing the border.
    PRInt32 width;
    PRInt32 height;

    mImage.GetWidth(&width);
    mImage.GetHeight(&height);
    nsIntRect mImageBound(0, 0, width, height);

    mInvalidRect.Inflate(1);
    mInvalidRect = mInvalidRect.Intersect(mImageBound);
#endif
    bool isCurrentFrame = mImage.GetCurrentFrameIndex() == (mFrameCount - 1);
    mObserver->OnDataAvailable(nsnull, isCurrentFrame, &mInvalidRect);
  }

  // Clear the invalidation rectangle
  mInvalidRect.SetEmpty();
}

/*
 * Hook stubs. Override these as necessary in decoder implementations.
 */

void Decoder::InitInternal() { }
void Decoder::WriteInternal(const char* aBuffer, PRUint32 aCount) { }
void Decoder::FinishInternal() { }

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
  mImage.SetSize(aWidth, aHeight);

  // Notify the observer
  if (mObserver)
    mObserver->OnStartContainer(nsnull, &mImage);
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
  NS_ABORT_IF_FALSE(mFrameCount == mImage.GetNumFrames(),
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

  // Fire notifications
  if (mObserver) {
    mObserver->OnStopFrame(nsnull, mFrameCount - 1); // frame # is zero-indexed
    if (mFrameCount > 1 && !mIsAnimated) {
      mIsAnimated = true;
      mObserver->OnImageIsAnimated(nsnull);
    }
  }
}

void
Decoder::PostInvalidation(nsIntRect& aRect)
{
  // We should be mid-frame
  NS_ABORT_IF_FALSE(mInFrame, "Can't invalidate when not mid-frame!");

  // Account for the new region
  mInvalidRect.UnionRect(mInvalidRect, aRect);
}

void
Decoder::PostDecodeDone()
{
  NS_ABORT_IF_FALSE(!IsSizeDecode(), "Can't be done with decoding with size decode!");
  NS_ABORT_IF_FALSE(!mInFrame, "Can't be done decoding if we're mid-frame!");
  NS_ABORT_IF_FALSE(!mDecodeDone, "Decode already done!");
  mDecodeDone = true;

  // Set premult before DecodingComplete(), since DecodingComplete() calls Optimize()
  int frames = GetFrameCount();
  bool isNonPremult = GetDecodeFlags() & DECODER_NO_PREMULTIPLY_ALPHA;
  for (int i = 0; i < frames; i++) {
    mImage.SetFrameAsNonPremult(i, isNonPremult);
  }

  // Notify
  mImage.DecodingComplete();
  if (mObserver) {
    mObserver->OnStopContainer(nsnull, &mImage);
    mObserver->OnStopDecode(nsnull, NS_OK, nsnull);
  }
}

void
Decoder::PostDataError()
{
  mDataError = true;
}

void
Decoder::PostDecoderError(nsresult aFailureCode)
{
  NS_ABORT_IF_FALSE(NS_FAILED(aFailureCode), "Not a failure code!");

  mFailCode = aFailureCode;

  // XXXbholley - we should report the image URI here, but imgContainer
  // needs to know its URI first
  NS_WARNING("Image decoding error - This is probably a bug!");
}

} // namespace image
} // namespace mozilla
