/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <iostream>
#include <map>
#include <algorithm>
#include <string>

using namespace std;

#include "base/basictypes.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"

#include "nspr.h"
#include "nss.h"
#include "ssl.h"
#include "prthread.h"

#include "FakeMediaStreams.h"
#include "FakeMediaStreamsImpl.h"
#include "PeerConnectionImpl.h"
#include "runnable_utils.h"
#include "nsStaticComponents.h"
#include "nsIDOMRTCPeerConnection.h"

#include "mtransport_test_utils.h"
MtransportTestUtils *test_utils;



static int kDefaultTimeout = 5000;


namespace test {


static const std::string strSampleSdpAudioVideoNoIce =
  "v=0\r\n"
  "o=Mozilla-SIPUA 4949 0 IN IP4 10.86.255.143\r\n"
  "s=SIP Call\r\n"
  "t=0 0\r\n"
  "a=ice-ufrag:qkEP\r\n"
  "a=ice-pwd:ed6f9GuHjLcoCN6sC/Eh7fVl\r\n"
  "m=audio 16384 RTP/AVP 0 8 9 101\r\n"
  "c=IN IP4 10.86.255.143\r\n"
  "a=rtpmap:0 PCMU/8000\r\n"
  "a=rtpmap:8 PCMA/8000\r\n"
  "a=rtpmap:9 G722/8000\r\n"
  "a=rtpmap:101 telephone-event/8000\r\n"
  "a=fmtp:101 0-15\r\n"
  "a=sendrecv\r\n"
  "a=candidate:1 1 UDP 2130706431 192.168.2.1 50005 typ host\r\n"
  "a=candidate:2 2 UDP 2130706431 192.168.2.2 50006 typ host\r\n"
  "m=video 1024 RTP/AVP 97\r\n"
  "c=IN IP4 10.86.255.143\r\n"
  "a=rtpmap:120 VP8/90000\r\n"
  "a=fmtp:97 profile-level-id=42E00C\r\n"
  "a=sendrecv\r\n"
  "a=candidate:1 1 UDP 2130706431 192.168.2.3 50007 typ host\r\n"
  "a=candidate:2 2 UDP 2130706431 192.168.2.4 50008 typ host\r\n";


static const std::string strSampleCandidate =
  "a=candidate:1 1 UDP 2130706431 192.168.2.1 50005 typ host\r\n";

static const std::string strSampleMid = "";

static const unsigned short nSamplelevel = 2;

enum sdpTestFlags
{
  SHOULD_SEND_AUDIO     = (1<<0),
  SHOULD_RECV_AUDIO     = (1<<1),
  SHOULD_INACTIVE_AUDIO = (1<<2),
  SHOULD_REJECT_AUDIO   = (1<<3),
  SHOULD_OMIT_AUDIO     = (1<<4),
  DONT_CHECK_AUDIO      = (1<<5),

  SHOULD_SEND_VIDEO     = (1<<8),
  SHOULD_RECV_VIDEO     = (1<<9),
  SHOULD_INACTIVE_VIDEO = (1<<10),
  SHOULD_REJECT_VIDEO   = (1<<11),
  SHOULD_OMIT_VIDEO     = (1<<12),
  DONT_CHECK_VIDEO      = (1<<13),

  SHOULD_SENDRECV_AUDIO = SHOULD_SEND_AUDIO | SHOULD_RECV_AUDIO,
  SHOULD_SENDRECV_VIDEO = SHOULD_SEND_VIDEO | SHOULD_RECV_VIDEO,
  SHOULD_SENDRECV_AV = SHOULD_SENDRECV_AUDIO | SHOULD_SENDRECV_VIDEO,

  AUDIO_FLAGS = SHOULD_SEND_AUDIO | SHOULD_RECV_AUDIO
                | SHOULD_INACTIVE_AUDIO | SHOULD_REJECT_AUDIO
                | DONT_CHECK_AUDIO | SHOULD_OMIT_AUDIO,

  VIDEO_FLAGS = SHOULD_SEND_VIDEO | SHOULD_RECV_VIDEO
                | SHOULD_INACTIVE_VIDEO | SHOULD_REJECT_VIDEO
                | DONT_CHECK_VIDEO | SHOULD_OMIT_VIDEO
};

enum offerAnswerFlags
{
  OFFER_NONE  = 0, // Sugar to make function calls clearer.
  OFFER_AUDIO = (1<<0),
  OFFER_VIDEO = (1<<1),
  // Leaving some room here for other media types
  ANSWER_NONE  = 0, // Sugar to make function calls clearer.
  ANSWER_AUDIO = (1<<8),
  ANSWER_VIDEO = (1<<9),

  OFFER_AV = OFFER_AUDIO | OFFER_VIDEO,
  ANSWER_AV = ANSWER_AUDIO | ANSWER_VIDEO
};


class TestObserver : public IPeerConnectionObserver
{
public:
  enum Action {
    OFFER,
    ANSWER
  };

  enum StateType {
    kReadyState,
    kIceState,
    kSdpState,
    kSipccState
  };

  enum ResponseState {
    stateNoResponse,
    stateSuccess,
    stateError
  };

  TestObserver(sipcc::PeerConnectionImpl *peerConnection) :
    state(stateNoResponse),
    onAddStreamCalled(false),
    pc(peerConnection) {
  }

  virtual ~TestObserver() {}

  std::vector<nsDOMMediaStream *> GetStreams() { return streams; }

  NS_DECL_ISUPPORTS
  NS_DECL_IPEERCONNECTIONOBSERVER

