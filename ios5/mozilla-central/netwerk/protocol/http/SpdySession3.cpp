/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHttp.h"
#include "SpdySession3.h"
#include "SpdyStream3.h"
#include "nsHttpConnection.h"
#include "nsHttpHandler.h"
#include "prnetdb.h"
#include "mozilla/Telemetry.h"
#include "mozilla/Preferences.h"
#include "prprf.h"

#ifdef DEBUG
// defined by the socket transport service while active
extern PRThread *gSocketThread;
#endif

namespace mozilla {
namespace net {

// SpdySession3 has multiple inheritance of things that implement
// nsISupports, so this magic is taken from nsHttpPipeline that
// implements some of the same abstract classes.
NS_IMPL_THREADSAFE_ADDREF(SpdySession3)
NS_IMPL_THREADSAFE_RELEASE(SpdySession3)
NS_INTERFACE_MAP_BEGIN(SpdySession3)
    NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsAHttpConnection)
NS_INTERFACE_MAP_END

SpdySession3::SpdySession3(nsAHttpTransaction *aHttpTransaction,
                         nsISocketTransport *aSocketTransport,
                         PRInt32 firstPriority)
  : mSocketTransport(aSocketTransport),
    mSegmentReader(nsnull),
    mSegmentWriter(nsnull),
    mSendingChunkSize(ASpdySession::kSendingChunkSize),
    mNextStreamID(1),
    mConcurrentHighWater(0),
    mDownstreamState(BUFFERING_FRAME_HEADER),
    mInputFrameBufferSize(kDefaultBufferSize),
    mInputFrameBufferUsed(0),
    mInputFrameDataLast(false),
    mInputFrameDataStream(nsnull),
    mNeedsCleanup(nsnull),
    mShouldGoAway(false),
    mClosed(false),
    mCleanShutdown(false),
    mDataPending(false),
    mGoAwayID(0),
    mMaxConcurrent(kDefaultMaxConcurrent),
    mConcurrent(0),
    mServerPushedResources(0),
    mServerInitialWindow(kDefaultServerRwin),
    mOutputQueueSize(kDefaultQueueSize),
    mOutputQueueUsed(0),
    mOutputQueueSent(0),
    mLastReadEpoch(PR_IntervalNow()),
    mPingSentEpoch(0),
    mNextPingID(1),
    mPingThresholdExperiment(false)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  LOG3(("SpdySession3::SpdySession3 %p transaction 1 = %p",
        this, aHttpTransaction));
  
  mStreamIDHash.Init();
  mStreamTransactionHash.Init();
  mConnection = aHttpTransaction->Connection();
  mInputFrameBuffer = new char[mInputFrameBufferSize];
  mOutputQueueBuffer = new char[mOutputQueueSize];
  zlibInit();
  
  mSendingChunkSize = gHttpHandler->SpdySendingChunkSize();
  GenerateSettings();

  if (!aHttpTransaction->IsNullTransaction())
    AddStream(aHttpTransaction, firstPriority);
  mLastDataReadEpoch = mLastReadEpoch;
  
  DeterminePingThreshold();
}

void
SpdySession3::DeterminePingThreshold()
{
  mPingThreshold = gHttpHandler->SpdyPingThreshold();

  if (!mPingThreshold || !gHttpHandler->AllowExperiments())
    return;

  PRUint32 randomVal = gHttpHandler->Get32BitsOfPseudoRandom();
  
  // Use the lower 10 bits to select 1 in 1024 sessions for the
  // ping threshold experiment. Somewhat less than that will actually be
  // used because random values greater than the total http idle timeout
  // for the session are discarded.
  if ((randomVal & 0x3ff) != 1)  // lottery
    return;
  
  randomVal = randomVal >> 10; // those bits are used up

  // This session has been selected - use a random ping threshold of 10 +
  // a random number from 0 to 255, based on the next 8 bits of the
  // random buffer
  PRIntervalTime randomThreshold =
    PR_SecondsToInterval((randomVal & 0xff) + 10);
  if (randomThreshold > gHttpHandler->IdleTimeout())
    return;
  
  mPingThreshold = randomThreshold;
  mPingThresholdExperiment = true;
  LOG3(("SpdySession3 %p Ping Threshold Experimental Selection : %dsec\n",
        this, PR_IntervalToSeconds(mPingThreshold)));
}

PLDHashOperator
SpdySession3::ShutdownEnumerator(nsAHttpTransaction *key,
                                nsAutoPtr<SpdyStream3> &stream,
                                void *closure)
{
  SpdySession3 *self = static_cast<SpdySession3 *>(closure);
 
  // On a clean server hangup the server sets the GoAwayID to be the ID of
  // the last transaction it processed. If the ID of stream in the
  // local session is greater than that it can safely be restarted because the
  // server guarantees it was not partially processed.
  if (self->mCleanShutdown && (stream->StreamID() > self->mGoAwayID))
    self->CloseStream(stream, NS_ERROR_NET_RESET); // can be restarted
  else
    self->CloseStream(stream, NS_ERROR_ABORT);

  return PL_DHASH_NEXT;
}

SpdySession3::~SpdySession3()
{
  LOG3(("SpdySession3::~SpdySession3 %p mDownstreamState=%X",
        this, mDownstreamState));

  inflateEnd(&mDownstreamZlib);
  deflateEnd(&mUpstreamZlib);
  
  mStreamTransactionHash.Enumerate(ShutdownEnumerator, this);
  Telemetry::Accumulate(Telemetry::SPDY_PARALLEL_STREAMS, mConcurrentHighWater);
  Telemetry::Accumulate(Telemetry::SPDY_REQUEST_PER_CONN, (mNextStreamID - 1) / 2);
  Telemetry::Accumulate(Telemetry::SPDY_SERVER_INITIATED_STREAMS,
                        mServerPushedResources);
}

void
SpdySession3::LogIO(SpdySession3 *self, SpdyStream3 *stream, const char *label,
                   const char *data, PRUint32 datalen)
{
  if (!LOG4_ENABLED())
    return;
  
  LOG4(("SpdySession3::LogIO %p stream=%p id=0x%X [%s]",
        self, stream, stream ? stream->StreamID() : 0, label));

  // Max line is (16 * 3) + 10(prefix) + newline + null
  char linebuf[128];
  PRUint32 index;
  char *line = linebuf;

  linebuf[127] = 0;

  for (index = 0; index < datalen; ++index) {
    if (!(index % 16)) {
      if (index) {
        *line = 0;
        LOG4(("%s", linebuf));
      }
      line = linebuf;
      PR_snprintf(line, 128, "%08X: ", index);
      line += 10;
    }
    PR_snprintf(line, 128 - (line - linebuf), "%02X ",
                ((unsigned char *)data)[index]);
    line += 3;
  }
  if (index) {
    *line = 0;
    LOG4(("%s", linebuf));
  }
}

typedef nsresult  (*Control_FX) (SpdySession3 *self);
static Control_FX sControlFunctions[] = 
{
  nsnull,
  SpdySession3::HandleSynStream,
  SpdySession3::HandleSynReply,
  SpdySession3::HandleRstStream,
  SpdySession3::HandleSettings,
  SpdySession3::HandleNoop,
  SpdySession3::HandlePing,
  SpdySession3::HandleGoAway,
  SpdySession3::HandleHeaders,
  SpdySession3::HandleWindowUpdate
};

bool
SpdySession3::RoomForMoreConcurrent()
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  return (mConcurrent < mMaxConcurrent);
}

bool
SpdySession3::RoomForMoreStreams()
{
  if (mNextStreamID + mStreamTransactionHash.Count() * 2 > kMaxStreamID)
    return false;

  return !mShouldGoAway;
}

PRIntervalTime
SpdySession3::IdleTime()
{
  return PR_IntervalNow() - mLastDataReadEpoch;
}

void
SpdySession3::ReadTimeoutTick(PRIntervalTime now)
{
    NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
    NS_ABORT_IF_FALSE(mNextPingID & 1, "Ping Counter Not Odd");

    if (!mPingThreshold)
      return;

    LOG(("SpdySession3::ReadTimeoutTick %p delta since last read %ds\n",
         this, PR_IntervalToSeconds(now - mLastReadEpoch)));

    if ((now - mLastReadEpoch) < mPingThreshold) {
      // recent activity means ping is not an issue
      if (mPingSentEpoch)
        ClearPing(true);
      return;
    }

    if (mPingSentEpoch) {
      LOG(("SpdySession3::ReadTimeoutTick %p handle outstanding ping\n"));
      if ((now - mPingSentEpoch) >= gHttpHandler->SpdyPingTimeout()) {
        LOG(("SpdySession3::ReadTimeoutTick %p Ping Timer Exhaustion\n",
             this));
        ClearPing(false);
        Close(NS_ERROR_NET_TIMEOUT);
      }
      return;
    }
    
    LOG(("SpdySession3::ReadTimeoutTick %p generating ping 0x%X\n",
         this, mNextPingID));

    if (mNextPingID == 0xffffffff) {
      LOG(("SpdySession3::ReadTimeoutTick %p cannot form ping - ids exhausted\n",
           this));
      return;
    }

    mPingSentEpoch = PR_IntervalNow();
    if (!mPingSentEpoch)
      mPingSentEpoch = 1; // avoid the 0 sentinel value
    GeneratePing(mNextPingID);
    mNextPingID += 2;

    if (mNextPingID == 0xffffffff) {
      LOG(("SpdySession3::ReadTimeoutTick %p "
           "ping ids exhausted marking goaway\n", this));
      mShouldGoAway = true;
    }
}

void
SpdySession3::ClearPing(bool pingOK)
{
  mPingSentEpoch = 0;

  if (mPingThresholdExperiment) {
    LOG3(("SpdySession3::ClearPing %p mPingThresholdExperiment %dsec %s\n",
          this, PR_IntervalToSeconds(mPingThreshold),
          pingOK ? "pass" :"fail"));

    if (pingOK)
      Telemetry::Accumulate(Telemetry::SPDY_PING_EXPERIMENT_PASS,
                            PR_IntervalToSeconds(mPingThreshold));
    else
      Telemetry::Accumulate(Telemetry::SPDY_PING_EXPERIMENT_FAIL,
                            PR_IntervalToSeconds(mPingThreshold));
    mPingThreshold = gHttpHandler->SpdyPingThreshold();
    mPingThresholdExperiment = false;
  }
}

