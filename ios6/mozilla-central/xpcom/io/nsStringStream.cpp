/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sts=4 sw=4 cin et: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Based on original code from nsIStringStream.cpp
 */

#include "ipc/IPCMessageUtils.h"

#include "nsStringStream.h"
#include "nsStreamUtils.h"
#include "nsReadableUtils.h"
#include "nsISeekableStream.h"
#include "nsISupportsPrimitives.h"
#include "nsCRT.h"
#include "prerror.h"
#include "plstr.h"
#include "nsIClassInfoImpl.h"
#include "mozilla/Attributes.h"
#include "mozilla/ipc/InputStreamUtils.h"
#include "nsIIPCSerializableInputStream.h"

using namespace mozilla::ipc;

//-----------------------------------------------------------------------------
// nsIStringInputStream implementation
//-----------------------------------------------------------------------------

class nsStringInputStream MOZ_FINAL : public nsIStringInputStream
                                    , public nsISeekableStream
                                    , public nsISupportsCString
                                    , public nsIIPCSerializableInputStream
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIINPUTSTREAM
    NS_DECL_NSISTRINGINPUTSTREAM
    NS_DECL_NSISEEKABLESTREAM
    NS_DECL_NSISUPPORTSPRIMITIVE
    NS_DECL_NSISUPPORTSCSTRING
    NS_DECL_NSIIPCSERIALIZABLEINPUTSTREAM

    nsStringInputStream()
    {
        Clear();
    }

private:
    ~nsStringInputStream()
    {}

    uint32_t Length() const
    {
        return mData.Length();
    }

    uint32_t LengthRemaining() const
    {
        return Length() - mOffset;
    }

    void Clear()
    {
        mData.SetIsVoid(true);
    }

    bool Closed()
    {
        return mData.IsVoid();
    }

    nsDependentCSubstring mData;
    uint32_t mOffset;
};

// This class needs to support threadsafe refcounting since people often
// allocate a string stream, and then read it from a background thread.
NS_IMPL_THREADSAFE_ADDREF(nsStringInputStream)
NS_IMPL_THREADSAFE_RELEASE(nsStringInputStream)

NS_IMPL_CLASSINFO(nsStringInputStream, NULL, nsIClassInfo::THREADSAFE,
                  NS_STRINGINPUTSTREAM_CID)
NS_IMPL_QUERY_INTERFACE5_CI(nsStringInputStream,
                            nsIStringInputStream,
                            nsIInputStream,
                            nsISupportsCString,
                            nsISeekableStream,
                            nsIIPCSerializableInputStream)
NS_IMPL_CI_INTERFACE_GETTER4(nsStringInputStream,
                             nsIStringInputStream,
                             nsIInputStream,
                             nsISupportsCString,
                             nsISeekableStream)

/////////
// nsISupportsCString implementation
/////////

NS_IMETHODIMP
nsStringInputStream::GetType(uint16_t *type)
{
    *type = TYPE_CSTRING;
    return NS_OK;
}

NS_IMETHODIMP
nsStringInputStream::GetData(nsACString &data)
{
    // The stream doesn't have any data when it is closed.  We could fake it
    // and return an empty string here, but it seems better to keep this return
    // value consistent with the behavior of the other 'getter' methods.
    NS_ENSURE_TRUE(!Closed(), NS_BASE_STREAM_CLOSED);

    data.Assign(mData);
    return NS_OK;
}

NS_IMETHODIMP
nsStringInputStream::SetData(const nsACString &data)
{
    mData.Assign(data);
    mOffset = 0;
    return NS_OK;
}

NS_IMETHODIMP
nsStringInputStream::ToString(char **result)
{
    // NOTE: This method may result in data loss, so we do not implement it.
    return NS_ERROR_NOT_IMPLEMENTED;
}

/////////
// nsIStringInputStream implementation
/////////

NS_IMETHODIMP
nsStringInputStream::SetData(const char *data, int32_t dataLen)
{
    NS_ENSURE_ARG_POINTER(data);
    mData.Assign(data, dataLen);
    mOffset = 0;
    return NS_OK;
}

NS_IMETHODIMP
nsStringInputStream::AdoptData(char *data, int32_t dataLen)
{
    NS_ENSURE_ARG_POINTER(data);
    mData.Adopt(data, dataLen);
    mOffset = 0;
    return NS_OK;
}

NS_IMETHODIMP
nsStringInputStream::ShareData(const char *data, int32_t dataLen)
{
    NS_ENSURE_ARG_POINTER(data);

    if (dataLen < 0)
        dataLen = strlen(data);

    mData.Rebind(data, dataLen);
    mOffset = 0;
    return NS_OK;
}

/////////
// nsIInputStream implementation
/////////

NS_IMETHODIMP
nsStringInputStream::Close()
{
    Clear();
    return NS_OK;
}
    
NS_IMETHODIMP
nsStringInputStream::Available(uint64_t *aLength)
{
    NS_ASSERTION(aLength, "null ptr");

    if (Closed())
        return NS_BASE_STREAM_CLOSED;

    *aLength = LengthRemaining();
    return NS_OK;
}

NS_IMETHODIMP
nsStringInputStream::Read(char* aBuf, uint32_t aCount, uint32_t *aReadCount)
{
    NS_ASSERTION(aBuf, "null ptr");
    return ReadSegments(NS_CopySegmentToBuffer, aBuf, aCount, aReadCount);
}

