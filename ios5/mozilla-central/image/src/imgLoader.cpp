/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Attributes.h"
#include "mozilla/FunctionTimer.h"
#include "mozilla/Preferences.h"

#include "ImageLogging.h"
#include "imgLoader.h"
#include "imgRequestProxy.h"

#include "RasterImage.h"
/* We end up pulling in windows.h because we eventually hit gfxWindowsSurface;
 * windows.h defines LoadImage, so we have to #undef it or imgLoader::LoadImage
 * gets changed.
 * This #undef needs to be in multiple places because we don't always pull
 * headers in in the same order.
 */
#undef LoadImage

#include "nsCOMPtr.h"

#include "nsContentUtils.h"
#include "nsCrossSiteListenerProxy.h"
#include "nsNetUtil.h"
#include "nsStreamUtils.h"
#include "nsIHttpChannel.h"
#include "nsICachingChannel.h"
#include "nsIInterfaceRequestor.h"
#include "nsIProgressEventSink.h"
#include "nsIChannelEventSink.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIServiceManager.h"
#include "nsIFileURL.h"
#include "nsThreadUtils.h"
#include "nsXPIDLString.h"
#include "nsCRT.h"
#include "nsIDocument.h"
#include "nsPIDOMWindow.h"

#include "netCore.h"

#include "nsURILoader.h"

#include "nsIComponentRegistrar.h"

#include "nsIApplicationCache.h"
#include "nsIApplicationCacheContainer.h"

#include "nsIMemoryReporter.h"
#include "nsIPrivateBrowsingService.h"

// we want to explore making the document own the load group
// so we can associate the document URI with the load group.
// until this point, we have an evil hack:
#include "nsIHttpChannelInternal.h"  
#include "nsIContentSecurityPolicy.h"
#include "nsIChannelPolicy.h"

#include "nsContentUtils.h"

using namespace mozilla;
using namespace mozilla::image;

#if defined(DEBUG_pavlov) || defined(DEBUG_timeless)
#include "nsISimpleEnumerator.h"
#include "nsXPCOM.h"
#include "nsISupportsPrimitives.h"
#include "nsXPIDLString.h"
#include "nsComponentManagerUtils.h"


static void PrintImageDecoders()
{
  nsCOMPtr<nsIComponentRegistrar> compMgr;
  if (NS_FAILED(NS_GetComponentRegistrar(getter_AddRefs(compMgr))) || !compMgr)
    return;
  nsCOMPtr<nsISimpleEnumerator> enumer;
  if (NS_FAILED(compMgr->EnumerateContractIDs(getter_AddRefs(enumer))) || !enumer)
    return;
  
  nsCString str;
  nsCOMPtr<nsISupports> s;
  bool more = false;
  while (NS_SUCCEEDED(enumer->HasMoreElements(&more)) && more) {
    enumer->GetNext(getter_AddRefs(s));
    if (s) {
      nsCOMPtr<nsISupportsCString> ss(do_QueryInterface(s));

      nsCAutoString xcs;
      ss->GetData(xcs);

      NS_NAMED_LITERAL_CSTRING(decoderContract, "@mozilla.org/image/decoder;3?type=");

      if (StringBeginsWith(xcs, decoderContract)) {
        printf("Have decoder for mime type: %s\n", xcs.get()+decoderContract.Length());
      }
    }
  }
}
#endif

NS_MEMORY_REPORTER_MALLOC_SIZEOF_FUN(ImagesMallocSizeOf, "images")

class imgMemoryReporter MOZ_FINAL :
  public nsIMemoryMultiReporter
{
public:
  imgMemoryReporter()
  {
  }

  NS_DECL_ISUPPORTS

  NS_IMETHOD GetName(nsACString &name)
  {
    name.Assign("images");
    return NS_OK;
  }

  NS_IMETHOD CollectReports(nsIMemoryMultiReporterCallback *callback,
                            nsISupports *closure)
  {
    AllSizes chrome;
    AllSizes content;
    imgLoader::sChromeCache.EnumerateRead(EntryAllSizes, &chrome);
    imgLoader::sCache.EnumerateRead(EntryAllSizes, &content);

#define REPORT(_path, _kind, _amount, _desc)                                  \
    do {                                                                      \
      nsresult rv;                                                            \
      rv = callback->Callback(EmptyCString(), NS_LITERAL_CSTRING(_path),      \
                              _kind, nsIMemoryReporter::UNITS_BYTES, _amount, \
                              NS_LITERAL_CSTRING(_desc), closure);            \
      NS_ENSURE_SUCCESS(rv, rv);                                              \
    } while (0)

    REPORT("explicit/images/chrome/used/raw",
           nsIMemoryReporter::KIND_HEAP, chrome.mUsedRaw,
           "Memory used by in-use chrome images (compressed data).");

    REPORT("explicit/images/chrome/used/uncompressed-heap",
           nsIMemoryReporter::KIND_HEAP, chrome.mUsedUncompressedHeap,
           "Memory used by in-use chrome images (uncompressed data).");

    REPORT("explicit/images/chrome/used/uncompressed-nonheap",
           nsIMemoryReporter::KIND_NONHEAP, chrome.mUsedUncompressedNonheap,
           "Memory used by in-use chrome images (uncompressed data).");

    REPORT("explicit/images/chrome/unused/raw",
           nsIMemoryReporter::KIND_HEAP, chrome.mUnusedRaw,
           "Memory used by not in-use chrome images (compressed data).");

    REPORT("explicit/images/chrome/unused/uncompressed-heap",
           nsIMemoryReporter::KIND_HEAP, chrome.mUnusedUncompressedHeap,
           "Memory used by not in-use chrome images (uncompressed data).");

    REPORT("explicit/images/chrome/unused/uncompressed-nonheap",
           nsIMemoryReporter::KIND_NONHEAP, chrome.mUnusedUncompressedNonheap,
           "Memory used by not in-use chrome images (uncompressed data).");

    REPORT("explicit/images/content/used/raw",
           nsIMemoryReporter::KIND_HEAP, content.mUsedRaw,
           "Memory used by in-use content images (compressed data).");

    REPORT("explicit/images/content/used/uncompressed-heap",
           nsIMemoryReporter::KIND_HEAP, content.mUsedUncompressedHeap,
           "Memory used by in-use content images (uncompressed data).");

    REPORT("explicit/images/content/used/uncompressed-nonheap",
           nsIMemoryReporter::KIND_NONHEAP, content.mUsedUncompressedNonheap,
           "Memory used by in-use content images (uncompressed data).");

    REPORT("explicit/images/content/unused/raw",
           nsIMemoryReporter::KIND_HEAP, content.mUnusedRaw,
           "Memory used by not in-use content images (compressed data).");

    REPORT("explicit/images/content/unused/uncompressed-heap",
           nsIMemoryReporter::KIND_HEAP, content.mUnusedUncompressedHeap,
           "Memory used by not in-use content images (uncompressed data).");

    REPORT("explicit/images/content/unused/uncompressed-nonheap",
           nsIMemoryReporter::KIND_NONHEAP, content.mUnusedUncompressedNonheap,
           "Memory used by not in-use content images (uncompressed data).");

#undef REPORT

    return NS_OK;
  }

  NS_IMETHOD GetExplicitNonHeap(PRInt64 *n)
  {
    size_t n2 = 0;
    imgLoader::sChromeCache.EnumerateRead(EntryExplicitNonHeapSize, &n2);
    imgLoader::sCache.EnumerateRead(EntryExplicitNonHeapSize, &n2);
    *n = n2;
    return NS_OK;
  }

  static PRInt64 GetImagesContentUsedUncompressed()
  {
    size_t n = 0;
    imgLoader::sCache.EnumerateRead(EntryUsedUncompressedSize, &n);
    return n;
  }

private:
  struct AllSizes {
    size_t mUsedRaw;
    size_t mUsedUncompressedHeap;
    size_t mUsedUncompressedNonheap;
    size_t mUnusedRaw;
    size_t mUnusedUncompressedHeap;
    size_t mUnusedUncompressedNonheap;

    AllSizes() {
      memset(this, 0, sizeof(*this));
    }
  };

  static PLDHashOperator EntryAllSizes(const nsACString&,
                                       imgCacheEntry *entry,
                                       void *userArg)
  {
    nsRefPtr<imgRequest> req = entry->GetRequest();
    Image *image = static_cast<Image*>(req->mImage.get());
    if (image) {
      AllSizes *sizes = static_cast<AllSizes*>(userArg);
      if (entry->HasNoProxies()) {
        sizes->mUnusedRaw +=
          image->HeapSizeOfSourceWithComputedFallback(ImagesMallocSizeOf);
        sizes->mUnusedUncompressedHeap +=
          image->HeapSizeOfDecodedWithComputedFallback(ImagesMallocSizeOf);
        sizes->mUnusedUncompressedNonheap += image->NonHeapSizeOfDecoded();
      } else {
        sizes->mUsedRaw +=
          image->HeapSizeOfSourceWithComputedFallback(ImagesMallocSizeOf);
        sizes->mUsedUncompressedHeap +=
          image->HeapSizeOfDecodedWithComputedFallback(ImagesMallocSizeOf);
        sizes->mUsedUncompressedNonheap += image->NonHeapSizeOfDecoded();
      }
    }

    return PL_DHASH_NEXT;
  }

  static PLDHashOperator EntryExplicitNonHeapSize(const nsACString&,
                                                  imgCacheEntry *entry,
                                                  void *userArg)
  {
    size_t *n = static_cast<size_t*>(userArg);
    nsRefPtr<imgRequest> req = entry->GetRequest();
    Image *image = static_cast<Image*>(req->mImage.get());
    if (image) {
      *n += image->NonHeapSizeOfDecoded();
    }

    return PL_DHASH_NEXT;
  }

  static PLDHashOperator EntryUsedUncompressedSize(const nsACString&,
                                                   imgCacheEntry *entry,
                                                   void *userArg)
  {
    if (!entry->HasNoProxies()) {
      size_t *n = static_cast<size_t*>(userArg);
      nsRefPtr<imgRequest> req = entry->GetRequest();
      Image *image = static_cast<Image*>(req->mImage.get());
      if (image) {
        *n += image->HeapSizeOfDecodedWithComputedFallback(ImagesMallocSizeOf);
        *n += image->NonHeapSizeOfDecoded();
      }
    }

    return PL_DHASH_NEXT;
  }
};