PRUint32
SpdySession3::RegisterStreamID(SpdyStream3 *stream)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  LOG3(("SpdySession3::RegisterStreamID session=%p stream=%p id=0x%X "
        "concurrent=%d",this, stream, mNextStreamID, mConcurrent));

  NS_ABORT_IF_FALSE(mNextStreamID < 0xfffffff0,
                    "should have stopped admitting streams");
  
  PRUint32 result = mNextStreamID;
  mNextStreamID += 2;

  // We've used up plenty of ID's on this session. Start
  // moving to a new one before there is a crunch involving
  // server push streams or concurrent non-registered submits
  if (mNextStreamID >= kMaxStreamID)
    mShouldGoAway = true;

  // integrity check
  if (mStreamIDHash.Get(result)) {
    LOG3(("   New ID already present\n"));
    NS_ABORT_IF_FALSE(false, "New ID already present in mStreamIDHash");
    mShouldGoAway = true;
    return kDeadStreamID;
  }

  mStreamIDHash.Put(result, stream);
  return result;
}

bool
SpdySession3::AddStream(nsAHttpTransaction *aHttpTransaction,
                       PRInt32 aPriority)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  NS_ABORT_IF_FALSE(!mStreamTransactionHash.Get(aHttpTransaction),
                    "AddStream duplicate transaction pointer");

  // integrity check
  if (mStreamTransactionHash.Get(aHttpTransaction)) {
    LOG3(("   New transaction already present\n"));
    NS_ABORT_IF_FALSE(false, "New transaction already present in hash");
    return false;
  }

  aHttpTransaction->SetConnection(this);
  SpdyStream3 *stream = new SpdyStream3(aHttpTransaction,
                                      this,
                                      mSocketTransport,
                                      mSendingChunkSize,
                                      &mUpstreamZlib,
                                      aPriority);

  
  LOG3(("SpdySession3::AddStream session=%p stream=%p NextID=0x%X (tentative)",
        this, stream, mNextStreamID));

  mStreamTransactionHash.Put(aHttpTransaction, stream);

  if (RoomForMoreConcurrent()) {
    LOG3(("SpdySession3::AddStream %p stream %p activated immediately.",
          this, stream));
    ActivateStream(stream);
  }
  else {
    LOG3(("SpdySession3::AddStream %p stream %p queued.",
          this, stream));
    mQueuedStreams.Push(stream);
  }
  
  return true;
}

void
SpdySession3::ActivateStream(SpdyStream3 *stream)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  mConcurrent++;
  if (mConcurrent > mConcurrentHighWater)
    mConcurrentHighWater = mConcurrent;
  LOG3(("SpdySession3::AddStream %p activating stream %p Currently %d "
        "streams in session, high water mark is %d",
        this, stream, mConcurrent, mConcurrentHighWater));

  mReadyForWrite.Push(stream);
  SetWriteCallbacks();

  // Kick off the SYN transmit without waiting for the poll loop
  // This won't work for stream id=1 because there is no segment reader
  // yet.
  if (mSegmentReader) {
    PRUint32 countRead;
    ReadSegments(nsnull, kDefaultBufferSize, &countRead);
  }
}

void
SpdySession3::ProcessPending()
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  while (RoomForMoreConcurrent()) {
    SpdyStream3 *stream = static_cast<SpdyStream3 *>(mQueuedStreams.PopFront());
    if (!stream)
      return;
    LOG3(("SpdySession3::ProcessPending %p stream %p activated from queue.",
          this, stream));
    ActivateStream(stream);
  }
}

nsresult
SpdySession3::NetworkRead(nsAHttpSegmentWriter *writer, char *buf,
                         PRUint32 count, PRUint32 *countWritten)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  nsresult rv = writer->OnWriteSegment(buf, count, countWritten);
  if (NS_SUCCEEDED(rv) && *countWritten > 0)
    mLastReadEpoch = PR_IntervalNow();
  return rv;
}

void
SpdySession3::SetWriteCallbacks()
{
  if (mConnection && (GetWriteQueueSize() || mOutputQueueUsed))
      mConnection->ResumeSend();
}

void
SpdySession3::FlushOutputQueue()
{
  if (!mSegmentReader || !mOutputQueueUsed)
    return;
  
  nsresult rv;
  PRUint32 countRead;
  PRUint32 avail = mOutputQueueUsed - mOutputQueueSent;

  rv = mSegmentReader->
    OnReadSegment(mOutputQueueBuffer.get() + mOutputQueueSent, avail,
                                     &countRead);
  LOG3(("SpdySession3::FlushOutputQueue %p sz=%d rv=%x actual=%d",
        this, avail, rv, countRead));
  
  // Dont worry about errors on write, we will pick this up as a read error too
  if (NS_FAILED(rv))
    return;
  
  if (countRead == avail) {
    mOutputQueueUsed = 0;
    mOutputQueueSent = 0;
    return;
  }

  mOutputQueueSent += countRead;

  // If the output queue is close to filling up and we have sent out a good
  // chunk of data from the beginning then realign it.
  
  if ((mOutputQueueSent >= kQueueMinimumCleanup) &&
      ((mOutputQueueSize - mOutputQueueUsed) < kQueueTailRoom)) {
    mOutputQueueUsed -= mOutputQueueSent;
    memmove(mOutputQueueBuffer.get(),
            mOutputQueueBuffer.get() + mOutputQueueSent,
            mOutputQueueUsed);
    mOutputQueueSent = 0;
  }
}

void
SpdySession3::DontReuse()
{
  mShouldGoAway = true;
  if (!mStreamTransactionHash.Count())
    Close(NS_OK);
}

PRUint32
SpdySession3::GetWriteQueueSize()
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  return mReadyForWrite.GetSize();
}

void
SpdySession3::ChangeDownstreamState(enum stateType newState)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  LOG3(("SpdyStream3::ChangeDownstreamState() %p from %X to %X",
        this, mDownstreamState, newState));
  mDownstreamState = newState;
}

void
SpdySession3::ResetDownstreamState()
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  LOG3(("SpdyStream3::ResetDownstreamState() %p", this));
  ChangeDownstreamState(BUFFERING_FRAME_HEADER);

  if (mInputFrameDataLast && mInputFrameDataStream) {
    mInputFrameDataLast = false;
    if (!mInputFrameDataStream->RecvdFin()) {
      mInputFrameDataStream->SetRecvdFin(true);
      --mConcurrent;
      ProcessPending();
    }
  }
  mInputFrameBufferUsed = 0;
  mInputFrameDataStream = nsnull;
}

void
SpdySession3::EnsureBuffer(nsAutoArrayPtr<char> &buf,
                          PRUint32 newSize,
                          PRUint32 preserve,
                          PRUint32 &objSize)
{
  if (objSize >= newSize)
      return;
  
  // Leave a little slop on the new allocation - add 2KB to
  // what we need and then round the result up to a 4KB (page)
  // boundary.

  objSize = (newSize + 2048 + 4095) & ~4095;
  
  nsAutoArrayPtr<char> tmp(new char[objSize]);
  memcpy(tmp, buf, preserve);
  buf = tmp;
}

void
SpdySession3::zlibInit()
{
  mDownstreamZlib.zalloc = SpdyStream3::zlib_allocator;
  mDownstreamZlib.zfree = SpdyStream3::zlib_destructor;
  mDownstreamZlib.opaque = Z_NULL;

  inflateInit(&mDownstreamZlib);

  mUpstreamZlib.zalloc = SpdyStream3::zlib_allocator;
  mUpstreamZlib.zfree = SpdyStream3::zlib_destructor;
  mUpstreamZlib.opaque = Z_NULL;

  deflateInit(&mUpstreamZlib, Z_DEFAULT_COMPRESSION);
  deflateSetDictionary(&mUpstreamZlib,
                       SpdyStream3::kDictionary,
                       sizeof(SpdyStream3::kDictionary));
}

// Need to decompress some data in order to keep the compression
// context correct, but we really don't care what the result is
nsresult
SpdySession3::UncompressAndDiscard(PRUint32 offset,
                                   PRUint32 blockLen)
{
  char *blockStart = mInputFrameBuffer + offset;
  unsigned char trash[2048];
  mDownstreamZlib.avail_in = blockLen;
  mDownstreamZlib.next_in = reinterpret_cast<unsigned char *>(blockStart);

  do {
    mDownstreamZlib.next_out = trash;
    mDownstreamZlib.avail_out = sizeof(trash);
    int zlib_rv = inflate(&mDownstreamZlib, Z_NO_FLUSH);

    if (zlib_rv == Z_NEED_DICT)
      inflateSetDictionary(&mDownstreamZlib, SpdyStream3::kDictionary,
                           sizeof(SpdyStream3::kDictionary));

    if (zlib_rv == Z_DATA_ERROR || zlib_rv == Z_MEM_ERROR)
      return NS_ERROR_FAILURE;
  }
  while (mDownstreamZlib.avail_in);
  return NS_OK;
}

void
SpdySession3::GeneratePing(PRUint32 aID)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession3::GeneratePing %p 0x%X\n", this, aID));

  EnsureBuffer(mOutputQueueBuffer, mOutputQueueUsed + 12,
               mOutputQueueUsed, mOutputQueueSize);
  char *packet = mOutputQueueBuffer.get() + mOutputQueueUsed;
  mOutputQueueUsed += 12;

  packet[0] = kFlag_Control;
  packet[1] = kVersion;
  packet[2] = 0;
  packet[3] = CONTROL_TYPE_PING;
  packet[4] = 0;                                  /* flags */
  packet[5] = 0;
  packet[6] = 0;
  packet[7] = 4;                                  /* length */
  
  aID = PR_htonl(aID);
  memcpy(packet + 8, &aID, 4);

  LogIO(this, nsnull, "Generate Ping", packet, 12);
  FlushOutputQueue();
}