NS_IMETHODIMP
nsStringInputStream::ReadSegments(nsWriteSegmentFun writer, void *closure,
                                  uint32_t aCount, uint32_t *result)
{
    NS_ASSERTION(result, "null ptr");
    NS_ASSERTION(Length() >= mOffset, "bad stream state");

    if (Closed())
        return NS_BASE_STREAM_CLOSED;

    // We may be at end-of-file
    uint32_t maxCount = LengthRemaining();
    if (maxCount == 0) {
        *result = 0;
        return NS_OK;
    }

    if (aCount > maxCount)
        aCount = maxCount;
    nsresult rv = writer(this, closure, mData.BeginReading() + mOffset, 0, aCount, result);
    if (NS_SUCCEEDED(rv)) {
        NS_ASSERTION(*result <= aCount,
                     "writer should not write more than we asked it to write");
        mOffset += *result;
    }

    // errors returned from the writer end here!
    return NS_OK;
}
    
NS_IMETHODIMP
nsStringInputStream::IsNonBlocking(bool *aNonBlocking)
{
    *aNonBlocking = true;
    return NS_OK;
}

/////////
// nsISeekableStream implementation
/////////

NS_IMETHODIMP 
nsStringInputStream::Seek(int32_t whence, int64_t offset)
{
    if (Closed())
        return NS_BASE_STREAM_CLOSED;

    // Compute new stream position.  The given offset may be a negative value.
 
    int64_t newPos = offset;
    switch (whence) {
    case NS_SEEK_SET:
        break;
    case NS_SEEK_CUR:
        newPos += mOffset;
        break;
    case NS_SEEK_END:
        newPos += Length();
        break;
    default:
        NS_ERROR("invalid whence");
        return NS_ERROR_INVALID_ARG;
    }

    NS_ENSURE_ARG(newPos >= 0);
    NS_ENSURE_ARG(newPos <= Length());

    mOffset = (uint32_t)newPos;
    return NS_OK;
}

NS_IMETHODIMP
nsStringInputStream::Tell(int64_t* outWhere)
{
    if (Closed())
        return NS_BASE_STREAM_CLOSED;

    *outWhere = mOffset;
    return NS_OK;
}

NS_IMETHODIMP
nsStringInputStream::SetEOF()
{
    if (Closed())
        return NS_BASE_STREAM_CLOSED;

    mOffset = Length();
    return NS_OK;
}

void
nsStringInputStream::Serialize(InputStreamParams& aParams)
{
    StringInputStreamParams params;
    params.data() = PromiseFlatCString(mData);
    aParams = params;
}

bool
nsStringInputStream::Deserialize(const InputStreamParams& aParams)
{
    if (aParams.type() != InputStreamParams::TStringInputStreamParams) {
        NS_ERROR("Received unknown parameters from the other process!");
        return false;
    }

    const StringInputStreamParams& params =
        aParams.get_StringInputStreamParams();

    if (NS_FAILED(SetData(params.data()))) {
        NS_WARNING("SetData failed!");
        return false;
    }

    return true;
}

nsresult
NS_NewByteInputStream(nsIInputStream** aStreamResult,
                      const char* aStringToRead, int32_t aLength,
                      nsAssignmentType aAssignment)
{
    NS_PRECONDITION(aStreamResult, "null out ptr");

    nsStringInputStream* stream = new nsStringInputStream();
    if (! stream)
        return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(stream);

    nsresult rv;
    switch (aAssignment) {
    case NS_ASSIGNMENT_COPY:
        rv = stream->SetData(aStringToRead, aLength);
        break;
    case NS_ASSIGNMENT_DEPEND:
        rv = stream->ShareData(aStringToRead, aLength);
        break;
    case NS_ASSIGNMENT_ADOPT:
        rv = stream->AdoptData(const_cast<char*>(aStringToRead), aLength);
        break;
    default:
        NS_ERROR("invalid assignment type");
        rv = NS_ERROR_INVALID_ARG;
    }
    
    if (NS_FAILED(rv)) {
        NS_RELEASE(stream);
        return rv;
    }
    
    *aStreamResult = stream;
    return NS_OK;
}

nsresult
NS_NewStringInputStream(nsIInputStream** aStreamResult,
                        const nsAString& aStringToRead)
{
    NS_LossyConvertUTF16toASCII data(aStringToRead); // truncates high-order bytes
    return NS_NewCStringInputStream(aStreamResult, data);
}

nsresult
NS_NewCStringInputStream(nsIInputStream** aStreamResult,
                         const nsACString& aStringToRead)
{
    NS_PRECONDITION(aStreamResult, "null out ptr");

    nsStringInputStream* stream = new nsStringInputStream();
    if (! stream)
        return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(stream);

    stream->SetData(aStringToRead);

    *aStreamResult = stream;
    return NS_OK;
}

// factory method for constructing a nsStringInputStream object
nsresult
nsStringInputStreamConstructor(nsISupports *outer, REFNSIID iid, void **result)
{
    *result = nullptr;

    NS_ENSURE_TRUE(!outer, NS_ERROR_NO_AGGREGATION);

    nsStringInputStream *inst = new nsStringInputStream();
    if (!inst)
        return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(inst);
    nsresult rv = inst->QueryInterface(iid, result);
    NS_RELEASE(inst);

    return rv;
}