// This is used by telemetry.
NS_MEMORY_REPORTER_IMPLEMENT(
  ImagesContentUsedUncompressed,
  "images-content-used-uncompressed",
  KIND_OTHER,
  UNITS_BYTES,
  imgMemoryReporter::GetImagesContentUsedUncompressed,
  "This is the sum of the 'explicit/images/content/used/uncompressed-heap' "
  "and 'explicit/images/content/used/uncompressed-nonheap' numbers.  However, "
  "it is measured at a different time and so may give slightly different "
  "results.")

NS_IMPL_ISUPPORTS1(imgMemoryReporter, nsIMemoryMultiReporter)

NS_IMPL_ISUPPORTS3(nsProgressNotificationProxy,
                     nsIProgressEventSink,
                     nsIChannelEventSink,
                     nsIInterfaceRequestor)

NS_IMETHODIMP
nsProgressNotificationProxy::OnProgress(nsIRequest* request,
                                        nsISupports* ctxt,
                                        PRUint64 progress,
                                        PRUint64 progressMax)
{
  nsCOMPtr<nsILoadGroup> loadGroup;
  request->GetLoadGroup(getter_AddRefs(loadGroup));

  nsCOMPtr<nsIProgressEventSink> target;
  NS_QueryNotificationCallbacks(mOriginalCallbacks,
                                loadGroup,
                                NS_GET_IID(nsIProgressEventSink),
                                getter_AddRefs(target));
  if (!target)
    return NS_OK;
  return target->OnProgress(mImageRequest, ctxt, progress, progressMax);
}

NS_IMETHODIMP
nsProgressNotificationProxy::OnStatus(nsIRequest* request,
                                      nsISupports* ctxt,
                                      nsresult status,
                                      const PRUnichar* statusArg)
{
  nsCOMPtr<nsILoadGroup> loadGroup;
  request->GetLoadGroup(getter_AddRefs(loadGroup));

  nsCOMPtr<nsIProgressEventSink> target;
  NS_QueryNotificationCallbacks(mOriginalCallbacks,
                                loadGroup,
                                NS_GET_IID(nsIProgressEventSink),
                                getter_AddRefs(target));
  if (!target)
    return NS_OK;
  return target->OnStatus(mImageRequest, ctxt, status, statusArg);
}

NS_IMETHODIMP
nsProgressNotificationProxy::AsyncOnChannelRedirect(nsIChannel *oldChannel,
                                                    nsIChannel *newChannel,
                                                    PRUint32 flags,
                                                    nsIAsyncVerifyRedirectCallback *cb)
{
  // Tell the original original callbacks about it too
  nsCOMPtr<nsILoadGroup> loadGroup;
  newChannel->GetLoadGroup(getter_AddRefs(loadGroup));
  nsCOMPtr<nsIChannelEventSink> target;
  NS_QueryNotificationCallbacks(mOriginalCallbacks,
                                loadGroup,
                                NS_GET_IID(nsIChannelEventSink),
                                getter_AddRefs(target));
  if (!target) {
      cb->OnRedirectVerifyCallback(NS_OK);
      return NS_OK;
  }

  // Delegate to |target| if set, reusing |cb|
  return target->AsyncOnChannelRedirect(oldChannel, newChannel, flags, cb);
}

NS_IMETHODIMP
nsProgressNotificationProxy::GetInterface(const nsIID& iid,
                                          void** result)
{
  if (iid.Equals(NS_GET_IID(nsIProgressEventSink))) {
    *result = static_cast<nsIProgressEventSink*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }
  if (iid.Equals(NS_GET_IID(nsIChannelEventSink))) {
    *result = static_cast<nsIChannelEventSink*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }
  if (mOriginalCallbacks)
    return mOriginalCallbacks->GetInterface(iid, result);
  return NS_NOINTERFACE;
}

static void NewRequestAndEntry(bool aForcePrincipalCheckForCacheEntry,
                               imgRequest **aRequest, imgCacheEntry **aEntry)
{
  nsRefPtr<imgRequest> request = new imgRequest();
  nsRefPtr<imgCacheEntry> entry = new imgCacheEntry(request, aForcePrincipalCheckForCacheEntry);
  request.forget(aRequest);
  entry.forget(aEntry);
}

static bool ShouldRevalidateEntry(imgCacheEntry *aEntry,
                              nsLoadFlags aFlags,
                              bool aHasExpired)
{
  bool bValidateEntry = false;

  if (aFlags & nsIRequest::LOAD_BYPASS_CACHE)
    return false;

  if (aFlags & nsIRequest::VALIDATE_ALWAYS) {
    bValidateEntry = true;
  }
  else if (aEntry->GetMustValidate()) {
    bValidateEntry = true;
  }
  //
  // The cache entry has expired...  Determine whether the stale cache
  // entry can be used without validation...
  //
  else if (aHasExpired) {
    //
    // VALIDATE_NEVER and VALIDATE_ONCE_PER_SESSION allow stale cache
    // entries to be used unless they have been explicitly marked to
    // indicate that revalidation is necessary.
    //
    if (aFlags & (nsIRequest::VALIDATE_NEVER | 
                  nsIRequest::VALIDATE_ONCE_PER_SESSION)) 
    {
      bValidateEntry = false;
    }
    //
    // LOAD_FROM_CACHE allows a stale cache entry to be used... Otherwise,
    // the entry must be revalidated.
    //
    else if (!(aFlags & nsIRequest::LOAD_FROM_CACHE)) {
      bValidateEntry = true;
    }
  }

  return bValidateEntry;
}

// Returns true if this request is compatible with the given CORS mode on the
// given loading principal, and false if the request may not be reused due
// to CORS.
static bool
ValidateCORSAndPrincipal(imgRequest* request, bool forcePrincipalCheck,
                         PRInt32 corsmode, nsIPrincipal* loadingPrincipal)
{
  // If the entry's CORS mode doesn't match, or the CORS mode matches but the
  // document principal isn't the same, we can't use this request.
  if (request->GetCORSMode() != corsmode) {
    return false;
  } else if (request->GetCORSMode() != imgIRequest::CORS_NONE ||
             forcePrincipalCheck) {
    nsCOMPtr<nsIPrincipal> otherprincipal = request->GetLoadingPrincipal();

    // If we previously had a principal, but we don't now, we can't use this
    // request.
    if (otherprincipal && !loadingPrincipal) {
      return false;
    }

    if (otherprincipal && loadingPrincipal) {
      bool equals = false;
      otherprincipal->Equals(loadingPrincipal, &equals);
      return equals;
    }
  }

  return true;
}

static nsresult NewImageChannel(nsIChannel **aResult,
                                // If aForcePrincipalCheckForCacheEntry is
                                // true, then we will force a principal check
                                // even when not using CORS before assuming we
                                // have a cache hit on a cache entry that we
                                // create for this channel.  This is an out
                                // param that should be set to true if this
                                // channel ends up depending on
                                // aLoadingPrincipal and false otherwise.
                                bool *aForcePrincipalCheckForCacheEntry,
                                nsIURI *aURI,
                                nsIURI *aInitialDocumentURI,
                                nsIURI *aReferringURI,
                                nsILoadGroup *aLoadGroup,
                                const nsCString& aAcceptHeader,
                                nsLoadFlags aLoadFlags,
                                nsIChannelPolicy *aPolicy,
                                nsIPrincipal *aLoadingPrincipal)
{
  nsresult rv;
  nsCOMPtr<nsIChannel> newChannel;
  nsCOMPtr<nsIHttpChannel> newHttpChannel;
 
  nsCOMPtr<nsIInterfaceRequestor> callbacks;

  if (aLoadGroup) {
    // Get the notification callbacks from the load group for the new channel.
    //
    // XXX: This is not exactly correct, because the network request could be
    //      referenced by multiple windows...  However, the new channel needs
    //      something.  So, using the 'first' notification callbacks is better
    //      than nothing...
    //
    aLoadGroup->GetNotificationCallbacks(getter_AddRefs(callbacks));
  }

  // Pass in a NULL loadgroup because this is the underlying network request.
  // This request may be referenced by several proxy image requests (psossibly
  // in different documents).
  // If all of the proxy requests are canceled then this request should be
  // canceled too.
  //
  rv = NS_NewChannel(aResult,
                     aURI,        // URI 
                     nsnull,      // Cached IOService
                     nsnull,      // LoadGroup
                     callbacks,   // Notification Callbacks
                     aLoadFlags,
                     aPolicy);
  if (NS_FAILED(rv))
    return rv;

  *aForcePrincipalCheckForCacheEntry = false;

  // Initialize HTTP-specific attributes
  newHttpChannel = do_QueryInterface(*aResult);
  if (newHttpChannel) {
    newHttpChannel->SetRequestHeader(NS_LITERAL_CSTRING("Accept"),
                                     aAcceptHeader,
                                     false);

    nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal = do_QueryInterface(newHttpChannel);
    NS_ENSURE_TRUE(httpChannelInternal, NS_ERROR_UNEXPECTED);
    httpChannelInternal->SetDocumentURI(aInitialDocumentURI);
    newHttpChannel->SetReferrer(aReferringURI);
  }

  // Image channels are loaded by default with reduced priority.
  nsCOMPtr<nsISupportsPriority> p = do_QueryInterface(*aResult);
  if (p) {
    PRUint32 priority = nsISupportsPriority::PRIORITY_LOW;

    if (aLoadFlags & nsIRequest::LOAD_BACKGROUND)
      ++priority; // further reduce priority for background loads

    p->AdjustPriority(priority);
  }

  bool setOwner = nsContentUtils::SetUpChannelOwner(aLoadingPrincipal,
                                                      *aResult, aURI, false);
  *aForcePrincipalCheckForCacheEntry = setOwner;

  return NS_OK;
}

static PRUint32 SecondsFromPRTime(PRTime prTime)
{
  return PRUint32(PRInt64(prTime) / PRInt64(PR_USEC_PER_SEC));
}

imgCacheEntry::imgCacheEntry(imgRequest *request, bool forcePrincipalCheck)
 : mRequest(request),
   mDataSize(0),
   mTouchedTime(SecondsFromPRTime(PR_Now())),
   mExpiryTime(0),
   mMustValidate(false),
   // We start off as evicted so we don't try to update the cache. PutIntoCache
   // will set this to false.
   mEvicted(true),
   mHasNoProxies(true),
   mForcePrincipalCheck(forcePrincipalCheck)
{}

imgCacheEntry::~imgCacheEntry()
{
  LOG_FUNC(gImgLog, "imgCacheEntry::~imgCacheEntry()");
}

