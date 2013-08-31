/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */

/* This Source Code Form Is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* DASH - Dynamic Adaptive Streaming over HTTP
 *
 * DASH is an adaptive bitrate streaming technology where a multimedia file is
 * partitioned into one or more segments and delivered to a client using HTTP.
 *
 * see DASHDecoder.cpp for info on DASH interaction with the media engine.*/

#include "nsTimeRanges.h"
#include "VideoFrameContainer.h"
#include "AbstractMediaDecoder.h"
#include "DASHReader.h"
#include "DASHDecoder.h"

namespace mozilla {

#ifdef PR_LOGGING
PRLogModuleInfo* gDASHReaderLog;
#define LOG(msg, ...) PR_LOG(gDASHReaderLog, PR_LOG_DEBUG, \
                             ("%p [DASHReader] " msg, this, __VA_ARGS__))
#define LOG1(msg) PR_LOG(gDASHReaderLog, PR_LOG_DEBUG, \
                         ("%p [DASHReader] " msg, this))
#else
#define LOG(msg, ...)
#define LOG1(msg)
#endif

DASHReader::DASHReader(AbstractMediaDecoder* aDecoder) :
  MediaDecoderReader(aDecoder),
  mReadMetadataMonitor("media.dashreader.readmetadata"),
  mReadyToReadMetadata(false),
  mDecoderIsShuttingDown(false),
  mAudioReader(this),
  mVideoReader(this),
  mAudioReaders(this),
  mVideoReaders(this),
  mSwitchVideoReaders(false),
  mSwitchCount(-1)
{
  MOZ_COUNT_CTOR(DASHReader);
#ifdef PR_LOGGING
  if (!gDASHReaderLog) {
    gDASHReaderLog = PR_NewLogModule("DASHReader");
  }
#endif
}

DASHReader::~DASHReader()
{
  MOZ_COUNT_DTOR(DASHReader);
}