  ResponseState state;
  char *lastString;
  uint32_t lastStatusCode;
  uint32_t lastStateType;
  bool onAddStreamCalled;

private:
  sipcc::PeerConnectionImpl *pc;
  std::vector<nsDOMMediaStream *> streams;
};

NS_IMPL_THREADSAFE_ISUPPORTS1(TestObserver, IPeerConnectionObserver)

NS_IMETHODIMP
TestObserver::OnCreateOfferSuccess(const char* offer)
{
  lastString = strdup(offer);
  state = stateSuccess;
  cout << "onCreateOfferSuccess = " << offer << endl;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnCreateOfferError(uint32_t code)
{
  lastStatusCode = code;
  state = stateError;
  cout << "onCreateOfferError" << endl;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnCreateAnswerSuccess(const char* answer)
{
  lastString = strdup(answer);
  state = stateSuccess;
  cout << "onCreateAnswerSuccess = " << answer << endl;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnCreateAnswerError(uint32_t code)
{
  lastStatusCode = code;
  cout << "onCreateAnswerError = " << code << endl;
  state = stateError;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnSetLocalDescriptionSuccess(uint32_t code)
{
  lastStatusCode = code;
  state = stateSuccess;
  cout << "onSetLocalDescriptionSuccess = " << code << endl;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnSetRemoteDescriptionSuccess(uint32_t code)
{
  lastStatusCode = code;
  state = stateSuccess;
  cout << "onSetRemoteDescriptionSuccess = " << code << endl;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnSetLocalDescriptionError(uint32_t code)
{
  lastStatusCode = code;
  state = stateError;
  cout << "onSetLocalDescriptionError = " << code << endl;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnSetRemoteDescriptionError(uint32_t code)
{
  lastStatusCode = code;
  state = stateError;
  cout << "onSetRemoteDescriptionError = " << code << endl;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::NotifyConnection()
{
  cout << "NotifyConection" << endl;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::NotifyClosedConnection()
{
  cout << "NotifyClosedConection" << endl;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::NotifyDataChannel(nsIDOMDataChannel *channel)
{
  cout << "NotifyDataChannel" << endl;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnStateChange(uint32_t state_type)
{
  nsresult rv;
  uint32_t gotstate;

  switch (state_type)
  {
  case kReadyState:
    rv = pc->GetReadyState(&gotstate);
    NS_ENSURE_SUCCESS(rv, rv);
    cout << "Ready State: " << gotstate << endl;
    break;
  case kIceState:
    rv = pc->GetIceState(&gotstate);
    NS_ENSURE_SUCCESS(rv, rv);
    cout << "ICE State: " << gotstate << endl;
    break;
  case kSdpState:
    cout << "SDP State: " << endl;
    // NS_ENSURE_SUCCESS(rv, rv);
    break;
  case kSipccState:
    rv = pc->GetSipccState(&gotstate);
    NS_ENSURE_SUCCESS(rv, rv);
    cout << "SIPCC State: " << gotstate << endl;
    break;
  default:
    // Unknown State
    break;
  }

  state = stateSuccess;
  lastStateType = state_type;
  return NS_OK;
}


NS_IMETHODIMP
TestObserver::OnAddStream(nsIDOMMediaStream *stream, const char *type)
{
  PR_ASSERT(stream);

  nsDOMMediaStream *ms = static_cast<nsDOMMediaStream *>(stream);

  cout << "OnAddStream called hints=" << ms->GetHintContents() << " type=" << type << " thread=" <<
    PR_GetCurrentThread() << endl ;

  onAddStreamCalled = true;

  streams.push_back(ms);

  // We know that the media stream is secretly a Fake_SourceMediaStream,
  // so now we can start it pulling from us
  Fake_SourceMediaStream *fs = static_cast<Fake_SourceMediaStream *>(ms->GetStream());

  test_utils->sts_target()->Dispatch(
    WrapRunnable(fs, &Fake_SourceMediaStream::Start),
    NS_DISPATCH_NORMAL);

  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnRemoveStream()
{
  state = stateSuccess;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnAddTrack()
{
  state = stateSuccess;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::OnRemoveTrack()
{
  state = stateSuccess;
  return NS_OK;
}

NS_IMETHODIMP
TestObserver::FoundIceCandidate(const char* strCandidate)
{
  return NS_OK;
}

class ParsedSDP {
 public:
  //Line number with the corresponding SDP line.
  typedef pair<int, string> SdpLine;

  ParsedSDP(std::string sdp):
    sdp_(),
    sdp_without_ice_(),
    ice_candidates_(),
    levels_(0),
    num_lines(0)
  {
    sdp_ = sdp;
    Parse();
  }


  void ReplaceLine(std::string objType, std::string content)
  {
    std::multimap<std::string, SdpLine>::iterator it;
    it = sdp_map_.find(objType);
    if(it != sdp_map_.end()) {
      SdpLine sdp_line_pair = (*it).second;
      int line_no = sdp_line_pair.first;
      sdp_map_.erase(it);
      std::string value = content.substr(objType.length());
      sdp_map_.insert(std::pair<std::string, SdpLine>(objType, make_pair(line_no,value)));
    }
  }

  void AddLine(std::string content)
  {
    size_t whiteSpace = content.find(' ');
    std::string key;
    std::string value;
    if(whiteSpace == string::npos) {
      key = content.substr(0,  content.size() - 2);
      value = "";
    } else {
      key = content.substr(0, whiteSpace);
      value = content.substr(whiteSpace+1);
    }
    sdp_map_.insert(std::pair<std::string, SdpLine>(key, make_pair(num_lines,value)));
    num_lines++;
  }

  //Parse SDP as string into map that looks like:
  // key: sdp content till first space
  // value : <line_number, sdp content after the first space>
  void Parse()
  {
    size_t prev = 0;
    size_t found = 0;
    num_lines = 0;
    for(;;) {
      found = sdp_.find('\n', found + 1);
      if (found == string::npos)
        break;
      std::string line = sdp_.substr(prev, (found - prev) + 1);
      size_t whiteSpace = line.find(' ');
      std::string key;
      std::string value;
      if(whiteSpace == string::npos) {
        //this is the line with no extra contents
        //example, v=0, a=sendrecv
        key = line.substr(0, line.size() - 2);
        //<line_no>:<valeu>
        value = "";
      } else {
        key = line.substr(0, whiteSpace);
        //<line_no>:<value>
        value = line.substr(whiteSpace+1);
      }
      SdpLine sdp_line_pair = make_pair(num_lines,value);
      sdp_map_.insert(std::pair<std::string, SdpLine>(key, sdp_line_pair));
      num_lines++;
      //storing ice candidates separately for quick acesss as needed
      //for the trickle unit tests
      if (line.find("a=candidate") == 0) {
        // This is a candidate, strip of a= and \r\n
        std::string cand = line.substr(2, line.size() - 4);
        ice_candidates_.insert(std::pair<int, std::string>(levels_, cand));
       } else {
        sdp_without_ice_ += line;
      }
      if (line.find("m=") == 0) {
        // This is an m-line
        ++levels_;
      }
      prev = found + 1;
    }
  }

  //Convert Internal SDP representation into String representation
  std::string getSdp()
  {
     std::vector<std::string> sdp_lines(num_lines);
     for (std::multimap<std::string, SdpLine>::iterator it = sdp_map_.begin();
         it != sdp_map_.end(); ++it) {

      SdpLine sdp_line_pair = (*it).second;
      std::string value;
      if(sdp_line_pair.second.length() == 0) {
        value = (*it).first + "\r\n";
        sdp_lines[sdp_line_pair.first] = value;
      } else {
        value = (*it).first + ' ' + sdp_line_pair.second;
        sdp_lines[sdp_line_pair.first] = value;
      }
   }

    //generate our final sdp in string format
    std::string sdp;
    for(int i=0; i < sdp_lines.size(); i++)
    {
      sdp += sdp_lines[i];
    }

    return sdp;
  }



  std::string sdp_;
  std::string sdp_without_ice_;
  std::multimap<int, std::string> ice_candidates_;
  std::multimap<std::string, SdpLine> sdp_map_;
  int levels_;
  int num_lines;
};

class SignalingAgent {
 public:
  SignalingAgent() : pc(nullptr) {}

  ~SignalingAgent() {
    pc->GetMainThread()->Dispatch(
      WrapRunnable(this, &SignalingAgent::Close),
      NS_DISPATCH_SYNC);
  }

  void Init(nsCOMPtr<nsIThread> thread)
  {
    size_t found = 2;
    ASSERT_TRUE(found > 0);

    pc = sipcc::PeerConnectionImpl::CreatePeerConnection();
    ASSERT_TRUE(pc);

    pObserver = new TestObserver(pc);
    ASSERT_TRUE(pObserver);

    ASSERT_EQ(pc->Initialize(pObserver, nullptr, thread), NS_OK);

    ASSERT_TRUE_WAIT(sipcc_state() == sipcc::PeerConnectionImpl::kStarted,
                     kDefaultTimeout);
    ASSERT_TRUE_WAIT(ice_state() == sipcc::PeerConnectionImpl::kIceWaiting, 5000);
    cout << "Init Complete" << endl;
  }

  uint32_t sipcc_state()
  {
    uint32_t res;

    pc->GetSipccState(&res);
    return res;
  }

  uint32_t ice_state()
  {
    uint32_t res;

    pc->GetIceState(&res);
    return res;
  }

  void Close()
  {
    cout << "Close" << endl;

    pc->Close(false);
    pc = nullptr;

    // Shutdown is synchronous evidently.
    // ASSERT_TRUE(pObserver->WaitForObserverCall());
    // ASSERT_EQ(pc->sipcc_state(), sipcc::PeerConnectionInterface::kIdle);
  }

  char* offer() const { return offer_; }
  char* answer() const { return answer_; }

  std::string getLocalDescription() const {
    char *sdp = nullptr;
    pc->GetLocalDescription(&sdp);
    if (!sdp) {
      return "";
    }
    return sdp;
  }

  std::string getRemoteDescription() const {
    char *sdp = 0;
    pc->GetRemoteDescription(&sdp);
    if (!sdp) {
      return "";
    }
    return sdp;
  }

  void CreateOffer(sipcc::MediaConstraints& constraints,
                   uint32_t offerFlags, uint32_t sdpCheck) {

    // Create a media stream as if it came from GUM
    Fake_AudioStreamSource *audio_stream =
      new Fake_AudioStreamSource();

    nsresult ret;
    test_utils->sts_target()->Dispatch(
      WrapRunnableRet(audio_stream, &Fake_MediaStream::Start, &ret),
        NS_DISPATCH_SYNC);

    ASSERT_TRUE(NS_SUCCEEDED(ret));

    // store in object to be used by RemoveStream
    nsRefPtr<nsDOMMediaStream> domMediaStream = new nsDOMMediaStream(audio_stream);
    domMediaStream_ = domMediaStream;

    uint32_t aHintContents = 0;

    if (offerFlags & OFFER_AUDIO) {
      aHintContents |= nsDOMMediaStream::HINT_CONTENTS_AUDIO;
    }
    if (offerFlags & OFFER_VIDEO) {
      aHintContents |= nsDOMMediaStream::HINT_CONTENTS_VIDEO;
    }

    domMediaStream->SetHintContents(aHintContents);

    pc->AddStream(domMediaStream);
    domMediaStream_ = domMediaStream;

    // Now call CreateOffer as JS would
    pObserver->state = TestObserver::stateNoResponse;
    ASSERT_EQ(pc->CreateOffer(constraints), NS_OK);
    ASSERT_TRUE_WAIT(pObserver->state == TestObserver::stateSuccess, kDefaultTimeout);
    SDPSanityCheck(pObserver->lastString, sdpCheck, true);
    offer_ = pObserver->lastString;
  }

  void CreateOfferExpectError(sipcc::MediaConstraints& constraints) {
    ASSERT_EQ(pc->CreateOffer(constraints), NS_OK);
    ASSERT_TRUE_WAIT(pObserver->state == TestObserver::stateError, kDefaultTimeout);
  }

void CreateAnswer(sipcc::MediaConstraints& constraints, std::string offer,
                    uint32_t offerAnswerFlags,
                    uint32_t sdpCheck = DONT_CHECK_AUDIO|DONT_CHECK_VIDEO) {
    // Create a media stream as if it came from GUM
    nsRefPtr<nsDOMMediaStream> domMediaStream = new nsDOMMediaStream();

    uint32_t aHintContents = 0;

    if (offerAnswerFlags & ANSWER_AUDIO) {
      aHintContents |= nsDOMMediaStream::HINT_CONTENTS_AUDIO;
    }
    if (offerAnswerFlags & ANSWER_VIDEO) {
      aHintContents |= nsDOMMediaStream::HINT_CONTENTS_VIDEO;
    }

    domMediaStream->SetHintContents(aHintContents);

    pc->AddStream(domMediaStream);

    // Decide if streams are disabled for offer or answer
    // then perform SDP checking based on which stream disabled

    pObserver->state = TestObserver::stateNoResponse;
    ASSERT_EQ(pc->CreateAnswer(constraints), NS_OK);
    ASSERT_TRUE_WAIT(pObserver->state == TestObserver::stateSuccess, kDefaultTimeout);
    SDPSanityCheck(pObserver->lastString, sdpCheck, false);

    answer_ = pObserver->lastString;
  }

  // At present, we use the hints field in a stream to find and
  // remove it. This only works if the specified hints flags are
  // unique among all streams in the PeerConnection. This is not
  // generally true, and will need significant revision once
  // multiple streams are supported.
  void CreateOfferRemoveStream(sipcc::MediaConstraints& constraints,
                               uint32_t hints, uint32_t sdpCheck) {

    domMediaStream_->SetHintContents(hints);

    // This currently "removes" a stream that has the same audio/video
    // hints as were passed in.
    // When complete RemoveStream will remove and entire stream and its tracks
    // not just disable a track as this is currently doing
    pc->RemoveStream(domMediaStream_);

    // Now call CreateOffer as JS would
    pObserver->state = TestObserver::stateNoResponse;
    ASSERT_EQ(pc->CreateOffer(constraints), NS_OK);
    ASSERT_TRUE_WAIT(pObserver->state == TestObserver::stateSuccess, kDefaultTimeout);
    SDPSanityCheck(pObserver->lastString, sdpCheck, true);
    offer_ = pObserver->lastString;
  }

  void SetRemote(TestObserver::Action action, std::string remote) {
    pObserver->state = TestObserver::stateNoResponse;
    ASSERT_EQ(pc->SetRemoteDescription(action, remote.c_str()), NS_OK);
    ASSERT_TRUE_WAIT(pObserver->state == TestObserver::stateSuccess, kDefaultTimeout);
  }

  void SetLocal(TestObserver::Action action, std::string local) {
    pObserver->state = TestObserver::stateNoResponse;
    ASSERT_EQ(pc->SetLocalDescription(action, local.c_str()), NS_OK);
    ASSERT_TRUE_WAIT(pObserver->state == TestObserver::stateSuccess, kDefaultTimeout);
  }

  void DoTrickleIce(ParsedSDP &sdp) {
    for (std::multimap<int, std::string>::iterator it = sdp.ice_candidates_.begin();
         it != sdp.ice_candidates_.end(); ++it) {
      if ((*it).first != 0) {
        std::cerr << "Adding trickle ICE candidate " << (*it).second << std::endl;

        ASSERT_TRUE(NS_SUCCEEDED(pc->AddIceCandidate((*it).second.c_str(), "", (*it).first)));
      }
    }
  }


  bool IceCompleted() {
    uint32_t state;
    pc->GetIceState(&state);
    return state == sipcc::PeerConnectionImpl::kIceConnected;
  }

  void AddIceCandidate(const char* candidate, const char* mid, unsigned short level) {
    pc->AddIceCandidate(candidate, mid, level);
  }

  int GetPacketsReceived(int stream) {
    std::vector<nsDOMMediaStream *> streams = pObserver->GetStreams();

    if ((int) streams.size() <= stream) {
      return 0;
    }

    return streams[stream]->GetStream()->AsSourceStream()->GetSegmentsAdded();
  }

  int GetPacketsSent(int stream) {
    return static_cast<Fake_MediaStreamBase *>(
        domMediaStream_->GetStream())->GetSegmentsAdded();
  }

  //Stops generating new audio data for transmission.
  //Should be called before Cleanup of the peer connection.
  void CloseSendStreams() {
    static_cast<Fake_AudioStreamSource*>(
        domMediaStream_->GetStream())->StopStream();
  }

  //Stops pulling audio data off the receivers.
  //Should be called before Cleanup of the peer connection.
  void CloseReceiveStreams() {
    std::vector<nsDOMMediaStream *> streams =
                            pObserver->GetStreams();
    for(int i=0; i < streams.size(); i++) {
      streams[i]->GetStream()->AsSourceStream()->StopStream();
    }
  }

public:
  mozilla::RefPtr<sipcc::PeerConnectionImpl> pc;
  nsRefPtr<TestObserver> pObserver;
  char* offer_;
  char* answer_;
  nsRefPtr<nsDOMMediaStream> domMediaStream_;

private:
  void SDPSanityCheck(std::string sdp, uint32_t flags, bool offer)
  {
    ASSERT_NE(sdp.find("v=0"), std::string::npos);
    ASSERT_NE(sdp.find("c=IN IP4"), std::string::npos);
    ASSERT_NE(sdp.find("a=fingerprint:sha-256"), std::string::npos);

    cout << "SDPSanityCheck flags for "
         << (offer ? "offer" : "answer")
         << " = " << std::hex << std::showbase
         << flags << std::dec

         << ((flags & SHOULD_SEND_AUDIO)?" SHOULD_SEND_AUDIO":"")
         << ((flags & SHOULD_RECV_AUDIO)?" SHOULD_RECV_AUDIO":"")
         << ((flags & SHOULD_INACTIVE_AUDIO)?" SHOULD_INACTIVE_AUDIO":"")
         << ((flags & SHOULD_REJECT_AUDIO)?" SHOULD_REJECT_AUDIO":"")
         << ((flags & SHOULD_OMIT_AUDIO)?" SHOULD_OMIT_AUDIO":"")
         << ((flags & DONT_CHECK_AUDIO)?" DONT_CHECK_AUDIO":"")

         << ((flags & SHOULD_SEND_VIDEO)?" SHOULD_SEND_VIDEO":"")
         << ((flags & SHOULD_RECV_VIDEO)?" SHOULD_RECV_VIDEO":"")
         << ((flags & SHOULD_INACTIVE_VIDEO)?" SHOULD_INACTIVE_VIDEO":"")
         << ((flags & SHOULD_REJECT_VIDEO)?" SHOULD_REJECT_VIDEO":"")
         << ((flags & SHOULD_OMIT_VIDEO)?" SHOULD_OMIT_VIDEO":"")
         << ((flags & DONT_CHECK_VIDEO)?" DONT_CHECK_VIDEO":"")

         << endl;

    switch(flags & AUDIO_FLAGS) {
      case 0:
            ASSERT_EQ(sdp.find("a=rtpmap:109 opus/48000"), std::string::npos);
        break;
      case SHOULD_SEND_AUDIO:
            ASSERT_NE(sdp.find("a=rtpmap:109 opus/48000"), std::string::npos);
            ASSERT_NE(sdp.find(" 0-15\r\na=sendonly"), std::string::npos);
            if (offer) {
              ASSERT_NE(sdp.find("a=rtpmap:0 PCMU/8000"), std::string::npos);
            }
        break;
      case SHOULD_RECV_AUDIO:
            ASSERT_NE(sdp.find("a=rtpmap:109 opus/48000"), std::string::npos);
            ASSERT_NE(sdp.find(" 0-15\r\na=recvonly"), std::string::npos);
            if (offer) {
              ASSERT_NE(sdp.find("a=rtpmap:0 PCMU/8000"), std::string::npos);
            }
        break;
      case SHOULD_SENDRECV_AUDIO:
            ASSERT_NE(sdp.find("a=rtpmap:109 opus/48000"), std::string::npos);
            ASSERT_NE(sdp.find(" 0-15\r\na=sendrecv"), std::string::npos);
            if (offer) {
              ASSERT_NE(sdp.find("a=rtpmap:0 PCMU/8000"), std::string::npos);
            }
        break;
      case SHOULD_INACTIVE_AUDIO:
            ASSERT_NE(sdp.find("a=rtpmap:109 opus/48000"), std::string::npos);
            ASSERT_NE(sdp.find(" 0-15\r\na=inactive"), std::string::npos);
        break;
      case SHOULD_REJECT_AUDIO:
            ASSERT_EQ(sdp.find("a=rtpmap:109 opus/48000"), std::string::npos);
            ASSERT_NE(sdp.find("m=audio 0 "), std::string::npos);
        break;
      case SHOULD_OMIT_AUDIO:
            ASSERT_EQ(sdp.find("m=audio"), std::string::npos);
        break;
      case DONT_CHECK_AUDIO:
        break;
      default:
            ASSERT_FALSE("Missing case in switch statement");
    }

    switch(flags & VIDEO_FLAGS) {
      case 0:
            ASSERT_EQ(sdp.find("a=rtpmap:120 VP8/90000"), std::string::npos);
        break;
      case SHOULD_SEND_VIDEO:
            ASSERT_NE(sdp.find("a=rtpmap:120 VP8/90000\r\na=sendonly"),
                  std::string::npos);
        break;
      case SHOULD_RECV_VIDEO:
            ASSERT_NE(sdp.find("a=rtpmap:120 VP8/90000\r\na=recvonly"),
                  std::string::npos);
        break;
      case SHOULD_SENDRECV_VIDEO:
            ASSERT_NE(sdp.find("a=rtpmap:120 VP8/90000\r\na=sendrecv"),
                  std::string::npos);
        break;
      case SHOULD_INACTIVE_VIDEO:
            ASSERT_NE(sdp.find("a=rtpmap:120 VP8/90000\r\na=inactive"),
                      std::string::npos);
        break;
      case SHOULD_REJECT_VIDEO:
            ASSERT_NE(sdp.find("m=video 0 "), std::string::npos);
        break;
      case SHOULD_OMIT_VIDEO:
            ASSERT_EQ(sdp.find("m=video"), std::string::npos);
        break;
      case DONT_CHECK_VIDEO:
        break;
      default:
            ASSERT_FALSE("Missing case in switch statement");
    }
  }
};

class SignalingEnvironment : public ::testing::Environment {
 public:
  void TearDown() {
    sipcc::PeerConnectionImpl::Shutdown();
  }
};

class SignalingTest : public ::testing::Test {
public:
  static void SetUpTestCase() {
    nsIThread *thread;

    nsresult rv = NS_NewThread(&thread);
    ASSERT_TRUE(NS_SUCCEEDED(rv));

    gThread = thread;
  }

  void SetUp() {
    a1_.Init(gThread);
    a2_.Init(gThread);
  }

  static void TearDownTestCase() {
    gThread = nullptr;
  }

  void CreateOffer(sipcc::MediaConstraints& constraints,
                   uint32_t offerFlags, uint32_t sdpCheck) {
    a1_.CreateOffer(constraints, offerFlags, sdpCheck);
  }

  void CreateSetOffer(sipcc::MediaConstraints& constraints, uint32_t sdpCheck) {
    a1_.CreateOffer(constraints, OFFER_AV, sdpCheck);
    a1_.SetLocal(TestObserver::OFFER, a1_.offer());
  }

  void OfferAnswer(sipcc::MediaConstraints& aconstraints,
                   sipcc::MediaConstraints& bconstraints,
                   uint32_t offerAnswerFlags,
                   bool finishAfterAnswer, uint32_t offerSdpCheck,
                   uint32_t answerSdpCheck) {
    a1_.CreateOffer(aconstraints, offerAnswerFlags, offerSdpCheck);
    a1_.SetLocal(TestObserver::OFFER, a1_.offer());
    a2_.SetRemote(TestObserver::OFFER, a1_.offer());
    a2_.CreateAnswer(bconstraints, a1_.offer(),
                     offerAnswerFlags, answerSdpCheck);
    if(true == finishAfterAnswer) {
        a2_.SetLocal(TestObserver::ANSWER, a2_.answer());
        a1_.SetRemote(TestObserver::ANSWER, a2_.answer());

        ASSERT_TRUE_WAIT(a1_.IceCompleted() == true, kDefaultTimeout);
        ASSERT_TRUE_WAIT(a2_.IceCompleted() == true, kDefaultTimeout);
    }
  }

  void OfferModifiedAnswer(sipcc::MediaConstraints& aconstraints,
                           sipcc::MediaConstraints& bconstraints,
                           uint32_t offerSdpCheck, uint32_t answerSdpCheck) {
    a1_.CreateOffer(aconstraints, OFFER_AV, offerSdpCheck);
    a1_.SetLocal(TestObserver::OFFER, a1_.offer());
    a2_.SetRemote(TestObserver::OFFER, a1_.offer());
    a2_.CreateAnswer(bconstraints, a1_.offer(), OFFER_AV | ANSWER_AV,
                     answerSdpCheck);
    a2_.SetLocal(TestObserver::ANSWER, a2_.answer());
    ParsedSDP sdpWrapper(a2_.answer());
    sdpWrapper.ReplaceLine("m=audio", "m=audio 65375 RTP/SAVPF 109 8 101\r\n");
    sdpWrapper.AddLine("a=rtpmap:8 PCMA/8000\r\n");
    cout << "Modified SDP " << sdpWrapper.getSdp() << endl;
    a1_.SetRemote(TestObserver::ANSWER, sdpWrapper.getSdp());
    ASSERT_TRUE_WAIT(a1_.IceCompleted() == true, kDefaultTimeout);
    ASSERT_TRUE_WAIT(a2_.IceCompleted() == true, kDefaultTimeout);
  }

  void OfferAnswerTrickle(sipcc::MediaConstraints& aconstraints,
                          sipcc::MediaConstraints& bconstraints,
                          uint32_t offerSdpCheck, uint32_t answerSdpCheck) {
    a1_.CreateOffer(aconstraints, OFFER_AV, offerSdpCheck);
    a1_.SetLocal(TestObserver::OFFER, a1_.offer());
    ParsedSDP a1_offer(a1_.offer());
    a2_.SetRemote(TestObserver::OFFER, a1_offer.sdp_without_ice_);
    a2_.CreateAnswer(bconstraints, a1_offer.sdp_without_ice_,
                     OFFER_AV|ANSWER_AV, answerSdpCheck);
    a2_.SetLocal(TestObserver::ANSWER, a2_.answer());
    ParsedSDP a2_answer(a2_.answer());
    a1_.SetRemote(TestObserver::ANSWER, a2_answer.sdp_without_ice_);
    // Now set the trickle ICE candidates
    a1_.DoTrickleIce(a2_answer);
    a2_.DoTrickleIce(a1_offer);
    ASSERT_TRUE_WAIT(a1_.IceCompleted() == true, kDefaultTimeout);
    ASSERT_TRUE_WAIT(a2_.IceCompleted() == true, kDefaultTimeout);
  }

  void CreateOfferRemoveStream(sipcc::MediaConstraints& constraints,
                               uint32_t hints, uint32_t sdpCheck) {
    sipcc::MediaConstraints aconstraints;
    aconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
    aconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
    a1_.CreateOffer(aconstraints, OFFER_AV, SHOULD_SENDRECV_AV );
    a1_.CreateOfferRemoveStream(constraints, hints, sdpCheck);
  }

  void CreateOfferAudioOnly(sipcc::MediaConstraints& constraints,
                            uint32_t sdpCheck) {
    a1_.CreateOffer(constraints, OFFER_AUDIO, sdpCheck);
  }

  void CreateOfferAddCandidate(sipcc::MediaConstraints& constraints,
                               const char * candidate, const char * mid,
                               unsigned short level, uint32_t sdpCheck) {
    a1_.CreateOffer(constraints, OFFER_AV, sdpCheck);
    a1_.AddIceCandidate(candidate, mid, level);
  }

 protected:
  static nsCOMPtr<nsIThread> gThread;
  SignalingAgent a1_;  // Canonically "caller"
  SignalingAgent a2_;  // Canonically "callee"
};

nsCOMPtr<nsIThread> SignalingTest::gThread;


TEST_F(SignalingTest, JustInit)
{
}

TEST_F(SignalingTest, CreateSetOffer)
{
  sipcc::MediaConstraints constraints;
  CreateSetOffer(constraints, SHOULD_SENDRECV_AV);
}

TEST_F(SignalingTest, CreateOfferAudioVideoConstraintUndefined)
{
  sipcc::MediaConstraints constraints;
  CreateOffer(constraints, OFFER_AV, SHOULD_SENDRECV_AV);
}

TEST_F(SignalingTest, CreateOfferNoVideoStream)
{
  sipcc::MediaConstraints constraints;
  constraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  constraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  CreateOffer(constraints, OFFER_AUDIO,
              SHOULD_SENDRECV_AUDIO | SHOULD_OMIT_VIDEO);
}

TEST_F(SignalingTest, CreateOfferNoAudioStream)
{
  sipcc::MediaConstraints constraints;
  constraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  constraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  CreateOffer(constraints, OFFER_VIDEO,
              SHOULD_OMIT_AUDIO | SHOULD_SENDRECV_VIDEO);
}

TEST_F(SignalingTest, CreateOfferDontReceiveAudio)
{
  sipcc::MediaConstraints constraints;
  constraints.setBooleanConstraint("OfferToReceiveAudio", false, false);
  constraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  constraints.setBooleanConstraint("VoiceActivityDetection", true, true);
  CreateOffer(constraints, OFFER_AV,
              SHOULD_SEND_AUDIO | SHOULD_SENDRECV_VIDEO);
}

TEST_F(SignalingTest, CreateOfferDontReceiveVideo)
{
  sipcc::MediaConstraints constraints;
  constraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  constraints.setBooleanConstraint("OfferToReceiveVideo", false, false);
  CreateOffer(constraints, OFFER_AV,
              SHOULD_SENDRECV_AUDIO | SHOULD_SEND_VIDEO);
}

TEST_F(SignalingTest, CreateOfferRemoveAudioStream)
{
  sipcc::MediaConstraints constraints;
  constraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  constraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  CreateOfferRemoveStream(constraints, nsDOMMediaStream::HINT_CONTENTS_AUDIO,
              SHOULD_RECV_AUDIO | SHOULD_SENDRECV_VIDEO);
}

TEST_F(SignalingTest, CreateOfferDontReceiveAudioRemoveAudioStream)
{
  sipcc::MediaConstraints constraints;
  constraints.setBooleanConstraint("OfferToReceiveAudio", false, false);
  constraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  CreateOfferRemoveStream(constraints, nsDOMMediaStream::HINT_CONTENTS_AUDIO,
              SHOULD_SENDRECV_VIDEO);
}

TEST_F(SignalingTest, CreateOfferDontReceiveVideoRemoveVideoStream)
{
  sipcc::MediaConstraints constraints;
  constraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  constraints.setBooleanConstraint("OfferToReceiveVideo", false, false);
  CreateOfferRemoveStream(constraints, nsDOMMediaStream::HINT_CONTENTS_VIDEO,
              SHOULD_SENDRECV_AUDIO);
}

TEST_F(SignalingTest, OfferAnswerNothingDisabled)
{
  sipcc::MediaConstraints constraints;
  OfferAnswer(constraints, constraints, OFFER_AV | ANSWER_AV, false,
              SHOULD_SENDRECV_AV, SHOULD_SENDRECV_AV);
}

TEST_F(SignalingTest, OfferAnswerDontReceiveAudioOnOffer)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", false, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  sipcc::MediaConstraints answerconstraints;
  answerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  answerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  OfferAnswer(offerconstraints, answerconstraints, OFFER_AV | ANSWER_AV,
              false, SHOULD_SEND_AUDIO | SHOULD_SENDRECV_VIDEO,
              SHOULD_RECV_AUDIO | SHOULD_SENDRECV_VIDEO);
}

TEST_F(SignalingTest, OfferAnswerDontReceiveVideoOnOffer)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", false, false);
  sipcc::MediaConstraints answerconstraints;
  answerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  answerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  OfferAnswer(offerconstraints, answerconstraints, OFFER_AV | ANSWER_AV,
              false, SHOULD_SENDRECV_AUDIO | SHOULD_SEND_VIDEO,
              SHOULD_SENDRECV_AUDIO | SHOULD_RECV_VIDEO);
}

TEST_F(SignalingTest, OfferAnswerDontReceiveAudioOnAnswer)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  sipcc::MediaConstraints answerconstraints;
  answerconstraints.setBooleanConstraint("OfferToReceiveAudio", false, false);
  answerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  OfferAnswer(offerconstraints, answerconstraints, OFFER_AV | ANSWER_AV,
              false, SHOULD_SENDRECV_AV,
              SHOULD_SEND_AUDIO | SHOULD_SENDRECV_VIDEO);
}

TEST_F(SignalingTest, OfferAnswerDontReceiveVideoOnAnswer)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  sipcc::MediaConstraints answerconstraints;
  answerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  answerconstraints.setBooleanConstraint("OfferToReceiveVideo", false, false);
  OfferAnswer(offerconstraints, answerconstraints, OFFER_AV | ANSWER_AV,
              false, SHOULD_SENDRECV_AV,
              SHOULD_SENDRECV_AUDIO | SHOULD_SEND_VIDEO);
}

TEST_F(SignalingTest, OfferAnswerDontAddAudioStreamOnOffer)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  sipcc::MediaConstraints answerconstraints;
  answerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  answerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  OfferAnswer(offerconstraints, answerconstraints, OFFER_VIDEO | ANSWER_AV,
              false, SHOULD_OMIT_AUDIO | SHOULD_SENDRECV_VIDEO,
              SHOULD_OMIT_AUDIO | SHOULD_SENDRECV_VIDEO);
}

TEST_F(SignalingTest, OfferAnswerDontAddVideoStreamOnOffer)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  sipcc::MediaConstraints answerconstraints;
  answerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  answerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  OfferAnswer(offerconstraints, answerconstraints, OFFER_AUDIO | ANSWER_AV,
              false, SHOULD_SENDRECV_AUDIO | SHOULD_OMIT_VIDEO,
              SHOULD_SENDRECV_AUDIO | SHOULD_OMIT_VIDEO);
}

TEST_F(SignalingTest, OfferAnswerDontAddAudioStreamOnAnswer)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  sipcc::MediaConstraints answerconstraints;
  answerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  answerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  OfferAnswer(offerconstraints, answerconstraints, OFFER_AV | ANSWER_VIDEO,
              false, SHOULD_SENDRECV_AV,
              SHOULD_RECV_AUDIO | SHOULD_SENDRECV_VIDEO);
}

TEST_F(SignalingTest, OfferAnswerDontAddVideoStreamOnAnswer)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  sipcc::MediaConstraints answerconstraints;
  answerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  answerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  OfferAnswer(offerconstraints, answerconstraints, OFFER_AV | ANSWER_AUDIO,
              false, SHOULD_SENDRECV_AV,
              SHOULD_SENDRECV_AUDIO | SHOULD_RECV_VIDEO);
}

TEST_F(SignalingTest, OfferAnswerDontAddVideoStreamOnAnswerDontReceiveVideoOnAnswer)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  sipcc::MediaConstraints answerconstraints;
  answerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  answerconstraints.setBooleanConstraint("OfferToReceiveVideo", false, false);
  OfferAnswer(offerconstraints, answerconstraints, OFFER_AV | ANSWER_AUDIO,
              false, SHOULD_SENDRECV_AV, SHOULD_SENDRECV_AUDIO );
}

TEST_F(SignalingTest, OfferAnswerDontAddAudioStreamOnAnswerDontReceiveAudioOnAnswer)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  sipcc::MediaConstraints answerconstraints;
  answerconstraints.setBooleanConstraint("OfferToReceiveAudio", false, false);
  answerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  OfferAnswer(offerconstraints, answerconstraints, OFFER_AV | ANSWER_VIDEO,
              false, SHOULD_SENDRECV_AV,
              SHOULD_REJECT_AUDIO | SHOULD_SENDRECV_VIDEO);
}

TEST_F(SignalingTest, OfferAnswerDontAddAudioStreamOnOfferDontReceiveAudioOnOffer)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", false, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  sipcc::MediaConstraints answerconstraints;
  answerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  answerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  OfferAnswer(offerconstraints, answerconstraints, OFFER_VIDEO | ANSWER_AV,
              false, SHOULD_SENDRECV_VIDEO, SHOULD_SENDRECV_VIDEO);
}

TEST_F(SignalingTest, OfferAnswerDontAddVideoStreamOnOfferDontReceiveVideoOnOffer)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", false, false);
  sipcc::MediaConstraints answerconstraints;
  answerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  answerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  OfferAnswer(offerconstraints, answerconstraints, OFFER_AUDIO | ANSWER_AV,
              false, SHOULD_SENDRECV_AUDIO | SHOULD_OMIT_VIDEO,
              SHOULD_SENDRECV_AUDIO | SHOULD_OMIT_VIDEO);
}

TEST_F(SignalingTest, OfferAnswerDontReceiveAudioNoAudioStreamOnOfferDontReceiveVideoOnAnswer)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", false, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  sipcc::MediaConstraints answerconstraints;
  answerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  answerconstraints.setBooleanConstraint("OfferToReceiveVideo", false, false);
  OfferAnswer(offerconstraints, answerconstraints, OFFER_VIDEO | ANSWER_AV,
              false, SHOULD_SENDRECV_VIDEO, SHOULD_SEND_VIDEO);
}

TEST_F(SignalingTest, CreateOfferAddCandidate)
{
  sipcc::MediaConstraints constraints;
  CreateOfferAddCandidate(constraints, strSampleCandidate.c_str(),
                          strSampleMid.c_str(), nSamplelevel,
                          SHOULD_SENDRECV_AV);
}

// XXX adam@nostrum.com -- This test seems questionable; we need to think
// through what actually needs to be tested here.
TEST_F(SignalingTest, OfferAnswerReNegotiateOfferAnswerDontReceiveVideoNoVideoStream)
{
  sipcc::MediaConstraints aconstraints;
  aconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  aconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);

  sipcc::MediaConstraints bconstraints;
  bconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  bconstraints.setBooleanConstraint("OfferToReceiveVideo", false, false);

  OfferAnswer(aconstraints, aconstraints, OFFER_AV | ANSWER_AV,
              false, SHOULD_SENDRECV_AV, SHOULD_SENDRECV_AV);
  OfferAnswer(bconstraints, bconstraints, OFFER_AUDIO | ANSWER_AV,
              false, SHOULD_SENDRECV_AUDIO | SHOULD_SEND_VIDEO,
              SHOULD_SENDRECV_AUDIO | SHOULD_INACTIVE_VIDEO);
}

TEST_F(SignalingTest, OfferAnswerDontAddAudioStreamOnAnswerNoConstraints)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  sipcc::MediaConstraints answerconstraints;
  OfferAnswer(offerconstraints, answerconstraints, OFFER_AV | ANSWER_VIDEO,
              false, SHOULD_SENDRECV_AV,
              SHOULD_RECV_AUDIO | SHOULD_SENDRECV_VIDEO);
}

