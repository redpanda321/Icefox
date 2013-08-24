/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Util.h"
#include "mozilla/Attributes.h"

#include "necko-config.h"

#include "nsCache.h"
#include "nsCacheService.h"
#include "nsCacheRequest.h"
#include "nsCacheEntry.h"
#include "nsCacheEntryDescriptor.h"
#include "nsCacheDevice.h"
#include "nsMemoryCacheDevice.h"
#include "nsICacheVisitor.h"
#include "nsDiskCacheDevice.h"
#include "nsDiskCacheDeviceSQL.h"

#include "nsIMemoryReporter.h"
#include "nsIObserverService.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsIFile.h"
#include "nsIOService.h"
#include "nsDirectoryServiceDefs.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsThreadUtils.h"
#include "nsProxyRelease.h"
#include "nsVoidArray.h"
#include "nsDeleteDir.h"
#include "nsNetCID.h"
#include <math.h>  // for log()
#include "mozilla/Services.h"
#include "nsITimer.h"

#include "mozilla/FunctionTimer.h"

#include "mozilla/net/NeckoCommon.h"

using namespace mozilla;

/******************************************************************************
 * nsCacheProfilePrefObserver
 *****************************************************************************/
#define DISK_CACHE_ENABLE_PREF      "browser.cache.disk.enable"
#define DISK_CACHE_DIR_PREF         "browser.cache.disk.parent_directory"
#define DISK_CACHE_SMART_SIZE_FIRST_RUN_PREF\
    "browser.cache.disk.smart_size.first_run"
#define DISK_CACHE_SMART_SIZE_ENABLED_PREF \
    "browser.cache.disk.smart_size.enabled"
#define DISK_CACHE_SMART_SIZE_PREF "browser.cache.disk.smart_size_cached_value"
#define DISK_CACHE_CAPACITY_PREF    "browser.cache.disk.capacity"
#define DISK_CACHE_MAX_ENTRY_SIZE_PREF "browser.cache.disk.max_entry_size"
#define DISK_CACHE_CAPACITY         256000

#define OFFLINE_CACHE_ENABLE_PREF   "browser.cache.offline.enable"
#define OFFLINE_CACHE_DIR_PREF      "browser.cache.offline.parent_directory"
#define OFFLINE_CACHE_CAPACITY_PREF "browser.cache.offline.capacity"
#define OFFLINE_CACHE_CAPACITY      512000

#define MEMORY_CACHE_ENABLE_PREF    "browser.cache.memory.enable"
#define MEMORY_CACHE_CAPACITY_PREF  "browser.cache.memory.capacity"
#define MEMORY_CACHE_MAX_ENTRY_SIZE_PREF "browser.cache.memory.max_entry_size"

#define CACHE_COMPRESSION_LEVEL_PREF "browser.cache.compression_level"
#define CACHE_COMPRESSION_LEVEL     1

#define SANITIZE_ON_SHUTDOWN_PREF   "privacy.sanitize.sanitizeOnShutdown"
#define CLEAR_ON_SHUTDOWN_PREF      "privacy.clearOnShutdown.cache"

static const char * observerList[] = { 
    "profile-before-change",
    "profile-do-change",
    NS_XPCOM_SHUTDOWN_OBSERVER_ID,
    "last-pb-context-exited"
};
static const char * prefList[] = { 
    DISK_CACHE_ENABLE_PREF,
    DISK_CACHE_SMART_SIZE_ENABLED_PREF,
    DISK_CACHE_CAPACITY_PREF,
    DISK_CACHE_DIR_PREF,
    DISK_CACHE_MAX_ENTRY_SIZE_PREF,
    OFFLINE_CACHE_ENABLE_PREF,
    OFFLINE_CACHE_CAPACITY_PREF,
    OFFLINE_CACHE_DIR_PREF,
    MEMORY_CACHE_ENABLE_PREF,
    MEMORY_CACHE_CAPACITY_PREF,
    MEMORY_CACHE_MAX_ENTRY_SIZE_PREF,
    CACHE_COMPRESSION_LEVEL_PREF,
    SANITIZE_ON_SHUTDOWN_PREF,
    CLEAR_ON_SHUTDOWN_PREF
};

// Cache sizes, in KB
const PRInt32 DEFAULT_CACHE_SIZE = 250 * 1024;  // 250 MB
const PRInt32 MIN_CACHE_SIZE = 50 * 1024;       //  50 MB
#ifdef ANDROID
const PRInt32 MAX_CACHE_SIZE = 200 * 1024;      // 200 MB
#else
const PRInt32 MAX_CACHE_SIZE = 1024 * 1024;     //   1 GB
#endif
// Default cache size was 50 MB for many years until FF 4:
const PRInt32 PRE_GECKO_2_0_DEFAULT_CACHE_SIZE = 50 * 1024;

class nsCacheProfilePrefObserver : public nsIObserver
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOBSERVER

    nsCacheProfilePrefObserver()
        : mHaveProfile(false)
        , mDiskCacheEnabled(false)
        , mDiskCacheCapacity(0)
        , mDiskCacheMaxEntrySize(-1) // -1 means "no limit"
        , mSmartSizeEnabled(false)
        , mOfflineCacheEnabled(false)
        , mOfflineCacheCapacity(0)
        , mMemoryCacheEnabled(true)
        , mMemoryCacheCapacity(-1)
        , mMemoryCacheMaxEntrySize(-1) // -1 means "no limit"
        , mCacheCompressionLevel(CACHE_COMPRESSION_LEVEL)
        , mSanitizeOnShutdown(false)
        , mClearCacheOnShutdown(false)
    {
    }

    virtual ~nsCacheProfilePrefObserver() {}
    
    nsresult        Install();
    void            Remove();
    nsresult        ReadPrefs(nsIPrefBranch* branch);
    
    bool            DiskCacheEnabled();
    PRInt32         DiskCacheCapacity()         { return mDiskCacheCapacity; }
    void            SetDiskCacheCapacity(PRInt32);
    PRInt32         DiskCacheMaxEntrySize()     { return mDiskCacheMaxEntrySize; }
    nsIFile *       DiskCacheParentDirectory()  { return mDiskCacheParentDirectory; }
    bool            SmartSizeEnabled()          { return mSmartSizeEnabled; }

    bool            OfflineCacheEnabled();
    PRInt32         OfflineCacheCapacity()         { return mOfflineCacheCapacity; }
    nsIFile *       OfflineCacheParentDirectory()  { return mOfflineCacheParentDirectory; }
    
    bool            MemoryCacheEnabled();
    PRInt32         MemoryCacheCapacity();
    PRInt32         MemoryCacheMaxEntrySize()     { return mMemoryCacheMaxEntrySize; }

    PRInt32         CacheCompressionLevel();

    bool            SanitizeAtShutdown() { return mSanitizeOnShutdown && mClearCacheOnShutdown; }

    static PRUint32 GetSmartCacheSize(const nsAString& cachePath,
                                      PRUint32 currentSize);

private:
    bool                    PermittedToSmartSize(nsIPrefBranch*, bool firstRun);
    bool                    mHaveProfile;
    
    bool                    mDiskCacheEnabled;
    PRInt32                 mDiskCacheCapacity; // in kilobytes
    PRInt32                 mDiskCacheMaxEntrySize; // in kilobytes
    nsCOMPtr<nsIFile>       mDiskCacheParentDirectory;
    bool                    mSmartSizeEnabled;

    bool                    mOfflineCacheEnabled;
    PRInt32                 mOfflineCacheCapacity; // in kilobytes
    nsCOMPtr<nsIFile>       mOfflineCacheParentDirectory;
    
    bool                    mMemoryCacheEnabled;
    PRInt32                 mMemoryCacheCapacity; // in kilobytes
    PRInt32                 mMemoryCacheMaxEntrySize; // in kilobytes

    PRInt32                 mCacheCompressionLevel;

    bool                    mSanitizeOnShutdown;
    bool                    mClearCacheOnShutdown;
};

NS_IMPL_THREADSAFE_ISUPPORTS1(nsCacheProfilePrefObserver, nsIObserver)

class nsSetDiskSmartSizeCallback MOZ_FINAL : public nsITimerCallback
{
public:
    NS_DECL_ISUPPORTS

    NS_IMETHOD Notify(nsITimer* aTimer) {
        if (nsCacheService::gService) {
            nsCacheServiceAutoLock autoLock(LOCK_TELEM(NSSETDISKSMARTSIZECALLBACK_NOTIFY));
            nsCacheService::gService->SetDiskSmartSize_Locked();
            nsCacheService::gService->mSmartSizeTimer = nsnull;
        }
        return NS_OK;
    }
};

NS_IMPL_THREADSAFE_ISUPPORTS1(nsSetDiskSmartSizeCallback, nsITimerCallback)

// Runnable sent to main thread after the cache IO thread calculates available
// disk space, so that there is no race in setting mDiskCacheCapacity.
class nsSetSmartSizeEvent: public nsRunnable 
{
public:
    nsSetSmartSizeEvent(PRInt32 smartSize)
        : mSmartSize(smartSize) {}

    NS_IMETHOD Run() 
    {
        NS_ASSERTION(NS_IsMainThread(), 
                     "Setting smart size data off the main thread");

        // Main thread may have already called nsCacheService::Shutdown
        if (!nsCacheService::gService || !nsCacheService::gService->mObserver)
            return NS_ERROR_NOT_AVAILABLE;

        // Ensure smart sizing wasn't switched off while event was pending.
        // It is safe to access the observer without the lock since we are
        // on the main thread and the value changes only on the main thread.
        if (!nsCacheService::gService->mObserver->SmartSizeEnabled())
            return NS_OK;

        nsCacheService::SetDiskCacheCapacity(mSmartSize);

        nsCOMPtr<nsIPrefBranch> ps = do_GetService(NS_PREFSERVICE_CONTRACTID);
        if (!ps ||
            NS_FAILED(ps->SetIntPref(DISK_CACHE_SMART_SIZE_PREF, mSmartSize)))
            NS_WARNING("Failed to set smart size pref");

        return NS_OK;
    }

private:
    PRInt32 mSmartSize;
};


// Runnable sent from main thread to cacheIO thread
class nsGetSmartSizeEvent: public nsRunnable
{
public:
    nsGetSmartSizeEvent(const nsAString& cachePath, PRUint32 currentSize)
      : mCachePath(cachePath)
      , mCurrentSize(currentSize)
    {}
   
    // Calculates user's disk space available on a background thread and
    // dispatches this value back to the main thread.
    NS_IMETHOD Run()
    {
        PRUint32 size;
        size = nsCacheProfilePrefObserver::GetSmartCacheSize(mCachePath,
                                                             mCurrentSize);
        NS_DispatchToMainThread(new nsSetSmartSizeEvent(size));
        return NS_OK;
    }

private:
    nsString mCachePath;
    PRUint32 mCurrentSize;
};

class nsBlockOnCacheThreadEvent : public nsRunnable {
public:
    nsBlockOnCacheThreadEvent()
    {
    }
    NS_IMETHOD Run()
    {
        nsCacheServiceAutoLock autoLock(LOCK_TELEM(NSBLOCKONCACHETHREADEVENT_RUN));
#ifdef PR_LOGGING
        CACHE_LOG_DEBUG(("nsBlockOnCacheThreadEvent [%p]\n", this));
#endif
        nsCacheService::gService->mCondVar.Notify();
        return NS_OK;
    }
};


nsresult
nsCacheProfilePrefObserver::Install()
{
    // install profile-change observer
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (!observerService)
        return NS_ERROR_FAILURE;
    
    nsresult rv, rv2 = NS_OK;
    for (unsigned int i=0; i<ArrayLength(observerList); i++) {
        rv = observerService->AddObserver(this, observerList[i], false);
        if (NS_FAILED(rv)) 
            rv2 = rv;
    }
    
    // install preferences observer
    nsCOMPtr<nsIPrefBranch> branch = do_GetService(NS_PREFSERVICE_CONTRACTID);
    if (!branch) return NS_ERROR_FAILURE;

    for (unsigned int i=0; i<ArrayLength(prefList); i++) {
        rv = branch->AddObserver(prefList[i], this, false);
        if (NS_FAILED(rv))
            rv2 = rv;
    }

    // Determine if we have a profile already
    //     Install() is called *after* the profile-after-change notification
    //     when there is only a single profile, or it is specified on the
    //     commandline at startup.
    //     In that case, we detect the presence of a profile by the existence
    //     of the NS_APP_USER_PROFILE_50_DIR directory.

    nsCOMPtr<nsIFile> directory;
    rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                getter_AddRefs(directory));
    if (NS_SUCCEEDED(rv))
        mHaveProfile = true;

    rv = ReadPrefs(branch);
    NS_ENSURE_SUCCESS(rv, rv);

    return rv2;
}


