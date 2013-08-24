/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * The MIME stream separates headers and a datastream. It also allows
 * automatic creation of the content-length header.
 */

#include "IPC/IPCMessageUtils.h"
#include "mozilla/net/NeckoMessageUtils.h"

#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsIMultiplexInputStream.h"
#include "nsIMIMEInputStream.h"
#include "nsISeekableStream.h"
#include "nsIStringStream.h"
#include "nsString.h"
#include "nsMIMEInputStream.h"
#include "nsIIPCSerializable.h"
#include "nsIClassInfoImpl.h"

class nsMIMEInputStream : public nsIMIMEInputStream,
                          public nsISeekableStream,
                          public nsIIPCSerializable
{
public:
    nsMIMEInputStream();
    virtual ~nsMIMEInputStream();

    NS_DECL_ISUPPORTS
    NS_DECL_NSIINPUTSTREAM
    NS_DECL_NSIMIMEINPUTSTREAM
    NS_DECL_NSISEEKABLESTREAM
    NS_DECL_NSIIPCSERIALIZABLE
    
    NS_METHOD Init();

private:

    void InitStreams();

    struct ReadSegmentsState {
        nsIInputStream* mThisStream;
        nsWriteSegmentFun mWriter;
        void* mClosure;
    };
    static NS_METHOD ReadSegCb(nsIInputStream* aIn, void* aClosure,
                               const char* aFromRawSegment, PRUint32 aToOffset,
                               PRUint32 aCount, PRUint32 *aWriteCount);

    nsCString mHeaders;
    nsCOMPtr<nsIStringInputStream> mHeaderStream;
    
    nsCString mContentLength;
    nsCOMPtr<nsIStringInputStream> mCLStream;
    
    nsCOMPtr<nsIInputStream> mData;
    nsCOMPtr<nsIMultiplexInputStream> mStream;
    bool mAddContentLength;
    bool mStartedReading;
};

NS_IMPL_THREADSAFE_ADDREF(nsMIMEInputStream)
NS_IMPL_THREADSAFE_RELEASE(nsMIMEInputStream)

NS_IMPL_CLASSINFO(nsMIMEInputStream, NULL, nsIClassInfo::THREADSAFE,
                  NS_MIMEINPUTSTREAM_CID)

NS_IMPL_QUERY_INTERFACE4_CI(nsMIMEInputStream,
                            nsIMIMEInputStream,
                            nsIInputStream,
                            nsISeekableStream,
                            nsIIPCSerializable)
NS_IMPL_CI_INTERFACE_GETTER4(nsMIMEInputStream,
                             nsIMIMEInputStream,
                             nsIInputStream,
                             nsISeekableStream,
                             nsIIPCSerializable)

nsMIMEInputStream::nsMIMEInputStream() : mAddContentLength(false),
                                         mStartedReading(false)
{
}

nsMIMEInputStream::~nsMIMEInputStream()
{
}

