/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CC_Common.h"

#include "CSFLogStream.h"

#include "CSFMediaProvider.h"
#include "CSFAudioTermination.h"
#include "CSFVideoTermination.h"
#include "MediaConduitErrors.h"
#include "MediaConduitInterface.h"
#include "MediaPipeline.h"
#include "VcmSIPCCBinding.h"
#include "csf_common.h"
#include "PeerConnectionImpl.h"
#include "nsThreadUtils.h"
#include "transportflow.h"
#include "transportlayer.h"
#include "transportlayerdtls.h"
#include "transportlayerice.h"
#include "runnable_utils.h"
#include "cpr_stdlib.h"
#include "cpr_string.h"

#include <stdlib.h>
#include <stdio.h>
#include <ssl.h>
#include <sslproto.h>

extern "C" {
#include "ccsdp.h"
#include "vcm.h"
#include "cip_mmgr_mediadefinitions.h"
#include "cip_Sipcc_CodecMask.h"

extern void lsm_start_multipart_tone_timer (vcm_tones_t tone,
                                            uint32_t delay,
                                            cc_call_handle_t callId);
extern void lsm_start_continuous_tone_timer (vcm_tones_t tone,
                                             uint32_t delay,
                                             cc_call_handle_t callId);
extern void lsm_update_active_tone(vcm_tones_t tone, cc_call_handle_t call_handle);
extern void lsm_stop_multipart_tone_timer(void);
extern void lsm_stop_continuous_tone_timer(void);

}//end extern "C"

static const char* logTag = "VcmSipccBinding";

typedef enum {
    CC_AUDIO_1,
    CC_VIDEO_1
} cc_media_cap_name;

#define SIPSDP_ILBC_MODE20 20

/* static */

using namespace CSF;

VcmSIPCCBinding * VcmSIPCCBinding::gSelf = NULL;
int VcmSIPCCBinding::gAudioCodecMask = 0;
int VcmSIPCCBinding::gVideoCodecMask = 0;
nsIThread *VcmSIPCCBinding::gMainThread = NULL;

static mozilla::RefPtr<TransportFlow> vcmCreateTransportFlow(sipcc::PeerConnectionImpl *pc,
                                                             int level, bool rtcp,
                                                             const char *fingerprint_alg,
                                                             const char *fingerprint
                                                             );

// Convenience macro to acquire PC

#define ENSURE_PC(pc, errval) \
  do { \
    if (!pc.impl()) {                                                 \
      CSFLogDebug(logTag, "%s: couldn't acquire peerconnection %s", __FUNCTION__, peerconnection); \
      return errval; \
    }         \
  } while(0)

VcmSIPCCBinding::VcmSIPCCBinding ()
  : streamObserver(NULL)
{
    delete gSelf;//delete is NULL safe, so I don't need to check if it's NULL
    gSelf = this;
}

VcmSIPCCBinding::~VcmSIPCCBinding ()
{
    assert(gSelf);
    gSelf = NULL;
}

void VcmSIPCCBinding::setStreamObserver(StreamObserver* obs)
{
	streamObserver = obs;
}

/* static */
StreamObserver * VcmSIPCCBinding::getStreamObserver()
{
    if (gSelf != NULL)
    	return gSelf->streamObserver;

    return NULL;
}

void VcmSIPCCBinding::setMediaProviderObserver(MediaProviderObserver* obs)
{
	mediaProviderObserver = obs;
}


MediaProviderObserver * VcmSIPCCBinding::getMediaProviderObserver()
{
    if (gSelf != NULL)
    	return gSelf->mediaProviderObserver;

    return NULL;
}

void VcmSIPCCBinding::setAudioCodecs(int codecMask)
{
  CSFLogDebugS(logTag, "SETTING AUDIO: " << codecMask);
  VcmSIPCCBinding::gAudioCodecMask = codecMask;
}

void VcmSIPCCBinding::setVideoCodecs(int codecMask)
{
  CSFLogDebugS(logTag, "SETTING VIDEO: " << codecMask);
  VcmSIPCCBinding::gVideoCodecMask = codecMask;
}

int VcmSIPCCBinding::getAudioCodecs()
{
  return VcmSIPCCBinding::gAudioCodecMask;
}

int VcmSIPCCBinding::getVideoCodecs()
{
  return VcmSIPCCBinding::gVideoCodecMask;
}

void VcmSIPCCBinding::setMainThread(nsIThread *thread)
{
  gMainThread = thread;
}

nsIThread* VcmSIPCCBinding::getMainThread()
{
  return gMainThread;
}

/* static */
AudioTermination * VcmSIPCCBinding::getAudioTermination()
{
    // commenting as part of media provider removal
    return NULL;
}

/* static */
VideoTermination * VcmSIPCCBinding::getVideoTermination()
{
    // commenting as part of media provider removal
    return NULL;
}

/* static */
AudioControl * VcmSIPCCBinding::getAudioControl()
{
    // commenting as part of media provider removal
    return NULL;
}

/* static */
VideoControl * VcmSIPCCBinding::getVideoControl()
{
    // commenting as part of media provider removal
    return NULL;
}

/*
 * Used to play busy verfication tone
 */
#define BUSY_VERIFICATION_DELAY       (10000)

/*
 * Temp until added to config
 */

#define TOH_DELAY                (10000)/*
 * Used to play msg waiting and stutter dialtones.
 * Both tones are 100ms on/off repeating 10 and
 * 3 times respectively, followed by steady dialtone.
 * Due to DSP limitations we first tell the DSP to
 * play the 100ms on/off pairs the correct number of
 * times, set a timer, and then tell it to play dialtone.
 */
#define MSG_WAITING_DELAY   (2050)
#define STUTTER_DELAY       (650)

/*
 * Used to play the busy verfication tone which
 * is two seconds of dialtone followed by the
 * callwaiting tone every ten seconds.
 */
#define BUSY_VERIFY_DELAY   (12000)


#define VCM_MIN_VOLUME_BYTE       0
#define VCM_MAX_VOLUME_BYTE       255
#define VCM_MIN_VOLUME_LEVEL      0
#define VCM_MAX_VOLUME_LEVEL      240
#define VCM_RNG_MAX_VOLUME_LEVEL  248
#define VCM_DEFAULT_AUDIO_VOLUME  144
#define VCM_DEFAULT_RINGER_VOLUME 80
#define VCM_VOLUME_ADJUST_LEVEL   8


