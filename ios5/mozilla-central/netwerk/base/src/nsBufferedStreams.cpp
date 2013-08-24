/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "IPC/IPCMessageUtils.h"
#include "mozilla/net/NeckoMessageUtils.h"

#include "nsAlgorithm.h"
#include "nsBufferedStreams.h"
#include "nsStreamUtils.h"
#include "nsCRT.h"
#include "nsNetCID.h"
#include "nsIClassInfoImpl.h"

#ifdef DEBUG_brendan
# define METERING
#endif

#ifdef METERING
# include <stdio.h>
# define METER(x)       x
# define MAX_BIG_SEEKS  20

static struct {
    PRUint32            mSeeksWithinBuffer;
    PRUint32            mSeeksOutsideBuffer;
    PRUint32            mBufferReadUponSeek;
    PRUint32            mBufferUnreadUponSeek;
    PRUint32            mBytesReadFromBuffer;
    PRUint32            mBigSeekIndex;
    struct {
        PRInt64         mOldOffset;
        PRInt64         mNewOffset;
    } mBigSeek[MAX_BIG_SEEKS];
} bufstats;
#else
# define METER(x)       /* nothing */
#endif

////////////////////////////////////////////////////////////////////////////////
// nsBufferedStream

nsBufferedStream::nsBufferedStream()
    : mBuffer(nsnull),
      mBufferStartOffset(0),
      mCursor(0), 
      mFillPoint(0),
      mStream(nsnull),
      mBufferDisabled(false),
      mEOF(false),
      mGetBufferCount(0)
{
}

nsBufferedStream::~nsBufferedStream()
{
    Close();
}

NS_IMPL_THREADSAFE_ISUPPORTS1(nsBufferedStream, nsISeekableStream)

nsresult
nsBufferedStream::Init(nsISupports* stream, PRUint32 bufferSize)
{
    NS_ASSERTION(stream, "need to supply a stream");
    NS_ASSERTION(mStream == nsnull, "already inited");
    mStream = stream;
    NS_IF_ADDREF(mStream);
    mBufferSize = bufferSize;
    mBufferStartOffset = 0;
    mCursor = 0;
    mBuffer = new char[bufferSize];
    if (mBuffer == nsnull)
        return NS_ERROR_OUT_OF_MEMORY;
    return NS_OK;
}

nsresult
nsBufferedStream::Close()
{
    NS_IF_RELEASE(mStream);
    if (mBuffer) {
        delete[] mBuffer;
        mBuffer = nsnull;
        mBufferSize = 0;
        mBufferStartOffset = 0;
        mCursor = 0;
        mFillPoint = 0;
    }
#ifdef METERING
    {
        static FILE *tfp;
        if (!tfp) {
            tfp = fopen("/tmp/bufstats", "w");
            if (tfp)
                setvbuf(tfp, NULL, _IOLBF, 0);
        }
        if (tfp) {
            fprintf(tfp, "seeks within buffer:    %u\n",
                    bufstats.mSeeksWithinBuffer);
            fprintf(tfp, "seeks outside buffer:   %u\n",
                    bufstats.mSeeksOutsideBuffer);
            fprintf(tfp, "buffer read on seek:    %u\n",
                    bufstats.mBufferReadUponSeek);
            fprintf(tfp, "buffer unread on seek:  %u\n",
                    bufstats.mBufferUnreadUponSeek);
            fprintf(tfp, "bytes read from buffer: %u\n",
                    bufstats.mBytesReadFromBuffer);
            for (PRUint32 i = 0; i < bufstats.mBigSeekIndex; i++) {
                fprintf(tfp, "bigseek[%u] = {old: %u, new: %u}\n",
                        i,
                        bufstats.mBigSeek[i].mOldOffset,
                        bufstats.mBigSeek[i].mNewOffset);
            }
        }
    }
#endif
    return NS_OK;
}