void imgCacheEntry::Touch(bool updateTime /* = true */)
{
  LOG_SCOPE(gImgLog, "imgCacheEntry::Touch");

  if (updateTime)
    mTouchedTime = SecondsFromPRTime(PR_Now());

  UpdateCache();
}

void imgCacheEntry::UpdateCache(PRInt32 diff /* = 0 */)
{
  // Don't update the cache if we've been removed from it or it doesn't care
  // about our size or usage.
  if (!Evicted() && HasNoProxies()) {
    nsCOMPtr<nsIURI> uri;
    mRequest->GetURI(getter_AddRefs(uri));
    imgLoader::CacheEntriesChanged(uri, diff);
  }
}

void imgCacheEntry::SetHasNoProxies(bool hasNoProxies)
{
#if defined(PR_LOGGING)
  nsCOMPtr<nsIURI> uri;
  mRequest->GetURI(getter_AddRefs(uri));
  nsCAutoString spec;
  if (uri)
    uri->GetSpec(spec);
  if (hasNoProxies)
    LOG_FUNC_WITH_PARAM(gImgLog, "imgCacheEntry::SetHasNoProxies true", "uri", spec.get());
  else
    LOG_FUNC_WITH_PARAM(gImgLog, "imgCacheEntry::SetHasNoProxies false", "uri", spec.get());
#endif

  mHasNoProxies = hasNoProxies;
}

imgCacheQueue::imgCacheQueue()
 : mDirty(false),
   mSize(0)
{}

void imgCacheQueue::UpdateSize(PRInt32 diff)
{
  mSize += diff;
}

PRUint32 imgCacheQueue::GetSize() const
{
  return mSize;
}

#include <algorithm>
using namespace std;

void imgCacheQueue::Remove(imgCacheEntry *entry)
{
  queueContainer::iterator it = find(mQueue.begin(), mQueue.end(), entry);
  if (it != mQueue.end()) {
    mSize -= (*it)->GetDataSize();
    mQueue.erase(it);
    MarkDirty();
  }
}

void imgCacheQueue::Push(imgCacheEntry *entry)
{
  mSize += entry->GetDataSize();

  nsRefPtr<imgCacheEntry> refptr(entry);
  mQueue.push_back(refptr);
  MarkDirty();
}

already_AddRefed<imgCacheEntry> imgCacheQueue::Pop()
{
  if (mQueue.empty())
    return nsnull;
  if (IsDirty())
    Refresh();

  nsRefPtr<imgCacheEntry> entry = mQueue[0];
  std::pop_heap(mQueue.begin(), mQueue.end(), imgLoader::CompareCacheEntries);
  mQueue.pop_back();

  mSize -= entry->GetDataSize();
  imgCacheEntry *ret = entry;
  NS_ADDREF(ret);
  return ret;
}

void imgCacheQueue::Refresh()
{
  std::make_heap(mQueue.begin(), mQueue.end(), imgLoader::CompareCacheEntries);
  mDirty = false;
}

void imgCacheQueue::MarkDirty()
{
  mDirty = true;
}

bool imgCacheQueue::IsDirty()
{
  return mDirty;
}

PRUint32 imgCacheQueue::GetNumElements() const
{
  return mQueue.size();
}

imgCacheQueue::iterator imgCacheQueue::begin()
{
  return mQueue.begin();
}
imgCacheQueue::const_iterator imgCacheQueue::begin() const
{
  return mQueue.begin();
}

imgCacheQueue::iterator imgCacheQueue::end()
{
  return mQueue.end();
}
imgCacheQueue::const_iterator imgCacheQueue::end() const
{
  return mQueue.end();
}

nsresult imgLoader::CreateNewProxyForRequest(imgRequest *aRequest, nsILoadGroup *aLoadGroup,
                                             imgIDecoderObserver *aObserver,
                                             nsLoadFlags aLoadFlags, imgIRequest *aProxyRequest,
                                             imgIRequest **_retval)
{
  LOG_SCOPE_WITH_PARAM(gImgLog, "imgLoader::CreateNewProxyForRequest", "imgRequest", aRequest);

  /* XXX If we move decoding onto separate threads, we should save off the
     calling thread here and pass it off to |proxyRequest| so that it call
     proxy calls to |aObserver|.
   */

  imgRequestProxy *proxyRequest;
  if (aProxyRequest) {
    proxyRequest = static_cast<imgRequestProxy *>(aProxyRequest);
  } else {
    proxyRequest = new imgRequestProxy();
  }
  NS_ADDREF(proxyRequest);

  /* It is important to call |SetLoadFlags()| before calling |Init()| because
     |Init()| adds the request to the loadgroup.
   */
  proxyRequest->SetLoadFlags(aLoadFlags);

  nsCOMPtr<nsIURI> uri;
  aRequest->GetURI(getter_AddRefs(uri));

  // init adds itself to imgRequest's list of observers
  nsresult rv = proxyRequest->Init(aRequest, aLoadGroup, aRequest->mImage, uri, aObserver);
  if (NS_FAILED(rv)) {
    NS_RELEASE(proxyRequest);
    return rv;
  }

  // transfer reference to caller
  *_retval = static_cast<imgIRequest*>(proxyRequest);

  return NS_OK;
}

class imgCacheObserver MOZ_FINAL : public nsIObserver
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
private:
  imgLoader mLoader;
};

NS_IMPL_ISUPPORTS1(imgCacheObserver, nsIObserver)

NS_IMETHODIMP
imgCacheObserver::Observe(nsISupports* aSubject, const char* aTopic, const PRUnichar* aSomeData)
{
  if (strcmp(aTopic, "memory-pressure") == 0) {
    DiscardTracker::DiscardAll();
    mLoader.MinimizeCaches();
  } else if (strcmp(aTopic, "chrome-flush-skin-caches") == 0 ||
             strcmp(aTopic, "chrome-flush-caches") == 0) {
    mLoader.ClearChromeImageCache();
  }
  return NS_OK;
}

class imgCacheExpirationTracker MOZ_FINAL
  : public nsExpirationTracker<imgCacheEntry, 3>
{
  enum { TIMEOUT_SECONDS = 10 };
public:
  imgCacheExpirationTracker();

protected:
  void NotifyExpired(imgCacheEntry *entry);
};

imgCacheExpirationTracker::imgCacheExpirationTracker()
 : nsExpirationTracker<imgCacheEntry, 3>(TIMEOUT_SECONDS * 1000)
{}

void imgCacheExpirationTracker::NotifyExpired(imgCacheEntry *entry)
{
  // Hold on to a reference to this entry, because the expiration tracker
  // mechanism doesn't.
  nsRefPtr<imgCacheEntry> kungFuDeathGrip(entry);

#if defined(PR_LOGGING)
  nsRefPtr<imgRequest> req(entry->GetRequest());
  if (req) {
    nsCOMPtr<nsIURI> uri;
    req->GetURI(getter_AddRefs(uri));
    nsCAutoString spec;
    uri->GetSpec(spec);
    LOG_FUNC_WITH_PARAM(gImgLog, "imgCacheExpirationTracker::NotifyExpired", "entry", spec.get());
  }
#endif

  // We can be called multiple times on the same entry. Don't do work multiple
  // times.
  if (!entry->Evicted())
    imgLoader::RemoveFromCache(entry);

  imgLoader::VerifyCacheSizes();
}

imgCacheObserver *gCacheObserver;
imgCacheExpirationTracker *gCacheTracker;

imgLoader::imgCacheTable imgLoader::sCache;
imgCacheQueue imgLoader::sCacheQueue;

imgLoader::imgCacheTable imgLoader::sChromeCache;
imgCacheQueue imgLoader::sChromeCacheQueue;

PRFloat64 imgLoader::sCacheTimeWeight;
PRUint32 imgLoader::sCacheMaxSize;

NS_IMPL_ISUPPORTS5(imgLoader, imgILoader, nsIContentSniffer, imgICache, nsISupportsWeakReference, nsIObserver)

imgLoader::imgLoader()
{
  /* member initializers and constructor code */
}

imgLoader::~imgLoader()
{
  /* destructor code */
}

void imgLoader::VerifyCacheSizes()
{
#ifdef DEBUG
  if (!gCacheTracker)
    return;

  PRUint32 cachesize = sCache.Count() + sChromeCache.Count();
  PRUint32 queuesize = sCacheQueue.GetNumElements() + sChromeCacheQueue.GetNumElements();
  PRUint32 trackersize = 0;
  for (nsExpirationTracker<imgCacheEntry, 3>::Iterator it(gCacheTracker); it.Next(); )
    trackersize++;
  NS_ABORT_IF_FALSE(queuesize == trackersize, "Queue and tracker sizes out of sync!");
  NS_ABORT_IF_FALSE(queuesize <= cachesize, "Queue has more elements than cache!");
#endif
}

imgLoader::imgCacheTable & imgLoader::GetCache(nsIURI *aURI)
{
  bool chrome = false;
  aURI->SchemeIs("chrome", &chrome);
  if (chrome)
    return sChromeCache;
  else
    return sCache;
}

imgCacheQueue & imgLoader::GetCacheQueue(nsIURI *aURI)
{
  bool chrome = false;
  aURI->SchemeIs("chrome", &chrome);
  if (chrome)
    return sChromeCacheQueue;
  else
    return sCacheQueue;
}

nsresult imgLoader::InitCache()
{
  NS_TIME_FUNCTION;

  nsresult rv;
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (!os)
    return NS_ERROR_FAILURE;
  
  gCacheObserver = new imgCacheObserver();
  NS_ADDREF(gCacheObserver);

  os->AddObserver(gCacheObserver, "memory-pressure", false);
  os->AddObserver(gCacheObserver, "chrome-flush-skin-caches", false);
  os->AddObserver(gCacheObserver, "chrome-flush-caches", false);

  gCacheTracker = new imgCacheExpirationTracker();

  sCache.Init();
  sChromeCache.Init();

  PRInt32 timeweight;
  rv = Preferences::GetInt("image.cache.timeweight", &timeweight);
  if (NS_SUCCEEDED(rv))
    sCacheTimeWeight = timeweight / 1000.0;
  else
    sCacheTimeWeight = 0.5;

  PRInt32 cachesize;
  rv = Preferences::GetInt("image.cache.size", &cachesize);
  if (NS_SUCCEEDED(rv))
    sCacheMaxSize = cachesize;
  else
    sCacheMaxSize = 5 * 1024 * 1024;

  NS_RegisterMemoryMultiReporter(new imgMemoryReporter());
  NS_RegisterMemoryReporter(new NS_MEMORY_REPORTER_NAME(ImagesContentUsedUncompressed));
  
  return NS_OK;
}