nsresult
DASHReader::ResetDecode()
{
  MediaDecoderReader::ResetDecode();
  nsresult rv;
  for (uint i = 0; i < mAudioReaders.Length(); i++) {
    rv = mAudioReaders[i]->ResetDecode();
    NS_ENSURE_SUCCESS(rv, rv);
  }
  for (uint i = 0; i < mVideoReaders.Length(); i++) {
    rv = mVideoReaders[i]->ResetDecode();
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult
DASHReader::Init(MediaDecoderReader* aCloneDonor)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");

  ReentrantMonitorAutoEnter mon(mDecoder->GetReentrantMonitor());
  NS_ASSERTION(mAudioReaders.Length() != 0 && mVideoReaders.Length() != 0,
               "Audio and video readers should exist already.");

  nsresult rv;
  for (uint i = 0; i < mAudioReaders.Length(); i++) {
    rv = mAudioReaders[i]->Init(nullptr);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  for (uint i = 0; i < mVideoReaders.Length(); i++) {
    rv = mVideoReaders[i]->Init(nullptr);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

void
DASHReader::AddAudioReader(DASHRepReader* aAudioReader)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  NS_ENSURE_TRUE(aAudioReader, );

  ReentrantMonitorAutoEnter mon(mDecoder->GetReentrantMonitor());

  mAudioReaders.AppendElement(aAudioReader);
  // XXX For now, just pick the first reader to be default.
  if (!mAudioReader)
    mAudioReader = aAudioReader;
}

void
DASHReader::AddVideoReader(DASHRepReader* aVideoReader)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  NS_ENSURE_TRUE(aVideoReader, );

  ReentrantMonitorAutoEnter mon(mDecoder->GetReentrantMonitor());

  mVideoReaders.AppendElement(aVideoReader);
  // XXX For now, just pick the first reader to be default.
  if (!mVideoReader)
    mVideoReader = aVideoReader;
}

bool
DASHReader::HasAudio()
{
  NS_ASSERTION(mDecoder->OnDecodeThread(), "Should be on decode thread.");
  return mAudioReader ? mAudioReader->HasAudio() : false;
}

bool
DASHReader::HasVideo()
{
  NS_ASSERTION(mDecoder->OnDecodeThread(), "Should be on decode thread.");
  return mVideoReader ? mVideoReader->HasVideo() : false;
}

int64_t
DASHReader::VideoQueueMemoryInUse()
{
  ReentrantMonitorConditionallyEnter mon(!mDecoder->OnDecodeThread(),
                                         mDecoder->GetReentrantMonitor());
  return VideoQueueMemoryInUse();
}

int64_t
DASHReader::AudioQueueMemoryInUse()
{
  ReentrantMonitorConditionallyEnter mon(!mDecoder->OnDecodeThread(),
                                         mDecoder->GetReentrantMonitor());
  return AudioQueueMemoryInUse();
}

bool
DASHReader::DecodeVideoFrame(bool &aKeyframeSkip,
                             int64_t aTimeThreshold)
{
  NS_ASSERTION(mDecoder->OnDecodeThread(), "Should be on decode thread.");
  if (mVideoReader) {
   return mVideoReader->DecodeVideoFrame(aKeyframeSkip, aTimeThreshold);
  } else {
   return false;
  }
}

bool
DASHReader::DecodeAudioData()
{
  NS_ASSERTION(mDecoder->OnDecodeThread(), "Should be on decode thread.");
  return (mAudioReader ? mAudioReader->DecodeAudioData() : false);
}

nsresult
DASHReader::ReadMetadata(VideoInfo* aInfo,
                         MetadataTags** aTags)
{
  NS_ASSERTION(mDecoder->OnDecodeThread(), "Should be on decode thread.");

  // Wait for MPD to be parsed and child readers created.
  LOG1("Waiting for metadata download.");
  nsresult rv = WaitForMetadata();
  // If we get an abort, return silently; the decoder is shutting down.
  if (NS_ERROR_ABORT == rv) {
    return NS_OK;
  }
  // Verify no other errors before continuing.
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ASSERTION(aTags, "Called with null MetadataTags**.");
  *aTags = nullptr;

  // Get metadata from child readers.
  VideoInfo audioInfo, videoInfo;

  // Read metadata for all video streams.
  for (uint i = 0; i < mVideoReaders.Length(); i++) {
    // Use an nsAutoPtr here to ensure |tags| memory does not leak.
    nsAutoPtr<nsHTMLMediaElement::MetadataTags> tags;
    rv = mVideoReaders[i]->ReadMetadata(&videoInfo, getter_Transfers(tags));
    NS_ENSURE_SUCCESS(rv, rv);
    // Use metadata from current video sub reader to populate aInfo.
    if (mVideoReaders[i] == mVideoReader) {
      mInfo.mHasVideo      = videoInfo.mHasVideo;
      mInfo.mDisplay       = videoInfo.mDisplay;
    }
  }
  // Read metadata for audio stream.
  // Note: Getting metadata tags from audio reader only for now.
  // XXX Audio stream switching not yet supported.
  if (mAudioReader) {
    rv = mAudioReader->ReadMetadata(&audioInfo, aTags);
    NS_ENSURE_SUCCESS(rv, rv);
    mInfo.mHasAudio      = audioInfo.mHasAudio;
    mInfo.mAudioRate     = audioInfo.mAudioRate;
    mInfo.mAudioChannels = audioInfo.mAudioChannels;
    mInfo.mStereoMode    = audioInfo.mStereoMode;
  }

  *aInfo = mInfo;

  return NS_OK;
}

nsresult
DASHReader::Seek(int64_t aTime,
                 int64_t aStartTime,
                 int64_t aEndTime,
                 int64_t aCurrentTime)
{
  NS_ASSERTION(mDecoder->OnDecodeThread(), "Should be on decode thread.");

  NS_ENSURE_SUCCESS(ResetDecode(), NS_ERROR_FAILURE);

  LOG("Seeking to [%.2fs]", aTime/1000000.0);

  nsresult rv;
  DASHDecoder* dashDecoder = static_cast<DASHDecoder*>(mDecoder);

  if (mAudioReader) {
    int64_t subsegmentIdx = -1;
    {
      ReentrantMonitorAutoEnter mon(mDecoder->GetReentrantMonitor());
      subsegmentIdx = mAudioReader->GetSubsegmentForSeekTime(aTime);
      NS_ENSURE_TRUE(0 <= subsegmentIdx, NS_ERROR_ILLEGAL_VALUE);
    }
    dashDecoder->NotifySeekInAudioSubsegment(subsegmentIdx);

    rv = mAudioReader->Seek(aTime, aStartTime, aEndTime, aCurrentTime);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (mVideoReader) {
    // Determine the video subsegment we're seeking to.
    int32_t subsegmentIdx = -1;
    {
      ReentrantMonitorAutoEnter mon(mDecoder->GetReentrantMonitor());
      subsegmentIdx = mVideoReader->GetSubsegmentForSeekTime(aTime);
      NS_ENSURE_TRUE(0 <= subsegmentIdx, NS_ERROR_ILLEGAL_VALUE);
    }

    LOG("Seek to [%.2fs] found in video subsegment [%d]",
        aTime/1000000.0, subsegmentIdx);

    // Determine if/which video reader previously downloaded this subsegment.
    int32_t readerIdx = dashDecoder->GetRepIdxForVideoSubsegmentLoad(subsegmentIdx);

    dashDecoder->NotifySeekInVideoSubsegment(readerIdx, subsegmentIdx);

    if (0 <= readerIdx) {
      NS_ENSURE_TRUE(readerIdx < mVideoReaders.Length(),
                     NS_ERROR_ILLEGAL_VALUE);
      // Switch to this reader and do the Seek.
      DASHRepReader* fromReader = mVideoReader;
      DASHRepReader* toReader = mVideoReaders[readerIdx];

      {
        ReentrantMonitorAutoEnter mon(mDecoder->GetReentrantMonitor());
        if (fromReader != toReader) {
          LOG("Switching video readers now from [%p] to [%p] for a seek to "
              "[%.2fs] in subsegment [%d]",
              fromReader, toReader, aTime/1000000.0, subsegmentIdx);

          mVideoReader = toReader;
        }
      }

      rv = mVideoReader->Seek(aTime, aStartTime, aEndTime, aCurrentTime);
      if (NS_FAILED(rv)) {
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Go back to the appropriate count in the switching history, and setup
      // this main reader and the sub readers for the next switch (if any).
      {
        ReentrantMonitorAutoEnter mon(mDecoder->GetReentrantMonitor());
        mSwitchCount = dashDecoder->GetSwitchCountAtVideoSubsegment(subsegmentIdx);
        LOG("After mVideoReader->Seek() mSwitchCount %d", mSwitchCount);
        NS_ENSURE_TRUE(0 <= mSwitchCount, NS_ERROR_ILLEGAL_VALUE);
        NS_ENSURE_TRUE(mSwitchCount <= subsegmentIdx, NS_ERROR_ILLEGAL_VALUE);
      }
    } else {
      LOG("Error getting rep idx for video subsegment [%d]",
          subsegmentIdx);
    }
  }
  return NS_OK;
}

nsresult
DASHReader::GetBuffered(nsTimeRanges* aBuffered,
                        int64_t aStartTime)
{
  NS_ENSURE_ARG(aBuffered);

  MediaResource* resource = nullptr;
  AbstractMediaDecoder* decoder = nullptr;

  // Need to find intersect of |nsTimeRanges| for audio and video.
  nsTimeRanges audioBuffered, videoBuffered;
  uint32_t audioRangeCount, videoRangeCount;

  nsresult rv = NS_OK;

  // First, get buffered ranges for sub-readers.
  ReentrantMonitorConditionallyEnter mon(!mDecoder->OnDecodeThread(),
                                         mDecoder->GetReentrantMonitor());
  if (mAudioReader) {
    decoder = mAudioReader->GetDecoder();
    NS_ENSURE_TRUE(decoder, NS_ERROR_NULL_POINTER);
    resource = decoder->GetResource();
    NS_ENSURE_TRUE(resource, NS_ERROR_NULL_POINTER);
    resource->Pin();
    rv = mAudioReader->GetBuffered(&audioBuffered, aStartTime);
    NS_ENSURE_SUCCESS(rv, rv);
    resource->Unpin();
    rv = audioBuffered.GetLength(&audioRangeCount);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  if (mVideoReader) {
    decoder = mVideoReader->GetDecoder();
    NS_ENSURE_TRUE(decoder, NS_ERROR_NULL_POINTER);
    resource = decoder->GetResource();
    NS_ENSURE_TRUE(resource, NS_ERROR_NULL_POINTER);
    resource->Pin();
    rv = mVideoReader->GetBuffered(&videoBuffered, aStartTime);
    NS_ENSURE_SUCCESS(rv, rv);
    resource->Unpin();
    rv = videoBuffered.GetLength(&videoRangeCount);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Now determine buffered data for available sub-readers.
  if (mAudioReader && mVideoReader) {
    // Calculate intersecting ranges.
    for (uint32_t i = 0; i < audioRangeCount; i++) {
      // |A|udio, |V|ideo, |I|ntersect.
      double startA, startV, startI;
      double endA, endV, endI;
      rv = audioBuffered.Start(i, &startA);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = audioBuffered.End(i, &endA);
      NS_ENSURE_SUCCESS(rv, rv);

      for (uint32_t j = 0; j < videoRangeCount; j++) {
        rv = videoBuffered.Start(i, &startV);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = videoBuffered.End(i, &endV);
        NS_ENSURE_SUCCESS(rv, rv);

        // If video block is before audio block, compare next video block.
        if (startA > endV) {
          continue;
        // If video block is after audio block, all of them are; compare next
        // audio block.
        } else if (endA < startV) {
          break;
        }
        // Calculate intersections of current audio and video blocks.
        startI = (startA > startV) ? startA : startV;
        endI = (endA > endV) ? endV : endA;
        aBuffered->Add(startI, endI);
      }
    }
  } else if (mAudioReader) {
    *aBuffered = audioBuffered;
  } else if (mVideoReader) {
    *aBuffered = videoBuffered;
  } else {
    return NS_ERROR_NOT_INITIALIZED;
  }

  return NS_OK;
}

VideoData*
DASHReader::FindStartTime(int64_t& aOutStartTime)
{
  NS_ASSERTION(mDecoder->OnStateMachineThread() || mDecoder->OnDecodeThread(),
               "Should be on state machine or decode thread.");

  // Extract the start times of the bitstreams in order to calculate
  // the duration.
  int64_t videoStartTime = INT64_MAX;
  int64_t audioStartTime = INT64_MAX;
  VideoData* videoData = nullptr;

  ReentrantMonitorConditionallyEnter mon(!mDecoder->OnDecodeThread(),
                                         mDecoder->GetReentrantMonitor());
  if (HasVideo()) {
    // Forward to video reader.
    videoData = mVideoReader->DecodeToFirstVideoData();
    if (videoData) {
      videoStartTime = videoData->mTime;
    }
  }
  if (HasAudio()) {
    // Forward to audio reader.
    AudioData* audioData = mAudioReader->DecodeToFirstAudioData();
    if (audioData) {
      audioStartTime = audioData->mTime;
    }
  }

  int64_t startTime = NS_MIN(videoStartTime, audioStartTime);
  if (startTime != INT64_MAX) {
    aOutStartTime = startTime;
  }

  return videoData;
}

MediaQueue<AudioData>&
DASHReader::AudioQueue()
{
  ReentrantMonitorConditionallyEnter mon(!mDecoder->OnDecodeThread(),
                                         mDecoder->GetReentrantMonitor());
  NS_ASSERTION(mAudioReader, "mAudioReader is NULL!");
  return mAudioQueue;
}

MediaQueue<VideoData>&
DASHReader::VideoQueue()
{
  ReentrantMonitorConditionallyEnter mon(!mDecoder->OnDecodeThread(),
                                         mDecoder->GetReentrantMonitor());
  NS_ASSERTION(mVideoReader, "mVideoReader is NULL!");
  return mVideoQueue;
}

void
DASHReader::RequestVideoReaderSwitch(uint32_t aFromReaderIdx,
                                     uint32_t aToReaderIdx,
                                     uint32_t aSubsegmentIdx)
{
  NS_ASSERTION(NS_IsMainThread(), "Should be on main thread.");
  NS_ASSERTION(aFromReaderIdx < mVideoReaders.Length(),
               "From index is greater than number of video readers!");
  NS_ASSERTION(aToReaderIdx < mVideoReaders.Length(),
               "To index is greater than number of video readers!");
  NS_ASSERTION(aToReaderIdx != aFromReaderIdx,
               "Don't request switches to same reader!");
  mDecoder->GetReentrantMonitor().AssertCurrentThreadIn();

  if (mSwitchCount < 0) {
    mSwitchCount = 0;
  }

  DASHRepReader* fromReader = mVideoReaders[aFromReaderIdx];
  DASHRepReader* toReader = mVideoReaders[aToReaderIdx];

  LOG("Switch requested from reader [%d] [%p] to reader [%d] [%p] "
      "at subsegment[%d].",
      aFromReaderIdx, fromReader, aToReaderIdx, toReader, aSubsegmentIdx);

  // Append the subsegment index to the list of pending switches.
  for (uint32_t i = 0; i < mSwitchToVideoSubsegmentIndexes.Length(); i++) {
    if (mSwitchToVideoSubsegmentIndexes[i] == aSubsegmentIdx) {
      // A backwards |Seek| has changed the switching history; delete from
      // this point on.
      mSwitchToVideoSubsegmentIndexes.TruncateLength(i);
      break;
    }
  }
  mSwitchToVideoSubsegmentIndexes.AppendElement(aSubsegmentIdx);

  // Tell the SWITCH FROM reader when it should stop reading.
  fromReader->RequestSwitchAtSubsegment(aSubsegmentIdx, toReader);

  // Tell the SWITCH TO reader to seek to the correct offset.
  toReader->RequestSeekToSubsegment(aSubsegmentIdx);

  mSwitchVideoReaders = true;
}

void
DASHReader::PossiblySwitchVideoReaders()
{
  NS_ASSERTION(mDecoder, "Decoder should not be null");
  NS_ASSERTION(mDecoder->OnDecodeThread(), "Should be on decode thread.");

  // Flag to switch streams is set in |RequestVideoReaderSwitch|.
  if (!mSwitchVideoReaders) {
    return;
  }

  // Only switch if we reached a switch access point.
  NS_ENSURE_TRUE(0 <= mSwitchCount, );
  NS_ENSURE_TRUE((uint32_t)mSwitchCount < mSwitchToVideoSubsegmentIndexes.Length(), );
  uint32_t switchIdx = mSwitchToVideoSubsegmentIndexes[mSwitchCount];
  if (!mVideoReader->HasReachedSubsegment(switchIdx)) {
    return;
  }

  // Get Representation index to switch to.
  DASHDecoder* dashDecoder = static_cast<DASHDecoder*>(mDecoder);
  int32_t toReaderIdx = dashDecoder->GetRepIdxForVideoSubsegmentLoad(switchIdx);
  NS_ENSURE_TRUE(0 <= toReaderIdx, );
  NS_ENSURE_TRUE((uint32_t)toReaderIdx < mVideoReaders.Length(), );

  DASHRepReader* fromReader = mVideoReader;
  DASHRepReader* toReader = mVideoReaders[toReaderIdx];
  NS_ENSURE_TRUE(fromReader != toReader, );

  LOG("Switching video readers now from [%p] to [%p] at subsegment [%d]: "
      "mSwitchCount [%d].",
      fromReader, toReader, switchIdx, mSwitchCount);

  // Switch readers while in the monitor.
  ReentrantMonitorAutoEnter mon(mDecoder->GetReentrantMonitor());
  mVideoReader = toReader;

  // Prep readers for next switch, also while in monitor.
  if ((uint32_t)++mSwitchCount < mSwitchToVideoSubsegmentIndexes.Length()) {
    // Get the subsegment at which to switch.
    switchIdx = mSwitchToVideoSubsegmentIndexes[mSwitchCount];

    // Update from and to reader ptrs for next switch.
    fromReader = toReader;
    toReaderIdx = dashDecoder->GetRepIdxForVideoSubsegmentLoad(switchIdx);
    toReader = mVideoReaders[toReaderIdx];
    NS_ENSURE_TRUE((uint32_t)toReaderIdx < mVideoReaders.Length(), );
    NS_ENSURE_TRUE(fromReader != toReader, );

    // Tell the SWITCH FROM reader when it should stop reading.
    fromReader->RequestSwitchAtSubsegment(switchIdx, toReader);

    // Tell the SWITCH TO reader to seek to the correct offset.
    toReader->RequestSeekToSubsegment(switchIdx);
  } else {
    // If there are no more pending switches, unset the switch readers flag.
    mSwitchVideoReaders = false;
  }
}

void
DASHReader::PrepareToDecode()
{
  NS_ASSERTION(mDecoder->OnDecodeThread(), "Should be on decode thread.");

  // Flag to switch streams is set by |DASHDecoder|.
  if (!mSwitchVideoReaders) {
    return;
  }

  PossiblySwitchVideoReaders();

  // Prepare each sub reader for decoding: includes seeking to the correct
  // offset if a seek was previously requested.
  for (uint32_t i = 0; i < mVideoReaders.Length(); i++) {
    mVideoReaders[i]->PrepareToDecode();
  }
}

} // namespace mozilla

