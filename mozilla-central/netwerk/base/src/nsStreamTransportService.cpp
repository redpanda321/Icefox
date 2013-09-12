/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsStreamTransportService.h"
#include "nsXPCOMCIDInternal.h"
#include "nsNetSegmentUtils.h"
#include "nsTransportUtils.h"
#include "nsStreamUtils.h"
#include "nsError.h"
#include "nsNetCID.h"

#include "nsIServiceManager.h"
#include "nsIAsyncInputStream.h"
#include "nsIAsyncOutputStream.h"
#include "nsISeekableStream.h"
#include "nsIPipe.h"
#include "nsITransport.h"
#include "nsIRunnable.h"
#include "nsIObserverService.h"
#include "mozilla/Services.h"

//-----------------------------------------------------------------------------
// nsInputStreamTransport
//
// Implements nsIInputStream as a wrapper around the real input stream.  This
// allows the transport to support seeking, range-limiting, progress reporting,
// and close-when-done semantics while utilizing NS_AsyncCopy.
//-----------------------------------------------------------------------------

class nsInputStreamTransport : public nsITransport
                             , public nsIInputStream
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSITRANSPORT
    NS_DECL_NSIINPUTSTREAM

    nsInputStreamTransport(nsIInputStream *source,
                           uint64_t offset,
                           uint64_t limit,
                           bool closeWhenDone)
        : mSource(source)
        , mOffset(offset)
        , mLimit(limit)
        , mCloseWhenDone(closeWhenDone)
        , mFirstTime(true)
        , mInProgress(false)
    {
    }

    virtual ~nsInputStreamTransport()
    {
    }

private:
    nsCOMPtr<nsIAsyncInputStream>   mPipeIn;

    // while the copy is active, these members may only be accessed from the
    // nsIInputStream implementation.
    nsCOMPtr<nsITransportEventSink> mEventSink;
    nsCOMPtr<nsIInputStream>        mSource;
    uint64_t                        mOffset;
    uint64_t                        mLimit;
    bool                            mCloseWhenDone;
    bool                            mFirstTime;

    // this variable serves as a lock to prevent the state of the transport
    // from being modified once the copy is in progress.
    bool                            mInProgress;
};

NS_IMPL_THREADSAFE_ISUPPORTS2(nsInputStreamTransport,
                              nsITransport,
                              nsIInputStream)

/** nsITransport **/

NS_IMETHODIMP
nsInputStreamTransport::OpenInputStream(uint32_t flags,
                                        uint32_t segsize,
                                        uint32_t segcount,
                                        nsIInputStream **result)
{
    NS_ENSURE_TRUE(!mInProgress, NS_ERROR_IN_PROGRESS);

    nsresult rv;
    nsCOMPtr<nsIEventTarget> target =
            do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID, &rv);
    if (NS_FAILED(rv)) return rv;

    // XXX if the caller requests an unbuffered stream, then perhaps
    //     we'd want to simply return mSource; however, then we would
    //     not be reading mSource on a background thread.  is this ok?
 
    bool nonblocking = !(flags & OPEN_BLOCKING);

    net_ResolveSegmentParams(segsize, segcount);

    nsCOMPtr<nsIAsyncOutputStream> pipeOut;
    rv = NS_NewPipe2(getter_AddRefs(mPipeIn),
                     getter_AddRefs(pipeOut),
                     nonblocking, true,
                     segsize, segcount);
    if (NS_FAILED(rv)) return rv;

    mInProgress = true;

    // startup async copy process...
    rv = NS_AsyncCopy(this, pipeOut, target,
                      NS_ASYNCCOPY_VIA_WRITESEGMENTS, segsize);
    if (NS_SUCCEEDED(rv))
        NS_ADDREF(*result = mPipeIn);

    return rv;
}