void
nsCacheProfilePrefObserver::Remove()
{
    // remove Observer Service observers
    nsCOMPtr<nsIObserverService> obs =
        mozilla::services::GetObserverService();
    if (obs) {
        for (unsigned int i=0; i<ArrayLength(observerList); i++) {
            obs->RemoveObserver(this, observerList[i]);
        }
    }

    // remove Pref Service observers
    nsCOMPtr<nsIPrefBranch> prefs =
        do_GetService(NS_PREFSERVICE_CONTRACTID);
    if (!prefs)
        return;
    for (unsigned int i=0; i<ArrayLength(prefList); i++)
        prefs->RemoveObserver(prefList[i], this); // remove cache pref observers
}

void
nsCacheProfilePrefObserver::SetDiskCacheCapacity(PRInt32 capacity)
{
    mDiskCacheCapacity = NS_MAX(0, capacity);
}


NS_IMETHODIMP
nsCacheProfilePrefObserver::Observe(nsISupports *     subject,
                                    const char *      topic,
                                    const PRUnichar * data_unicode)
{
    nsresult rv;
    NS_ConvertUTF16toUTF8 data(data_unicode);
    CACHE_LOG_ALWAYS(("Observe [topic=%s data=%s]\n", topic, data.get()));

    if (!strcmp(NS_XPCOM_SHUTDOWN_OBSERVER_ID, topic)) {
        // xpcom going away, shutdown cache service
        if (nsCacheService::GlobalInstance())
            nsCacheService::GlobalInstance()->Shutdown();
    
    } else if (!strcmp("profile-before-change", topic)) {
        // profile before change
        mHaveProfile = false;

        // XXX shutdown devices
        nsCacheService::OnProfileShutdown(!strcmp("shutdown-cleanse",
                                                  data.get()));
        
    } else if (!strcmp("profile-do-change", topic)) {
        // profile after change
        mHaveProfile = true;
        nsCOMPtr<nsIPrefBranch> branch = do_GetService(NS_PREFSERVICE_CONTRACTID);
        ReadPrefs(branch);
        nsCacheService::OnProfileChanged();
    
    } else if (!strcmp(NS_PREFBRANCH_PREFCHANGE_TOPIC_ID, topic)) {

        // ignore pref changes until we're done switch profiles
        if (!mHaveProfile)  
            return NS_OK;

        nsCOMPtr<nsIPrefBranch> branch = do_QueryInterface(subject, &rv);
        if (NS_FAILED(rv))  
            return rv;

        // which preference changed?
        if (!strcmp(DISK_CACHE_ENABLE_PREF, data.get())) {

            rv = branch->GetBoolPref(DISK_CACHE_ENABLE_PREF,
                                     &mDiskCacheEnabled);
            if (NS_FAILED(rv))  
                return rv;
            nsCacheService::SetDiskCacheEnabled(DiskCacheEnabled());

        } else if (!strcmp(DISK_CACHE_CAPACITY_PREF, data.get())) {

            PRInt32 capacity = 0;
            rv = branch->GetIntPref(DISK_CACHE_CAPACITY_PREF, &capacity);
            if (NS_FAILED(rv))  
                return rv;
            mDiskCacheCapacity = NS_MAX(0, capacity);
            nsCacheService::SetDiskCacheCapacity(mDiskCacheCapacity);
       
        // Update the cache capacity when smart sizing is turned on/off 
        } else if (!strcmp(DISK_CACHE_SMART_SIZE_ENABLED_PREF, data.get())) {
            // Is the update because smartsizing was turned on, or off?
            rv = branch->GetBoolPref(DISK_CACHE_SMART_SIZE_ENABLED_PREF,
                                     &mSmartSizeEnabled);
            if (NS_FAILED(rv)) 
                return rv;
            PRInt32 newCapacity = 0;
            if (mSmartSizeEnabled) {
                nsCacheService::SetDiskSmartSize();
            } else {
                // Smart sizing switched off: use user specified size
                rv = branch->GetIntPref(DISK_CACHE_CAPACITY_PREF, &newCapacity);
                if (NS_FAILED(rv)) 
                    return rv;
                mDiskCacheCapacity = NS_MAX(0, newCapacity);
                nsCacheService::SetDiskCacheCapacity(mDiskCacheCapacity);
            }
        } else if (!strcmp(DISK_CACHE_MAX_ENTRY_SIZE_PREF, data.get())) {
            PRInt32 newMaxSize;
            rv = branch->GetIntPref(DISK_CACHE_MAX_ENTRY_SIZE_PREF,
                                    &newMaxSize);
            if (NS_FAILED(rv)) 
                return rv;

            mDiskCacheMaxEntrySize = NS_MAX(-1, newMaxSize);
            nsCacheService::SetDiskCacheMaxEntrySize(mDiskCacheMaxEntrySize);
          
#if 0            
        } else if (!strcmp(DISK_CACHE_DIR_PREF, data.get())) {
            // XXX We probaby don't want to respond to this pref except after
            // XXX profile changes.  Ideally, there should be somekind of user
            // XXX notification that the pref change won't take effect until
            // XXX the next time the profile changes (browser launch)
#endif            
        } else

        // which preference changed?
        if (!strcmp(OFFLINE_CACHE_ENABLE_PREF, data.get())) {

            rv = branch->GetBoolPref(OFFLINE_CACHE_ENABLE_PREF,
                                     &mOfflineCacheEnabled);
            if (NS_FAILED(rv))  return rv;
            nsCacheService::SetOfflineCacheEnabled(OfflineCacheEnabled());

        } else if (!strcmp(OFFLINE_CACHE_CAPACITY_PREF, data.get())) {

            PRInt32 capacity = 0;
            rv = branch->GetIntPref(OFFLINE_CACHE_CAPACITY_PREF, &capacity);
            if (NS_FAILED(rv))  return rv;
            mOfflineCacheCapacity = NS_MAX(0, capacity);
            nsCacheService::SetOfflineCacheCapacity(mOfflineCacheCapacity);
#if 0
        } else if (!strcmp(OFFLINE_CACHE_DIR_PREF, data.get())) {
            // XXX We probaby don't want to respond to this pref except after
            // XXX profile changes.  Ideally, there should be some kind of user
            // XXX notification that the pref change won't take effect until
            // XXX the next time the profile changes (browser launch)
#endif
        } else

        if (!strcmp(MEMORY_CACHE_ENABLE_PREF, data.get())) {

            rv = branch->GetBoolPref(MEMORY_CACHE_ENABLE_PREF,
                                     &mMemoryCacheEnabled);
            if (NS_FAILED(rv))  
                return rv;
            nsCacheService::SetMemoryCache();
            
        } else if (!strcmp(MEMORY_CACHE_CAPACITY_PREF, data.get())) {

            mMemoryCacheCapacity = -1;
            (void) branch->GetIntPref(MEMORY_CACHE_CAPACITY_PREF,
                                      &mMemoryCacheCapacity);
            nsCacheService::SetMemoryCache();
        } else if (!strcmp(MEMORY_CACHE_MAX_ENTRY_SIZE_PREF, data.get())) {
            PRInt32 newMaxSize;
            rv = branch->GetIntPref(MEMORY_CACHE_MAX_ENTRY_SIZE_PREF,
                                     &newMaxSize);
            if (NS_FAILED(rv)) 
                return rv;
            
            mMemoryCacheMaxEntrySize = NS_MAX(-1, newMaxSize);
            nsCacheService::SetMemoryCacheMaxEntrySize(mMemoryCacheMaxEntrySize);
        } else if (!strcmp(CACHE_COMPRESSION_LEVEL_PREF, data.get())) {
            mCacheCompressionLevel = CACHE_COMPRESSION_LEVEL;
            (void)branch->GetIntPref(CACHE_COMPRESSION_LEVEL_PREF,
                                     &mCacheCompressionLevel);
            mCacheCompressionLevel = NS_MAX(0, mCacheCompressionLevel);
            mCacheCompressionLevel = NS_MIN(9, mCacheCompressionLevel);
        } else if (!strcmp(SANITIZE_ON_SHUTDOWN_PREF, data.get())) {
            rv = branch->GetBoolPref(SANITIZE_ON_SHUTDOWN_PREF,
                                     &mSanitizeOnShutdown);
            if (NS_FAILED(rv))
                return rv;
            nsCacheService::SetDiskCacheEnabled(DiskCacheEnabled());
        } else if (!strcmp(CLEAR_ON_SHUTDOWN_PREF, data.get())) {
            rv = branch->GetBoolPref(CLEAR_ON_SHUTDOWN_PREF,
                                     &mClearCacheOnShutdown);
            if (NS_FAILED(rv))
                return rv;
            nsCacheService::SetDiskCacheEnabled(DiskCacheEnabled());
        }
    } else if (!strcmp("last-pb-context-exited", topic)) {
        nsCacheService::LeavePrivateBrowsing();
    }

    return NS_OK;
}

// Returns default ("smart") size (in KB) of cache, given available disk space
// (also in KB)
static PRUint32
SmartCacheSize(const PRUint32 availKB)
{
    if (availKB > 100 * 1024 * 1024)
        return MAX_CACHE_SIZE;  // skip computing if we're over 100 GB

    // Grow/shrink in 10 MB units, deliberately, so that in the common case we
    // don't shrink cache and evict items every time we startup (it's important
    // that we don't slow down startup benchmarks).
    PRUint32 sz10MBs = 0;
    PRUint32 avail10MBs = availKB / (1024*10);

    // .5% of space above 25 GB
    if (avail10MBs > 2500) {
        sz10MBs += (avail10MBs - 2500)*.005;
        avail10MBs = 2500;
    }
    // 1% of space between 7GB -> 25 GB
    if (avail10MBs > 700) {
        sz10MBs += (avail10MBs - 700)*.01;
        avail10MBs = 700;
    }
    // 5% of space between 500 MB -> 7 GB
    if (avail10MBs > 50) {
        sz10MBs += (avail10MBs - 50)*.05;
        avail10MBs = 50;
    }

#ifdef ANDROID
    // On Android, smaller/older devices may have very little storage and
    // device owners may be sensitive to storage footprint: Use a smaller
    // percentage of available space and a smaller minimum.

    // 20% of space up to 500 MB (10 MB min)
    sz10MBs += NS_MAX<PRUint32>(1, avail10MBs * .2);
#else
    // 40% of space up to 500 MB (50 MB min)
    sz10MBs += NS_MAX<PRUint32>(5, avail10MBs * .4);
#endif

    return NS_MIN<PRUint32>(MAX_CACHE_SIZE, sz10MBs * 10 * 1024);
}

 /* Computes our best guess for the default size of the user's disk cache, 
  * based on the amount of space they have free on their hard drive. 
  * We use a tiered scheme: the more space available, 
  * the larger the disk cache will be. However, we do not want
  * to enable the disk cache to grow to an unbounded size, so the larger the
  * user's available space is, the smaller of a percentage we take. We set a
  * lower bound of 50MB and an upper bound of 1GB.  
  *
  *@param:  None.
  *@return: The size that the user's disk cache should default to, in kBytes.
  */
PRUint32
nsCacheProfilePrefObserver::GetSmartCacheSize(const nsAString& cachePath,
                                              PRUint32 currentSize)
{
    // Check for free space on device where cache directory lives
    nsresult rv;
    nsCOMPtr<nsIFile> 
        cacheDirectory (do_CreateInstance(NS_LOCAL_FILE_CONTRACTID, &rv));
    if (NS_FAILED(rv) || !cacheDirectory)
        return DEFAULT_CACHE_SIZE;
    rv = cacheDirectory->InitWithPath(cachePath);
    if (NS_FAILED(rv))
        return DEFAULT_CACHE_SIZE;
    PRInt64 bytesAvailable;
    rv = cacheDirectory->GetDiskSpaceAvailable(&bytesAvailable);
    if (NS_FAILED(rv))
        return DEFAULT_CACHE_SIZE;

    return SmartCacheSize((bytesAvailable / 1024) + currentSize);
}

/* Determine if we are permitted to dynamically size the user's disk cache based
 * on their disk space available. We may do this so long as the pref 
 * smart_size.enabled is true.
 */
bool
nsCacheProfilePrefObserver::PermittedToSmartSize(nsIPrefBranch* branch, bool
                                                 firstRun)
{
    nsresult rv;
    if (firstRun) {
        // check if user has set cache size in the past
        bool userSet;
        rv = branch->PrefHasUserValue(DISK_CACHE_CAPACITY_PREF, &userSet);
        if (NS_FAILED(rv)) userSet = true;
        if (userSet) {
            PRInt32 oldCapacity;
            // If user explicitly set cache size to be smaller than old default
            // of 50 MB, then keep user's value. Otherwise use smart sizing.
            rv = branch->GetIntPref(DISK_CACHE_CAPACITY_PREF, &oldCapacity);
            if (oldCapacity < PRE_GECKO_2_0_DEFAULT_CACHE_SIZE) {
                mSmartSizeEnabled = false;
                branch->SetBoolPref(DISK_CACHE_SMART_SIZE_ENABLED_PREF,
                                    mSmartSizeEnabled);
                return mSmartSizeEnabled;
            }
        }
        // Set manual setting to MAX cache size as starting val for any
        // adjustment by user: (bug 559942 comment 65)
        branch->SetIntPref(DISK_CACHE_CAPACITY_PREF, MAX_CACHE_SIZE);
    }

    rv = branch->GetBoolPref(DISK_CACHE_SMART_SIZE_ENABLED_PREF,
                             &mSmartSizeEnabled);
    if (NS_FAILED(rv))
        mSmartSizeEnabled = false;
    return mSmartSizeEnabled;
}


