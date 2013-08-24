/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTArray.h"
#include "nsAudioAvailableEventManager.h"
#include "VideoUtils.h"

static const nsTArray< nsCOMPtr<nsIRunnable> >::size_type MAX_PENDING_EVENTS = 100;

using namespace mozilla;

class nsAudioAvailableEventRunner : public nsRunnable
{
private:
  nsCOMPtr<nsBuiltinDecoder> mDecoder;
  nsAutoArrayPtr<float> mFrameBuffer;
public:
  nsAudioAvailableEventRunner(nsBuiltinDecoder* aDecoder, float* aFrameBuffer,
                              PRUint32 aFrameBufferLength, float aTime) :
    mDecoder(aDecoder),
    mFrameBuffer(aFrameBuffer),
    mFrameBufferLength(aFrameBufferLength),
    mTime(aTime)
  {
    MOZ_COUNT_CTOR(nsAudioAvailableEventRunner);
  }

  ~nsAudioAvailableEventRunner() {
    MOZ_COUNT_DTOR(nsAudioAvailableEventRunner);
  }

  NS_IMETHOD Run()
  {
    mDecoder->AudioAvailable(mFrameBuffer.forget(), mFrameBufferLength, mTime);
    return NS_OK;
  }

  const PRUint32 mFrameBufferLength;

  // Start time of the buffer data (in seconds).
  const float mTime;
};


nsAudioAvailableEventManager::nsAudioAvailableEventManager(nsBuiltinDecoder* aDecoder) :
  mDecoder(aDecoder),
  mSignalBuffer(new float[mDecoder->GetFrameBufferLength()]),
  mSignalBufferLength(mDecoder->GetFrameBufferLength()),
  mNewSignalBufferLength(mSignalBufferLength),
  mSignalBufferPosition(0),
  mReentrantMonitor("media.audioavailableeventmanager"),
  mHasListener(false)
{
  MOZ_COUNT_CTOR(nsAudioAvailableEventManager);
}

nsAudioAvailableEventManager::~nsAudioAvailableEventManager()
{
  MOZ_COUNT_DTOR(nsAudioAvailableEventManager);
}

void nsAudioAvailableEventManager::Init(PRUint32 aChannels, PRUint32 aRate)
{
  NS_ASSERTION(aChannels != 0 && aRate != 0, "Audio metadata not known.");
  mSamplesPerSecond = static_cast<float>(aChannels * aRate);
}

void nsAudioAvailableEventManager::DispatchPendingEvents(PRUint64 aCurrentTime)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  if (!mHasListener) {
    return;
  }

  while (mPendingEvents.Length() > 0) {
    nsAudioAvailableEventRunner* e =
      (nsAudioAvailableEventRunner*)mPendingEvents[0].get();
    if (e->mTime * USECS_PER_S > aCurrentTime) {
      break;
    }
    nsCOMPtr<nsIRunnable> event = mPendingEvents[0];
    mPendingEvents.RemoveElementAt(0);
    NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
  }
}

