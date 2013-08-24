/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * Mozilla Corporation
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Dave Camp <dcamp@mozilla.com>
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

#ifndef nsOfflineCacheUpdate_h__
#define nsOfflineCacheUpdate_h__

#include "nsIOfflineCacheUpdate.h"

#include "nsAutoPtr.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsICacheService.h"
#include "nsIChannelEventSink.h"
#include "nsIDOMDocument.h"
#include "nsIDOMNode.h"
#include "nsIDOMLoadStatus.h"
#include "nsIInterfaceRequestor.h"
#include "nsIMutableArray.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsIApplicationCache.h"
#include "nsIRequestObserver.h"
#include "nsIRunnable.h"
#include "nsIStreamListener.h"
#include "nsIURI.h"
#include "nsIWebProgressListener.h"
#include "nsClassHashtable.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsWeakReference.h"
#include "nsICryptoHash.h"

class nsOfflineCacheUpdate;

class nsICacheEntryDescriptor;
class nsIUTF8StringEnumerator;

class nsOfflineCacheUpdateItem : public nsIDOMLoadStatus
                               , public nsIStreamListener
                               , public nsIRunnable
                               , public nsIInterfaceRequestor
                               , public nsIChannelEventSink
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIDOMLOADSTATUS
    NS_DECL_NSIREQUESTOBSERVER
    NS_DECL_NSISTREAMLISTENER
    NS_DECL_NSIRUNNABLE
    NS_DECL_NSIINTERFACEREQUESTOR
    NS_DECL_NSICHANNELEVENTSINK

    nsOfflineCacheUpdateItem(nsOfflineCacheUpdate *aUpdate,
                             nsIURI *aURI,
                             nsIURI *aReferrerURI,
                             nsIApplicationCache *aPreviousApplicationCache,
                             const nsACString &aClientID,
                             PRUint32 aType);
    virtual ~nsOfflineCacheUpdateItem();

    nsCOMPtr<nsIURI>           mURI;
    nsCOMPtr<nsIURI>           mReferrerURI;
    nsCOMPtr<nsIApplicationCache> mPreviousApplicationCache;
    nsCString                  mClientID;
    nsCString                  mCacheKey;
    PRUint32                   mItemType;

    nsresult OpenChannel();
    nsresult Cancel();
    nsresult GetRequestSucceeded(PRBool * succeeded);

private:
    nsOfflineCacheUpdate*          mUpdate;
    nsCOMPtr<nsIChannel>           mChannel;
    PRUint16                       mState;

protected:
    PRInt64                        mBytesRead;
};


class nsOfflineManifestItem : public nsOfflineCacheUpdateItem
{
public:
    NS_DECL_NSISTREAMLISTENER
    NS_DECL_NSIREQUESTOBSERVER

    nsOfflineManifestItem(nsOfflineCacheUpdate *aUpdate,
                          nsIURI *aURI,
                          nsIURI *aReferrerURI,
                          nsIApplicationCache *aPreviousApplicationCache,
                          const nsACString &aClientID);
    virtual ~nsOfflineManifestItem();

    nsCOMArray<nsIURI> &GetExplicitURIs() { return mExplicitURIs; }
    nsCOMArray<nsIURI> &GetFallbackURIs() { return mFallbackURIs; }

    nsTArray<nsCString> &GetOpportunisticNamespaces()
        { return mOpportunisticNamespaces; }
    nsIArray *GetNamespaces()
        { return mNamespaces.get(); }

    PRBool ParseSucceeded()
        { return (mParserState != PARSE_INIT && mParserState != PARSE_ERROR); }
    PRBool NeedsUpdate() { return mParserState != PARSE_INIT && mNeedsUpdate; }

    void GetManifestHash(nsCString &aManifestHash)
        { aManifestHash = mManifestHashValue; }

private:
    static NS_METHOD ReadManifest(nsIInputStream *aInputStream,
                                  void *aClosure,
                                  const char *aFromSegment,
                                  PRUint32 aOffset,
                                  PRUint32 aCount,
                                  PRUint32 *aBytesConsumed);

    nsresult AddNamespace(PRUint32 namespaceType,
                          const nsCString &namespaceSpec,
                          const nsCString &data);

    nsresult HandleManifestLine(const nsCString::const_iterator &aBegin,
                                const nsCString::const_iterator &aEnd);

    /**
     * Saves "offline-manifest-hash" meta data from the old offline cache
     * token to mOldManifestHashValue member to be compared on
     * successfull load.
     */
    nsresult GetOldManifestContentHash(nsIRequest *aRequest);
    /**
     * This method setups the mNeedsUpdate to PR_FALSE when hash value
     * of the just downloaded manifest file is the same as stored in cache's 
     * "offline-manifest-hash" meta data. Otherwise stores the new value
     * to this meta data.
     */
    nsresult CheckNewManifestContentHash(nsIRequest *aRequest);

    void ReadStrictFileOriginPolicyPref();

    enum {
        PARSE_INIT,
        PARSE_CACHE_ENTRIES,
        PARSE_FALLBACK_ENTRIES,
        PARSE_BYPASS_ENTRIES,
        PARSE_ERROR
    } mParserState;

    nsCString mReadBuf;

    nsCOMArray<nsIURI> mExplicitURIs;
    nsCOMArray<nsIURI> mFallbackURIs;

    // All opportunistic caching namespaces.  Used to decide whether
    // to include previously-opportunistically-cached entries.
    nsTArray<nsCString> mOpportunisticNamespaces;