nsresult
nsCacheProfilePrefObserver::ReadPrefs(nsIPrefBranch* branch)
{
    nsresult rv = NS_OK;

    // read disk cache device prefs
    mDiskCacheEnabled = true;  // presume disk cache is enabled
    (void) branch->GetBoolPref(DISK_CACHE_ENABLE_PREF, &mDiskCacheEnabled);

    mDiskCacheCapacity = DISK_CACHE_CAPACITY;
    (void)branch->GetIntPref(DISK_CACHE_CAPACITY_PREF, &mDiskCacheCapacity);
    mDiskCacheCapacity = NS_MAX(0, mDiskCacheCapacity);

    (void) branch->GetIntPref(DISK_CACHE_MAX_ENTRY_SIZE_PREF,
                              &mDiskCacheMaxEntrySize);
    mDiskCacheMaxEntrySize = NS_MAX(-1, mDiskCacheMaxEntrySize);
    
    (void) branch->GetComplexValue(DISK_CACHE_DIR_PREF,     // ignore error
                                   NS_GET_IID(nsIFile),
                                   getter_AddRefs(mDiskCacheParentDirectory));
    
    if (!mDiskCacheParentDirectory) {
        nsCOMPtr<nsIFile>  directory;

        // try to get the disk cache parent directory
        rv = NS_GetSpecialDirectory(NS_APP_CACHE_PARENT_DIR,
                                    getter_AddRefs(directory));
        if (NS_FAILED(rv)) {
            // try to get the profile directory (there may not be a profile yet)
            nsCOMPtr<nsIFile> profDir;
            NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                   getter_AddRefs(profDir));
            NS_GetSpecialDirectory(NS_APP_USER_PROFILE_LOCAL_50_DIR,
                                   getter_AddRefs(directory));
            if (!directory)
                directory = profDir;
            else if (profDir) {
                bool same;
                if (NS_SUCCEEDED(profDir->Equals(directory, &same)) && !same) {
                    // We no longer store the cache directory in the main
                    // profile directory, so we should cleanup the old one.
                    rv = profDir->AppendNative(NS_LITERAL_CSTRING("Cache"));
                    if (NS_SUCCEEDED(rv)) {
                        bool exists;
                        if (NS_SUCCEEDED(profDir->Exists(&exists)) && exists)
                            nsDeleteDir::DeleteDir(profDir, false);
                    }
                }
            }
        }
        // use file cache in build tree only if asked, to avoid cache dir litter
        if (!directory && PR_GetEnv("NECKO_DEV_ENABLE_DISK_CACHE")) {
            rv = NS_GetSpecialDirectory(NS_XPCOM_CURRENT_PROCESS_DIR,
                                        getter_AddRefs(directory));
        }
        if (directory)
            mDiskCacheParentDirectory = do_QueryInterface(directory, &rv);
    }
    if (mDiskCacheParentDirectory) {
        bool firstSmartSizeRun;
        rv = branch->GetBoolPref(DISK_CACHE_SMART_SIZE_FIRST_RUN_PREF, 
                                 &firstSmartSizeRun); 
        if (NS_FAILED(rv)) 
            firstSmartSizeRun = false;
        if (PermittedToSmartSize(branch, firstSmartSizeRun)) {
            // Avoid evictions: use previous cache size until smart size event
            // updates mDiskCacheCapacity
            rv = branch->GetIntPref(firstSmartSizeRun ?
                                    DISK_CACHE_CAPACITY_PREF :
                                    DISK_CACHE_SMART_SIZE_PREF,
                                    &mDiskCacheCapacity);
            if (NS_FAILED(rv))
                mDiskCacheCapacity = DEFAULT_CACHE_SIZE;
        }

        if (firstSmartSizeRun) {
            // It is no longer our first run
            rv = branch->SetBoolPref(DISK_CACHE_SMART_SIZE_FIRST_RUN_PREF, 
                                     false);
            if (NS_FAILED(rv)) 
                NS_WARNING("Failed setting first_run pref in ReadPrefs.");
        }
    }

    // read offline cache device prefs
    mOfflineCacheEnabled = true;  // presume offline cache is enabled
    (void) branch->GetBoolPref(OFFLINE_CACHE_ENABLE_PREF,
                              &mOfflineCacheEnabled);

    mOfflineCacheCapacity = OFFLINE_CACHE_CAPACITY;
    (void)branch->GetIntPref(OFFLINE_CACHE_CAPACITY_PREF,
                             &mOfflineCacheCapacity);
    mOfflineCacheCapacity = NS_MAX(0, mOfflineCacheCapacity);

    (void) branch->GetComplexValue(OFFLINE_CACHE_DIR_PREF,     // ignore error
                                   NS_GET_IID(nsIFile),
                                   getter_AddRefs(mOfflineCacheParentDirectory));

    if (!mOfflineCacheParentDirectory) {
        nsCOMPtr<nsIFile>  directory;

        // try to get the offline cache parent directory
        rv = NS_GetSpecialDirectory(NS_APP_CACHE_PARENT_DIR,
                                    getter_AddRefs(directory));
        if (NS_FAILED(rv)) {
            // try to get the profile directory (there may not be a profile yet)
            nsCOMPtr<nsIFile> profDir;
            NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                   getter_AddRefs(profDir));
            NS_GetSpecialDirectory(NS_APP_USER_PROFILE_LOCAL_50_DIR,
                                   getter_AddRefs(directory));
            if (!directory)
                directory = profDir;
        }
#if DEBUG
        if (!directory) {
            // use current process directory during development
            rv = NS_GetSpecialDirectory(NS_XPCOM_CURRENT_PROCESS_DIR,
                                        getter_AddRefs(directory));
        }
#endif
        if (directory)
            mOfflineCacheParentDirectory = do_QueryInterface(directory, &rv);
    }

    // read memory cache device prefs
    (void) branch->GetBoolPref(MEMORY_CACHE_ENABLE_PREF, &mMemoryCacheEnabled);

    mMemoryCacheCapacity = -1;
    (void) branch->GetIntPref(MEMORY_CACHE_CAPACITY_PREF,
                              &mMemoryCacheCapacity);

    (void) branch->GetIntPref(MEMORY_CACHE_MAX_ENTRY_SIZE_PREF,
                              &mMemoryCacheMaxEntrySize);
    mMemoryCacheMaxEntrySize = NS_MAX(-1, mMemoryCacheMaxEntrySize);

    // read cache compression level pref
    mCacheCompressionLevel = CACHE_COMPRESSION_LEVEL;
    (void)branch->GetIntPref(CACHE_COMPRESSION_LEVEL_PREF,
                             &mCacheCompressionLevel);
    mCacheCompressionLevel = NS_MAX(0, mCacheCompressionLevel);
    mCacheCompressionLevel = NS_MIN(9, mCacheCompressionLevel);

    // read cache shutdown sanitization prefs
    (void) branch->GetBoolPref(SANITIZE_ON_SHUTDOWN_PREF,
                               &mSanitizeOnShutdown);
    (void) branch->GetBoolPref(CLEAR_ON_SHUTDOWN_PREF,
                               &mClearCacheOnShutdown);

    return rv;
}

nsresult
nsCacheService::DispatchToCacheIOThread(nsIRunnable* event)
{
    if (!gService->mCacheIOThread) return NS_ERROR_NOT_AVAILABLE;
    return gService->mCacheIOThread->Dispatch(event, NS_DISPATCH_NORMAL);
}

nsresult
nsCacheService::SyncWithCacheIOThread()
{
    gService->mLock.AssertCurrentThreadOwns();
    if (!gService->mCacheIOThread) return NS_ERROR_NOT_AVAILABLE;

    nsCOMPtr<nsIRunnable> event = new nsBlockOnCacheThreadEvent();

    // dispatch event - it will notify the monitor when it's done
    nsresult rv =
        gService->mCacheIOThread->Dispatch(event, NS_DISPATCH_NORMAL);
    if (NS_FAILED(rv)) {
        NS_WARNING("Failed dispatching block-event");
        return NS_ERROR_UNEXPECTED;
    }

    // wait until notified, then return
    rv = gService->mCondVar.Wait();

    return rv;
}


bool
nsCacheProfilePrefObserver::DiskCacheEnabled()
{
    if ((mDiskCacheCapacity == 0) || (!mDiskCacheParentDirectory))  return false;
    return mDiskCacheEnabled && (!mSanitizeOnShutdown || !mClearCacheOnShutdown);
}


bool
nsCacheProfilePrefObserver::OfflineCacheEnabled()
{
    if ((mOfflineCacheCapacity == 0) || (!mOfflineCacheParentDirectory))
        return false;

    return mOfflineCacheEnabled;
}


bool
nsCacheProfilePrefObserver::MemoryCacheEnabled()
{
    if (mMemoryCacheCapacity == 0)  return false;
    return mMemoryCacheEnabled;
}


/**
 * MemoryCacheCapacity
 *
 * If the browser.cache.memory.capacity preference is positive, we use that
 * value for the amount of memory available for the cache.
 *
 * If browser.cache.memory.capacity is zero, the memory cache is disabled.
 * 
 * If browser.cache.memory.capacity is negative or not present, we use a
 * formula that grows less than linearly with the amount of system memory, 
 * with an upper limit on the cache size. No matter how much physical RAM is
 * present, the default cache size would not exceed 32 MB. This maximum would
 * apply only to systems with more than 4 GB of RAM (e.g. terminal servers)
 *
 *   RAM   Cache
 *   ---   -----
 *   32 Mb   2 Mb
 *   64 Mb   4 Mb
 *  128 Mb   6 Mb
 *  256 Mb  10 Mb
 *  512 Mb  14 Mb
 * 1024 Mb  18 Mb
 * 2048 Mb  24 Mb
 * 4096 Mb  30 Mb
 *
 * The equation for this is (for cache size C and memory size K (kbytes)):
 *  x = log2(K) - 14
 *  C = x^2/3 + x + 2/3 + 0.1 (0.1 for rounding)
 *  if (C > 32) C = 32
 */

PRInt32
nsCacheProfilePrefObserver::MemoryCacheCapacity()
{
    PRInt32 capacity = mMemoryCacheCapacity;
    if (capacity >= 0) {
        CACHE_LOG_DEBUG(("Memory cache capacity forced to %d\n", capacity));
        return capacity;
    }

    static PRUint64 bytes = PR_GetPhysicalMemorySize();
    CACHE_LOG_DEBUG(("Physical Memory size is %llu\n", bytes));

    // If getting the physical memory failed, arbitrarily assume
    // 32 MB of RAM. We use a low default to have a reasonable
    // size on all the devices we support.
    if (bytes == 0)
        bytes = 32 * 1024 * 1024;

    // Conversion from unsigned int64 to double doesn't work on all platforms.
    // We need to truncate the value at LL_MAXINT to make sure we don't
    // overflow.
    if (LL_CMP(bytes, >, LL_MAXINT))
        bytes = LL_MAXINT;

    PRUint64 kbytes;
    LL_SHR(kbytes, bytes, 10);

    double kBytesD;
    LL_L2D(kBytesD, (PRInt64) kbytes);

    double x = log(kBytesD)/log(2.0) - 14;
    if (x > 0) {
        capacity = (PRInt32)(x * x / 3.0 + x + 2.0 / 3 + 0.1); // 0.1 for rounding
        if (capacity > 32)
            capacity = 32;
        capacity   *= 1024;
    } else {
        capacity    = 0;
    }

    return capacity;
}

PRInt32
nsCacheProfilePrefObserver::CacheCompressionLevel()
{
    return mCacheCompressionLevel;
}

/******************************************************************************
 * nsProcessRequestEvent
 *****************************************************************************/

class nsProcessRequestEvent : public nsRunnable {
public:
    nsProcessRequestEvent(nsCacheRequest *aRequest)
    {
        mRequest = aRequest;
    }

    NS_IMETHOD Run()
    {
        nsresult rv;

        NS_ASSERTION(mRequest->mListener,
                     "Sync OpenCacheEntry() posted to background thread!");

        nsCacheServiceAutoLock lock(LOCK_TELEM(NSPROCESSREQUESTEVENT_RUN));
        rv = nsCacheService::gService->ProcessRequest(mRequest,
                                                      false,
                                                      nsnull);

        // Don't delete the request if it was queued
        if (!(mRequest->IsBlocking() &&
            rv == NS_ERROR_CACHE_WAIT_FOR_VALIDATION))
            delete mRequest;

        return NS_OK;
    }

protected:
    virtual ~nsProcessRequestEvent() {}

private:
    nsCacheRequest *mRequest;
};

/******************************************************************************
 * nsNotifyDoomListener
 *****************************************************************************/

class nsNotifyDoomListener : public nsRunnable {
public:
    nsNotifyDoomListener(nsICacheListener *listener,
                         nsresult status)
        : mListener(listener)      // transfers reference
        , mStatus(status)
    {}

