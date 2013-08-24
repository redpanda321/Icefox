/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set et cin ts=4 sw=4 sts=4: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHttpChannel_h__
#define nsHttpChannel_h__

#include "HttpBaseChannel.h"

#include "nsHttpTransaction.h"
#include "nsInputStreamPump.h"
#include "nsThreadUtils.h"
#include "nsTArray.h"

#include "nsIHttpEventSink.h"
#include "nsICachingChannel.h"
#include "nsICacheEntryDescriptor.h"
#include "nsICacheListener.h"
#include "nsIApplicationCacheChannel.h"
#include "nsIPrompt.h"
#include "nsIResumableChannel.h"
#include "nsIProtocolProxyCallback.h"
#include "nsICancelable.h"
#include "nsIHttpAuthenticableChannel.h"
#include "nsIHttpChannelAuthProvider.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsITimedChannel.h"
#include "nsIFile.h"
#include "nsDNSPrefetch.h"
#include "TimingStruct.h"
#include "AutoClose.h"
#include "mozilla/Telemetry.h"

class nsAHttpConnection;

namespace mozilla { namespace net {

class HttpCacheQuery;

//-----------------------------------------------------------------------------
// nsHttpChannel
//-----------------------------------------------------------------------------

class nsHttpChannel : public HttpBaseChannel
                    , public HttpAsyncAborter<nsHttpChannel>
                    , public nsIStreamListener
                    , public nsICachingChannel
                    , public nsICacheListener
                    , public nsITransportEventSink
                    , public nsIProtocolProxyCallback
                    , public nsIHttpAuthenticableChannel
                    , public nsIApplicationCacheChannel
                    , public nsIAsyncVerifyRedirectCallback
                    , public nsITimedChannel
{
public:
    NS_DECL_ISUPPORTS_INHERITED
    NS_DECL_NSIREQUESTOBSERVER
    NS_DECL_NSISTREAMLISTENER
    NS_DECL_NSICACHEINFOCHANNEL
    NS_DECL_NSICACHINGCHANNEL
    NS_DECL_NSICACHELISTENER
    NS_DECL_NSITRANSPORTEVENTSINK
    NS_DECL_NSIPROTOCOLPROXYCALLBACK
    NS_DECL_NSIPROXIEDCHANNEL
    NS_DECL_NSIAPPLICATIONCACHECONTAINER
    NS_DECL_NSIAPPLICATIONCACHECHANNEL
    NS_DECL_NSIASYNCVERIFYREDIRECTCALLBACK
    NS_DECL_NSITIMEDCHANNEL

    // nsIHttpAuthenticableChannel. We can't use
    // NS_DECL_NSIHTTPAUTHENTICABLECHANNEL because it duplicates cancel() and
    // others.
    NS_IMETHOD GetIsSSL(bool *aIsSSL);
    NS_IMETHOD GetProxyMethodIsConnect(bool *aProxyMethodIsConnect);
    NS_IMETHOD GetServerResponseHeader(nsACString & aServerResponseHeader);
    NS_IMETHOD GetProxyChallenges(nsACString & aChallenges);
    NS_IMETHOD GetWWWChallenges(nsACString & aChallenges);
    NS_IMETHOD SetProxyCredentials(const nsACString & aCredentials);
    NS_IMETHOD SetWWWCredentials(const nsACString & aCredentials);
    NS_IMETHOD OnAuthAvailable();
    NS_IMETHOD OnAuthCancelled(bool userCancel);
    // Functions we implement from nsIHttpAuthenticableChannel but are
    // declared in HttpBaseChannel must be implemented in this class. We
    // just call the HttpBaseChannel:: impls.
    NS_IMETHOD GetLoadFlags(nsLoadFlags *aLoadFlags);
    NS_IMETHOD GetURI(nsIURI **aURI);
    NS_IMETHOD GetNotificationCallbacks(nsIInterfaceRequestor **aCallbacks);
    NS_IMETHOD GetLoadGroup(nsILoadGroup **aLoadGroup);
    NS_IMETHOD GetRequestMethod(nsACString& aMethod);

    nsHttpChannel();
    virtual ~nsHttpChannel();

    virtual nsresult Init(nsIURI *aURI, PRUint8 aCaps, nsProxyInfo *aProxyInfo);

    // Methods HttpBaseChannel didn't implement for us or that we override.
    //
    // nsIRequest
    NS_IMETHOD Cancel(nsresult status);
    NS_IMETHOD Suspend();
    NS_IMETHOD Resume();
    // nsIChannel
    NS_IMETHOD GetSecurityInfo(nsISupports **aSecurityInfo);
    NS_IMETHOD AsyncOpen(nsIStreamListener *listener, nsISupports *aContext);
    // nsIHttpChannelInternal
    NS_IMETHOD SetupFallbackChannel(const char *aFallbackKey);
    // nsISupportsPriority
    NS_IMETHOD SetPriority(PRInt32 value);
    // nsIResumableChannel
    NS_IMETHOD ResumeAt(PRUint64 startPos, const nsACString& entityID);

public: /* internal necko use only */ 

    void InternalSetUploadStream(nsIInputStream *uploadStream) 
      { mUploadStream = uploadStream; }
    void SetUploadStreamHasHeaders(bool hasHeaders) 
      { mUploadStreamHasHeaders = hasHeaders; }

    nsresult SetReferrerInternal(nsIURI *referrer) {
        nsCAutoString spec;
        nsresult rv = referrer->GetAsciiSpec(spec);
        if (NS_FAILED(rv)) return rv;
        mReferrer = referrer;
        mRequestHead.SetHeader(nsHttp::Referer, spec);
        return NS_OK;
    }

    // This allows cache entry to be marked as foreign even after channel itself
    // is gone.  Needed for e10s (see HttpChannelParent::RecvDocumentChannelCleanup)
    class OfflineCacheEntryAsForeignMarker {
        nsCOMPtr<nsIApplicationCache> mApplicationCache;
        nsCString mCacheKey;
    public:
        OfflineCacheEntryAsForeignMarker(nsIApplicationCache* appCache,
                                         const nsCSubstring& key)
             : mApplicationCache(appCache)
             , mCacheKey(key)
        {}

        nsresult MarkAsForeign();
    };

    OfflineCacheEntryAsForeignMarker* GetOfflineCacheEntryAsForeignMarker();

    /**
     * Returns true if this channel is operating in private browsing mode,
     * false otherwise.
     */
    bool UsingPrivateBrowsing() {
        bool usingPB;
        GetUsingPrivateBrowsing(&usingPB);
        return usingPB;
    }

private:
    typedef nsresult (nsHttpChannel::*nsContinueRedirectionFunc)(nsresult result);

    bool     RequestIsConditional();
    nsresult Connect();
    nsresult ContinueConnect();
    void     SpeculativeConnect();
    nsresult SetupTransaction();
    nsresult CallOnStartRequest();
    nsresult ProcessResponse();
    nsresult ContinueProcessResponse(nsresult);
    nsresult ProcessNormal();
    nsresult ContinueProcessNormal(nsresult);
    nsresult ProcessNotModified();
    nsresult AsyncProcessRedirection(PRUint32 httpStatus);
    nsresult ContinueProcessRedirection(nsresult);
    nsresult ContinueProcessRedirectionAfterFallback(nsresult);
    bool     ShouldSSLProxyResponseContinue(PRUint32 httpStatus);
    nsresult ProcessFailedSSLConnect(PRUint32 httpStatus);
    nsresult ProcessFallback(bool *waitingForRedirectCallback);
    nsresult ContinueProcessFallback(nsresult);
    void     HandleAsyncAbort();
    nsresult EnsureAssocReq();

    nsresult ContinueOnStartRequest1(nsresult);
    nsresult ContinueOnStartRequest2(nsresult);
    nsresult ContinueOnStartRequest3(nsresult);

    // redirection specific methods
    void     HandleAsyncRedirect();
    nsresult ContinueHandleAsyncRedirect(nsresult);
    void     HandleAsyncNotModified();
    void     HandleAsyncFallback();
    nsresult ContinueHandleAsyncFallback(nsresult);
    nsresult PromptTempRedirect();
    virtual nsresult SetupReplacementChannel(nsIURI *, nsIChannel *,
                                             bool preserveMethod,
                                             bool forProxy);

    // proxy specific methods
    nsresult ProxyFailover();
    nsresult AsyncDoReplaceWithProxy(nsIProxyInfo *);
    nsresult ContinueDoReplaceWithProxy(nsresult);
    void HandleAsyncReplaceWithProxy();
    nsresult ContinueHandleAsyncReplaceWithProxy(nsresult);
    nsresult ResolveProxy();

    // cache specific methods
    nsresult OpenCacheEntry(bool usingSSL);
    nsresult OnOfflineCacheEntryAvailable(nsICacheEntryDescriptor *aEntry,
                                          nsCacheAccessMode aAccess,
                                          nsresult aResult);
    nsresult OpenNormalCacheEntry(bool usingSSL);
    nsresult OnNormalCacheEntryAvailable(nsICacheEntryDescriptor *aEntry,
                                         nsCacheAccessMode aAccess,
                                         nsresult aResult);
    nsresult OpenOfflineCacheEntryForWriting();
    nsresult OnOfflineCacheEntryForWritingAvailable(
        nsICacheEntryDescriptor *aEntry,
        nsCacheAccessMode aAccess,
        nsresult aResult);
    nsresult OnCacheEntryAvailableInternal(nsICacheEntryDescriptor *entry,
                                           nsCacheAccessMode access,
                                           nsresult status);
    nsresult GenerateCacheKey(PRUint32 postID, nsACString &key);
    nsresult UpdateExpirationTime();
    nsresult CheckCache();
    bool ShouldUpdateOfflineCacheEntry();
    nsresult ReadFromCache(bool alreadyMarkedValid);
    void     CloseCacheEntry(bool doomOnFailure);
    void     CloseOfflineCacheEntry();
    nsresult InitCacheEntry();
    void     UpdateInhibitPersistentCachingFlag();
    nsresult InitOfflineCacheEntry();
    nsresult AddCacheEntryHeaders(nsICacheEntryDescriptor *entry);
    nsresult StoreAuthorizationMetaData(nsICacheEntryDescriptor *entry);
    nsresult FinalizeCacheEntry();
    nsresult InstallCacheListener(PRUint32 offset = 0);
    nsresult InstallOfflineCacheListener();
    void     MaybeInvalidateCacheEntryForSubsequentGet();
    nsCacheStoragePolicy DetermineStoragePolicy(bool isPrivate);
    nsresult DetermineCacheAccess(nsCacheAccessMode *_retval);
    void     AsyncOnExamineCachedResponse();

    // Handle the bogus Content-Encoding Apache sometimes sends
    void ClearBogusContentEncodingIfNeeded();

    // byte range request specific methods
    nsresult ProcessPartialContent();
    nsresult OnDoneReadingPartialCacheEntry(bool *streamDone);

    nsresult DoAuthRetry(nsAHttpConnection *);

    void     HandleAsyncRedirectChannelToHttps();
    nsresult AsyncRedirectChannelToHttps();
    nsresult ContinueAsyncRedirectChannelToHttps(nsresult rv);

    /**
     * A function that takes care of reading STS headers and enforcing STS 
     * load rules.  After a secure channel is erected, STS requires the channel
     * to be trusted or any STS header data on the channel is ignored.
     * This is called from ProcessResponse.
     */
    nsresult ProcessSTSHeader();

    void InvalidateCacheEntryForLocation(const char *location);
    void AssembleCacheKey(const char *spec, PRUint32 postID, nsACString &key);
    nsresult CreateNewURI(const char *loc, nsIURI **newURI);
    void DoInvalidateCacheEntry(const nsCString &key);

    // Ref RFC2616 13.10: "invalidation... MUST only be performed if
    // the host part is the same as in the Request-URI"
    inline bool HostPartIsTheSame(nsIURI *uri) {
        nsCAutoString tmpHost1, tmpHost2;
        return (NS_SUCCEEDED(mURI->GetAsciiHost(tmpHost1)) &&
                NS_SUCCEEDED(uri->GetAsciiHost(tmpHost2)) &&
                (tmpHost1 == tmpHost2));
    }

    inline static bool DoNotRender3xxBody(nsresult rv) {
        return rv == NS_ERROR_REDIRECT_LOOP         ||
               rv == NS_ERROR_CORRUPTED_CONTENT     ||
               rv == NS_ERROR_UNKNOWN_PROTOCOL      ||
               rv == NS_ERROR_MALFORMED_URI;
    }

private:
    nsCOMPtr<nsISupports>             mSecurityInfo;
    nsCOMPtr<nsICancelable>           mProxyRequest;

    nsRefPtr<nsInputStreamPump>       mTransactionPump;
    nsRefPtr<nsHttpTransaction>       mTransaction;

    PRUint64                          mLogicalOffset;

    // cache specific data
    nsRefPtr<HttpCacheQuery>          mCacheQuery;
    nsCOMPtr<nsICacheEntryDescriptor> mCacheEntry;
    // We must close mCacheInputStream explicitly to avoid leaks.
    AutoClose<nsIInputStream>         mCacheInputStream;
    nsRefPtr<nsInputStreamPump>       mCachePump;
    nsAutoPtr<nsHttpResponseHead>     mCachedResponseHead;
    nsCOMPtr<nsISupports>             mCachedSecurityInfo;
    nsCacheAccessMode                 mCacheAccess;
    mozilla::Telemetry::ID            mCacheEntryDeviceTelemetryID;
    PRUint32                          mPostID;
    PRUint32                          mRequestTime;

    typedef nsresult (nsHttpChannel:: *nsOnCacheEntryAvailableCallback)(
        nsICacheEntryDescriptor *, nsCacheAccessMode, nsresult);
    nsOnCacheEntryAvailableCallback   mOnCacheEntryAvailableCallback;

    nsCOMPtr<nsICacheEntryDescriptor> mOfflineCacheEntry;
    nsCacheAccessMode                 mOfflineCacheAccess;
    PRUint32                          mOfflineCacheLastModifiedTime;
    nsCOMPtr<nsIApplicationCache>     mApplicationCacheForWrite;

    // auth specific data
    nsCOMPtr<nsIHttpChannelAuthProvider> mAuthProvider;

    // Proxy info to replace with
    nsCOMPtr<nsIProxyInfo>            mTargetProxyInfo;

    // If the channel is associated with a cache, and the URI matched
    // a fallback namespace, this will hold the key for the fallback
    // cache entry.
    nsCString                         mFallbackKey;

    friend class AutoRedirectVetoNotifier;
    friend class HttpAsyncAborter<nsHttpChannel>;
    friend class HttpCacheQuery;

    nsCOMPtr<nsIURI>                  mRedirectURI;
    nsCOMPtr<nsIChannel>              mRedirectChannel;
    PRUint32                          mRedirectType;

    // state flags
    PRUint32                          mCachedContentIsValid     : 1;
    PRUint32                          mCachedContentIsPartial   : 1;
    PRUint32                          mTransactionReplaced      : 1;
    PRUint32                          mAuthRetryPending         : 1;
    PRUint32                          mResuming                 : 1;
    PRUint32                          mInitedCacheEntry         : 1;
    // True if we are loading a fallback cache entry from the
    // application cache.
    PRUint32                          mFallbackChannel          : 1;
    // True if consumer added its own If-None-Match or If-Modified-Since
    // headers. In such a case we must not override them in the cache code
    // and also we want to pass possible 304 code response through.
    PRUint32                          mCustomConditionalRequest : 1;
    PRUint32                          mFallingBack              : 1;
    PRUint32                          mWaitingForRedirectCallback : 1;
    // True if mRequestTime has been set. In such a case it is safe to update
    // the cache entry's expiration time. Otherwise, it is not(see bug 567360).
    PRUint32                          mRequestTimeInitialized : 1;

    nsTArray<nsContinueRedirectionFunc> mRedirectFuncStack;

    PRTime                            mChannelCreationTime;
    mozilla::TimeStamp                mChannelCreationTimestamp;
    mozilla::TimeStamp                mAsyncOpenTime;
    mozilla::TimeStamp                mCacheReadStart;
    mozilla::TimeStamp                mCacheReadEnd;
    // copied from the transaction before we null out mTransaction
    // so that the timing can still be queried from OnStopRequest
    TimingStruct                      mTransactionTimings;
    // Needed for accurate DNS timing
    nsRefPtr<nsDNSPrefetch>           mDNSPrefetch;

    nsresult WaitForRedirectCallback();
    void PushRedirectAsyncFunc(nsContinueRedirectionFunc func);
    void PopRedirectAsyncFunc(nsContinueRedirectionFunc func);

protected:
    virtual void DoNotifyListenerCleanup();

private: // cache telemetry
    bool mDidReval;
};

} } // namespace mozilla::net

#endif // nsHttpChannel_h__