TEST_F(SignalingTest, OfferAnswerDontAddVideoStreamOnAnswerNoConstraints)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  sipcc::MediaConstraints answerconstraints;
  OfferAnswer(offerconstraints, answerconstraints, OFFER_AV | ANSWER_AUDIO,
              false, SHOULD_SENDRECV_AV,
              SHOULD_SENDRECV_AUDIO | SHOULD_RECV_VIDEO);
}

TEST_F(SignalingTest, OfferAnswerDontAddAudioVideoStreamsOnAnswerNoConstraints)
{
  sipcc::MediaConstraints offerconstraints;
  offerconstraints.setBooleanConstraint("OfferToReceiveAudio", true, false);
  offerconstraints.setBooleanConstraint("OfferToReceiveVideo", true, false);
  sipcc::MediaConstraints answerconstraints;
  OfferAnswer(offerconstraints, answerconstraints, OFFER_AV | ANSWER_NONE,
              false, SHOULD_SENDRECV_AV,
              SHOULD_RECV_AUDIO | SHOULD_RECV_VIDEO);
}

TEST_F(SignalingTest, FullCall)
{
  sipcc::MediaConstraints constraints;
  OfferAnswer(constraints, constraints, OFFER_AV | ANSWER_AV,
              true, SHOULD_SENDRECV_AV, SHOULD_SENDRECV_AV);

  PR_Sleep(kDefaultTimeout * 2); // Wait for some data to get written

  a1_.CloseSendStreams();
  a2_.CloseReceiveStreams();
  // Check that we wrote a bunch of data
  ASSERT_GE(a1_.GetPacketsSent(0), 40);
  //ASSERT_GE(a2_.GetPacketsSent(0), 40);
  //ASSERT_GE(a1_.GetPacketsReceived(0), 40);
  ASSERT_GE(a2_.GetPacketsReceived(0), 40);
}