    NS_IMETHOD Run()
    {
        mListener->OnCacheEntryDoomed(mStatus);
        NS_RELEASE(mListener);
        return NS_OK;
    }

private:
    nsICacheListener *mListener;
    nsresult          mStatus;
};

/******************************************************************************
 * nsDoomEvent
 *****************************************************************************/

class nsDoomEvent : public nsRunnable {
public:
    nsDoomEvent(nsCacheSession *session,
                const nsACString &key,
                nsICacheListener *listener)
    {
        mKey = *session->ClientID();
        mKey.Append(':');
        mKey.Append(key);
        mStoragePolicy = session->StoragePolicy();
        mListener = listener;
        mThread = do_GetCurrentThread();
        // We addref the listener here and release it in nsNotifyDoomListener
        // on the callers thread. If posting of nsNotifyDoomListener event fails
        // we leak the listener which is better than releasing it on a wrong
        // thread.
        NS_IF_ADDREF(mListener);
    }

    NS_IMETHOD Run()
    {
        nsCacheServiceAutoLock lock(LOCK_TELEM(NSDOOMEVENT_RUN));

        bool foundActive = true;
        nsresult status = NS_ERROR_NOT_AVAILABLE;
        nsCacheEntry *entry;
        entry = nsCacheService::gService->mActiveEntries.GetEntry(&mKey);
        if (!entry) {
            bool collision = false;
            foundActive = false;
            entry = nsCacheService::gService->SearchCacheDevices(&mKey,
                                                                 mStoragePolicy,
                                                                 &collision);
        }

        if (entry) {
            status = NS_OK;
            nsCacheService::gService->DoomEntry_Internal(entry, foundActive);
        }

        if (mListener) {
            mThread->Dispatch(new nsNotifyDoomListener(mListener, status),
                              NS_DISPATCH_NORMAL);
            // posted event will release the reference on the correct thread
            mListener = nsnull;
        }

        return NS_OK;
    }

private:
    nsCString             mKey;
    nsCacheStoragePolicy  mStoragePolicy;
    nsICacheListener     *mListener;
    nsCOMPtr<nsIThread>   mThread;
};

/******************************************************************************
 * nsCacheService
 *****************************************************************************/
nsCacheService *   nsCacheService::gService = nsnull;

static nsCOMPtr<nsIMemoryReporter> MemoryCacheReporter = nsnull;

NS_THREADSAFE_MEMORY_REPORTER_IMPLEMENT(NetworkMemoryCache,
    "explicit/network-memory-cache",
    KIND_HEAP,
    UNITS_BYTES,
    nsCacheService::MemoryDeviceSize,
    "Memory used by the network memory cache.")

NS_IMPL_THREADSAFE_ISUPPORTS1(nsCacheService, nsICacheService)

nsCacheService::nsCacheService()
    : mLock("nsCacheService.mLock"),
      mCondVar(mLock, "nsCacheService.mCondVar"),
      mInitialized(false),
      mClearingEntries(false),
      mEnableMemoryDevice(true),
      mEnableDiskDevice(true),
      mMemoryDevice(nsnull),
      mDiskDevice(nsnull),
      mOfflineDevice(nsnull),
      mTotalEntries(0),
      mCacheHits(0),
      mCacheMisses(0),
      mMaxKeyLength(0),
      mMaxDataSize(0),
      mMaxMetaSize(0),
      mDeactivateFailures(0),
      mDeactivatedUnboundEntries(0)
{
    NS_ASSERTION(gService==nsnull, "multiple nsCacheService instances!");
    gService = this;

    // create list of cache devices
    PR_INIT_CLIST(&mDoomedEntries);
    mCustomOfflineDevices.Init();
}

nsCacheService::~nsCacheService()
{
    if (mInitialized) // Shutdown hasn't been called yet.
        (void) Shutdown();

    gService = nsnull;
}


nsresult
nsCacheService::Init()
{
    NS_TIME_FUNCTION;

    // Thie method must be called on the main thread because mCacheIOThread must
    // only be modified on the main thread.
    if (!NS_IsMainThread()) {
        NS_ERROR("nsCacheService::Init called off the main thread");
        return NS_ERROR_NOT_SAME_THREAD;
    }

    NS_ASSERTION(!mInitialized, "nsCacheService already initialized.");
    if (mInitialized)
        return NS_ERROR_ALREADY_INITIALIZED;

    if (mozilla::net::IsNeckoChild()) {
        return NS_ERROR_UNEXPECTED;
    }

    CACHE_LOG_INIT();

    nsresult rv = NS_NewNamedThread("Cache I/O",
                                    getter_AddRefs(mCacheIOThread));
    if (NS_FAILED(rv)) {
        NS_RUNTIMEABORT("Can't create cache IO thread");
    }

    rv = nsDeleteDir::Init();
    if (NS_FAILED(rv)) {
        NS_WARNING("Can't initialize nsDeleteDir");
    }

    // initialize hashtable for active cache entries
    rv = mActiveEntries.Init();
    if (NS_FAILED(rv)) return rv;
    
    // create profile/preference observer
    mObserver = new nsCacheProfilePrefObserver();
    if (!mObserver)  return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(mObserver);
    
    mObserver->Install();
    mEnableDiskDevice    = mObserver->DiskCacheEnabled();
    mEnableOfflineDevice = mObserver->OfflineCacheEnabled();
    mEnableMemoryDevice  = mObserver->MemoryCacheEnabled();

    mInitialized = true;
    return NS_OK;
}

// static
PLDHashOperator
nsCacheService::ShutdownCustomCacheDeviceEnum(const nsAString& aProfileDir,
                                              nsRefPtr<nsOfflineCacheDevice>& aDevice,
                                              void* aUserArg)
{
    aDevice->Shutdown();
    return PL_DHASH_REMOVE;
}

void
nsCacheService::Shutdown()
{
    // Thie method must be called on the main thread because mCacheIOThread must
    // only be modified on the main thread.
    if (!NS_IsMainThread()) {
        NS_RUNTIMEABORT("nsCacheService::Shutdown called off the main thread");
    }

    nsCOMPtr<nsIThread> cacheIOThread;
    Telemetry::AutoTimer<Telemetry::NETWORK_DISK_CACHE_SHUTDOWN> totalTimer;

    bool shouldSanitize = false;
    nsCOMPtr<nsIFile> parentDir;

    {
    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_SHUTDOWN));
    NS_ASSERTION(mInitialized, 
                 "can't shutdown nsCacheService unless it has been initialized.");

    if (mInitialized) {

        mInitialized = false;

        // Clear entries
        ClearDoomList();
        ClearActiveEntries();

        if (mSmartSizeTimer) {
            mSmartSizeTimer->Cancel();
            mSmartSizeTimer = nsnull;
        }

        // Make sure to wait for any pending cache-operations before
        // proceeding with destructive actions (bug #620660)
        (void) SyncWithCacheIOThread();

        // obtain the disk cache directory in case we need to sanitize it
        parentDir = mObserver->DiskCacheParentDirectory();
        shouldSanitize = mObserver->SanitizeAtShutdown();
        mObserver->Remove();
        NS_RELEASE(mObserver);
        
        // unregister memory reporter, before deleting the memory device, just
        // to be safe
        NS_UnregisterMemoryReporter(MemoryCacheReporter);
        MemoryCacheReporter = nsnull;

        // deallocate memory and disk caches
        delete mMemoryDevice;
        mMemoryDevice = nsnull;

        delete mDiskDevice;
        mDiskDevice = nsnull;

        if (mOfflineDevice)
            mOfflineDevice->Shutdown();

        NS_IF_RELEASE(mOfflineDevice);

        mCustomOfflineDevices.Enumerate(&nsCacheService::ShutdownCustomCacheDeviceEnum, nsnull);

#ifdef PR_LOGGING
        LogCacheStatistics();
#endif

        mCacheIOThread.swap(cacheIOThread);
    }
    } // lock

    if (cacheIOThread)
        cacheIOThread->Shutdown();

    if (shouldSanitize) {
        nsresult rv = parentDir->AppendNative(NS_LITERAL_CSTRING("Cache"));
        if (NS_SUCCEEDED(rv)) {
            bool exists;
            if (NS_SUCCEEDED(parentDir->Exists(&exists)) && exists)
                nsDeleteDir::DeleteDir(parentDir, false);
        }
        Telemetry::AutoTimer<Telemetry::NETWORK_DISK_CACHE_SHUTDOWN_CLEAR_PRIVATE> timer;
        nsDeleteDir::Shutdown(shouldSanitize);
    } else {
        Telemetry::AutoTimer<Telemetry::NETWORK_DISK_CACHE_DELETEDIR_SHUTDOWN> timer;
        nsDeleteDir::Shutdown(shouldSanitize);
    }
}


nsresult
nsCacheService::Create(nsISupports* aOuter, const nsIID& aIID, void* *aResult)
{
    nsresult  rv;

    if (aOuter != nsnull)
        return NS_ERROR_NO_AGGREGATION;

    nsCacheService * cacheService = new nsCacheService();
    if (cacheService == nsnull)
        return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(cacheService);
    rv = cacheService->Init();
    if (NS_SUCCEEDED(rv)) {
        rv = cacheService->QueryInterface(aIID, aResult);
    }
    NS_RELEASE(cacheService);
    return rv;
}


NS_IMETHODIMP
nsCacheService::CreateSession(const char *          clientID,
                              nsCacheStoragePolicy  storagePolicy, 
                              bool                  streamBased,
                              nsICacheSession     **result)
{
    *result = nsnull;

    if (this == nsnull)  return NS_ERROR_NOT_AVAILABLE;

    nsCacheSession * session = new nsCacheSession(clientID, storagePolicy, streamBased);
    if (!session)  return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(*result = session);

    return NS_OK;
}


nsresult
nsCacheService::EvictEntriesForSession(nsCacheSession * session)
{
    NS_ASSERTION(gService, "nsCacheService::gService is null.");
    return gService->EvictEntriesForClient(session->ClientID()->get(),
                                 session->StoragePolicy());
}

namespace {

class EvictionNotifierRunnable : public nsRunnable
{
public:
    EvictionNotifierRunnable(nsISupports* aSubject)
        : mSubject(aSubject)
    { }

    NS_DECL_NSIRUNNABLE

private:
    nsCOMPtr<nsISupports> mSubject;
};

NS_IMETHODIMP
EvictionNotifierRunnable::Run()
{
    nsCOMPtr<nsIObserverService> obsSvc =
        mozilla::services::GetObserverService();
    if (obsSvc) {
        obsSvc->NotifyObservers(mSubject,
                                NS_CACHESERVICE_EMPTYCACHE_TOPIC_ID,
                                nsnull);
    }
    return NS_OK;
}

} // anonymous namespace

nsresult
nsCacheService::EvictEntriesForClient(const char *          clientID,
                                      nsCacheStoragePolicy  storagePolicy)
{
    nsRefPtr<EvictionNotifierRunnable> r = new EvictionNotifierRunnable(this);
    NS_DispatchToMainThread(r);

    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_EVICTENTRIESFORCLIENT));
    nsresult res = NS_OK;

    if (storagePolicy == nsICache::STORE_ANYWHERE ||
        storagePolicy == nsICache::STORE_ON_DISK) {

        if (mEnableDiskDevice) {
            nsresult rv = NS_OK;
            if (!mDiskDevice)
                rv = CreateDiskDevice();
            if (mDiskDevice)
                rv = mDiskDevice->EvictEntries(clientID);
            if (NS_FAILED(rv))
                res = rv;
        }
    }

    // Only clear the offline cache if it has been specifically asked for.
    if (storagePolicy == nsICache::STORE_OFFLINE) {
        if (mEnableOfflineDevice) {
            nsresult rv = NS_OK;
            if (!mOfflineDevice)
                rv = CreateOfflineDevice();
            if (mOfflineDevice)
                rv = mOfflineDevice->EvictEntries(clientID);
            if (NS_FAILED(rv))
                res = rv;
        }
    }

    if (storagePolicy == nsICache::STORE_ANYWHERE ||
        storagePolicy == nsICache::STORE_IN_MEMORY) {
        // If there is no memory device, there is no need to evict it...
        if (mMemoryDevice) {
            nsresult rv = mMemoryDevice->EvictEntries(clientID);
            if (NS_FAILED(rv))
                res = rv;
        }
    }

    return res;
}


nsresult        
nsCacheService::IsStorageEnabledForPolicy(nsCacheStoragePolicy  storagePolicy,
                                          bool *              result)
{
    if (gService == nsnull) return NS_ERROR_NOT_AVAILABLE;
    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_ISSTORAGEENABLEDFORPOLICY));

    *result = gService->IsStorageEnabledForPolicy_Locked(storagePolicy);
    return NS_OK;
}


nsresult
nsCacheService::DoomEntry(nsCacheSession   *session,
                          const nsACString &key,
                          nsICacheListener *listener)
{
    CACHE_LOG_DEBUG(("Dooming entry for session %p, key %s\n",
                     session, PromiseFlatCString(key).get()));
    NS_ASSERTION(gService, "nsCacheService::gService is null.");

    if (!gService->mInitialized)
        return NS_ERROR_NOT_INITIALIZED;

    return DispatchToCacheIOThread(new nsDoomEvent(session, key, listener));
}


