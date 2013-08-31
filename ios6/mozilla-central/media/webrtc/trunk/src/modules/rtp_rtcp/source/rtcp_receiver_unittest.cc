/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */


/*
 * This file includes unit tests for the RTCPReceiver.
 */
#include <gmock/gmock.h>
#include <gtest/gtest.h>

// Note: This file has no directory. Lint warning must be ignored.
#include "common_types.h"
#include "modules/remote_bitrate_estimator/include/remote_bitrate_estimator.h"
#include "modules/remote_bitrate_estimator/include/mock/mock_remote_bitrate_observer.h"
#include "modules/rtp_rtcp/source/rtp_utility.h"
#include "modules/rtp_rtcp/source/rtcp_sender.h"
#include "modules/rtp_rtcp/source/rtcp_receiver.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_impl.h"

namespace webrtc {

namespace {  // Anonymous namespace; hide utility functions and classes.

// A very simple packet builder class for building RTCP packets.
class PacketBuilder {
 public:
  static const int kMaxPacketSize = 1024;

  PacketBuilder()
      : pos_(0),
        pos_of_len_(0) {
  }


  void Add8(WebRtc_UWord8 byte) {
    EXPECT_LT(pos_, kMaxPacketSize - 1);
    buffer_[pos_] = byte;
    ++ pos_;
  }

  void Add16(WebRtc_UWord16 word) {
    Add8(word >> 8);
    Add8(word & 0xFF);
  }

  void Add32(WebRtc_UWord32 word) {
    Add8(word >> 24);
    Add8((word >> 16) & 0xFF);
    Add8((word >> 8) & 0xFF);
    Add8(word & 0xFF);
  }

  void Add64(WebRtc_UWord32 upper_half, WebRtc_UWord32 lower_half) {
    Add32(upper_half);
    Add32(lower_half);
  }

  // Set the 5-bit value in the 1st byte of the header
  // and the payload type. Set aside room for the length field,
  // and make provision for backpatching it.
  // Note: No way to set the padding bit.
  void AddRtcpHeader(int payload, int format_or_count) {
    PatchLengthField();
    Add8(0x80 | (format_or_count & 0x1F));
    Add8(payload);
    pos_of_len_ = pos_;
    Add16(0xDEAD);  // Initialize length to "clearly illegal".
  }

  void AddTmmbrBandwidth(int mantissa, int exponent, int overhead) {
    // 6 bits exponent, 17 bits mantissa, 9 bits overhead.
    WebRtc_UWord32 word = 0;
    word |= (exponent << 26);
    word |= ((mantissa & 0x1FFFF) << 9);
    word |= (overhead & 0x1FF);
    Add32(word);
  }

  void AddSrPacket(WebRtc_UWord32 sender_ssrc) {
    AddRtcpHeader(200, 0);
    Add32(sender_ssrc);
    Add64(0x10203, 0x4050607);  // NTP timestamp
    Add32(0x10203);  // RTP timestamp
    Add32(0);  // Sender's packet count
    Add32(0);  // Sender's octet count
  }

  const WebRtc_UWord8* packet() {
    PatchLengthField();
    return buffer_;
  }

  unsigned int length() {
    return pos_;
  }
 private:
  void PatchLengthField() {
    if (pos_of_len_ > 0) {
      // Backpatch the packet length. The client must have taken
      // care of proper padding to 32-bit words.
      int this_packet_length = (pos_ - pos_of_len_ - 2);
      ASSERT_EQ(0, this_packet_length % 4)
          << "Packets must be a multiple of 32 bits long"
          << " pos " << pos_ << " pos_of_len " << pos_of_len_;
      buffer_[pos_of_len_] = this_packet_length >> 10;
      buffer_[pos_of_len_+1] = (this_packet_length >> 2) & 0xFF;
      pos_of_len_ = 0;
    }
  }