TEST_F(SignalingTest, FullCallAudioOnly)
{
  sipcc::MediaConstraints constraints;
  OfferAnswer(constraints, constraints, OFFER_AUDIO | ANSWER_AUDIO,
              true, SHOULD_SENDRECV_AUDIO, SHOULD_SENDRECV_AUDIO);

  PR_Sleep(kDefaultTimeout * 2); // Wait for some data to get written

  a1_.CloseSendStreams();
  a2_.CloseReceiveStreams();
  // Check that we wrote a bunch of data
  ASSERT_GE(a1_.GetPacketsSent(0), 40);
  //ASSERT_GE(a2_.GetPacketsSent(0), 40);
  //ASSERT_GE(a1_.GetPacketsReceived(0), 40);
  ASSERT_GE(a2_.GetPacketsReceived(0), 40);
}

TEST_F(SignalingTest, FullCallVideoOnly)
{
  sipcc::MediaConstraints constraints;
  OfferAnswer(constraints, constraints, OFFER_VIDEO | ANSWER_VIDEO,
              true, SHOULD_SENDRECV_VIDEO, SHOULD_SENDRECV_VIDEO);

  PR_Sleep(kDefaultTimeout * 2); // Wait for some data to get written

  a1_.CloseSendStreams();
  a2_.CloseReceiveStreams();

  // FIXME -- Ideally we would check that packets were sent
  // and received; however, the test driver setup does not
  // currently support sending/receiving with Fake_VideoStreamSource.
  //
  // Check that we wrote a bunch of data
  // ASSERT_GE(a1_.GetPacketsSent(0), 40);
  //ASSERT_GE(a2_.GetPacketsSent(0), 40);
  //ASSERT_GE(a1_.GetPacketsReceived(0), 40);
  // ASSERT_GE(a2_.GetPacketsReceived(0), 40);
}