    // Array of nsIApplicationCacheNamespace objects specified by the
    // manifest.
    nsCOMPtr<nsIMutableArray> mNamespaces;

    PRBool mNeedsUpdate;
    PRBool mStrictFileOriginPolicy;

    // manifest hash data
    nsCOMPtr<nsICryptoHash> mManifestHash;
    PRBool mManifestHashInitialized;
    nsCString mManifestHashValue;
    nsCString mOldManifestHashValue;
};

class nsOfflineCacheUpdateOwner
{
public:
    virtual nsresult UpdateFinished(nsOfflineCacheUpdate *aUpdate) = 0;
};

class nsOfflineCacheUpdate : public nsIOfflineCacheUpdate
                           , public nsOfflineCacheUpdateOwner
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOFFLINECACHEUPDATE

    nsOfflineCacheUpdate();
    ~nsOfflineCacheUpdate();

    static nsresult GetCacheKey(nsIURI *aURI, nsACString &aKey);

    nsresult Init();

    nsresult Begin();
    nsresult Cancel();

    void LoadCompleted();
    void ManifestCheckCompleted(nsresult aStatus,
                                const nsCString &aManifestHash);
    void AddDocument(nsIDOMDocument *aDocument);

    void SetOwner(nsOfflineCacheUpdateOwner *aOwner);

    virtual nsresult UpdateFinished(nsOfflineCacheUpdate *aUpdate);

private:
    nsresult HandleManifest(PRBool *aDoUpdate);
    nsresult AddURI(nsIURI *aURI, PRUint32 aItemType);

    nsresult ProcessNextURI();

    // Adds items from the previous cache witha type matching aType.
    // If namespaceFilter is non-null, only items matching the
    // specified namespaces will be added.
    nsresult AddExistingItems(PRUint32 aType,
                              nsTArray<nsCString>* namespaceFilter = nsnull);

    nsresult GatherObservers(nsCOMArray<nsIOfflineCacheUpdateObserver> &aObservers);
    nsresult NotifyError();
    nsresult NotifyChecking();
    nsresult NotifyNoUpdate();
    nsresult NotifyObsolete();
    nsresult NotifyDownloading();
    nsresult NotifyStarted(nsOfflineCacheUpdateItem *aItem);
    nsresult NotifyCompleted(nsOfflineCacheUpdateItem *aItem);
    nsresult AssociateDocument(nsIDOMDocument *aDocument,
                               nsIApplicationCache *aApplicationCache);
    nsresult ScheduleImplicit();
    nsresult Finish();

    enum {
        STATE_UNINITIALIZED,
        STATE_INITIALIZED,
        STATE_CHECKING,
        STATE_DOWNLOADING,
        STATE_CANCELLED,
        STATE_FINISHED
    } mState;

    nsOfflineCacheUpdateOwner *mOwner;

    PRPackedBool mAddedItems;
    PRPackedBool mPartialUpdate;
    PRPackedBool mSucceeded;
    PRPackedBool mObsolete;

    nsCString mUpdateDomain;
    nsCOMPtr<nsIURI> mManifestURI;

    nsCOMPtr<nsIURI> mDocumentURI;

    nsCString mClientID;
    nsCOMPtr<nsIApplicationCache> mApplicationCache;
    nsCOMPtr<nsIApplicationCache> mPreviousApplicationCache;

    nsCOMPtr<nsIObserverService> mObserverService;

    nsRefPtr<nsOfflineManifestItem> mManifestItem;

    /* Items being updated */
    PRInt32 mCurrentItem;
    nsTArray<nsRefPtr<nsOfflineCacheUpdateItem> > mItems;

    /* Clients watching this update for changes */
    nsCOMArray<nsIWeakReference> mWeakObservers;
    nsCOMArray<nsIOfflineCacheUpdateObserver> mObservers;

    /* Documents that requested this update */
    nsCOMArray<nsIDOMDocument> mDocuments;

    /* Reschedule count.  When an update is rescheduled due to
     * mismatched manifests, the reschedule count will be increased. */
    PRUint32 mRescheduleCount;

    nsRefPtr<nsOfflineCacheUpdate> mImplicitUpdate;
};

class nsOfflineCacheUpdateService : public nsIOfflineCacheUpdateService
                                  , public nsIObserver
                                  , public nsOfflineCacheUpdateOwner
                                  , public nsSupportsWeakReference
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOFFLINECACHEUPDATESERVICE
    NS_DECL_NSIOBSERVER

    nsOfflineCacheUpdateService();
    ~nsOfflineCacheUpdateService();

    nsresult Init();

    nsresult Schedule(nsOfflineCacheUpdate *aUpdate);
    nsresult Schedule(nsIURI *aManifestURI,
                      nsIURI *aDocumentURI,
                      nsIDOMDocument *aDocument,
                      nsIOfflineCacheUpdate **aUpdate);

    virtual nsresult UpdateFinished(nsOfflineCacheUpdate *aUpdate);

    /**
     * Returns the singleton nsOfflineCacheUpdateService without an addref, or
     * nsnull if the service couldn't be created.
     */
    static nsOfflineCacheUpdateService *EnsureService();

    /** Addrefs and returns the singleton nsOfflineCacheUpdateService. */
    static nsOfflineCacheUpdateService *GetInstance();

private:
    nsresult ProcessNextUpdate();

    nsTArray<nsRefPtr<nsOfflineCacheUpdate> > mUpdates;

    PRBool mDisabled;
    PRBool mUpdateRunning;
};

#endif