bool          
nsCacheService::IsStorageEnabledForPolicy_Locked(nsCacheStoragePolicy  storagePolicy)
{
    if (gService->mEnableMemoryDevice &&
        (storagePolicy == nsICache::STORE_ANYWHERE ||
         storagePolicy == nsICache::STORE_IN_MEMORY)) {
        return true;
    }
    if (gService->mEnableDiskDevice &&
        (storagePolicy == nsICache::STORE_ANYWHERE ||
         storagePolicy == nsICache::STORE_ON_DISK  ||
         storagePolicy == nsICache::STORE_ON_DISK_AS_FILE)) {
        return true;
    }
    if (gService->mEnableOfflineDevice &&
        storagePolicy == nsICache::STORE_OFFLINE) {
        return true;
    }
    
    return false;
}

NS_IMETHODIMP nsCacheService::VisitEntries(nsICacheVisitor *visitor)
{
    NS_ENSURE_ARG_POINTER(visitor);

    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_VISITENTRIES));

    if (!(mEnableDiskDevice || mEnableMemoryDevice))
        return NS_ERROR_NOT_AVAILABLE;

    // XXX record the fact that a visitation is in progress, 
    // XXX i.e. keep list of visitors in progress.
    
    nsresult rv = NS_OK;
    // If there is no memory device, there are then also no entries to visit...
    if (mMemoryDevice) {
        rv = mMemoryDevice->Visit(visitor);
        if (NS_FAILED(rv)) return rv;
    }

    if (mEnableDiskDevice) {
        if (!mDiskDevice) {
            rv = CreateDiskDevice();
            if (NS_FAILED(rv)) return rv;
        }
        rv = mDiskDevice->Visit(visitor);
        if (NS_FAILED(rv)) return rv;
    }

    if (mEnableOfflineDevice) {
        if (!mOfflineDevice) {
            rv = CreateOfflineDevice();
            if (NS_FAILED(rv)) return rv;
        }
        rv = mOfflineDevice->Visit(visitor);
        if (NS_FAILED(rv)) return rv;
    }

    // XXX notify any shutdown process that visitation is complete for THIS visitor.
    // XXX keep queue of visitors

    return NS_OK;
}


NS_IMETHODIMP nsCacheService::EvictEntries(nsCacheStoragePolicy storagePolicy)
{
    return  EvictEntriesForClient(nsnull, storagePolicy);
}

NS_IMETHODIMP nsCacheService::GetCacheIOTarget(nsIEventTarget * *aCacheIOTarget)
{
    NS_ENSURE_ARG_POINTER(aCacheIOTarget);

    // Because mCacheIOThread can only be changed on the main thread, it can be
    // read from the main thread without the lock. This is useful to prevent
    // blocking the main thread on other cache operations.
    if (!NS_IsMainThread()) {
        Lock(LOCK_TELEM(NSCACHESERVICE_GETCACHEIOTARGET));
    }

    nsresult rv;
    if (mCacheIOThread) {
        NS_ADDREF(*aCacheIOTarget = mCacheIOThread);
        rv = NS_OK;
    } else {
        *aCacheIOTarget = nsnull;
        rv = NS_ERROR_NOT_AVAILABLE;
    }

    if (!NS_IsMainThread()) {
        Unlock();
    }

    return rv;
}

/**
 * Internal Methods
 */
nsresult
nsCacheService::CreateDiskDevice()
{
    if (!mInitialized)      return NS_ERROR_NOT_AVAILABLE;
    if (!mEnableDiskDevice) return NS_ERROR_NOT_AVAILABLE;
    if (mDiskDevice)        return NS_OK;

    mDiskDevice = new nsDiskCacheDevice;
    if (!mDiskDevice)       return NS_ERROR_OUT_OF_MEMORY;

    // set the preferences
    mDiskDevice->SetCacheParentDirectory(mObserver->DiskCacheParentDirectory());
    mDiskDevice->SetCapacity(mObserver->DiskCacheCapacity());
    mDiskDevice->SetMaxEntrySize(mObserver->DiskCacheMaxEntrySize());
    
    nsresult rv = mDiskDevice->Init();
    if (NS_FAILED(rv)) {
#if DEBUG
        printf("###\n");
        printf("### mDiskDevice->Init() failed (0x%.8x)\n", rv);
        printf("###    - disabling disk cache for this session.\n");
        printf("###\n");
#endif        
        mEnableDiskDevice = false;
        delete mDiskDevice;
        mDiskDevice = nsnull;
        return rv;
    }

    NS_ASSERTION(!mSmartSizeTimer, "Smartsize timer was already fired!");

    // Disk device is usually created during the startup. Delay smart size
    // calculation to avoid possible massive IO caused by eviction of entries
    // in case the new smart size is smaller than current cache usage.
    mSmartSizeTimer = do_CreateInstance("@mozilla.org/timer;1", &rv);
    if (NS_SUCCEEDED(rv)) {
        rv = mSmartSizeTimer->InitWithCallback(new nsSetDiskSmartSizeCallback(),
                                               1000*60*3,
                                               nsITimer::TYPE_ONE_SHOT);
        if (NS_FAILED(rv)) {
            NS_WARNING("Failed to post smart size timer");
            mSmartSizeTimer = nsnull;
        }
    } else {
        NS_WARNING("Can't create smart size timer");
    }
    // Ignore state of the timer and return success since the purpose of the
    // method (create the disk-device) has been fulfilled

    return NS_OK;
}

nsresult
nsCacheService::GetOfflineDevice(nsOfflineCacheDevice **aDevice)
{
    if (!mOfflineDevice) {
        nsresult rv = CreateOfflineDevice();
        NS_ENSURE_SUCCESS(rv, rv);
    }

    NS_ADDREF(*aDevice = mOfflineDevice);
    return NS_OK;
}

nsresult
nsCacheService::GetCustomOfflineDevice(nsIFile *aProfileDir,
                                       PRInt32 aQuota,
                                       nsOfflineCacheDevice **aDevice)
{
    nsresult rv;

    nsAutoString profilePath;
    rv = aProfileDir->GetPath(profilePath);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!mCustomOfflineDevices.Get(profilePath, aDevice)) {
        rv = CreateCustomOfflineDevice(aProfileDir, aQuota, aDevice);
        NS_ENSURE_SUCCESS(rv, rv);

        (*aDevice)->SetAutoShutdown();
        mCustomOfflineDevices.Put(profilePath, *aDevice);
    }

    return NS_OK;
}

nsresult
nsCacheService::CreateOfflineDevice()
{
    CACHE_LOG_ALWAYS(("Creating default offline device"));

    if (mOfflineDevice)        return NS_OK;
    if (!mObserver)            return NS_ERROR_NOT_AVAILABLE;

    nsresult rv = CreateCustomOfflineDevice(
        mObserver->OfflineCacheParentDirectory(),
        mObserver->OfflineCacheCapacity(),
        &mOfflineDevice);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
}

nsresult
nsCacheService::CreateCustomOfflineDevice(nsIFile *aProfileDir,
                                          PRInt32 aQuota,
                                          nsOfflineCacheDevice **aDevice)
{
    NS_ENSURE_ARG(aProfileDir);

#if defined(PR_LOGGING)
    nsCAutoString profilePath;
    aProfileDir->GetNativePath(profilePath);
    CACHE_LOG_ALWAYS(("Creating custom offline device, %s, %d",
                      profilePath.BeginReading(), aQuota));
#endif

    if (!mInitialized)         return NS_ERROR_NOT_AVAILABLE;
    if (!mEnableOfflineDevice) return NS_ERROR_NOT_AVAILABLE;

    *aDevice = new nsOfflineCacheDevice;

    NS_ADDREF(*aDevice);

    // set the preferences
    (*aDevice)->SetCacheParentDirectory(aProfileDir);
    (*aDevice)->SetCapacity(aQuota);

    nsresult rv = (*aDevice)->Init();
    if (NS_FAILED(rv)) {
        CACHE_LOG_DEBUG(("OfflineDevice->Init() failed (0x%.8x)\n", rv));
        CACHE_LOG_DEBUG(("    - disabling offline cache for this session.\n"));

        NS_RELEASE(*aDevice);
    }
    return rv;
}

nsresult
nsCacheService::CreateMemoryDevice()
{
    if (!mInitialized)        return NS_ERROR_NOT_AVAILABLE;
    if (!mEnableMemoryDevice) return NS_ERROR_NOT_AVAILABLE;
    if (mMemoryDevice)        return NS_OK;

    mMemoryDevice = new nsMemoryCacheDevice;
    if (!mMemoryDevice)       return NS_ERROR_OUT_OF_MEMORY;
    
    // set preference
    PRInt32 capacity = mObserver->MemoryCacheCapacity();
    CACHE_LOG_DEBUG(("Creating memory device with capacity %d\n", capacity));
    mMemoryDevice->SetCapacity(capacity);
    mMemoryDevice->SetMaxEntrySize(mObserver->MemoryCacheMaxEntrySize());

    nsresult rv = mMemoryDevice->Init();
    if (NS_FAILED(rv)) {
        NS_WARNING("Initialization of Memory Cache failed.");
        delete mMemoryDevice;
        mMemoryDevice = nsnull;
    }

    MemoryCacheReporter =
        new NS_MEMORY_REPORTER_NAME(NetworkMemoryCache);
    NS_RegisterMemoryReporter(MemoryCacheReporter);

    return rv;
}

nsresult
nsCacheService::RemoveCustomOfflineDevice(nsOfflineCacheDevice *aDevice)
{
    nsCOMPtr<nsIFile> profileDir = aDevice->BaseDirectory();
    if (!profileDir)
        return NS_ERROR_UNEXPECTED;

    nsAutoString profilePath;
    nsresult rv = profileDir->GetPath(profilePath);
    NS_ENSURE_SUCCESS(rv, rv);

    mCustomOfflineDevices.Remove(profilePath);
    return NS_OK;
}

nsresult
nsCacheService::CreateRequest(nsCacheSession *   session,
                              const nsACString & clientKey,
                              nsCacheAccessMode  accessRequested,
                              bool               blockingMode,
                              nsICacheListener * listener,
                              nsCacheRequest **  request)
{
    NS_ASSERTION(request, "CreateRequest: request is null");
     
    nsCString * key = new nsCString(*session->ClientID());
    if (!key)
        return NS_ERROR_OUT_OF_MEMORY;
    key->Append(':');
    key->Append(clientKey);

    if (mMaxKeyLength < key->Length()) mMaxKeyLength = key->Length();

    // create request
    *request = new  nsCacheRequest(key, listener, accessRequested, blockingMode, session);    
    if (!*request) {
        delete key;
        return NS_ERROR_OUT_OF_MEMORY;
    }

    if (!listener)  return NS_OK;  // we're sync, we're done.

    // get the request's thread
    (*request)->mThread = do_GetCurrentThread();
    
    return NS_OK;
}


class nsCacheListenerEvent : public nsRunnable
{
public:
    nsCacheListenerEvent(nsICacheListener *listener,
                         nsICacheEntryDescriptor *descriptor,
                         nsCacheAccessMode accessGranted,
                         nsresult status)
        : mListener(listener)      // transfers reference
        , mDescriptor(descriptor)  // transfers reference (may be null)
        , mAccessGranted(accessGranted)
        , mStatus(status)
    {}

    NS_IMETHOD Run()
    {
        mListener->OnCacheEntryAvailable(mDescriptor, mAccessGranted, mStatus);

        NS_RELEASE(mListener);
        NS_IF_RELEASE(mDescriptor);
        return NS_OK;
    }

private:
    // We explicitly leak mListener or mDescriptor if Run is not called
    // because otherwise we cannot guarantee that they are destroyed on
    // the right thread.

    nsICacheListener        *mListener;
    nsICacheEntryDescriptor *mDescriptor;
    nsCacheAccessMode        mAccessGranted;
    nsresult                 mStatus;
};


nsresult
nsCacheService::NotifyListener(nsCacheRequest *          request,
                               nsICacheEntryDescriptor * descriptor,
                               nsCacheAccessMode         accessGranted,
                               nsresult                  status)
{
    NS_ASSERTION(request->mThread, "no thread set in async request!");

    // Swap ownership, and release listener on target thread...
    nsICacheListener *listener = request->mListener;
    request->mListener = nsnull;

    nsCOMPtr<nsIRunnable> ev =
            new nsCacheListenerEvent(listener, descriptor,
                                     accessGranted, status);
    if (!ev) {
        // Better to leak listener and descriptor if we fail because we don't
        // want to destroy them inside the cache service lock or on potentially
        // the wrong thread.
        return NS_ERROR_OUT_OF_MEMORY;
    }

    return request->mThread->Dispatch(ev, NS_DISPATCH_NORMAL);
}