NS_METHOD nsMIMEInputStream::Init()
{
    nsresult rv = NS_OK;
    mStream = do_CreateInstance("@mozilla.org/io/multiplex-input-stream;1",
                                &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    mHeaderStream = do_CreateInstance("@mozilla.org/io/string-input-stream;1",
                                      &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    mCLStream = do_CreateInstance("@mozilla.org/io/string-input-stream;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mStream->AppendStream(mHeaderStream);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = mStream->AppendStream(mCLStream);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
}


/* attribute boolean addContentLength; */
NS_IMETHODIMP
nsMIMEInputStream::GetAddContentLength(bool *aAddContentLength)
{
    *aAddContentLength = mAddContentLength;
    return NS_OK;
}
NS_IMETHODIMP
nsMIMEInputStream::SetAddContentLength(bool aAddContentLength)
{
    NS_ENSURE_FALSE(mStartedReading, NS_ERROR_FAILURE);
    mAddContentLength = aAddContentLength;
    return NS_OK;
}

/* void addHeader ([const] in string name, [const] in string value); */
NS_IMETHODIMP
nsMIMEInputStream::AddHeader(const char *aName, const char *aValue)
{
    NS_ENSURE_FALSE(mStartedReading, NS_ERROR_FAILURE);
    mHeaders.Append(aName);
    mHeaders.AppendLiteral(": ");
    mHeaders.Append(aValue);
    mHeaders.AppendLiteral("\r\n");

    // Just in case someone somehow uses our stream, lets at least
    // let the stream have a valid pointer. The stream will be properly
    // initialized in nsMIMEInputStream::InitStreams
    mHeaderStream->ShareData(mHeaders.get(), 0);

    return NS_OK;
}

/* void setData (in nsIInputStream stream); */
NS_IMETHODIMP
nsMIMEInputStream::SetData(nsIInputStream *aStream)
{
    NS_ENSURE_FALSE(mStartedReading, NS_ERROR_FAILURE);
    // Remove the old stream if there is one
    if (mData)
        mStream->RemoveStream(2);

    mData = aStream;
    if (aStream)
        mStream->AppendStream(mData);
    return NS_OK;
}

// set up the internal streams
void nsMIMEInputStream::InitStreams()
{
    NS_ASSERTION(!mStartedReading,
                 "Don't call initStreams twice without rewinding");

    mStartedReading = true;

    // We'll use the content-length stream to add the final \r\n
    if (mAddContentLength) {
        PRUint32 cl = 0;
        if (mData) {
            mData->Available(&cl);
        }
        mContentLength.AssignLiteral("Content-Length: ");
        mContentLength.AppendInt((PRInt32)cl);
        mContentLength.AppendLiteral("\r\n\r\n");
    }
    else {
        mContentLength.AssignLiteral("\r\n");
    }
    mCLStream->ShareData(mContentLength.get(), -1);
    mHeaderStream->ShareData(mHeaders.get(), -1);
}



#define INITSTREAMS         \
if (!mStartedReading) {     \
    InitStreams();          \
}

// Reset mStartedReading when Seek-ing to start
NS_IMETHODIMP
nsMIMEInputStream::Seek(PRInt32 whence, PRInt64 offset)
{
    nsresult rv;
    nsCOMPtr<nsISeekableStream> stream = do_QueryInterface(mStream);
    if (whence == NS_SEEK_SET && LL_EQ(offset, LL_Zero())) {
        rv = stream->Seek(whence, offset);
        if (NS_SUCCEEDED(rv))
            mStartedReading = false;
    }
    else {
        INITSTREAMS;
        rv = stream->Seek(whence, offset);
    }

    return rv;
}

// Proxy ReadSegments since we need to be a good little nsIInputStream
NS_IMETHODIMP nsMIMEInputStream::ReadSegments(nsWriteSegmentFun aWriter,
                                              void *aClosure, PRUint32 aCount,
                                              PRUint32 *_retval)
{
    INITSTREAMS;
    ReadSegmentsState state;
    state.mThisStream = this;
    state.mWriter = aWriter;
    state.mClosure = aClosure;
    return mStream->ReadSegments(ReadSegCb, &state, aCount, _retval);
}

NS_METHOD
nsMIMEInputStream::ReadSegCb(nsIInputStream* aIn, void* aClosure,
                             const char* aFromRawSegment,
                             PRUint32 aToOffset, PRUint32 aCount,
                             PRUint32 *aWriteCount)
{
    ReadSegmentsState* state = (ReadSegmentsState*)aClosure;
    return  (state->mWriter)(state->mThisStream,
                             state->mClosure,
                             aFromRawSegment,
                             aToOffset,
                             aCount,
                             aWriteCount);
}

/**
 * Forward everything else to the mStream after calling InitStreams()
 */

// nsIInputStream
NS_IMETHODIMP nsMIMEInputStream::Close(void) { INITSTREAMS; return mStream->Close(); }
NS_IMETHODIMP nsMIMEInputStream::Available(PRUint32 *_retval) { INITSTREAMS; return mStream->Available(_retval); }
NS_IMETHODIMP nsMIMEInputStream::Read(char * buf, PRUint32 count, PRUint32 *_retval) { INITSTREAMS; return mStream->Read(buf, count, _retval); }
NS_IMETHODIMP nsMIMEInputStream::IsNonBlocking(bool *aNonBlocking) { INITSTREAMS; return mStream->IsNonBlocking(aNonBlocking); }

// nsISeekableStream
NS_IMETHODIMP nsMIMEInputStream::Tell(PRInt64 *_retval)
{
    INITSTREAMS;
    nsCOMPtr<nsISeekableStream> stream = do_QueryInterface(mStream);
    return stream->Tell(_retval);
}
NS_IMETHODIMP nsMIMEInputStream::SetEOF(void) {
    INITSTREAMS;
    nsCOMPtr<nsISeekableStream> stream = do_QueryInterface(mStream);
    return stream->SetEOF();
}


/**
 * Factory method used by do_CreateInstance
 */

nsresult
nsMIMEInputStreamConstructor(nsISupports *outer, REFNSIID iid, void **result)
{
    *result = nsnull;

    if (outer)
        return NS_ERROR_NO_AGGREGATION;

    nsMIMEInputStream *inst = new nsMIMEInputStream();
    if (!inst)
        return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(inst);

    nsresult rv = inst->Init();
    if (NS_FAILED(rv)) {
        NS_RELEASE(inst);
        return rv;
    }

    rv = inst->QueryInterface(iid, result);
    NS_RELEASE(inst);

    return rv;
}

bool
nsMIMEInputStream::Read(const IPC::Message *aMsg, void **aIter)
{
    using IPC::ReadParam;

    if (!ReadParam(aMsg, aIter, &mHeaders) ||
        !ReadParam(aMsg, aIter, &mContentLength) ||
        !ReadParam(aMsg, aIter, &mStartedReading))
        return false;

    // nsMIMEInputStream::Init() already appended mHeaderStream & mCLStream
    mHeaderStream->ShareData(mHeaders.get(),
                             mStartedReading? mHeaders.Length() : 0);
    mCLStream->ShareData(mContentLength.get(),
                         mStartedReading? mContentLength.Length() : 0);

    IPC::InputStream inputStream;
    if (!ReadParam(aMsg, aIter, &inputStream))
        return false;

    nsCOMPtr<nsIInputStream> stream(inputStream);
    mData = stream;
    if (stream) {
        nsresult rv = mStream->AppendStream(mData);
        if (NS_FAILED(rv))
            return false;
    }

    if (!ReadParam(aMsg, aIter, &mAddContentLength))
        return false;

    return true;
}

void
nsMIMEInputStream::Write(IPC::Message *aMsg)
{
    using IPC::WriteParam;

    WriteParam(aMsg, mHeaders);
    WriteParam(aMsg, mContentLength);
    WriteParam(aMsg, mStartedReading);

    IPC::InputStream inputStream(mData);
    WriteParam(aMsg, inputStream);

    WriteParam(aMsg, mAddContentLength);
}
