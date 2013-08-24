/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#if !defined(nsOggCodecState_h_)
#define nsOggCodecState_h_

#include <ogg/ogg.h>
#include <theora/theoradec.h>
#ifdef MOZ_TREMOR
#include <tremor/ivorbiscodec.h>
#else
#include <vorbis/codec.h>
#endif
#ifdef MOZ_OPUS
#include <opus/opus.h>
// For MOZ_SAMPLE_TYPE_*
#include "nsBuiltinDecoderStateMachine.h"
#include "nsBuiltinDecoderReader.h"
#endif
#include <nsAutoRef.h>
#include <nsDeque.h>
#include <nsTArray.h>
#include <nsClassHashtable.h>
#include "VideoUtils.h"

#include "mozilla/StandardInteger.h"

// Uncomment the following to validate that we're predicting the number
// of Vorbis samples in each packet correctly.
#define VALIDATE_VORBIS_SAMPLE_CALCULATION
#ifdef  VALIDATE_VORBIS_SAMPLE_CALCULATION
#include <map>
#endif

// Deallocates a packet, used in nsPacketQueue below.
class OggPacketDeallocator : public nsDequeFunctor {
  virtual void* operator() (void* aPacket) {
    ogg_packet* p = static_cast<ogg_packet*>(aPacket);
    delete [] p->packet;
    delete p;
    return nsnull;
  }
};

// A queue of ogg_packets. When we read a page, we extract the page's packets
// and buffer them in the owning stream's nsOggCodecState. This is because
// if we're skipping up to the next keyframe in very large frame sized videos,
// there may be several megabytes of data between keyframes, and the
// ogg_stream_state would end up resizing its buffer every time we added a
// new 4KB page to the bitstream, which kills performance on Windows. This
// also gives us the option to timestamp packets rather than decoded
// frames/samples, reducing the amount of frames/samples we must decode to
// determine start-time at a particular offset, and gives us finer control
// over memory usage.
class nsPacketQueue : private nsDeque {
public:
  nsPacketQueue() : nsDeque(new OggPacketDeallocator()) {}
  ~nsPacketQueue() { Erase(); }
  bool IsEmpty() { return nsDeque::GetSize() == 0; }
  void Append(ogg_packet* aPacket);
  ogg_packet* PopFront() { return static_cast<ogg_packet*>(nsDeque::PopFront()); }
  ogg_packet* PeekFront() { return static_cast<ogg_packet*>(nsDeque::PeekFront()); }
  void PushFront(ogg_packet* aPacket) { nsDeque::PushFront(aPacket); }
  void PushBack(ogg_packet* aPacket) { nsDeque::PushFront(aPacket); }
  void Erase() { nsDeque::Erase(); }
};

// Encapsulates the data required for decoding an ogg bitstream and for
// converting granulepos to timestamps.
class nsOggCodecState {
public:
  // Ogg types we know about
  enum CodecType {
    TYPE_VORBIS=0,
    TYPE_THEORA=1,
    TYPE_OPUS=2,
    TYPE_SKELETON=3,
    TYPE_UNKNOWN=4
  };

  virtual ~nsOggCodecState();
  
  // Factory for creating nsCodecStates. Use instead of constructor.
  // aPage should be a beginning-of-stream page.
  static nsOggCodecState* Create(ogg_page* aPage);
  
  virtual CodecType GetType() { return TYPE_UNKNOWN; }

  // Reads a header packet. Returns true when last header has been read.
  // This function takes ownership of the packet and is responsible for
  // releasing it or queuing it for later processing.
  virtual bool DecodeHeader(ogg_packet* aPacket) {
    return (mDoneReadingHeaders = true);
  }

  // Returns the end time that a granulepos represents.
  virtual PRInt64 Time(PRInt64 granulepos) { return -1; }

  // Returns the start time that a granulepos represents.
  virtual PRInt64 StartTime(PRInt64 granulepos) { return -1; }

  // Initializes the codec state.
  virtual bool Init();

  // Returns true when this bitstream has finished reading all its
  // header packets.
  bool DoneReadingHeaders() { return mDoneReadingHeaders; }

  // Deactivates the bitstream. Only the primary video and audio bitstreams
  // should be active.
  void Deactivate() {
    mActive = false;
    mDoneReadingHeaders = true;
    Reset();
  }

  // Resets decoding state.
  virtual nsresult Reset();

  // Returns true if the nsOggCodecState thinks this packet is a header
  // packet. Note this does not verify the validity of the header packet,
  // it just guarantees that the packet is marked as a header packet (i.e.
  // it is definintely not a data packet). Do not use this to identify
  // streams, use it to filter header packets from data packets while
  // decoding.
  virtual bool IsHeader(ogg_packet* aPacket) { return false; }