TEST_F(SignalingTest, OfferModifiedAnswer)
{
  sipcc::MediaConstraints constraints;
  OfferModifiedAnswer(constraints, constraints, SHOULD_SENDRECV_AV,
                      SHOULD_SENDRECV_AV);
  PR_Sleep(kDefaultTimeout * 2); // Wait for completion
  a1_.CloseSendStreams();
  a2_.CloseReceiveStreams();
}

TEST_F(SignalingTest, FullCallTrickle)
{
  sipcc::MediaConstraints constraints;
  OfferAnswerTrickle(constraints, constraints,
                     SHOULD_SENDRECV_AV, SHOULD_SENDRECV_AV);

  std::cerr << "ICE handshake completed" << std::endl;
  PR_Sleep(kDefaultTimeout * 2); // Wait for some data to get written

  a1_.CloseSendStreams();
  a2_.CloseReceiveStreams();
  ASSERT_GE(a1_.GetPacketsSent(0), 40);
  ASSERT_GE(a2_.GetPacketsReceived(0), 40);
}

// This test comes from Bug 810220
TEST_F(SignalingTest, AudioOnlyG711Call)
{
  sipcc::MediaConstraints constraints;
  std::string offer =
    "v=0\r\n"
    "o=- 1 1 IN IP4 148.147.200.251\r\n"
    "s=-\r\n"
    "b=AS:64\r\n"
    "t=0 0\r\n"
    "a=fingerprint:sha-256 F3:FA:20:C0:CD:48:C4:5F:02:5F:A5:D3:21:D0:2D:48:"
      "7B:31:60:5C:5A:D8:0D:CD:78:78:6C:6D:CE:CC:0C:67\r\n"
    "m=audio 9000 RTP/AVP 0 8 126\r\n"
    "c=IN IP4 148.147.200.251\r\n"
    "b=TIAS:64000\r\n"
    "a=rtpmap:0 PCMU/8000\r\n"
    "a=rtpmap:8 PCMA/8000\r\n"
    "a=rtpmap:126 telephone-event/8000\r\n"
    "a=candidate:0 1 udp 2130706432 148.147.200.251 9000 typ host\r\n"
    "a=candidate:0 2 udp 2130706432 148.147.200.251 9005 typ host\r\n"
    "a=ice-ufrag:cYuakxkEKH+RApYE\r\n"
    "a=ice-pwd:bwtpzLZD+3jbu8vQHvEa6Xuq\r\n"
    "a=sendrecv\r\n";

  std::cout << "Setting offer to:" << std::endl << offer << std::endl;
  a2_.SetRemote(TestObserver::OFFER, offer);

  std::cout << "Creating answer:" << std::endl;
  a2_.CreateAnswer(constraints, offer, OFFER_AUDIO | ANSWER_AUDIO,
                   DONT_CHECK_AUDIO | DONT_CHECK_VIDEO);

  std::string answer = a2_.answer();

  // They didn't offer opus, so our answer shouldn't include it.
  ASSERT_EQ(answer.find(" opus/"), std::string::npos);

  // They also didn't offer video or application
  ASSERT_EQ(answer.find("video"), std::string::npos);
  ASSERT_EQ(answer.find("application"), std::string::npos);

  // We should answer with PCMU and telephone-event
  ASSERT_NE(answer.find(" PCMU/8000"), std::string::npos);
  ASSERT_NE(answer.find(" telephone-event/8000"), std::string::npos);

  // Double-check the directionality
  ASSERT_NE(answer.find("\r\na=sendrecv"), std::string::npos);

}

