/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_RTP_RTCP_SOURCE_RTP_RECEIVER_VIDEO_H_
#define WEBRTC_MODULES_RTP_RTCP_SOURCE_RTP_RECEIVER_VIDEO_H_

#include "rtp_rtcp_defines.h"
#include "rtp_utility.h"

#include "typedefs.h"

#include "modules/remote_bitrate_estimator/include/remote_bitrate_estimator.h"
#include "modules/remote_bitrate_estimator/overuse_detector.h"
#include "modules/remote_bitrate_estimator/remote_rate_control.h"
#include "Bitrate.h"
#include "scoped_ptr.h"

namespace webrtc {
class ReceiverFEC;
class ModuleRtpRtcpImpl;
class CriticalSectionWrapper;

class RTPReceiverVideo {
 public:
  RTPReceiverVideo(const WebRtc_Word32 id,
                   RemoteBitrateEstimator* remote_bitrate,
                   ModuleRtpRtcpImpl* owner);

  virtual ~RTPReceiverVideo();

  ModuleRTPUtility::Payload* RegisterReceiveVideoPayload(
      const char payloadName[RTP_PAYLOAD_NAME_SIZE],
      const WebRtc_Word8 payloadType,
      const WebRtc_UWord32 maxRate);

  WebRtc_Word32 ParseVideoCodecSpecific(
      WebRtcRTPHeader* rtpHeader,
      const WebRtc_UWord8* payloadData,
      const WebRtc_UWord16 payloadDataLength,
      const RtpVideoCodecTypes videoType,
      const bool isRED,
      const WebRtc_UWord8* incomingRtpPacket,
      const WebRtc_UWord16 incomingRtpPacketSize,
      const WebRtc_Word64 nowMS);

  virtual WebRtc_Word32 ReceiveRecoveredPacketCallback(
      WebRtcRTPHeader* rtpHeader,
      const WebRtc_UWord8* payloadData,
      const WebRtc_UWord16 payloadDataLength);

  void SetPacketOverHead(WebRtc_UWord16 packetOverHead);

 protected:
  virtual WebRtc_Word32 CallbackOfReceivedPayloadData(
      const WebRtc_UWord8* payloadData,
      const WebRtc_UWord16 payloadSize,
      const WebRtcRTPHeader* rtpHeader) = 0;

  virtual WebRtc_UWord32 TimeStamp() const = 0;
  virtual WebRtc_UWord16 SequenceNumber() const = 0;

  virtual WebRtc_UWord32 PayloadTypeToPayload(
      const WebRtc_UWord8 payloadType,
      ModuleRTPUtility::Payload*& payload) const = 0;

  virtual bool RetransmitOfOldPacket(
      const WebRtc_UWord16 sequenceNumber,
      const WebRtc_UWord32 rtpTimeStamp) const  = 0;

  virtual WebRtc_Word8 REDPayloadType() const = 0;

  WebRtc_Word32 SetCodecType(const RtpVideoCodecTypes videoType,
                             WebRtcRTPHeader* rtpHeader) const;

  WebRtc_Word32 ParseVideoCodecSpecificSwitch(
      WebRtcRTPHeader* rtpHeader,
      const WebRtc_UWord8* payloadData,
      const WebRtc_UWord16 payloadDataLength,
      const RtpVideoCodecTypes videoType);

  WebRtc_Word32 ReceiveGenericCodec(WebRtcRTPHeader *rtpHeader,
                                    const WebRtc_UWord8* payloadData,
                                    const WebRtc_UWord16 payloadDataLength);

  WebRtc_Word32 ReceiveVp8Codec(WebRtcRTPHeader *rtpHeader,
                                const WebRtc_UWord8* payloadData,
                                const WebRtc_UWord16 payloadDataLength);

  WebRtc_Word32 BuildRTPheader(const WebRtcRTPHeader* rtpHeader,
                               WebRtc_UWord8* dataBuffer) const;

 private:
  WebRtc_Word32             _id;

  CriticalSectionWrapper*   _criticalSectionReceiverVideo;

  // FEC
  bool                      _currentFecFrameDecoded;
  ReceiverFEC*              _receiveFEC;

  // BWE
  RemoteBitrateEstimator* remote_bitrate_;
  WebRtc_UWord16            _packetOverHead;
};
} // namespace webrtc
#endif // WEBRTC_MODULES_RTP_RTCP_SOURCE_RTP_RECEIVER_VIDEO_H_