  // Returns the next packet in the stream, or nsnull if there are no more
  // packets buffered in the packet queue. More packets can be buffered by
  // inserting one or more pages into the stream by calling PageIn(). The
  // caller is responsible for deleting returned packet's using
  // nsOggCodecState::ReleasePacket(). The packet will have a valid granulepos.
  ogg_packet* PacketOut();

  // Releases the memory used by a cloned packet. Every packet returned by
  // PacketOut() must be free'd using this function.
  static void ReleasePacket(ogg_packet* aPacket);

  // Extracts all packets from the page, and inserts them into the packet
  // queue. They can be extracted by calling PacketOut(). Packets from an
  // inactive stream are not buffered, i.e. this call has no effect for
  // inactive streams. Multiple pages may need to be inserted before
  // PacketOut() starts to return packets, as granulepos may need to be
  // captured.
  virtual nsresult PageIn(ogg_page* aPage);

  // Number of packets read.  
  PRUint64 mPacketCount;

  // Serial number of the bitstream.
  PRUint32 mSerial;

  // Ogg specific state.
  ogg_stream_state mState;

  // Queue of as yet undecoded packets. Packets are guaranteed to have
  // a valid granulepos.
  nsPacketQueue mPackets;

  // Is the bitstream active; whether we're decoding and playing this bitstream.
  bool mActive;
  
  // True when all headers packets have been read.
  bool mDoneReadingHeaders;

protected:
  // Constructs a new nsOggCodecState. aActive denotes whether the stream is
  // active. For streams of unsupported or unknown types, aActive should be
  // false.
  nsOggCodecState(ogg_page* aBosPage, bool aActive);

  // Deallocates all packets stored in mUnstamped, and clears the array.
  void ClearUnstamped();

  // Extracts packets out of mState until a data packet with a non -1
  // granulepos is encountered, or no more packets are readable. Header
  // packets are pushed into the packet queue immediately, and data packets
  // are buffered in mUnstamped. Once a non -1 granulepos packet is read
  // the granulepos of the packets in mUnstamped can be inferred, and they
  // can be pushed over to mPackets. Used by PageIn() implementations in
  // subclasses.
  nsresult PacketOutUntilGranulepos(bool& aFoundGranulepos);

  // Temporary buffer in which to store packets while we're reading packets
  // in order to capture granulepos.
  nsTArray<ogg_packet*> mUnstamped;
};

class nsVorbisState : public nsOggCodecState {
public:
  nsVorbisState(ogg_page* aBosPage);
  virtual ~nsVorbisState();

  CodecType GetType() { return TYPE_VORBIS; }
  bool DecodeHeader(ogg_packet* aPacket);
  PRInt64 Time(PRInt64 granulepos);
  bool Init();
  nsresult Reset();
  bool IsHeader(ogg_packet* aPacket);
  nsresult PageIn(ogg_page* aPage); 

  // Returns the end time that a granulepos represents.
  static PRInt64 Time(vorbis_info* aInfo, PRInt64 aGranulePos); 

  vorbis_info mInfo;
  vorbis_comment mComment;
  vorbis_dsp_state mDsp;
  vorbis_block mBlock;

private:

  // Reconstructs the granulepos of Vorbis packets stored in the mUnstamped
  // array.
  nsresult ReconstructVorbisGranulepos();

  // The "block size" of the previously decoded Vorbis packet, or 0 if we've
  // not yet decoded anything. This is used to calculate the number of samples
  // in a Vorbis packet, since each Vorbis packet depends on the previous
  // packet while being decoded.
  long mPrevVorbisBlockSize;

  // Granulepos (end sample) of the last decoded Vorbis packet. This is used
  // to calculate the Vorbis granulepos when we don't find a granulepos to
  // back-propagate from.
  PRInt64 mGranulepos;

#ifdef VALIDATE_VORBIS_SAMPLE_CALCULATION
  // When validating that we've correctly predicted Vorbis packets' number
  // of samples, we store each packet's predicted number of samples in this
  // map, and verify we decode the predicted number of samples.
  std::map<ogg_packet*, long> mVorbisPacketSamples;
#endif

  // Records that aPacket is predicted to have aSamples samples.
  // This function has no effect if VALIDATE_VORBIS_SAMPLE_CALCULATION
  // is not defined.
  void RecordVorbisPacketSamples(ogg_packet* aPacket, long aSamples);