// This test comes from Bug814038
TEST_F(SignalingTest, ChromeOfferAnswer)
{
  sipcc::MediaConstraints constraints;

  // This is captured SDP from an early interop attempt with Chrome.
  std::string offer =
    "v=0\r\n"
    "o=- 1713781661 2 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=group:BUNDLE audio video\r\n"

    "m=audio 1 RTP/SAVPF 103 104 111 0 8 107 106 105 13 126\r\n"
    "a=fingerprint:sha-1 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:"
      "5D:49:6B:19:E5:7C:AB\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:1 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:lBrbdDfrVBH1cldN\r\n"
    "a=ice-pwd:rzh23jet4QpCaEoj9Sl75pL3\r\n"
    "a=ice-options:google-ice\r\n"
    "a=sendrecv\r\n"
    "a=mid:audio\r\n"
    "a=rtcp-mux\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 inline:"
      "RzrYlzpkTsvgYFD1hQqNCzQ7y4emNLKI1tODsjim\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=rtpmap:104 ISAC/32000\r\n"
    // NOTE: the actual SDP that Chrome sends at the moment
    // doesn't indicate two channels. I've amended their SDP
    // here, under the assumption that the constraints
    // described in draft-spittka-payload-rtp-opus will
    // eventually be implemented by Google.
    "a=rtpmap:111 opus/48000/2\r\n"
    "a=rtpmap:0 PCMU/8000\r\n"
    "a=rtpmap:8 PCMA/8000\r\n"
    "a=rtpmap:107 CN/48000\r\n"
    "a=rtpmap:106 CN/32000\r\n"
    "a=rtpmap:105 CN/16000\r\n"
    "a=rtpmap:13 CN/8000\r\n"
    "a=rtpmap:126 telephone-event/8000\r\n"
    "a=ssrc:661333377 cname:KIXaNxUlU5DP3fVS\r\n"
    "a=ssrc:661333377 msid:A5UL339RyGxT7zwgyF12BFqesxkmbUsaycp5 a0\r\n"
    "a=ssrc:661333377 mslabel:A5UL339RyGxT7zwgyF12BFqesxkmbUsaycp5\r\n"
    "a=ssrc:661333377 label:A5UL339RyGxT7zwgyF12BFqesxkmbUsaycp5a0\r\n"

    "m=video 1 RTP/SAVPF 100 101 102\r\n"
    "a=fingerprint:sha-1 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:"
      "6B:19:E5:7C:AB\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:1 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:lBrbdDfrVBH1cldN\r\n"
    "a=ice-pwd:rzh23jet4QpCaEoj9Sl75pL3\r\n"
    "a=ice-options:google-ice\r\n"
    "a=sendrecv\r\n"
    "a=mid:video\r\n"
    "a=rtcp-mux\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 inline:"
      "RzrYlzpkTsvgYFD1hQqNCzQ7y4emNLKI1tODsjim\r\n"
    "a=rtpmap:100 VP8/90000\r\n"
    "a=rtpmap:101 red/90000\r\n"
    "a=rtpmap:102 ulpfec/90000\r\n"
    "a=ssrc:3012607008 cname:KIXaNxUlU5DP3fVS\r\n"
    "a=ssrc:3012607008 msid:A5UL339RyGxT7zwgyF12BFqesxkmbUsaycp5 v0\r\n"
    "a=ssrc:3012607008 mslabel:A5UL339RyGxT7zwgyF12BFqesxkmbUsaycp5\r\n"
    "a=ssrc:3012607008 label:A5UL339RyGxT7zwgyF12BFqesxkmbUsaycp5v0\r\n";


  std::cout << "Setting offer to:" << std::endl << offer << std::endl;
  a2_.SetRemote(TestObserver::OFFER, offer);

  std::cout << "Creating answer:" << std::endl;
  a2_.CreateAnswer(constraints, offer, OFFER_AUDIO | ANSWER_AUDIO);

  std::string answer = a2_.answer();
}