nsresult imgLoader::Init()
{
  ReadAcceptHeaderPref();

  Preferences::AddWeakObserver(this, "image.http.accept");

  // Listen for when we leave private browsing mode
  nsCOMPtr<nsIObserverService> obService = mozilla::services::GetObserverService();
  if (obService)
    obService->AddObserver(this, NS_PRIVATE_BROWSING_SWITCH_TOPIC, true);

  return NS_OK;
}

NS_IMETHODIMP
imgLoader::Observe(nsISupports* aSubject, const char* aTopic, const PRUnichar* aData)
{
  // We listen for pref change notifications...
  if (!strcmp(aTopic, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID)) {
    if (!strcmp(NS_ConvertUTF16toUTF8(aData).get(), "image.http.accept")) {
      ReadAcceptHeaderPref();
    }
  }

  // ...and exits from private browsing.
  else if (!strcmp(aTopic, NS_PRIVATE_BROWSING_SWITCH_TOPIC)) {
    if (NS_LITERAL_STRING(NS_PRIVATE_BROWSING_LEAVE).Equals(aData))
      ClearImageCache();
  }

  // (Nothing else should bring us here)
  else {
    NS_ABORT_IF_FALSE(0, "Invalid topic received");
  }

  return NS_OK;
}

void imgLoader::ReadAcceptHeaderPref()
{
  nsAdoptingCString accept = Preferences::GetCString("image.http.accept");
  if (accept)
    mAcceptHeader = accept;
  else
    mAcceptHeader = "image/png,image/*;q=0.8,*/*;q=0.5";
}

/* void clearCache (in boolean chrome); */
NS_IMETHODIMP imgLoader::ClearCache(bool chrome)
{
  if (chrome)
    return ClearChromeImageCache();
  else
    return ClearImageCache();
}

/* void removeEntry(in nsIURI uri); */
NS_IMETHODIMP imgLoader::RemoveEntry(nsIURI *uri)
{
  if (RemoveFromCache(uri))
    return NS_OK;

  return NS_ERROR_NOT_AVAILABLE;
}

/* imgIRequest findEntry(in nsIURI uri); */
NS_IMETHODIMP imgLoader::FindEntryProperties(nsIURI *uri, nsIProperties **_retval)
{
  nsRefPtr<imgCacheEntry> entry;
  nsCAutoString spec;
  imgCacheTable &cache = GetCache(uri);

  uri->GetSpec(spec);
  *_retval = nsnull;

  if (cache.Get(spec, getter_AddRefs(entry)) && entry) {
    if (gCacheTracker && entry->HasNoProxies())
      gCacheTracker->MarkUsed(entry);

    nsRefPtr<imgRequest> request = getter_AddRefs(entry->GetRequest());
    if (request) {
      *_retval = request->Properties();
      NS_ADDREF(*_retval);
    }
  }

  return NS_OK;
}

void imgLoader::Shutdown()
{
  ClearChromeImageCache();
  ClearImageCache();
  NS_IF_RELEASE(gCacheObserver);
  delete gCacheTracker;
  gCacheTracker = nsnull;
}

nsresult imgLoader::ClearChromeImageCache()
{
  return EvictEntries(sChromeCache);
}

nsresult imgLoader::ClearImageCache()
{
  return EvictEntries(sCache);
}

void imgLoader::MinimizeCaches()
{
  EvictEntries(sCacheQueue);
  EvictEntries(sChromeCacheQueue);
}

bool imgLoader::PutIntoCache(nsIURI *key, imgCacheEntry *entry)
{
  imgCacheTable &cache = GetCache(key);

  nsCAutoString spec;
  key->GetSpec(spec);

  LOG_STATIC_FUNC_WITH_PARAM(gImgLog, "imgLoader::PutIntoCache", "uri", spec.get());

  // Check to see if this request already exists in the cache and is being
  // loaded on a different thread. If so, don't allow this entry to be added to
  // the cache.
  nsRefPtr<imgCacheEntry> tmpCacheEntry;
  if (cache.Get(spec, getter_AddRefs(tmpCacheEntry)) && tmpCacheEntry) {
    PR_LOG(gImgLog, PR_LOG_DEBUG,
           ("[this=%p] imgLoader::PutIntoCache -- Element already in the cache", nsnull));
    nsRefPtr<imgRequest> tmpRequest = getter_AddRefs(tmpCacheEntry->GetRequest());

    // If it already exists, and we're putting the same key into the cache, we
    // should remove the old version.
    PR_LOG(gImgLog, PR_LOG_DEBUG,
           ("[this=%p] imgLoader::PutIntoCache -- Replacing cached element", nsnull));

    RemoveFromCache(key);
  } else {
    PR_LOG(gImgLog, PR_LOG_DEBUG,
           ("[this=%p] imgLoader::PutIntoCache -- Element NOT already in the cache", nsnull));
  }

  cache.Put(spec, entry);

  // We can be called to resurrect an evicted entry.
  if (entry->Evicted())
    entry->SetEvicted(false);

  // If we're resurrecting an entry with no proxies, put it back in the
  // tracker and queue.
  if (entry->HasNoProxies()) {
    nsresult addrv = NS_OK;

    if (gCacheTracker)
      addrv = gCacheTracker->AddObject(entry);

    if (NS_SUCCEEDED(addrv)) {
      imgCacheQueue &queue = GetCacheQueue(key);
      queue.Push(entry);
    }
  }

  nsRefPtr<imgRequest> request(getter_AddRefs(entry->GetRequest()));
  request->SetIsInCache(true);

  return true;
}

bool imgLoader::SetHasNoProxies(nsIURI *key, imgCacheEntry *entry)
{
#if defined(PR_LOGGING)
  nsCAutoString spec;
  key->GetSpec(spec);

  LOG_STATIC_FUNC_WITH_PARAM(gImgLog, "imgLoader::SetHasNoProxies", "uri", spec.get());
#endif

  if (entry->Evicted())
    return false;

  imgCacheQueue &queue = GetCacheQueue(key);

  nsresult addrv = NS_OK;

  if (gCacheTracker)
    addrv = gCacheTracker->AddObject(entry);

  if (NS_SUCCEEDED(addrv)) {
    queue.Push(entry);
    entry->SetHasNoProxies(true);
  }

  imgCacheTable &cache = GetCache(key);
  CheckCacheLimits(cache, queue);

  return true;
}

bool imgLoader::SetHasProxies(nsIURI *key)
{
  VerifyCacheSizes();

  imgCacheTable &cache = GetCache(key);

  nsCAutoString spec;
  key->GetSpec(spec);

  LOG_STATIC_FUNC_WITH_PARAM(gImgLog, "imgLoader::SetHasProxies", "uri", spec.get());

  nsRefPtr<imgCacheEntry> entry;
  if (cache.Get(spec, getter_AddRefs(entry)) && entry && entry->HasNoProxies()) {
    imgCacheQueue &queue = GetCacheQueue(key);
    queue.Remove(entry);

    if (gCacheTracker)
      gCacheTracker->RemoveObject(entry);

    entry->SetHasNoProxies(false);

    return true;
  }

  return false;
}

void imgLoader::CacheEntriesChanged(nsIURI *uri, PRInt32 sizediff /* = 0 */)
{
  imgCacheQueue &queue = GetCacheQueue(uri);
  queue.MarkDirty();
  queue.UpdateSize(sizediff);
}

void imgLoader::CheckCacheLimits(imgCacheTable &cache, imgCacheQueue &queue)
{
  if (queue.GetNumElements() == 0)
    NS_ASSERTION(queue.GetSize() == 0, 
                 "imgLoader::CheckCacheLimits -- incorrect cache size");

  // Remove entries from the cache until we're back under our desired size.
  while (queue.GetSize() >= sCacheMaxSize) {
    // Remove the first entry in the queue.
    nsRefPtr<imgCacheEntry> entry(queue.Pop());

    NS_ASSERTION(entry, "imgLoader::CheckCacheLimits -- NULL entry pointer");

#if defined(PR_LOGGING)
    nsRefPtr<imgRequest> req(entry->GetRequest());
    if (req) {
      nsCOMPtr<nsIURI> uri;
      req->GetURI(getter_AddRefs(uri));
      nsCAutoString spec;
      uri->GetSpec(spec);
      LOG_STATIC_FUNC_WITH_PARAM(gImgLog, "imgLoader::CheckCacheLimits", "entry", spec.get());
    }
#endif

    if (entry)
      RemoveFromCache(entry);
  }
}

