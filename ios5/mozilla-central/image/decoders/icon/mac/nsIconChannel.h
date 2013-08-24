/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsIconChannel_h___
#define nsIconChannel_h___

#include "mozilla/Attributes.h"

#include "nsCOMPtr.h"
#include "nsXPIDLString.h"
#include "nsIChannel.h"
#include "nsILoadGroup.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIInputStreamPump.h"
#include "nsIStreamListener.h"
#include "nsIURI.h"

class nsIFile;

class nsIconChannel MOZ_FINAL : public nsIChannel, public nsIStreamListener
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUEST
  NS_DECL_NSICHANNEL
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER

  nsIconChannel();
  virtual ~nsIconChannel();

  nsresult Init(nsIURI* uri);

protected:
  nsCOMPtr<nsIURI> mUrl;
  nsCOMPtr<nsIURI> mOriginalURI;
  PRInt32          mContentLength;
  nsCOMPtr<nsILoadGroup> mLoadGroup;
  nsCOMPtr<nsIInterfaceRequestor> mCallbacks;
  nsCOMPtr<nsISupports>  mOwner; 
  
  nsCOMPtr<nsIInputStreamPump> mPump;
  nsCOMPtr<nsIStreamListener>  mListener;
  
  nsresult MakeInputStream(nsIInputStream** _retval, bool nonBlocking);
  
  nsresult ExtractIconInfoFromUrl(nsIFile ** aLocalFile, PRUint32 * aDesiredImageSize,
                           nsACString &aContentType, nsACString &aFileExtension);
};

#endif /* nsIconChannel_h___ */