nsresult
nsCacheService::ProcessRequest(nsCacheRequest *           request,
                               bool                       calledFromOpenCacheEntry,
                               nsICacheEntryDescriptor ** result)
{
    // !!! must be called with mLock held !!!
    nsresult           rv;
    nsCacheEntry *     entry = nsnull;
    nsCacheEntry *     doomedEntry = nsnull;
    nsCacheAccessMode  accessGranted = nsICache::ACCESS_NONE;
    if (result) *result = nsnull;

    while(1) {  // Activate entry loop
        rv = ActivateEntry(request, &entry, &doomedEntry);  // get the entry for this request
        if (NS_FAILED(rv))  break;

        while(1) { // Request Access loop
            NS_ASSERTION(entry, "no entry in Request Access loop!");
            // entry->RequestAccess queues request on entry
            rv = entry->RequestAccess(request, &accessGranted);
            if (rv != NS_ERROR_CACHE_WAIT_FOR_VALIDATION) break;

            if (request->IsBlocking()) {
                if (request->mListener) {
                    // async exits - validate, doom, or close will resume
                    return rv;
                }

                // XXX this is probably wrong...
                Unlock();
                rv = request->WaitForValidation();
                Lock(LOCK_TELEM(NSCACHESERVICE_PROCESSREQUEST));
            }

            PR_REMOVE_AND_INIT_LINK(request);
            if (NS_FAILED(rv)) break;   // non-blocking mode returns WAIT_FOR_VALIDATION error
            // okay, we're ready to process this request, request access again
        }
        if (rv != NS_ERROR_CACHE_ENTRY_DOOMED)  break;

        if (entry->IsNotInUse()) {
            // this request was the last one keeping it around, so get rid of it
            DeactivateEntry(entry);
        }
        // loop back around to look for another entry
    }

    if (NS_SUCCEEDED(rv) && request->mProfileDir) {
        // Custom cache directory has been demanded.  Preset the cache device.
        if (entry->StoragePolicy() != nsICache::STORE_OFFLINE) {
            // Failsafe check: this is implemented only for offline cache atm.
            rv = NS_ERROR_FAILURE;
        } else {
            nsRefPtr<nsOfflineCacheDevice> customCacheDevice;
            rv = GetCustomOfflineDevice(request->mProfileDir, -1,
                                        getter_AddRefs(customCacheDevice));
            if (NS_SUCCEEDED(rv))
                entry->SetCustomCacheDevice(customCacheDevice);
        }
    }

    nsICacheEntryDescriptor *descriptor = nsnull;
    
    if (NS_SUCCEEDED(rv))
        rv = entry->CreateDescriptor(request, accessGranted, &descriptor);

    // If doomedEntry is set, ActivatEntry() doomed an existing entry and
    // created a new one for that cache-key. However, any pending requests
    // on the doomed entry were not processed and we need to do that here.
    // This must be done after adding the created entry to list of active
    // entries (which is done in ActivateEntry()) otherwise the hashkeys crash
    // (see bug ##561313). It is also important to do this after creating a
    // descriptor for this request, or some other request may end up being
    // executed first for the newly created entry.
    // Finally, it is worth to emphasize that if doomedEntry is set,
    // ActivateEntry() created a new entry for the request, which will be
    // initialized by RequestAccess() and they both should have returned NS_OK.
    if (doomedEntry) {
        (void) ProcessPendingRequests(doomedEntry);
        if (doomedEntry->IsNotInUse())
            DeactivateEntry(doomedEntry);
        doomedEntry = nsnull;
    }

    if (request->mListener) {  // Asynchronous
    
        if (NS_FAILED(rv) && calledFromOpenCacheEntry && request->IsBlocking())
            return rv;  // skip notifying listener, just return rv to caller
            
        // call listener to report error or descriptor
        nsresult rv2 = NotifyListener(request, descriptor, accessGranted, rv);
        if (NS_FAILED(rv2) && NS_SUCCEEDED(rv)) {
            rv = rv2;  // trigger delete request
        }
    } else {        // Synchronous
        *result = descriptor;
    }
    return rv;
}


nsresult
nsCacheService::OpenCacheEntry(nsCacheSession *           session,
                               const nsACString &         key,
                               nsCacheAccessMode          accessRequested,
                               bool                       blockingMode,
                               nsICacheListener *         listener,
                               nsICacheEntryDescriptor ** result)
{
    CACHE_LOG_DEBUG(("Opening entry for session %p, key %s, mode %d, blocking %d\n",
                     session, PromiseFlatCString(key).get(), accessRequested,
                     blockingMode));
    NS_ASSERTION(gService, "nsCacheService::gService is null.");
    if (result)
        *result = nsnull;

    if (!gService->mInitialized)
        return NS_ERROR_NOT_INITIALIZED;

    nsCacheRequest * request = nsnull;

    nsresult rv = gService->CreateRequest(session,
                                          key,
                                          accessRequested,
                                          blockingMode,
                                          listener,
                                          &request);
    if (NS_FAILED(rv))  return rv;

    CACHE_LOG_DEBUG(("Created request %p\n", request));

    // Process the request on the background thread if we are on the main thread
    // and the the request is asynchronous
    if (NS_IsMainThread() && listener && gService->mCacheIOThread) {
        nsCOMPtr<nsIRunnable> ev =
            new nsProcessRequestEvent(request);
        rv = DispatchToCacheIOThread(ev);

        // delete request if we didn't post the event
        if (NS_FAILED(rv))
            delete request;
    }
    else {

        nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_OPENCACHEENTRY));
        rv = gService->ProcessRequest(request, true, result);

        // delete requests that have completed
        if (!(listener && blockingMode &&
            (rv == NS_ERROR_CACHE_WAIT_FOR_VALIDATION)))
            delete request;
    }

    return rv;
}


nsresult
nsCacheService::ActivateEntry(nsCacheRequest * request, 
                              nsCacheEntry ** result,
                              nsCacheEntry ** doomedEntry)
{
    CACHE_LOG_DEBUG(("Activate entry for request %p\n", request));
    if (!mInitialized || mClearingEntries)
        return NS_ERROR_NOT_AVAILABLE;

    nsresult        rv = NS_OK;

    NS_ASSERTION(request != nsnull, "ActivateEntry called with no request");
    if (result) *result = nsnull;
    if (doomedEntry) *doomedEntry = nsnull;
    if ((!request) || (!result) || (!doomedEntry))
        return NS_ERROR_NULL_POINTER;

    // check if the request can be satisfied
    if (!mEnableMemoryDevice && !request->IsStreamBased())
        return NS_ERROR_FAILURE;
    if (!IsStorageEnabledForPolicy_Locked(request->StoragePolicy()))
        return NS_ERROR_FAILURE;

    // search active entries (including those not bound to device)
    nsCacheEntry *entry = mActiveEntries.GetEntry(request->mKey);
    CACHE_LOG_DEBUG(("Active entry for request %p is %p\n", request, entry));

    if (!entry) {
        // search cache devices for entry
        bool collision = false;
        entry = SearchCacheDevices(request->mKey, request->StoragePolicy(), &collision);
        CACHE_LOG_DEBUG(("Device search for request %p returned %p\n",
                         request, entry));
        // When there is a hashkey collision just refuse to cache it...
        if (collision) return NS_ERROR_CACHE_IN_USE;

        if (entry)  entry->MarkInitialized();
    } else {
        NS_ASSERTION(entry->IsActive(), "Inactive entry found in mActiveEntries!");
    }

    if (entry) {
        ++mCacheHits;
        entry->Fetched();
    } else {
        ++mCacheMisses;
    }

    if (entry &&
        ((request->AccessRequested() == nsICache::ACCESS_WRITE) ||
         ((request->StoragePolicy() != nsICache::STORE_OFFLINE) &&
          (entry->mExpirationTime <= SecondsFromPRTime(PR_Now()) &&
           request->WillDoomEntriesIfExpired()))))

    {
        // this is FORCE-WRITE request or the entry has expired
        // we doom entry without processing pending requests, but store it in
        // doomedEntry which causes pending requests to be processed below
        rv = DoomEntry_Internal(entry, false);
        *doomedEntry = entry;
        if (NS_FAILED(rv)) {
            // XXX what to do?  Increment FailedDooms counter?
        }
        entry = nsnull;
    }

    if (!entry) {
        if (! (request->AccessRequested() & nsICache::ACCESS_WRITE)) {
            // this is a READ-ONLY request
            rv = NS_ERROR_CACHE_KEY_NOT_FOUND;
            goto error;
        }

        entry = new nsCacheEntry(request->mKey,
                                 request->IsStreamBased(),
                                 request->StoragePolicy());
        if (!entry)
            return NS_ERROR_OUT_OF_MEMORY;

        if (request->IsPrivate())
            entry->MarkPrivate();
        
        entry->Fetched();
        ++mTotalEntries;

        // XXX  we could perform an early bind in some cases based on storage policy
    }

    if (!entry->IsActive()) {
        rv = mActiveEntries.AddEntry(entry);
        if (NS_FAILED(rv)) goto error;
        CACHE_LOG_DEBUG(("Added entry %p to mActiveEntries\n", entry));
        entry->MarkActive();  // mark entry active, because it's now in mActiveEntries
    }
    *result = entry;
    return NS_OK;
    
 error:
    *result = nsnull;
    delete entry;
    return rv;
}


nsCacheEntry *
nsCacheService::SearchCacheDevices(nsCString * key, nsCacheStoragePolicy policy, bool *collision)
{
    Telemetry::AutoTimer<Telemetry::CACHE_DEVICE_SEARCH> timer;
    nsCacheEntry * entry = nsnull;

    CACHE_LOG_DEBUG(("mMemoryDevice: 0x%p\n", mMemoryDevice));

    *collision = false;
    if ((policy == nsICache::STORE_ANYWHERE) || (policy == nsICache::STORE_IN_MEMORY)) {
        // If there is no memory device, then there is nothing to search...
        if (mMemoryDevice) {
            entry = mMemoryDevice->FindEntry(key, collision);
            CACHE_LOG_DEBUG(("Searching mMemoryDevice for key %s found: 0x%p, "
                             "collision: %d\n", key->get(), entry, collision));
        }
    }

    if (!entry && 
        ((policy == nsICache::STORE_ANYWHERE) || (policy == nsICache::STORE_ON_DISK))) {

        if (mEnableDiskDevice) {
            if (!mDiskDevice) {
                nsresult rv = CreateDiskDevice();
                if (NS_FAILED(rv))
                    return nsnull;
            }
            
            entry = mDiskDevice->FindEntry(key, collision);
        }
    }

    if (!entry && (policy == nsICache::STORE_OFFLINE ||
                   (policy == nsICache::STORE_ANYWHERE &&
                    gIOService->IsOffline()))) {

        if (mEnableOfflineDevice) {
            if (!mOfflineDevice) {
                nsresult rv = CreateOfflineDevice();
                if (NS_FAILED(rv))
                    return nsnull;
            }

            entry = mOfflineDevice->FindEntry(key, collision);
        }
    }

    return entry;
}


nsCacheDevice *
nsCacheService::EnsureEntryHasDevice(nsCacheEntry * entry)
{
    nsCacheDevice * device = entry->CacheDevice();
    // return device if found, possibly null if the entry is doomed i.e prevent
    // doomed entries to bind to a device (see e.g. bugs #548406 and #596443)
    if (device || entry->IsDoomed())  return device;

    PRInt64 predictedDataSize = entry->PredictedDataSize();
    if (entry->IsStreamData() && entry->IsAllowedOnDisk() && mEnableDiskDevice) {
        // this is the default
        if (!mDiskDevice) {
            (void)CreateDiskDevice();  // ignore the error (check for mDiskDevice instead)
        }

        if (mDiskDevice) {
            // Bypass the cache if Content-Length says the entry will be too big
            if (predictedDataSize != -1 &&
                entry->StoragePolicy() != nsICache::STORE_ON_DISK_AS_FILE &&
                mDiskDevice->EntryIsTooBig(predictedDataSize)) {
                DebugOnly<nsresult> rv = nsCacheService::DoomEntry(entry);
                NS_ASSERTION(NS_SUCCEEDED(rv),"DoomEntry() failed.");
                return nsnull;
            }

            entry->MarkBinding();  // enter state of binding
            nsresult rv = mDiskDevice->BindEntry(entry);
            entry->ClearBinding(); // exit state of binding
            if (NS_SUCCEEDED(rv))
                device = mDiskDevice;
        }
    }

    // if we can't use mDiskDevice, try mMemoryDevice
    if (!device && mEnableMemoryDevice && entry->IsAllowedInMemory()) {        
        if (!mMemoryDevice) {
            (void)CreateMemoryDevice();  // ignore the error (check for mMemoryDevice instead)
        }
        if (mMemoryDevice) {
            // Bypass the cache if Content-Length says entry will be too big
            if (predictedDataSize != -1 &&
                mMemoryDevice->EntryIsTooBig(predictedDataSize)) {
                DebugOnly<nsresult> rv = nsCacheService::DoomEntry(entry);
                NS_ASSERTION(NS_SUCCEEDED(rv),"DoomEntry() failed.");
                return nsnull;
            }

            entry->MarkBinding();  // enter state of binding
            nsresult rv = mMemoryDevice->BindEntry(entry);
            entry->ClearBinding(); // exit state of binding
            if (NS_SUCCEEDED(rv))
                device = mMemoryDevice;
        }
    }

    if (!device && entry->IsStreamData() &&
        entry->IsAllowedOffline() && mEnableOfflineDevice) {
        if (!mOfflineDevice) {
            (void)CreateOfflineDevice(); // ignore the error (check for mOfflineDevice instead)
        }

        device = entry->CustomCacheDevice()
               ? entry->CustomCacheDevice()
               : mOfflineDevice;

        if (device) {
            entry->MarkBinding();
            nsresult rv = device->BindEntry(entry);
            entry->ClearBinding();
            if (NS_FAILED(rv))
                device = nsnull;
        }
    }

    if (device) 
        entry->SetCacheDevice(device);
    return device;
}