bool imgLoader::ValidateRequestWithNewChannel(imgRequest *request,
                                                nsIURI *aURI,
                                                nsIURI *aInitialDocumentURI,
                                                nsIURI *aReferrerURI,
                                                nsILoadGroup *aLoadGroup,
                                                imgIDecoderObserver *aObserver,
                                                nsISupports *aCX,
                                                nsLoadFlags aLoadFlags,
                                                imgIRequest *aExistingRequest,
                                                imgIRequest **aProxyRequest,
                                                nsIChannelPolicy *aPolicy,
                                                nsIPrincipal* aLoadingPrincipal,
                                                PRInt32 aCORSMode)
{
  // now we need to insert a new channel request object inbetween the real
  // request and the proxy that basically delays loading the image until it
  // gets a 304 or figures out that this needs to be a new request

  nsresult rv;

  // If we're currently in the middle of validating this request, just hand
  // back a proxy to it; the required work will be done for us.
  if (request->mValidator) {
    rv = CreateNewProxyForRequest(request, aLoadGroup, aObserver,
                                  aLoadFlags, aExistingRequest, 
                                  reinterpret_cast<imgIRequest **>(aProxyRequest));
    if (NS_FAILED(rv)) {
      return false;
    }

    if (*aProxyRequest) {
      imgRequestProxy* proxy = static_cast<imgRequestProxy*>(*aProxyRequest);

      // We will send notifications from imgCacheValidator::OnStartRequest().
      // In the mean time, we must defer notifications because we are added to
      // the imgRequest's proxy list, and we can get extra notifications
      // resulting from methods such as RequestDecode(). See bug 579122.
      proxy->SetNotificationsDeferred(true);

      // Attach the proxy without notifying
      request->mValidator->AddProxy(proxy);
    }

    return NS_SUCCEEDED(rv);

  } else {
    // We will rely on Necko to cache this request when it's possible, and to
    // tell imgCacheValidator::OnStartRequest whether the request came from its
    // cache.
    nsCOMPtr<nsIChannel> newChannel;
    bool forcePrincipalCheck;
    rv = NewImageChannel(getter_AddRefs(newChannel),
                         &forcePrincipalCheck,
                         aURI,
                         aInitialDocumentURI,
                         aReferrerURI,
                         aLoadGroup,
                         mAcceptHeader,
                         aLoadFlags,
                         aPolicy,
                         aLoadingPrincipal);
    if (NS_FAILED(rv)) {
      return false;
    }

    nsCOMPtr<imgIRequest> req;
    rv = CreateNewProxyForRequest(request, aLoadGroup, aObserver,
                                  aLoadFlags, aExistingRequest, getter_AddRefs(req));
    if (NS_FAILED(rv)) {
      return false;
    }

    // Make sure that OnStatus/OnProgress calls have the right request set...
    nsRefPtr<nsProgressNotificationProxy> progressproxy =
        new nsProgressNotificationProxy(newChannel, req);
    if (!progressproxy)
      return false;

    nsRefPtr<imgCacheValidator> hvc =
      new imgCacheValidator(progressproxy, request, aCX, forcePrincipalCheck);

    nsCOMPtr<nsIStreamListener> listener = hvc.get();

    // We must set the notification callbacks before setting up the
    // CORS listener, because that's also interested inthe
    // notification callbacks.
    newChannel->SetNotificationCallbacks(hvc);

    if (aCORSMode != imgIRequest::CORS_NONE) {
      bool withCredentials = aCORSMode == imgIRequest::CORS_USE_CREDENTIALS;
      nsCOMPtr<nsIStreamListener> corsproxy =
        new nsCORSListenerProxy(hvc, aLoadingPrincipal, newChannel, withCredentials, &rv);
      if (NS_FAILED(rv)) {
        return false;
      }

      listener = corsproxy;
    }

    request->mValidator = hvc;

    imgRequestProxy* proxy = static_cast<imgRequestProxy*>
                               (static_cast<imgIRequest*>(req.get()));

    // We will send notifications from imgCacheValidator::OnStartRequest().
    // In the mean time, we must defer notifications because we are added to
    // the imgRequest's proxy list, and we can get extra notifications
    // resulting from methods such as RequestDecode(). See bug 579122.
    proxy->SetNotificationsDeferred(true);

    // Add the proxy without notifying
    hvc->AddProxy(proxy);

    rv = newChannel->AsyncOpen(listener, nsnull);
    if (NS_SUCCEEDED(rv))
      NS_ADDREF(*aProxyRequest = req.get());

    return NS_SUCCEEDED(rv);
  }
}

bool imgLoader::ValidateEntry(imgCacheEntry *aEntry,
                                nsIURI *aURI,
                                nsIURI *aInitialDocumentURI,
                                nsIURI *aReferrerURI,
                                nsILoadGroup *aLoadGroup,
                                imgIDecoderObserver *aObserver,
                                nsISupports *aCX,
                                nsLoadFlags aLoadFlags,
                                bool aCanMakeNewChannel,
                                imgIRequest *aExistingRequest,
                                imgIRequest **aProxyRequest,
                                nsIChannelPolicy *aPolicy,
                                nsIPrincipal* aLoadingPrincipal,
                                PRInt32 aCORSMode)
{
  LOG_SCOPE(gImgLog, "imgLoader::ValidateEntry");

  bool hasExpired;
  PRUint32 expirationTime = aEntry->GetExpiryTime();
  if (expirationTime <= SecondsFromPRTime(PR_Now())) {
    hasExpired = true;
  } else {
    hasExpired = false;
  }

  nsresult rv;

  // Special treatment for file URLs - aEntry has expired if file has changed
  nsCOMPtr<nsIFileURL> fileUrl(do_QueryInterface(aURI));
  if (fileUrl) {
    PRUint32 lastModTime = aEntry->GetTouchedTime();

    nsCOMPtr<nsIFile> theFile;
    rv = fileUrl->GetFile(getter_AddRefs(theFile));
    if (NS_SUCCEEDED(rv)) {
      PRInt64 fileLastMod;
      rv = theFile->GetLastModifiedTime(&fileLastMod);
      if (NS_SUCCEEDED(rv)) {
        // nsIFile uses millisec, NSPR usec
        fileLastMod *= 1000;
        hasExpired = SecondsFromPRTime((PRTime)fileLastMod) > lastModTime;
      }
    }
  }

  nsRefPtr<imgRequest> request(aEntry->GetRequest());

  if (!request)
    return false;

  if (!ValidateCORSAndPrincipal(request, aEntry->ForcePrincipalCheck(),
                                aCORSMode, aLoadingPrincipal))
    return false;

  bool validateRequest = false;

  // If the request's loadId is the same as the aCX, then it is ok to use
  // this one because it has already been validated for this context.
  //
  // XXX: nsnull seems to be a 'special' key value that indicates that NO
  //      validation is required.
  //
  void *key = (void *)aCX;
  if (request->mLoadId != key) {
    // If we would need to revalidate this entry, but we're being told to
    // bypass the cache, we don't allow this entry to be used.
    if (aLoadFlags & nsIRequest::LOAD_BYPASS_CACHE)
      return false;

    // Determine whether the cache aEntry must be revalidated...
    validateRequest = ShouldRevalidateEntry(aEntry, aLoadFlags, hasExpired);

    PR_LOG(gImgLog, PR_LOG_DEBUG,
           ("imgLoader::ValidateEntry validating cache entry. " 
            "validateRequest = %d", validateRequest));
  }
#if defined(PR_LOGGING)
  else if (!key) {
    nsCAutoString spec;
    aURI->GetSpec(spec);

    PR_LOG(gImgLog, PR_LOG_DEBUG,
           ("imgLoader::ValidateEntry BYPASSING cache validation for %s " 
            "because of NULL LoadID", spec.get()));
  }
#endif

  // We can't use a cached request if it comes from a different
  // application cache than this load is expecting.
  nsCOMPtr<nsIApplicationCacheContainer> appCacheContainer;
  nsCOMPtr<nsIApplicationCache> requestAppCache;
  nsCOMPtr<nsIApplicationCache> groupAppCache;
  if ((appCacheContainer = do_GetInterface(request->mRequest)))
    appCacheContainer->GetApplicationCache(getter_AddRefs(requestAppCache));
  if ((appCacheContainer = do_QueryInterface(aLoadGroup)))
    appCacheContainer->GetApplicationCache(getter_AddRefs(groupAppCache));

  if (requestAppCache != groupAppCache) {
    PR_LOG(gImgLog, PR_LOG_DEBUG,
           ("imgLoader::ValidateEntry - Unable to use cached imgRequest "
            "[request=%p] because of mismatched application caches\n",
            address_of(request)));
    return false;
  }

  if (validateRequest && aCanMakeNewChannel) {
    LOG_SCOPE(gImgLog, "imgLoader::ValidateRequest |cache hit| must validate");

    return ValidateRequestWithNewChannel(request, aURI, aInitialDocumentURI,
                                         aReferrerURI, aLoadGroup, aObserver,
                                         aCX, aLoadFlags, aExistingRequest,
                                         aProxyRequest, aPolicy,
                                         aLoadingPrincipal, aCORSMode);
  }

  return !validateRequest;
}


bool imgLoader::RemoveFromCache(nsIURI *aKey)
{
  if (!aKey) return false;

  imgCacheTable &cache = GetCache(aKey);
  imgCacheQueue &queue = GetCacheQueue(aKey);

  nsCAutoString spec;
  aKey->GetSpec(spec);

  LOG_STATIC_FUNC_WITH_PARAM(gImgLog, "imgLoader::RemoveFromCache", "uri", spec.get());

  nsRefPtr<imgCacheEntry> entry;
  if (cache.Get(spec, getter_AddRefs(entry)) && entry) {
    cache.Remove(spec);

    NS_ABORT_IF_FALSE(!entry->Evicted(), "Evicting an already-evicted cache entry!");

    // Entries with no proxies are in the tracker.
    if (entry->HasNoProxies()) {
      if (gCacheTracker)
        gCacheTracker->RemoveObject(entry);
      queue.Remove(entry);
    }

    entry->SetEvicted(true);

    nsRefPtr<imgRequest> request(getter_AddRefs(entry->GetRequest()));
    request->SetIsInCache(false);

    return true;
  }
  else
    return false;
}

bool imgLoader::RemoveFromCache(imgCacheEntry *entry)
{
  LOG_STATIC_FUNC(gImgLog, "imgLoader::RemoveFromCache entry");

  nsRefPtr<imgRequest> request(getter_AddRefs(entry->GetRequest()));
  if (request) {
    nsCOMPtr<nsIURI> key;
    if (NS_SUCCEEDED(request->GetURI(getter_AddRefs(key))) && key) {
      imgCacheTable &cache = GetCache(key);
      imgCacheQueue &queue = GetCacheQueue(key);
      nsCAutoString spec;
      key->GetSpec(spec);

      LOG_STATIC_FUNC_WITH_PARAM(gImgLog, "imgLoader::RemoveFromCache", "entry's uri", spec.get());

      cache.Remove(spec);

      if (entry->HasNoProxies()) {
        LOG_STATIC_FUNC(gImgLog, "imgLoader::RemoveFromCache removing from tracker");
        if (gCacheTracker)
          gCacheTracker->RemoveObject(entry);
        queue.Remove(entry);
      }

      entry->SetEvicted(true);
      request->SetIsInCache(false);

      return true;
    }
  }

  return false;
}

static PLDHashOperator EnumEvictEntries(const nsACString&, 
                                        nsRefPtr<imgCacheEntry> &aData,
                                        void *data)
{
  nsTArray<nsRefPtr<imgCacheEntry> > *entries = 
    reinterpret_cast<nsTArray<nsRefPtr<imgCacheEntry> > *>(data);

  entries->AppendElement(aData);

  return PL_DHASH_NEXT;
}

nsresult imgLoader::EvictEntries(imgCacheTable &aCacheToClear)
{
  LOG_STATIC_FUNC(gImgLog, "imgLoader::EvictEntries table");

  // We have to make a temporary, since RemoveFromCache removes the element
  // from the queue, invalidating iterators.
  nsTArray<nsRefPtr<imgCacheEntry> > entries;
  aCacheToClear.Enumerate(EnumEvictEntries, &entries);

  for (PRUint32 i = 0; i < entries.Length(); ++i)
    if (!RemoveFromCache(entries[i]))
      return NS_ERROR_FAILURE;

  return NS_OK;
}

