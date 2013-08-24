/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCacheMetaData.h"
#include "nsICacheEntryDescriptor.h"
#include "prmem.h"

const char *
nsCacheMetaData::GetElement(const char * key)
{
    const char * data = mBuffer;
    const char * limit = mBuffer + mMetaSize;

    while (data < limit) {
        // Point to the value part
        const char * value = data + strlen(data) + 1;
        NS_ABORT_IF_FALSE(value < limit, "Cache Metadata corrupted");
        if (strcmp(data, key) == 0)
            return value;

        // Skip value part
        data = value + strlen(value) + 1;
    }
    NS_ABORT_IF_FALSE(data == limit, "Metadata corrupted");
    return nsnull;
}


nsresult
nsCacheMetaData::SetElement(const char * key,
                            const char * value)
{
    const PRUint32 keySize = strlen(key) + 1;
    char * pos = (char *)GetElement(key);

    if (!value) {
        // No value means remove the key/value pair completely, if existing
        if (pos) {
            PRUint32 oldValueSize = strlen(pos) + 1;
            PRUint32 offset = pos - mBuffer;
            PRUint32 remainder = mMetaSize - (offset + oldValueSize);

            memmove(pos - keySize, pos + oldValueSize, remainder);
            mMetaSize -= keySize + oldValueSize;
        }
        return NS_OK;
    }

    const PRUint32 valueSize = strlen(value) + 1;
    PRUint32 newSize = mMetaSize + valueSize;
    if (pos) {
        const PRUint32 oldValueSize = strlen(pos) + 1;
        const PRUint32 offset = pos - mBuffer;
        const PRUint32 remainder = mMetaSize - (offset + oldValueSize);

        // Update the value in place
        newSize -= oldValueSize;
        nsresult rv = EnsureBuffer(newSize);
        NS_ENSURE_SUCCESS(rv, rv);

        // Move the remainder to the right place
        pos = mBuffer + offset;
        memmove(pos + valueSize, pos + oldValueSize, remainder);
    } else {
        // allocate new meta data element
        newSize += keySize;
        nsresult rv = EnsureBuffer(newSize);
        NS_ENSURE_SUCCESS(rv, rv);

        // Add after last element
        pos = mBuffer + mMetaSize;
        memcpy(pos, key, keySize);
        pos += keySize;
    }

    // Update value
    memcpy(pos, value, valueSize);
    mMetaSize = newSize;

    return NS_OK;
}

nsresult
nsCacheMetaData::FlattenMetaData(char * buffer, PRUint32 bufSize)
{
    if (mMetaSize > bufSize) {
        NS_ERROR("buffer size too small for meta data.");
        return NS_ERROR_OUT_OF_MEMORY;
    }

    memcpy(buffer, mBuffer, mMetaSize);
    return NS_OK;
}

nsresult
nsCacheMetaData::UnflattenMetaData(const char * data, PRUint32 size)
{
    if (data && size) {
        // Check if the metadata ends with a zero byte.
        if (data[size-1] != '\0') {
            NS_ERROR("Cache MetaData is not null terminated");
            return NS_ERROR_ILLEGAL_VALUE;
        }
        // Check that there are an even number of zero bytes
        // to match the pattern { key \0 value \0 }
        bool odd = false;
        for (PRUint32 i = 0; i < size; i++) {
            if (data[i] == '\0') 
                odd = !odd;
        }
        if (odd) {
            NS_ERROR("Cache MetaData is malformed");
            return NS_ERROR_ILLEGAL_VALUE;
        }

        nsresult rv = EnsureBuffer(size);
        NS_ENSURE_SUCCESS(rv, rv);

        memcpy(mBuffer, data, size);
        mMetaSize = size;
    }
    return NS_OK;
}

nsresult
nsCacheMetaData::VisitElements(nsICacheMetaDataVisitor * visitor)
{
    const char * data = mBuffer;
    const char * limit = mBuffer + mMetaSize;

    while (data < limit) {
        const char * key = data;
        // Skip key part
        data += strlen(data) + 1;
        NS_ABORT_IF_FALSE(data < limit, "Metadata corrupted");
        bool keepGoing;
        nsresult rv = visitor->VisitMetaDataElement(key, data, &keepGoing);
        if (NS_FAILED(rv) || !keepGoing)
            return NS_OK;

        // Skip value part
        data += strlen(data) + 1;
    }
    NS_ABORT_IF_FALSE(data == limit, "Metadata corrupted");
    return NS_OK;
}

nsresult
nsCacheMetaData::EnsureBuffer(PRUint32 bufSize)
{
    if (mBufferSize < bufSize) {
        char * buf = (char *)PR_REALLOC(mBuffer, bufSize);
        if (!buf) {
            return NS_ERROR_OUT_OF_MEMORY;
        }
        mBuffer = buf;
        mBufferSize = bufSize;
    }
    return NS_OK;
}        