NS_IMETHODIMP
nsBufferedStream::Seek(PRInt32 whence, PRInt64 offset)
{
    if (mStream == nsnull)
        return NS_BASE_STREAM_CLOSED;
    
    // If the underlying stream isn't a random access store, then fail early.
    // We could possibly succeed for the case where the seek position denotes
    // something that happens to be read into the buffer, but that would make
    // the failure data-dependent.
    nsresult rv;
    nsCOMPtr<nsISeekableStream> ras = do_QueryInterface(mStream, &rv);
    if (NS_FAILED(rv)) return rv;

    PRInt64 absPos = 0;
    switch (whence) {
      case nsISeekableStream::NS_SEEK_SET:
        absPos = offset;
        break;
      case nsISeekableStream::NS_SEEK_CUR:
        absPos = mBufferStartOffset;
        absPos += mCursor;
        absPos += offset;
        break;
      case nsISeekableStream::NS_SEEK_END:
        absPos = -1;
        break;
      default:
        NS_NOTREACHED("bogus seek whence parameter");
        return NS_ERROR_UNEXPECTED;
    }

    // Let mCursor point into the existing buffer if the new position is
    // between the current cursor and the mFillPoint "fencepost" -- the
    // client may never get around to a Read or Write after this Seek.
    // Read and Write worry about flushing and filling in that event.
    // But if we're at EOF, make sure to pass the seek through to the
    // underlying stream, because it may have auto-closed itself and
    // needs to reopen.
    PRUint32 offsetInBuffer = PRUint32(absPos - mBufferStartOffset);
    if (offsetInBuffer <= mFillPoint && !mEOF) {
        METER(bufstats.mSeeksWithinBuffer++);
        mCursor = offsetInBuffer;
        return NS_OK;
    }

    METER(bufstats.mSeeksOutsideBuffer++);
    METER(bufstats.mBufferReadUponSeek += mCursor);
    METER(bufstats.mBufferUnreadUponSeek += mFillPoint - mCursor);
    rv = Flush();
    if (NS_FAILED(rv)) return rv;

    rv = ras->Seek(whence, offset);
    if (NS_FAILED(rv)) return rv;

    mEOF = false;

    // Recompute whether the offset we're seeking to is in our buffer.
    // Note that we need to recompute because Flush() might have
    // changed mBufferStartOffset.
    offsetInBuffer = PRUint32(absPos - mBufferStartOffset);
    if (offsetInBuffer <= mFillPoint) {
        // It's safe to just set mCursor to offsetInBuffer.  In particular, we
        // want to avoid calling Fill() here since we already have the data that
        // was seeked to and calling Fill() might auto-close our underlying
        // stream in some cases.
        mCursor = offsetInBuffer;
        return NS_OK;
    }

    METER(if (bufstats.mBigSeekIndex < MAX_BIG_SEEKS)
              bufstats.mBigSeek[bufstats.mBigSeekIndex].mOldOffset =
                  mBufferStartOffset + PRInt64(mCursor));
    const PRInt64 minus1 = -1;
    if (absPos == minus1) {
        // then we had the SEEK_END case, above
        PRInt64 tellPos;
        rv = ras->Tell(&tellPos);
        mBufferStartOffset = tellPos;
        if (NS_FAILED(rv)) return rv;
    }
    else {
        mBufferStartOffset = absPos;
    }
    METER(if (bufstats.mBigSeekIndex < MAX_BIG_SEEKS)
              bufstats.mBigSeek[bufstats.mBigSeekIndex++].mNewOffset =
                  mBufferStartOffset);

    mFillPoint = mCursor = 0;
    return Fill();
}

NS_IMETHODIMP
nsBufferedStream::Tell(PRInt64 *result)
{
    if (mStream == nsnull)
        return NS_BASE_STREAM_CLOSED;
    
    PRInt64 result64 = mBufferStartOffset;
    result64 += mCursor;
    *result = result64;
    return NS_OK;
}

NS_IMETHODIMP
nsBufferedStream::SetEOF()
{
    if (mStream == nsnull)
        return NS_BASE_STREAM_CLOSED;
    
    nsresult rv;
    nsCOMPtr<nsISeekableStream> ras = do_QueryInterface(mStream, &rv);
    if (NS_FAILED(rv)) return rv;

    rv = ras->SetEOF();
    if (NS_SUCCEEDED(rv))
        mEOF = true;
    return rv;
}

////////////////////////////////////////////////////////////////////////////////
// nsBufferedInputStream

NS_IMPL_ADDREF_INHERITED(nsBufferedInputStream, nsBufferedStream)
NS_IMPL_RELEASE_INHERITED(nsBufferedInputStream, nsBufferedStream)

NS_IMPL_CLASSINFO(nsBufferedInputStream, NULL, nsIClassInfo::THREADSAFE,
                  NS_BUFFEREDINPUTSTREAM_CID)

