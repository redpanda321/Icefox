/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */

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
 *  The Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Daniel Witte <dwitte@mozilla.com>
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

#ifndef mozilla_net_HttpBaseChannel_h
#define mozilla_net_HttpBaseChannel_h

#include "nsHttp.h"
#include "nsAutoPtr.h"
#include "nsHashPropertyBag.h"
#include "nsProxyInfo.h"
#include "nsHttpRequestHead.h"
#include "nsHttpResponseHead.h"
#include "nsHttpConnectionInfo.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIUploadChannel.h"
#include "nsIUploadChannel2.h"
#include "nsIProgressEventSink.h"
#include "nsIURI.h"
#include "nsISupportsPriority.h"
#include "nsIApplicationCache.h"
#include "nsIResumableChannel.h"

#define DIE_WITH_ASYNC_OPEN_MSG()                                              \
  do {                                                                         \
    fprintf(stderr,                                                            \
            "*&*&*&*&*&*&*&**&*&&*& FATAL ERROR: '%s' "                        \
            "called after AsyncOpen: %s +%d",                                  \
            __FUNCTION__, __FILE__, __LINE__);                                 \
    NS_ABORT();                                                                \
    return NS_ERROR_NOT_IMPLEMENTED;                                           \
  } while (0)

#define ENSURE_CALLED_BEFORE_ASYNC_OPEN()                                      \
  if (mIsPending)                                                              \
    DIE_WITH_ASYNC_OPEN_MSG();                                                 \
  if (mWasOpened)                                                              \
    DIE_WITH_ASYNC_OPEN_MSG();                                                 \
  NS_ENSURE_TRUE(!mIsPending, NS_ERROR_IN_PROGRESS);                           \
  NS_ENSURE_TRUE(!mWasOpened, NS_ERROR_ALREADY_OPENED);

namespace mozilla {
namespace net {

typedef enum { eUploadStream_null = -1,
               eUploadStream_hasNoHeaders = 0,
               eUploadStream_hasHeaders = 1 } UploadStreamInfoType;

/*
 * This class is a partial implementation of nsIHttpChannel.  It contains code
 * shared by nsHttpChannel and HttpChannelChild. 
 * - Note that this class has nothing to do with nsBaseChannel, which is an
 *   earlier effort at a base class for channels that somehow never made it all
 *   the way to the HTTP channel.
 */
class HttpBaseChannel : public nsHashPropertyBag
                      , public nsIHttpChannel
                      , public nsIHttpChannelInternal
                      , public nsIUploadChannel
                      , public nsIUploadChannel2
                      , public nsISupportsPriority
                      , public nsIResumableChannel
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIUPLOADCHANNEL
  NS_DECL_NSIUPLOADCHANNEL2

  HttpBaseChannel();
  virtual ~HttpBaseChannel();

  virtual nsresult Init(nsIURI *aURI, PRUint8 aCaps, nsProxyInfo *aProxyInfo);

  // nsIRequest
  NS_IMETHOD GetName(nsACString& aName);
  NS_IMETHOD IsPending(PRBool *aIsPending);
  NS_IMETHOD GetStatus(nsresult *aStatus);
  NS_IMETHOD GetLoadGroup(nsILoadGroup **aLoadGroup);
  NS_IMETHOD SetLoadGroup(nsILoadGroup *aLoadGroup);
  NS_IMETHOD GetLoadFlags(nsLoadFlags *aLoadFlags);
  NS_IMETHOD SetLoadFlags(nsLoadFlags aLoadFlags);

  // nsIChannel
  NS_IMETHOD GetOriginalURI(nsIURI **aOriginalURI);
  NS_IMETHOD SetOriginalURI(nsIURI *aOriginalURI);
  NS_IMETHOD GetURI(nsIURI **aURI);
  NS_IMETHOD GetOwner(nsISupports **aOwner);
  NS_IMETHOD SetOwner(nsISupports *aOwner);
  NS_IMETHOD GetNotificationCallbacks(nsIInterfaceRequestor **aCallbacks);
  NS_IMETHOD SetNotificationCallbacks(nsIInterfaceRequestor *aCallbacks);
  NS_IMETHOD GetContentType(nsACString& aContentType);
  NS_IMETHOD SetContentType(const nsACString& aContentType);
  NS_IMETHOD GetContentCharset(nsACString& aContentCharset);
  NS_IMETHOD SetContentCharset(const nsACString& aContentCharset);
  NS_IMETHOD GetContentDisposition(nsACString& aContentDisposition);
  NS_IMETHOD GetContentLength(PRInt64 *aContentLength);
  NS_IMETHOD SetContentLength(PRInt64 aContentLength);
  NS_IMETHOD Open(nsIInputStream **aResult);