PRInt64
nsCacheService::MemoryDeviceSize()
{
    nsMemoryCacheDevice *memoryDevice = GlobalInstance()->mMemoryDevice;
    return memoryDevice ? memoryDevice->TotalSize() : 0;
}

nsresult
nsCacheService::DoomEntry(nsCacheEntry * entry)
{
    return gService->DoomEntry_Internal(entry, true);
}


nsresult
nsCacheService::DoomEntry_Internal(nsCacheEntry * entry,
                                   bool doProcessPendingRequests)
{
    if (entry->IsDoomed())  return NS_OK;
    
    CACHE_LOG_DEBUG(("Dooming entry %p\n", entry));
    nsresult  rv = NS_OK;
    entry->MarkDoomed();
    
    NS_ASSERTION(!entry->IsBinding(), "Dooming entry while binding device.");
    nsCacheDevice * device = entry->CacheDevice();
    if (device)  device->DoomEntry(entry);

    if (entry->IsActive()) {
        // remove from active entries
        mActiveEntries.RemoveEntry(entry);
        CACHE_LOG_DEBUG(("Removed entry %p from mActiveEntries\n", entry));
        entry->MarkInactive();
     }

    // put on doom list to wait for descriptors to close
    NS_ASSERTION(PR_CLIST_IS_EMPTY(entry), "doomed entry still on device list");
    PR_APPEND_LINK(entry, &mDoomedEntries);

    // handle pending requests only if we're supposed to
    if (doProcessPendingRequests) {
        // tell pending requests to get on with their lives...
        rv = ProcessPendingRequests(entry);

        // All requests have been removed, but there may still be open descriptors
        if (entry->IsNotInUse()) {
            DeactivateEntry(entry); // tell device to get rid of it
        }
    }
    return rv;
}


void
nsCacheService::OnProfileShutdown(bool cleanse)
{
    if (!gService)  return;
    if (!gService->mInitialized) {
        // The cache service has been shut down, but someone is still holding
        // a reference to it. Ignore this call.
        return;
    }
    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_ONPROFILESHUTDOWN));
    gService->mClearingEntries = true;

    gService->DoomActiveEntries(nsnull);
    gService->ClearDoomList();

    // Make sure to wait for any pending cache-operations before
    // proceeding with destructive actions (bug #620660)
    (void) SyncWithCacheIOThread();

    if (gService->mDiskDevice && gService->mEnableDiskDevice) {
        if (cleanse)
            gService->mDiskDevice->EvictEntries(nsnull);

        gService->mDiskDevice->Shutdown();
    }
    gService->mEnableDiskDevice = false;

    if (gService->mOfflineDevice && gService->mEnableOfflineDevice) {
        if (cleanse)
            gService->mOfflineDevice->EvictEntries(nsnull);

        gService->mOfflineDevice->Shutdown();
    }
    gService->mCustomOfflineDevices.Enumerate(
        &nsCacheService::ShutdownCustomCacheDeviceEnum, nsnull);

    gService->mEnableOfflineDevice = false;

    if (gService->mMemoryDevice) {
        // clear memory cache
        gService->mMemoryDevice->EvictEntries(nsnull);
    }

    gService->mClearingEntries = false;
}


void
nsCacheService::OnProfileChanged()
{
    if (!gService)  return;

    CACHE_LOG_DEBUG(("nsCacheService::OnProfileChanged"));
 
    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_ONPROFILECHANGED));
    
    gService->mEnableDiskDevice    = gService->mObserver->DiskCacheEnabled();
    gService->mEnableOfflineDevice = gService->mObserver->OfflineCacheEnabled();
    gService->mEnableMemoryDevice  = gService->mObserver->MemoryCacheEnabled();

    if (gService->mDiskDevice) {
        gService->mDiskDevice->SetCacheParentDirectory(gService->mObserver->DiskCacheParentDirectory());
        gService->mDiskDevice->SetCapacity(gService->mObserver->DiskCacheCapacity());

        // XXX initialization of mDiskDevice could be made lazily, if mEnableDiskDevice is false
        nsresult rv = gService->mDiskDevice->Init();
        if (NS_FAILED(rv)) {
            NS_ERROR("nsCacheService::OnProfileChanged: Re-initializing disk device failed");
            gService->mEnableDiskDevice = false;
            // XXX delete mDiskDevice?
        }
    }

    if (gService->mOfflineDevice) {
        gService->mOfflineDevice->SetCacheParentDirectory(gService->mObserver->OfflineCacheParentDirectory());
        gService->mOfflineDevice->SetCapacity(gService->mObserver->OfflineCacheCapacity());

        // XXX initialization of mOfflineDevice could be made lazily, if mEnableOfflineDevice is false
        nsresult rv = gService->mOfflineDevice->Init();
        if (NS_FAILED(rv)) {
            NS_ERROR("nsCacheService::OnProfileChanged: Re-initializing offline device failed");
            gService->mEnableOfflineDevice = false;
            // XXX delete mOfflineDevice?
        }
    }

    // If memoryDevice exists, reset its size to the new profile
    if (gService->mMemoryDevice) {
        if (gService->mEnableMemoryDevice) {
            // make sure that capacity is reset to the right value
            PRInt32 capacity = gService->mObserver->MemoryCacheCapacity();
            CACHE_LOG_DEBUG(("Resetting memory device capacity to %d\n",
                             capacity));
            gService->mMemoryDevice->SetCapacity(capacity);
        } else {
            // tell memory device to evict everything
            CACHE_LOG_DEBUG(("memory device disabled\n"));
            gService->mMemoryDevice->SetCapacity(0);
            // Don't delete memory device, because some entries may be active still...
        }
    }
}


void
nsCacheService::SetDiskCacheEnabled(bool    enabled)
{
    if (!gService)  return;
    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_SETDISKCACHEENABLED));
    gService->mEnableDiskDevice = enabled;
}


void
nsCacheService::SetDiskCacheCapacity(PRInt32  capacity)
{
    if (!gService)  return;
    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_SETDISKCACHECAPACITY));

    if (gService->mDiskDevice) {
        gService->mDiskDevice->SetCapacity(capacity);
    }

    if (gService->mObserver)
        gService->mEnableDiskDevice = gService->mObserver->DiskCacheEnabled();
}

void
nsCacheService::SetDiskCacheMaxEntrySize(PRInt32  maxSize)
{
    if (!gService)  return;
    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_SETDISKCACHEMAXENTRYSIZE));

    if (gService->mDiskDevice) {
        gService->mDiskDevice->SetMaxEntrySize(maxSize);
    }
}

void
nsCacheService::SetMemoryCacheMaxEntrySize(PRInt32  maxSize)
{
    if (!gService)  return;
    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_SETMEMORYCACHEMAXENTRYSIZE));

    if (gService->mMemoryDevice) {
        gService->mMemoryDevice->SetMaxEntrySize(maxSize);
    }
}

void
nsCacheService::SetOfflineCacheEnabled(bool    enabled)
{
    if (!gService)  return;
    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_SETOFFLINECACHEENABLED));
    gService->mEnableOfflineDevice = enabled;
}

void
nsCacheService::SetOfflineCacheCapacity(PRInt32  capacity)
{
    if (!gService)  return;
    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_SETOFFLINECACHECAPACITY));

    if (gService->mOfflineDevice) {
        gService->mOfflineDevice->SetCapacity(capacity);
    }

    gService->mEnableOfflineDevice = gService->mObserver->OfflineCacheEnabled();
}


void
nsCacheService::SetMemoryCache()
{
    if (!gService)  return;

    CACHE_LOG_DEBUG(("nsCacheService::SetMemoryCache"));

    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_SETMEMORYCACHE));

    gService->mEnableMemoryDevice = gService->mObserver->MemoryCacheEnabled();

    if (gService->mEnableMemoryDevice) {
        if (gService->mMemoryDevice) {
            PRInt32 capacity = gService->mObserver->MemoryCacheCapacity();
            // make sure that capacity is reset to the right value
            CACHE_LOG_DEBUG(("Resetting memory device capacity to %d\n",
                             capacity));
            gService->mMemoryDevice->SetCapacity(capacity);
        }
    } else {
        if (gService->mMemoryDevice) {
            // tell memory device to evict everything
            CACHE_LOG_DEBUG(("memory device disabled\n"));
            gService->mMemoryDevice->SetCapacity(0);
            // Don't delete memory device, because some entries may be active still...
        }
    }
}


/******************************************************************************
 * static methods for nsCacheEntryDescriptor
 *****************************************************************************/
void
nsCacheService::CloseDescriptor(nsCacheEntryDescriptor * descriptor)
{
    // ask entry to remove descriptor
    nsCacheEntry * entry       = descriptor->CacheEntry();
    bool           stillActive = entry->RemoveDescriptor(descriptor);
    nsresult       rv          = NS_OK;

    if (!entry->IsValid()) {
        rv = gService->ProcessPendingRequests(entry);
    }

    if (!stillActive) {
        gService->DeactivateEntry(entry);
    }
}


nsresult        
nsCacheService::GetFileForEntry(nsCacheEntry *         entry,
                                nsIFile **             result)
{
    nsCacheDevice * device = gService->EnsureEntryHasDevice(entry);
    if (!device)  return  NS_ERROR_UNEXPECTED;
    
    return device->GetFileForEntry(entry, result);
}


nsresult
nsCacheService::OpenInputStreamForEntry(nsCacheEntry *     entry,
                                        nsCacheAccessMode  mode,
                                        PRUint32           offset,
                                        nsIInputStream  ** result)
{
    nsCacheDevice * device = gService->EnsureEntryHasDevice(entry);
    if (!device)  return  NS_ERROR_UNEXPECTED;

    return device->OpenInputStreamForEntry(entry, mode, offset, result);
}

nsresult
nsCacheService::OpenOutputStreamForEntry(nsCacheEntry *     entry,
                                         nsCacheAccessMode  mode,
                                         PRUint32           offset,
                                         nsIOutputStream ** result)
{
    nsCacheDevice * device = gService->EnsureEntryHasDevice(entry);
    if (!device)  return  NS_ERROR_UNEXPECTED;

    return device->OpenOutputStreamForEntry(entry, mode, offset, result);
}


nsresult
nsCacheService::OnDataSizeChange(nsCacheEntry * entry, PRInt32 deltaSize)
{
    nsCacheDevice * device = gService->EnsureEntryHasDevice(entry);
    if (!device)  return  NS_ERROR_UNEXPECTED;

    return device->OnDataSizeChange(entry, deltaSize);
}

void
nsCacheService::Lock(mozilla::Telemetry::ID mainThreadLockerID)
{
    mozilla::Telemetry::ID lockerID;
    mozilla::Telemetry::ID generalID;

    if (NS_IsMainThread()) {
        lockerID = mainThreadLockerID;
        generalID = mozilla::Telemetry::CACHE_SERVICE_LOCK_WAIT_MAINTHREAD;
    } else {
        lockerID = mozilla::Telemetry::HistogramCount;
        generalID = mozilla::Telemetry::CACHE_SERVICE_LOCK_WAIT;
	}

    TimeStamp start(TimeStamp::Now());

    gService->mLock.Lock();

    TimeStamp stop(TimeStamp::Now());

    // Telemetry isn't thread safe on its own, but this is OK because we're
    // protecting it with the cache lock. 
    if (lockerID != mozilla::Telemetry::HistogramCount) {
        mozilla::Telemetry::AccumulateTimeDelta(lockerID, start, stop);
    }
    mozilla::Telemetry::AccumulateTimeDelta(generalID, start, stop);
}

void
nsCacheService::Unlock()
{
    gService->mLock.AssertCurrentThreadOwns();

    nsTArray<nsISupports*> doomed;
    doomed.SwapElements(gService->mDoomedObjects);

    gService->mLock.Unlock();

    for (PRUint32 i = 0; i < doomed.Length(); ++i)
        doomed[i]->Release();
}

void
nsCacheService::ReleaseObject_Locked(nsISupports * obj,
                                     nsIEventTarget * target)
{
    gService->mLock.AssertCurrentThreadOwns();

    bool isCur;
    if (!target || (NS_SUCCEEDED(target->IsOnCurrentThread(&isCur)) && isCur)) {
        gService->mDoomedObjects.AppendElement(obj);
    } else {
        NS_ProxyRelease(target, obj);
    }
}