extern "C" {


/* MediaTermination APIs expect dynmic payload type in upper 16 bits and
   codec type in lower 16 bits. */
#define CREATE_MT_MAP(a,b)        ((a << 16) | b)
#define DYNAMIC_PAYLOAD_TYPE(x)    ((x >> 16) & 0xFFFF)

#define    MAX_SPROP_LEN    32

#define VCM_ERROR -1

struct h264_video
{
    char       sprop_parameter_set[MAX_SPROP_LEN];
    int        packetization_mode;
    int        profile_level_id;
    int        max_mbps;
    int        max_fs;
    int        max_cpb;
    int        max_dpb;
    int        max_br;
    int        tias_bw;
};

/**
 *  start/stop ringing
 *
 *  @param[in] ringMode   - VCM ring mode (ON/OFF)
 *  @param[in] once       - type of ring - continuous or one shot.
 *  @param[in] alert_info - header specified ring mode.
 *  @param[in] line       - the line on which to start/stop ringing
 *
 *  @return    void
 */

void vcmControlRinger (vcm_ring_mode_t ringMode,
                       short once,
                       cc_boolean alert_info,
                       int line,
                       cc_callid_t call_id)
{
    const char fname[] = "vcmControlRinger";

    CSFLogDebug( logTag, "%s: ringMode=%d once=%d", fname, ringMode, once);

    /* we need to pass the line parameter for this */

#if 0
     // <emannion> part of media provider removal
    if ( ringMode == VCM_RING_OFF )
    {
        //call media_ring_stop
        if ( VcmSIPCCBinding::getAudioTermination()->ringStop( line ) != 0 )
        {
            CSFLogDebug( logTag, "%s: mediaRingStop failed", fname);
        }
    }
    else if ( VcmSIPCCBinding::getAudioTermination()->ringStart( line, map_ring_mode(ringMode), (once != 0) ) != 0 )
    {
        CSFLogDebug( logTag, "%s: mediaRingStart failed", fname);
    }
#endif
}

/**
 *  To be Deprecated
 *  This may be needed to be implemented if the DSP doesn't automatically enable the side tone
 *  The stack will make a call to this method based on the call state. Provide a stub if this is not needed.
 *
 *  @param[in] side_tone - cc_boolean to enable/disable side tone
 *
 *  @return void
 *
 */

void vcmEnableSidetone(cc_uint16_t side_tone)
{
    /* NOT REQD for TNP */
    CSFLogDebug( logTag, "vcmEnableSidetone: vcmEnableSidetone(): called");
}

/*
 *  Function:map_algorithmID
 *
 *  Parameters:algorithmID
 *
 *  Description: Converts VCM algorithmID type to media layer defs.
 *
 *  Returns:algorithmID value corresponding to VCM algorithmID
 *
 */
static EncryptionAlgorithm
map_algorithmID( vcm_crypto_algorithmID algorithmID )
{
    switch ( algorithmID )
    {
    case VCM_AES_128_COUNTER:
        return EncryptionAlgorithm_AES_128_COUNTER;

    default:
        return EncryptionAlgorithm_NONE;
    }
}

/*
 *  Function:vcm_init
 *
 *  Parameters:none
 *
 *  Description: Nothing to do for TNP.
 *
 *  Returns:none
 *
 */
void vcmInit (void)
{
    CSFLogDebug( logTag, "vcmInit() called.");
}

/**
 * The unloading of the VCM module
 */
void vcmUnload (void)
{
    CSFLogDebug( logTag, "vcmUnload() called.");
}

/**
 *  Allocate(Reserve) a receive port.
 *
 *  @param[in]  mcap_id - Media Capability ID
 *  @param[in]  group_id - group identifier to which stream belongs.
 *  @param[in]  stream_id - stream identifier
 *  @param[in]  call_handle  - call identifier
 *  @param[in]  port_requested - port requested (if zero -> give any)
 *  @param[out]  port_allocated - port that was actually allocated.
 *
 *  @return    void
 *
 */

void vcmRxAllocPort(cc_mcapid_t mcap_id,
                    cc_groupid_t group_id,
                    cc_streamid_t stream_id,
                    cc_call_handle_t  call_handle,
                    cc_uint16_t port_requested,
                    int *port_allocated)
{
    CSFLogDebug( logTag, "vcmRxAllocPort(): group_id=%d stream_id=%d call_handle=%d port_requested = %d",
        group_id, stream_id, call_handle, port_requested);

    // Not in SDP/PeerConnection mode
    int port = -1;
    bool isVideo = false;
    if(CC_IS_AUDIO(mcap_id))
    {
      isVideo = false;
      if ( VcmSIPCCBinding::getAudioTermination() != NULL )
        port = VcmSIPCCBinding::getAudioTermination()->rxAlloc( group_id, stream_id, port_requested );
    }
    else if(CC_IS_VIDEO(mcap_id))
    {
      isVideo = true;
      if ( VcmSIPCCBinding::getVideoTermination() != NULL )
        port = VcmSIPCCBinding::getVideoTermination()->rxAlloc( group_id, stream_id, port_requested );
    }

    StreamObserver* obs = VcmSIPCCBinding::getStreamObserver();
    if(obs != NULL)
      obs->registerStream(call_handle, stream_id, isVideo);

    CSFLogDebug( logTag, "vcmRxAllocPort(): allocated port %d", port);
    *port_allocated = port;
}


/**
 *  Gets the ICE parameters for a stream. Called "alloc" for style consistency
 *
 *  @param[in]  mcap_id - Media Capability ID
 *  @param[in]  group_id - group identifier to which stream belongs.
 *  @param[in]  stream_id - stream identifier
 *  @param[in]  call_handle  - call identifier
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[out] default_addrp - the ICE default addr
 *  @param[out] port_allocatedp - the ICE default port
 *  @param[out] candidatesp - the ICE candidate array
 *  @param[out] candidate_ctp length of the array
 *
 *  @return    void
 *
 */
static void vcmRxAllocICE_m(cc_mcapid_t mcap_id,
                            cc_groupid_t group_id,
			    cc_streamid_t stream_id,
			    cc_call_handle_t  call_handle,
			    const char *peerconnection,
			    uint16_t level,
			    char **default_addrp, /* Out */
			    int *default_portp, /* Out */
			    char ***candidatesp, /* Out */
			    int *candidate_ctp /* Out */
)
{
  *default_portp = -1;

  CSFLogDebug( logTag, "%s: group_id=%d stream_id=%d call_handle=%d PC = %s",
    __FUNCTION__, group_id, stream_id, call_handle, peerconnection);

  // Note: we don't acquire any media resources here, and we assume that the
  // ICE streams already exist, so we're just acquiring them. Any logic
  // to make them on demand is elsewhere.
  sipcc::PeerConnectionWrapper pc(peerconnection);
  ENSURE_PC(pc, /**/);

  CSFLogDebug( logTag, "%s: Getting stream %d", __FUNCTION__, level);
  mozilla::RefPtr<NrIceMediaStream> stream = pc.impl()->media()->
    ice_media_stream(level-1);
  MOZ_ASSERT(stream);
  if (!stream) {
    return;
  }

  std::vector<std::string> candidates = stream->GetCandidates();
  CSFLogDebug( logTag, "%s: Got %d candidates", __FUNCTION__, candidates.size());

  std::string default_addr;
  int default_port;

  nsresult res = stream->GetDefaultCandidate(1, &default_addr, &default_port);
  MOZ_ASSERT(NS_SUCCEEDED(res));
  if (!NS_SUCCEEDED(res)) {
    return;
  }

  CSFLogDebug( logTag, "%s: Got default candidates %s:%d", __FUNCTION__,
    default_addr.c_str(), default_port);

  // Note: this leaks memory if we are out of memory. Oh well.
  *candidatesp = (char **) cpr_malloc(candidates.size() * sizeof(char *));
  if (!(*candidatesp))
    return;

  for (size_t i=0; i<candidates.size(); i++) {
    (*candidatesp)[i] = (char *) cpr_malloc(candidates[i].size() + 1);
    sstrncpy((*candidatesp)[i], candidates[i].c_str(), candidates[i].size() + 1);
  }
  *candidate_ctp = candidates.size();

  // Copy the default address
  *default_addrp = (char *) cpr_malloc(default_addr.size() + 1);
  if (!*default_addrp)
    return;
  sstrncpy(*default_addrp, default_addr.c_str(), default_addr.size() + 1);
  *default_portp = default_port; /* This is the signal that things are cool */
}


/**
 *  Gets the ICE parameters for a stream. Called "alloc" for style consistency
 *
 *  This is a thunk to vcmRxAllocICE_m
 *
 *  @param[in]  group_id - group identifier to which stream belongs.
 *  @param[in]  stream_id - stream identifier
 *  @param[in]  call_handle  - call identifier
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[out] default_addrp - the ICE default addr
 *  @param[out] port_allocatedp - the ICE default port
 *  @param[out] candidatesp - the ICE candidate array
 *  @param[out] candidate_ctp length of the array
 *
  *  @return    void
 *
 */
void vcmRxAllocICE(cc_mcapid_t mcap_id,
                   cc_groupid_t group_id,
                   cc_streamid_t stream_id,
                   cc_call_handle_t  call_handle,
                   const char *peerconnection,
                   uint16_t level,
                   char **default_addrp, /* Out */
                   int *default_portp, /* Out */
                   char ***candidatesp, /* Out */
                   int *candidate_ctp /* Out */
                   )
{
  VcmSIPCCBinding::getMainThread()->Dispatch(
      WrapRunnableNM(&vcmRxAllocICE_m,
                     mcap_id,
                     group_id,
                     stream_id,
                     call_handle,
                     peerconnection,
                     level,
                     default_addrp,
                     default_portp,
                     candidatesp,
                     candidate_ctp),
      NS_DISPATCH_SYNC);
}

/* Get ICE global parameters (ufrag and pwd)
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[out] ufragp - where to put the ufrag
 *  @param[out] pwdp - where to put the pwd
 *
 *  @return void
 */
static void vcmGetIceParams_m(const char *peerconnection,
                              char **ufragp,
                              char **pwdp)
{
  CSFLogDebug( logTag, "%s: PC = %s", __FUNCTION__, peerconnection);

  *ufragp = *pwdp = NULL;

 // Note: we don't acquire any media resources here, and we assume that the
  // ICE streams already exist, so we're just acquiring them. Any logic
  // to make them on demand is elsewhere.
  sipcc::PeerConnectionWrapper pc(peerconnection);
  ENSURE_PC(pc, /**/);

  std::vector<std::string> attrs = pc.impl()->media()->
    ice_ctx()->GetGlobalAttributes();

  // Now fish through these looking for a ufrag and passwd
  char *ufrag = NULL;
  char *pwd = NULL;

  for (size_t i=0; i<attrs.size(); i++) {
    if (attrs[i].compare(0, strlen("ice-ufrag:"), "ice-ufrag:") == 0) {
      if (!ufrag) {
        ufrag = (char *) cpr_malloc(attrs[i].size() + 1);
        if (!ufrag)
          return;
        sstrncpy(ufrag, attrs[i].c_str(), attrs[i].size() + 1);
        ufrag[attrs[i].size()] = 0;
      }
    }

    if (attrs[i].compare(0, strlen("ice-pwd:"), "ice-pwd:") == 0) {
      pwd = (char *) cpr_malloc(attrs[i].size() + 1);
      if (!pwd)
        return;
      sstrncpy(pwd, attrs[i].c_str(), attrs[i].size() + 1);
      pwd[attrs[i].size()] = 0;
    }

  }
  if (!ufrag || !pwd) {
    MOZ_ASSERT(PR_FALSE);
    cpr_free(ufrag);
    cpr_free(pwd);
    CSFLogDebug( logTag, "%s: no ufrag or password", __FUNCTION__);
    return;
  }

  *ufragp = ufrag;
  *pwdp = pwd;

  return;
}

/* Get ICE global parameters (ufrag and pwd)
 *
 * This is a thunk to vcmGetIceParams_m.
 *
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[out] ufragp - where to put the ufrag
 *  @param[out] pwdp - where to put the pwd
 *
 *  @return void
 */
void vcmGetIceParams(const char *peerconnection,
                     char **ufragp,
                     char **pwdp)
{
  VcmSIPCCBinding::getMainThread()->Dispatch(
      WrapRunnableNM(&vcmGetIceParams_m,
                     peerconnection,
                     ufragp,
                     pwdp),
      NS_DISPATCH_SYNC);
}


/* Set remote ICE global parameters.
 *
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[in]  ufrag - the ufrag
 *  @param[in]  pwd - the pwd
 *
 *  @return 0 success, error failure
 */
static short vcmSetIceSessionParams_m(const char *peerconnection,
                                      char *ufrag,
                                      char *pwd)
{
  CSFLogDebug( logTag, "%s: PC = %s", __FUNCTION__, peerconnection);

  sipcc::PeerConnectionWrapper pc(peerconnection);
  ENSURE_PC(pc, VCM_ERROR);

  std::vector<std::string> attributes;

  if (ufrag)
    attributes.push_back(ufrag);
  if (pwd)
    attributes.push_back(pwd);

  nsresult res = pc.impl()->media()->ice_ctx()->
    ParseGlobalAttributes(attributes);

  if (!NS_SUCCEEDED(res)) {
    CSFLogError( logTag, "%s: couldn't parse global parameters", __FUNCTION__ );
    return VCM_ERROR;
  }

  return 0;
}

/* Set remote ICE global parameters.
 *
 * This is a thunk to vcmSetIceSessionParams_m.
 *
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[in]  ufrag - the ufrag
 *  @param[in]  pwd - the pwd
 *
 *  @return 0 success, error failure
 */
short vcmSetIceSessionParams(const char *peerconnection,
                             char *ufrag,
                             char *pwd)
{
  short ret;

  VcmSIPCCBinding::getMainThread()->Dispatch(
      WrapRunnableNMRet(&vcmSetIceSessionParams_m,
                        peerconnection,
                        ufrag,
                        pwd,
                        &ret),
      NS_DISPATCH_SYNC);

  return ret;
}

/* Set ice candidate for trickle ICE.
 *
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[in]  icecandidate - the icecandidate
 *  @param[in]  level - the m line level
 *
 *  @return 0 success, error failure
 */
static short vcmSetIceCandidate_m(const char *peerconnection,
                                  const char *icecandidate,
                                  uint16_t level)
{
  CSFLogDebug( logTag, "%s: PC = %s", __FUNCTION__, peerconnection);

  sipcc::PeerConnectionWrapper pc(peerconnection);
  ENSURE_PC(pc, VCM_ERROR);

  CSFLogDebug( logTag, "%s(): Getting stream %d", __FUNCTION__, level);
  mozilla::RefPtr<NrIceMediaStream> stream = pc.impl()->media()->
    ice_media_stream(level-1);
  if (!stream)
    return VCM_ERROR;

  nsresult res;
  nsresult rv = pc.impl()->media()->ice_ctx()->thread()->Dispatch(
    WrapRunnableRet(stream, &NrIceMediaStream::ParseTrickleCandidate, icecandidate, &res),
    NS_DISPATCH_SYNC);

  if (!NS_SUCCEEDED(rv)) {
    CSFLogError( logTag, "%s(): Could not dispatch to ICE thread", __FUNCTION__, level);
    return VCM_ERROR;
  }

  if (!NS_SUCCEEDED(res)) {
    CSFLogError( logTag, "%s(): Could not parse trickle candidate for stream %d", __FUNCTION__, level);
    return VCM_ERROR;
  }

  return 0;
}

/* Set ice candidate for trickle ICE.
 *
 * This is a thunk to vcmSetIceCandidate_m
 *
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[in]  icecandidate - the icecandidate
 *  @param[in]  level - the m line level
 *
 *  @return 0 success, error failure
 */
short vcmSetIceCandidate(const char *peerconnection,
                         const char *icecandidate,
                         uint16_t level)
{
  short ret;

  VcmSIPCCBinding::getMainThread()->Dispatch(
      WrapRunnableNMRet(&vcmSetIceCandidate_m,
                        peerconnection,
                        icecandidate,
                        level,
                        &ret),
      NS_DISPATCH_SYNC);

  return ret;
}


/* Start ICE checks
 *  @param[in]  peerconnection - the peerconnection in use
 *  @return 0 success, error failure
 */
static short vcmStartIceChecks_m(const char *peerconnection, cc_boolean isControlling)
{
  CSFLogDebug( logTag, "%s: PC = %s", __FUNCTION__, peerconnection);

  sipcc::PeerConnectionWrapper pc(peerconnection);  
  ENSURE_PC(pc, VCM_ERROR);

  nsresult res;
  res = pc.impl()->media()->ice_ctx()->SetControlling(
      isControlling ? NrIceCtx::ICE_CONTROLLING : NrIceCtx::ICE_CONTROLLED);
  if (!NS_SUCCEEDED(res)) {
    CSFLogError( logTag, "%s: couldn't set controlling", __FUNCTION__ );
    return VCM_ERROR;
  }
  nsresult rv = pc.impl()->media()->ice_ctx()->thread()->Dispatch(
    WrapRunnableRet(pc.impl()->media()->ice_ctx(), &NrIceCtx::StartChecks, &res),
      NS_DISPATCH_SYNC);

  if (!NS_SUCCEEDED(rv)) {
    CSFLogError( logTag, "%s(): Could not dispatch to ICE thread", __FUNCTION__);
    return VCM_ERROR;
  }
  if (!NS_SUCCEEDED(res)) {
    CSFLogError( logTag, "%s: couldn't start ICE checks", __FUNCTION__ );
    return VCM_ERROR;
  }
  return 0;
}


/* Start ICE checks
 *
 * This is a thunk to vcmStartIceChecks_m
 *
 *  @param[in]  peerconnection - the peerconnection in use
 *  @return 0 success, error failure
 */
short vcmStartIceChecks(const char *peerconnection, cc_boolean isControlling)
{
  short ret;

  VcmSIPCCBinding::getMainThread()->Dispatch(
      WrapRunnableNMRet(&vcmStartIceChecks_m,
                        peerconnection,
                        isControlling,
                        &ret),
      NS_DISPATCH_SYNC);

  return ret;
}

/* Set remote ICE media-level parameters.
 *
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[in]  level - the m-line
 *  @param[in]  ufrag - the ufrag
 *  @param[in]  pwd - the pwd
 *  @param[in]  candidates - the candidates
 *  @param[i]   candidate_ct - the number of candidates
 *  @return 0 success, error failure
 */
static short vcmSetIceMediaParams_m(const char *peerconnection,
                                    int level,
                                    char *ufrag,
                                    char *pwd,
                                    char **candidates,
                                    int candidate_ct)
{
  CSFLogDebug( logTag, "%s: PC = %s", __FUNCTION__, peerconnection);

  sipcc::PeerConnectionWrapper pc(peerconnection);
  ENSURE_PC(pc, VCM_ERROR);

  CSFLogDebug( logTag, "%s(): Getting stream %d", __FUNCTION__, level);
  mozilla::RefPtr<NrIceMediaStream> stream = pc.impl()->media()->
    ice_media_stream(level-1);
  if (!stream)
    return VCM_ERROR;

  std::vector<std::string> attributes;

  if (ufrag)
    attributes.push_back(ufrag);
  if (pwd)
    attributes.push_back(pwd);

  for (int i = 0; i<candidate_ct; i++) {
    attributes.push_back(candidates[i]);
  }

  nsresult res = stream->ParseAttributes(attributes);

  if (!NS_SUCCEEDED(res)) {
    CSFLogError( logTag, "%s: couldn't parse global parameters", __FUNCTION__ );
    return VCM_ERROR;
  }

  return 0;
}

/* Set remote ICE media-level parameters.
 *
 * This is a thunk to vcmSetIceMediaParams_w
 *
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[in]  level - the m-line
 *  @param[in]  ufrag - the ufrag
 *  @param[in]  pwd - the pwd
 *  @param[in]  candidates - the candidates
 *  @param[i]   candidate_ct - the number of candidates
 *  @return 0 success, error failure
 */
short vcmSetIceMediaParams(const char *peerconnection,
                           int level,
                           char *ufrag,
                           char *pwd,
                           char **candidates,
                           int candidate_ct)
{
  short ret;

  VcmSIPCCBinding::getMainThread()->Dispatch(
      WrapRunnableNMRet(&vcmSetIceMediaParams_m,
                     peerconnection,
                     level,
                     ufrag,
                     pwd,
                     candidates,
                     candidate_ct,
                     &ret),
      NS_DISPATCH_SYNC);

  return ret;
}


/*
 * Create a remote stream
 *
 *  @param[in] mcap_id - group identifier to which stream belongs.
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[out] pc_stream_id - the id of the allocated stream
 *
 *  TODO(ekr@rtfm.com): Revise along with everything else for the
 *  new stream model.
 *
 *  Returns: zero(0) for success; otherwise, ERROR for failure
 */
static short vcmCreateRemoteStream_m(
  cc_mcapid_t mcap_id,
  const char *peerconnection,
  int *pc_stream_id) {
  uint32_t hints = 0;
  nsresult res;

  CSFLogDebug( logTag, "%s", __FUNCTION__);
  sipcc::PeerConnectionWrapper pc(peerconnection);
  ENSURE_PC(pc, VCM_ERROR);

  if (CC_IS_AUDIO(mcap_id)) {
    hints |= nsDOMMediaStream::HINT_CONTENTS_AUDIO;
  }
  if (CC_IS_VIDEO(mcap_id)) {
    hints |= nsDOMMediaStream::HINT_CONTENTS_VIDEO;
  }

  nsRefPtr<sipcc::RemoteSourceStreamInfo> info;
  res = pc.impl()->CreateRemoteSourceStreamInfo(hints, &info);
  if (NS_FAILED(res)) {
    return VCM_ERROR;
  }

  res = pc.impl()->media()->AddRemoteStream(info, pc_stream_id);
  if (NS_FAILED(res)) {
    return VCM_ERROR;
  }

  if (CC_IS_AUDIO(mcap_id)) {
    mozilla::AudioSegment *segment = new mozilla::AudioSegment();
    segment->Init(1); // 1 Channel
    // TODO(ekr@rtfm.com): Clean up Track IDs
    info->GetMediaStream()->GetStream()->AsSourceStream()->AddTrack(1, 16000, 0, segment);

    // We aren't going to add any more tracks
    info->GetMediaStream()->GetStream()->AsSourceStream()->
        AdvanceKnownTracksTime(mozilla::STREAM_TIME_MAX);
  }
  if (CC_IS_VIDEO(mcap_id)) {
    // AddTrack takes ownership of segment
  }

  CSFLogDebug( logTag, "%s: created remote stream with index %d hints=%d",
    __FUNCTION__, *pc_stream_id, hints);

  return 0;
}

/*
 * Create a remote stream
 *
 * This is a thunk to vcmCreateRemoteStream_m
 *
 *  @param[in] mcap_id - group identifier to which stream belongs.
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[out] pc_stream_id - the id of the allocated stream
 *
 *  TODO(ekr@rtfm.com): Revise along with everything else for the
 *  new stream model.
 *
 *  Returns: zero(0) for success; otherwise, ERROR for failure
 */
short vcmCreateRemoteStream(cc_mcapid_t mcap_id,
                            const char *peerconnection,
                            int *pc_stream_id)
{
  short ret;

  VcmSIPCCBinding::getMainThread()->Dispatch(
      WrapRunnableNMRet(&vcmCreateRemoteStream_m,
                        mcap_id,
                        peerconnection,
                        pc_stream_id,
                        &ret),
      NS_DISPATCH_SYNC);

  return ret;
}


/*
 * Get DTLS key data
 *
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[out] digest_algp    - the digest algorithm e.g. 'SHA-1'
 *  @param[in] max_digest_alg_len - length of string
 *  @param[out] digestp        - the digest string
 *  @param[in] max_digest_len - length of string
 *
 *  Returns: zero(0) for success; otherwise, ERROR for failure
 */
static short vcmGetDtlsIdentity_m(const char *peerconnection,
                                  char *digest_algp,
                                  size_t max_digest_alg_len,
                                  char *digestp,
                                  size_t max_digest_len) {
  sipcc::PeerConnectionWrapper pc(peerconnection);
  ENSURE_PC(pc, VCM_ERROR);

  unsigned char digest[TransportLayerDtls::kMaxDigestLength];
  size_t digest_len;

  nsresult res = pc.impl()->GetIdentity()->ComputeFingerprint("sha-256", digest,
                                                               sizeof(digest),
                                                               &digest_len);
  if (!NS_SUCCEEDED(res)) {
    CSFLogError( logTag, "%s: Could not compute identity fingerprint", __FUNCTION__);
    return VCM_ERROR;
  }

  // digest_len should be 32 for SHA-256
  PR_ASSERT(digest_len == 32);
  std::string fingerprint_txt = DtlsIdentity::FormatFingerprint(digest, digest_len);
  if (max_digest_len <= fingerprint_txt.size()) {
    CSFLogError( logTag, "%s: Formatted digest will not fit in provided buffer",
                 __FUNCTION__);
    return VCM_ERROR;
  }

  sstrncpy(digest_algp, "sha-256", max_digest_alg_len);
  sstrncpy(digestp, fingerprint_txt.c_str(), max_digest_len);

  return 0;
}

/*
 * Get DTLS key data
 *
 * This is a thunk to vcmGetDtlsIdentity_m
 *
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[out] digest_algp    - the digest algorithm e.g. 'SHA-1'
 *  @param[in] max_digest_alg_len - length of string
 *  @param[out] digestp        - the digest string
 *  @param[in] max_digest_len - length of string
 *
 *  Returns: zero(0) for success; otherwise, ERROR for failure
 */
short vcmGetDtlsIdentity(const char *peerconnection,
                         char *digest_algp,
                         size_t max_digest_alg_len,
                         char *digestp,
                         size_t max_digest_len) {
  short ret;

  VcmSIPCCBinding::getMainThread()->Dispatch(
      WrapRunnableNMRet(&vcmGetDtlsIdentity_m,
                        peerconnection,
                        digest_algp,
                        max_digest_alg_len,
                        digestp,
                        max_digest_len,
                        &ret),
      NS_DISPATCH_SYNC);

  return ret;
}

/* Set negotiated DataChannel parameters.
 *
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[in]  streams - the number of streams for this data channel
 *  @param[in]  sctp_port - the negotiated sctp port
 *  @param[in]  protocol - the protocol as a string
 *
 *  @return 0 success, error failure
 */
short vcmSetDataChannelParameters(const char *peerconnection, cc_uint16_t streams, int sctp_port, const char* protocol)
{
  CSFLogDebug( logTag, "%s: PC = %s", __FUNCTION__, peerconnection);

  return 0;
}



/**
 *   Should we remove this from external API
 *
 *  @param[in] mcap_id - group identifier to which stream belongs.
 *  @param[in]     group_id         - group identifier
 *  @param[in]     stream_id        - stream identifier
 *  @param[in]     call_handle      - call identifier
 *  @param[in]     port_requested   - requested port.
 *  @param[in]     listen_ip        - local IP for listening
 *  @param[in]     is_multicast     - multicast stream?
 *  @param[in,out] port_allocated   - allocated(reserved) port
 *
 *  tbd need to see if we can deprecate this API
 *
 *  @return       0 success, ERROR failure.
 *
 */

short vcmRxOpen(cc_mcapid_t mcap_id,
                cc_groupid_t group_id,
                cc_streamid_t stream_id,
                cc_call_handle_t call_handle,
                cc_uint16_t port_requested,
                cpr_ip_addr_t *listen_ip,
                cc_boolean is_multicast,
                int *port_allocated)
{
    char fname[] = "vcmRxOpen";

    char dottedIP[20] = "";
    if(listen_ip)
    {
    	csf_sprintf(dottedIP, sizeof(dottedIP), "%u.%u.%u.%u",
                (listen_ip->u.ip4 >> 24) & 0xff, (listen_ip->u.ip4 >> 16) & 0xff,
                (listen_ip->u.ip4 >> 8) & 0xff, listen_ip->u.ip4 & 0xff );
    }

    CSFLogDebug( logTag, "%s: group_id=%d call_handle=%d listen=%s:%d is_mcast=%d",
                      fname, group_id, call_handle, dottedIP, port_requested, is_multicast);

    switch ( mcap_id )
    {
    case CC_AUDIO_1:
        CSFLogDebug( logTag, "%s: audio stream", fname);
        if ( VcmSIPCCBinding::getAudioTermination() != NULL )
            *port_allocated = VcmSIPCCBinding::getAudioTermination()->rxOpen( group_id, stream_id,
                                                    port_requested, listen_ip ? listen_ip->u.ip4 : 0,
                                                    (is_multicast != 0) );
        break;
    case CC_VIDEO_1:
        CSFLogDebug( logTag, "%s: video stream", fname);
        if ( VcmSIPCCBinding::getVideoTermination() != NULL )
            *port_allocated = VcmSIPCCBinding::getVideoTermination()->rxOpen( group_id, stream_id,
                                                    port_requested, listen_ip ? listen_ip->u.ip4 : 0,
                                                    (is_multicast != 0) );
        break;

    default:
        break;
    }
    return 0;
}

/**
 *  Start receive stream
 *  Note: For video calls, for a given call_handle there will be
 *        two media lines and the corresponding group_id/stream_id pair.
 *        One RTP session is requested from media server for each
 *        media line(group/stream) i.e. a video call would result in
 *        two rtp_sessions in our session info list created by two
 *        calls to vcm_rx/tx with mcap_id of AUDIO and VIDEO respectively.
 *
 *  @param[in]    mcap_id     - media type id
 *  @param[in]    group_id    - group identifier associated with the stream
 *  @param[in]    stream_id   - id of the stream one per each media line
 *  @param[in]    call_handle - call identifier
 *  @param[in]    payload     - payload information
 *  @param[in]    local_addr  - local ip address to use.
 *  @param[in]    port        - local port (receive)
 *  @param[in]    algorithmID - crypto alogrithm ID
 *  @param[in]    rx_key      - rx key used when algorithm ID is encrypting
 *  @param[in]    attrs       - media attributes
 *
 *  @return   zero(0) for success; otherwise, -1 for failure
 *
 */
int vcmRxStart(cc_mcapid_t mcap_id,
        cc_groupid_t group_id,
        cc_streamid_t stream_id,
        cc_call_handle_t  call_handle,
        const vcm_payload_info_t *payload,
        cpr_ip_addr_t *local_addr,
        cc_uint16_t port,
        vcm_crypto_algorithmID algorithmID,
        vcm_crypto_key_t *rx_key,
        vcm_mediaAttrs_t *attrs)
{
    uint8_t    *key;
    uint8_t    *salt;
    cc_uint16_t    key_len;
    cc_uint16_t    salt_len;
    char        fname[] = "vcmRxStart";

    CSFLogDebug( logTag, "%s: group_id=%d call_handle=%d payload=%d port=%d"
        " algID=%d", fname, group_id, call_handle, payload->remote_rtp_pt,
        port, algorithmID);

    if (call_handle == CC_NO_CALL_ID) {
        /* no operation when no call ID */
        return VCM_ERROR;
    }

    /* Set up crypto parameters */
    switch ( algorithmID )
    {
    case VCM_AES_128_COUNTER:
        if ( rx_key == NULL )
        {
            /* No key provided */
            CSFLogDebug( logTag, "vcmRxStart(): No key for algorithm ID %d",
                      algorithmID);
            return VCM_ERROR;
        }
        /* Set up key and Salt parameters */
        key_len = rx_key->key_len;
        key = &rx_key->key[0];
        salt_len = rx_key->salt_len;
        salt = &rx_key->salt[0];
        break;

    default:
        /* Give dummy data to avoid passing NULL for key/salt */
        key_len = 0;
        key = (uint8_t *)"";
        salt_len = 0;
        salt = (uint8_t *)"";
        break;
    }

    switch ( mcap_id )
    {
    case CC_AUDIO_1:
        if ( VcmSIPCCBinding::getAudioTermination() != NULL )
            return VcmSIPCCBinding::getAudioTermination()->rxStart(
                group_id, stream_id, payload->remote_rtp_pt,
                attrs->audio.packetization_period, port,
                attrs->audio.avt_payload_type, map_algorithmID(algorithmID),
                key, key_len, salt, salt_len, attrs->audio.mixing_mode,
                attrs->audio.mixing_party );
        break;

    case CC_VIDEO_1:
        if ( VcmSIPCCBinding::getVideoTermination() != NULL )
            return VcmSIPCCBinding::getVideoTermination()->rxStart(
                group_id, stream_id, payload->remote_rtp_pt,
                0, port, 0, map_algorithmID(algorithmID), key, key_len,
                salt, salt_len, 0, 0);
        break;

    default:
        break;
    }
    return VCM_ERROR;
}


/**
 *  start rx stream
 *  Same concept as vcmRxStart but for ICE/PeerConnection-based flows
 *
 *  @param[in]   mcap_id      - media cap id
 *  @param[in]   group_id     - group identifier to which the stream belongs
 *  @param[in]   stream_id    - stream id of the given media type.
 *  @param[in]   level        - the m-line index
 *  @param[in]   pc_stream_id - the media stream index (from PC.addStream())
 *  @param[i]n   pc_track_id  - the track within the media stream
 *  @param[in]   call_handle  - call handle
 *  @param[in]   peerconnection - the peerconnection in use
 *  @param[in]   num_payloads   - number of negotiated payloads
 *  @param[in]   payloads       - negotiated codec details list
 *  @param[in]   fingerprint_alg - the DTLS fingerprint algorithm
 *  @param[in]   fingerprint  - the DTLS fingerprint
 *  @param[in]   attrs        - media attributes
 *
 *  Returns: zero(0) for success; otherwise, ERROR for failure
 */

static int vcmRxStartICE_m(cc_mcapid_t mcap_id,
        cc_groupid_t group_id,
        cc_streamid_t stream_id,
        int level,
        int pc_stream_id,
        int pc_track_id,
        cc_call_handle_t  call_handle,
        const char *peerconnection,
        int num_payloads,
        const vcm_payload_info_t* payloads,
        const char *fingerprint_alg,
        const char *fingerprint,
        vcm_mediaAttrs_t *attrs)
{
  CSFLogDebug( logTag, "%s(%s)", __FUNCTION__, peerconnection);

  // Find the PC.
  sipcc::PeerConnectionWrapper pc(peerconnection);
  ENSURE_PC(pc, VCM_ERROR);

  if(!payloads) {
      CSFLogError( logTag, "Unitialized payload list");
      return VCM_ERROR;
  }

  // Find the stream we need
  nsRefPtr<sipcc::RemoteSourceStreamInfo> stream =
    pc.impl()->media()->GetRemoteStream(pc_stream_id);
  if (!stream) {
    // This should never happen
    PR_ASSERT(PR_FALSE);
    return VCM_ERROR;
  }
  // Create the transport flows
  mozilla::RefPtr<TransportFlow> rtp_flow =
      vcmCreateTransportFlow(pc.impl(), level, false,
                             fingerprint_alg, fingerprint);
  if (!rtp_flow) {
      CSFLogError( logTag, "Could not create RTP flow");
      return VCM_ERROR;
  }
  mozilla::RefPtr<TransportFlow> rtcp_flow =
      vcmCreateTransportFlow(pc.impl(), level, true,
                             fingerprint_alg, fingerprint);
  if (!rtcp_flow) {
      CSFLogError( logTag, "Could not create RTCP flow");
      return VCM_ERROR;
  }

  if (CC_IS_AUDIO(mcap_id)) {
    std::vector<mozilla::AudioCodecConfig *> configs;
    // Instantiate an appropriate conduit
    mozilla::RefPtr<mozilla::AudioSessionConduit> conduit =
                    mozilla::AudioSessionConduit::Create();
    if(!conduit)
      return VCM_ERROR;

    mozilla::AudioCodecConfig *config_raw;

    for(int i=0; i <num_payloads ; i++)
    {
      config_raw = new mozilla::AudioCodecConfig(
        payloads[i].remote_rtp_pt,
        ccsdpCodecName(payloads[i].codec_type),
        payloads[i].audio.frequency,
        payloads[i].audio.packet_size,
        payloads[i].audio.channels,
        payloads[i].audio.bitrate);
      configs.push_back(config_raw);
    }

    if (conduit->ConfigureRecvMediaCodecs(configs))
      return VCM_ERROR;


    // Now we have all the pieces, create the pipeline
    mozilla::RefPtr<mozilla::MediaPipeline> pipeline =
      new mozilla::MediaPipelineReceiveAudio(
        pc.impl()->GetHandle(),
        pc.impl()->GetMainThread().get(),
        pc.impl()->GetSTSThread(),
        stream->GetMediaStream()->GetStream(),
        conduit, rtp_flow, rtcp_flow);

    nsresult res = pipeline->Init();
    if (NS_FAILED(res)) {
      CSFLogError(logTag, "Failure initializing audio pipeline");
      return VCM_ERROR;
    }

    CSFLogDebug(logTag, "Created audio pipeline %p, conduit=%p, pc_stream=%d pc_track=%d",
                pipeline.get(), conduit.get(), pc_stream_id, pc_track_id);

    stream->StorePipeline(pc_track_id, pipeline);
  } else if (CC_IS_VIDEO(mcap_id)) {

    std::vector<mozilla::VideoCodecConfig *> configs;
    // Instantiate an appropriate conduit
    mozilla::RefPtr<mozilla::VideoSessionConduit> conduit =
             mozilla::VideoSessionConduit::Create();
    if(!conduit)
      return VCM_ERROR;

    mozilla::VideoCodecConfig *config_raw;

    for(int i=0; i <num_payloads; i++)
    {
      config_raw = new mozilla::VideoCodecConfig(
        payloads[i].remote_rtp_pt,
        ccsdpCodecName(payloads[i].codec_type),
        payloads[i].video.width,
        payloads[i].video.height);
      configs.push_back(config_raw);
    }

    if (conduit->ConfigureRecvMediaCodecs(configs))
      return VCM_ERROR;

    // Now we have all the pieces, create the pipeline
    mozilla::RefPtr<mozilla::MediaPipeline> pipeline =
        new mozilla::MediaPipelineReceiveVideo(
            pc.impl()->GetHandle(),
            pc.impl()->GetMainThread().get(),
            pc.impl()->GetSTSThread(),
            stream->GetMediaStream()->GetStream(),
            conduit, rtp_flow, rtcp_flow);

    nsresult res = pipeline->Init();
    if (NS_FAILED(res)) {
      CSFLogError(logTag, "Failure initializing video pipeline");
      return VCM_ERROR;
    }

    CSFLogDebug(logTag, "Created video pipeline %p, conduit=%p, pc_stream=%d pc_track=%d",
                pipeline.get(), conduit.get(), pc_stream_id, pc_track_id);

    stream->StorePipeline(pc_track_id, pipeline);
  } else {
    CSFLogError(logTag, "%s: mcap_id unrecognized", __FUNCTION__);
    return VCM_ERROR;
  }

  CSFLogDebug( logTag, "%s success", __FUNCTION__);
  return 0;
}


/**
 *  start rx stream
 *  Same concept as vcmRxStart but for ICE/PeerConnection-based flows
 *
 *  This is a thunk to vcmRxStartICE_m
 *
 *  @param[in]   mcap_id      - media cap id
 *  @param[in]   group_id     - group identifier to which the stream belongs
 *  @param[in]   stream_id    - stream id of the given media type.
 *  @param[in]   level        - the m-line index
 *  @param[in]   pc_stream_id - the media stream index (from PC.addStream())
 *  @param[i]n   pc_track_id  - the track within the media stream
 *  @param[in]   call_handle  - call handle
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[in]  num_payloads   - number of negotiated payloads
 *  @param[in]  payloads       - negotiated codec details list
 *  @param[in]   fingerprint_alg - the DTLS fingerprint algorithm
 *  @param[in]   fingerprint  - the DTLS fingerprint
 *  @param[in]   attrs        - media attributes
 *
 *  Returns: zero(0) for success; otherwise, ERROR for failure
 */

int vcmRxStartICE(cc_mcapid_t mcap_id,
                  cc_groupid_t group_id,
                  cc_streamid_t stream_id,
                  int level,
                  int pc_stream_id,
                  int pc_track_id,
                  cc_call_handle_t  call_handle,
                  const char *peerconnection,
                  int num_payloads,
                  const vcm_payload_info_t* payloads,
                  const char *fingerprint_alg,
                  const char *fingerprint,
                  vcm_mediaAttrs_t *attrs)
{
  int ret;

  VcmSIPCCBinding::getMainThread()->Dispatch(
      WrapRunnableNMRet(&vcmRxStartICE_m,
                        mcap_id,
                        group_id,
                        stream_id,
                        level,
                        pc_stream_id,
                        pc_track_id,
                        call_handle,
                        peerconnection,
                        num_payloads,
                        payloads,
                        fingerprint_alg,
                        fingerprint,
                        attrs,
                        &ret),
      NS_DISPATCH_SYNC);

  return ret;
}


/**
 *  Close the receive stream.
 *
 *  @param[in]    mcap_id - Media Capability ID
 *  @param[in]    group_id - group identifier that belongs to the stream.
 *  @param[in]    stream_id - stream id of the given media type.
 *  @param[in]    call_handle  - call identifier
 *
 *  @return   None
 *
 */

void vcmRxClose(cc_mcapid_t mcap_id,
        cc_groupid_t group_id,
        cc_streamid_t stream_id,
        cc_call_handle_t  call_handle)
{
    char fname[] = "vcmRxClose";

    CSFLogDebug( logTag, "%s: group_id=%d call_handle=%d", fname, group_id, call_handle);

    if (call_handle == CC_NO_CALL_ID) {
        CSFLogDebugS( logTag, "No CALL ID");
        /* no operation when no call ID */
        return;
    }
    switch ( mcap_id )
    {
    case CC_AUDIO_1:
        if ( VcmSIPCCBinding::getAudioTermination() != NULL )
            VcmSIPCCBinding::getAudioTermination()->rxClose( group_id, stream_id );
        break;

    case CC_VIDEO_1:
        if ( VcmSIPCCBinding::getVideoTermination() != NULL )
            VcmSIPCCBinding::getVideoTermination()->rxClose( group_id, stream_id );
        break;

    default:
        break;
    }
}

/**
 *  Release the allocated port
 * @param[in] mcap_id   - media capability id (0 is audio)
 * @param[in] group_id  - group identifier
 * @param[in] stream_id - stream identifier
 * @param[in] call_handle   - call id
 * @param[in] port     - port to be released
 *
 * @return void
 */

void vcmRxReleasePort  (cc_mcapid_t mcap_id,
                        cc_groupid_t group_id,
                        cc_streamid_t stream_id,
                        cc_call_handle_t  call_handle,
                        int port)
{
    CSFLogDebug( logTag, "vcmRxReleasePort(): group_id=%d stream_id=%d call_handle=%d port=%d",
                      group_id, stream_id, call_handle, port);

    if(CC_IS_AUDIO(mcap_id))
    {
        if ( VcmSIPCCBinding::getAudioTermination() != NULL )
            VcmSIPCCBinding::getAudioTermination()->rxRelease( group_id, stream_id, port );
    }
    else if(CC_IS_VIDEO(mcap_id))
    {
        if ( VcmSIPCCBinding::getVideoTermination() != NULL )
           VcmSIPCCBinding::getVideoTermination()->rxRelease( group_id, stream_id, port );
    }

    StreamObserver* obs = VcmSIPCCBinding::getStreamObserver();
    if(obs != NULL)
    	obs->deregisterStream(call_handle, stream_id);
}

/*
 *  Function:map_tone_type
 *
 *  Description: Convert to corresponding JPhone tone.
 *
 *  Parameters:  tone - vcm tone
 *
 *  Returns:     Mapped tone type
 *
 */
static ToneType
map_tone_type( vcm_tones_t tone )
{
    switch ( tone )
    {
    case VCM_INSIDE_DIAL_TONE:
        return ToneType_INSIDE_DIAL_TONE;
    case VCM_OUTSIDE_DIAL_TONE:
        return ToneType_OUTSIDE_DIAL_TONE;
    case VCM_LINE_BUSY_TONE:
        return ToneType_LINE_BUSY_TONE;
    case VCM_ALERTING_TONE:
        return ToneType_ALERTING_TONE;
    case VCM_BUSY_VERIFY_TONE:
        return ToneType_BUSY_VERIFY_TONE;
    case VCM_STUTTER_TONE:
        return ToneType_STUTTER_TONE;
    case VCM_MSG_WAITING_TONE:
        return ToneType_MSG_WAITING_TONE;
    case VCM_REORDER_TONE:
        return ToneType_REORDER_TONE;
    case VCM_CALL_WAITING_TONE:
        return ToneType_CALL_WAITING_TONE;
    case VCM_CALL_WAITING_2_TONE:
        return ToneType_CALL_WAITING_2_TONE;
    case VCM_CALL_WAITING_3_TONE:
        return ToneType_CALL_WAITING_3_TONE;
    case VCM_CALL_WAITING_4_TONE:
        return ToneType_CALL_WAITING_4_TONE;
    case VCM_HOLD_TONE:
        return ToneType_HOLD_TONE;
    case VCM_CONFIRMATION_TONE:
        return ToneType_CONFIRMATION_TONE;
    case VCM_PERMANENT_SIGNAL_TONE:
        return ToneType_PERMANENT_SIGNAL_TONE;
    case VCM_REMINDER_RING_TONE:
        return ToneType_REMINDER_RING_TONE;
    case VCM_NO_TONE:
        return ToneType_NO_TONE;
    case VCM_ZIP_ZIP:
        return ToneType_ZIP_ZIP;
    case VCM_ZIP:
        return ToneType_ZIP;
    case VCM_BEEP_BONK:
        return ToneType_BEEP_BONK;
    case VCM_RECORDERWARNING_TONE:
        return ToneType_RECORDERWARNING_TONE;
    case VCM_RECORDERDETECTED_TONE:
        return ToneType_RECORDERDETECTED_TONE;
    case VCM_MONITORWARNING_TONE:
        return ToneType_MONITORWARNING_TONE;
    case VCM_SECUREWARNING_TONE:
        return ToneType_SECUREWARNING_TONE;
    default:
        CSFLogDebugS( logTag, "map_tone_type(): WARNING..tone type not mapped.");
        return ToneType_NO_TONE;
    }
}

/**
 *  Start a tone (continuous)
 *
 *  Parameters:
 *  @param[in] tone       - tone type
 *  @param[in] alert_info - alertinfo header
 *  @param[in] call_handle    - call identifier
 *  @param[in] group_id - identifier of the group to which stream belongs
 *  @param[in] stream_id   - stream identifier.
 *  @param[in] direction  - network, speaker, both
 *
 *  @return void
 *
 */

void vcmToneStart(vcm_tones_t tone,
        short alert_info,
        cc_call_handle_t  call_handle,
        cc_groupid_t group_id,
        cc_streamid_t stream_id,
        cc_uint16_t direction)
{
    const char fname[] = "vcmToneStart";

    CSFLogDebug( logTag, "%s:tone=%d call_handle=%d dir=%d", fname, tone, call_handle, direction);

    VcmSIPCCBinding::getAudioTermination()->toneStart( map_tone_type(tone), (ToneDirection)direction,
                                alert_info, group_id, stream_id, FALSE );
    /*
     * Set delay value for multi-part tones and repeated tones.
     * Currently the only multi-part tones are stutter and message
     * waiting tones. The only repeated tones are call waiting and
     * tone on hold tones.  If the DSP ever supports stutter and
     * message waiting tones, these tones can be removed from this
     * switch statement.
     */

    /*
    switch ( tone )
    {
    case VCM_MSG_WAITING_TONE:
        lsm_start_multipart_tone_timer(tone, MSG_WAITING_DELAY, call_handle);
        break;

    case VCM_HOLD_TONE:
        lsm_start_continuous_tone_timer(tone, TOH_DELAY, call_handle);
        break;

    default:
        break;
    }
    */

    /*
     * Update dcb->active_tone if start request
     * is for an infinite duration tone.
     */
    //lsm_update_active_tone(tone, call_handle);
}

/**
 * Plays a short tone. uses the open audio path.
 * If no audio path is open, plays on speaker.
 *
 * @param[in] tone       - tone type
 * @param[in] alert_info - alertinfo header
 * @param[in] call_handle    - call identifier
 * @param[in] group_id - identifier of the group to which stream belongs
 * @param[in] stream_id   - stream identifier.
 * @param[in] direction  - network, speaker, both
 *
 * @return void
 */

void vcmToneStartWithSpeakerAsBackup(vcm_tones_t tone,
        short alert_info,
        cc_call_handle_t  call_handle,
        cc_groupid_t group_id,
        cc_streamid_t stream_id,
        cc_uint16_t direction)
{
    const char fname[] = "vcmToneStartWithSpeakerAsBackup";

    CSFLogDebug( logTag, "%s:tone=%d call_handle=%d dir=%d", fname, tone, call_handle, direction);

    VcmSIPCCBinding::getAudioTermination()->toneStart( map_tone_type(tone), (ToneDirection)direction,
                                alert_info, group_id, stream_id, TRUE );
    /*
     * Update dcb->active_tone if start request
     * is for an infinite duration tone.
     */
    //lsm_update_active_tone(tone, call_handle);
}

/**
 *  Stop the tone being played.
 *
 *  Description: Stop the tone being played currently
 *
 *
 * @param[in] tone - tone to be stopeed
 * @param[in] group_id - associated stream's group
 * @param[in] stream_id - associated stream id
 * @param[in] call_handle - the context (call) for this tone.
 *
 * @return void
 *
 */

void vcmToneStop(vcm_tones_t tone,
        cc_groupid_t group_id,
        cc_streamid_t stream_id,
        cc_call_handle_t call_handle)
{
    const char fname[] = "vcmToneStop";

    CSFLogDebug( logTag, "%s:tone=%d stream_id=%d", fname, tone, stream_id);
/*
    lsm_stop_multipart_tone_timer();
    lsm_stop_continuous_tone_timer();
    */

    VcmSIPCCBinding::getAudioTermination()->toneStop( map_tone_type(tone), group_id, stream_id );
}

/**
 *  should we remove from external API
 *
 *  @param[in]  mcap_id - Media Capability ID
 *  @param[in]  group_id - group to which stream belongs
 *  @param[in]  stream_id - stream identifier
 *  @param[in]  call_handle - call identifier
 *
 *  @return  zero(0) for success; otherwise, ERROR for failure
 *
 */

short vcmTxOpen(cc_mcapid_t mcap_id,
        cc_groupid_t group_id,
        cc_streamid_t stream_id,
        cc_call_handle_t  call_handle)
{
    char        fname[] = "vcmTxOpen";

    CSFLogDebug( logTag, "%s: group_id=%d call_handle=%d", fname, group_id, call_handle);

    if (call_handle == CC_NO_CALL_ID) {
        /* no operation when no call ID */
        return VCM_ERROR;
    }
    return 0;
}

/**
 *  start tx stream
 *  Note: For video calls, for a given call_handle there will be
 *        two media lines and the corresponding group_id/stream_id pair.
 *        One RTP session is requested from media server for each
 *        media line(group/stream) i.e. a video call would result in
 *        two rtp_sessions in our session info list created by two
 *        calls to vcm_rx/tx with mcap_id of AUDIO and VIDEO respectively.
 *
 *  @param[in]   mcap_id      - media cap id
 *  @param[in]   group_id     - group identifier to which the stream belongs
 *  @param[in]   stream_id    - stream id of the given media type.
 *  @param[in]   call_handle      - call identifier
 *  @param[in]   payload      - payload information
 *  @param[in]   tos          - bit marking
 *  @param[in]   local_addr   - local address
 *  @param[in]   local_port   - local port
 *  @param[in]   remote_ip_addr - remote ip address
 *  @param[in]   remote_port  - remote port
 *  @param[in]   algorithmID  - crypto alogrithm ID
 *  @param[in]   tx_key       - tx key used when algorithm ID is encrypting.
 *  @param[in]   attrs        - media attributes
 *
 *  Returns: zero(0) for success; otherwise, ERROR for failure
 *
 */
int vcmTxStart(cc_mcapid_t mcap_id,
        cc_groupid_t group_id,
        cc_streamid_t stream_id,
        cc_call_handle_t  call_handle,
        const vcm_payload_info_t *payload,
        short tos,
        cpr_ip_addr_t *local_addr,
        cc_uint16_t local_port,
        cpr_ip_addr_t *remote_ip_addr,
        cc_uint16_t remote_port,
        vcm_crypto_algorithmID algorithmID,
        vcm_crypto_key_t *tx_key,
        vcm_mediaAttrs_t *attrs)
{
    const char fname[] = "vcmTxStart";
    uint8_t    *key;
    uint8_t    *salt;
    cc_uint16_t    key_len;
    cc_uint16_t    salt_len;

    char dottedIP[20];
    csf_sprintf(dottedIP, sizeof(dottedIP), "%u.%u.%u.%u",
                (remote_ip_addr->u.ip4 >> 24) & 0xff, (remote_ip_addr->u.ip4 >> 16) & 0xff,
                (remote_ip_addr->u.ip4 >> 8) & 0xff, remote_ip_addr->u.ip4 & 0xff );

    CSFLogDebug( logTag, "%s: group_id=%d call_handle=%d payload=%d tos=%d local_port=%d remote=%s:%d algID=%d",
        fname, group_id, call_handle, payload->remote_rtp_pt, tos, local_port,
        dottedIP, remote_port, algorithmID);

    if (call_handle == CC_NO_CALL_ID) {
        /* no operation when no call ID */
        return VCM_ERROR;
    }

    /* Set up crypto parameters */
    switch ( algorithmID )
    {
    case VCM_AES_128_COUNTER:
        if ( tx_key == NULL )
        {
            /* No key provided */
            CSFLogDebug( logTag, "%s: No key for algorithm ID %d", fname, algorithmID);
            return VCM_ERROR;
        }
        /* Set up key and Salt parameters */
        key_len  = tx_key->key_len;
        key      = &tx_key->key[0];
        salt_len = tx_key->salt_len;
        salt     = &tx_key->salt[0];
        break;

    default:
        /* Give dummy data to avoid passing NULL for key/salt */
        key_len  = 0;
        key      = (uint8_t *)"";
        salt_len = 0;
        salt     = (uint8_t *)"";
        break;
    }

    switch ( mcap_id )
    {
    case CC_AUDIO_1:
        if ( VcmSIPCCBinding::getAudioTermination() != NULL )
            return VcmSIPCCBinding::getAudioTermination()->txStart(
                group_id, stream_id, payload->remote_rtp_pt,
                attrs->audio.packetization_period, (attrs->audio.vad != 0),
                tos, dottedIP, remote_port, attrs->audio.avt_payload_type,
                map_algorithmID(algorithmID), key, key_len, salt, salt_len,
                attrs->audio.mixing_mode, attrs->audio.mixing_party );

        break;

    case CC_VIDEO_1:
        if ( VcmSIPCCBinding::getVideoTermination() != NULL )
           return VcmSIPCCBinding::getVideoTermination()->txStart(
              group_id, stream_id, payload->remote_rtp_pt,
              0, 0, tos, dottedIP, remote_port, 0,
              map_algorithmID(algorithmID), key, key_len, salt, salt_len, 0, 0);
        break;

    default:
        break;
    }
    return VCM_ERROR;
}


/**
 *  start tx stream
 *  Same concept as vcmTxStart but for ICE/PeerConnection-based flows
 *
 *  @param[in]   mcap_id      - media cap id
 *  @param[in]   group_id     - group identifier to which the stream belongs
 *  @param[in]   stream_id    - stream id of the given media type.
 *  @param[in]   level        - the m-line index
 *  @param[in]   pc_stream_id - the media stream index (from PC.addStream())
 *  @param[i]n   pc_track_id  - the track within the media stream
 *  @param[in]   call_handle  - call handle
 *  @param[in]   peerconnection - the peerconnection in use
 *  @param[in]   payload      - payload information
 *  @param[in]   tos          - bit marking
 *  @param[in]   fingerprint_alg - the DTLS fingerprint algorithm
 *  @param[in]   fingerprint  - the DTLS fingerprint
 *  @param[in]   attrs        - media attributes
 *
 *  Returns: zero(0) for success; otherwise, ERROR for failure
 *
 */
#define EXTRACT_DYNAMIC_PAYLOAD_TYPE(PTYPE) ((PTYPE)>>16)

static int vcmTxStartICE_m(cc_mcapid_t mcap_id,
        cc_groupid_t group_id,
        cc_streamid_t stream_id,
        int level,
        int pc_stream_id,
        int pc_track_id,
        cc_call_handle_t  call_handle,
        const char *peerconnection,
        const vcm_payload_info_t *payload,
        short tos,
        const char *fingerprint_alg,
        const char *fingerprint,
        vcm_mediaAttrs_t *attrs)
{
  CSFLogDebug( logTag, "%s(%s)", __FUNCTION__, peerconnection);

  // Find the PC and get the stream
  sipcc::PeerConnectionWrapper pc(peerconnection);
  ENSURE_PC(pc, VCM_ERROR);
  nsRefPtr<sipcc::LocalSourceStreamInfo> stream = pc.impl()->media()->
    GetLocalStream(pc_stream_id);

  // Create the transport flows
  mozilla::RefPtr<TransportFlow> rtp_flow =
      vcmCreateTransportFlow(pc.impl(), level, false,
                             fingerprint_alg, fingerprint);
  if (!rtp_flow) {
      CSFLogError( logTag, "Could not create RTP flow");
      return VCM_ERROR;
  }
  mozilla::RefPtr<TransportFlow> rtcp_flow =
      vcmCreateTransportFlow(pc.impl(), level, true,
                             fingerprint_alg, fingerprint);
  if (!rtcp_flow) {
      CSFLogError( logTag, "Could not create RTCP flow");
      return VCM_ERROR;
  }

  if (CC_IS_AUDIO(mcap_id)) {
    mozilla::AudioCodecConfig *config_raw;
    config_raw = new mozilla::AudioCodecConfig(
      payload->remote_rtp_pt,
      ccsdpCodecName(payload->codec_type),
      payload->audio.frequency,
      payload->audio.packet_size,
      payload->audio.channels,
      payload->audio.bitrate);

    // Take possession of this pointer
    mozilla::ScopedDeletePtr<mozilla::AudioCodecConfig> config(config_raw);

    // Instantiate an appropriate conduit
    mozilla::RefPtr<mozilla::AudioSessionConduit> conduit =
      mozilla::AudioSessionConduit::Create();

    if (!conduit || conduit->ConfigureSendMediaCodec(config))
      return VCM_ERROR;

    mozilla::RefPtr<mozilla::MediaPipeline> pipeline =
        new mozilla::MediaPipelineTransmit(
            pc.impl()->GetHandle(),
            pc.impl()->GetMainThread().get(),
            pc.impl()->GetSTSThread(),
            stream->GetMediaStream()->GetStream(),
            conduit, rtp_flow, rtcp_flow);

    nsresult res = pipeline->Init();
    if (NS_FAILED(res)) {
      CSFLogError(logTag, "Failure initializing audio pipeline");
      return VCM_ERROR;
    }
    CSFLogDebug(logTag, "Created audio pipeline %p, conduit=%p, pc_stream=%d pc_track=%d",
                pipeline.get(), conduit.get(), pc_stream_id, pc_track_id);


    // Now we have all the pieces, create the pipeline
    stream->StorePipeline(pc_track_id, pipeline);

  } else if (CC_IS_VIDEO(mcap_id)) {
    mozilla::VideoCodecConfig *config_raw;
    config_raw = new mozilla::VideoCodecConfig(
      payload->remote_rtp_pt,
      ccsdpCodecName(payload->codec_type),
      payload->video.width,
      payload->video.height);

    // Take possession of this pointer
    mozilla::ScopedDeletePtr<mozilla::VideoCodecConfig> config(config_raw);

    // Instantiate an appropriate conduit
    mozilla::RefPtr<mozilla::VideoSessionConduit> conduit =
      mozilla::VideoSessionConduit::Create();

    // Find the appropriate media conduit config
    if (!conduit || conduit->ConfigureSendMediaCodec(config))
      return VCM_ERROR;

    // Now we have all the pieces, create the pipeline
    mozilla::RefPtr<mozilla::MediaPipeline> pipeline =
        new mozilla::MediaPipelineTransmit(
            pc.impl()->GetHandle(),
            pc.impl()->GetMainThread().get(),
            pc.impl()->GetSTSThread(),
            stream->GetMediaStream()->GetStream(),
            conduit, rtp_flow, rtcp_flow);

    nsresult res = pipeline->Init();
    if (NS_FAILED(res)) {
      CSFLogError(logTag, "Failure initializing video pipeline");
      return VCM_ERROR;
    }

    CSFLogDebug(logTag, "Created video pipeline %p, conduit=%p, pc_stream=%d pc_track=%d",
                pipeline.get(), conduit.get(), pc_stream_id, pc_track_id);

    stream->StorePipeline(pc_track_id, pipeline);
  } else {
    CSFLogError(logTag, "%s: mcap_id unrecognized", __FUNCTION__);
    return VCM_ERROR;
  }

  CSFLogDebug( logTag, "%s success", __FUNCTION__);
  return 0;
}

/**
 *  start tx stream
 *  Same concept as vcmTxStart but for ICE/PeerConnection-based flows
 *
 *  This is a thunk to vcmTxStartICE_m
 *
 *  @param[in]   mcap_id      - media cap id
 *  @param[in]   group_id     - group identifier to which the stream belongs
 *  @param[in]   stream_id    - stream id of the given media type.
 *  @param[in]   level        - the m-line index
 *  @param[in]   pc_stream_id - the media stream index (from PC.addStream())
 *  @param[i]n   pc_track_id  - the track within the media stream
 *  @param[in]   call_handle  - call handle
 *  @param[in]  peerconnection - the peerconnection in use
 *  @param[in]   payload      - payload type
 *  @param[in]   tos          - bit marking
 *  @param[in]   fingerprint_alg - the DTLS fingerprint algorithm
 *  @param[in]   fingerprint  - the DTLS fingerprint
 *  @param[in]   attrs        - media attributes
 *
 *  Returns: zero(0) for success; otherwise, ERROR for failure
 *
 */
#define EXTRACT_DYNAMIC_PAYLOAD_TYPE(PTYPE) ((PTYPE)>>16)

int vcmTxStartICE(cc_mcapid_t mcap_id,
                  cc_groupid_t group_id,
                  cc_streamid_t stream_id,
                  int level,
                  int pc_stream_id,
                  int pc_track_id,
                  cc_call_handle_t  call_handle,
                  const char *peerconnection,
                  const vcm_payload_info_t *payload,
                  short tos,
                  const char *fingerprint_alg,
                  const char *fingerprint,
                  vcm_mediaAttrs_t *attrs)
{
  int ret;

  VcmSIPCCBinding::getMainThread()->Dispatch(
      WrapRunnableNMRet(&vcmTxStartICE_m,
                        mcap_id,
                        group_id,
                        stream_id,
                        level,
                        pc_stream_id,
                        pc_track_id,
                        call_handle,
                        peerconnection,
                        payload,
                        tos,
                        fingerprint_alg,
                        fingerprint,
                        attrs,
                        &ret),
      NS_DISPATCH_SYNC);
  
  return ret;
}


/**
 *  Close the transmit stream
 *
 *  @param[in] mcap_id - Media Capability ID
 *  @param[in] group_id - identifier of the group to which stream belongs
 *  @param[in] stream_id - stream id of the given media type.
 *  @param[in] call_handle  - call identifier
 *
 *  @return     void
 */

void vcmTxClose(cc_mcapid_t mcap_id,
        cc_groupid_t group_id,
        cc_streamid_t stream_id,
        cc_call_handle_t  call_handle)
{
    const char fname[] = "vcmTxClose";

    CSFLogDebug( logTag, "%s: group_id=%d call_handle=%d", fname, group_id, call_handle);

    if (call_handle == CC_NO_CALL_ID) {
        /* no operation when no call ID */
        return;
    }

    switch ( mcap_id )
    {
    case CC_AUDIO_1:
        if ( VcmSIPCCBinding::getAudioTermination() != NULL )
            VcmSIPCCBinding::getAudioTermination()->txClose( group_id, stream_id);
        break;

    case CC_VIDEO_1:
        if ( VcmSIPCCBinding::getVideoTermination() != NULL )
           VcmSIPCCBinding::getVideoTermination()->txClose( group_id, stream_id);
        break;

    default:
        break;
    }
}

#if 0
static CodecRequestType
map_codec_request_type( int request_type )
{
    switch ( request_type )
    {
    default:
    case cip_sipcc_CodecMask_DSP_IGNORE:     return CodecRequestType_IGNORE;
    case cip_sipcc_CodecMask_DSP_DECODEONLY: return CodecRequestType_DECODEONLY;
    case cip_sipcc_CodecMask_DSP_ENCODEONLY: return CodecRequestType_ENCODEONLY;
    case cip_sipcc_CodecMask_DSP_FULLDUPLEX: return CodecRequestType_FULLDUPLEX;
    }
}
#endif

/**
 * Get current list of audio codec that could be used
 * @param request_type -
 * The request_type should be VCM_DECODEONLY/ENCODEONLY/FULLDUPLEX/IGNORE
 *
 * @return A bit mask should be returned that specifies the list of the audio
 * codecs. The bit mask should conform to the defines in this file.
 * #define VCM_RESOURCE_G711     0x00000001
 * #define VCM_RESOURCE_G729A    0x00000002
 * ....
 */

int vcmGetAudioCodecList(int request_type)
{
// Direct access to media layer replaced by locally stored codecMask
// set in PeerConnectionImpl
#if 0
    const char fname[] = "vcmGetAudioCodecList";
    int retVal;
    int codecMask = 0;

    CSFLogDebug( logTag, "%s(request_type = %d)", fname, request_type);

    retVal = VcmSIPCCBinding::getAudioTermination() ? VcmSIPCCBinding::getAudioTermination()->getCodecList( map_codec_request_type(request_type) ) : 0;

    if ( retVal & AudioCodecMask_G711 ) {    codecMask |= cip_sipcc_CodecMask_DSP_RESOURCE_G711; CSFLogDebug( logTag, "%s", " G711"); }
    if ( retVal & AudioCodecMask_LINEAR ) {  codecMask |= cip_sipcc_CodecMask_DSP_RESOURCE_LINEAR; CSFLogDebug( logTag, "%s", " LINEAR" ); }
    if ( retVal & AudioCodecMask_G722 ) {    codecMask |= cip_sipcc_CodecMask_DSP_RESOURCE_G722; CSFLogDebug( logTag, "%s", " G722"); }
    if ( retVal & AudioCodecMask_iLBC )  {   codecMask |= cip_sipcc_CodecMask_DSP_RESOURCE_iLBC; CSFLogDebug( logTag, "%s", " iLBC"); }
    if ( retVal & AudioCodecMask_iSAC )   {  codecMask |= cip_sipcc_CodecMask_DSP_RESOURCE_iSAC; CSFLogDebug( logTag, "%s", " iSAC "); }

    CSFLogDebug( logTag, "%s(codec_mask = %X)", fname, codecMask);
    return codecMask;
#else
  int codecMask = VcmSIPCCBinding::getAudioCodecs();
  CSFLogDebug(logTag, "GetAudioCodecList returning %X", codecMask);

  return codecMask;
#endif
}

/**
 * Get current list of video codec that could be used
 * @param request_type -
 * The request_type should be VCM_DECODEONLY/ENCODEONLY/FULLDUPLEX/IGNORE
 *
 * @return A bit mask should be returned that specifies the list of the audio
 * codecs. The bit mask should conform to the defines in this file.
 * #define VCM_RESOURCE_G711     0x00000001
 * #define VCM_RESOURCE_G729A    0x00000002
 * ....
 */

#ifndef DSP_H264
#define DSP_H264        0x00000001
#endif

#ifndef DSP_H263
#define DSP_H263        0x00000002
#endif

int vcmGetVideoCodecList(int request_type)
{
// Direct access to media layer replaced by locally stored codecMask
// set in PeerConnectionImpl
#if 0
    const char fname[] = "vcmGetVideoCodecList";
    int retVal = 0;
    int codecMask = 0;

    CSFLogDebug( logTag, "%s(request_type = %d)", fname, request_type);

    retVal = VcmSIPCCBinding::getVideoTermination() ? VcmSIPCCBinding::getVideoTermination()->getCodecList( map_codec_request_type(request_type) ) : 0;

    if ( retVal & VideoCodecMask_H264 )    codecMask |= DSP_H264;
    if ( retVal & VideoCodecMask_H263 )    codecMask |= DSP_H263;

    CSFLogDebug( logTag, "%s(codec_mask = %X)", fname, codecMask);

    //return codecMask;
	return VCM_CODEC_RESOURCE_H264;
#else
  int codecMask = VcmSIPCCBinding::getVideoCodecs();
  CSFLogDebug(logTag, "GetVideoCodecList returning %X", codecMask);

  return codecMask;
#endif
}

/**
 * Get max supported H.264 video packetization mode.
 * @return maximum supported video packetization mode for H.264. Value returned
 * must be 0 or 1. Value 2 is not supported yet.
 */
int vcmGetVideoMaxSupportedPacketizationMode()
{
    return 0;
}

/**
 *  MEDIA control received from far end on signaling path
 *
 *  @param call_handle - call_handle of the call
 *  @param to_encoder - the control request received
 *        Only FAST_PICTURE_UPDATE is supported
 *
 *  @return  void
 *
 */

void vcmMediaControl(cc_call_handle_t  call_handle, vcm_media_control_to_encoder_t to_encoder)
{
    if ( to_encoder == VCM_MEDIA_CONTROL_PICTURE_FAST_UPDATE )
    {
    	StreamObserver* obs = VcmSIPCCBinding::getStreamObserver();
    	if (obs != NULL)
    	{
    		obs->sendIFrame(call_handle);
    	}
    }
}

/**
 * Get the rx/tx stream statistics associated with the call.
 *
 * @param[in]  mcap_id  - media type (audio/video)
 * @param[in]  group_id - group id of the stream
 * @param[in]  stream_id - stram id of the stream
 * @param[in]  call_handle - call identifier
 * @param[out] rx_stats - ptr to the rx field in the stats struct
 * @param[out] tx_stats - ptr to the tx field in the stats struct
 *
 */

int vcmGetRtpStats(cc_mcapid_t mcap_id,
        cc_groupid_t group_id,
        cc_streamid_t stream_id,
        cc_call_handle_t  call_handle,
        char *rx_stats,
        char *tx_stats)
{
    return 0;
}

/**
 *  specifies DSCP marking for RTCP streams
 *
 *  @param group_id - call_handle of the call
 *  @param dscp - the DSCP value to be used
 *
 *  @return  void
 */

void vcmSetRtcpDscp(cc_groupid_t group_id, int dscp)
{
    // do nothing
}

/**
 *
 * The wlan interface puts into unique situation where call control
 * has to allocate the worst case bandwith before creating a
 * inbound or outbound call. The function call will interface through
 * media API into wlan to get the call bandwidth. The function
 * return is asynchronous and will block till the return media
 * callback signals to continue the execution.
 *
 * @note If not using WLAN interface simply return true
 *
 * @return true if the bandwidth can be allocated else false.
 */

cc_boolean vcmAllocateBandwidth(cc_call_handle_t  call_handle, int sessions)
{
    return(TRUE);
}

/**
 *
 * Free the bandwidth allocated for this call
 * using the vcmAllocateBandwidth API
 *
 * @note  If not using WLAN provide a stub
 */

void vcmRemoveBandwidth(cc_call_handle_t  call_handle)
{
    //do nothing
}

/**
 * @brief vcmActivateWlan
 *
 * Free the bandwidth allocated for this call
 * using the vcmAllocateBandwidth API
 *
 * @note If not using WLAN provide a stub
 */

void vcmActivateWlan(cc_boolean is_active)
{
    // do nothing
}

/**
 *  free the media pointer allocated in vcm_negotiate_attrs method
 *
 *  @param ptr - pointer to be freed
 *
 *  @return  void
 */
void vcmFreeMediaPtr(void *ptr)
{
  free(ptr);
}

/**
 * Verify if the SDP attributes for the requested video codec are acceptable
 *
 * This method is called for video codecs only. This method should parse the
 * Video SDP attributes using the SDP helper API and verify if received
 * attributes are acceptable. If the attributes are acceptable any attribute
 * values if needed by vcmTxStart method should be bundled in the desired
 * structure and its pointer should be returned in rccappptr. This opaque
 * pointer shall be provided again when vcmTxStart is invoked.
 *
 * @param [in] media_type - codec for which we are negotiating
 * @param [in] sdp_p - opaque SDP pointer to be used via SDP helper APIs
 * @param [in] level - Parameter to be used with SDP helper APIs
 * @param [out] rcapptr - variable to return the allocated attrib structure
 *
 * @return cc_boolean - true if attributes are accepted false otherwise
 */

cc_boolean vcmCheckAttribs(cc_uint32_t media_type, void *sdp_p, int level, void **rcapptr)
{
    CSFLogDebug( logTag, "vcmCheckAttribs(): media=%d", media_type);

    cc_uint16_t     temp;
    const char      *ptr;
    uint32_t        t_uint;
    struct h264_video *rcap;

    *rcapptr = NULL;

    switch (media_type)
    {
    case RTP_VP8:
    	return TRUE;

    case RTP_H264_P0:
    case RTP_H264_P1:

        rcap = (struct h264_video *) cpr_malloc( sizeof(struct h264_video) );
        if ( rcap == NULL )
        {
            CSFLogDebugS( logTag, "vcmCheckAttribs(): Malloc Failed for rcap");
            return FALSE;
        }
        memset( rcap, 0, sizeof(struct h264_video) );

        if ( (ptr = ccsdpAttrGetFmtpParamSets(sdp_p, level, 0, 1)) != NULL )
        {
            memset(rcap->sprop_parameter_set, 0, csf_countof(rcap->sprop_parameter_set));
            sstrncpy(rcap->sprop_parameter_set, ptr, csf_countof(rcap->sprop_parameter_set));
        }

        if ( ccsdpAttrGetFmtpPackMode(sdp_p, level, 0, 1, &temp) == SDP_SUCCESS )
        {
            rcap->packetization_mode = temp;
        }

        if ( (ptr = ccsdpAttrGetFmtpProfileLevelId(sdp_p, level, 0, 1)) != NULL )
        {
#ifdef _WIN32
            sscanf_s(ptr, "%x", &rcap->profile_level_id, sizeof(int*));
#else
            sscanf(ptr, "%x", &rcap->profile_level_id);
#endif
        }

        if ( ccsdpAttrGetFmtpMaxMbps(sdp_p, level, 0, 1, &t_uint) == SDP_SUCCESS )
        {
            rcap->max_mbps = t_uint;
        }

        if ( ccsdpAttrGetFmtpMaxFs(sdp_p, level, 0, 1, &t_uint) == SDP_SUCCESS )
        {
            rcap->max_fs = t_uint;
        }

        if ( ccsdpAttrGetFmtpMaxCpb(sdp_p, level, 0, 1, &t_uint) == SDP_SUCCESS )
        {
            rcap->max_cpb = t_uint;
        }

        if ( ccsdpAttrGetFmtpMaxCpb(sdp_p, level, 0, 1, &t_uint) == SDP_SUCCESS )
        {
            rcap->max_dpb = t_uint;
        }

        if ( ccsdpAttrGetFmtpMaxCpb(sdp_p, level, 0, 1, &t_uint) == SDP_SUCCESS )
        {
            rcap->max_br = t_uint;
        }

        rcap->tias_bw = ccsdpGetBandwidthValue(sdp_p, level, 1);
        if ( rcap->tias_bw == 0 )
        {
            // received bandwidth of 0 reject this
            free(rcap);
            return FALSE;
        }
        else if ( rcap->tias_bw == SDP_INVALID_VALUE )
        {
            // bandwidth not received pass 0 to ms
            rcap->tias_bw = 0;
        }

        CSFLogDebug( logTag, "vcmCheckAttribs(): Negotiated media attrs\nsprop=%s\npack_mode=%d\nprofile_level_id=%X\nmbps=%d\nmax_fs=%d\nmax_cpb=%d\nmax_dpb=%d\nbr=%d bw=%d\n",
            rcap->sprop_parameter_set,
            rcap->packetization_mode,
            rcap->profile_level_id,
            rcap->max_mbps,
            rcap->max_fs,
            rcap->max_cpb,
            rcap->max_dpb,
            rcap->max_br,
            rcap->tias_bw);

        *rcapptr = rcap;
        return TRUE;

    default:
        return FALSE;
    }
}

/**
 * Add Video attributes in the offer/answer SDP
 *
 * This method is called for video codecs only. This method should populate the
 * Video SDP attributes using the SDP helper API
 *
 * @param [in] sdp_p - opaque SDP pointer to be used via SDP helper APIs
 * @param [in] level - Parameter to be used with SDP helper APIs
 * @param [in] media_type - codec for which the SDP attributes are to be populated
 * @param [in] payload_number - RTP payload type used for the SDP
 * @param [in] isOffer - cc_boolean indicating we are encoding an offer or an aswer
 *
 * @return void
 */
void vcmPopulateAttribs(void *sdp_p, int level, cc_uint32_t media_type,
                          cc_uint16_t payload_number, cc_boolean isOffer)
{
    CSFLogDebug( logTag, "vcmPopulateAttribs(): media=%d PT=%d, isOffer=%d", media_type, payload_number, isOffer);
    uint16_t a_inst;//, a_inst2, a_inst3, a_inst4;
    int profile;
    char profile_level_id[MAX_SPROP_LEN];

    switch (media_type)
    {
    case RTP_H264_P0:
    case RTP_H264_P1:

        if ( ccsdpAddNewAttr(sdp_p, level, 0, SDP_ATTR_FMTP, &a_inst) != SDP_SUCCESS ) return;

        (void) ccsdpAttrSetFmtpPayloadType(sdp_p, level, 0, a_inst, payload_number);

        //(void) sdp_attr_set_fmtp_pack_mode(sdp_p, level, 0, a_inst, 1 /*packetization_mode*/);
        //(void) sdp_attr_set_fmtp_parameter_sets(sdp_p, level, 0, a_inst, "J0KAFJWgUH5A,KM4H8n==");    // NAL units 27 42 80 14 95 a0 50 7e 40 28 ce 07 f2

        //profile = 0x42E000 + H264ToSDPLevel( vt_GetClientProfileLevel() );
        profile = 0x42E00C;
        csf_sprintf(profile_level_id, MAX_SPROP_LEN, "%X", profile);
        (void) ccsdpAttrSetFmtpProfileLevelId(sdp_p, level, 0, a_inst, profile_level_id);

        //(void) sdp_attr_set_fmtp_max_mbps(sdp_p, level, 0, a_inst, max_mbps);
        //(void) sdp_attr_set_fmtp_max_fs(sdp_p, level, 0, a_inst, max_fs);
        //(void) sdp_attr_set_fmtp_max_cpb(sdp_p, level, 0, a_inst, max_cpb);
        //(void) sdp_attr_set_fmtp_max_dpb(sdp_p, level, 0, a_inst, max_dpb);
        //(void) sdp_attr_set_fmtp_max_br(sdp_p, level, 0, a_inst, max_br);
        //(void) sdp_add_new_bw_line(sdp_p, level, &a_inst);
        //(void) sdp_set_bw(sdp_p, level, a_inst, SDP_BW_MODIFIER_TIAS, tias_bw);

        break;

    default:
        break;
    }
}

/**
 * Send a DTMF digit
 *
 * This method is called for sending a DTMF tone for the specified duration
 *
 * @param [in] digit - the DTMF digit that needs to be played out.
 * @param [in] duration - duration of the tone
 * @param [in] direction - direction in which the tone needs to be played.
 *
 * @return void
 */
int vcmDtmfBurst(int digit, int duration, int direction)
{
    CSFLogDebug( logTag, "vcmDtmfBurst(): digit=%d duration=%d, direction=%d", digit, duration, direction);
    StreamObserver* obs = VcmSIPCCBinding::getStreamObserver();
    if(obs != NULL)
    	obs->dtmfBurst(digit, duration, direction);
    return 0;
}

/**
 * vcmGetILBCMode
 *
 * This method should return the mode that needs to be used in
 * SDP
 * @return int
 */
int vcmGetILBCMode()
{
    return 0;
}

} // extern "C"