NS_INTERFACE_MAP_BEGIN(nsBufferedInputStream)
    NS_INTERFACE_MAP_ENTRY(nsIInputStream)
    NS_INTERFACE_MAP_ENTRY(nsIBufferedInputStream)
    NS_INTERFACE_MAP_ENTRY(nsIStreamBufferAccess)
    NS_INTERFACE_MAP_ENTRY(nsIIPCSerializable)
    NS_IMPL_QUERY_CLASSINFO(nsBufferedInputStream)
NS_INTERFACE_MAP_END_INHERITING(nsBufferedStream)

NS_IMPL_CI_INTERFACE_GETTER5(nsBufferedInputStream,
                             nsIInputStream,
                             nsIBufferedInputStream,
                             nsISeekableStream,
                             nsIStreamBufferAccess,
                             nsIIPCSerializable)

nsresult
nsBufferedInputStream::Create(nsISupports *aOuter, REFNSIID aIID, void **aResult)
{
    NS_ENSURE_NO_AGGREGATION(aOuter);

    nsBufferedInputStream* stream = new nsBufferedInputStream();
    if (stream == nsnull)
        return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(stream);
    nsresult rv = stream->QueryInterface(aIID, aResult);
    NS_RELEASE(stream);
    return rv;
}

NS_IMETHODIMP
nsBufferedInputStream::Init(nsIInputStream* stream, PRUint32 bufferSize)
{
    return nsBufferedStream::Init(stream, bufferSize);
}

NS_IMETHODIMP
nsBufferedInputStream::Close()
{
    nsresult rv1 = NS_OK, rv2;
    if (mStream) {
        rv1 = Source()->Close();
        NS_RELEASE(mStream);
    }
    rv2 = nsBufferedStream::Close();
    if (NS_FAILED(rv1)) return rv1;
    return rv2;
}

NS_IMETHODIMP
nsBufferedInputStream::Available(PRUint32 *result)
{
    nsresult rv = NS_OK;
    *result = 0;
    if (mStream) {
        rv = Source()->Available(result);
    }
    *result += (mFillPoint - mCursor);
    return rv;
}

NS_IMETHODIMP
nsBufferedInputStream::Read(char * buf, PRUint32 count, PRUint32 *result)
{
    if (mBufferDisabled) {
        if (!mStream) {
            *result = 0;
            return NS_OK;
        }
        nsresult rv = Source()->Read(buf, count, result);
        if (NS_SUCCEEDED(rv)) {
            mBufferStartOffset += *result;  // so nsBufferedStream::Tell works
            if (*result == 0) {
                mEOF = true;
            }
        }
        return rv;
    }

    return ReadSegments(NS_CopySegmentToBuffer, buf, count, result);
}

NS_IMETHODIMP
nsBufferedInputStream::ReadSegments(nsWriteSegmentFun writer, void *closure,
                                    PRUint32 count, PRUint32 *result)
{
    *result = 0;

    if (!mStream)
        return NS_OK;

    nsresult rv = NS_OK;
    while (count > 0) {
        PRUint32 amt = NS_MIN(count, mFillPoint - mCursor);
        if (amt > 0) {
            PRUint32 read = 0;
            rv = writer(this, closure, mBuffer + mCursor, *result, amt, &read);
            if (NS_FAILED(rv)) {
                // errors returned from the writer end here!
                rv = NS_OK;
                break;
            }
            *result += read;
            count -= read;
            mCursor += read;
        }
        else {
            rv = Fill();
            if (NS_FAILED(rv) || mFillPoint == mCursor)
                break;
        }
    }
    return (*result > 0) ? NS_OK : rv;
}

NS_IMETHODIMP
nsBufferedInputStream::IsNonBlocking(bool *aNonBlocking)
{
    if (mStream)
        return Source()->IsNonBlocking(aNonBlocking);
    return NS_ERROR_NOT_INITIALIZED;
}

NS_IMETHODIMP
nsBufferedInputStream::Fill()
{
    if (mBufferDisabled)
        return NS_OK;
    NS_ENSURE_TRUE(mStream, NS_ERROR_NOT_INITIALIZED);

    nsresult rv;
    PRInt32 rem = PRInt32(mFillPoint - mCursor);
    if (rem > 0) {
        // slide the remainder down to the start of the buffer
        // |<------------->|<--rem-->|<--->|
        // b               c         f     s
        memcpy(mBuffer, mBuffer + mCursor, rem);
    }
    mBufferStartOffset += mCursor;
    mFillPoint = rem;
    mCursor = 0;

    PRUint32 amt;
    rv = Source()->Read(mBuffer + mFillPoint, mBufferSize - mFillPoint, &amt);
    if (NS_FAILED(rv)) return rv;

    if (amt == 0)
        mEOF = true;
    
    mFillPoint += amt;
    return NS_OK;
}

