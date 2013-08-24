/* vim:set ts=4 sw=4 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZ_LOGGING
#define FORCE_PR_LOG
#endif

#include "nsSocketTransport2.h"
#include "nsAtomicRefcnt.h"
#include "nsIOService.h"
#include "nsStreamUtils.h"
#include "nsNetSegmentUtils.h"
#include "nsNetAddr.h"
#include "nsTransportUtils.h"
#include "nsProxyInfo.h"
#include "nsNetCID.h"
#include "nsAutoPtr.h"
#include "nsCOMPtr.h"
#include "netCore.h"
#include "prmem.h"
#include "plstr.h"
#include "prnetdb.h"
#include "prerror.h"
#include "prerr.h"

#include "nsIServiceManager.h"
#include "nsISocketProviderService.h"
#include "nsISocketProvider.h"
#include "nsISSLSocketControl.h"
#include "nsINSSErrorsService.h"
#include "nsIPipe.h"
#include "nsIProgrammingLanguage.h"
#include "nsIClassInfoImpl.h"

#if defined(XP_WIN) || defined(MOZ_PLATFORM_MAEMO)
#include "nsNativeConnectionHelper.h"
#endif

using namespace mozilla;

//-----------------------------------------------------------------------------

static NS_DEFINE_CID(kSocketProviderServiceCID, NS_SOCKETPROVIDERSERVICE_CID);
static NS_DEFINE_CID(kDNSServiceCID, NS_DNSSERVICE_CID);

//-----------------------------------------------------------------------------

class nsSocketEvent : public nsRunnable
{
public:
    nsSocketEvent(nsSocketTransport *transport, PRUint32 type,
                  nsresult status = NS_OK, nsISupports *param = nsnull)
        : mTransport(transport)
        , mType(type)
        , mStatus(status)
        , mParam(param)
    {}

    NS_IMETHOD Run()
    {
        mTransport->OnSocketEvent(mType, mStatus, mParam);
        return NS_OK;
    }

private:
    nsRefPtr<nsSocketTransport> mTransport;

    PRUint32              mType;
    nsresult              mStatus;
    nsCOMPtr<nsISupports> mParam;
};

//-----------------------------------------------------------------------------

//#define TEST_CONNECT_ERRORS
#ifdef TEST_CONNECT_ERRORS
#include <stdlib.h>
static PRErrorCode RandomizeConnectError(PRErrorCode code)
{
    //
    // To test out these errors, load http://www.yahoo.com/.  It should load
    // correctly despite the random occurrence of these errors.
    //
    int n = rand();
    if (n > RAND_MAX/2) {
        struct {
            PRErrorCode err_code;
            const char *err_name;
        } 
        errors[] = {
            //
            // These errors should be recoverable provided there is another
            // IP address in mDNSRecord.
            //
            { PR_CONNECT_REFUSED_ERROR, "PR_CONNECT_REFUSED_ERROR" },
            { PR_CONNECT_TIMEOUT_ERROR, "PR_CONNECT_TIMEOUT_ERROR" },
            //
            // This error will cause this socket transport to error out;
            // however, if the consumer is HTTP, then the HTTP transaction
            // should be restarted when this error occurs.
            //
            { PR_CONNECT_RESET_ERROR, "PR_CONNECT_RESET_ERROR" },
        };
        n = n % (sizeof(errors)/sizeof(errors[0]));
        code = errors[n].err_code;
        SOCKET_LOG(("simulating NSPR error %d [%s]\n", code, errors[n].err_name));
    }
    return code;
}
#endif

//-----------------------------------------------------------------------------

static bool
IsNSSErrorCode(PRErrorCode code)
{
  return 
    ((code >= nsINSSErrorsService::NSS_SEC_ERROR_BASE) && 
      (code < nsINSSErrorsService::NSS_SEC_ERROR_LIMIT))
    ||
    ((code >= nsINSSErrorsService::NSS_SSL_ERROR_BASE) && 
      (code < nsINSSErrorsService::NSS_SSL_ERROR_LIMIT));
}

// this logic is duplicated from the implementation of
// nsINSSErrorsService::getXPCOMFromNSSError
// It might have been better to implement that interface here...
static nsresult
GetXPCOMFromNSSError(PRErrorCode code)
{
    return NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_SECURITY, -1 * code);
}

static nsresult
ErrorAccordingToNSPR(PRErrorCode errorCode)
{
    nsresult rv = NS_ERROR_FAILURE;
    switch (errorCode) {
    case PR_WOULD_BLOCK_ERROR:
        rv = NS_BASE_STREAM_WOULD_BLOCK;
        break;
    case PR_CONNECT_ABORTED_ERROR:
    case PR_CONNECT_RESET_ERROR:
        rv = NS_ERROR_NET_RESET;
        break;
    case PR_END_OF_FILE_ERROR: // XXX document this correlation
        rv = NS_ERROR_NET_INTERRUPT;
        break;
    case PR_CONNECT_REFUSED_ERROR:
    case PR_NETWORK_UNREACHABLE_ERROR: // XXX need new nsresult for this!
    case PR_HOST_UNREACHABLE_ERROR:    // XXX and this!
    case PR_ADDRESS_NOT_AVAILABLE_ERROR:
    // Treat EACCES as a soft error since (at least on Linux) connect() returns
    // EACCES when an IPv6 connection is blocked by a firewall. See bug 270784.
    case PR_ADDRESS_NOT_SUPPORTED_ERROR:
    case PR_NO_ACCESS_RIGHTS_ERROR:
        rv = NS_ERROR_CONNECTION_REFUSED;
        break;
    case PR_IO_TIMEOUT_ERROR:
    case PR_CONNECT_TIMEOUT_ERROR:
        rv = NS_ERROR_NET_TIMEOUT;
        break;
    default:
        if (IsNSSErrorCode(errorCode))
            rv = GetXPCOMFromNSSError(errorCode);
        break;
    }
    SOCKET_LOG(("ErrorAccordingToNSPR [in=%d out=%x]\n", errorCode, rv));
    return rv;
}

//-----------------------------------------------------------------------------
// socket input stream impl 
//-----------------------------------------------------------------------------

nsSocketInputStream::nsSocketInputStream(nsSocketTransport *trans)
    : mTransport(trans)
    , mReaderRefCnt(0)
    , mCondition(NS_OK)
    , mCallbackFlags(0)
    , mByteCount(0)
{
}

nsSocketInputStream::~nsSocketInputStream()
{
}

// called on the socket transport thread...
//
//   condition : failure code if socket has been closed
//
void
nsSocketInputStream::OnSocketReady(nsresult condition)
{
    SOCKET_LOG(("nsSocketInputStream::OnSocketReady [this=%x cond=%x]\n",
        this, condition));

    NS_ASSERTION(PR_GetCurrentThread() == gSocketThread, "wrong thread");

    nsCOMPtr<nsIInputStreamCallback> callback;
    {
        MutexAutoLock lock(mTransport->mLock);

        // update condition, but be careful not to erase an already
        // existing error condition.
        if (NS_SUCCEEDED(mCondition))
            mCondition = condition;

        // ignore event if only waiting for closure and not closed.
        if (NS_FAILED(mCondition) || !(mCallbackFlags & WAIT_CLOSURE_ONLY)) {
            callback = mCallback;
            mCallback = nsnull;
            mCallbackFlags = 0;
        }
    }

    if (callback)
        callback->OnInputStreamReady(this);
}

NS_IMPL_QUERY_INTERFACE2(nsSocketInputStream,
                         nsIInputStream,
                         nsIAsyncInputStream)

NS_IMETHODIMP_(nsrefcnt)
nsSocketInputStream::AddRef()
{
    NS_AtomicIncrementRefcnt(mReaderRefCnt);
    return mTransport->AddRef();
}

NS_IMETHODIMP_(nsrefcnt)
nsSocketInputStream::Release()
{
    if (NS_AtomicDecrementRefcnt(mReaderRefCnt) == 0)
        Close();
    return mTransport->Release();
}

NS_IMETHODIMP
nsSocketInputStream::Close()
{
    return CloseWithStatus(NS_BASE_STREAM_CLOSED);
}

NS_IMETHODIMP
nsSocketInputStream::Available(PRUint32 *avail)
{
    SOCKET_LOG(("nsSocketInputStream::Available [this=%x]\n", this));

    *avail = 0;

    PRFileDesc *fd;
    {
        MutexAutoLock lock(mTransport->mLock);

        if (NS_FAILED(mCondition))
            return mCondition;

        fd = mTransport->GetFD_Locked();
        if (!fd)
            return NS_OK;
    }

    // cannot hold lock while calling NSPR.  (worried about the fact that PSM
    // synchronously proxies notifications over to the UI thread, which could
    // mistakenly try to re-enter this code.)
    PRInt32 n = PR_Available(fd);

    // PSM does not implement PR_Available() so do a best approximation of it
    // with MSG_PEEK
    if ((n == -1) && (PR_GetError() == PR_NOT_IMPLEMENTED_ERROR)) {
        char c;

        n = PR_Recv(fd, &c, 1, PR_MSG_PEEK, 0);
        SOCKET_LOG(("nsSocketInputStream::Available [this=%x] "
                    "using PEEK backup n=%d]\n", this, n));
    }

    nsresult rv;
    {
        MutexAutoLock lock(mTransport->mLock);

        mTransport->ReleaseFD_Locked(fd);

        if (n >= 0)
            *avail = n;
        else {
            PRErrorCode code = PR_GetError();
            if (code == PR_WOULD_BLOCK_ERROR)
                return NS_OK;
            mCondition = ErrorAccordingToNSPR(code);
        }
        rv = mCondition;
    }
    if (NS_FAILED(rv))
        mTransport->OnInputClosed(rv);
    return rv;
}

NS_IMETHODIMP
nsSocketInputStream::Read(char *buf, PRUint32 count, PRUint32 *countRead)
{
    SOCKET_LOG(("nsSocketInputStream::Read [this=%x count=%u]\n", this, count));

    *countRead = 0;

    PRFileDesc* fd = nsnull;
    {
        MutexAutoLock lock(mTransport->mLock);

        if (NS_FAILED(mCondition))
            return (mCondition == NS_BASE_STREAM_CLOSED) ? NS_OK : mCondition;

        fd = mTransport->GetFD_Locked();
        if (!fd)
            return NS_BASE_STREAM_WOULD_BLOCK;
    }

    SOCKET_LOG(("  calling PR_Read [count=%u]\n", count));

    // cannot hold lock while calling NSPR.  (worried about the fact that PSM
    // synchronously proxies notifications over to the UI thread, which could
    // mistakenly try to re-enter this code.)
    PRInt32 n = PR_Read(fd, buf, count);

    SOCKET_LOG(("  PR_Read returned [n=%d]\n", n));

    nsresult rv = NS_OK;
    {
        MutexAutoLock lock(mTransport->mLock);

#ifdef ENABLE_SOCKET_TRACING
        if (n > 0)
            mTransport->TraceInBuf(buf, n);
#endif

        mTransport->ReleaseFD_Locked(fd);

        if (n > 0)
            mByteCount += (*countRead = n);
        else if (n < 0) {
            PRErrorCode code = PR_GetError();
            if (code == PR_WOULD_BLOCK_ERROR)
                return NS_BASE_STREAM_WOULD_BLOCK;
            mCondition = ErrorAccordingToNSPR(code);
        }
        rv = mCondition;
    }
    if (NS_FAILED(rv))
        mTransport->OnInputClosed(rv);

    // only send this notification if we have indeed read some data.
    // see bug 196827 for an example of why this is important.
    if (n > 0)
        mTransport->SendStatus(nsISocketTransport::STATUS_RECEIVING_FROM);
    return rv;
}

NS_IMETHODIMP
nsSocketInputStream::ReadSegments(nsWriteSegmentFun writer, void *closure,
                                  PRUint32 count, PRUint32 *countRead)
{
    // socket stream is unbuffered
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsSocketInputStream::IsNonBlocking(bool *nonblocking)
{
    *nonblocking = true;
    return NS_OK;
}

NS_IMETHODIMP
nsSocketInputStream::CloseWithStatus(nsresult reason)
{
    SOCKET_LOG(("nsSocketInputStream::CloseWithStatus [this=%x reason=%x]\n", this, reason));

    // may be called from any thread
 
    nsresult rv;
    {
        MutexAutoLock lock(mTransport->mLock);

        if (NS_SUCCEEDED(mCondition))
            rv = mCondition = reason;
        else
            rv = NS_OK;
    }
    if (NS_FAILED(rv))
        mTransport->OnInputClosed(rv);
    return NS_OK;
}

NS_IMETHODIMP
nsSocketInputStream::AsyncWait(nsIInputStreamCallback *callback,
                               PRUint32 flags,
                               PRUint32 amount,
                               nsIEventTarget *target)
{
    SOCKET_LOG(("nsSocketInputStream::AsyncWait [this=%x]\n", this));

    // This variable will be non-null when we want to call the callback
    // directly from this function, but outside the lock.
    // (different from callback when target is not null)
    nsCOMPtr<nsIInputStreamCallback> directCallback;
    {
        MutexAutoLock lock(mTransport->mLock);

        if (callback && target) {
            //
            // build event proxy
            //
            // failure to create an event proxy (most likely out of memory)
            // shouldn't alter the state of the transport.
            //
            nsCOMPtr<nsIInputStreamCallback> temp;
            nsresult rv = NS_NewInputStreamReadyEvent(getter_AddRefs(temp),
                                                      callback, target);
            if (NS_FAILED(rv)) return rv;
            mCallback = temp;
        }
        else
            mCallback = callback;

        if (NS_FAILED(mCondition))
            directCallback.swap(mCallback);
        else
            mCallbackFlags = flags;
    }
    if (directCallback)
        directCallback->OnInputStreamReady(this);
    else
        mTransport->OnInputPending();

    return NS_OK;
}

//-----------------------------------------------------------------------------
// socket output stream impl 
//-----------------------------------------------------------------------------

nsSocketOutputStream::nsSocketOutputStream(nsSocketTransport *trans)
    : mTransport(trans)
    , mWriterRefCnt(0)
    , mCondition(NS_OK)
    , mCallbackFlags(0)
    , mByteCount(0)
{
}

nsSocketOutputStream::~nsSocketOutputStream()
{
}

// called on the socket transport thread...
//
//   condition : failure code if socket has been closed
//
void
nsSocketOutputStream::OnSocketReady(nsresult condition)
{
    SOCKET_LOG(("nsSocketOutputStream::OnSocketReady [this=%x cond=%x]\n",
        this, condition));

    NS_ASSERTION(PR_GetCurrentThread() == gSocketThread, "wrong thread");

    nsCOMPtr<nsIOutputStreamCallback> callback;
    {
        MutexAutoLock lock(mTransport->mLock);

        // update condition, but be careful not to erase an already
        // existing error condition.
        if (NS_SUCCEEDED(mCondition))
            mCondition = condition;

        // ignore event if only waiting for closure and not closed.
        if (NS_FAILED(mCondition) || !(mCallbackFlags & WAIT_CLOSURE_ONLY)) {
            callback = mCallback;
            mCallback = nsnull;
            mCallbackFlags = 0;
        }
    }

    if (callback)
        callback->OnOutputStreamReady(this);
}

NS_IMPL_QUERY_INTERFACE2(nsSocketOutputStream,
                         nsIOutputStream,
                         nsIAsyncOutputStream)

NS_IMETHODIMP_(nsrefcnt)
nsSocketOutputStream::AddRef()
{
    NS_AtomicIncrementRefcnt(mWriterRefCnt);
    return mTransport->AddRef();
}

NS_IMETHODIMP_(nsrefcnt)
nsSocketOutputStream::Release()
{
    if (NS_AtomicDecrementRefcnt(mWriterRefCnt) == 0)
        Close();
    return mTransport->Release();
}

NS_IMETHODIMP
nsSocketOutputStream::Close()
{
    return CloseWithStatus(NS_BASE_STREAM_CLOSED);
}

NS_IMETHODIMP
nsSocketOutputStream::Flush()
{
    return NS_OK;
}

NS_IMETHODIMP
nsSocketOutputStream::Write(const char *buf, PRUint32 count, PRUint32 *countWritten)
{
    SOCKET_LOG(("nsSocketOutputStream::Write [this=%x count=%u]\n", this, count));

    *countWritten = 0;

    // A write of 0 bytes can be used to force the initial SSL handshake, so do
    // not reject that.

    PRFileDesc* fd = nsnull;
    {
        MutexAutoLock lock(mTransport->mLock);

        if (NS_FAILED(mCondition))
            return mCondition;
        
        fd = mTransport->GetFD_Locked();
        if (!fd)
            return NS_BASE_STREAM_WOULD_BLOCK;
    }

    SOCKET_LOG(("  calling PR_Write [count=%u]\n", count));

    // cannot hold lock while calling NSPR.  (worried about the fact that PSM
    // synchronously proxies notifications over to the UI thread, which could
    // mistakenly try to re-enter this code.)
    PRInt32 n = PR_Write(fd, buf, count);

    SOCKET_LOG(("  PR_Write returned [n=%d]\n", n));

    nsresult rv = NS_OK;
    {
        MutexAutoLock lock(mTransport->mLock);

#ifdef ENABLE_SOCKET_TRACING
        if (n > 0)
            mTransport->TraceOutBuf(buf, n);
#endif

        mTransport->ReleaseFD_Locked(fd);

        if (n > 0)
            mByteCount += (*countWritten = n);
        else if (n < 0) {
            PRErrorCode code = PR_GetError();
            if (code == PR_WOULD_BLOCK_ERROR)
                return NS_BASE_STREAM_WOULD_BLOCK;
            mCondition = ErrorAccordingToNSPR(code);
        }
        rv = mCondition;
    }
    if (NS_FAILED(rv))
        mTransport->OnOutputClosed(rv);

    // only send this notification if we have indeed written some data.
    // see bug 196827 for an example of why this is important.
    if (n > 0)
        mTransport->SendStatus(nsISocketTransport::STATUS_SENDING_TO);
    return rv;
}

NS_IMETHODIMP
nsSocketOutputStream::WriteSegments(nsReadSegmentFun reader, void *closure,
                                    PRUint32 count, PRUint32 *countRead)
{
    // socket stream is unbuffered
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_METHOD
nsSocketOutputStream::WriteFromSegments(nsIInputStream *input,
                                        void *closure,
                                        const char *fromSegment,
                                        PRUint32 offset,
                                        PRUint32 count,
                                        PRUint32 *countRead)
{
    nsSocketOutputStream *self = (nsSocketOutputStream *) closure;
    return self->Write(fromSegment, count, countRead);
}

NS_IMETHODIMP
nsSocketOutputStream::WriteFrom(nsIInputStream *stream, PRUint32 count, PRUint32 *countRead)
{
    return stream->ReadSegments(WriteFromSegments, this, count, countRead);
}

NS_IMETHODIMP
nsSocketOutputStream::IsNonBlocking(bool *nonblocking)
{
    *nonblocking = true;
    return NS_OK;
}

NS_IMETHODIMP
nsSocketOutputStream::CloseWithStatus(nsresult reason)
{
    SOCKET_LOG(("nsSocketOutputStream::CloseWithStatus [this=%x reason=%x]\n", this, reason));

    // may be called from any thread
 
    nsresult rv;
    {
        MutexAutoLock lock(mTransport->mLock);

        if (NS_SUCCEEDED(mCondition))
            rv = mCondition = reason;
        else
            rv = NS_OK;
    }
    if (NS_FAILED(rv))
        mTransport->OnOutputClosed(rv);
    return NS_OK;
}

NS_IMETHODIMP
nsSocketOutputStream::AsyncWait(nsIOutputStreamCallback *callback,
                                PRUint32 flags,
                                PRUint32 amount,
                                nsIEventTarget *target)
{
    SOCKET_LOG(("nsSocketOutputStream::AsyncWait [this=%x]\n", this));

    {
        MutexAutoLock lock(mTransport->mLock);

        if (callback && target) {
            //
            // build event proxy
            //
            // failure to create an event proxy (most likely out of memory)
            // shouldn't alter the state of the transport.
            //
            nsCOMPtr<nsIOutputStreamCallback> temp;
            nsresult rv = NS_NewOutputStreamReadyEvent(getter_AddRefs(temp),
                                                       callback, target);
            if (NS_FAILED(rv)) return rv;
            mCallback = temp;
        }
        else
            mCallback = callback;

        mCallbackFlags = flags;
    }
    mTransport->OnOutputPending();
    return NS_OK;
}

//-----------------------------------------------------------------------------
// socket transport impl
//-----------------------------------------------------------------------------

nsSocketTransport::nsSocketTransport()
    : mTypes(nsnull)
    , mTypeCount(0)
    , mPort(0)
    , mProxyPort(0)
    , mProxyTransparent(false)
    , mProxyTransparentResolvesHost(false)
    , mConnectionFlags(0)
    , mState(STATE_CLOSED)
    , mAttached(false)
    , mInputClosed(true)
    , mOutputClosed(true)
    , mResolving(false)
    , mNetAddrIsSet(false)
    , mLock("nsSocketTransport.mLock")
    , mFD(nsnull)
    , mFDref(0)
    , mFDconnected(false)
    , mInput(this)
    , mOutput(this)
    , mQoSBits(0x00)
{
    SOCKET_LOG(("creating nsSocketTransport @%x\n", this));

    NS_ADDREF(gSocketTransportService);

    mTimeouts[TIMEOUT_CONNECT]    = PR_UINT16_MAX; // no timeout
    mTimeouts[TIMEOUT_READ_WRITE] = PR_UINT16_MAX; // no timeout
}

nsSocketTransport::~nsSocketTransport()
{
    SOCKET_LOG(("destroying nsSocketTransport @%x\n", this));

    // cleanup socket type info
    if (mTypes) {
        PRUint32 i;
        for (i=0; i<mTypeCount; ++i)
            PL_strfree(mTypes[i]);
        free(mTypes);
    }
 
    nsSocketTransportService *serv = gSocketTransportService;
    NS_RELEASE(serv); // nulls argument
}

nsresult
nsSocketTransport::Init(const char **types, PRUint32 typeCount,
                        const nsACString &host, PRUint16 port,
                        nsIProxyInfo *givenProxyInfo)
{
    nsCOMPtr<nsProxyInfo> proxyInfo;
    if (givenProxyInfo) {
        proxyInfo = do_QueryInterface(givenProxyInfo);
        NS_ENSURE_ARG(proxyInfo);
    }

    // init socket type info

    mPort = port;
    mHost = host;

    const char *proxyType = nsnull;
    if (proxyInfo) {
        mProxyPort = proxyInfo->Port();
        mProxyHost = proxyInfo->Host();
        // grab proxy type (looking for "socks" for example)
        proxyType = proxyInfo->Type();
        if (proxyType && (strcmp(proxyType, "http") == 0 ||
                          strcmp(proxyType, "direct") == 0 ||
                          strcmp(proxyType, "unknown") == 0))
            proxyType = nsnull;
    }

    SOCKET_LOG(("nsSocketTransport::Init [this=%x host=%s:%hu proxy=%s:%hu]\n",
        this, mHost.get(), mPort, mProxyHost.get(), mProxyPort));

    // include proxy type as a socket type if proxy type is not "http"
    mTypeCount = typeCount + (proxyType != nsnull);
    if (!mTypeCount)
        return NS_OK;

    // if we have socket types, then the socket provider service had
    // better exist!
    nsresult rv;
    nsCOMPtr<nsISocketProviderService> spserv =
        do_GetService(kSocketProviderServiceCID, &rv);
    if (NS_FAILED(rv)) return rv;

    mTypes = (char **) malloc(mTypeCount * sizeof(char *));
    if (!mTypes)
        return NS_ERROR_OUT_OF_MEMORY;

    // now verify that each socket type has a registered socket provider.
    for (PRUint32 i = 0, type = 0; i < mTypeCount; ++i) {
        // store socket types
        if (i == 0 && proxyType)
            mTypes[i] = PL_strdup(proxyType);
        else
            mTypes[i] = PL_strdup(types[type++]);

        if (!mTypes[i]) {
            mTypeCount = i;
            return NS_ERROR_OUT_OF_MEMORY;
        }
        nsCOMPtr<nsISocketProvider> provider;
        rv = spserv->GetSocketProvider(mTypes[i], getter_AddRefs(provider));
        if (NS_FAILED(rv)) {
            NS_WARNING("no registered socket provider");
            return rv;
        }

        // note if socket type corresponds to a transparent proxy
        // XXX don't hardcode SOCKS here (use proxy info's flags instead).
        if ((strcmp(mTypes[i], "socks") == 0) ||
            (strcmp(mTypes[i], "socks4") == 0)) {
            mProxyTransparent = true;

            if (proxyInfo->Flags() & nsIProxyInfo::TRANSPARENT_PROXY_RESOLVES_HOST) {
                // we want the SOCKS layer to send the hostname
                // and port to the proxy and let it do the DNS.
                mProxyTransparentResolvesHost = true;
            }
        }
    }

    return NS_OK;
}

nsresult
nsSocketTransport::InitWithConnectedSocket(PRFileDesc *fd, const PRNetAddr *addr)
{
    NS_ASSERTION(!mFD, "already initialized");

    char buf[64];
    PR_NetAddrToString(addr, buf, sizeof(buf));
    mHost.Assign(buf);

    PRUint16 port;
    if (addr->raw.family == PR_AF_INET)
        port = addr->inet.port;
    else
        port = addr->ipv6.port;
    mPort = PR_ntohs(port);

    memcpy(&mNetAddr, addr, sizeof(PRNetAddr));

    mPollFlags = (PR_POLL_READ | PR_POLL_WRITE | PR_POLL_EXCEPT);
    mPollTimeout = mTimeouts[TIMEOUT_READ_WRITE];
    mState = STATE_TRANSFERRING;
    mNetAddrIsSet = true;

    mFD = fd;
    mFDref = 1;
    mFDconnected = 1;

    // make sure new socket is non-blocking
    PRSocketOptionData opt;
    opt.option = PR_SockOpt_Nonblocking;
    opt.value.non_blocking = true;
    PR_SetSocketOption(mFD, &opt);

    SOCKET_LOG(("nsSocketTransport::InitWithConnectedSocket [this=%p addr=%s:%hu]\n",
        this, mHost.get(), mPort));

    // jump to InitiateSocket to get ourselves attached to the STS poll list.
    return PostEvent(MSG_RETRY_INIT_SOCKET);
}

nsresult
nsSocketTransport::PostEvent(PRUint32 type, nsresult status, nsISupports *param)
{
    SOCKET_LOG(("nsSocketTransport::PostEvent [this=%p type=%u status=%x param=%p]\n",
        this, type, status, param));

    nsCOMPtr<nsIRunnable> event = new nsSocketEvent(this, type, status, param);
    if (!event)
        return NS_ERROR_OUT_OF_MEMORY;

    return gSocketTransportService->Dispatch(event, NS_DISPATCH_NORMAL);
}

void
nsSocketTransport::SendStatus(nsresult status)
{
    SOCKET_LOG(("nsSocketTransport::SendStatus [this=%x status=%x]\n", this, status));

    nsCOMPtr<nsITransportEventSink> sink;
    PRUint64 progress;
    {
        MutexAutoLock lock(mLock);
        sink = mEventSink;
        switch (status) {
        case STATUS_SENDING_TO:
            progress = mOutput.ByteCount();
            break;
        case STATUS_RECEIVING_FROM:
            progress = mInput.ByteCount();
            break;
        default:
            progress = 0;
            break;
        }
    }
    if (sink)
        sink->OnTransportStatus(this, status, progress, LL_MAXUINT);
}

nsresult
nsSocketTransport::ResolveHost()
{
    SOCKET_LOG(("nsSocketTransport::ResolveHost [this=%x]\n", this));

    nsresult rv;

    if (!mProxyHost.IsEmpty()) {
        if (!mProxyTransparent || mProxyTransparentResolvesHost) {
            // When not resolving mHost locally, we still want to ensure that
            // it only contains valid characters.  See bug 304904 for details.
            if (!net_IsValidHostName(mHost))
                return NS_ERROR_UNKNOWN_HOST;
        }
        if (mProxyTransparentResolvesHost) {
            // Name resolution is done on the server side.  Just pretend
            // client resolution is complete, this will get picked up later.
            // since we don't need to do DNS now, we bypass the resolving
            // step by initializing mNetAddr to an empty address, but we
            // must keep the port. The SOCKS IO layer will use the hostname
            // we send it when it's created, rather than the empty address
            // we send with the connect call.
            mState = STATE_RESOLVING;
            PR_SetNetAddr(PR_IpAddrAny, PR_AF_INET, SocketPort(), &mNetAddr);
            return PostEvent(MSG_DNS_LOOKUP_COMPLETE, NS_OK, nsnull);
        }
    }

    nsCOMPtr<nsIDNSService> dns = do_GetService(kDNSServiceCID, &rv);
    if (NS_FAILED(rv)) return rv;

    mResolving = true;

    PRUint32 dnsFlags = 0;
    if (mConnectionFlags & nsSocketTransport::BYPASS_CACHE)
        dnsFlags = nsIDNSService::RESOLVE_BYPASS_CACHE;
    if (mConnectionFlags & nsSocketTransport::DISABLE_IPV6)
        dnsFlags |= nsIDNSService::RESOLVE_DISABLE_IPV6;

    SendStatus(STATUS_RESOLVING);
    rv = dns->AsyncResolve(SocketHost(), dnsFlags, this, nsnull,
                           getter_AddRefs(mDNSRequest));
    if (NS_SUCCEEDED(rv)) {
        SOCKET_LOG(("  advancing to STATE_RESOLVING\n"));
        mState = STATE_RESOLVING;
    }
    return rv;
}

nsresult
nsSocketTransport::BuildSocket(PRFileDesc *&fd, bool &proxyTransparent, bool &usingSSL)
{
    SOCKET_LOG(("nsSocketTransport::BuildSocket [this=%x]\n", this));

    nsresult rv;

    proxyTransparent = false;
    usingSSL = false;

    if (mTypeCount == 0) {
        fd = PR_OpenTCPSocket(mNetAddr.raw.family);
        rv = fd ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
    }
    else {
        fd = nsnull;

        nsCOMPtr<nsISocketProviderService> spserv =
            do_GetService(kSocketProviderServiceCID, &rv);
        if (NS_FAILED(rv)) return rv;

        const char *host       = mHost.get();
        PRInt32     port       = (PRInt32) mPort;
        const char *proxyHost  = mProxyHost.IsEmpty() ? nsnull : mProxyHost.get();
        PRInt32     proxyPort  = (PRInt32) mProxyPort;
        PRUint32    proxyFlags = 0;

        PRUint32 i;
        for (i=0; i<mTypeCount; ++i) {
            nsCOMPtr<nsISocketProvider> provider;

            SOCKET_LOG(("  pushing io layer [%u:%s]\n", i, mTypes[i]));

            rv = spserv->GetSocketProvider(mTypes[i], getter_AddRefs(provider));
            if (NS_FAILED(rv))
                break;

            if (mProxyTransparentResolvesHost)
                proxyFlags |= nsISocketProvider::PROXY_RESOLVES_HOST;
            
            if (mConnectionFlags & nsISocketTransport::ANONYMOUS_CONNECT)
                proxyFlags |= nsISocketProvider::ANONYMOUS_CONNECT;

            nsCOMPtr<nsISupports> secinfo;
            if (i == 0) {
                // if this is the first type, we'll want the 
                // service to allocate a new socket
                rv = provider->NewSocket(mNetAddr.raw.family,
                                         host, port, proxyHost, proxyPort,
                                         proxyFlags, &fd,
                                         getter_AddRefs(secinfo));

                if (NS_SUCCEEDED(rv) && !fd) {
                    NS_NOTREACHED("NewSocket succeeded but failed to create a PRFileDesc");
                    rv = NS_ERROR_UNEXPECTED;
                }
            }
            else {
                // the socket has already been allocated, 
                // so we just want the service to add itself
                // to the stack (such as pushing an io layer)
                rv = provider->AddToSocket(mNetAddr.raw.family,
                                           host, port, proxyHost, proxyPort,
                                           proxyFlags, fd,
                                           getter_AddRefs(secinfo));
            }
            // proxyFlags = 0; not used below this point...
            if (NS_FAILED(rv))
                break;

            // if the service was ssl or starttls, we want to hold onto the socket info
            bool isSSL = (strcmp(mTypes[i], "ssl") == 0);
            if (isSSL || (strcmp(mTypes[i], "starttls") == 0)) {
                // remember security info and give notification callbacks to PSM...
                nsCOMPtr<nsIInterfaceRequestor> callbacks;
                {
                    MutexAutoLock lock(mLock);
                    mSecInfo = secinfo;
                    callbacks = mCallbacks;
                    SOCKET_LOG(("  [secinfo=%x callbacks=%x]\n", mSecInfo.get(), mCallbacks.get()));
                }
                // don't call into PSM while holding mLock!!
                nsCOMPtr<nsISSLSocketControl> secCtrl(do_QueryInterface(secinfo));
                if (secCtrl)
                    secCtrl->SetNotificationCallbacks(callbacks);
                // remember if socket type is SSL so we can ProxyStartSSL if need be.
                usingSSL = isSSL;
            }
            else if ((strcmp(mTypes[i], "socks") == 0) ||
                     (strcmp(mTypes[i], "socks4") == 0)) {
                // since socks is transparent, any layers above
                // it do not have to worry about proxy stuff
                proxyHost = nsnull;
                proxyPort = -1;
                proxyTransparent = true;
            }
        }

        if (NS_FAILED(rv)) {
            SOCKET_LOG(("  error pushing io layer [%u:%s rv=%x]\n", i, mTypes[i], rv));
            if (fd)
                PR_Close(fd);
        }
    }

    return rv;
}

nsresult
nsSocketTransport::InitiateSocket()
{
    SOCKET_LOG(("nsSocketTransport::InitiateSocket [this=%x]\n", this));

    nsresult rv;

    //
    // find out if it is going to be ok to attach another socket to the STS.
    // if not then we have to wait for the STS to tell us that it is ok.
    // the notification is asynchronous, which means that when we could be
    // in a race to call AttachSocket once notified.  for this reason, when
    // we get notified, we just re-enter this function.  as a result, we are
    // sure to ask again before calling AttachSocket.  in this way we deal
    // with the race condition.  though it isn't the most elegant solution,
    // it is far simpler than trying to build a system that would guarantee
    // FIFO ordering (which wouldn't even be that valuable IMO).  see bug
    // 194402 for more info.
    //
    if (!gSocketTransportService->CanAttachSocket()) {
        nsCOMPtr<nsIRunnable> event =
                new nsSocketEvent(this, MSG_RETRY_INIT_SOCKET);
        if (!event)
            return NS_ERROR_OUT_OF_MEMORY;
        return gSocketTransportService->NotifyWhenCanAttachSocket(event);
    }

    //
    // if we already have a connected socket, then just attach and return.
    //
    if (mFD) {
        rv = gSocketTransportService->AttachSocket(mFD, this);
        if (NS_SUCCEEDED(rv))
            mAttached = true;
        return rv;
    }

    //
    // create new socket fd, push io layers, etc.
    //
    PRFileDesc *fd;
    bool proxyTransparent;
    bool usingSSL;

    rv = BuildSocket(fd, proxyTransparent, usingSSL);
    if (NS_FAILED(rv)) {
        SOCKET_LOG(("  BuildSocket failed [rv=%x]\n", rv));
        return rv;
    }

    PRStatus status;

    // Make the socket non-blocking...
    PRSocketOptionData opt;
    opt.option = PR_SockOpt_Nonblocking;
    opt.value.non_blocking = true;
    status = PR_SetSocketOption(fd, &opt);
    NS_ASSERTION(status == PR_SUCCESS, "unable to make socket non-blocking");

    // disable the nagle algorithm - if we rely on it to coalesce writes into
    // full packets the final packet of a multi segment POST/PUT or pipeline
    // sequence is delayed a full rtt
    opt.option = PR_SockOpt_NoDelay;
    opt.value.no_delay = true;
    PR_SetSocketOption(fd, &opt);

    // if the network.tcp.sendbuffer preference is set, use it to size SO_SNDBUF
    // The Windows default of 8KB is too small and as of vista sp1, autotuning
    // only applies to receive window
    PRInt32 sndBufferSize;
    gSocketTransportService->GetSendBufferSize(&sndBufferSize);
    if (sndBufferSize > 0) {
        opt.option = PR_SockOpt_SendBufferSize;
        opt.value.send_buffer_size = sndBufferSize;
        PR_SetSocketOption(fd, &opt);
    }

    if (mQoSBits) {
        opt.option = PR_SockOpt_IpTypeOfService;
        opt.value.tos = mQoSBits;
        PR_SetSocketOption(fd, &opt);
    }

    // inform socket transport about this newly created socket...
    rv = gSocketTransportService->AttachSocket(fd, this);
    if (NS_FAILED(rv)) {
        PR_Close(fd);
        return rv;
    }
    mAttached = true;

    // assign mFD so that we can properly handle OnSocketDetached before we've
    // established a connection.
    {
        MutexAutoLock lock(mLock);
        mFD = fd;
        mFDref = 1;
        mFDconnected = false;
    }

    SOCKET_LOG(("  advancing to STATE_CONNECTING\n"));
    mState = STATE_CONNECTING;
    mPollTimeout = mTimeouts[TIMEOUT_CONNECT];
    SendStatus(STATUS_CONNECTING_TO);

#if defined(PR_LOGGING)
    if (SOCKET_LOG_ENABLED()) {
        char buf[64];
        PR_NetAddrToString(&mNetAddr, buf, sizeof(buf));
        SOCKET_LOG(("  trying address: %s\n", buf));
    }
#endif

    // 
    // Initiate the connect() to the host...  
    //
    status = PR_Connect(fd, &mNetAddr, NS_SOCKET_CONNECT_TIMEOUT);
    if (status == PR_SUCCESS) {
        // 
        // we are connected!
        //
        OnSocketConnected();
    }
    else {
        PRErrorCode code = PR_GetError();
#if defined(TEST_CONNECT_ERRORS)
        code = RandomizeConnectError(code);
#endif
        //
        // If the PR_Connect(...) would block, then poll for a connection.
        //
        if ((PR_WOULD_BLOCK_ERROR == code) || (PR_IN_PROGRESS_ERROR == code))
            mPollFlags = (PR_POLL_EXCEPT | PR_POLL_WRITE);
        //
        // If the socket is already connected, then return success...
        //
        else if (PR_IS_CONNECTED_ERROR == code) {
            //
            // we are connected!
            //
            OnSocketConnected();

            if (mSecInfo && !mProxyHost.IsEmpty() && proxyTransparent && usingSSL) {
                // if the connection phase is finished, and the ssl layer has
                // been pushed, and we were proxying (transparently; ie. nothing
                // has to happen in the protocol layer above us), it's time for
                // the ssl to start doing it's thing.
                nsCOMPtr<nsISSLSocketControl> secCtrl =
                    do_QueryInterface(mSecInfo);
                if (secCtrl) {
                    SOCKET_LOG(("  calling ProxyStartSSL()\n"));
                    secCtrl->ProxyStartSSL();
                }
                // XXX what if we were forced to poll on the socket for a successful
                // connection... wouldn't we need to call ProxyStartSSL after a call
                // to PR_ConnectContinue indicates that we are connected?
                //
                // XXX this appears to be what the old socket transport did.  why
                // isn't this broken?
            }
        }
        //
        // A SOCKS request was rejected; get the actual error code from
        // the OS error
        //
        else if (PR_UNKNOWN_ERROR == code &&
                 mProxyTransparent &&
                 !mProxyHost.IsEmpty()) {
            code = PR_GetOSError();
            rv = ErrorAccordingToNSPR(code);
        }
        //
        // The connection was refused...
        //
        else {
            rv = ErrorAccordingToNSPR(code);
            if ((rv == NS_ERROR_CONNECTION_REFUSED) && !mProxyHost.IsEmpty())
                rv = NS_ERROR_PROXY_CONNECTION_REFUSED;
        }
    }
    return rv;
}

bool
nsSocketTransport::RecoverFromError()
{
    NS_ASSERTION(NS_FAILED(mCondition), "there should be something wrong");

    SOCKET_LOG(("nsSocketTransport::RecoverFromError [this=%x state=%x cond=%x]\n",
        this, mState, mCondition));

    // can only recover from errors in these states
    if (mState != STATE_RESOLVING && mState != STATE_CONNECTING)
        return false;

    nsresult rv;

    // OK to check this outside mLock
    NS_ASSERTION(!mFDconnected, "socket should not be connected");

    // can only recover from these errors
    if (mCondition != NS_ERROR_CONNECTION_REFUSED &&
        mCondition != NS_ERROR_PROXY_CONNECTION_REFUSED &&
        mCondition != NS_ERROR_NET_TIMEOUT &&
        mCondition != NS_ERROR_UNKNOWN_HOST &&
        mCondition != NS_ERROR_UNKNOWN_PROXY_HOST)
        return false;

    bool tryAgain = false;

    if (mConnectionFlags & DISABLE_IPV6 &&
        mCondition == NS_ERROR_UNKNOWN_HOST &&
        mState == STATE_RESOLVING &&
        !mProxyTransparentResolvesHost) {
        SOCKET_LOG(("  trying lookup again with both ipv4/ipv6 enabled\n"));
        mConnectionFlags &= ~DISABLE_IPV6;
        tryAgain = true;
    }

    // try next ip address only if past the resolver stage...
    if (mState == STATE_CONNECTING && mDNSRecord) {
        mDNSRecord->ReportUnusable(SocketPort());
        
        nsresult rv = mDNSRecord->GetNextAddr(SocketPort(), &mNetAddr);
        if (NS_SUCCEEDED(rv)) {
            SOCKET_LOG(("  trying again with next ip address\n"));
            tryAgain = true;
        }
        else if (mConnectionFlags & DISABLE_IPV6) {
            // Drop state to closed.  This will trigger new round of DNS
            // resolving bellow.
            // XXX Here should idealy be set now non-existing flag DISABLE_IPV4
            SOCKET_LOG(("  failed to connect all ipv4 hosts,"
                        " trying lookup/connect again with both ipv4/ipv6\n"));
            mState = STATE_CLOSED;
            mConnectionFlags &= ~DISABLE_IPV6;
            tryAgain = true;
        }
    }

#if defined(XP_WIN) || defined(MOZ_PLATFORM_MAEMO)
    // If not trying next address, try to make a connection using dialup. 
    // Retry if that connection is made.
    if (!tryAgain) {
        bool autodialEnabled;
        gSocketTransportService->GetAutodialEnabled(&autodialEnabled);
        if (autodialEnabled) {
          tryAgain = nsNativeConnectionHelper::OnConnectionFailed(
                       NS_ConvertUTF8toUTF16(SocketHost()).get());
	    }
    }
#endif

    // prepare to try again.
    if (tryAgain) {
        PRUint32 msg;

        if (mState == STATE_CONNECTING) {
            mState = STATE_RESOLVING;
            msg = MSG_DNS_LOOKUP_COMPLETE;
        }
        else {
            mState = STATE_CLOSED;
            msg = MSG_ENSURE_CONNECT;
        }

        rv = PostEvent(msg, NS_OK);
        if (NS_FAILED(rv))
            tryAgain = false;
    }

    return tryAgain;
}

// called on the socket thread only
void
nsSocketTransport::OnMsgInputClosed(nsresult reason)
{
    SOCKET_LOG(("nsSocketTransport::OnMsgInputClosed [this=%x reason=%x]\n",
        this, reason));

    NS_ASSERTION(PR_GetCurrentThread() == gSocketThread, "wrong thread");

    mInputClosed = true;
    // check if event should affect entire transport
    if (NS_FAILED(reason) && (reason != NS_BASE_STREAM_CLOSED))
        mCondition = reason;                // XXX except if NS_FAILED(mCondition), right??
    else if (mOutputClosed)
        mCondition = NS_BASE_STREAM_CLOSED; // XXX except if NS_FAILED(mCondition), right??
    else {
        if (mState == STATE_TRANSFERRING)
            mPollFlags &= ~PR_POLL_READ;
        mInput.OnSocketReady(reason);
    }
}

// called on the socket thread only
void
nsSocketTransport::OnMsgOutputClosed(nsresult reason)
{
    SOCKET_LOG(("nsSocketTransport::OnMsgOutputClosed [this=%x reason=%x]\n",
        this, reason));

    NS_ASSERTION(PR_GetCurrentThread() == gSocketThread, "wrong thread");

    mOutputClosed = true;
    // check if event should affect entire transport
    if (NS_FAILED(reason) && (reason != NS_BASE_STREAM_CLOSED))
        mCondition = reason;                // XXX except if NS_FAILED(mCondition), right??
    else if (mInputClosed)
        mCondition = NS_BASE_STREAM_CLOSED; // XXX except if NS_FAILED(mCondition), right??
    else {
        if (mState == STATE_TRANSFERRING)
            mPollFlags &= ~PR_POLL_WRITE;
        mOutput.OnSocketReady(reason);
    }
}

void
nsSocketTransport::OnSocketConnected()
{
    SOCKET_LOG(("  advancing to STATE_TRANSFERRING\n"));

    mPollFlags = (PR_POLL_READ | PR_POLL_WRITE | PR_POLL_EXCEPT);
    mPollTimeout = mTimeouts[TIMEOUT_READ_WRITE];
    mState = STATE_TRANSFERRING;

    // Set the mNetAddrIsSet flag only when state has reached TRANSFERRING
    // because we need to make sure its value does not change due to failover
    mNetAddrIsSet = true;

    // assign mFD (must do this within the transport lock), but take care not
    // to trample over mFDref if mFD is already set.
    {
        MutexAutoLock lock(mLock);
        NS_ASSERTION(mFD, "no socket");
        NS_ASSERTION(mFDref == 1, "wrong socket ref count");
        mFDconnected = true;
    }

    SendStatus(STATUS_CONNECTED_TO);
}

PRFileDesc *
nsSocketTransport::GetFD_Locked()
{
    // mFD is not available to the streams while disconnected.
    if (!mFDconnected)
        return nsnull;

    if (mFD)
        mFDref++;

    return mFD;
}

void
nsSocketTransport::ReleaseFD_Locked(PRFileDesc *fd)
{
    NS_ASSERTION(mFD == fd, "wrong fd");

    if (--mFDref == 0) {
        SOCKET_LOG(("nsSocketTransport: calling PR_Close [this=%x]\n", this));
        PR_Close(mFD);
        mFD = nsnull;
    }
}

//-----------------------------------------------------------------------------
// socket event handler impl

void
nsSocketTransport::OnSocketEvent(PRUint32 type, nsresult status, nsISupports *param)
{
    SOCKET_LOG(("nsSocketTransport::OnSocketEvent [this=%p type=%u status=%x param=%p]\n",
        this, type, status, param));

    if (NS_FAILED(mCondition)) {
        // block event since we're apparently already dead.
        SOCKET_LOG(("  blocking event [condition=%x]\n", mCondition));
        //
        // notify input/output streams in case either has a pending notify.
        //
        mInput.OnSocketReady(mCondition);
        mOutput.OnSocketReady(mCondition);
        return;
    }

    switch (type) {
    case MSG_ENSURE_CONNECT:
        SOCKET_LOG(("  MSG_ENSURE_CONNECT\n"));
        //
        // ensure that we have created a socket, attached it, and have a
        // connection.
        //
        if (mState == STATE_CLOSED)
            mCondition = ResolveHost();
        else
            SOCKET_LOG(("  ignoring redundant event\n"));
        break;

    case MSG_DNS_LOOKUP_COMPLETE:
        if (mDNSRequest)  // only send this if we actually resolved anything
            SendStatus(STATUS_RESOLVED);

        SOCKET_LOG(("  MSG_DNS_LOOKUP_COMPLETE\n"));
        mDNSRequest = 0;
        if (param) {
            mDNSRecord = static_cast<nsIDNSRecord *>(param);
            mDNSRecord->GetNextAddr(SocketPort(), &mNetAddr);
        }
        // status contains DNS lookup status
        if (NS_FAILED(status)) {
            // When using a HTTP proxy, NS_ERROR_UNKNOWN_HOST means the HTTP 
            // proxy host is not found, so we fixup the error code.
            // For SOCKS proxies (mProxyTransparent == true), the socket 
            // transport resolves the real host here, so there's no fixup 
            // (see bug 226943).
            if ((status == NS_ERROR_UNKNOWN_HOST) && !mProxyTransparent &&
                !mProxyHost.IsEmpty())
                mCondition = NS_ERROR_UNKNOWN_PROXY_HOST;
            else
                mCondition = status;
        }
        else if (mState == STATE_RESOLVING)
            mCondition = InitiateSocket();
        break;

    case MSG_RETRY_INIT_SOCKET:
        mCondition = InitiateSocket();
        break;

    case MSG_INPUT_CLOSED:
        SOCKET_LOG(("  MSG_INPUT_CLOSED\n"));
        OnMsgInputClosed(status);
        break;

    case MSG_INPUT_PENDING:
        SOCKET_LOG(("  MSG_INPUT_PENDING\n"));
        OnMsgInputPending();
        break;

    case MSG_OUTPUT_CLOSED:
        SOCKET_LOG(("  MSG_OUTPUT_CLOSED\n"));
        OnMsgOutputClosed(status);
        break;

    case MSG_OUTPUT_PENDING:
        SOCKET_LOG(("  MSG_OUTPUT_PENDING\n"));
        OnMsgOutputPending();
        break;
    case MSG_TIMEOUT_CHANGED:
        SOCKET_LOG(("  MSG_TIMEOUT_CHANGED\n"));
        mPollTimeout = mTimeouts[(mState == STATE_TRANSFERRING)
          ? TIMEOUT_READ_WRITE : TIMEOUT_CONNECT];
        break;
    default:
        SOCKET_LOG(("  unhandled event!\n"));
    }
    
    if (NS_FAILED(mCondition)) {
        SOCKET_LOG(("  after event [this=%x cond=%x]\n", this, mCondition));
        if (!mAttached) // need to process this error ourselves...
            OnSocketDetached(nsnull);
    }
    else if (mPollFlags == PR_POLL_EXCEPT)
        mPollFlags = 0; // make idle
}

//-----------------------------------------------------------------------------
// socket handler impl

void
nsSocketTransport::OnSocketReady(PRFileDesc *fd, PRInt16 outFlags)
{
    SOCKET_LOG(("nsSocketTransport::OnSocketReady [this=%x outFlags=%hd]\n",
        this, outFlags));

    if (outFlags == -1) {
        SOCKET_LOG(("socket timeout expired\n"));
        mCondition = NS_ERROR_NET_TIMEOUT;
        return;
    }

    if (mState == STATE_TRANSFERRING) {
        // if waiting to write and socket is writable or hit an exception.
        if ((mPollFlags & PR_POLL_WRITE) && (outFlags & ~PR_POLL_READ)) {
            // assume that we won't need to poll any longer (the stream will
            // request that we poll again if it is still pending).
            mPollFlags &= ~PR_POLL_WRITE;
            mOutput.OnSocketReady(NS_OK);
        }
        // if waiting to read and socket is readable or hit an exception.
        if ((mPollFlags & PR_POLL_READ) && (outFlags & ~PR_POLL_WRITE)) {
            // assume that we won't need to poll any longer (the stream will
            // request that we poll again if it is still pending).
            mPollFlags &= ~PR_POLL_READ;
            mInput.OnSocketReady(NS_OK);
        }
        // Update poll timeout in case it was changed
        mPollTimeout = mTimeouts[TIMEOUT_READ_WRITE];
    }
    else if (mState == STATE_CONNECTING) {
        PRStatus status = PR_ConnectContinue(fd, outFlags);
        if (status == PR_SUCCESS) {
            //
            // we are connected!
            //
            OnSocketConnected();
        }
        else {
            PRErrorCode code = PR_GetError();
#if defined(TEST_CONNECT_ERRORS)
            code = RandomizeConnectError(code);
#endif
            //
            // If the connect is still not ready, then continue polling...
            //
            if ((PR_WOULD_BLOCK_ERROR == code) || (PR_IN_PROGRESS_ERROR == code)) {
                // Set up the select flags for connect...
                mPollFlags = (PR_POLL_EXCEPT | PR_POLL_WRITE);
                // Update poll timeout in case it was changed
                mPollTimeout = mTimeouts[TIMEOUT_CONNECT];
            }
            //
            // The SOCKS proxy rejected our request. Find out why.
            //
            else if (PR_UNKNOWN_ERROR == code &&
                     mProxyTransparent &&
                     !mProxyHost.IsEmpty()) {
                code = PR_GetOSError();
                mCondition = ErrorAccordingToNSPR(code);
            }
            else {
                //
                // else, the connection failed...
                //
                mCondition = ErrorAccordingToNSPR(code);
                if ((mCondition == NS_ERROR_CONNECTION_REFUSED) && !mProxyHost.IsEmpty())
                    mCondition = NS_ERROR_PROXY_CONNECTION_REFUSED;
                SOCKET_LOG(("  connection failed! [reason=%x]\n", mCondition));
            }
        }
    }
    else {
        NS_ERROR("unexpected socket state");
        mCondition = NS_ERROR_UNEXPECTED;
    }

    if (mPollFlags == PR_POLL_EXCEPT)
        mPollFlags = 0; // make idle
}

// called on the socket thread only
void
nsSocketTransport::OnSocketDetached(PRFileDesc *fd)
{
    SOCKET_LOG(("nsSocketTransport::OnSocketDetached [this=%x cond=%x]\n",
        this, mCondition));

    NS_ASSERTION(PR_GetCurrentThread() == gSocketThread, "wrong thread");

    // if we didn't initiate this detach, then be sure to pass an error
    // condition up to our consumers.  (e.g., STS is shutting down.)
    if (NS_SUCCEEDED(mCondition))
        mCondition = NS_ERROR_ABORT;

    if (RecoverFromError())
        mCondition = NS_OK;
    else {
        mState = STATE_CLOSED;

        // make sure there isn't any pending DNS request
        if (mDNSRequest) {
            mDNSRequest->Cancel(NS_ERROR_ABORT);
            mDNSRequest = 0;
        }

        //
        // notify input/output streams
        //
        mInput.OnSocketReady(mCondition);
        mOutput.OnSocketReady(mCondition);
    }

    // break any potential reference cycle between the security info object
    // and ourselves by resetting its notification callbacks object.  see
    // bug 285991 for details.
    nsCOMPtr<nsISSLSocketControl> secCtrl = do_QueryInterface(mSecInfo);
    if (secCtrl)
        secCtrl->SetNotificationCallbacks(nsnull);

    // finally, release our reference to the socket (must do this within
    // the transport lock) possibly closing the socket. Also release our
    // listeners to break potential refcount cycles.

    // We should be careful not to release mEventSink and mCallbacks while
    // we're locked, because releasing it might require acquiring the lock
    // again, so we just null out mEventSink and mCallbacks while we're
    // holding the lock, and let the stack based objects' destuctors take
    // care of destroying it if needed.
    nsCOMPtr<nsIInterfaceRequestor> ourCallbacks;
    nsCOMPtr<nsITransportEventSink> ourEventSink;
    {
        MutexAutoLock lock(mLock);
        if (mFD) {
            ReleaseFD_Locked(mFD);
            // flag mFD as unusable; this prevents other consumers from 
            // acquiring a reference to mFD.
            mFDconnected = false;
        }

        // We must release mCallbacks and mEventSink to avoid memory leak
        // but only when RecoverFromError() above failed. Otherwise we lose
        // link with UI and security callbacks on next connection attempt 
        // round. That would lead e.g. to a broken certificate exception page.
        if (NS_FAILED(mCondition)) {
            mCallbacks.swap(ourCallbacks);
            mEventSink.swap(ourEventSink);
        }
    }
}

//-----------------------------------------------------------------------------
// xpcom api

NS_IMPL_THREADSAFE_ISUPPORTS4(nsSocketTransport,
                              nsISocketTransport,
                              nsITransport,
                              nsIDNSListener,
                              nsIClassInfo)
NS_IMPL_CI_INTERFACE_GETTER3(nsSocketTransport,
                             nsISocketTransport,
                             nsITransport,
                             nsIDNSListener)

NS_IMETHODIMP
nsSocketTransport::OpenInputStream(PRUint32 flags,
                                   PRUint32 segsize,
                                   PRUint32 segcount,
                                   nsIInputStream **result)
{
    SOCKET_LOG(("nsSocketTransport::OpenInputStream [this=%x flags=%x]\n",
        this, flags));

    NS_ENSURE_TRUE(!mInput.IsReferenced(), NS_ERROR_UNEXPECTED);

    nsresult rv;
    nsCOMPtr<nsIAsyncInputStream> pipeIn;

    if (!(flags & OPEN_UNBUFFERED) || (flags & OPEN_BLOCKING)) {
        // XXX if the caller wants blocking, then the caller also gets buffered!
        //bool openBuffered = !(flags & OPEN_UNBUFFERED);
        bool openBlocking =  (flags & OPEN_BLOCKING);

        net_ResolveSegmentParams(segsize, segcount);

        // create a pipe
        nsCOMPtr<nsIAsyncOutputStream> pipeOut;
        rv = NS_NewPipe2(getter_AddRefs(pipeIn), getter_AddRefs(pipeOut),
                         !openBlocking, true, segsize, segcount);
        if (NS_FAILED(rv)) return rv;

        // async copy from socket to pipe
        rv = NS_AsyncCopy(&mInput, pipeOut, gSocketTransportService,
                          NS_ASYNCCOPY_VIA_WRITESEGMENTS, segsize);
        if (NS_FAILED(rv)) return rv;

        *result = pipeIn;
    }
    else
        *result = &mInput;

    // flag input stream as open
    mInputClosed = false;

    rv = PostEvent(MSG_ENSURE_CONNECT);
    if (NS_FAILED(rv)) return rv;

    NS_ADDREF(*result);
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::OpenOutputStream(PRUint32 flags,
                                    PRUint32 segsize,
                                    PRUint32 segcount,
                                    nsIOutputStream **result)
{
    SOCKET_LOG(("nsSocketTransport::OpenOutputStream [this=%x flags=%x]\n",
        this, flags));

    NS_ENSURE_TRUE(!mOutput.IsReferenced(), NS_ERROR_UNEXPECTED);

    nsresult rv;
    nsCOMPtr<nsIAsyncOutputStream> pipeOut;
    if (!(flags & OPEN_UNBUFFERED) || (flags & OPEN_BLOCKING)) {
        // XXX if the caller wants blocking, then the caller also gets buffered!
        //bool openBuffered = !(flags & OPEN_UNBUFFERED);
        bool openBlocking =  (flags & OPEN_BLOCKING);

        net_ResolveSegmentParams(segsize, segcount);

        // create a pipe
        nsCOMPtr<nsIAsyncInputStream> pipeIn;
        rv = NS_NewPipe2(getter_AddRefs(pipeIn), getter_AddRefs(pipeOut),
                         true, !openBlocking, segsize, segcount);
        if (NS_FAILED(rv)) return rv;

        // async copy from socket to pipe
        rv = NS_AsyncCopy(pipeIn, &mOutput, gSocketTransportService,
                          NS_ASYNCCOPY_VIA_READSEGMENTS, segsize);
        if (NS_FAILED(rv)) return rv;

        *result = pipeOut;
    }
    else
        *result = &mOutput;

    // flag output stream as open
    mOutputClosed = false;

    rv = PostEvent(MSG_ENSURE_CONNECT);
    if (NS_FAILED(rv)) return rv;

    NS_ADDREF(*result);
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::Close(nsresult reason)
{
    if (NS_SUCCEEDED(reason))
        reason = NS_BASE_STREAM_CLOSED;

    mInput.CloseWithStatus(reason);
    mOutput.CloseWithStatus(reason);
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetSecurityInfo(nsISupports **secinfo)
{
    MutexAutoLock lock(mLock);
    NS_IF_ADDREF(*secinfo = mSecInfo);
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetSecurityCallbacks(nsIInterfaceRequestor **callbacks)
{
    MutexAutoLock lock(mLock);
    NS_IF_ADDREF(*callbacks = mCallbacks);
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetSecurityCallbacks(nsIInterfaceRequestor *callbacks)
{
    nsCOMPtr<nsISupports> secinfo;
    {
        MutexAutoLock lock(mLock);
        mCallbacks = callbacks;
        SOCKET_LOG(("Reset callbacks for secinfo=%p callbacks=%p\n",
                    mSecInfo.get(), mCallbacks.get()));

        secinfo = mSecInfo;
    }

    // don't call into PSM while holding mLock!!
    nsCOMPtr<nsISSLSocketControl> secCtrl(do_QueryInterface(secinfo));
    if (secCtrl)
        secCtrl->SetNotificationCallbacks(callbacks);

    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetEventSink(nsITransportEventSink *sink,
                                nsIEventTarget *target)
{
    nsCOMPtr<nsITransportEventSink> temp;
    if (target) {
        nsresult rv = net_NewTransportEventSinkProxy(getter_AddRefs(temp),
                                                     sink, target);
        if (NS_FAILED(rv))
            return rv;
        sink = temp.get();
    }

    MutexAutoLock lock(mLock);
    mEventSink = sink;
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::IsAlive(bool *result)
{
    *result = false;

    PRFileDesc* fd = nsnull;
    {
        MutexAutoLock lock(mLock);
        if (NS_FAILED(mCondition))
            return NS_OK;
        fd = GetFD_Locked();
        if (!fd)
            return NS_OK;
    }

    // XXX do some idle-time based checks??

    char c;
    PRInt32 rval = PR_Recv(fd, &c, 1, PR_MSG_PEEK, 0);

    if ((rval > 0) || (rval < 0 && PR_GetError() == PR_WOULD_BLOCK_ERROR))
        *result = true;

    {
        MutexAutoLock lock(mLock);
        ReleaseFD_Locked(fd);
    }
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetHost(nsACString &host)
{
    host = SocketHost();
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetPort(PRInt32 *port)
{
    *port = (PRInt32) SocketPort();
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetPeerAddr(PRNetAddr *addr)
{
    // once we are in the connected state, mNetAddr will not change.
    // so if we can verify that we are in the connected state, then
    // we can freely access mNetAddr from any thread without being
    // inside a critical section.

    if (!mNetAddrIsSet) {
        SOCKET_LOG(("nsSocketTransport::GetPeerAddr [this=%p state=%d] "
                    "NOT_AVAILABLE because not yet connected.", this, mState));
        return NS_ERROR_NOT_AVAILABLE;
    }

    memcpy(addr, &mNetAddr, sizeof(mNetAddr));
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetSelfAddr(PRNetAddr *addr)
{
    // we must not call any PR methods on our file descriptor
    // while holding mLock since those methods might re-enter
    // socket transport code.

    PRFileDesc *fd;
    {
        MutexAutoLock lock(mLock);
        fd = GetFD_Locked();
    }

    if (!fd)
        return NS_ERROR_NOT_CONNECTED;

    nsresult rv =
        (PR_GetSockName(fd, addr) == PR_SUCCESS) ? NS_OK : NS_ERROR_FAILURE;

    {
        MutexAutoLock lock(mLock);
        ReleaseFD_Locked(fd);
    }

    return rv;
}

/* nsINetAddr getScriptablePeerAddr (); */
NS_IMETHODIMP
nsSocketTransport::GetScriptablePeerAddr(nsINetAddr * *addr NS_OUTPARAM)
{
    PRNetAddr rawAddr;

    nsresult rv;
    rv = GetPeerAddr(&rawAddr);
    if (NS_FAILED(rv))
        return rv;

    NS_ADDREF(*addr = new nsNetAddr(&rawAddr));

    return NS_OK;
}