NS_IMETHODIMP
nsInputStreamTransport::OpenOutputStream(uint32_t flags,
                                         uint32_t segsize,
                                         uint32_t segcount,
                                         nsIOutputStream **result)
{
    // this transport only supports reading!
    NS_NOTREACHED("nsInputStreamTransport::OpenOutputStream");
    return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsInputStreamTransport::Close(nsresult reason)
{
    if (NS_SUCCEEDED(reason))
        reason = NS_BASE_STREAM_CLOSED;

    return mPipeIn->CloseWithStatus(reason);
}

NS_IMETHODIMP
nsInputStreamTransport::SetEventSink(nsITransportEventSink *sink,
                                     nsIEventTarget *target)
{
    NS_ENSURE_TRUE(!mInProgress, NS_ERROR_IN_PROGRESS);

    if (target)
        return net_NewTransportEventSinkProxy(getter_AddRefs(mEventSink),
                                              sink, target);

    mEventSink = sink;
    return NS_OK;
}

/** nsIInputStream **/

NS_IMETHODIMP
nsInputStreamTransport::Close()
{
    if (mCloseWhenDone)
        mSource->Close();

    // make additional reads return early...
    mOffset = mLimit = 0;
    return NS_OK;
}

NS_IMETHODIMP
nsInputStreamTransport::Available(uint64_t *result)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsInputStreamTransport::Read(char *buf, uint32_t count, uint32_t *result)
{
    if (mFirstTime) {
        mFirstTime = false;
        if (mOffset != 0) {
            // read from current position if offset equal to max
            if (mOffset != UINT64_MAX) {
                nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mSource);
                if (seekable)
                    seekable->Seek(nsISeekableStream::NS_SEEK_SET, mOffset);
            }
            // reset offset to zero so we can use it to enforce limit
            mOffset = 0;
        }
    }

    // limit amount read
    uint32_t max = mLimit - mOffset;
    if (max == 0) {
        *result = 0;
        return NS_OK;
    }
        
    if (count > max)
        count = max;

    nsresult rv = mSource->Read(buf, count, result);

    if (NS_SUCCEEDED(rv)) {
        mOffset += *result;
        if (mEventSink)
            mEventSink->OnTransportStatus(this, NS_NET_STATUS_READING, mOffset,
                                          mLimit);
    }
    return rv;
}

NS_IMETHODIMP
nsInputStreamTransport::ReadSegments(nsWriteSegmentFun writer, void *closure,
                                     uint32_t count, uint32_t *result)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsInputStreamTransport::IsNonBlocking(bool *result)
{
    *result = false;
    return NS_OK;
}

//-----------------------------------------------------------------------------
// nsOutputStreamTransport
//
// Implements nsIOutputStream as a wrapper around the real input stream.  This
// allows the transport to support seeking, range-limiting, progress reporting,
// and close-when-done semantics while utilizing NS_AsyncCopy.
//-----------------------------------------------------------------------------

class nsOutputStreamTransport : public nsITransport
                              , public nsIOutputStream
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSITRANSPORT
    NS_DECL_NSIOUTPUTSTREAM

    nsOutputStreamTransport(nsIOutputStream *sink,
                            uint64_t offset,
                            uint64_t limit,
                            bool closeWhenDone)
        : mSink(sink)
        , mOffset(offset)
        , mLimit(limit)
        , mCloseWhenDone(closeWhenDone)
        , mFirstTime(true)
        , mInProgress(false)
    {
    }

    virtual ~nsOutputStreamTransport()
    {
    }

private:
    nsCOMPtr<nsIAsyncOutputStream>  mPipeOut;
 
    // while the copy is active, these members may only be accessed from the
    // nsIOutputStream implementation.
    nsCOMPtr<nsITransportEventSink> mEventSink;
    nsCOMPtr<nsIOutputStream>       mSink;
    uint64_t                        mOffset;
    uint64_t                        mLimit;
    bool                            mCloseWhenDone;
    bool                            mFirstTime;

    // this variable serves as a lock to prevent the state of the transport
    // from being modified once the copy is in progress.
    bool                            mInProgress;
};

NS_IMPL_THREADSAFE_ISUPPORTS2(nsOutputStreamTransport,
                              nsITransport,
                              nsIOutputStream)

/** nsITransport **/

