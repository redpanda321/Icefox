/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#if !defined(MediaDecoderReader_h_)
#define MediaDecoderReader_h_

#include <nsDeque.h>
#include "nsSize.h"
#include "mozilla/ReentrantMonitor.h"
#include "MediaStreamGraph.h"
#include "SharedBuffer.h"
#include "ImageLayers.h"
#include "AudioSampleFormat.h"
#include "MediaResource.h"
#include "nsHTMLMediaElement.h"

namespace mozilla {

class AbstractMediaDecoder;

// Stores info relevant to presenting media frames.
class VideoInfo {
public:
  VideoInfo()
    : mAudioRate(44100),
      mAudioChannels(2),
      mDisplay(0,0),
      mStereoMode(STEREO_MODE_MONO),
      mHasAudio(false),
      mHasVideo(false)
  {}

  // Returns true if it's safe to use aPicture as the picture to be
  // extracted inside a frame of size aFrame, and scaled up to and displayed
  // at a size of aDisplay. You should validate the frame, picture, and
  // display regions before using them to display video frames.
  static bool ValidateVideoRegion(const nsIntSize& aFrame,
                                  const nsIntRect& aPicture,
                                  const nsIntSize& aDisplay);

  // Sample rate.
  uint32_t mAudioRate;

  // Number of audio channels.
  uint32_t mAudioChannels;

  // Size in pixels at which the video is rendered. This is after it has
  // been scaled by its aspect ratio.
  nsIntSize mDisplay;

  // Indicates the frame layout for single track stereo videos.
  StereoMode mStereoMode;

  // True if we have an active audio bitstream.
  bool mHasAudio;

  // True if we have an active video bitstream.
  bool mHasVideo;
};

// Holds chunk a decoded audio frames.
class AudioData {
public:

  AudioData(int64_t aOffset,
            int64_t aTime,
            int64_t aDuration,
            uint32_t aFrames,
            AudioDataValue* aData,
            uint32_t aChannels)
  : mOffset(aOffset),
    mTime(aTime),
    mDuration(aDuration),
    mFrames(aFrames),
    mChannels(aChannels),
    mAudioData(aData)
  {
    MOZ_COUNT_CTOR(AudioData);
  }

  ~AudioData()
  {
    MOZ_COUNT_DTOR(AudioData);
  }

  // If mAudioBuffer is null, creates it from mAudioData.
  void EnsureAudioBuffer();

  int64_t GetEnd() { return mTime + mDuration; }

  // Approximate byte offset of the end of the page on which this chunk
  // ends.
  const int64_t mOffset;

  int64_t mTime; // Start time of data in usecs.
  const int64_t mDuration; // In usecs.
  const uint32_t mFrames;
  const uint32_t mChannels;
  // At least one of mAudioBuffer/mAudioData must be non-null.
  // mChannels channels, each with mFrames frames
  nsRefPtr<SharedBuffer> mAudioBuffer;
  // mFrames frames, each with mChannels values
  nsAutoArrayPtr<AudioDataValue> mAudioData;
};

namespace layers {
class GraphicBufferLocked;
}

// Holds a decoded video frame, in YCbCr format. These are queued in the reader.
class VideoData {
public:
  typedef layers::ImageContainer ImageContainer;
  typedef layers::Image Image;

  // YCbCr data obtained from decoding the video. The index's are:
  //   0 = Y
  //   1 = Cb
  //   2 = Cr
  struct YCbCrBuffer {
    struct Plane {
      uint8_t* mData;
      uint32_t mWidth;
      uint32_t mHeight;
      uint32_t mStride;
      uint32_t mOffset;
      uint32_t mSkip;
    };

    Plane mPlanes[3];
  };

  // Constructs a VideoData object. Makes a copy of YCbCr data in aBuffer.
  // aTimecode is a codec specific number representing the timestamp of
  // the frame of video data. Returns nullptr if an error occurs. This may
  // indicate that memory couldn't be allocated to create the VideoData
  // object, or it may indicate some problem with the input data (e.g.
  // negative stride).
  static VideoData* Create(VideoInfo& aInfo,
                           ImageContainer* aContainer,
                           int64_t aOffset,
                           int64_t aTime,
                           int64_t aEndTime,
                           const YCbCrBuffer &aBuffer,
                           bool aKeyframe,
                           int64_t aTimecode,
                           nsIntRect aPicture);

  static VideoData* Create(VideoInfo& aInfo,
                           ImageContainer* aContainer,
                           int64_t aOffset,
                           int64_t aTime,
                           int64_t aEndTime,
                           layers::GraphicBufferLocked *aBuffer,
                           bool aKeyframe,
                           int64_t aTimecode,
                           nsIntRect aPicture);

  static VideoData* CreateFromImage(VideoInfo& aInfo,
                                    ImageContainer* aContainer,
                                    int64_t aOffset,
                                    int64_t aTime,
                                    int64_t aEndTime,
                                    const nsRefPtr<Image>& aImage,
                                    bool aKeyframe,
                                    int64_t aTimecode,
                                    nsIntRect aPicture);