NS_IMETHODIMP_(char*)
nsBufferedInputStream::GetBuffer(PRUint32 aLength, PRUint32 aAlignMask)
{
    NS_ASSERTION(mGetBufferCount == 0, "nested GetBuffer!");
    if (mGetBufferCount != 0)
        return nsnull;

    if (mBufferDisabled)
        return nsnull;

    char* buf = mBuffer + mCursor;
    PRUint32 rem = mFillPoint - mCursor;
    if (rem == 0) {
        if (NS_FAILED(Fill()))
            return nsnull;
        buf = mBuffer + mCursor;
        rem = mFillPoint - mCursor;
    }

    PRUint32 mod = (NS_PTR_TO_INT32(buf) & aAlignMask);
    if (mod) {
        PRUint32 pad = aAlignMask + 1 - mod;
        if (pad > rem)
            return nsnull;

        memset(buf, 0, pad);
        mCursor += pad;
        buf += pad;
        rem -= pad;
    }

    if (aLength > rem)
        return nsnull;
    mGetBufferCount++;
    return buf;
}

NS_IMETHODIMP_(void)
nsBufferedInputStream::PutBuffer(char* aBuffer, PRUint32 aLength)
{
    NS_ASSERTION(mGetBufferCount == 1, "stray PutBuffer!");
    if (--mGetBufferCount != 0)
        return;

    NS_ASSERTION(mCursor + aLength <= mFillPoint, "PutBuffer botch");
    mCursor += aLength;
}

NS_IMETHODIMP
nsBufferedInputStream::DisableBuffering()
{
    NS_ASSERTION(!mBufferDisabled, "redundant call to DisableBuffering!");
    NS_ASSERTION(mGetBufferCount == 0,
                 "DisableBuffer call between GetBuffer and PutBuffer!");
    if (mGetBufferCount != 0)
        return NS_ERROR_UNEXPECTED;

    // Empty the buffer so nsBufferedStream::Tell works.
    mBufferStartOffset += mCursor;
    mFillPoint = mCursor = 0;
    mBufferDisabled = true;
    return NS_OK;
}

NS_IMETHODIMP
nsBufferedInputStream::EnableBuffering()
{
    NS_ASSERTION(mBufferDisabled, "gratuitous call to EnableBuffering!");
    mBufferDisabled = false;
    return NS_OK;
}

NS_IMETHODIMP
nsBufferedInputStream::GetUnbufferedStream(nsISupports* *aStream)
{
    // Empty the buffer so subsequent i/o trumps any buffered data.
    mBufferStartOffset += mCursor;
    mFillPoint = mCursor = 0;

    *aStream = mStream;
    NS_IF_ADDREF(*aStream);
    return NS_OK;
}

bool
nsBufferedInputStream::Read(const IPC::Message *aMsg, void **aIter)
{
    using IPC::ReadParam;

    PRUint32 bufferSize;
    IPC::InputStream inputStream;
    if (!ReadParam(aMsg, aIter, &bufferSize) ||
        !ReadParam(aMsg, aIter, &inputStream))
        return false;

    nsCOMPtr<nsIInputStream> stream(inputStream);
    nsresult rv = Init(stream, bufferSize);
    if (NS_FAILED(rv))
        return false;

    return true;
}

void
nsBufferedInputStream::Write(IPC::Message *aMsg)
{
    using IPC::WriteParam;

    WriteParam(aMsg, mBufferSize);

    IPC::InputStream inputStream(Source());
    WriteParam(aMsg, inputStream);
}

////////////////////////////////////////////////////////////////////////////////
// nsBufferedOutputStream