nsresult imgLoader::EvictEntries(imgCacheQueue &aQueueToClear)
{
  LOG_STATIC_FUNC(gImgLog, "imgLoader::EvictEntries queue");

  // We have to make a temporary, since RemoveFromCache removes the element
  // from the queue, invalidating iterators.
  nsTArray<nsRefPtr<imgCacheEntry> > entries(aQueueToClear.GetNumElements());
  for (imgCacheQueue::const_iterator i = aQueueToClear.begin(); i != aQueueToClear.end(); ++i)
    entries.AppendElement(*i);

  for (PRUint32 i = 0; i < entries.Length(); ++i)
    if (!RemoveFromCache(entries[i]))
      return NS_ERROR_FAILURE;

  return NS_OK;
}

#define LOAD_FLAGS_CACHE_MASK    (nsIRequest::LOAD_BYPASS_CACHE | \
                                  nsIRequest::LOAD_FROM_CACHE)

#define LOAD_FLAGS_VALIDATE_MASK (nsIRequest::VALIDATE_ALWAYS |   \
                                  nsIRequest::VALIDATE_NEVER |    \
                                  nsIRequest::VALIDATE_ONCE_PER_SESSION)


/* imgIRequest loadImage (in nsIURI aURI, in nsIURI initialDocumentURI, in nsIPrincipal loadingPrincipal, in nsILoadGroup aLoadGroup, in imgIDecoderObserver aObserver, in nsISupports aCX, in nsLoadFlags aLoadFlags, in nsISupports cacheKey, in imgIRequest aRequest); */

NS_IMETHODIMP imgLoader::LoadImage(nsIURI *aURI, 
                                   nsIURI *aInitialDocumentURI,
                                   nsIURI *aReferrerURI,
                                   nsIPrincipal* aLoadingPrincipal,
                                   nsILoadGroup *aLoadGroup,
                                   imgIDecoderObserver *aObserver,
                                   nsISupports *aCX,
                                   nsLoadFlags aLoadFlags,
                                   nsISupports *aCacheKey,
                                   imgIRequest *aRequest,
                                   nsIChannelPolicy *aPolicy,
                                   imgIRequest **_retval)
{
  VerifyCacheSizes();

  NS_ASSERTION(aURI, "imgLoader::LoadImage -- NULL URI pointer");

  if (!aURI)
    return NS_ERROR_NULL_POINTER;

  nsCAutoString spec;
  aURI->GetSpec(spec);
  LOG_SCOPE_WITH_PARAM(gImgLog, "imgLoader::LoadImage", "aURI", spec.get());

  *_retval = nsnull;

  nsRefPtr<imgRequest> request;

  nsresult rv;
  nsLoadFlags requestFlags = nsIRequest::LOAD_NORMAL;

  // Get the default load flags from the loadgroup (if possible)...
  if (aLoadGroup) {
    aLoadGroup->GetLoadFlags(&requestFlags);
  }
  //
  // Merge the default load flags with those passed in via aLoadFlags.
  // Currently, *only* the caching, validation and background load flags
  // are merged...
  //
  // The flags in aLoadFlags take precedence over the default flags!
  //
  if (aLoadFlags & LOAD_FLAGS_CACHE_MASK) {
    // Override the default caching flags...
    requestFlags = (requestFlags & ~LOAD_FLAGS_CACHE_MASK) |
                   (aLoadFlags & LOAD_FLAGS_CACHE_MASK);
  }
  if (aLoadFlags & LOAD_FLAGS_VALIDATE_MASK) {
    // Override the default validation flags...
    requestFlags = (requestFlags & ~LOAD_FLAGS_VALIDATE_MASK) |
                   (aLoadFlags & LOAD_FLAGS_VALIDATE_MASK);
  }
  if (aLoadFlags & nsIRequest::LOAD_BACKGROUND) {
    // Propagate background loading...
    requestFlags |= nsIRequest::LOAD_BACKGROUND;
  }

  PRInt32 corsmode = imgIRequest::CORS_NONE;
  if (aLoadFlags & imgILoader::LOAD_CORS_ANONYMOUS) {
    corsmode = imgIRequest::CORS_ANONYMOUS;
  } else if (aLoadFlags & imgILoader::LOAD_CORS_USE_CREDENTIALS) {
    corsmode = imgIRequest::CORS_USE_CREDENTIALS;
  }

  nsRefPtr<imgCacheEntry> entry;

  // Look in the cache for our URI, and then validate it.
  // XXX For now ignore aCacheKey. We will need it in the future
  // for correctly dealing with image load requests that are a result
  // of post data.
  imgCacheTable &cache = GetCache(aURI);

  if (cache.Get(spec, getter_AddRefs(entry)) && entry) {
    if (ValidateEntry(entry, aURI, aInitialDocumentURI, aReferrerURI,
                      aLoadGroup, aObserver, aCX, requestFlags, true,
                      aRequest, _retval, aPolicy, aLoadingPrincipal, corsmode)) {
      request = getter_AddRefs(entry->GetRequest());

      // If this entry has no proxies, its request has no reference to the entry.
      if (entry->HasNoProxies()) {
        LOG_FUNC_WITH_PARAM(gImgLog, "imgLoader::LoadImage() adding proxyless entry", "uri", spec.get());
        NS_ABORT_IF_FALSE(!request->HasCacheEntry(), "Proxyless entry's request has cache entry!");
        request->SetCacheEntry(entry);

        if (gCacheTracker)
          gCacheTracker->MarkUsed(entry);
      } 

      entry->Touch();

#ifdef DEBUG_joe
      printf("CACHEGET: %d %s %d\n", time(NULL), spec.get(), entry->SizeOfData());
#endif
    }
    else {
      // We can't use this entry. We'll try to load it off the network, and if
      // successful, overwrite the old entry in the cache with a new one.
      entry = nsnull;
    }
  }

  // Keep the channel in this scope, so we can adjust its notificationCallbacks
  // later when we create the proxy.
  nsCOMPtr<nsIChannel> newChannel;
  // If we didn't get a cache hit, we need to load from the network.
  if (!request) {
    LOG_SCOPE(gImgLog, "imgLoader::LoadImage |cache miss|");

    bool forcePrincipalCheck;
    rv = NewImageChannel(getter_AddRefs(newChannel),
                         &forcePrincipalCheck,
                         aURI,
                         aInitialDocumentURI,
                         aReferrerURI,
                         aLoadGroup,
                         mAcceptHeader,
                         requestFlags,
                         aPolicy,
                         aLoadingPrincipal);
    if (NS_FAILED(rv))
      return NS_ERROR_FAILURE;

    NewRequestAndEntry(forcePrincipalCheck, getter_AddRefs(request),
                       getter_AddRefs(entry));

    PR_LOG(gImgLog, PR_LOG_DEBUG,
           ("[this=%p] imgLoader::LoadImage -- Created new imgRequest [request=%p]\n", this, request.get()));

    // Create a loadgroup for this new channel.  This way if the channel
    // is redirected, we'll have a way to cancel the resulting channel.
    nsCOMPtr<nsILoadGroup> loadGroup =
        do_CreateInstance(NS_LOADGROUP_CONTRACTID);
    newChannel->SetLoadGroup(loadGroup);

    request->Init(aURI, aURI, loadGroup, newChannel, entry, aCX,
                  aLoadingPrincipal, corsmode);

    // Pass the inner window ID of the loading document, if possible.
    nsCOMPtr<nsIDocument> doc = do_QueryInterface(aCX);
    if (doc) {
      request->SetInnerWindowID(doc->InnerWindowID());
    }

    // create the proxy listener
    nsCOMPtr<nsIStreamListener> pl = new ProxyListener(request.get());

    // See if we need to insert a CORS proxy between the proxy listener and the
    // request.
    nsCOMPtr<nsIStreamListener> listener = pl;
    if (corsmode != imgIRequest::CORS_NONE) {
      PR_LOG(gImgLog, PR_LOG_DEBUG,
             ("[this=%p] imgLoader::LoadImage -- Setting up a CORS load",
              this));
      bool withCredentials = corsmode == imgIRequest::CORS_USE_CREDENTIALS;

      nsCOMPtr<nsIStreamListener> corsproxy =
        new nsCORSListenerProxy(pl, aLoadingPrincipal, newChannel,
                                withCredentials, &rv);
      if (NS_FAILED(rv)) {
        PR_LOG(gImgLog, PR_LOG_DEBUG,
               ("[this=%p] imgLoader::LoadImage -- nsCORSListenerProxy "
                "creation failed: 0x%x\n", this, rv));
        request->CancelAndAbort(rv);
        return NS_ERROR_FAILURE;
      }

      listener = corsproxy;
    }

    PR_LOG(gImgLog, PR_LOG_DEBUG,
           ("[this=%p] imgLoader::LoadImage -- Calling channel->AsyncOpen()\n", this));

    nsresult openRes = newChannel->AsyncOpen(listener, nsnull);

    if (NS_FAILED(openRes)) {
      PR_LOG(gImgLog, PR_LOG_DEBUG,
             ("[this=%p] imgLoader::LoadImage -- AsyncOpen() failed: 0x%x\n",
              this, openRes));
      request->CancelAndAbort(openRes);
      return openRes;
    }

    // Try to add the new request into the cache.
    PutIntoCache(aURI, entry);
  } else {
    LOG_MSG_WITH_PARAM(gImgLog, 
                       "imgLoader::LoadImage |cache hit|", "request", request);
  }


  // If we didn't get a proxy when validating the cache entry, we need to create one.
  if (!*_retval) {
    // ValidateEntry() has three return values: "Is valid," "might be valid --
    // validating over network", and "not valid." If we don't have a _retval,
    // we know ValidateEntry is not validating over the network, so it's safe
    // to SetLoadId here because we know this request is valid for this context.
    //
    // Note, however, that this doesn't guarantee the behaviour we want (one
    // URL maps to the same image on a page) if we load the same image in a
    // different tab (see bug 528003), because its load id will get re-set, and
    // that'll cause us to validate over the network.
    request->SetLoadId(aCX);

    LOG_MSG(gImgLog, "imgLoader::LoadImage", "creating proxy request.");
    rv = CreateNewProxyForRequest(request, aLoadGroup, aObserver,
                                  requestFlags, aRequest, _retval);
    if (NS_FAILED(rv)) {
      return rv;
    }

    imgRequestProxy *proxy = static_cast<imgRequestProxy *>(*_retval);

    // Make sure that OnStatus/OnProgress calls have the right request set, if
    // we did create a channel here.
    if (newChannel) {
      nsCOMPtr<nsIInterfaceRequestor> requestor(
          new nsProgressNotificationProxy(newChannel, proxy));
      if (!requestor)
        return NS_ERROR_OUT_OF_MEMORY;
      newChannel->SetNotificationCallbacks(requestor);
    }

    // Note that it's OK to add here even if the request is done.  If it is,
    // it'll send a OnStopRequest() to the proxy in imgRequestProxy::Notify and
    // the proxy will be removed from the loadgroup.
    proxy->AddToLoadGroup();

    // If we're loading off the network, explicitly don't notify our proxy,
    // because necko (or things called from necko, such as imgCacheValidator)
    // are going to call our notifications asynchronously, and we can't make it
    // further asynchronous because observers might rely on imagelib completing
    // its work between the channel's OnStartRequest and OnStopRequest.
    if (!newChannel)
      proxy->NotifyListener();

    return rv;
  }

  NS_ASSERTION(*_retval, "imgLoader::LoadImage -- no return value");

  return NS_OK;
}