void
SpdySession3::GenerateRstStream(PRUint32 aStatusCode, PRUint32 aID)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession3::GenerateRst %p 0x%X %d\n", this, aID, aStatusCode));

  EnsureBuffer(mOutputQueueBuffer, mOutputQueueUsed + 16,
               mOutputQueueUsed, mOutputQueueSize);
  char *packet = mOutputQueueBuffer.get() + mOutputQueueUsed;
  mOutputQueueUsed += 16;

  packet[0] = kFlag_Control;
  packet[1] = kVersion;
  packet[2] = 0;
  packet[3] = CONTROL_TYPE_RST_STREAM;
  packet[4] = 0;                                  /* flags */
  packet[5] = 0;
  packet[6] = 0;
  packet[7] = 8;                                  /* length */
  
  aID = PR_htonl(aID);
  memcpy(packet + 8, &aID, 4);
  aStatusCode = PR_htonl(aStatusCode);
  memcpy(packet + 12, &aStatusCode, 4);

  LogIO(this, nsnull, "Generate Reset", packet, 16);
  FlushOutputQueue();
}

void
SpdySession3::GenerateGoAway()
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession3::GenerateGoAway %p\n", this));

  EnsureBuffer(mOutputQueueBuffer, mOutputQueueUsed + 12,
               mOutputQueueUsed, mOutputQueueSize);
  char *packet = mOutputQueueBuffer.get() + mOutputQueueUsed;
  mOutputQueueUsed += 12;

  memset(packet, 0, 12);
  packet[0] = kFlag_Control;
  packet[1] = kVersion;
  packet[3] = CONTROL_TYPE_GOAWAY;
  packet[7] = 4;                                  /* data length */
  
  // last-good-stream-id are bytes 8-11, when we accept server push this will
  // need to be set non zero

  LogIO(this, nsnull, "Generate GoAway", packet, 12);
  FlushOutputQueue();
}

void
SpdySession3::GenerateSettings()
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession3::GenerateSettings %p\n", this));

  static const PRUint32 dataLen = 12;
  EnsureBuffer(mOutputQueueBuffer, mOutputQueueUsed + 8 + dataLen,
               mOutputQueueUsed, mOutputQueueSize);
  char *packet = mOutputQueueBuffer.get() + mOutputQueueUsed;
  mOutputQueueUsed += 8 + dataLen;

  memset(packet, 0, 8 + dataLen);
  packet[0] = kFlag_Control;
  packet[1] = kVersion;
  packet[3] = CONTROL_TYPE_SETTINGS;
  packet[7] = dataLen;
  
  packet[11] = 1;                                 /* 1 setting */
  packet[15] = SETTINGS_TYPE_INITIAL_WINDOW;
  PRUint32 rwin = PR_htonl(kInitialRwin);
  memcpy(packet + 16, &rwin, 4);

  LogIO(this, nsnull, "Generate Settings", packet, 8 + dataLen);
  FlushOutputQueue();
}

// perform a bunch of integrity checks on the stream.
// returns true if passed, false (plus LOG and ABORT) if failed.
bool
SpdySession3::VerifyStream(SpdyStream3 *aStream, PRUint32 aOptionalID = 0)
{
  // This is annoying, but at least it is O(1)
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  if (!aStream)
    return true;

  PRUint32 test = 0;
  
  do {
    if (aStream->StreamID() == kDeadStreamID)
      break;

    nsAHttpTransaction *trans = aStream->Transaction();

    test++;  
    if (!trans)
      break;

    test++;
    if (mStreamTransactionHash.Get(trans) != aStream)
      break;
    
    if (aStream->StreamID()) {
      SpdyStream3 *idStream = mStreamIDHash.Get(aStream->StreamID());

      test++;
      if (idStream != aStream)
        break;

      if (aOptionalID) {
        test++;
        if (idStream->StreamID() != aOptionalID)
          break;
      }
    }

    // tests passed
    return true;
  } while (0);

  LOG(("SpdySession3 %p VerifyStream Failure %p stream->id=0x%X "
       "optionalID=0x%X trans=%p test=%d\n",
       this, aStream, aStream->StreamID(),
       aOptionalID, aStream->Transaction(), test));
  NS_ABORT_IF_FALSE(false, "VerifyStream");
  return false;
}

void
SpdySession3::CleanupStream(SpdyStream3 *aStream, nsresult aResult,
                           rstReason aResetCode)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession3::CleanupStream %p %p 0x%X %X\n",
        this, aStream, aStream->StreamID(), aResult));

  if (!VerifyStream(aStream)) {
    LOG(("SpdySession3::CleanupStream failed to verify stream\n"));
    return;
  }

  if (!aStream->RecvdFin() && aStream->StreamID()) {
    LOG3(("Stream had not processed recv FIN, sending RST code %X\n",
          aResetCode));
    GenerateRstStream(aResetCode, aStream->StreamID());
    --mConcurrent;
    ProcessPending();
  }
  
  CloseStream(aStream, aResult);

  // Remove the stream from the ID hash table. (this one isn't short, which is
  // why it is hashed.)
  mStreamIDHash.Remove(aStream->StreamID());

  // removing from the stream transaction hash will
  // delete the SpdyStream3 and drop the reference to
  // its transaction
  mStreamTransactionHash.Remove(aStream->Transaction());

  if (mShouldGoAway && !mStreamTransactionHash.Count())
    Close(NS_OK);
}

void
SpdySession3::CloseStream(SpdyStream3 *aStream, nsresult aResult)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession3::CloseStream %p %p 0x%x %X\n",
        this, aStream, aStream->StreamID(), aResult));

  // Check if partial frame reader
  if (aStream == mInputFrameDataStream) {
    LOG3(("Stream had active partial read frame on close"));
    ChangeDownstreamState(DISCARDING_DATA_FRAME);
    mInputFrameDataStream = nsnull;
  }

  // check the streams blocked on write, this is linear but the list
  // should be pretty short.
  PRUint32 size = mReadyForWrite.GetSize();
  for (PRUint32 count = 0; count < size; ++count) {
    SpdyStream3 *stream = static_cast<SpdyStream3 *>(mReadyForWrite.PopFront());
    if (stream != aStream)
      mReadyForWrite.Push(stream);
  }

  // Check the streams queued for activation. Because we normally accept a high
  // level of parallelization this should also be short.
  size = mQueuedStreams.GetSize();
  for (PRUint32 count = 0; count < size; ++count) {
    SpdyStream3 *stream = static_cast<SpdyStream3 *>(mQueuedStreams.PopFront());
    if (stream != aStream)
      mQueuedStreams.Push(stream);
  }

  // Send the stream the close() indication
  aStream->Close(aResult);
}

nsresult
SpdySession3::HandleSynStream(SpdySession3 *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_SYN_STREAM,
                    "wrong control type");
  
  if (self->mInputFrameDataSize < 18) {
    LOG3(("SpdySession3::HandleSynStream %p SYN_STREAM too short data=%d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  PRUint32 streamID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);
  PRUint32 associatedID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[3]);

  LOG3(("SpdySession3::HandleSynStream %p recv SYN_STREAM (push) "
        "for ID 0x%X associated with 0x%X.",
        self, streamID, associatedID));
    
  if (streamID & 0x01) {                   // test for odd stream ID
    LOG3(("SpdySession3::HandleSynStream %p recvd SYN_STREAM id must be even.",
          self));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  ++(self->mServerPushedResources);

  // Anytime we start using the high bit of stream ID (either client or server)
  // begin to migrate to a new session.
  if (streamID >= kMaxStreamID)
    self->mShouldGoAway = true;

  // Need to decompress the headers even though we aren't using them yet in
  // order to keep the compression context consistent for other syn_reply frames
  nsresult rv =
    self->UncompressAndDiscard(18, self->mInputFrameDataSize - 10);
  if (NS_FAILED(rv)) {
    LOG(("SpdySession3::HandleSynStream uncompress failed\n"));
    return rv;
  }

  // todo populate cache. For now, just reject server push p3
  self->GenerateRstStream(RST_REFUSED_STREAM, streamID);
  self->ResetDownstreamState();
  return NS_OK;
}

nsresult
SpdySession3::SetInputFrameDataStream(PRUint32 streamID)
{
  mInputFrameDataStream = mStreamIDHash.Get(streamID);
  if (VerifyStream(mInputFrameDataStream, streamID))
    return NS_OK;

  LOG(("SpdySession3::SetInputFrameDataStream failed to verify 0x%X\n",
       streamID));
  mInputFrameDataStream = nsnull;
  return NS_ERROR_UNEXPECTED;
}

nsresult
SpdySession3::HandleSynReply(SpdySession3 *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_SYN_REPLY,
                    "wrong control type");

  if (self->mInputFrameDataSize < 4) {
    LOG3(("SpdySession3::HandleSynReply %p SYN REPLY too short data=%d",
          self, self->mInputFrameDataSize));
    // A framing error is a session wide error that cannot be recovered
    return NS_ERROR_ILLEGAL_VALUE;
  }
  
  LOG3(("SpdySession3::HandleSynReply %p lookup via streamID in syn_reply.\n",
        self));
  PRUint32 streamID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);
  nsresult rv = self->SetInputFrameDataStream(streamID);
  if (NS_FAILED(rv))
    return rv;

  if (!self->mInputFrameDataStream) {
    // Cannot find stream. We can continue the SPDY session, but we need to
    // uncompress the header block to maintain the correct compression context

    LOG3(("SpdySession3::HandleSynReply %p lookup streamID in syn_reply "
          "0x%X failed. NextStreamID = 0x%X\n",
          self, streamID, self->mNextStreamID));

    if (streamID >= self->mNextStreamID)
      self->GenerateRstStream(RST_INVALID_STREAM, streamID);
    
    if (NS_FAILED(self->UncompressAndDiscard(12,
                                             self->mInputFrameDataSize - 4))) {
      LOG(("SpdySession3::HandleSynReply uncompress failed\n"));
      // this is fatal to the session
      return NS_ERROR_FAILURE;
    }

    self->ResetDownstreamState();
    return NS_OK;
  }

  // Uncompress the headers into a stream specific buffer, leaving them in
  // spdy format for the time being. Make certain to do this
  // step before any error handling that might abort the stream but not
  // the session becuase the session compression context will become
  // inconsistent if all of the compressed data is not processed.
  rv = self->mInputFrameDataStream->Uncompress(&self->mDownstreamZlib,
                                               self->mInputFrameBuffer + 12,
                                               self->mInputFrameDataSize - 4);

  if (NS_FAILED(rv)) {
    LOG(("SpdySession3::HandleSynReply uncompress failed\n"));
    return NS_ERROR_FAILURE;
  }

  if (self->mInputFrameDataStream->GetFullyOpen()) {
    // "If an endpoint receives multiple SYN_REPLY frames for the same active
    // stream ID, it MUST issue a stream error (Section 2.4.2) with the error
    // code STREAM_IN_USE."
    //
    // "STREAM_ALREADY_CLOSED. The endpoint received a data or SYN_REPLY
    // frame for a stream which is half closed."
    //
    // If the stream is open then just RST_STREAM with STREAM_IN_USE
    // If the stream is half closed then RST_STREAM with STREAM_ALREADY_CLOSED
    // abort the session
    //
    LOG3(("SpdySession3::HandleSynReply %p dup SYN_REPLY for 0x%X"
          " recvdfin=%d", self, self->mInputFrameDataStream->StreamID(),
          self->mInputFrameDataStream->RecvdFin()));

    self->CleanupStream(self->mInputFrameDataStream, NS_ERROR_ALREADY_OPENED,
                        self->mInputFrameDataStream->RecvdFin() ? 
                        RST_STREAM_ALREADY_CLOSED : RST_STREAM_IN_USE);
    self->ResetDownstreamState();
    return NS_OK;
  }
  self->mInputFrameDataStream->SetFullyOpen();

  self->mInputFrameDataLast = self->mInputFrameBuffer[4] & kFlag_Data_FIN;
  self->mInputFrameDataStream->UpdateTransportReadEvents(self->mInputFrameDataSize);
  self->mLastDataReadEpoch = self->mLastReadEpoch;

  if (self->mInputFrameBuffer[4] & ~kFlag_Data_FIN) {
    LOG3(("SynReply %p had undefined flag set 0x%X\n", self, streamID));
    self->CleanupStream(self->mInputFrameDataStream, NS_ERROR_ILLEGAL_VALUE,
                        RST_PROTOCOL_ERROR);
    self->ResetDownstreamState();
    return NS_OK;
  }

  if (!self->mInputFrameDataLast) {
    // don't process the headers yet as there could be more coming from HEADERS
    // frames
    self->ResetDownstreamState();
    return NS_OK;
  }

  rv = self->ResponseHeadersComplete();
  if (rv == NS_ERROR_ILLEGAL_VALUE) {
    LOG3(("SpdySession3::HandleSynReply %p PROTOCOL_ERROR detected 0x%X\n",
          self, streamID));
    self->CleanupStream(self->mInputFrameDataStream, rv, RST_PROTOCOL_ERROR);
    self->ResetDownstreamState();
    rv = NS_OK;
  }
  return rv;
}