NS_IMETHODIMP
nsOutputStreamTransport::OpenInputStream(uint32_t flags,
                                         uint32_t segsize,
                                         uint32_t segcount,
                                         nsIInputStream **result)
{
    // this transport only supports writing!
    NS_NOTREACHED("nsOutputStreamTransport::OpenInputStream");
    return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsOutputStreamTransport::OpenOutputStream(uint32_t flags,
                                          uint32_t segsize,
                                          uint32_t segcount,
                                          nsIOutputStream **result)
{
    NS_ENSURE_TRUE(!mInProgress, NS_ERROR_IN_PROGRESS);

    nsresult rv;
    nsCOMPtr<nsIEventTarget> target =
            do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID, &rv);
    if (NS_FAILED(rv)) return rv;

    // XXX if the caller requests an unbuffered stream, then perhaps
    //     we'd want to simply return mSink; however, then we would
    //     not be writing to mSink on a background thread.  is this ok?
 
    bool nonblocking = !(flags & OPEN_BLOCKING);

    net_ResolveSegmentParams(segsize, segcount);

    nsCOMPtr<nsIAsyncInputStream> pipeIn;
    rv = NS_NewPipe2(getter_AddRefs(pipeIn),
                     getter_AddRefs(mPipeOut),
                     true, nonblocking,
                     segsize, segcount);
    if (NS_FAILED(rv)) return rv;

    mInProgress = true;

    // startup async copy process...
    rv = NS_AsyncCopy(pipeIn, this, target,
                      NS_ASYNCCOPY_VIA_READSEGMENTS, segsize);
    if (NS_SUCCEEDED(rv))
        NS_ADDREF(*result = mPipeOut);

    return rv;
}

NS_IMETHODIMP
nsOutputStreamTransport::Close(nsresult reason)
{
    if (NS_SUCCEEDED(reason))
        reason = NS_BASE_STREAM_CLOSED;

    return mPipeOut->CloseWithStatus(reason);
}

NS_IMETHODIMP
nsOutputStreamTransport::SetEventSink(nsITransportEventSink *sink,
                                      nsIEventTarget *target)
{
    NS_ENSURE_TRUE(!mInProgress, NS_ERROR_IN_PROGRESS);

    if (target)
        return net_NewTransportEventSinkProxy(getter_AddRefs(mEventSink),
                                              sink, target);

    mEventSink = sink;
    return NS_OK;
}

/** nsIOutputStream **/

NS_IMETHODIMP
nsOutputStreamTransport::Close()
{
    if (mCloseWhenDone)
        mSink->Close();

    // make additional writes return early...
    mOffset = mLimit = 0;
    return NS_OK;
}

NS_IMETHODIMP
nsOutputStreamTransport::Flush()
{
    return NS_OK;
}

NS_IMETHODIMP
nsOutputStreamTransport::Write(const char *buf, uint32_t count, uint32_t *result)
{
    if (mFirstTime) {
        mFirstTime = false;
        if (mOffset != 0) {
            // write to current position if offset equal to max
            if (mOffset != UINT64_MAX) {
                nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mSink);
                if (seekable)
                    seekable->Seek(nsISeekableStream::NS_SEEK_SET, mOffset);
            }
            // reset offset to zero so we can use it to enforce limit
            mOffset = 0;
        }
    }

    // limit amount written
    uint32_t max = mLimit - mOffset;
    if (max == 0) {
        *result = 0;
        return NS_OK;
    }
        
    if (count > max)
        count = max;

    nsresult rv = mSink->Write(buf, count, result);

    if (NS_SUCCEEDED(rv)) {
        mOffset += *result;
        if (mEventSink)
            mEventSink->OnTransportStatus(this, NS_NET_STATUS_WRITING, mOffset,
                                          mLimit);
    }
    return rv;
}

NS_IMETHODIMP
nsOutputStreamTransport::WriteSegments(nsReadSegmentFun reader, void *closure,
                                       uint32_t count, uint32_t *result)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsOutputStreamTransport::WriteFrom(nsIInputStream *in, uint32_t count, uint32_t *result)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsOutputStreamTransport::IsNonBlocking(bool *result)
{
    *result = false;
    return NS_OK;
}

