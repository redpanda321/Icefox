/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GonkIOSurfaceImage.h"
#include "MediaDecoderReader.h"
#include "AbstractMediaDecoder.h"
#include "MediaDecoderStateMachine.h"
#include "VideoUtils.h"
#include "ImageContainer.h"

#include "mozilla/mozalloc.h"
#include "mozilla/StandardInteger.h"

namespace mozilla {

using layers::ImageContainer;
using layers::PlanarYCbCrImage;

// Verify these values are sane. Once we've checked the frame sizes, we then
// can do less integer overflow checking.
PR_STATIC_ASSERT(MAX_VIDEO_WIDTH < PlanarYCbCrImage::MAX_DIMENSION);
PR_STATIC_ASSERT(MAX_VIDEO_HEIGHT < PlanarYCbCrImage::MAX_DIMENSION);
PR_STATIC_ASSERT(PlanarYCbCrImage::MAX_DIMENSION < UINT32_MAX / PlanarYCbCrImage::MAX_DIMENSION);

// Un-comment to enable logging of seek bisections.
//#define SEEK_LOGGING

#ifdef PR_LOGGING
extern PRLogModuleInfo* gMediaDecoderLog;
#define LOG(type, msg) PR_LOG(gMediaDecoderLog, type, msg)
#ifdef SEEK_LOGGING
#define SEEK_LOG(type, msg) PR_LOG(gMediaDecoderLog, type, msg)
#else
#define SEEK_LOG(type, msg)
#endif
#else
#define LOG(type, msg)
#define SEEK_LOG(type, msg)
#endif

void
AudioData::EnsureAudioBuffer()
{
  if (mAudioBuffer)
    return;
  mAudioBuffer = SharedBuffer::Create(mFrames*mChannels*sizeof(AudioDataValue));

  AudioDataValue* data = static_cast<AudioDataValue*>(mAudioBuffer->Data());
  for (uint32_t i = 0; i < mFrames; ++i) {
    for (uint32_t j = 0; j < mChannels; ++j) {
      data[j*mFrames + i] = mAudioData[i*mChannels + j];
    }
  }
}

static bool
ValidatePlane(const VideoData::YCbCrBuffer::Plane& aPlane)
{
  return aPlane.mWidth <= PlanarYCbCrImage::MAX_DIMENSION &&
         aPlane.mHeight <= PlanarYCbCrImage::MAX_DIMENSION &&
         aPlane.mWidth * aPlane.mHeight < MAX_VIDEO_WIDTH * MAX_VIDEO_HEIGHT &&
         aPlane.mStride > 0;
}

static bool
IsYV12Format(const VideoData::YCbCrBuffer::Plane& aYPlane,
             const VideoData::YCbCrBuffer::Plane& aCbPlane,
             const VideoData::YCbCrBuffer::Plane& aCrPlane)
{
  return
    aYPlane.mWidth % 2 == 0 &&
    aYPlane.mHeight % 2 == 0 &&
    aYPlane.mWidth / 2 == aCbPlane.mWidth &&
    aYPlane.mHeight / 2 == aCbPlane.mHeight &&
    aCbPlane.mWidth == aCrPlane.mWidth &&
    aCbPlane.mHeight == aCrPlane.mHeight;
}

bool
VideoInfo::ValidateVideoRegion(const nsIntSize& aFrame,
                                 const nsIntRect& aPicture,
                                 const nsIntSize& aDisplay)
{
  return
    aFrame.width <= PlanarYCbCrImage::MAX_DIMENSION &&
    aFrame.height <= PlanarYCbCrImage::MAX_DIMENSION &&
    aFrame.width * aFrame.height <= MAX_VIDEO_WIDTH * MAX_VIDEO_HEIGHT &&
    aFrame.width * aFrame.height != 0 &&
    aPicture.width <= PlanarYCbCrImage::MAX_DIMENSION &&
    aPicture.x < PlanarYCbCrImage::MAX_DIMENSION &&
    aPicture.x + aPicture.width < PlanarYCbCrImage::MAX_DIMENSION &&
    aPicture.height <= PlanarYCbCrImage::MAX_DIMENSION &&
    aPicture.y < PlanarYCbCrImage::MAX_DIMENSION &&
    aPicture.y + aPicture.height < PlanarYCbCrImage::MAX_DIMENSION &&
    aPicture.width * aPicture.height <= MAX_VIDEO_WIDTH * MAX_VIDEO_HEIGHT &&
    aPicture.width * aPicture.height != 0 &&
    aDisplay.width <= PlanarYCbCrImage::MAX_DIMENSION &&
    aDisplay.height <= PlanarYCbCrImage::MAX_DIMENSION &&
    aDisplay.width * aDisplay.height <= MAX_VIDEO_WIDTH * MAX_VIDEO_HEIGHT &&
    aDisplay.width * aDisplay.height != 0;
}

VideoData::  VideoData(int64_t aOffset, int64_t aTime, int64_t aEndTime, int64_t aTimecode)
  : mOffset(aOffset),
    mTime(aTime),
    mEndTime(aEndTime),
    mTimecode(aTimecode),
    mDuplicate(true),
    mKeyframe(false)
{
  MOZ_COUNT_CTOR(VideoData);
  NS_ASSERTION(aEndTime >= aTime, "Frame must start before it ends.");
}

VideoData::VideoData(int64_t aOffset,
          int64_t aTime,
          int64_t aEndTime,
          bool aKeyframe,
          int64_t aTimecode,
          nsIntSize aDisplay)
  : mDisplay(aDisplay),
    mOffset(aOffset),
    mTime(aTime),
    mEndTime(aEndTime),
    mTimecode(aTimecode),
    mDuplicate(false),
    mKeyframe(aKeyframe)
{
  MOZ_COUNT_CTOR(VideoData);
  NS_ASSERTION(aEndTime >= aTime, "Frame must start before it ends.");
}

VideoData::~VideoData()
{
  MOZ_COUNT_DTOR(VideoData);
}


VideoData* VideoData::Create(VideoInfo& aInfo,
                             ImageContainer* aContainer,
                             int64_t aOffset,
                             int64_t aTime,
                             int64_t aEndTime,
                             const YCbCrBuffer& aBuffer,
                             bool aKeyframe,
                             int64_t aTimecode,
                             nsIntRect aPicture)
{
  if (!aContainer) {
    // Create a dummy VideoData with no image. This gives us something to
    // send to media streams if necessary.
    nsAutoPtr<VideoData> v(new VideoData(aOffset,
                                         aTime,
                                         aEndTime,
                                         aKeyframe,
                                         aTimecode,
                                         aInfo.mDisplay));
    return v.forget();
  }

  // The following situation should never happen unless there is a bug
  // in the decoder
  if (aBuffer.mPlanes[1].mWidth != aBuffer.mPlanes[2].mWidth ||
      aBuffer.mPlanes[1].mHeight != aBuffer.mPlanes[2].mHeight) {
    NS_ERROR("C planes with different sizes");
    return nullptr;
  }

  // The following situations could be triggered by invalid input
  if (aPicture.width <= 0 || aPicture.height <= 0) {
    NS_WARNING("Empty picture rect");
    return nullptr;
  }
  if (!ValidatePlane(aBuffer.mPlanes[0]) || !ValidatePlane(aBuffer.mPlanes[1]) ||
      !ValidatePlane(aBuffer.mPlanes[2])) {
    NS_WARNING("Invalid plane size");
    return nullptr;
  }

  // Ensure the picture size specified in the headers can be extracted out of
  // the frame we've been supplied without indexing out of bounds.
  CheckedUint32 xLimit = aPicture.x + CheckedUint32(aPicture.width);
  CheckedUint32 yLimit = aPicture.y + CheckedUint32(aPicture.height);
  if (!xLimit.isValid() || xLimit.value() > aBuffer.mPlanes[0].mStride ||
      !yLimit.isValid() || yLimit.value() > aBuffer.mPlanes[0].mHeight)
  {
    // The specified picture dimensions can't be contained inside the video
    // frame, we'll stomp memory if we try to copy it. Fail.
    NS_WARNING("Overflowing picture rect");
    return nullptr;
  }

  nsAutoPtr<VideoData> v(new VideoData(aOffset,
                                       aTime,
                                       aEndTime,
                                       aKeyframe,
                                       aTimecode,
                                       aInfo.mDisplay));
  const YCbCrBuffer::Plane &Y = aBuffer.mPlanes[0];
  const YCbCrBuffer::Plane &Cb = aBuffer.mPlanes[1];
  const YCbCrBuffer::Plane &Cr = aBuffer.mPlanes[2];

  // Currently our decoder only knows how to output to PLANAR_YCBCR
  // format.
  ImageFormat format[2] = {PLANAR_YCBCR, GRALLOC_PLANAR_YCBCR};
  if (IsYV12Format(Y, Cb, Cr)) {
    v->mImage = aContainer->CreateImage(format, 2);
  } else {
    v->mImage = aContainer->CreateImage(format, 1);
  }
  if (!v->mImage) {
    return nullptr;
  }
  NS_ASSERTION(v->mImage->GetFormat() == PLANAR_YCBCR ||
               v->mImage->GetFormat() == GRALLOC_PLANAR_YCBCR,
               "Wrong format?");
  PlanarYCbCrImage* videoImage = static_cast<PlanarYCbCrImage*>(v->mImage.get());

  PlanarYCbCrImage::Data data;
  data.mYChannel = Y.mData + Y.mOffset;
  data.mYSize = gfxIntSize(Y.mWidth, Y.mHeight);
  data.mYStride = Y.mStride;
  data.mYSkip = Y.mSkip;
  data.mCbChannel = Cb.mData + Cb.mOffset;
  data.mCrChannel = Cr.mData + Cr.mOffset;
  data.mCbCrSize = gfxIntSize(Cb.mWidth, Cb.mHeight);
  data.mCbCrStride = Cb.mStride;
  data.mCbSkip = Cb.mSkip;
  data.mCrSkip = Cr.mSkip;
  data.mPicX = aPicture.x;
  data.mPicY = aPicture.y;
  data.mPicSize = gfxIntSize(aPicture.width, aPicture.height);
  data.mStereoMode = aInfo.mStereoMode;

  videoImage->SetDelayedConversion(true);
  videoImage->SetData(data);
  return v.forget();
}

VideoData* VideoData::CreateFromImage(VideoInfo& aInfo,
                                      ImageContainer* aContainer,
                                      int64_t aOffset,
                                      int64_t aTime,
                                      int64_t aEndTime,
                                      const nsRefPtr<Image>& aImage,
                                      bool aKeyframe,
                                      int64_t aTimecode,
                                      nsIntRect aPicture)
{
  nsAutoPtr<VideoData> v(new VideoData(aOffset,
                                       aTime,
                                       aEndTime,
                                       aKeyframe,
                                       aTimecode,
                                       aInfo.mDisplay));
  v->mImage = aImage;
  return v.forget();
}

#ifdef MOZ_WIDGET_GONK
VideoData* VideoData::Create(VideoInfo& aInfo,
                             ImageContainer* aContainer,
                             int64_t aOffset,
                             int64_t aTime,
                             int64_t aEndTime,
                             mozilla::layers::GraphicBufferLocked *aBuffer,
                             bool aKeyframe,
                             int64_t aTimecode,
                             nsIntRect aPicture)
{
  if (!aContainer) {
    // Create a dummy VideoData with no image. This gives us something to
    // send to media streams if necessary.
    nsAutoPtr<VideoData> v(new VideoData(aOffset,
                                         aTime,
                                         aEndTime,
                                         aKeyframe,
                                         aTimecode,
                                         aInfo.mDisplay));
    return v.forget();
  }

  // The following situations could be triggered by invalid input
  if (aPicture.width <= 0 || aPicture.height <= 0) {
    NS_WARNING("Empty picture rect");
    return nullptr;
  }

  // Ensure the picture size specified in the headers can be extracted out of
  // the frame we've been supplied without indexing out of bounds.
  CheckedUint32 xLimit = aPicture.x + CheckedUint32(aPicture.width);
  CheckedUint32 yLimit = aPicture.y + CheckedUint32(aPicture.height);
  if (!xLimit.isValid() || !yLimit.isValid())
  {
    // The specified picture dimensions can't be contained inside the video
    // frame, we'll stomp memory if we try to copy it. Fail.
    NS_WARNING("Overflowing picture rect");
    return nullptr;
  }

  nsAutoPtr<VideoData> v(new VideoData(aOffset,
                                       aTime,
                                       aEndTime,
                                       aKeyframe,
                                       aTimecode,
                                       aInfo.mDisplay));

  ImageFormat format = GONK_IO_SURFACE;
  v->mImage = aContainer->CreateImage(&format, 1);
  if (!v->mImage) {
    return nullptr;
  }
  NS_ASSERTION(v->mImage->GetFormat() == GONK_IO_SURFACE,
               "Wrong format?");
  typedef mozilla::layers::GonkIOSurfaceImage GonkIOSurfaceImage;
  GonkIOSurfaceImage* videoImage = static_cast<GonkIOSurfaceImage*>(v->mImage.get());
  GonkIOSurfaceImage::Data data;

  data.mPicSize = gfxIntSize(aPicture.width, aPicture.height);
  data.mGraphicBuffer = aBuffer;

  videoImage->SetData(data);

  return v.forget();
}
#endif  // MOZ_WIDGET_GONK

void* MediaDecoderReader::VideoQueueMemoryFunctor::operator()(void* anObject) {
  const VideoData* v = static_cast<const VideoData*>(anObject);
  if (!v->mImage) {
    return nullptr;
  }
  NS_ASSERTION(v->mImage->GetFormat() == PLANAR_YCBCR,
               "Wrong format?");
  mozilla::layers::PlanarYCbCrImage* vi = static_cast<mozilla::layers::PlanarYCbCrImage*>(v->mImage.get());

  mResult += vi->GetDataSize();
  return nullptr;
}

MediaDecoderReader::MediaDecoderReader(AbstractMediaDecoder* aDecoder)
  : mDecoder(aDecoder)
{
  MOZ_COUNT_CTOR(MediaDecoderReader);
}

MediaDecoderReader::~MediaDecoderReader()
{
  ResetDecode();
  MOZ_COUNT_DTOR(MediaDecoderReader);
}

nsresult MediaDecoderReader::ResetDecode()
{
  nsresult res = NS_OK;

  VideoQueue().Reset();
  AudioQueue().Reset();

  return res;
}

VideoData* MediaDecoderReader::DecodeToFirstVideoData()
{
  bool eof = false;
  while (!eof && VideoQueue().GetSize() == 0) {
    {
      ReentrantMonitorAutoEnter decoderMon(mDecoder->GetReentrantMonitor());
      if (mDecoder->IsShutdown()) {
        return nullptr;
      }
    }
    bool keyframeSkip = false;
    eof = !DecodeVideoFrame(keyframeSkip, 0);
  }
  VideoData* d = nullptr;
  return (d = VideoQueue().PeekFront()) ? d : nullptr;
}

AudioData* MediaDecoderReader::DecodeToFirstAudioData()
{
  bool eof = false;
  while (!eof && AudioQueue().GetSize() == 0) {
    {
      ReentrantMonitorAutoEnter decoderMon(mDecoder->GetReentrantMonitor());
      if (mDecoder->IsShutdown()) {
        return nullptr;
      }
    }
    eof = !DecodeAudioData();
  }
  AudioData* d = nullptr;
  return (d = AudioQueue().PeekFront()) ? d : nullptr;
}

VideoData* MediaDecoderReader::FindStartTime(int64_t& aOutStartTime)
{
  NS_ASSERTION(mDecoder->OnStateMachineThread() || mDecoder->OnDecodeThread(),
               "Should be on state machine or decode thread.");

  // Extract the start times of the bitstreams in order to calculate
  // the duration.
  int64_t videoStartTime = INT64_MAX;
  int64_t audioStartTime = INT64_MAX;
  VideoData* videoData = nullptr;

  if (HasVideo()) {
    videoData = DecodeToFirstVideoData();
    if (videoData) {
      videoStartTime = videoData->mTime;
    }
  }
  if (HasAudio()) {
    AudioData* audioData = DecodeToFirstAudioData();
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

nsresult MediaDecoderReader::DecodeToTarget(int64_t aTarget)
{
  // Decode forward to the target frame. Start with video, if we have it.
  if (HasVideo()) {
    bool eof = false;
    int64_t startTime = -1;
    nsAutoPtr<VideoData> video;
    while (HasVideo() && !eof) {
      while (VideoQueue().GetSize() == 0 && !eof) {
        bool skip = false;
        eof = !DecodeVideoFrame(skip, 0);
        {
          ReentrantMonitorAutoEnter decoderMon(mDecoder->GetReentrantMonitor());
          if (mDecoder->IsShutdown()) {
            return NS_ERROR_FAILURE;
          }
        }
      }
      if (VideoQueue().GetSize() == 0) {
        // Hit end of file, we want to display the last frame of the video.
        if (video) {
          VideoQueue().PushFront(video.forget());
        }
        break;
      }
      video = VideoQueue().PeekFront();
      // If the frame end time is less than the seek target, we won't want
      // to display this frame after the seek, so discard it.
      if (video && video->mEndTime <= aTarget) {
        if (startTime == -1) {
          startTime = video->mTime;
        }
        VideoQueue().PopFront();
      } else {
        video.forget();
        break;
      }
    }
    {
      ReentrantMonitorAutoEnter decoderMon(mDecoder->GetReentrantMonitor());
      if (mDecoder->IsShutdown()) {
        return NS_ERROR_FAILURE;
      }
    }
    LOG(PR_LOG_DEBUG, ("First video frame after decode is %lld", startTime));
  }

  if (HasAudio()) {
    // Decode audio forward to the seek target.
    bool eof = false;
    while (HasAudio() && !eof) {
      while (!eof && AudioQueue().GetSize() == 0) {
        eof = !DecodeAudioData();
        {
          ReentrantMonitorAutoEnter decoderMon(mDecoder->GetReentrantMonitor());
          if (mDecoder->IsShutdown()) {
            return NS_ERROR_FAILURE;
          }
        }
      }
      const AudioData* audio = AudioQueue().PeekFront();
      if (!audio)
        break;
      CheckedInt64 startFrame = UsecsToFrames(audio->mTime, mInfo.mAudioRate);
      CheckedInt64 targetFrame = UsecsToFrames(aTarget, mInfo.mAudioRate);
      if (!startFrame.isValid() || !targetFrame.isValid()) {
        return NS_ERROR_FAILURE;
      }
      if (startFrame.value() + audio->mFrames <= targetFrame.value()) {
        // Our seek target lies after the frames in this AudioData. Pop it
        // off the queue, and keep decoding forwards.
        delete AudioQueue().PopFront();
        audio = nullptr;
        continue;
      }
      if (startFrame.value() > targetFrame.value()) {
        // The seek target doesn't lie in the audio block just after the last
        // audio frames we've seen which were before the seek target. This
        // could have been the first audio data we've seen after seek, i.e. the
        // seek terminated after the seek target in the audio stream. Just
        // abort the audio decode-to-target, the state machine will play
        // silence to cover the gap. Typically this happens in poorly muxed
        // files.
        NS_WARNING("Audio not synced after seek, maybe a poorly muxed file?");
        break;
      }

      // The seek target lies somewhere in this AudioData's frames, strip off
      // any frames which lie before the seek target, so we'll begin playback
      // exactly at the seek target.
      NS_ASSERTION(targetFrame.value() >= startFrame.value(),
                   "Target must at or be after data start.");
      NS_ASSERTION(targetFrame.value() < startFrame.value() + audio->mFrames,
                   "Data must end after target.");

      int64_t framesToPrune = targetFrame.value() - startFrame.value();
      if (framesToPrune > audio->mFrames) {
        // We've messed up somehow. Don't try to trim frames, the |frames|
        // variable below will overflow.
        NS_WARNING("Can't prune more frames that we have!");
        break;
      }
      uint32_t frames = audio->mFrames - static_cast<uint32_t>(framesToPrune);
      uint32_t channels = audio->mChannels;
      nsAutoArrayPtr<AudioDataValue> audioData(new AudioDataValue[frames * channels]);
      memcpy(audioData.get(),
             audio->mAudioData.get() + (framesToPrune * channels),
             frames * channels * sizeof(AudioDataValue));
      CheckedInt64 duration = FramesToUsecs(frames, mInfo.mAudioRate);
      if (!duration.isValid()) {
        return NS_ERROR_FAILURE;
      }
      nsAutoPtr<AudioData> data(new AudioData(audio->mOffset,
                                              aTarget,
                                              duration.value(),
                                              frames,
                                              audioData.forget(),
                                              channels));
      delete AudioQueue().PopFront();
      AudioQueue().PushFront(data.forget());
      break;
    }
  }
  return NS_OK;
}

} // namespace mozilla