  int pos_;
  // Where the length field of the current packet is.
  // Note that 0 is not a legal value, so is used for "uninitialized".
  int pos_of_len_;
  WebRtc_UWord8 buffer_[kMaxPacketSize];
};

// Fake system clock, controllable to the millisecond.
// The Epoch for this clock is Jan 1, 1970, as evidenced
// by the NTP calculation.
class FakeSystemClock : public RtpRtcpClock {
 public:
  FakeSystemClock()
      : time_in_ms_(1335900000) {}  // A nonzero, but fake, value.

  virtual WebRtc_Word64 GetTimeInMS() {
    return time_in_ms_;
  }

  virtual void CurrentNTP(WebRtc_UWord32& secs,
                          WebRtc_UWord32& frac) {
    secs = (time_in_ms_ / 1000) + ModuleRTPUtility::NTP_JAN_1970;
    // NTP_FRAC is 2^32 - number of ticks per second in the NTP fraction.
    frac = (WebRtc_UWord32)((time_in_ms_ % 1000)
                            * ModuleRTPUtility::NTP_FRAC / 1000);
  }

  void AdvanceClock(int ms_to_advance) {
    time_in_ms_ += ms_to_advance;
  }
 private:
  WebRtc_Word64 time_in_ms_;
};


// This test transport verifies that no functions get called.
class TestTransport : public Transport,
                      public RtpData {
 public:
  explicit TestTransport()
      : rtcp_receiver_(NULL) {
  }
  void SetRTCPReceiver(RTCPReceiver* rtcp_receiver) {
    rtcp_receiver_ = rtcp_receiver;
  }
  virtual int SendPacket(int /*ch*/, const void* /*data*/, int /*len*/) {
    ADD_FAILURE();  // FAIL() gives a compile error.
    return -1;
  }

  // Injects an RTCP packet into the receiver.
  virtual int SendRTCPPacket(int /* ch */, const void *packet, int packet_len) {
    ADD_FAILURE();
    return 0;
  }

  virtual int OnReceivedPayloadData(const WebRtc_UWord8* payloadData,
                                    const WebRtc_UWord16 payloadSize,
                                    const WebRtcRTPHeader* rtpHeader) {
    ADD_FAILURE();
    return 0;
  }
  RTCPReceiver* rtcp_receiver_;
};

class RtcpReceiverTest : public ::testing::Test {
 protected:
  RtcpReceiverTest()
      : over_use_detector_options_(),
        remote_bitrate_observer_(),
        remote_bitrate_estimator_(&remote_bitrate_observer_,
                                  over_use_detector_options_) {
    // system_clock_ = ModuleRTPUtility::GetSystemClock();
    system_clock_ = new FakeSystemClock();
    test_transport_ = new TestTransport();

    RtpRtcp::Configuration configuration;
    configuration.id = 0;
    configuration.audio = false;
    configuration.clock = system_clock_;
    configuration.outgoing_transport = test_transport_;
    configuration.remote_bitrate_estimator = &remote_bitrate_estimator_;
    rtp_rtcp_impl_ = new ModuleRtpRtcpImpl(configuration);
    rtcp_receiver_ = new RTCPReceiver(0, system_clock_, rtp_rtcp_impl_);
    test_transport_->SetRTCPReceiver(rtcp_receiver_);
  }
  ~RtcpReceiverTest() {
    delete rtcp_receiver_;
    delete rtp_rtcp_impl_;
    delete test_transport_;
    delete system_clock_;
  }

  // Injects an RTCP packet into the receiver.
  // Returns 0 for OK, non-0 for failure.
  int InjectRtcpPacket(const WebRtc_UWord8* packet,
                        WebRtc_UWord16 packet_len) {
    RTCPUtility::RTCPParserV2 rtcpParser(packet,
                                         packet_len,
                                         true);  // Allow non-compound RTCP

    RTCPHelp::RTCPPacketInformation rtcpPacketInformation;
    int result = rtcp_receiver_->IncomingRTCPPacket(rtcpPacketInformation,
                                                    &rtcpParser);
    rtcp_packet_info_ = rtcpPacketInformation;
    return result;
  }