/* imgIRequest loadImageWithChannel(in nsIChannel channel, in imgIDecoderObserver aObserver, in nsISupports cx, out nsIStreamListener); */
NS_IMETHODIMP imgLoader::LoadImageWithChannel(nsIChannel *channel, imgIDecoderObserver *aObserver, nsISupports *aCX, nsIStreamListener **listener, imgIRequest **_retval)
{
  NS_ASSERTION(channel, "imgLoader::LoadImageWithChannel -- NULL channel pointer");

  nsRefPtr<imgRequest> request;

  nsCOMPtr<nsIURI> uri;
  channel->GetURI(getter_AddRefs(uri));

  nsLoadFlags requestFlags = nsIRequest::LOAD_NORMAL;
  channel->GetLoadFlags(&requestFlags);

  nsRefPtr<imgCacheEntry> entry;

  if (requestFlags & nsIRequest::LOAD_BYPASS_CACHE) {
    RemoveFromCache(uri);
  } else {
    // Look in the cache for our URI, and then validate it.
    // XXX For now ignore aCacheKey. We will need it in the future
    // for correctly dealing with image load requests that are a result
    // of post data.
    imgCacheTable &cache = GetCache(uri);
    nsCAutoString spec;

    uri->GetSpec(spec);

    if (cache.Get(spec, getter_AddRefs(entry)) && entry) {
      // We don't want to kick off another network load. So we ask
      // ValidateEntry to only do validation without creating a new proxy. If
      // it says that the entry isn't valid any more, we'll only use the entry
      // we're getting if the channel is loading from the cache anyways.
      //
      // XXX -- should this be changed? it's pretty much verbatim from the old
      // code, but seems nonsensical.
      if (ValidateEntry(entry, uri, nsnull, nsnull, nsnull, aObserver, aCX,
                        requestFlags, false, nsnull, nsnull, nsnull,
                        nsnull, imgIRequest::CORS_NONE)) {
        request = getter_AddRefs(entry->GetRequest());
      } else {
        nsCOMPtr<nsICachingChannel> cacheChan(do_QueryInterface(channel));
        bool bUseCacheCopy;

        if (cacheChan)
          cacheChan->IsFromCache(&bUseCacheCopy);
        else
          bUseCacheCopy = false;

        if (!bUseCacheCopy)
          entry = nsnull;
        else {
          request = getter_AddRefs(entry->GetRequest());
        }
      }

      if (request && entry) {
        // If this entry has no proxies, its request has no reference to the entry.
        if (entry->HasNoProxies()) {
          LOG_FUNC_WITH_PARAM(gImgLog, "imgLoader::LoadImageWithChannel() adding proxyless entry", "uri", spec.get());
          NS_ABORT_IF_FALSE(!request->HasCacheEntry(), "Proxyless entry's request has cache entry!");
          request->SetCacheEntry(entry);

          if (gCacheTracker)
            gCacheTracker->MarkUsed(entry);
        } 
      }
    }
  }

  nsCOMPtr<nsILoadGroup> loadGroup;
  channel->GetLoadGroup(getter_AddRefs(loadGroup));

  // Filter out any load flags not from nsIRequest
  requestFlags &= nsIRequest::LOAD_REQUESTMASK;

  nsresult rv = NS_OK;
  if (request) {
    // we have this in our cache already.. cancel the current (document) load

    channel->Cancel(NS_ERROR_PARSED_DATA_CACHED); // this should fire an OnStopRequest

    *listener = nsnull; // give them back a null nsIStreamListener

    rv = CreateNewProxyForRequest(request, loadGroup, aObserver,
                                  requestFlags, nsnull, _retval);
    static_cast<imgRequestProxy*>(*_retval)->NotifyListener();
  } else {
    // Default to doing a principal check because we don't know who
    // started that load and whether their principal ended up being
    // inherited on the channel.
    NewRequestAndEntry(true, getter_AddRefs(request), getter_AddRefs(entry));

    // We use originalURI here to fulfil the imgIRequest contract on GetURI.
    nsCOMPtr<nsIURI> originalURI;
    channel->GetOriginalURI(getter_AddRefs(originalURI));

    // No principal specified here, because we're not passed one.
    request->Init(originalURI, uri, channel, channel, entry,
                  aCX, nsnull, imgIRequest::CORS_NONE);

    ProxyListener *pl = new ProxyListener(static_cast<nsIStreamListener *>(request.get()));
    NS_ADDREF(pl);

    *listener = static_cast<nsIStreamListener*>(pl);
    NS_ADDREF(*listener);

    NS_RELEASE(pl);

    // Try to add the new request into the cache.
    PutIntoCache(originalURI, entry);

    rv = CreateNewProxyForRequest(request, loadGroup, aObserver,
                                  requestFlags, nsnull, _retval);

    // Explicitly don't notify our proxy, because we're loading off the
    // network, and necko (or things called from necko, such as
    // imgCacheValidator) are going to call our notifications asynchronously,
    // and we can't make it further asynchronous because observers might rely
    // on imagelib completing its work between the channel's OnStartRequest and
    // OnStopRequest.
  }

  return rv;
}

NS_IMETHODIMP imgLoader::SupportImageWithMimeType(const char* aMimeType, bool *_retval)
{
  *_retval = false;
  nsCAutoString mimeType(aMimeType);
  ToLowerCase(mimeType);
  *_retval = (Image::GetDecoderType(mimeType.get()) == Image::eDecoderType_unknown)
    ? false : true;
  return NS_OK;
}

NS_IMETHODIMP imgLoader::GetMIMETypeFromContent(nsIRequest* aRequest,
                                                const PRUint8* aContents,
                                                PRUint32 aLength,
                                                nsACString& aContentType)
{
  return GetMimeTypeFromContent((const char*)aContents, aLength, aContentType);
}

/* static */
nsresult imgLoader::GetMimeTypeFromContent(const char* aContents, PRUint32 aLength, nsACString& aContentType)
{
  /* Is it a GIF? */
  if (aLength >= 6 && (!nsCRT::strncmp(aContents, "GIF87a", 6) ||
                       !nsCRT::strncmp(aContents, "GIF89a", 6)))
  {
    aContentType.AssignLiteral("image/gif");
  }

  /* or a PNG? */
  else if (aLength >= 8 && ((unsigned char)aContents[0]==0x89 &&
                   (unsigned char)aContents[1]==0x50 &&
                   (unsigned char)aContents[2]==0x4E &&
                   (unsigned char)aContents[3]==0x47 &&
                   (unsigned char)aContents[4]==0x0D &&
                   (unsigned char)aContents[5]==0x0A &&
                   (unsigned char)aContents[6]==0x1A &&
                   (unsigned char)aContents[7]==0x0A))
  { 
    aContentType.AssignLiteral("image/png");
  }

  /* maybe a JPEG (JFIF)? */
  /* JFIF files start with SOI APP0 but older files can start with SOI DQT
   * so we test for SOI followed by any marker, i.e. FF D8 FF
   * this will also work for SPIFF JPEG files if they appear in the future.
   *
   * (JFIF is 0XFF 0XD8 0XFF 0XE0 <skip 2> 0X4A 0X46 0X49 0X46 0X00)
   */
  else if (aLength >= 3 &&
     ((unsigned char)aContents[0])==0xFF &&
     ((unsigned char)aContents[1])==0xD8 &&
     ((unsigned char)aContents[2])==0xFF)
  {
    aContentType.AssignLiteral("image/jpeg");
  }

  /* or how about ART? */
  /* ART begins with JG (4A 47). Major version offset 2.
   * Minor version offset 3. Offset 4 must be NULL.
   */
  else if (aLength >= 5 &&
   ((unsigned char) aContents[0])==0x4a &&
   ((unsigned char) aContents[1])==0x47 &&
   ((unsigned char) aContents[4])==0x00 )
  {
    aContentType.AssignLiteral("image/x-jg");
  }

  else if (aLength >= 2 && !nsCRT::strncmp(aContents, "BM", 2)) {
    aContentType.AssignLiteral("image/bmp");
  }

  // ICOs always begin with a 2-byte 0 followed by a 2-byte 1.
  // CURs begin with 2-byte 0 followed by 2-byte 2.
  else if (aLength >= 4 && (!memcmp(aContents, "\000\000\001\000", 4) ||
                            !memcmp(aContents, "\000\000\002\000", 4))) {
    aContentType.AssignLiteral("image/x-icon");
  }

  else {
    /* none of the above?  I give up */
    return NS_ERROR_NOT_AVAILABLE;
  }

  return NS_OK;
}

/**
 * proxy stream listener class used to handle multipart/x-mixed-replace
 */

#include "nsIRequest.h"
#include "nsIStreamConverterService.h"
#include "nsXPIDLString.h"

NS_IMPL_ISUPPORTS2(ProxyListener, nsIStreamListener, nsIRequestObserver)

ProxyListener::ProxyListener(nsIStreamListener *dest) :
  mDestListener(dest)
{
  /* member initializers and constructor code */
}

ProxyListener::~ProxyListener()
{
  /* destructor code */
}


/** nsIRequestObserver methods **/