// ResponseHeadersComplete() returns NS_ERROR_ILLEGAL_VALUE when the stream
// should be reset with a PROTOCOL_ERROR, NS_OK when the SYN_REPLY was
// fine, and any other error is fatal to the session.
nsresult
SpdySession3::ResponseHeadersComplete()
{
  LOG3(("SpdySession3::ResponseHeadersComplete %p for 0x%X fin=%d",
        this, mInputFrameDataStream->StreamID(), mInputFrameDataLast));

  // The spdystream needs to see flattened http headers
  // Uncompressed spdy format headers currently live in
  // SpdyStream3::mDecompressBuffer - convert that to HTTP format in
  // mFlatHTTPResponseHeaders via ConvertHeaders()

  mFlatHTTPResponseHeadersOut = 0;
  nsresult rv = mInputFrameDataStream->ConvertHeaders(mFlatHTTPResponseHeaders);
  if (NS_FAILED(rv))
    return rv;

  ChangeDownstreamState(PROCESSING_COMPLETE_HEADERS);
  return NS_OK;
}

nsresult
SpdySession3::HandleRstStream(SpdySession3 *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_RST_STREAM,
                    "wrong control type");

  if (self->mInputFrameDataSize != 8) {
    LOG3(("SpdySession3::HandleRstStream %p RST_STREAM wrong length data=%d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  PRUint8 flags = reinterpret_cast<PRUint8 *>(self->mInputFrameBuffer.get())[4];

  PRUint32 streamID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);

  self->mDownstreamRstReason =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[3]);

  LOG3(("SpdySession3::HandleRstStream %p RST_STREAM Reason Code %u ID %x "
        "flags %x", self, self->mDownstreamRstReason, streamID, flags));

  if (flags != 0) {
    LOG3(("SpdySession3::HandleRstStream %p RST_STREAM with flags is illegal",
          self));
    return NS_ERROR_ILLEGAL_VALUE;
  }
  
  if (self->mDownstreamRstReason == RST_INVALID_STREAM ||
      self->mDownstreamRstReason == RST_STREAM_IN_USE ||
      self->mDownstreamRstReason == RST_FLOW_CONTROL_ERROR) {
    // basically just ignore this
    LOG3(("SpdySession3::HandleRstStream %p No Reset Processing Needed.\n"));
    self->ResetDownstreamState();
    return NS_OK;
  }

  nsresult rv = self->SetInputFrameDataStream(streamID);

  if (!self->mInputFrameDataStream) {
    if (NS_FAILED(rv))
      LOG(("SpdySession3::HandleRstStream %p lookup streamID for RST Frame "
           "0x%X failed reason = %d :: VerifyStream Failed\n", self, streamID,
           self->mDownstreamRstReason));

    LOG3(("SpdySession3::HandleRstStream %p lookup streamID for RST Frame "
          "0x%X failed reason = %d", self, streamID,
          self->mDownstreamRstReason));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  self->ChangeDownstreamState(PROCESSING_CONTROL_RST_STREAM);
  return NS_OK;
}

PLDHashOperator
SpdySession3::UpdateServerRwinEnumerator(nsAHttpTransaction *key,
                                         nsAutoPtr<SpdyStream3> &stream,
                                         void *closure)
{
  PRInt32 delta = *(static_cast<PRInt32 *>(closure));
  stream->UpdateRemoteWindow(delta);
  return PL_DHASH_NEXT;
}

nsresult
SpdySession3::HandleSettings(SpdySession3 *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_SETTINGS,
                    "wrong control type");

  if (self->mInputFrameDataSize < 4) {
    LOG3(("SpdySession3::HandleSettings %p SETTINGS wrong length data=%d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  PRUint32 numEntries =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);

  // Ensure frame is large enough for supplied number of entries
  // Each entry is 8 bytes, frame data is reduced by 4 to account for
  // the NumEntries value.
  if ((self->mInputFrameDataSize - 4) < (numEntries * 8)) {
    LOG3(("SpdySession3::HandleSettings %p SETTINGS wrong length data=%d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  LOG3(("SpdySession3::HandleSettings %p SETTINGS Control Frame with %d entries",
        self, numEntries));

  for (PRUint32 index = 0; index < numEntries; ++index) {
    unsigned char *setting = reinterpret_cast<unsigned char *>
      (self->mInputFrameBuffer.get()) + 12 + index * 8;

    PRUint32 flags = setting[0];
    PRUint32 id = PR_ntohl(reinterpret_cast<PRUint32 *>(setting)[0]) & 0xffffff;
    PRUint32 value =  PR_ntohl(reinterpret_cast<PRUint32 *>(setting)[1]);

    LOG3(("Settings ID %d, Flags %X, Value %d", id, flags, value));

    switch (id)
    {
    case SETTINGS_TYPE_UPLOAD_BW:
      Telemetry::Accumulate(Telemetry::SPDY_SETTINGS_UL_BW, value);
      break;
      
    case SETTINGS_TYPE_DOWNLOAD_BW:
      Telemetry::Accumulate(Telemetry::SPDY_SETTINGS_DL_BW, value);
      break;
      
    case SETTINGS_TYPE_RTT:
      Telemetry::Accumulate(Telemetry::SPDY_SETTINGS_RTT, value);
      break;
      
    case SETTINGS_TYPE_MAX_CONCURRENT:
      self->mMaxConcurrent = value;
      Telemetry::Accumulate(Telemetry::SPDY_SETTINGS_MAX_STREAMS, value);
      break;
      
    case SETTINGS_TYPE_CWND:
      Telemetry::Accumulate(Telemetry::SPDY_SETTINGS_CWND, value);
      break;
      
    case SETTINGS_TYPE_DOWNLOAD_RETRANS_RATE:
      Telemetry::Accumulate(Telemetry::SPDY_SETTINGS_RETRANS, value);
      break;
      
    case SETTINGS_TYPE_INITIAL_WINDOW:
      Telemetry::Accumulate(Telemetry::SPDY_SETTINGS_IW, value >> 10);
      {
        PRInt32 delta = value - self->mServerInitialWindow;
        self->mServerInitialWindow = value;

        // we need to add the delta to all open streams (delta can be negative)
        self->mStreamTransactionHash.Enumerate(UpdateServerRwinEnumerator,
                                               &delta);
      }
      break;
      
    default:
      break;
    }
    
  }
  
  self->ResetDownstreamState();
  return NS_OK;
}

nsresult
SpdySession3::HandleNoop(SpdySession3 *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_NOOP,
                    "wrong control type");

  // Should not be receiving noop frames in spdy/3, so we'll just
  // make a log and ignore it

  LOG3(("SpdySession3::HandleNoop %p NOP.", self));

  self->ResetDownstreamState();
  return NS_OK;
}

nsresult
SpdySession3::HandlePing(SpdySession3 *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_PING,
                    "wrong control type");

  if (self->mInputFrameDataSize != 4) {
    LOG3(("SpdySession3::HandlePing %p PING had wrong amount of data %d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  PRUint32 pingID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);

  LOG3(("SpdySession3::HandlePing %p PING ID 0x%X.", self, pingID));

  if (pingID & 0x01) {
    // presumably a reply to our timeout ping
    self->ClearPing(true);
  }
  else {
    // Servers initiate even numbered pings, go ahead and echo it back
    self->GeneratePing(pingID);
  }
    
  self->ResetDownstreamState();
  return NS_OK;
}

nsresult
SpdySession3::HandleGoAway(SpdySession3 *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_GOAWAY,
                    "wrong control type");

  if (self->mInputFrameDataSize != 4) {
    LOG3(("SpdySession3::HandleGoAway %p GOAWAY had wrong amount of data %d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  self->mShouldGoAway = true;
  self->mGoAwayID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);
  self->mCleanShutdown = true;
  
  LOG3(("SpdySession3::HandleGoAway %p GOAWAY Last-Good-ID 0x%X.",
        self, self->mGoAwayID));
  self->ResumeRecv();
  self->ResetDownstreamState();
  return NS_OK;
}

nsresult
SpdySession3::HandleHeaders(SpdySession3 *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_HEADERS,
                    "wrong control type");

  if (self->mInputFrameDataSize < 4) {
    LOG3(("SpdySession3::HandleHeaders %p HEADERS had wrong amount of data %d",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  PRUint32 streamID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);
  LOG3(("SpdySession3::HandleHeaders %p HEADERS for Stream 0x%X.\n",
        self, streamID));
  nsresult rv = self->SetInputFrameDataStream(streamID);
  if (NS_FAILED(rv))
    return rv;

  if (!self->mInputFrameDataStream) {
    LOG3(("SpdySession3::HandleHeaders %p lookup streamID 0x%X failed.\n",
          self, streamID));
    if (streamID >= self->mNextStreamID)
      self->GenerateRstStream(RST_INVALID_STREAM, streamID);

    if (NS_FAILED(self->UncompressAndDiscard(12,
                                             self->mInputFrameDataSize - 4))) {
      LOG(("SpdySession3::HandleSynReply uncompress failed\n"));
      // this is fatal to the session
      return NS_ERROR_FAILURE;
    }
    self->ResetDownstreamState();
    return NS_OK;
  }

  // Uncompress the headers into local buffers in the SpdyStream, leaving
  // them in spdy format for the time being. Make certain to do this
  // step before any error handling that might abort the stream but not
  // the session becuase the session compression context will become
  // inconsistent if all of the compressed data is not processed.
  rv = self->mInputFrameDataStream->Uncompress(&self->mDownstreamZlib,
                                               self->mInputFrameBuffer + 12,
                                               self->mInputFrameDataSize - 4);
  if (NS_FAILED(rv)) {
    LOG(("SpdySession3::HandleHeaders uncompress failed\n"));
    return NS_ERROR_FAILURE;
  }

  self->mInputFrameDataLast = self->mInputFrameBuffer[4] & kFlag_Data_FIN;
  self->mInputFrameDataStream->
    UpdateTransportReadEvents(self->mInputFrameDataSize);
  self->mLastDataReadEpoch = self->mLastReadEpoch;

  if (self->mInputFrameBuffer[4] & ~kFlag_Data_FIN) {
    LOG3(("Headers %p had undefined flag set 0x%X\n", self, streamID));
    self->CleanupStream(self->mInputFrameDataStream, NS_ERROR_ILLEGAL_VALUE,
                        RST_PROTOCOL_ERROR);
    self->ResetDownstreamState();
    return NS_OK;
  }

  if (!self->mInputFrameDataLast) {
    // don't process the headers yet as there could be more HEADERS frames
    self->ResetDownstreamState();
    return NS_OK;
  }

  rv = self->ResponseHeadersComplete();
  if (rv == NS_ERROR_ILLEGAL_VALUE) {
    LOG3(("SpdySession3::HanndleHeaders %p PROTOCOL_ERROR detected 0x%X\n",
          self, streamID));
    self->CleanupStream(self->mInputFrameDataStream, rv, RST_PROTOCOL_ERROR);
    self->ResetDownstreamState();
    rv = NS_OK;
  }
  return rv;
}

nsresult
SpdySession3::HandleWindowUpdate(SpdySession3 *self)
{
  NS_ABORT_IF_FALSE(self->mFrameControlType == CONTROL_TYPE_WINDOW_UPDATE,
                    "wrong control type");

  if (self->mInputFrameDataSize < 8) {
    LOG3(("SpdySession3::HandleWindowUpdate %p Window Update wrong length %d\n",
          self, self->mInputFrameDataSize));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  PRUint32 delta =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[3]);
  delta &= 0x7fffffff;
  PRUint32 streamID =
    PR_ntohl(reinterpret_cast<PRUint32 *>(self->mInputFrameBuffer.get())[2]);
  streamID &= 0x7fffffff;

  LOG3(("SpdySession3::HandleWindowUpdate %p len=%d for Stream 0x%X.\n",
        self, delta, streamID));
  nsresult rv = self->SetInputFrameDataStream(streamID);
  if (NS_FAILED(rv))
    return rv;

  if (!self->mInputFrameDataStream) {
    LOG3(("SpdySession3::HandleWindowUpdate %p lookup streamID 0x%X failed.\n",
          self, streamID));
    if (streamID >= self->mNextStreamID)
      self->GenerateRstStream(RST_INVALID_STREAM, streamID);
    self->ResetDownstreamState();
    return NS_OK;
  }

  PRInt64 oldRemoteWindow = self->mInputFrameDataStream->RemoteWindow();
  self->mInputFrameDataStream->UpdateRemoteWindow(delta);
  
  LOG3(("SpdySession3::HandleWindowUpdate %p stream 0x%X window "
        "%d increased by %d.\n", self, streamID, oldRemoteWindow, delta));

  // If the stream had a <=0 window, that has now opened
  // schedule it for writing again
  if (oldRemoteWindow <= 0 &&
      self->mInputFrameDataStream->RemoteWindow() > 0) {
    self->mReadyForWrite.Push(self->mInputFrameDataStream);
    self->SetWriteCallbacks();
  }

  self->ResetDownstreamState();
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsAHttpTransaction. It is expected that nsHttpConnection is the caller
// of these methods
//-----------------------------------------------------------------------------

void
SpdySession3::OnTransportStatus(nsITransport* aTransport,
                               nsresult aStatus,
                               PRUint64 aProgress)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  switch (aStatus) {
    // These should appear only once, deliver to the first
    // transaction on the session.
  case NS_NET_STATUS_RESOLVING_HOST:
  case NS_NET_STATUS_RESOLVED_HOST:
  case NS_NET_STATUS_CONNECTING_TO:
  case NS_NET_STATUS_CONNECTED_TO:
  {
    SpdyStream3 *target = mStreamIDHash.Get(1);
    if (target)
      target->Transaction()->OnTransportStatus(aTransport, aStatus, aProgress);
    break;
  }

  default:
    // The other transport events are ignored here because there is no good
    // way to map them to the right transaction in spdy. Instead, the events
    // are generated again from the spdy code and passed directly to the
    // correct transaction.

    // NS_NET_STATUS_SENDING_TO:
    // This is generated by the socket transport when (part) of
    // a transaction is written out
    //
    // There is no good way to map it to the right transaction in spdy,
    // so it is ignored here and generated separately when the SYN_STREAM
    // is sent from SpdyStream3::TransmitFrame

    // NS_NET_STATUS_WAITING_FOR:
    // Created by nsHttpConnection when the request has been totally sent.
    // There is no good way to map it to the right transaction in spdy,
    // so it is ignored here and generated separately when the same
    // condition is complete in SpdyStream3 when there is no more
    // request body left to be transmitted.

    // NS_NET_STATUS_RECEIVING_FROM
    // Generated in spdysession whenever we read a data frame or a syn_reply
    // that can be attributed to a particular stream/transaction

    break;
  }
}

// ReadSegments() is used to write data to the network. Generally, HTTP
// request data is pulled from the approriate transaction and
// converted to SPDY data. Sometimes control data like window-update are
// generated instead.

nsresult
SpdySession3::ReadSegments(nsAHttpSegmentReader *reader,
                          PRUint32 count,
                          PRUint32 *countRead)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  
  NS_ABORT_IF_FALSE(!mSegmentReader || !reader || (mSegmentReader == reader),
                    "Inconsistent Write Function Callback");

  if (reader)
    mSegmentReader = reader;

  nsresult rv;
  *countRead = 0;

  LOG3(("SpdySession3::ReadSegments %p", this));

  NS_ABORT_IF_FALSE(!mSegmentReader || !reader || (mSegmentReader == reader),
                    "Inconsistent Write Function Callback");

  if (reader)
    mSegmentReader = reader;

  SpdyStream3 *stream = static_cast<SpdyStream3 *>(mReadyForWrite.PopFront());
  if (!stream) {
    LOG3(("SpdySession3 %p could not identify a stream to write; suspending.",
          this));
    FlushOutputQueue();
    SetWriteCallbacks();
    return NS_BASE_STREAM_WOULD_BLOCK;
  }
  
  LOG3(("SpdySession3 %p will write from SpdyStream3 %p 0x%X "
        "block-input=%d block-output=%d\n", this, stream, stream->StreamID(),
        stream->RequestBlockedOnRead(), stream->BlockedOnRwin()));

  rv = stream->ReadSegments(this, count, countRead);

  // Not every permutation of stream->ReadSegents produces data (and therefore
  // tries to flush the output queue) - SENDING_FIN_STREAM can be an example
  // of that. But we might still have old data buffered that would be good
  // to flush.
  FlushOutputQueue();

  if (stream->RequestBlockedOnRead()) {
    
    // We are blocked waiting for input - either more http headers or
    // any request body data. When more data from the request stream
    // becomes available the httptransaction will call conn->ResumeSend().
    
    LOG3(("SpdySession3::ReadSegments %p dealing with block on read", this));

    // call readsegments again if there are other streams ready
    // to run in this session
    if (GetWriteQueueSize())
      rv = NS_OK;
    else
      rv = NS_BASE_STREAM_WOULD_BLOCK;
    SetWriteCallbacks();
    return rv;
  }
  
  if (NS_FAILED(rv)) {
    LOG3(("SpdySession3::ReadSegments %p returning FAIL code %X",
          this, rv));
    if (rv != NS_BASE_STREAM_WOULD_BLOCK)
      CleanupStream(stream, rv, RST_CANCEL);
    return rv;
  }
  
  if (*countRead > 0) {
    LOG3(("SpdySession3::ReadSegments %p stream=%p countread=%d",
          this, stream, *countRead));
    mReadyForWrite.Push(stream);
    SetWriteCallbacks();
    return rv;
  }

  if (stream->BlockedOnRwin()) {
    LOG3(("SpdySession3 %p will stream %p 0x%X suspended for flow control\n",
          this, stream, stream->StreamID()));
    return NS_BASE_STREAM_WOULD_BLOCK;
  }
  
  LOG3(("SpdySession3::ReadSegments %p stream=%p stream send complete",
        this, stream));
  
  /* we now want to recv data */
  ResumeRecv();

  // call readsegments again if there are other streams ready
  // to go in this session
  SetWriteCallbacks();

  return rv;
}

// WriteSegments() is used to read data off the socket. Generally this is
// just the SPDY frame header and from there the appropriate SPDYStream
// is identified from the Stream-ID. The http transaction associated with
// that read then pulls in the data directly, which it will feed to
// OnWriteSegment(). That function will gateway it into http and feed
// it to the appropriate transaction.

// we call writer->OnWriteSegment via NetworkRead() to get a spdy header.. 
// and decide if it is data or control.. if it is control, just deal with it.
// if it is data, identify the spdy stream
// call stream->WriteSegemnts which can call this::OnWriteSegment to get the
// data. It always gets full frames if they are part of the stream

nsresult
SpdySession3::WriteSegments(nsAHttpSegmentWriter *writer,
                           PRUint32 count,
                           PRUint32 *countWritten)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  
  nsresult rv;
  *countWritten = 0;

  if (mClosed)
    return NS_ERROR_FAILURE;

  SetWriteCallbacks();
  
  // We buffer all control frames and act on them in this layer.
  // We buffer the first 8 bytes of data frames (the header) but
  // the actual data is passed through unprocessed.
  
  if (mDownstreamState == BUFFERING_FRAME_HEADER) {
    // The first 8 bytes of every frame is header information that
    // we are going to want to strip before passing to http. That is
    // true of both control and data packets.
    
    NS_ABORT_IF_FALSE(mInputFrameBufferUsed < 8,
                      "Frame Buffer Used Too Large for State");

    rv = NetworkRead(writer, mInputFrameBuffer + mInputFrameBufferUsed,
                     8 - mInputFrameBufferUsed, countWritten);

    if (NS_FAILED(rv)) {
      LOG3(("SpdySession3 %p buffering frame header read failure %x\n",
            this, rv));
      // maybe just blocked reading from network
      if (rv == NS_BASE_STREAM_WOULD_BLOCK)
        ResumeRecv();
      return rv;
    }

    LogIO(this, nsnull, "Reading Frame Header",
          mInputFrameBuffer + mInputFrameBufferUsed, *countWritten);

    mInputFrameBufferUsed += *countWritten;

    if (mInputFrameBufferUsed < 8)
    {
      LOG3(("SpdySession3::WriteSegments %p "
            "BUFFERING FRAME HEADER incomplete size=%d",
            this, mInputFrameBufferUsed));
      return rv;
    }

    // For both control and data frames the second 32 bit word of the header
    // is 8-flags, 24-length. (network byte order)
    mInputFrameDataSize =
      PR_ntohl(reinterpret_cast<PRUint32 *>(mInputFrameBuffer.get())[1]);
    mInputFrameDataSize &= 0x00ffffff;
    mInputFrameDataRead = 0;
    
    if (mInputFrameBuffer[0] & kFlag_Control) {
      EnsureBuffer(mInputFrameBuffer, mInputFrameDataSize + 8, 8,
                   mInputFrameBufferSize);
      ChangeDownstreamState(BUFFERING_CONTROL_FRAME);
      
      // The first 32 bit word of the header is
      // 1 ctrl - 15 version - 16 type
      PRUint16 version =
        PR_ntohs(reinterpret_cast<PRUint16 *>(mInputFrameBuffer.get())[0]);
      version &= 0x7fff;
      
      mFrameControlType =
        PR_ntohs(reinterpret_cast<PRUint16 *>(mInputFrameBuffer.get())[1]);
      
      LOG3(("SpdySession3::WriteSegments %p - Control Frame Identified "
            "type %d version %d data len %d",
            this, mFrameControlType, version, mInputFrameDataSize));

      if (mFrameControlType >= CONTROL_TYPE_LAST ||
          mFrameControlType <= CONTROL_TYPE_FIRST)
        return NS_ERROR_ILLEGAL_VALUE;

      if (version != kVersion)
        return NS_ERROR_ILLEGAL_VALUE;
    }
    else {
      ChangeDownstreamState(PROCESSING_DATA_FRAME);

      Telemetry::Accumulate(Telemetry::SPDY_CHUNK_RECVD,
                            mInputFrameDataSize >> 10);
      mLastDataReadEpoch = mLastReadEpoch;

      PRUint32 streamID =
        PR_ntohl(reinterpret_cast<PRUint32 *>(mInputFrameBuffer.get())[0]);
      rv = SetInputFrameDataStream(streamID);
      if (NS_FAILED(rv)) {
        LOG(("SpdySession3::WriteSegments %p lookup streamID 0x%X failed. "
              "probably due to verification.\n", this, streamID));
        return rv;
      }
      if (!mInputFrameDataStream) {
        LOG3(("SpdySession3::WriteSegments %p lookup streamID 0x%X failed. "
              "Next = 0x%X", this, streamID, mNextStreamID));
        if (streamID >= mNextStreamID)
          GenerateRstStream(RST_INVALID_STREAM, streamID);
        ChangeDownstreamState(DISCARDING_DATA_FRAME);
      }
      else if (mInputFrameDataStream->RecvdFin()) {
        LOG3(("SpdySession3::WriteSegments %p streamID 0x%X "
              "Data arrived for already server closed stream.\n",
              this, streamID));
        GenerateRstStream(RST_STREAM_ALREADY_CLOSED, streamID);
        ChangeDownstreamState(DISCARDING_DATA_FRAME);
      }
      else if (!mInputFrameDataStream->RecvdData()) {
        LOG3(("SpdySession3 %p First Data Frame Flushes Headers stream 0x%X\n",
              this, streamID));

        mInputFrameDataStream->SetRecvdData(true);
        rv = ResponseHeadersComplete();
        if (rv == NS_ERROR_ILLEGAL_VALUE) {
          LOG3(("SpdySession3 %p PROTOCOL_ERROR detected 0x%X\n",
                this, streamID));
          CleanupStream(mInputFrameDataStream, rv, RST_PROTOCOL_ERROR);
          ChangeDownstreamState(DISCARDING_DATA_FRAME);
        }
        else {
          mDataPending = true;
        }
      }

      mInputFrameDataLast = (mInputFrameBuffer[4] & kFlag_Data_FIN);
      LOG3(("Start Processing Data Frame. "
            "Session=%p Stream ID 0x%X Stream Ptr %p Fin=%d Len=%d",
            this, streamID, mInputFrameDataStream, mInputFrameDataLast,
            mInputFrameDataSize));
      UpdateLocalRwin(mInputFrameDataStream, mInputFrameDataSize);
    }
  }

  if (mDownstreamState == PROCESSING_CONTROL_RST_STREAM) {
    if (mDownstreamRstReason == RST_REFUSED_STREAM)
      rv = NS_ERROR_NET_RESET;            //we can retry this 100% safely
    else if (mDownstreamRstReason == RST_CANCEL ||
             mDownstreamRstReason == RST_PROTOCOL_ERROR ||
             mDownstreamRstReason == RST_INTERNAL_ERROR ||
             mDownstreamRstReason == RST_UNSUPPORTED_VERSION)
      rv = NS_ERROR_NET_INTERRUPT;
    else if (mDownstreamRstReason == RST_FRAME_TOO_LARGE)
      rv = NS_ERROR_FILE_TOO_BIG;
    else
      rv = NS_ERROR_ILLEGAL_VALUE;

    if (mDownstreamRstReason != RST_REFUSED_STREAM &&
        mDownstreamRstReason != RST_CANCEL)
      mShouldGoAway = true;

    // mInputFrameDataStream is reset by ChangeDownstreamState
    SpdyStream3 *stream = mInputFrameDataStream;
    ResetDownstreamState();
    LOG3(("SpdySession3::WriteSegments cleanup stream on recv of rst "
          "session=%p stream=%p 0x%X\n", this, stream,
          stream ? stream->StreamID() : 0));
    CleanupStream(stream, rv, RST_CANCEL);
    return NS_OK;
  }

  if (mDownstreamState == PROCESSING_DATA_FRAME ||
      mDownstreamState == PROCESSING_COMPLETE_HEADERS) {

    // The cleanup stream should only be set while stream->WriteSegments is
    // on the stack and then cleaned up in this code block afterwards.
    NS_ABORT_IF_FALSE(!mNeedsCleanup, "cleanup stream set unexpectedly");
    mNeedsCleanup = nsnull;                     /* just in case */

    mSegmentWriter = writer;
    rv = mInputFrameDataStream->WriteSegments(this, count, countWritten);
    mSegmentWriter = nsnull;

    mLastDataReadEpoch = mLastReadEpoch;

    if (rv == NS_BASE_STREAM_CLOSED) {
      // This will happen when the transaction figures out it is EOF, generally
      // due to a content-length match being made
      SpdyStream3 *stream = mInputFrameDataStream;

      // if we were doing PROCESSING_COMPLETE_HEADERS need to pop the state
      // back to PROCESSING_DATA_FRAME where we came from
      mDownstreamState = PROCESSING_DATA_FRAME;

      if (mInputFrameDataRead == mInputFrameDataSize)
        ResetDownstreamState();
      LOG3(("SpdySession3::WriteSegments session=%p stream=%p 0x%X "
            "needscleanup=%p. cleanup stream based on "
            "stream->writeSegments returning BASE_STREAM_CLOSED\n",
            this, stream, stream ? stream->StreamID() : 0,
            mNeedsCleanup));
      CleanupStream(stream, NS_OK, RST_CANCEL);
      NS_ABORT_IF_FALSE(!mNeedsCleanup, "double cleanup out of data frame");
      mNeedsCleanup = nsnull;                     /* just in case */
      return NS_OK;
    }
    
    if (mNeedsCleanup) {
      LOG3(("SpdySession3::WriteSegments session=%p stream=%p 0x%X "
            "cleanup stream based on mNeedsCleanup.\n",
            this, mNeedsCleanup, mNeedsCleanup ? mNeedsCleanup->StreamID() : 0));
      CleanupStream(mNeedsCleanup, NS_OK, RST_CANCEL);
      mNeedsCleanup = nsnull;
    }

    return rv;
  }

  if (mDownstreamState == DISCARDING_DATA_FRAME) {
    char trash[4096];
    PRUint32 count = NS_MIN(4096U, mInputFrameDataSize - mInputFrameDataRead);

    if (!count) {
      ResetDownstreamState();
      ResumeRecv();
      return NS_BASE_STREAM_WOULD_BLOCK;
    }

    rv = NetworkRead(writer, trash, count, countWritten);

    if (NS_FAILED(rv)) {
      LOG3(("SpdySession3 %p discard frame read failure %x\n", this, rv));
      // maybe just blocked reading from network
      if (rv == NS_BASE_STREAM_WOULD_BLOCK)
        ResumeRecv();
      return rv;
    }

    LogIO(this, nsnull, "Discarding Frame", trash, *countWritten);

    mInputFrameDataRead += *countWritten;

    if (mInputFrameDataRead == mInputFrameDataSize)
      ResetDownstreamState();
    return rv;
  }
  
  if (mDownstreamState != BUFFERING_CONTROL_FRAME) {
    // this cannot happen
    NS_ABORT_IF_FALSE(false, "Not in Bufering Control Frame State");
    return NS_ERROR_UNEXPECTED;
  }

  NS_ABORT_IF_FALSE(mInputFrameBufferUsed == 8,
                    "Frame Buffer Header Not Present");

  rv = NetworkRead(writer, mInputFrameBuffer + 8 + mInputFrameDataRead,
                   mInputFrameDataSize - mInputFrameDataRead, countWritten);

  if (NS_FAILED(rv)) {
    LOG3(("SpdySession3 %p buffering control frame read failure %x\n",
          this, rv));
    // maybe just blocked reading from network
    if (rv == NS_BASE_STREAM_WOULD_BLOCK)
      ResumeRecv();
    return rv;
  }

  LogIO(this, nsnull, "Reading Control Frame",
        mInputFrameBuffer + 8 + mInputFrameDataRead, *countWritten);

  mInputFrameDataRead += *countWritten;

  if (mInputFrameDataRead != mInputFrameDataSize)
    return NS_OK;

  // This check is actually redundant, the control type was previously
  // checked to make sure it was in range, but we will check it again
  // at time of use to make sure a regression doesn't creep in.
  if (mFrameControlType >= CONTROL_TYPE_LAST ||
      mFrameControlType <= CONTROL_TYPE_FIRST) 
  {
    NS_ABORT_IF_FALSE(false, "control type out of range");
    return NS_ERROR_ILLEGAL_VALUE;
  }
  rv = sControlFunctions[mFrameControlType](this);

  NS_ABORT_IF_FALSE(NS_FAILED(rv) ||
                    mDownstreamState != BUFFERING_CONTROL_FRAME,
                    "Control Handler returned OK but did not change state");

  if (mShouldGoAway && !mStreamTransactionHash.Count())
    Close(NS_OK);
  return rv;
}

void
SpdySession3::UpdateLocalRwin(SpdyStream3 *stream,
                              PRUint32 bytes)
{
  // If this data packet was not for a valid or live stream then there
  // is no reason to mess with the flow control
  if (!stream || stream->RecvdFin())
    return;

  LOG3(("SpdySession3::UpdateLocalRwin %p 0x%X %d\n",
        this, stream->StreamID(), bytes));
  stream->DecrementLocalWindow(bytes);

  // Don't necessarily ack every data packet. Only do it
  // after a significant amount of data.
  PRUint64 unacked = stream->LocalUnAcked();

  if (unacked < kMinimumToAck) {
    // Sanity check to make sure this won't let the window drop below 1MB
    PR_STATIC_ASSERT(kMinimumToAck < kInitialRwin);
    PR_STATIC_ASSERT((kInitialRwin - kMinimumToAck) > 1024 * 1024);

    return;
  }

  // Generate window updates directly out of spdysession instead of the stream
  // in order to avoid queue delays in getting the ACK out.
  PRUint32 toack = unacked & 0x7fffffff;
  
  LOG3(("SpdySession3::UpdateLocalRwin Ack %p 0x%X %d\n",
        this, stream->StreamID(), toack));
  stream->IncrementLocalWindow(toack);
    
  static const PRUint32 dataLen = 8;
  EnsureBuffer(mOutputQueueBuffer, mOutputQueueUsed + 8 + dataLen,
               mOutputQueueUsed, mOutputQueueSize);
  char *packet = mOutputQueueBuffer.get() + mOutputQueueUsed;
  mOutputQueueUsed += 8 + dataLen;

  memset(packet, 0, 8 + dataLen);
  packet[0] = kFlag_Control;
  packet[1] = kVersion;
  packet[3] = CONTROL_TYPE_WINDOW_UPDATE;
  packet[7] = dataLen;
  
  PRUint32 id = PR_htonl(stream->StreamID());
  memcpy(packet + 8, &id, 4);
  toack = PR_htonl(toack);
  memcpy(packet + 12, &toack, 4);

  LogIO(this, stream, "Window Update", packet, 8 + dataLen);
  FlushOutputQueue();
}

void
SpdySession3::Close(nsresult aReason)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");

  if (mClosed)
    return;

  LOG3(("SpdySession3::Close %p %X", this, aReason));

  mClosed = true;

  NS_ABORT_IF_FALSE(mStreamTransactionHash.Count() ==
                    mStreamIDHash.Count(),
                    "index corruption");
  mStreamTransactionHash.Enumerate(ShutdownEnumerator, this);
  mStreamIDHash.Clear();
  mStreamTransactionHash.Clear();

  if (NS_SUCCEEDED(aReason))
    GenerateGoAway();
  mConnection = nsnull;
  mSegmentReader = nsnull;
  mSegmentWriter = nsnull;
}

void
SpdySession3::CloseTransaction(nsAHttpTransaction *aTransaction,
                              nsresult aResult)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession3::CloseTransaction %p %p %x", this, aTransaction, aResult));

  // Generally this arrives as a cancel event from the connection manager.

  // need to find the stream and call CleanupStream() on it.
  SpdyStream3 *stream = mStreamTransactionHash.Get(aTransaction);
  if (!stream) {
    LOG3(("SpdySession3::CloseTransaction %p %p %x - not found.",
          this, aTransaction, aResult));
    return;
  }
  LOG3(("SpdySession3::CloseTranscation probably a cancel. "
        "this=%p, trans=%p, result=%x, streamID=0x%X stream=%p",
        this, aTransaction, aResult, stream->StreamID(), stream));
  CleanupStream(stream, aResult, RST_CANCEL);
  ResumeRecv();
}


//-----------------------------------------------------------------------------
// nsAHttpSegmentReader
//-----------------------------------------------------------------------------

nsresult
SpdySession3::OnReadSegment(const char *buf,
                           PRUint32 count,
                           PRUint32 *countRead)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  
  nsresult rv;
  
  // If we can release old queued data then we can try and write the new
  // data directly to the network without using the output queue at all
  if (mOutputQueueUsed)
    FlushOutputQueue();

  if (!mOutputQueueUsed && mSegmentReader) {
    // try and write directly without output queue
    rv = mSegmentReader->OnReadSegment(buf, count, countRead);

    if (rv == NS_BASE_STREAM_WOULD_BLOCK)
      *countRead = 0;
    else if (NS_FAILED(rv))
      return rv;
    
    if (*countRead < count) {
      PRUint32 required = count - *countRead;
      // assuming a commitment() happened, this ensurebuffer is a nop
      // but just in case the queuesize is too small for the required data
      // call ensurebuffer().
      EnsureBuffer(mOutputQueueBuffer, required, 0, mOutputQueueSize);
      memcpy(mOutputQueueBuffer.get(), buf + *countRead, required);
      mOutputQueueUsed = required;
    }
    
    *countRead = count;
    return NS_OK;
  }

  // At this point we are going to buffer the new data in the output
  // queue if it fits. By coalescing multiple small submissions into one larger
  // buffer we can get larger writes out to the network later on.

  // This routine should not be allowed to fill up the output queue
  // all on its own - at least kQueueReserved bytes are always left
  // for other routines to use - but this is an all-or-nothing function,
  // so if it will not all fit just return WOULD_BLOCK

  if ((mOutputQueueUsed + count) > (mOutputQueueSize - kQueueReserved))
    return NS_BASE_STREAM_WOULD_BLOCK;
  
  memcpy(mOutputQueueBuffer.get() + mOutputQueueUsed, buf, count);
  mOutputQueueUsed += count;
  *countRead = count;

  FlushOutputQueue();

  return NS_OK;
}

nsresult
SpdySession3::CommitToSegmentSize(PRUint32 count)
{
  if (mOutputQueueUsed)
    FlushOutputQueue();

  // would there be enough room to buffer this if needed?
  if ((mOutputQueueUsed + count) <= (mOutputQueueSize - kQueueReserved))
    return NS_OK;
  
  // if we are using part of our buffers already, try again later
  if (mOutputQueueUsed)
    return NS_BASE_STREAM_WOULD_BLOCK;

  // not enough room to buffer even with completely empty buffers.
  // normal frames are max 4kb, so the only case this can really happen
  // is a SYN_STREAM with technically unbounded headers. That is highly
  // unlikely, but possible. Create enough room for it because the buffers
  // will be necessary - SSL does not absorb writes of very large sizes
  // in single sends.

  EnsureBuffer(mOutputQueueBuffer, count + kQueueReserved, 0, mOutputQueueSize);

  NS_ABORT_IF_FALSE((mOutputQueueUsed + count) <=
                    (mOutputQueueSize - kQueueReserved),
                    "buffer not as large as expected");

  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsAHttpSegmentWriter
//-----------------------------------------------------------------------------

nsresult
SpdySession3::OnWriteSegment(char *buf,
                            PRUint32 count,
                            PRUint32 *countWritten)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  nsresult rv;

  if (!mSegmentWriter) {
    // the only way this could happen would be if Close() were called on the
    // stack with WriteSegments()
    return NS_ERROR_FAILURE;
  }
  
  if (mDownstreamState == PROCESSING_DATA_FRAME) {

    if (mInputFrameDataLast &&
        mInputFrameDataRead == mInputFrameDataSize) {
      *countWritten = 0;
      SetNeedsCleanup();
      return NS_BASE_STREAM_CLOSED;
    }
    
    count = NS_MIN(count, mInputFrameDataSize - mInputFrameDataRead);
    rv = NetworkRead(mSegmentWriter, buf, count, countWritten);
    if (NS_FAILED(rv))
      return rv;

    LogIO(this, mInputFrameDataStream, "Reading Data Frame",
          buf, *countWritten);

    mInputFrameDataRead += *countWritten;
    
    mInputFrameDataStream->UpdateTransportReadEvents(*countWritten);
    if ((mInputFrameDataRead == mInputFrameDataSize) && !mInputFrameDataLast)
      ResetDownstreamState();

    return rv;
  }
  
  if (mDownstreamState == PROCESSING_COMPLETE_HEADERS) {
    
    if (mFlatHTTPResponseHeaders.Length() == mFlatHTTPResponseHeadersOut &&
        mInputFrameDataLast) {
      *countWritten = 0;
      SetNeedsCleanup();
      return NS_BASE_STREAM_CLOSED;
    }
      
    count = NS_MIN(count,
                   mFlatHTTPResponseHeaders.Length() -
                   mFlatHTTPResponseHeadersOut);
    memcpy(buf,
           mFlatHTTPResponseHeaders.get() + mFlatHTTPResponseHeadersOut,
           count);
    mFlatHTTPResponseHeadersOut += count;
    *countWritten = count;

    if (mFlatHTTPResponseHeaders.Length() == mFlatHTTPResponseHeadersOut) {
      if (mDataPending) {
        // Now ready to process data frames - pop PROCESING_DATA_FRAME back onto
        // the stack because receipt of that first data frame triggered the
        // response header processing
        mDataPending = false;
        ChangeDownstreamState(PROCESSING_DATA_FRAME);
      }
      else if (!mInputFrameDataLast) {
        // If more frames are expected in this stream, then reset the state so they can be
        // handled. Otherwise (e.g. a 0 length response with the fin on the SYN_REPLY)
        // stay in PROCESSING_COMPLETE_HEADERS state so the SetNeedsCleanup() code above can
        // cleanup the stream.
        ResetDownstreamState();
      }
    }
    
    return NS_OK;
  }

  return NS_ERROR_UNEXPECTED;
}

void
SpdySession3::SetNeedsCleanup()
{
  LOG3(("SpdySession3::SetNeedsCleanup %p - recorded downstream fin of "
        "stream %p 0x%X", this, mInputFrameDataStream,
        mInputFrameDataStream->StreamID()));

  // This will result in Close() being called
  NS_ABORT_IF_FALSE(!mNeedsCleanup, "mNeedsCleanup unexpectedly set");
  mNeedsCleanup = mInputFrameDataStream;
  ResetDownstreamState();
}

//-----------------------------------------------------------------------------
// Modified methods of nsAHttpConnection
//-----------------------------------------------------------------------------

void
SpdySession3::TransactionHasDataToWrite(nsAHttpTransaction *caller)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession3::TransactionHasDataToWrite %p trans=%p", this, caller));

  // a trapped signal from the http transaction to the connection that
  // it is no longer blocked on read.

  SpdyStream3 *stream = mStreamTransactionHash.Get(caller);
  if (!stream || !VerifyStream(stream)) {
    LOG3(("SpdySession3::TransactionHasDataToWrite %p caller %p not found",
          this, caller));
    return;
  }
  
  LOG3(("SpdySession3::TransactionHasDataToWrite %p ID is 0x%X\n",
        this, stream->StreamID()));

  mReadyForWrite.Push(stream);
}

void
SpdySession3::TransactionHasDataToWrite(SpdyStream3 *stream)
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  LOG3(("SpdySession3::TransactionHasDataToWrite %p stream=%p ID=%x",
        this, stream, stream->StreamID()));

  mReadyForWrite.Push(stream);
  SetWriteCallbacks();
}