NS_IMPL_ADDREF_INHERITED(nsBufferedOutputStream, nsBufferedStream)
NS_IMPL_RELEASE_INHERITED(nsBufferedOutputStream, nsBufferedStream)
// This QI uses NS_INTERFACE_MAP_ENTRY_CONDITIONAL to check for
// non-nullness of mSafeStream.
NS_INTERFACE_MAP_BEGIN(nsBufferedOutputStream)
    NS_INTERFACE_MAP_ENTRY(nsIOutputStream)
    NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsISafeOutputStream, mSafeStream)
    NS_INTERFACE_MAP_ENTRY(nsIBufferedOutputStream)
    NS_INTERFACE_MAP_ENTRY(nsIStreamBufferAccess)
NS_INTERFACE_MAP_END_INHERITING(nsBufferedStream)

nsresult
nsBufferedOutputStream::Create(nsISupports *aOuter, REFNSIID aIID, void **aResult)
{
    NS_ENSURE_NO_AGGREGATION(aOuter);

    nsBufferedOutputStream* stream = new nsBufferedOutputStream();
    if (stream == nsnull)
        return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(stream);
    nsresult rv = stream->QueryInterface(aIID, aResult);
    NS_RELEASE(stream);
    return rv;
}

NS_IMETHODIMP
nsBufferedOutputStream::Init(nsIOutputStream* stream, PRUint32 bufferSize)
{
    // QI stream to an nsISafeOutputStream, to see if we should support it
    mSafeStream = do_QueryInterface(stream);

    return nsBufferedStream::Init(stream, bufferSize);
}

NS_IMETHODIMP
nsBufferedOutputStream::Close()
{
    nsresult rv1, rv2 = NS_OK, rv3;
    rv1 = Flush();
    // If we fail to Flush all the data, then we close anyway and drop the
    // remaining data in the buffer. We do this because it's what Unix does
    // for fclose and close. However, we report the error from Flush anyway.
    if (mStream) {
        rv2 = Sink()->Close();
        NS_RELEASE(mStream);
    }
    rv3 = nsBufferedStream::Close();
    if (NS_FAILED(rv1)) return rv1;
    if (NS_FAILED(rv2)) return rv2;
    return rv3;
}

NS_IMETHODIMP
nsBufferedOutputStream::Write(const char *buf, PRUint32 count, PRUint32 *result)
{
    nsresult rv = NS_OK;
    PRUint32 written = 0;
    while (count > 0) {
        PRUint32 amt = NS_MIN(count, mBufferSize - mCursor);
        if (amt > 0) {
            memcpy(mBuffer + mCursor, buf + written, amt);
            written += amt;
            count -= amt;
            mCursor += amt;
            if (mFillPoint < mCursor)
                mFillPoint = mCursor;
        }
        else {
            NS_ASSERTION(mFillPoint, "iloop in nsBufferedOutputStream::Write!");
            rv = Flush();
            if (NS_FAILED(rv)) break;
        }
    }
    *result = written;
    return (written > 0) ? NS_OK : rv;
}

NS_IMETHODIMP
nsBufferedOutputStream::Flush()
{
    nsresult rv;
    PRUint32 amt;
    if (!mStream) {
        // Stream already cancelled/flushed; probably because of error.
        return NS_OK;
    }
    rv = Sink()->Write(mBuffer, mFillPoint, &amt);
    if (NS_FAILED(rv)) return rv;
    mBufferStartOffset += amt;
    if (amt == mFillPoint) {
        mFillPoint = mCursor = 0;
        return NS_OK;   // flushed everything
    }

    // slide the remainder down to the start of the buffer
    // |<-------------->|<---|----->|
    // b                a    c      s
    PRUint32 rem = mFillPoint - amt;
    memcpy(mBuffer, mBuffer + amt, rem);
    mFillPoint = mCursor = rem;
    return NS_ERROR_FAILURE;        // didn't flush all
}

// nsISafeOutputStream
NS_IMETHODIMP
nsBufferedOutputStream::Finish()
{
    // flush the stream, to write out any buffered data...
    nsresult rv = nsBufferedOutputStream::Flush();
    if (NS_FAILED(rv))
        NS_WARNING("failed to flush buffered data! possible dataloss");

    // ... and finish the underlying stream...
    if (NS_SUCCEEDED(rv))
        rv = mSafeStream->Finish();
    else
        Sink()->Close();

    // ... and close the buffered stream, so any further attempts to flush/close
    // the buffered stream won't cause errors.
    nsBufferedStream::Close();

    return rv;
}

static NS_METHOD
nsReadFromInputStream(nsIOutputStream* outStr,
                      void* closure,
                      char* toRawSegment, 
                      PRUint32 offset,
                      PRUint32 count,
                      PRUint32 *readCount)
{
    nsIInputStream* fromStream = (nsIInputStream*)closure;
    return fromStream->Read(toRawSegment, count, readCount);
}