static mozilla::RefPtr<TransportFlow>
vcmCreateTransportFlow(sipcc::PeerConnectionImpl *pc, int level, bool rtcp,
                       const char *fingerprint_alg,
                       const char *fingerprint) {

  // TODO(ekr@rtfm.com): Check that if the flow already exists the digest
  // is the same. The only way that can happen is if
  //
  // (a) We have an error or
  // (b) The other side bundled but had mismatched digests for each line
  //
  // Not clear that either of these cases matters.
  mozilla::RefPtr<TransportFlow> flow;
  flow = pc->media()->GetTransportFlow(level, rtcp);

  if (!flow) {
    CSFLogDebug(logTag, "Making new transport flow for level=%d rtcp=%s", level, rtcp ? "true" : "false");

    char id[32];
    PR_snprintf(id, sizeof(id), "%s:%d,%s",
                pc->GetHandle().c_str(), level, rtcp ? "rtcp" : "rtp");
    flow = new TransportFlow(id);


    ScopedDeletePtr<TransportLayerIce> ice(
        new TransportLayerIce("flow", pc->media()->ice_ctx(),
                              pc->media()->ice_media_stream(level-1),
                              rtcp ? 2 : 1));

    ScopedDeletePtr<TransportLayerDtls> dtls(new TransportLayerDtls());
    dtls->SetRole(pc->GetRole() == sipcc::PeerConnectionImpl::kRoleOfferer ?
                  TransportLayerDtls::CLIENT : TransportLayerDtls::SERVER);
    dtls->SetIdentity(pc->GetIdentity());

    unsigned char remote_digest[TransportLayerDtls::kMaxDigestLength];
    size_t digest_len;

    nsresult res = DtlsIdentity::ParseFingerprint(fingerprint,
                                                  remote_digest,
                                                  sizeof(remote_digest),
                                                  &digest_len);
    if (!NS_SUCCEEDED(res)) {
      CSFLogError(logTag, "Could not convert fingerprint");
      return NULL;
    }
    res = dtls->SetVerificationDigest(fingerprint_alg, remote_digest, digest_len);
    if (!NS_SUCCEEDED(res)) {
      CSFLogError(logTag, "Could not set remote DTLS digest");
      return NULL;
    }

    std::vector<uint16_t> srtp_ciphers;
    srtp_ciphers.push_back(SRTP_AES128_CM_HMAC_SHA1_80);
    srtp_ciphers.push_back(SRTP_AES128_CM_HMAC_SHA1_32);

    res = dtls->SetSrtpCiphers(srtp_ciphers);
    if (!NS_SUCCEEDED(res)) {
      CSFLogError(logTag, "Couldn't set SRTP ciphers");
      return NULL;
    }

    std::queue<TransportLayer *> layers;
    layers.push(ice.forget());
    layers.push(dtls.forget());


    // Layers are now owned by the flow.
    nsresult rv = pc->media()->ice_ctx()->thread()->Dispatch(
        WrapRunnableRet(flow, &TransportFlow::PushLayers, layers, &res),
        NS_DISPATCH_SYNC);

    if (NS_FAILED(rv) || NS_FAILED(res)) {
      return NULL;
    }

    pc->media()->AddTransportFlow(level, rtcp, flow);
  }

  return flow;
}