//-----------------------------------------------------------------------------
// nsStreamTransportService
//-----------------------------------------------------------------------------

nsStreamTransportService::~nsStreamTransportService()
{
    NS_ASSERTION(!mPool, "thread pool wasn't shutdown");
}

nsresult
nsStreamTransportService::Init()
{
    mPool = do_CreateInstance(NS_THREADPOOL_CONTRACTID);
    NS_ENSURE_STATE(mPool);

    // Configure the pool
    mPool->SetThreadLimit(4);
    mPool->SetIdleThreadLimit(1);
    mPool->SetIdleThreadTimeout(PR_SecondsToInterval(60));
    mPool->SetName(NS_LITERAL_CSTRING("StreamTrans"));

    nsCOMPtr<nsIObserverService> obsSvc =
        mozilla::services::GetObserverService();
    if (obsSvc)
        obsSvc->AddObserver(this, "xpcom-shutdown-threads", false);
    return NS_OK;
}

NS_IMPL_THREADSAFE_ISUPPORTS3(nsStreamTransportService,
                              nsIStreamTransportService,
                              nsIEventTarget,
                              nsIObserver)

NS_IMETHODIMP
nsStreamTransportService::Dispatch(nsIRunnable *task, uint32_t flags)
{
    NS_ENSURE_TRUE(mPool, NS_ERROR_NOT_INITIALIZED);
    return mPool->Dispatch(task, flags);
}

NS_IMETHODIMP
nsStreamTransportService::IsOnCurrentThread(bool *result)
{
    NS_ENSURE_TRUE(mPool, NS_ERROR_NOT_INITIALIZED);
    return mPool->IsOnCurrentThread(result);
}

NS_IMETHODIMP
nsStreamTransportService::CreateInputTransport(nsIInputStream *stream,
                                               int64_t offset,
                                               int64_t limit,
                                               bool closeWhenDone,
                                               nsITransport **result)
{
    nsInputStreamTransport *trans =
        new nsInputStreamTransport(stream, offset, limit, closeWhenDone);
    if (!trans)
        return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(*result = trans);
    return NS_OK;
}

NS_IMETHODIMP
nsStreamTransportService::CreateOutputTransport(nsIOutputStream *stream,
                                                int64_t offset,
                                                int64_t limit,
                                                bool closeWhenDone,
                                                nsITransport **result)
{
    nsOutputStreamTransport *trans =
        new nsOutputStreamTransport(stream, offset, limit, closeWhenDone);
    if (!trans)
        return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(*result = trans);
    return NS_OK;
}

NS_IMETHODIMP
nsStreamTransportService::RaiseThreadLimit()
{
    NS_ENSURE_TRUE(mPool, NS_ERROR_NOT_INITIALIZED);

    uint32_t threadLimit;
    nsresult rv = mPool->GetThreadLimit(&threadLimit);
    NS_ENSURE_SUCCESS(rv, rv);

    return mPool->SetThreadLimit(threadLimit + 1);
}

NS_IMETHODIMP
nsStreamTransportService::LowerThreadLimit()
{
    NS_ENSURE_TRUE(mPool, NS_ERROR_NOT_INITIALIZED);

    uint32_t threadLimit;
    nsresult rv = mPool->GetThreadLimit(&threadLimit);
    NS_ENSURE_SUCCESS(rv, rv);

    if (threadLimit == 4) {
      NS_WARNING("Badly nested raise/lower thread limit!");
      return NS_ERROR_UNEXPECTED;
    }

    return mPool->SetThreadLimit(threadLimit - 1);
}

NS_IMETHODIMP
nsStreamTransportService::Observe(nsISupports *subject, const char *topic,
                                  const PRUnichar *data)
{
  NS_ASSERTION(strcmp(topic, "xpcom-shutdown-threads") == 0, "oops");

  if (mPool) {
    mPool->Shutdown();
    mPool = nullptr;
  }
  return NS_OK;
}