TEST_F(SignalingTest, OfferAllDynamicTypes)
{
  sipcc::MediaConstraints constraints;
  std::string offer;
  for (int i = 96; i < 128; i++)
  {
    std::stringstream ss;
    ss << i;
    std::cout << "Trying dynamic pt = " << i << std::endl;
    offer =
      "v=0\r\n"
      "o=- 1 1 IN IP4 148.147.200.251\r\n"
      "s=-\r\n"
      "b=AS:64\r\n"
      "t=0 0\r\n"
      "a=fingerprint:sha-256 F3:FA:20:C0:CD:48:C4:5F:02:5F:A5:D3:21:D0:2D:48:"
        "7B:31:60:5C:5A:D8:0D:CD:78:78:6C:6D:CE:CC:0C:67\r\n"
      "m=audio 9000 RTP/AVP " + ss.str() + "\r\n"
      "c=IN IP4 148.147.200.251\r\n"
      "b=TIAS:64000\r\n"
      "a=rtpmap:" + ss.str() +" opus/48000/2\r\n"
      "a=candidate:0 1 udp 2130706432 148.147.200.251 9000 typ host\r\n"
      "a=candidate:0 2 udp 2130706432 148.147.200.251 9005 typ host\r\n"
      "a=ice-ufrag:cYuakxkEKH+RApYE\r\n"
      "a=ice-pwd:bwtpzLZD+3jbu8vQHvEa6Xuq\r\n"
      "a=sendrecv\r\n";

      //std::cout << "Setting offer to:" << std::endl << offer << std::endl;
      a2_.SetRemote(TestObserver::OFFER, offer);

      //std::cout << "Creating answer:" << std::endl;
      a2_.CreateAnswer(constraints, offer, OFFER_AUDIO | ANSWER_AUDIO);

      std::string answer = a2_.answer();

      ASSERT_NE(answer.find(ss.str() + " opus/"), std::string::npos);
  }

}