  // Constructs a duplicate VideoData object. This intrinsically tells the
  // player that it does not need to update the displayed frame when this
  // frame is played; this frame is identical to the previous.
  static VideoData* CreateDuplicate(int64_t aOffset,
                                    int64_t aTime,
                                    int64_t aEndTime,
                                    int64_t aTimecode)
  {
    return new VideoData(aOffset, aTime, aEndTime, aTimecode);
  }

  ~VideoData();

  int64_t GetEnd() { return mEndTime; }

  // Dimensions at which to display the video frame. The picture region
  // will be scaled to this size. This is should be the picture region's
  // dimensions scaled with respect to its aspect ratio.
  nsIntSize mDisplay;

  // Approximate byte offset of the end of the frame in the media.
  int64_t mOffset;

  // Start time of frame in microseconds.
  int64_t mTime;

  // End time of frame in microseconds.
  int64_t mEndTime;

  // Codec specific internal time code. For Ogg based codecs this is the
  // granulepos.
  int64_t mTimecode;

  // This frame's image.
  nsRefPtr<Image> mImage;

  // When true, denotes that this frame is identical to the frame that
  // came before; it's a duplicate. mBuffer will be empty.
  bool mDuplicate;
  bool mKeyframe;

public:
  VideoData(int64_t aOffset, int64_t aTime, int64_t aEndTime, int64_t aTimecode);

  VideoData(int64_t aOffset,
            int64_t aTime,
            int64_t aEndTime,
            bool aKeyframe,
            int64_t aTimecode,
            nsIntSize aDisplay);

};

// Thread and type safe wrapper around nsDeque.
template <class T>
class MediaQueueDeallocator : public nsDequeFunctor {
  virtual void* operator() (void* anObject) {
    delete static_cast<T*>(anObject);
    return nullptr;
  }
};

template <class T> class MediaQueue : private nsDeque {
 public:

   MediaQueue()
     : nsDeque(new MediaQueueDeallocator<T>()),
       mReentrantMonitor("mediaqueue"),
       mEndOfStream(false)
   {}

  ~MediaQueue() {
    Reset();
  }

  inline int32_t GetSize() {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    return nsDeque::GetSize();
  }

  inline void Push(T* aItem) {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    nsDeque::Push(aItem);
  }

  inline void PushFront(T* aItem) {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    nsDeque::PushFront(aItem);
  }

  inline T* Pop() {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    return static_cast<T*>(nsDeque::Pop());
  }

  inline T* PopFront() {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    return static_cast<T*>(nsDeque::PopFront());
  }

  inline T* Peek() {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    return static_cast<T*>(nsDeque::Peek());
  }

  inline T* PeekFront() {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    return static_cast<T*>(nsDeque::PeekFront());
  }

  inline void Empty() {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    nsDeque::Empty();
  }

  inline void Erase() {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    nsDeque::Erase();
  }

  void Reset() {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    while (GetSize() > 0) {
      T* x = PopFront();
      delete x;
    }
    mEndOfStream = false;
  }

  bool AtEndOfStream() {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    return GetSize() == 0 && mEndOfStream;
  }

  // Returns true if the media queue has had its last item added to it.
  // This happens when the media stream has been completely decoded. Note this
  // does not mean that the corresponding stream has finished playback.
  bool IsFinished() {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    return mEndOfStream;
  }

  // Informs the media queue that it won't be receiving any more items.
  void Finish() {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    mEndOfStream = true;
  }

  // Returns the approximate number of microseconds of items in the queue.
  int64_t Duration() {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    if (GetSize() < 2) {
      return 0;
    }
    T* last = Peek();
    T* first = PeekFront();
    return last->mTime - first->mTime;
  }

  void LockedForEach(nsDequeFunctor& aFunctor) const {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    ForEach(aFunctor);
  }

  // Extracts elements from the queue into aResult, in order.
  // Elements whose start time is before aTime are ignored.
  void GetElementsAfter(int64_t aTime, nsTArray<T*>* aResult) {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    if (!GetSize())
      return;
    int32_t i;
    for (i = GetSize() - 1; i > 0; --i) {
      T* v = static_cast<T*>(ObjectAt(i));
      if (v->GetEnd() < aTime)
        break;
    }
    // Elements less than i have a end time before aTime. It's also possible
    // that the element at i has a end time before aTime, but that's OK.
    for (; i < GetSize(); ++i) {
      aResult->AppendElement(static_cast<T*>(ObjectAt(i)));
    }
  }

private:
  mutable ReentrantMonitor mReentrantMonitor;

  // True when we've decoded the last frame of data in the
  // bitstream for which we're queueing frame data.
  bool mEndOfStream;
};

// Encapsulates the decoding and reading of media data. Reading can only be
// done on the decode thread. Never hold the decoder monitor when
// calling into this class. Unless otherwise specified, methods and fields of
// this class can only be accessed on the decode thread.
class MediaDecoderReader {
public:
  MediaDecoderReader(AbstractMediaDecoder* aDecoder);
  virtual ~MediaDecoderReader();

  NS_INLINE_DECL_REFCOUNTING(MediaDecoderReader)