  // HttpBaseChannel::nsIHttpChannel
  NS_IMETHOD GetRequestMethod(nsACString& aMethod);
  NS_IMETHOD SetRequestMethod(const nsACString& aMethod);
  NS_IMETHOD GetReferrer(nsIURI **referrer);
  NS_IMETHOD SetReferrer(nsIURI *referrer);
  NS_IMETHOD GetRequestHeader(const nsACString& aHeader, nsACString& aValue);
  NS_IMETHOD SetRequestHeader(const nsACString& aHeader, 
                              const nsACString& aValue, PRBool aMerge);
  NS_IMETHOD VisitRequestHeaders(nsIHttpHeaderVisitor *visitor);
  NS_IMETHOD GetResponseHeader(const nsACString &header, nsACString &value);
  NS_IMETHOD SetResponseHeader(const nsACString& header, 
                               const nsACString& value, PRBool merge);
  NS_IMETHOD VisitResponseHeaders(nsIHttpHeaderVisitor *visitor);
  NS_IMETHOD GetAllowPipelining(PRBool *value);
  NS_IMETHOD SetAllowPipelining(PRBool value);
  NS_IMETHOD GetRedirectionLimit(PRUint32 *value);
  NS_IMETHOD SetRedirectionLimit(PRUint32 value);
  NS_IMETHOD IsNoStoreResponse(PRBool *value);
  NS_IMETHOD IsNoCacheResponse(PRBool *value);
  NS_IMETHOD GetResponseStatus(PRUint32 *aValue);
  NS_IMETHOD GetResponseStatusText(nsACString& aValue);
  NS_IMETHOD GetRequestSucceeded(PRBool *aValue);

  // nsIHttpChannelInternal
  NS_IMETHOD GetDocumentURI(nsIURI **aDocumentURI);
  NS_IMETHOD SetDocumentURI(nsIURI *aDocumentURI);
  NS_IMETHOD GetRequestVersion(PRUint32 *major, PRUint32 *minor);
  NS_IMETHOD GetResponseVersion(PRUint32 *major, PRUint32 *minor);
  NS_IMETHOD SetCookie(const char *aCookieHeader);
  NS_IMETHOD GetForceAllowThirdPartyCookie(PRBool *aForce);
  NS_IMETHOD SetForceAllowThirdPartyCookie(PRBool aForce);
  NS_IMETHOD GetCanceled(PRBool *aCanceled);

  // nsISupportsPriority
  NS_IMETHOD GetPriority(PRInt32 *value);
  NS_IMETHOD AdjustPriority(PRInt32 delta);

  // nsIResumableChannel
  NS_IMETHOD GetEntityID(nsACString& aEntityID);

protected:
  void AddCookiesToRequest();
  virtual nsresult SetupReplacementChannel(nsIURI *,
                                           nsIChannel *,
                                           PRBool preserveMethod);

  // Helper function to simplify getting notification callbacks.
  template <class T>
  void GetCallback(nsCOMPtr<T> &aResult)
  {
    NS_QueryNotificationCallbacks(mCallbacks, mLoadGroup,
                                  NS_GET_TEMPLATE_IID(T),
                                  getter_AddRefs(aResult));
  }

  nsCOMPtr<nsIURI>                  mURI;
  nsCOMPtr<nsIURI>                  mOriginalURI;
  nsCOMPtr<nsIURI>                  mDocumentURI;
  nsCOMPtr<nsIStreamListener>       mListener;
  nsCOMPtr<nsISupports>             mListenerContext;
  nsCOMPtr<nsILoadGroup>            mLoadGroup;
  nsCOMPtr<nsISupports>             mOwner;
  nsCOMPtr<nsIInterfaceRequestor>   mCallbacks;
  nsCOMPtr<nsIProgressEventSink>    mProgressSink;
  nsCOMPtr<nsIURI>                  mReferrer;
  nsCOMPtr<nsIApplicationCache>     mApplicationCache;

  nsHttpRequestHead                 mRequestHead;
  nsCOMPtr<nsIInputStream>          mUploadStream;
  nsAutoPtr<nsHttpResponseHead>     mResponseHead;
  nsRefPtr<nsHttpConnectionInfo>    mConnectionInfo;

  nsCString                         mSpec; // ASCII encoded URL spec
  nsCString                         mContentTypeHint;
  nsCString                         mContentCharsetHint;
  nsCString                         mUserSetCookieHeader;

  // Resumable channel specific data
  nsCString                         mEntityID;
  PRUint64                          mStartPos;

  nsresult                          mStatus;
  PRUint32                          mLoadFlags;
  PRInt16                           mPriority;
  PRUint8                           mCaps;
  PRUint8                           mRedirectionLimit;

  PRUint32                          mCanceled                   : 1;
  PRUint32                          mIsPending                  : 1;
  PRUint32                          mWasOpened                  : 1;
  PRUint32                          mResponseHeadersModified    : 1;
  PRUint32                          mAllowPipelining            : 1;
  PRUint32                          mForceAllowThirdPartyCookie : 1;
  PRUint32                          mUploadStreamHasHeaders     : 1;
  PRUint32                          mInheritApplicationCache    : 1;
  PRUint32                          mChooseApplicationCache     : 1;
  PRUint32                          mLoadedFromApplicationCache : 1;
};


} // namespace net
} // namespace mozilla

#endif // mozilla_net_HttpBaseChannel_h