  OverUseDetectorOptions over_use_detector_options_;
  FakeSystemClock* system_clock_;
  ModuleRtpRtcpImpl* rtp_rtcp_impl_;
  RTCPReceiver* rtcp_receiver_;
  TestTransport* test_transport_;
  RTCPHelp::RTCPPacketInformation rtcp_packet_info_;
  MockRemoteBitrateObserver remote_bitrate_observer_;
  RemoteBitrateEstimator remote_bitrate_estimator_;
};


TEST_F(RtcpReceiverTest, BrokenPacketIsIgnored) {
  const WebRtc_UWord8 bad_packet[] = {0, 0, 0, 0};
  EXPECT_EQ(0, InjectRtcpPacket(bad_packet, sizeof(bad_packet)));
  EXPECT_EQ(0U, rtcp_packet_info_.rtcpPacketTypeFlags);
}

TEST_F(RtcpReceiverTest, InjectSrPacket) {
  const WebRtc_UWord32 kSenderSsrc = 0x10203;
  PacketBuilder p;
  p.AddSrPacket(kSenderSsrc);
  EXPECT_EQ(0, InjectRtcpPacket(p.packet(), p.length()));
  // The parser will note the remote SSRC on a SR from other than his
  // expected peer, but will not flag that he's gotten a packet.
  EXPECT_EQ(kSenderSsrc, rtcp_packet_info_.remoteSSRC);
  EXPECT_EQ(0U,
            kRtcpSr & rtcp_packet_info_.rtcpPacketTypeFlags);
}

TEST_F(RtcpReceiverTest, TmmbrReceivedWithNoIncomingPacket) {
  // This call is expected to fail because no data has arrived.
  EXPECT_EQ(-1, rtcp_receiver_->TMMBRReceived(0, 0, NULL));
}

TEST_F(RtcpReceiverTest, TmmbrPacketAccepted) {
  const WebRtc_UWord32 kMediaFlowSsrc = 0x2040608;
  const WebRtc_UWord32 kSenderSsrc = 0x10203;
  const WebRtc_UWord32 kMediaRecipientSsrc = 0x101;
  rtcp_receiver_->SetSSRC(kMediaFlowSsrc);  // Matches "media source" above.

  PacketBuilder p;
  p.AddSrPacket(kSenderSsrc);
  // TMMBR packet.
  p.AddRtcpHeader(205, 3);
  p.Add32(kSenderSsrc);
  p.Add32(kMediaRecipientSsrc);
  p.Add32(kMediaFlowSsrc);
  p.AddTmmbrBandwidth(30000, 0, 0);  // 30 Kbits/sec bandwidth, no overhead.

  EXPECT_EQ(0, InjectRtcpPacket(p.packet(), p.length()));
  EXPECT_EQ(1, rtcp_receiver_->TMMBRReceived(0, 0, NULL));
  TMMBRSet candidate_set;
  candidate_set.VerifyAndAllocateSet(1);
  EXPECT_EQ(1, rtcp_receiver_->TMMBRReceived(1, 0, &candidate_set));
  EXPECT_LT(0U, candidate_set.Tmmbr(0));
  EXPECT_EQ(kMediaRecipientSsrc, candidate_set.Ssrc(0));
}

TEST_F(RtcpReceiverTest, TmmbrPacketNotForUsIgnored) {
  const WebRtc_UWord32 kMediaFlowSsrc = 0x2040608;
  const WebRtc_UWord32 kSenderSsrc = 0x10203;
  const WebRtc_UWord32 kMediaRecipientSsrc = 0x101;
  const WebRtc_UWord32 kOtherMediaFlowSsrc = 0x9999;

  PacketBuilder p;
  p.AddSrPacket(kSenderSsrc);
  // TMMBR packet.
  p.AddRtcpHeader(205, 3);
  p.Add32(kSenderSsrc);
  p.Add32(kMediaRecipientSsrc);
  p.Add32(kOtherMediaFlowSsrc);  // This SSRC is not what we're sending.
  p.AddTmmbrBandwidth(30000, 0, 0);

  rtcp_receiver_->SetSSRC(kMediaFlowSsrc);
  EXPECT_EQ(0, InjectRtcpPacket(p.packet(), p.length()));
  EXPECT_EQ(0, rtcp_receiver_->TMMBRReceived(0, 0, NULL));
}

TEST_F(RtcpReceiverTest, TmmbrPacketZeroRateIgnored) {
  const WebRtc_UWord32 kMediaFlowSsrc = 0x2040608;
  const WebRtc_UWord32 kSenderSsrc = 0x10203;
  const WebRtc_UWord32 kMediaRecipientSsrc = 0x101;
  rtcp_receiver_->SetSSRC(kMediaFlowSsrc);  // Matches "media source" above.

  PacketBuilder p;
  p.AddSrPacket(kSenderSsrc);
  // TMMBR packet.
  p.AddRtcpHeader(205, 3);
  p.Add32(kSenderSsrc);
  p.Add32(kMediaRecipientSsrc);
  p.Add32(kMediaFlowSsrc);
  p.AddTmmbrBandwidth(0, 0, 0);  // Rate zero.

  EXPECT_EQ(0, InjectRtcpPacket(p.packet(), p.length()));
  EXPECT_EQ(0, rtcp_receiver_->TMMBRReceived(0, 0, NULL));
}

TEST_F(RtcpReceiverTest, TmmbrThreeConstraintsTimeOut) {
  const WebRtc_UWord32 kMediaFlowSsrc = 0x2040608;
  const WebRtc_UWord32 kSenderSsrc = 0x10203;
  const WebRtc_UWord32 kMediaRecipientSsrc = 0x101;
  rtcp_receiver_->SetSSRC(kMediaFlowSsrc);  // Matches "media source" above.

  // Inject 3 packets "from" kMediaRecipientSsrc, Ssrc+1, Ssrc+2.
  // The times of arrival are starttime + 0, starttime + 5 and starttime + 10.
  for (WebRtc_UWord32 ssrc = kMediaRecipientSsrc;
       ssrc < kMediaRecipientSsrc+3; ++ssrc) {
    PacketBuilder p;
    p.AddSrPacket(kSenderSsrc);
    // TMMBR packet.
    p.AddRtcpHeader(205, 3);
    p.Add32(kSenderSsrc);
    p.Add32(ssrc);
    p.Add32(kMediaFlowSsrc);
    p.AddTmmbrBandwidth(30000, 0, 0);  // 30 Kbits/sec bandwidth, no overhead.

    EXPECT_EQ(0, InjectRtcpPacket(p.packet(), p.length()));
    system_clock_->AdvanceClock(5000);  // 5 seconds between each packet.
  }
  // It is now starttime+15.
  EXPECT_EQ(3, rtcp_receiver_->TMMBRReceived(0, 0, NULL));
  TMMBRSet candidate_set;
  candidate_set.VerifyAndAllocateSet(3);
  EXPECT_EQ(3, rtcp_receiver_->TMMBRReceived(3, 0, &candidate_set));
  EXPECT_LT(0U, candidate_set.Tmmbr(0));
  // We expect the timeout to be 25 seconds. Advance the clock by 12
  // seconds, timing out the first packet.
  system_clock_->AdvanceClock(12000);
  // Odd behaviour: Just counting them does not trigger the timeout.
  EXPECT_EQ(3, rtcp_receiver_->TMMBRReceived(0, 0, NULL));
  // Odd behaviour: There's only one left after timeout, not 2.
  EXPECT_EQ(1, rtcp_receiver_->TMMBRReceived(3, 0, &candidate_set));
  EXPECT_EQ(kMediaRecipientSsrc + 2, candidate_set.Ssrc(0));
}


}  // Anonymous namespace

}  // namespace webrtc