TEST_F(SignalingTest, OfferAnswerCheckDescriptions)
{
  sipcc::MediaConstraints constraints;
  OfferAnswer(constraints, constraints, OFFER_AV | ANSWER_AV, true,
              SHOULD_SENDRECV_AV, SHOULD_SENDRECV_AV);

  std::cout << "Caller's LocalDescription: " << std::endl <<
    a1_.getLocalDescription() << std::endl << std::endl;
  std::cout << "Caller's Remote Description: " << std::endl <<
    a1_.getRemoteDescription() << std::endl << std::endl;
  std::cout << "Callee's LocalDescription: " << std::endl <<
    a2_.getLocalDescription() << std::endl << std::endl;
  std::cout << "Callee's Remote Description: " << std::endl <<
    a2_.getRemoteDescription() << std::endl << std::endl;

  ASSERT_EQ(a1_.getLocalDescription(),a2_.getRemoteDescription());
  ASSERT_EQ(a2_.getLocalDescription(),a1_.getRemoteDescription());
}

TEST_F(SignalingTest, CheckTrickleSdpChange)
{
  sipcc::MediaConstraints constraints;
  OfferAnswerTrickle(constraints, constraints,
                     SHOULD_SENDRECV_AV, SHOULD_SENDRECV_AV);
  std::cerr << "ICE handshake completed" << std::endl;

  PR_Sleep(kDefaultTimeout * 2); // Wait for some data to get written
  a1_.CloseSendStreams();
  a2_.CloseReceiveStreams();

  std::cout << "Caller's LocalDescription: " << std::endl <<
    a1_.getLocalDescription() << std::endl << std::endl;
  std::cout << "Caller's Remote Description: " << std::endl <<
    a1_.getRemoteDescription() << std::endl << std::endl;
  std::cout << "Callee's LocalDescription: " << std::endl <<
    a2_.getLocalDescription() << std::endl << std::endl;
  std::cout << "Callee's Remote Description: " << std::endl <<
    a2_.getRemoteDescription() << std::endl << std::endl;

  ASSERT_NE(a1_.getLocalDescription().find("\r\na=candidate"), string::npos);
  ASSERT_NE(a1_.getRemoteDescription().find("\r\na=candidate"), string::npos);
  ASSERT_NE(a2_.getLocalDescription().find("\r\na=candidate"), string::npos);
  ASSERT_NE(a2_.getRemoteDescription().find("\r\na=candidate"), string::npos);
  ASSERT_EQ(a1_.getLocalDescription(),a2_.getRemoteDescription());
  ASSERT_EQ(a2_.getLocalDescription(),a1_.getRemoteDescription());
}

} // End namespace test.

int main(int argc, char **argv) {
  // This test can cause intermittent oranges on the builders
  CHECK_ENVIRONMENT_FLAG("MOZ_WEBRTC_TESTS")

  test_utils = new MtransportTestUtils();
  NSS_NoDB_Init(NULL);
  NSS_SetDomesticPolicy();

  ::testing::InitGoogleTest(&argc, argv);

  for(int i=0; i<argc; i++) {
    if (!strcmp(argv[i],"-t")) {
      kDefaultTimeout = 20000;
    }
  }

  ::testing::AddGlobalTestEnvironment(new test::SignalingEnvironment);
  int result = RUN_ALL_TESTS();
  delete test_utils;
  return result;
}