void nsAudioAvailableEventManager::QueueWrittenAudioData(AudioDataValue* aAudioData,
                                                         PRUint32 aAudioDataLength,
                                                         PRUint64 aEndTimeSampleOffset)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  if (!mHasListener) {
    return;
  }

  PRUint32 currentBufferSize = mNewSignalBufferLength;
  if (currentBufferSize == 0) {
    NS_WARNING("Decoder framebuffer length not set.");
    return;
  }

  if (!mSignalBuffer ||
      (mSignalBufferPosition == 0 && mSignalBufferLength != currentBufferSize)) {
    if (!mSignalBuffer || (mSignalBufferLength < currentBufferSize)) {
      // Only resize if buffer is empty or smaller.
      mSignalBuffer = new float[currentBufferSize];
    }
    mSignalBufferLength = currentBufferSize;
  }
  AudioDataValue* audioData = aAudioData;
  PRUint32 audioDataLength = aAudioDataLength;
  PRUint32 signalBufferTail = mSignalBufferLength - mSignalBufferPosition;

  // Group audio samples into optimal size for event dispatch, and queue.
  while (signalBufferTail <= audioDataLength) {
    float time = 0.0;
    // Guard against unsigned number overflow during first frame time calculation.
    if (aEndTimeSampleOffset > mSignalBufferPosition + audioDataLength) {
      time = (aEndTimeSampleOffset - mSignalBufferPosition - audioDataLength) / 
             mSamplesPerSecond;
    }

    // Fill the signalBuffer.
    PRUint32 i;
    float *signalBuffer = mSignalBuffer.get() + mSignalBufferPosition;
    if (audioData) {
      for (i = 0; i < signalBufferTail; ++i) {
        signalBuffer[i] = MOZ_CONVERT_AUDIO_SAMPLE(audioData[i]);
      }
    } else {
      memset(signalBuffer, 0, signalBufferTail*sizeof(signalBuffer[0]));
    }
    if (audioData) {
      audioData += signalBufferTail;
    }

    NS_ASSERTION(audioDataLength >= signalBufferTail,
                 "audioDataLength about to wrap past zero to +infinity!");
    audioDataLength -= signalBufferTail;

    if (mPendingEvents.Length() > 0) {
      // Check last event timecode to make sure that all queued events
      // are in non-descending sequence.
      nsAudioAvailableEventRunner* lastPendingEvent =
        (nsAudioAvailableEventRunner*)mPendingEvents[mPendingEvents.Length() - 1].get();
      if (lastPendingEvent->mTime > time) {
        // Clear the queue to start a fresh sequence.
        mPendingEvents.Clear();
      } else if (mPendingEvents.Length() >= MAX_PENDING_EVENTS) {
        NS_WARNING("Hit audio event queue max.");
        mPendingEvents.RemoveElementsAt(0, mPendingEvents.Length() - MAX_PENDING_EVENTS + 1);
      }
    }

    // Inform the element that we've written audio data.
    nsCOMPtr<nsIRunnable> event =
      new nsAudioAvailableEventRunner(mDecoder, mSignalBuffer.forget(),
                                      mSignalBufferLength, time);
    mPendingEvents.AppendElement(event);

    // Reset the buffer
    mSignalBufferLength = currentBufferSize;
    mSignalBuffer = new float[currentBufferSize];
    mSignalBufferPosition = 0;
    signalBufferTail = currentBufferSize;
  }

  NS_ASSERTION(mSignalBufferPosition + audioDataLength < mSignalBufferLength,
               "Intermediate signal buffer must fit at least one more item.");

  if (audioDataLength > 0) {
    // Add data to the signalBuffer.
    PRUint32 i;
    float *signalBuffer = mSignalBuffer.get() + mSignalBufferPosition;
    if (audioData) {
      for (i = 0; i < audioDataLength; ++i) {
        signalBuffer[i] = MOZ_CONVERT_AUDIO_SAMPLE(audioData[i]);
      }
    } else {
      memset(signalBuffer, 0, audioDataLength*sizeof(signalBuffer[0]));
    }
    mSignalBufferPosition += audioDataLength;
  }
}

void nsAudioAvailableEventManager::Clear()
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  mPendingEvents.Clear();
  mSignalBufferPosition = 0;
}

void nsAudioAvailableEventManager::Drain(PRUint64 aEndTime)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  if (!mHasListener) {
    return;
  }

  // Force all pending events to go now.
  for (PRUint32 i = 0; i < mPendingEvents.Length(); ++i) {
    nsCOMPtr<nsIRunnable> event = mPendingEvents[i];
    NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL);
  }
  mPendingEvents.Clear();

  // If there is anything left in the signal buffer, put it in an event and fire.
  if (0 == mSignalBufferPosition)
    return;

  // Zero-pad the end of the signal buffer so it's complete.
  memset(mSignalBuffer.get() + mSignalBufferPosition, 0,
         (mSignalBufferLength - mSignalBufferPosition) * sizeof(float));

  // Force this last event to go now.
  float time = (aEndTime / static_cast<float>(USECS_PER_S)) - 
               (mSignalBufferPosition / mSamplesPerSecond);
  nsCOMPtr<nsIRunnable> lastEvent =
    new nsAudioAvailableEventRunner(mDecoder, mSignalBuffer.forget(),
                                    mSignalBufferLength, time);
  NS_DispatchToMainThread(lastEvent, NS_DISPATCH_NORMAL);

  mSignalBufferPosition = 0;
}

void nsAudioAvailableEventManager::SetSignalBufferLength(PRUint32 aLength)
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  mNewSignalBufferLength = aLength;
}

void nsAudioAvailableEventManager::NotifyAudioAvailableListener()
{
  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  mHasListener = true;
}