/* void onStartRequest (in nsIRequest request, in nsISupports ctxt); */
NS_IMETHODIMP ProxyListener::OnStartRequest(nsIRequest *aRequest, nsISupports *ctxt)
{
  if (!mDestListener)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
  if (channel) {
    nsCAutoString contentType;
    nsresult rv = channel->GetContentType(contentType);

    if (!contentType.IsEmpty()) {
     /* If multipart/x-mixed-replace content, we'll insert a MIME decoder
        in the pipeline to handle the content and pass it along to our
        original listener.
      */
      if (NS_LITERAL_CSTRING("multipart/x-mixed-replace").Equals(contentType)) {

        nsCOMPtr<nsIStreamConverterService> convServ(do_GetService("@mozilla.org/streamConverters;1", &rv));
        if (NS_SUCCEEDED(rv)) {
          nsCOMPtr<nsIStreamListener> toListener(mDestListener);
          nsCOMPtr<nsIStreamListener> fromListener;

          rv = convServ->AsyncConvertData("multipart/x-mixed-replace",
                                          "*/*",
                                          toListener,
                                          nsnull,
                                          getter_AddRefs(fromListener));
          if (NS_SUCCEEDED(rv))
            mDestListener = fromListener;
        }
      }
    }
  }

  return mDestListener->OnStartRequest(aRequest, ctxt);
}

/* void onStopRequest (in nsIRequest request, in nsISupports ctxt, in nsresult status); */
NS_IMETHODIMP ProxyListener::OnStopRequest(nsIRequest *aRequest, nsISupports *ctxt, nsresult status)
{
  if (!mDestListener)
    return NS_ERROR_FAILURE;

  return mDestListener->OnStopRequest(aRequest, ctxt, status);
}

/** nsIStreamListener methods **/

/* void onDataAvailable (in nsIRequest request, in nsISupports ctxt, in nsIInputStream inStr, in unsigned long sourceOffset, in unsigned long count); */
NS_IMETHODIMP ProxyListener::OnDataAvailable(nsIRequest *aRequest, nsISupports *ctxt, nsIInputStream *inStr, PRUint32 sourceOffset, PRUint32 count)
{
  if (!mDestListener)
    return NS_ERROR_FAILURE;

  return mDestListener->OnDataAvailable(aRequest, ctxt, inStr, sourceOffset, count);
}

/**
 * http validate class.  check a channel for a 304
 */

NS_IMPL_ISUPPORTS5(imgCacheValidator, nsIStreamListener, nsIRequestObserver,
                   nsIChannelEventSink, nsIInterfaceRequestor,
                   nsIAsyncVerifyRedirectCallback)

imgLoader imgCacheValidator::sImgLoader;

imgCacheValidator::imgCacheValidator(nsProgressNotificationProxy* progress,
                                     imgRequest *request, void *aContext,
                                     bool forcePrincipalCheckForCacheEntry)
 : mProgressProxy(progress),
   mRequest(request),
   mContext(aContext)
{
  NewRequestAndEntry(forcePrincipalCheckForCacheEntry,
                     getter_AddRefs(mNewRequest), getter_AddRefs(mNewEntry));
}

imgCacheValidator::~imgCacheValidator()
{
  if (mRequest) {
    mRequest->mValidator = nsnull;
  }
}

void imgCacheValidator::AddProxy(imgRequestProxy *aProxy)
{
  // aProxy needs to be in the loadgroup since we're validating from
  // the network.
  aProxy->AddToLoadGroup();

  mProxies.AppendObject(aProxy);
}

/** nsIRequestObserver methods **/

/* void onStartRequest (in nsIRequest request, in nsISupports ctxt); */
NS_IMETHODIMP imgCacheValidator::OnStartRequest(nsIRequest *aRequest, nsISupports *ctxt)
{
  // If this request is coming from cache and has the same URI as our
  // imgRequest, the request all our proxies are pointing at is valid, and all
  // we have to do is tell them to notify their listeners.
  nsCOMPtr<nsICachingChannel> cacheChan(do_QueryInterface(aRequest));
  nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
  if (cacheChan && channel) {
    bool isFromCache = false;
    cacheChan->IsFromCache(&isFromCache);

    nsCOMPtr<nsIURI> channelURI;
    bool sameURI = false;
    channel->GetURI(getter_AddRefs(channelURI));
    if (channelURI)
      channelURI->Equals(mRequest->mCurrentURI, &sameURI);

    if (isFromCache && sameURI) {
      PRUint32 count = mProxies.Count();
      for (PRInt32 i = count-1; i>=0; i--) {
        imgRequestProxy *proxy = static_cast<imgRequestProxy *>(mProxies[i]);

        // Proxies waiting on cache validation should be deferring notifications.
        // Undefer them.
        NS_ABORT_IF_FALSE(proxy->NotificationsDeferred(),
                          "Proxies waiting on cache validation should be "
                          "deferring notifications!");
        proxy->SetNotificationsDeferred(false);

        // Notify synchronously, because we're already in OnStartRequest, an
        // asynchronously-called function.
        proxy->SyncNotifyListener();
      }

      // We don't need to load this any more.
      aRequest->Cancel(NS_BINDING_ABORTED);

      mRequest->SetLoadId(mContext);
      mRequest->mValidator = nsnull;

      mRequest = nsnull;

      mNewRequest = nsnull;
      mNewEntry = nsnull;

      return NS_OK;
    }
  }

  // We can't load out of cache. We have to create a whole new request for the
  // data that's coming in off the channel.
  nsCOMPtr<nsIURI> uri;
  mRequest->GetURI(getter_AddRefs(uri));

#if defined(PR_LOGGING)
  nsCAutoString spec;
  uri->GetSpec(spec);
  LOG_MSG_WITH_PARAM(gImgLog, "imgCacheValidator::OnStartRequest creating new request", "uri", spec.get());
#endif

  PRInt32 corsmode = mRequest->GetCORSMode();
  nsCOMPtr<nsIPrincipal> loadingPrincipal = mRequest->GetLoadingPrincipal();

  // Doom the old request's cache entry
  mRequest->RemoveFromCache();

  mRequest->mValidator = nsnull;
  mRequest = nsnull;

  // We use originalURI here to fulfil the imgIRequest contract on GetURI.
  nsCOMPtr<nsIURI> originalURI;
  channel->GetOriginalURI(getter_AddRefs(originalURI));
  mNewRequest->Init(originalURI, uri, aRequest, channel, mNewEntry,
                    mContext, loadingPrincipal,
                    corsmode);

  mDestListener = new ProxyListener(mNewRequest);

  // Try to add the new request into the cache. Note that the entry must be in
  // the cache before the proxies' ownership changes, because adding a proxy
  // changes the caching behaviour for imgRequests.
  sImgLoader.PutIntoCache(originalURI, mNewEntry);

  PRUint32 count = mProxies.Count();
  for (PRInt32 i = count-1; i>=0; i--) {
    imgRequestProxy *proxy = static_cast<imgRequestProxy *>(mProxies[i]);
    proxy->ChangeOwner(mNewRequest);

    // Proxies waiting on cache validation should be deferring notifications.
    // Undefer them.
    NS_ABORT_IF_FALSE(proxy->NotificationsDeferred(),
                      "Proxies waiting on cache validation should be "
                      "deferring notifications!");
    proxy->SetNotificationsDeferred(false);

    // Notify synchronously, because we're already in OnStartRequest, an
    // asynchronously-called function.
    proxy->SyncNotifyListener();
  }

  mNewRequest = nsnull;
  mNewEntry = nsnull;

  return mDestListener->OnStartRequest(aRequest, ctxt);
}

/* void onStopRequest (in nsIRequest request, in nsISupports ctxt, in nsresult status); */
NS_IMETHODIMP imgCacheValidator::OnStopRequest(nsIRequest *aRequest, nsISupports *ctxt, nsresult status)
{
  if (!mDestListener)
    return NS_OK;

  return mDestListener->OnStopRequest(aRequest, ctxt, status);
}

/** nsIStreamListener methods **/


/* void onDataAvailable (in nsIRequest request, in nsISupports ctxt, in nsIInputStream inStr, in unsigned long sourceOffset, in unsigned long count); */
NS_IMETHODIMP imgCacheValidator::OnDataAvailable(nsIRequest *aRequest, nsISupports *ctxt, nsIInputStream *inStr, PRUint32 sourceOffset, PRUint32 count)
{
  if (!mDestListener) {
    // XXX see bug 113959
    PRUint32 _retval;
    inStr->ReadSegments(NS_DiscardSegment, nsnull, count, &_retval);
    return NS_OK;
  }

  return mDestListener->OnDataAvailable(aRequest, ctxt, inStr, sourceOffset, count);
}

/** nsIInterfaceRequestor methods **/

NS_IMETHODIMP imgCacheValidator::GetInterface(const nsIID & aIID, void **aResult)
{
  if (aIID.Equals(NS_GET_IID(nsIChannelEventSink)))
    return QueryInterface(aIID, aResult);

  return mProgressProxy->GetInterface(aIID, aResult);
}

// These functions are materially the same as the same functions in imgRequest.
// We duplicate them because we're verifying whether cache loads are necessary,
// not unconditionally loading.

/** nsIChannelEventSink methods **/
NS_IMETHODIMP imgCacheValidator::AsyncOnChannelRedirect(nsIChannel *oldChannel,
                                                        nsIChannel *newChannel, PRUint32 flags,
                                                        nsIAsyncVerifyRedirectCallback *callback)
{
  // Note all cache information we get from the old channel.
  mNewRequest->SetCacheValidation(mNewEntry, oldChannel);

  // Prepare for callback
  mRedirectCallback = callback;
  mRedirectChannel = newChannel;

  return mProgressProxy->AsyncOnChannelRedirect(oldChannel, newChannel, flags, this);
}

NS_IMETHODIMP imgCacheValidator::OnRedirectVerifyCallback(nsresult aResult)
{
  // If we've already been told to abort, just do so.
  if (NS_FAILED(aResult)) {
      mRedirectCallback->OnRedirectVerifyCallback(aResult);
      mRedirectCallback = nsnull;
      mRedirectChannel = nsnull;
      return NS_OK;
  }

  // make sure we have a protocol that returns data rather than opens
  // an external application, e.g. mailto:
  nsCOMPtr<nsIURI> uri;
  mRedirectChannel->GetURI(getter_AddRefs(uri));
  bool doesNotReturnData = false;
  NS_URIChainHasFlags(uri, nsIProtocolHandler::URI_DOES_NOT_RETURN_DATA,
                      &doesNotReturnData);

  nsresult result = NS_OK;

  if (doesNotReturnData) {
    result = NS_ERROR_ABORT;
  }

  mRedirectCallback->OnRedirectVerifyCallback(result);
  mRedirectCallback = nsnull;
  mRedirectChannel = nsnull;
  return NS_OK;
}
