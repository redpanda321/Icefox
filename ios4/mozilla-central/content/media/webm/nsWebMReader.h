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
 *  Chris Pearce <chris@pearce.org.nz>
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
#if !defined(nsWebMReader_h_)
#define nsWebMReader_h_

#include "nsDeque.h"
#include "nsBuiltinDecoderReader.h"
#include "nestegg/nestegg.h"
#include "vpx/vpx_decoder.h"
#include "vpx/vp8dx.h"
#include "vorbis/codec.h"

class nsMediaDecoder;

// Thread and type safe wrapper around nsDeque.
class PacketQueueDeallocator : public nsDequeFunctor {
  virtual void* operator() (void* anObject) {
    nestegg_free_packet(static_cast<nestegg_packet*>(anObject));
    return nsnull;
  }
};

// Typesafe queue for holding nestegg packets. It has
// ownership of the items in the queue and will free them
// when destroyed.
class PacketQueue : private nsDeque {
 public:
   PacketQueue()
     : nsDeque(new PacketQueueDeallocator())
   {}
  
  ~PacketQueue() {
    Reset();
  }

  inline PRInt32 GetSize() { 
    return nsDeque::GetSize();
  }
  
  inline void Push(nestegg_packet* aItem) {
    NS_ASSERTION(aItem, "NULL pushed to PacketQueue");
    nsDeque::Push(aItem);
  }
  
  inline void PushFront(nestegg_packet* aItem) {
    NS_ASSERTION(aItem, "NULL pushed to PacketQueue");
    nsDeque::PushFront(aItem);
  }

  inline nestegg_packet* PopFront() {
    return static_cast<nestegg_packet*>(nsDeque::PopFront());
  }
  
  void Reset() {
    while (GetSize() > 0) {
      nestegg_free_packet(PopFront());
    }
  }
};


class nsWebMReader : public nsBuiltinDecoderReader
{
public:
  nsWebMReader(nsBuiltinDecoder* aDecoder);
  ~nsWebMReader();

  virtual nsresult Init();
  virtual nsresult ResetDecode();
  virtual PRBool DecodeAudioData();

  // If the Theora granulepos has not been captured, it may read several packets
  // until one with a granulepos has been captured, to ensure that all packets
  // read have valid time info.  
  virtual PRBool DecodeVideoFrame(PRBool &aKeyframeSkip,
                                  PRInt64 aTimeThreshold);

  virtual PRBool HasAudio()
  {
    mozilla::MonitorAutoEnter mon(mMonitor);
    return mHasAudio;
  }

  virtual PRBool HasVideo()
  {
    mozilla::MonitorAutoEnter mon(mMonitor);
    return mHasVideo;
  }

  virtual nsresult ReadMetadata();
  virtual nsresult Seek(PRInt64 aTime, PRInt64 aStartTime, PRInt64 aEndTime, PRInt64 aCurrentTime);
  virtual nsresult GetBuffered(nsTimeRanges* aBuffered, PRInt64 aStartTime);

private:
  // Value passed to NextPacket to determine if we are reading a video or an
  // audio packet.
  enum TrackType {
    VIDEO = 0,
    AUDIO = 1
  };

  // Read a packet from the nestegg file. Returns NULL if all packets for
  // the particular track have been read. Pass VIDEO or AUDIO to indicate the
  // type of the packet we want to read.
  nestegg_packet* NextPacket(TrackType aTrackType);

  // Returns an initialized ogg packet with data obtained from the WebM container.
  ogg_packet InitOggPacket(unsigned char* aData,
                           size_t aLength,
                           PRBool aBOS,
                           PRBool aEOS,
                           PRInt64 aGranulepos);
                     
  // Decode a nestegg packet of audio data. Push the audio data on the
  // audio queue. Returns PR_TRUE when there's more audio to decode,
  // PR_FALSE if the audio is finished, end of file has been reached,
  // or an un-recoverable read error has occured. The reader's monitor
  // must be held during this call. This function will free the packet
  // so the caller must not use the packet after calling.
  PRBool DecodeAudioPacket(nestegg_packet* aPacket);

  // Release context and set to null. Called when an error occurs during
  // reading metadata or destruction of the reader itself.
  void Cleanup();

private:
  // libnestegg context for webm container. Access on state machine thread
  // or decoder thread only.
  nestegg* mContext;

  // VP8 decoder state
  vpx_codec_ctx_t  mVP8;

  // Vorbis decoder state
  vorbis_info mVorbisInfo;
  vorbis_comment mVorbisComment;
  vorbis_dsp_state mVorbisDsp;
  vorbis_block mVorbisBlock;
  PRUint32 mPacketCount;
  PRUint32 mChannels;

  // Queue of video and audio packets that have been read but not decoded. These
  // must only be accessed from the state machine thread.
  PacketQueue mVideoPackets;
  PacketQueue mAudioPackets;

  // Index of video and audio track to play
  PRUint32 mVideoTrack;
  PRUint32 mAudioTrack;

  // Time in ms of the start of the first audio sample we've decoded.
  PRInt64 mAudioStartMs;

  // Number of samples we've decoded since decoding began at mAudioStartMs.
  PRUint64 mAudioSamples;

  // Booleans to indicate if we have audio and/or video data
  PRPackedBool mHasVideo;
  PRPackedBool mHasAudio;
};

#endif