  // Initializes the reader, returns NS_OK on success, or NS_ERROR_FAILURE
  // on failure.
  virtual nsresult Init(MediaDecoderReader* aCloneDonor) = 0;

  // Resets all state related to decoding, emptying all buffers etc.
  virtual nsresult ResetDecode();

  // Decodes an unspecified amount of audio data, enqueuing the audio data
  // in mAudioQueue. Returns true when there's more audio to decode,
  // false if the audio is finished, end of file has been reached,
  // or an un-recoverable read error has occured.
  virtual bool DecodeAudioData() = 0;

  // Steps to carry out at the start of the |DecodeLoop|.
  virtual void PrepareToDecode() { }

  // Reads and decodes one video frame. Packets with a timestamp less
  // than aTimeThreshold will be decoded (unless they're not keyframes
  // and aKeyframeSkip is true), but will not be added to the queue.
  virtual bool DecodeVideoFrame(bool &aKeyframeSkip,
                                int64_t aTimeThreshold) = 0;

  virtual bool HasAudio() = 0;
  virtual bool HasVideo() = 0;

  // Read header data for all bitstreams in the file. Fills aInfo with
  // the data required to present the media, and optionally fills *aTags
  // with tag metadata from the file.
  // Returns NS_OK on success, or NS_ERROR_FAILURE on failure.
  virtual nsresult ReadMetadata(VideoInfo* aInfo,
                                MetadataTags** aTags) = 0;

  // Stores the presentation time of the first frame we'd be able to play if
  // we started playback at the current position. Returns the first video
  // frame, if we have video.
  virtual VideoData* FindStartTime(int64_t& aOutStartTime);

  // Moves the decode head to aTime microseconds. aStartTime and aEndTime
  // denote the start and end times of the media in usecs, and aCurrentTime
  // is the current playback position in microseconds.
  virtual nsresult Seek(int64_t aTime,
                        int64_t aStartTime,
                        int64_t aEndTime,
                        int64_t aCurrentTime) = 0;
  
  // Called when the decode thread is started, before calling any other
  // decode, read metadata, or seek functions. Do any thread local setup
  // in this function.
  virtual void OnDecodeThreadStart() {}
  
  // Called when the decode thread is about to finish, after all calls to
  // any other decode, read metadata, or seek functions. Any backend specific
  // thread local tear down must be done in this function. Note that another
  // decode thread could start up and run in future.
  virtual void OnDecodeThreadFinish() {}

protected:
  // Queue of audio frames. This queue is threadsafe, and is accessed from
  // the audio, decoder, state machine, and main threads.
  MediaQueue<AudioData> mAudioQueue;

  // Queue of video frames. This queue is threadsafe, and is accessed from
  // the decoder, state machine, and main threads.
  MediaQueue<VideoData> mVideoQueue;

public:
  // Populates aBuffered with the time ranges which are buffered. aStartTime
  // must be the presentation time of the first frame in the media, e.g.
  // the media time corresponding to playback time/position 0. This function
  // should only be called on the main thread.
  virtual nsresult GetBuffered(nsTimeRanges* aBuffered,
                               int64_t aStartTime) = 0;

  class VideoQueueMemoryFunctor : public nsDequeFunctor {
  public:
    VideoQueueMemoryFunctor() : mResult(0) {}

    virtual void* operator()(void* anObject);

    int64_t mResult;
  };

  virtual int64_t VideoQueueMemoryInUse() {
    VideoQueueMemoryFunctor functor;
    mVideoQueue.LockedForEach(functor);
    return functor.mResult;
  }

  class AudioQueueMemoryFunctor : public nsDequeFunctor {
  public:
    AudioQueueMemoryFunctor() : mResult(0) {}

    virtual void* operator()(void* anObject) {
      const AudioData* audioData = static_cast<const AudioData*>(anObject);
      mResult += audioData->mFrames * audioData->mChannels * sizeof(AudioDataValue);
      return nullptr;
    }

    int64_t mResult;
  };

  virtual int64_t AudioQueueMemoryInUse() {
    AudioQueueMemoryFunctor functor;
    mAudioQueue.LockedForEach(functor);
    return functor.mResult;
  }

  // Only used by WebMReader for now, so stub here rather than in every
  // reader than inherits from MediaDecoderReader.
  virtual void NotifyDataArrived(const char* aBuffer, uint32_t aLength, int64_t aOffset) {}

  virtual MediaQueue<AudioData>& AudioQueue() { return mAudioQueue; }
  virtual MediaQueue<VideoData>& VideoQueue() { return mVideoQueue; }

  // Returns a pointer to the decoder.
  AbstractMediaDecoder* GetDecoder() {
    return mDecoder;
  }

  AudioData* DecodeToFirstAudioData();
  VideoData* DecodeToFirstVideoData();

protected:
  // Pumps the decode until we reach frames required to play at time aTarget
  // (usecs).
  nsresult DecodeToTarget(int64_t aTarget);

  // Reference to the owning decoder object.
  AbstractMediaDecoder* mDecoder;

  // Stores presentation info required for playback.
  VideoInfo mInfo;
};

} // namespace mozilla

#endif