  // Verifies that aPacket has had its number of samples predicted.
  // This function has no effect if VALIDATE_VORBIS_SAMPLE_CALCULATION
  // is not defined.
  void AssertHasRecordedPacketSamples(ogg_packet* aPacket);

public:
  // Asserts that the number of samples predicted for aPacket is aSamples.
  // This function has no effect if VALIDATE_VORBIS_SAMPLE_CALCULATION
  // is not defined.
  void ValidateVorbisPacketSamples(ogg_packet* aPacket, long aSamples);

};

// Returns 1 if the Theora info struct is decoding a media of Theora
// version (maj,min,sub) or later, otherwise returns 0.
int TheoraVersion(th_info* info,
                  unsigned char maj,
                  unsigned char min,
                  unsigned char sub);

class nsTheoraState : public nsOggCodecState {
public:
  nsTheoraState(ogg_page* aBosPage);
  virtual ~nsTheoraState();

  CodecType GetType() { return TYPE_THEORA; }
  bool DecodeHeader(ogg_packet* aPacket);
  PRInt64 Time(PRInt64 granulepos);
  PRInt64 StartTime(PRInt64 granulepos);
  bool Init();
  bool IsHeader(ogg_packet* aPacket);
  nsresult PageIn(ogg_page* aPage); 

  // Returns the maximum number of microseconds which a keyframe can be offset
  // from any given interframe.
  PRInt64 MaxKeyframeOffset();

  // Returns the end time that a granulepos represents.
  static PRInt64 Time(th_info* aInfo, PRInt64 aGranulePos); 

  th_info mInfo;
  th_comment mComment;
  th_setup_info *mSetup;
  th_dec_ctx* mCtx;

  float mPixelAspectRatio;

private:

  // Reconstructs the granulepos of Theora packets stored in the
  // mUnstamped array. mUnstamped must be filled with consecutive packets from
  // the stream, with the last packet having a known granulepos. Using this
  // known granulepos, and the known frame numbers, we recover the granulepos
  // of all frames in the array. This enables us to determine their timestamps.
  void ReconstructTheoraGranulepos();

};

class nsOpusState : public nsOggCodecState {
#ifdef MOZ_OPUS
public:
  nsOpusState(ogg_page* aBosPage);
  virtual ~nsOpusState();

  CodecType GetType() { return TYPE_OPUS; }
  bool DecodeHeader(ogg_packet* aPacket);
  PRInt64 Time(PRInt64 aGranulepos);
  bool Init();
  nsresult Reset();
  nsresult Reset(bool aStart);
  bool IsHeader(ogg_packet* aPacket);
  nsresult PageIn(ogg_page* aPage);

  // Returns the end time that a granulepos represents.
  static PRInt64 Time(int aPreSkip, PRInt64 aGranulepos);

  // Various fields from the Ogg Opus header.
  int mRate;        // Sample rate the decoder uses (always 48 kHz).
  PRUint32 mNominalRate; // Original sample rate of the data (informational).
  int mChannels;    // Number of channels the stream encodes.
  PRUint16 mPreSkip; // Number of samples to strip after decoder reset.
#ifdef MOZ_SAMPLE_TYPE_FLOAT32
  float mGain;      // Gain to apply to decoder output.
#else
  PRInt32 mGain_Q16; // Gain to apply to the decoder output.
#endif
  int mChannelMapping; // Channel mapping family.
  int mStreams;     // Number of packed streams in each packet.

  OpusDecoder *mDecoder;
  int mSkip;        // Number of samples left to trim before playback.
  // Granule position (end sample) of the last decoded Opus packet. This is
  // used to calculate the amount we should trim from the last packet.
  PRInt64 mPrevPacketGranulepos;

private:

  // Reconstructs the granulepos of Opus packets stored in the
  // mUnstamped array. mUnstamped must be filled with consecutive packets from
  // the stream, with the last packet having a known granulepos. Using this
  // known granulepos, and the known frame numbers, we recover the granulepos
  // of all frames in the array. This enables us to determine their timestamps.
  bool ReconstructOpusGranulepos();

  // Granule position (end sample) of the last decoded Opus page. This is
  // used to calculate the Opus per-packet granule positions on the last page,
  // where we may need to trim some samples from the end.
  PRInt64 mPrevPageGranulepos;

#endif /* MOZ_OPUS */
};

// Constructs a 32bit version number out of two 16 bit major,minor
// version numbers.
#define SKELETON_VERSION(major, minor) (((major)<<16)|(minor))