bool
SpdySession3::IsPersistent()
{
  return true;
}

nsresult
SpdySession3::TakeTransport(nsISocketTransport **,
                           nsIAsyncInputStream **,
                           nsIAsyncOutputStream **)
{
  NS_ABORT_IF_FALSE(false, "TakeTransport of SpdySession3");
  return NS_ERROR_UNEXPECTED;
}

nsHttpConnection *
SpdySession3::TakeHttpConnection()
{
  NS_ABORT_IF_FALSE(false, "TakeHttpConnection of SpdySession3");
  return nsnull;
}

PRUint32
SpdySession3::CancelPipeline(nsresult reason)
{
  // we don't pipeline inside spdy, so this isn't an issue
  return 0;
}

nsAHttpTransaction::Classifier
SpdySession3::Classification()
{
  if (!mConnection)
    return nsAHttpTransaction::CLASS_GENERAL;
  return mConnection->Classification();
}

//-----------------------------------------------------------------------------
// unused methods of nsAHttpTransaction
// We can be sure of this because SpdySession3 is only constructed in
// nsHttpConnection and is never passed out of that object
//-----------------------------------------------------------------------------

void
SpdySession3::SetConnection(nsAHttpConnection *)
{
  // This is unexpected
  NS_ABORT_IF_FALSE(false, "SpdySession3::SetConnection()");
}

