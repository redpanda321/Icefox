/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsStreamLoader.h"
#include "nsIInputStream.h"
#include "nsIChannel.h"
#include "nsNetError.h"

nsStreamLoader::nsStreamLoader()
  : mData(nsnull),
    mAllocated(0),
    mLength(0)
{
}

nsStreamLoader::~nsStreamLoader()
{
  if (mData) {
    NS_Free(mData);
  }
}

NS_IMETHODIMP
nsStreamLoader::Init(nsIStreamLoaderObserver* observer)
{
  NS_ENSURE_ARG_POINTER(observer);
  mObserver = observer;
  return NS_OK;
}

nsresult
nsStreamLoader::Create(nsISupports *aOuter, REFNSIID aIID, void **aResult)
{
  if (aOuter) return NS_ERROR_NO_AGGREGATION;

  nsStreamLoader* it = new nsStreamLoader();
  if (it == nsnull)
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(it);
  nsresult rv = it->QueryInterface(aIID, aResult);
  NS_RELEASE(it);
  return rv;
}

NS_IMPL_ISUPPORTS3(nsStreamLoader, nsIStreamLoader,
                   nsIRequestObserver, nsIStreamListener)

NS_IMETHODIMP 
nsStreamLoader::GetNumBytesRead(PRUint32* aNumBytes)
{
  *aNumBytes = mLength;
  return NS_OK;
}

/* readonly attribute nsIRequest request; */
NS_IMETHODIMP 
nsStreamLoader::GetRequest(nsIRequest **aRequest)
{
  NS_IF_ADDREF(*aRequest = mRequest);
  return NS_OK;
}

NS_IMETHODIMP 
nsStreamLoader::OnStartRequest(nsIRequest* request, nsISupports *ctxt)
{
  nsCOMPtr<nsIChannel> chan( do_QueryInterface(request) );
  if (chan) {
    PRInt64 contentLength = -1;
    chan->GetContentLength(&contentLength);
    if (contentLength >= 0) {
      if (contentLength > PR_UINT32_MAX) {
        // too big to fit in a PRUint32, bail now.
        // XXX make mAllocated and mLength 64-bit
        return NS_ERROR_OUT_OF_MEMORY;
      }

      // preallocate buffer
      mData = static_cast<PRUint8*>(NS_Alloc(contentLength));
      if (!mData) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      mAllocated = contentLength;
    }
  }
  mContext = ctxt;
  return NS_OK;
}

NS_IMETHODIMP 
nsStreamLoader::OnStopRequest(nsIRequest* request, nsISupports *ctxt,
                              nsresult aStatus)
{
  if (mObserver) {
    // provide nsIStreamLoader::request during call to OnStreamComplete
    mRequest = request;
    nsresult rv = mObserver->OnStreamComplete(this, mContext, aStatus,
                                              mLength, mData);
    if (rv == NS_SUCCESS_ADOPTED_DATA) {
      // the observer now owns the data buffer, and the loader must
      // not deallocate it
      mData = nsnull;
      mLength = 0;
      mAllocated = 0;
    }
    // done.. cleanup
    mRequest = 0;
    mObserver = 0;
    mContext = 0;
  }
  return NS_OK;
}

NS_METHOD
nsStreamLoader::WriteSegmentFun(nsIInputStream *inStr,
                                void *closure,
                                const char *fromSegment,
                                PRUint32 toOffset,
                                PRUint32 count,
                                PRUint32 *writeCount)
{
  nsStreamLoader *self = (nsStreamLoader *) closure;

  if (count > PR_UINT32_MAX - self->mLength) {
    return NS_ERROR_ILLEGAL_VALUE; // is there a better error to use here?
  }

  if (self->mLength + count > self->mAllocated) {
    self->mData = static_cast<PRUint8*>(NS_Realloc(self->mData,
                                                   self->mLength + count));
    if (!self->mData) {
      self->mLength = 0;
      self->mAllocated = 0;
      return NS_ERROR_OUT_OF_MEMORY;
    }
    self->mAllocated = self->mLength + count;
  }

  ::memcpy(self->mData + self->mLength, fromSegment, count);
  self->mLength += count;

  *writeCount = count;

  return NS_OK;
}

NS_IMETHODIMP 
nsStreamLoader::OnDataAvailable(nsIRequest* request, nsISupports *ctxt, 
                                nsIInputStream *inStr, 
                                PRUint32 sourceOffset, PRUint32 count)
{
  PRUint32 countRead;
  return inStr->ReadSegments(WriteSegmentFun, this, count, &countRead);
}