class nsSkeletonState : public nsOggCodecState {
public:
  nsSkeletonState(ogg_page* aBosPage);
  ~nsSkeletonState();
  CodecType GetType() { return TYPE_SKELETON; }
  bool DecodeHeader(ogg_packet* aPacket);
  PRInt64 Time(PRInt64 granulepos) { return -1; }
  bool Init() { return true; }
  bool IsHeader(ogg_packet* aPacket) { return true; }

  // Return true if the given time (in milliseconds) is within
  // the presentation time defined in the skeleton track.
  bool IsPresentable(PRInt64 aTime) { return aTime >= mPresentationTime; }

  // Stores the offset of the page on which a keyframe starts,
  // and its presentation time.
  class nsKeyPoint {
  public:
    nsKeyPoint()
      : mOffset(INT64_MAX),
        mTime(INT64_MAX) {}

    nsKeyPoint(PRInt64 aOffset, PRInt64 aTime)
      : mOffset(aOffset),
        mTime(aTime) {}

    // Offset from start of segment/link-in-the-chain in bytes.
    PRInt64 mOffset;

    // Presentation time in usecs.
    PRInt64 mTime;

    bool IsNull() {
      return mOffset == INT64_MAX &&
             mTime == INT64_MAX;
    }
  };

  // Stores a keyframe's byte-offset, presentation time and the serialno
  // of the stream it belongs to.
  class nsSeekTarget {
  public:
    nsSeekTarget() : mSerial(0) {}
    nsKeyPoint mKeyPoint;
    PRUint32 mSerial;
    bool IsNull() {
      return mKeyPoint.IsNull() &&
             mSerial == 0;
    }
  };

  // Determines from the seek index the keyframe which you must seek back to
  // in order to get all keyframes required to render all streams with
  // serialnos in aTracks, at time aTarget.
  nsresult IndexedSeekTarget(PRInt64 aTarget,
                             nsTArray<PRUint32>& aTracks,
                             nsSeekTarget& aResult);

  bool HasIndex() const {
    return mIndex.IsInitialized() && mIndex.Count() > 0;
  }

  // Returns the duration of the active tracks in the media, if we have
  // an index. aTracks must be filled with the serialnos of the active tracks.
  // The duration is calculated as the greatest end time of all active tracks,
  // minus the smalled start time of all the active tracks.
  nsresult GetDuration(const nsTArray<PRUint32>& aTracks, PRInt64& aDuration);

private:

  // Decodes an index packet. Returns false on failure.
  bool DecodeIndex(ogg_packet* aPacket);

  // Gets the keypoint you must seek to in order to get the keyframe required
  // to render the stream at time aTarget on stream with serial aSerialno.
  nsresult IndexedSeekTargetForTrack(PRUint32 aSerialno,
                                     PRInt64 aTarget,
                                     nsKeyPoint& aResult);

  // Version of the decoded skeleton track, as per the SKELETON_VERSION macro.
  PRUint32 mVersion;

  // Presentation time of the resource in milliseconds
  PRInt64 mPresentationTime;

  // Length of the resource in bytes.
  PRInt64 mLength;

  // Stores the keyframe index and duration information for a particular
  // stream.
  class nsKeyFrameIndex {
  public:

    nsKeyFrameIndex(PRInt64 aStartTime, PRInt64 aEndTime) 
      : mStartTime(aStartTime),
        mEndTime(aEndTime)
    {
      MOZ_COUNT_CTOR(nsKeyFrameIndex);
    }

    ~nsKeyFrameIndex() {
      MOZ_COUNT_DTOR(nsKeyFrameIndex);
    }

    void Add(PRInt64 aOffset, PRInt64 aTimeMs) {
      mKeyPoints.AppendElement(nsKeyPoint(aOffset, aTimeMs));
    }

    const nsKeyPoint& Get(PRUint32 aIndex) const {
      return mKeyPoints[aIndex];
    }

    PRUint32 Length() const {
      return mKeyPoints.Length();
    }

    // Presentation time of the first sample in this stream in usecs.
    const PRInt64 mStartTime;

    // End time of the last sample in this stream in usecs.
    const PRInt64 mEndTime;

  private:
    nsTArray<nsKeyPoint> mKeyPoints;
  };

  // Maps Ogg serialnos to the index-keypoint list.
  nsClassHashtable<nsUint32HashKey, nsKeyFrameIndex> mIndex;
};

// This allows the use of nsAutoRefs for an ogg_packet that properly free the
// contents of the packet.
template <>
class nsAutoRefTraits<ogg_packet> : public nsPointerRefTraits<ogg_packet>
{
public:
  static void Release(ogg_packet* aPacket) {
    nsOggCodecState::ReleasePacket(aPacket);
  }
};

#endif