/* nsINetAddr getScriptableSelfAddr (); */
NS_IMETHODIMP
nsSocketTransport::GetScriptableSelfAddr(nsINetAddr * *addr NS_OUTPARAM)
{
    PRNetAddr rawAddr;

    nsresult rv;
    rv = GetSelfAddr(&rawAddr);
    if (NS_FAILED(rv))
        return rv;

    NS_ADDREF(*addr = new nsNetAddr(&rawAddr));

    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetTimeout(PRUint32 type, PRUint32 *value)
{
    NS_ENSURE_ARG_MAX(type, nsISocketTransport::TIMEOUT_READ_WRITE);
    *value = (PRUint32) mTimeouts[type];
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetTimeout(PRUint32 type, PRUint32 value)
{
    NS_ENSURE_ARG_MAX(type, nsISocketTransport::TIMEOUT_READ_WRITE);
    // truncate overly large timeout values.
    mTimeouts[type] = (PRUint16) NS_MIN(value, PR_UINT16_MAX);
    PostEvent(MSG_TIMEOUT_CHANGED);
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetQoSBits(PRUint8 aQoSBits)
{
    // Don't do any checking here of bits.  Why?  Because as of RFC-4594
    // several different Class Selector and Assured Forwarding values
    // have been defined, but that isn't to say more won't be added later.
    // In that case, any checking would be an impediment to interoperating
    // with newer QoS definitions.

    mQoSBits = aQoSBits;
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetQoSBits(PRUint8 *aQoSBits)
{
    *aQoSBits = mQoSBits;
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::OnLookupComplete(nsICancelable *request,
                                    nsIDNSRecord  *rec,
                                    nsresult       status)
{
    // flag host lookup complete for the benefit of the ResolveHost method.
    mResolving = false;

    nsresult rv = PostEvent(MSG_DNS_LOOKUP_COMPLETE, status, rec);

    // if posting a message fails, then we should assume that the socket
    // transport has been shutdown.  this should never happen!  if it does
    // it means that the socket transport service was shutdown before the
    // DNS service.
    if (NS_FAILED(rv))
        NS_WARNING("unable to post DNS lookup complete message");

    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetInterfaces(PRUint32 *count, nsIID * **array)
{
    return NS_CI_INTERFACE_GETTER_NAME(nsSocketTransport)(count, array);
}

NS_IMETHODIMP
nsSocketTransport::GetHelperForLanguage(PRUint32 language, nsISupports **_retval)
{
    *_retval = nsnull;
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetContractID(char * *aContractID)
{
    *aContractID = nsnull;
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetClassDescription(char * *aClassDescription)
{
    *aClassDescription = nsnull;
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetClassID(nsCID * *aClassID)
{
    *aClassID = nsnull;
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetImplementationLanguage(PRUint32 *aImplementationLanguage)
{
    *aImplementationLanguage = nsIProgrammingLanguage::CPLUSPLUS;
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetFlags(PRUint32 *aFlags)
{
    *aFlags = nsIClassInfo::THREADSAFE;
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::GetClassIDNoAlloc(nsCID *aClassIDNoAlloc)
{
    return NS_ERROR_NOT_AVAILABLE;
}


NS_IMETHODIMP
nsSocketTransport::GetConnectionFlags(PRUint32 *value)
{
    *value = mConnectionFlags;
    return NS_OK;
}

NS_IMETHODIMP
nsSocketTransport::SetConnectionFlags(PRUint32 value)
{
    mConnectionFlags = value;
    return NS_OK;
}


#ifdef ENABLE_SOCKET_TRACING

#include <stdio.h>
#include <ctype.h>
#include "prenv.h"

static void
DumpBytesToFile(const char *path, const char *header, const char *buf, PRInt32 n)
{
    FILE *fp = fopen(path, "a");

    fprintf(fp, "\n%s [%d bytes]\n", header, n);

    const unsigned char *p;
    while (n) {
        p = (const unsigned char *) buf;

        PRInt32 i, row_max = NS_MIN(16, n);

        for (i = 0; i < row_max; ++i)
            fprintf(fp, "%02x  ", *p++);
        for (i = row_max; i < 16; ++i)
            fprintf(fp, "    ");

        p = (const unsigned char *) buf;
        for (i = 0; i < row_max; ++i, ++p) {
            if (isprint(*p))
                fprintf(fp, "%c", *p);
            else
                fprintf(fp, ".");
        }

        fprintf(fp, "\n");
        buf += row_max;
        n -= row_max;
    }

    fprintf(fp, "\n");
    fclose(fp);
}

void
nsSocketTransport::TraceInBuf(const char *buf, PRInt32 n)
{
    char *val = PR_GetEnv("NECKO_SOCKET_TRACE_LOG");
    if (!val || !*val)
        return;

    nsCAutoString header;
    header.Assign(NS_LITERAL_CSTRING("Reading from: ") + mHost);
    header.Append(':');
    header.AppendInt(mPort);

    DumpBytesToFile(val, header.get(), buf, n);
}

void
nsSocketTransport::TraceOutBuf(const char *buf, PRInt32 n)
{
    char *val = PR_GetEnv("NECKO_SOCKET_TRACE_LOG");
    if (!val || !*val)
        return;

    nsCAutoString header;
    header.Assign(NS_LITERAL_CSTRING("Writing to: ") + mHost);
    header.Append(':');
    header.AppendInt(mPort);

    DumpBytesToFile(val, header.get(), buf, n);
}

#endif