void
SpdySession3::GetSecurityCallbacks(nsIInterfaceRequestor **,
                                  nsIEventTarget **)
{
  // This is unexpected
  NS_ABORT_IF_FALSE(false, "SpdySession3::GetSecurityCallbacks()");
}

void
SpdySession3::SetSSLConnectFailed()
{
  NS_ABORT_IF_FALSE(false, "SpdySession3::SetSSLConnectFailed()");
}

bool
SpdySession3::IsDone()
{
  return !mStreamTransactionHash.Count();
}

nsresult
SpdySession3::Status()
{
  NS_ABORT_IF_FALSE(false, "SpdySession3::Status()");
  return NS_ERROR_UNEXPECTED;
}

PRUint8
SpdySession3::Caps()
{
  NS_ABORT_IF_FALSE(false, "SpdySession3::Caps()");
  return 0;
}

PRUint32
SpdySession3::Available()
{
  NS_ABORT_IF_FALSE(false, "SpdySession3::Available()");
  return 0;
}

nsHttpRequestHead *
SpdySession3::RequestHead()
{
  NS_ABORT_IF_FALSE(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  NS_ABORT_IF_FALSE(false,
                    "SpdySession3::RequestHead() "
                    "should not be called after SPDY is setup");
  return NULL;
}

PRUint32
SpdySession3::Http1xTransactionCount()
{
  return 0;
}

// used as an enumerator by TakeSubTransactions()
static PLDHashOperator
TakeStream(nsAHttpTransaction *key,
           nsAutoPtr<SpdyStream3> &stream,
           void *closure)
{
  nsTArray<nsRefPtr<nsAHttpTransaction> > *list =
    static_cast<nsTArray<nsRefPtr<nsAHttpTransaction> > *>(closure);

  list->AppendElement(key);

  // removing the stream from the hash will delete the stream
  // and drop the transaction reference the hash held
  return PL_DHASH_REMOVE;
}

nsresult
SpdySession3::TakeSubTransactions(
    nsTArray<nsRefPtr<nsAHttpTransaction> > &outTransactions)
{
  // Generally this cannot be done with spdy as transactions are
  // started right away.

  LOG3(("SpdySession3::TakeSubTransactions %p\n", this));

  if (mConcurrentHighWater > 0)
    return NS_ERROR_ALREADY_OPENED;

  LOG3(("   taking %d\n", mStreamTransactionHash.Count()));

  mStreamTransactionHash.Enumerate(TakeStream, &outTransactions);
  return NS_OK;
}

nsresult
SpdySession3::AddTransaction(nsAHttpTransaction *)
{
  // This API is meant for pipelining, SpdySession3's should be
  // extended with AddStream()

  NS_ABORT_IF_FALSE(false,
                    "SpdySession3::AddTransaction() should not be called");

  return NS_ERROR_NOT_IMPLEMENTED;
}

PRUint32
SpdySession3::PipelineDepth()
{
  return IsDone() ? 0 : 1;
}

nsresult
SpdySession3::SetPipelinePosition(PRInt32 position)
{
  // This API is meant for pipelining, SpdySession3's should be
  // extended with AddStream()

  NS_ABORT_IF_FALSE(false,
                    "SpdySession3::SetPipelinePosition() should not be called");

  return NS_ERROR_NOT_IMPLEMENTED;
}

PRInt32
SpdySession3::PipelinePosition()
{
    return 0;
}

//-----------------------------------------------------------------------------
// Pass through methods of nsAHttpConnection
//-----------------------------------------------------------------------------

nsAHttpConnection *
SpdySession3::Connection()
{
  NS_ASSERTION(PR_GetCurrentThread() == gSocketThread, "wrong thread");
  return mConnection;
}

nsresult
SpdySession3::OnHeadersAvailable(nsAHttpTransaction *transaction,
                                nsHttpRequestHead *requestHead,
                                nsHttpResponseHead *responseHead,
                                bool *reset)
{
  return mConnection->OnHeadersAvailable(transaction,
                                         requestHead,
                                         responseHead,
                                         reset);
}

bool
SpdySession3::IsReused()
{
  return mConnection->IsReused();
}

nsresult
SpdySession3::PushBack(const char *buf, PRUint32 len)
{
  return mConnection->PushBack(buf, len);
}

} // namespace mozilla::net
} // namespace mozilla