nsresult
nsCacheService::SetCacheElement(nsCacheEntry * entry, nsISupports * element)
{
    entry->SetData(element);
    entry->TouchData();
    return NS_OK;
}


nsresult
nsCacheService::ValidateEntry(nsCacheEntry * entry)
{
    nsCacheDevice * device = gService->EnsureEntryHasDevice(entry);
    if (!device)  return  NS_ERROR_UNEXPECTED;

    entry->MarkValid();
    nsresult rv = gService->ProcessPendingRequests(entry);
    NS_ASSERTION(rv == NS_OK, "ProcessPendingRequests failed.");
    // XXX what else should be done?

    return rv;
}


PRInt32
nsCacheService::CacheCompressionLevel()
{
    PRInt32 level = gService->mObserver->CacheCompressionLevel();
    return level;
}


void
nsCacheService::DeactivateEntry(nsCacheEntry * entry)
{
    CACHE_LOG_DEBUG(("Deactivating entry %p\n", entry));
    nsresult  rv = NS_OK;
    NS_ASSERTION(entry->IsNotInUse(), "### deactivating an entry while in use!");
    nsCacheDevice * device = nsnull;

    if (mMaxDataSize < entry->DataSize() )     mMaxDataSize = entry->DataSize();
    if (mMaxMetaSize < entry->MetaDataSize() ) mMaxMetaSize = entry->MetaDataSize();

    if (entry->IsDoomed()) {
        // remove from Doomed list
        PR_REMOVE_AND_INIT_LINK(entry);
    } else if (entry->IsActive()) {
        // remove from active entries
        mActiveEntries.RemoveEntry(entry);
        CACHE_LOG_DEBUG(("Removed deactivated entry %p from mActiveEntries\n",
                         entry));
        entry->MarkInactive();

        // bind entry if necessary to store meta-data
        device = EnsureEntryHasDevice(entry); 
        if (!device) {
            CACHE_LOG_DEBUG(("DeactivateEntry: unable to bind active "
                             "entry %p\n",
                             entry));
            NS_WARNING("DeactivateEntry: unable to bind active entry\n");
            return;
        }
    } else {
        // if mInitialized == false,
        // then we're shutting down and this state is okay.
        NS_ASSERTION(!mInitialized, "DeactivateEntry: bad cache entry state.");
    }

    device = entry->CacheDevice();
    if (device) {
        rv = device->DeactivateEntry(entry);
        if (NS_FAILED(rv)) {
            // increment deactivate failure count
            ++mDeactivateFailures;
        }
    } else {
        // increment deactivating unbound entry statistic
        ++mDeactivatedUnboundEntries;
        delete entry; // because no one else will
    }
}


nsresult
nsCacheService::ProcessPendingRequests(nsCacheEntry * entry)
{
    nsresult            rv = NS_OK;
    nsCacheRequest *    request = (nsCacheRequest *)PR_LIST_HEAD(&entry->mRequestQ);
    nsCacheRequest *    nextRequest;
    bool                newWriter = false;
    
    CACHE_LOG_DEBUG(("ProcessPendingRequests for %sinitialized %s %salid entry %p\n",
                    (entry->IsInitialized()?"" : "Un"),
                    (entry->IsDoomed()?"DOOMED" : ""),
                    (entry->IsValid()? "V":"Inv"), entry));

    if (request == &entry->mRequestQ)  return NS_OK;    // no queued requests

    if (!entry->IsDoomed() && entry->IsInvalid()) {
        // 1st descriptor closed w/o MarkValid()
        NS_ASSERTION(PR_CLIST_IS_EMPTY(&entry->mDescriptorQ), "shouldn't be here with open descriptors");

#if DEBUG
        // verify no ACCESS_WRITE requests(shouldn't have any of these)
        while (request != &entry->mRequestQ) {
            NS_ASSERTION(request->AccessRequested() != nsICache::ACCESS_WRITE,
                         "ACCESS_WRITE request should have been given a new entry");
            request = (nsCacheRequest *)PR_NEXT_LINK(request);
        }
        request = (nsCacheRequest *)PR_LIST_HEAD(&entry->mRequestQ);        
#endif
        // find first request with ACCESS_READ_WRITE (if any) and promote it to 1st writer
        while (request != &entry->mRequestQ) {
            if (request->AccessRequested() == nsICache::ACCESS_READ_WRITE) {
                newWriter = true;
                CACHE_LOG_DEBUG(("  promoting request %p to 1st writer\n", request));
                break;
            }

            request = (nsCacheRequest *)PR_NEXT_LINK(request);
        }
        
        if (request == &entry->mRequestQ)   // no requests asked for ACCESS_READ_WRITE, back to top
            request = (nsCacheRequest *)PR_LIST_HEAD(&entry->mRequestQ);
        
        // XXX what should we do if there are only READ requests in queue?
        // XXX serialize their accesses, give them only read access, but force them to check validate flag?
        // XXX or do readers simply presume the entry is valid
        // See fix for bug #467392 below
    }

    nsCacheAccessMode  accessGranted = nsICache::ACCESS_NONE;

    while (request != &entry->mRequestQ) {
        nextRequest = (nsCacheRequest *)PR_NEXT_LINK(request);
        CACHE_LOG_DEBUG(("  %sync request %p for %p\n",
                        (request->mListener?"As":"S"), request, entry));

        if (request->mListener) {

            // Async request
            PR_REMOVE_AND_INIT_LINK(request);

            if (entry->IsDoomed()) {
                rv = ProcessRequest(request, false, nsnull);
                if (rv == NS_ERROR_CACHE_WAIT_FOR_VALIDATION)
                    rv = NS_OK;
                else
                    delete request;

                if (NS_FAILED(rv)) {
                    // XXX what to do?
                }
            } else if (entry->IsValid() || newWriter) {
                rv = entry->RequestAccess(request, &accessGranted);
                NS_ASSERTION(NS_SUCCEEDED(rv),
                             "if entry is valid, RequestAccess must succeed.");
                // XXX if (newWriter)  NS_ASSERTION( accessGranted == request->AccessRequested(), "why not?");

                // entry->CreateDescriptor dequeues request, and queues descriptor
                nsICacheEntryDescriptor *descriptor = nsnull;
                rv = entry->CreateDescriptor(request,
                                             accessGranted,
                                             &descriptor);

                // post call to listener to report error or descriptor
                rv = NotifyListener(request, descriptor, accessGranted, rv);
                delete request;
                if (NS_FAILED(rv)) {
                    // XXX what to do?
                }
                
            } else {
                // read-only request to an invalid entry - need to wait for
                // the entry to become valid so we post an event to process
                // the request again later (bug #467392)
                nsCOMPtr<nsIRunnable> ev =
                    new nsProcessRequestEvent(request);
                rv = DispatchToCacheIOThread(ev);
                if (NS_FAILED(rv)) {
                    delete request; // avoid leak
                }
            }
        } else {

            // Synchronous request
            request->WakeUp();
        }
        if (newWriter)  break;  // process remaining requests after validation
        request = nextRequest;
    }

    return NS_OK;
}


void
nsCacheService::ClearPendingRequests(nsCacheEntry * entry)
{
    nsCacheRequest * request = (nsCacheRequest *)PR_LIST_HEAD(&entry->mRequestQ);
    
    while (request != &entry->mRequestQ) {
        nsCacheRequest * next = (nsCacheRequest *)PR_NEXT_LINK(request);

        // XXX we're just dropping these on the floor for now...definitely wrong.
        PR_REMOVE_AND_INIT_LINK(request);
        delete request;
        request = next;
    }
}


void
nsCacheService::ClearDoomList()
{
    nsCacheEntry * entry = (nsCacheEntry *)PR_LIST_HEAD(&mDoomedEntries);

    while (entry != &mDoomedEntries) {
        nsCacheEntry * next = (nsCacheEntry *)PR_NEXT_LINK(entry);
        
         entry->DetachDescriptors();
         DeactivateEntry(entry);
         entry = next;
    }        
}


void
nsCacheService::ClearActiveEntries()
{
    mActiveEntries.VisitEntries(DeactivateAndClearEntry, nsnull);
    mActiveEntries.Shutdown();
}


PLDHashOperator
nsCacheService::DeactivateAndClearEntry(PLDHashTable *    table,
                                        PLDHashEntryHdr * hdr,
                                        PRUint32          number,
                                        void *            arg)
{
    nsCacheEntry * entry = ((nsCacheEntryHashTableEntry *)hdr)->cacheEntry;
    NS_ASSERTION(entry, "### active entry = nsnull!");
    // only called from Shutdown() so we don't worry about pending requests
    gService->ClearPendingRequests(entry);
    entry->DetachDescriptors();
    
    entry->MarkInactive();  // so we don't call Remove() while we're enumerating
    gService->DeactivateEntry(entry);
    
    return PL_DHASH_REMOVE; // and continue enumerating
}

struct ActiveEntryArgs
{
    nsTArray<nsCacheEntry*>* mActiveArray;
    nsCacheService::DoomCheckFn mCheckFn;
};

void
nsCacheService::DoomActiveEntries(DoomCheckFn check)
{
    nsAutoTArray<nsCacheEntry*, 8> array;
    ActiveEntryArgs args = { &array, check };

    mActiveEntries.VisitEntries(RemoveActiveEntry, &args);

    PRUint32 count = array.Length();
    for (PRUint32 i=0; i < count; ++i)
        DoomEntry_Internal(array[i], true);
}

PLDHashOperator
nsCacheService::RemoveActiveEntry(PLDHashTable *    table,
                                  PLDHashEntryHdr * hdr,
                                  PRUint32          number,
                                  void *            arg)
{
    nsCacheEntry * entry = ((nsCacheEntryHashTableEntry *)hdr)->cacheEntry;
    NS_ASSERTION(entry, "### active entry = nsnull!");

    ActiveEntryArgs* args = static_cast<ActiveEntryArgs*>(arg);
    if (args->mCheckFn && !args->mCheckFn(entry))
        return PL_DHASH_NEXT;

    NS_ASSERTION(args->mActiveArray, "### array = nsnull!");
    args->mActiveArray->AppendElement(entry);

    // entry is being removed from the active entry list
    entry->MarkInactive();
    return PL_DHASH_REMOVE; // and continue enumerating
}


#if defined(PR_LOGGING)
void
nsCacheService::LogCacheStatistics()
{
    PRUint32 hitPercentage = (PRUint32)((((double)mCacheHits) /
        ((double)(mCacheHits + mCacheMisses))) * 100);
    CACHE_LOG_ALWAYS(("\nCache Service Statistics:\n\n"));
    CACHE_LOG_ALWAYS(("    TotalEntries   = %d\n", mTotalEntries));
    CACHE_LOG_ALWAYS(("    Cache Hits     = %d\n", mCacheHits));
    CACHE_LOG_ALWAYS(("    Cache Misses   = %d\n", mCacheMisses));
    CACHE_LOG_ALWAYS(("    Cache Hit %%    = %d%%\n", hitPercentage));
    CACHE_LOG_ALWAYS(("    Max Key Length = %d\n", mMaxKeyLength));
    CACHE_LOG_ALWAYS(("    Max Meta Size  = %d\n", mMaxMetaSize));
    CACHE_LOG_ALWAYS(("    Max Data Size  = %d\n", mMaxDataSize));
    CACHE_LOG_ALWAYS(("\n"));
    CACHE_LOG_ALWAYS(("    Deactivate Failures         = %d\n",
                      mDeactivateFailures));
    CACHE_LOG_ALWAYS(("    Deactivated Unbound Entries = %d\n",
                      mDeactivatedUnboundEntries));
}
#endif

nsresult
nsCacheService::SetDiskSmartSize()
{
    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_SETDISKSMARTSIZE));

    if (!gService) return NS_ERROR_NOT_AVAILABLE;

    return gService->SetDiskSmartSize_Locked();
}

nsresult
nsCacheService::SetDiskSmartSize_Locked()
{
    nsresult rv;

    if (!mObserver->DiskCacheParentDirectory())
        return NS_ERROR_NOT_AVAILABLE;

    if (!mDiskDevice)
        return NS_ERROR_NOT_AVAILABLE;

    if (!mObserver->SmartSizeEnabled())
        return NS_ERROR_NOT_AVAILABLE;

    nsAutoString cachePath;
    rv = mObserver->DiskCacheParentDirectory()->GetPath(cachePath);
    if (NS_SUCCEEDED(rv)) {
        nsCOMPtr<nsIRunnable> event =
            new nsGetSmartSizeEvent(cachePath, mDiskDevice->getCacheSize());
        DispatchToCacheIOThread(event);
    } else {
        return NS_ERROR_FAILURE;
    }

    return NS_OK;
}

static bool
IsEntryPrivate(nsCacheEntry* entry)
{
    return entry->IsPrivate();
}

void
nsCacheService::LeavePrivateBrowsing()
{
    nsCacheServiceAutoLock lock(LOCK_TELEM(NSCACHESERVICE_LEAVEPRIVATEBROWSING));

    gService->DoomActiveEntries(IsEntryPrivate);

    if (gService->mMemoryDevice) {
        // clear memory cache
        gService->mMemoryDevice->EvictPrivateEntries();
    }
}