NS_IMETHODIMP
nsBufferedOutputStream::WriteFrom(nsIInputStream *inStr, PRUint32 count, PRUint32 *_retval)
{
    return WriteSegments(nsReadFromInputStream, inStr, count, _retval);
}

NS_IMETHODIMP
nsBufferedOutputStream::WriteSegments(nsReadSegmentFun reader, void * closure, PRUint32 count, PRUint32 *_retval)
{
    *_retval = 0;
    nsresult rv;
    while (count > 0) {
        PRUint32 left = NS_MIN(count, mBufferSize - mCursor);
        if (left == 0) {
            rv = Flush();
            if (NS_FAILED(rv))
              return rv;

            continue;
        }

        PRUint32 read = 0;
        rv = reader(this, closure, mBuffer + mCursor, *_retval, left, &read);

        if (NS_FAILED(rv)) // If we have written some data, return ok
            return (*_retval > 0) ? NS_OK : rv;
        mCursor += read;
        *_retval += read;
        count -= read;
        mFillPoint = NS_MAX(mFillPoint, mCursor);
    }
    return NS_OK;
}

NS_IMETHODIMP
nsBufferedOutputStream::IsNonBlocking(bool *aNonBlocking)
{
    if (mStream)
        return Sink()->IsNonBlocking(aNonBlocking);
    return NS_ERROR_NOT_INITIALIZED;
}

NS_IMETHODIMP_(char*)
nsBufferedOutputStream::GetBuffer(PRUint32 aLength, PRUint32 aAlignMask)
{
    NS_ASSERTION(mGetBufferCount == 0, "nested GetBuffer!");
    if (mGetBufferCount != 0)
        return nsnull;

    if (mBufferDisabled)
        return nsnull;

    char* buf = mBuffer + mCursor;
    PRUint32 rem = mBufferSize - mCursor;
    if (rem == 0) {
        if (NS_FAILED(Flush()))
            return nsnull;
        buf = mBuffer + mCursor;
        rem = mBufferSize - mCursor;
    }

    PRUint32 mod = (NS_PTR_TO_INT32(buf) & aAlignMask);
    if (mod) {
        PRUint32 pad = aAlignMask + 1 - mod;
        if (pad > rem)
            return nsnull;

        memset(buf, 0, pad);
        mCursor += pad;
        buf += pad;
        rem -= pad;
    }

    if (aLength > rem)
        return nsnull;
    mGetBufferCount++;
    return buf;
}

NS_IMETHODIMP_(void)
nsBufferedOutputStream::PutBuffer(char* aBuffer, PRUint32 aLength)
{
    NS_ASSERTION(mGetBufferCount == 1, "stray PutBuffer!");
    if (--mGetBufferCount != 0)
        return;

    NS_ASSERTION(mCursor + aLength <= mBufferSize, "PutBuffer botch");
    mCursor += aLength;
    if (mFillPoint < mCursor)
        mFillPoint = mCursor;
}

NS_IMETHODIMP
nsBufferedOutputStream::DisableBuffering()
{
    NS_ASSERTION(!mBufferDisabled, "redundant call to DisableBuffering!");
    NS_ASSERTION(mGetBufferCount == 0,
                 "DisableBuffer call between GetBuffer and PutBuffer!");
    if (mGetBufferCount != 0)
        return NS_ERROR_UNEXPECTED;

    // Empty the buffer so nsBufferedStream::Tell works.
    nsresult rv = Flush();
    if (NS_FAILED(rv))
        return rv;

    mBufferDisabled = true;
    return NS_OK;
}

NS_IMETHODIMP
nsBufferedOutputStream::EnableBuffering()
{
    NS_ASSERTION(mBufferDisabled, "gratuitous call to EnableBuffering!");
    mBufferDisabled = false;
    return NS_OK;
}

NS_IMETHODIMP
nsBufferedOutputStream::GetUnbufferedStream(nsISupports* *aStream)
{
    // Empty the buffer so subsequent i/o trumps any buffered data.
    if (mFillPoint) {
        nsresult rv = Flush();
        if (NS_FAILED(rv))
            return rv;
    }

    *aStream = mStream;
    NS_IF_ADDREF(*aStream);
    return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
