/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsXULPrototypeCache.h"

#include "plstr.h"
#include "nsXULPrototypeDocument.h"
#include "nsCSSStyleSheet.h"
#include "nsIScriptRuntime.h"
#include "nsIServiceManager.h"
#include "nsIURI.h"

#include "nsIChromeRegistry.h"
#include "nsIFile.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"
#include "nsIObserverService.h"
#include "nsIStringStream.h"
#include "nsIStorageStream.h"

#include "nsNetUtil.h"
#include "nsAppDirectoryServiceDefs.h"

#include "jsapi.h"

#include "mozilla/Preferences.h"
#include "mozilla/scache/StartupCache.h"
#include "mozilla/scache/StartupCacheUtils.h"

using namespace mozilla;
using namespace mozilla::scache;

static bool gDisableXULCache = false; // enabled by default
static const char kDisableXULCachePref[] = "nglayout.debug.disable_xul_cache";
static const char kXULCacheInfoKey[] = "nsXULPrototypeCache.startupCache";
static const char kXULCachePrefix[] = "xulcache";

//----------------------------------------------------------------------

static int
DisableXULCacheChangedCallback(const char* aPref, void* aClosure)
{
    gDisableXULCache =
        Preferences::GetBool(kDisableXULCachePref, gDisableXULCache);

    // Flush the cache, regardless
    nsXULPrototypeCache* cache = nsXULPrototypeCache::GetInstance();
    if (cache)
        cache->Flush();

    return 0;
}

//----------------------------------------------------------------------

StartupCache*   nsXULPrototypeCache::gStartupCache = nsnull;
nsXULPrototypeCache*  nsXULPrototypeCache::sInstance = nsnull;


nsXULPrototypeCache::nsXULPrototypeCache()
{
}


nsXULPrototypeCache::~nsXULPrototypeCache()
{
    FlushScripts();
}


NS_IMPL_THREADSAFE_ISUPPORTS1(nsXULPrototypeCache, nsIObserver)

/* static */ nsXULPrototypeCache*
nsXULPrototypeCache::GetInstance()
{
    if (!sInstance) {
        NS_ADDREF(sInstance = new nsXULPrototypeCache());

        sInstance->mPrototypeTable.Init();
        sInstance->mStyleSheetTable.Init();
        sInstance->mScriptTable.Init();
        sInstance->mXBLDocTable.Init();

        sInstance->mCacheURITable.Init();
        sInstance->mInputStreamTable.Init();
        sInstance->mOutputStreamTable.Init();

        gDisableXULCache =
            Preferences::GetBool(kDisableXULCachePref, gDisableXULCache);
        Preferences::RegisterCallback(DisableXULCacheChangedCallback,
                                      kDisableXULCachePref);

        nsCOMPtr<nsIObserverService> obsSvc =
            mozilla::services::GetObserverService();
        if (obsSvc) {
            nsXULPrototypeCache *p = sInstance;
            obsSvc->AddObserver(p, "chrome-flush-skin-caches", false);
            obsSvc->AddObserver(p, "chrome-flush-caches", false);
            obsSvc->AddObserver(p, "startupcache-invalidate", false);
        }
		
    }
    return sInstance;
}

/* static */ StartupCache*
nsXULPrototypeCache::GetStartupCache()
{
    return gStartupCache;
}

//----------------------------------------------------------------------

NS_IMETHODIMP
nsXULPrototypeCache::Observe(nsISupports* aSubject,
                             const char *aTopic,
                             const PRUnichar *aData)
{
    if (!strcmp(aTopic, "chrome-flush-skin-caches")) {
        FlushSkinFiles();
    }
    else if (!strcmp(aTopic, "chrome-flush-caches")) {
        Flush();
    }
    else if (!strcmp(aTopic, "startupcache-invalidate")) {
        AbortCaching();
    }
    else {
        NS_WARNING("Unexpected observer topic.");
    }
    return NS_OK;
}

nsXULPrototypeDocument*
nsXULPrototypeCache::GetPrototype(nsIURI* aURI)
{
    nsXULPrototypeDocument* protoDoc = mPrototypeTable.GetWeak(aURI);
    if (protoDoc)
        return protoDoc;

    nsresult rv = BeginCaching(aURI);
    if (NS_FAILED(rv))
        return nsnull;

    // No prototype in XUL memory cache. Spin up the cache Service.
    nsCOMPtr<nsIObjectInputStream> ois;
    rv = GetInputStream(aURI, getter_AddRefs(ois));
    if (NS_FAILED(rv))
        return nsnull;
    
    nsRefPtr<nsXULPrototypeDocument> newProto;
    rv = NS_NewXULPrototypeDocument(getter_AddRefs(newProto));
    if (NS_FAILED(rv))
        return nsnull;
    
    rv = newProto->Read(ois);
    if (NS_SUCCEEDED(rv)) {
        rv = PutPrototype(newProto);
    } else {
        newProto = nsnull;
    }
    
    mInputStreamTable.Remove(aURI);
    RemoveFromCacheSet(aURI);
    return newProto;
}

nsresult
nsXULPrototypeCache::PutPrototype(nsXULPrototypeDocument* aDocument)
{
    nsCOMPtr<nsIURI> uri = aDocument->GetURI();
    // Put() releases any old value and addrefs the new one
    mPrototypeTable.Put(uri, aDocument);

    return NS_OK;
}

nsresult
nsXULPrototypeCache::PutStyleSheet(nsCSSStyleSheet* aStyleSheet)
{
    nsIURI* uri = aStyleSheet->GetSheetURI();

    mStyleSheetTable.Put(uri, aStyleSheet);

    return NS_OK;
}


JSScript*
nsXULPrototypeCache::GetScript(nsIURI* aURI)
{
    CacheScriptEntry entry;
    if (!mScriptTable.Get(aURI, &entry)) {
        return nsnull;
    }
    return entry.mScriptObject;
}


/* static */
static PLDHashOperator
ReleaseScriptObjectCallback(nsIURI* aKey, CacheScriptEntry &aData, void* aClosure)
{
    nsCOMPtr<nsIScriptRuntime> rt;
    if (NS_SUCCEEDED(NS_GetJSRuntime(getter_AddRefs(rt))))
        rt->DropScriptObject(aData.mScriptObject);
    return PL_DHASH_REMOVE;
}

nsresult
nsXULPrototypeCache::PutScript(nsIURI* aURI, JSScript* aScriptObject)
{
    CacheScriptEntry existingEntry;
    if (mScriptTable.Get(aURI, &existingEntry)) {
#ifdef DEBUG
        nsCAutoString scriptName;
        aURI->GetSpec(scriptName);
        nsCAutoString message("Loaded script ");
        message += scriptName;
        message += " twice (bug 392650)";
        NS_WARNING(message.get());
#endif
        // Reuse the callback used for enumeration in FlushScripts
        ReleaseScriptObjectCallback(aURI, existingEntry, nsnull);
    }

    CacheScriptEntry entry = {aScriptObject};

    mScriptTable.Put(aURI, entry);

    // Lock the object from being gc'd until it is removed from the cache
    nsCOMPtr<nsIScriptRuntime> rt;
    nsresult rv = NS_GetJSRuntime(getter_AddRefs(rt));
    if (NS_SUCCEEDED(rv))
        rv = rt->HoldScriptObject(aScriptObject);
    NS_ASSERTION(NS_SUCCEEDED(rv), "Failed to GC lock the object");

    // On failure doing the lock, we should remove the map entry?
    return rv;
}

void
nsXULPrototypeCache::FlushScripts()
{
    // This callback will unlock each object so it can once again be gc'd.
    // XXX - this might be slow - we fetch the runtime each and every object.
    mScriptTable.Enumerate(ReleaseScriptObjectCallback, nsnull);
}


nsresult
nsXULPrototypeCache::PutXBLDocumentInfo(nsXBLDocumentInfo* aDocumentInfo)
{
    nsIURI* uri = aDocumentInfo->DocumentURI();

    nsRefPtr<nsXBLDocumentInfo> info;
    mXBLDocTable.Get(uri, getter_AddRefs(info));
    if (!info) {
        mXBLDocTable.Put(uri, aDocumentInfo);
    }
    return NS_OK;
}

static PLDHashOperator
FlushSkinXBL(nsIURI* aKey, nsRefPtr<nsXBLDocumentInfo>& aDocInfo, void* aClosure)
{
  nsCAutoString str;
  aKey->GetPath(str);

  PLDHashOperator ret = PL_DHASH_NEXT;

  if (!strncmp(str.get(), "/skin", 5)) {
    ret = PL_DHASH_REMOVE;
  }

  return ret;
}

static PLDHashOperator
FlushSkinSheets(nsIURI* aKey, nsRefPtr<nsCSSStyleSheet>& aSheet, void* aClosure)
{
  nsCAutoString str;
  aSheet->GetSheetURI()->GetPath(str);

  PLDHashOperator ret = PL_DHASH_NEXT;

  if (!strncmp(str.get(), "/skin", 5)) {
    // This is a skin binding. Add the key to the list.
    ret = PL_DHASH_REMOVE;
  }
  return ret;
}

static PLDHashOperator
FlushScopedSkinStylesheets(nsIURI* aKey, nsRefPtr<nsXBLDocumentInfo> &aDocInfo, void* aClosure)
{
  aDocInfo->FlushSkinStylesheets();
  return PL_DHASH_NEXT;
}

void
nsXULPrototypeCache::FlushSkinFiles()
{
  // Flush out skin XBL files from the cache.
  mXBLDocTable.Enumerate(FlushSkinXBL, nsnull);

  // Now flush out our skin stylesheets from the cache.
  mStyleSheetTable.Enumerate(FlushSkinSheets, nsnull);

  // Iterate over all the remaining XBL and make sure cached
  // scoped skin stylesheets are flushed and refetched by the
  // prototype bindings.
  mXBLDocTable.Enumerate(FlushScopedSkinStylesheets, nsnull);
}


void
nsXULPrototypeCache::Flush()
{
    mPrototypeTable.Clear();

    // Clear the script cache, as it refers to prototype-owned mJSObjects.
    FlushScripts();

    mStyleSheetTable.Clear();
    mXBLDocTable.Clear();
}


bool
nsXULPrototypeCache::IsEnabled()
{
    return !gDisableXULCache;
}

static bool gDisableXULDiskCache = false;           // enabled by default

void
nsXULPrototypeCache::AbortCaching()
{
#ifdef DEBUG_brendan
    NS_BREAK();
#endif

    // Flush the XUL cache for good measure, in case we cached a bogus/downrev
    // script, somehow.
    Flush();

    // Clear the cache set
    mCacheURITable.Clear();
}


static const char kDisableXULDiskCachePref[] = "nglayout.debug.disable_xul_fastload";

void
nsXULPrototypeCache::RemoveFromCacheSet(nsIURI* aURI)
{
    mCacheURITable.Remove(aURI);
}

nsresult
nsXULPrototypeCache::WritePrototype(nsXULPrototypeDocument* aPrototypeDocument)
{
    nsresult rv = NS_OK, rv2 = NS_OK;

    // We're here before the startupcache service has been initialized, probably because
    // of the profile manager. Bail quietly, don't worry, we'll be back later.
    if (!gStartupCache)
        return NS_OK;

    nsCOMPtr<nsIURI> protoURI = aPrototypeDocument->GetURI();

    // Remove this document from the cache table. We use the table's
    // emptiness instead of a counter to decide when the caching process 
    // has completed.
    RemoveFromCacheSet(protoURI);

    nsCOMPtr<nsIObjectOutputStream> oos;
    rv = GetOutputStream(protoURI, getter_AddRefs(oos));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = aPrototypeDocument->Write(oos);
    NS_ENSURE_SUCCESS(rv, rv);
    FinishOutputStream(protoURI);
    return NS_FAILED(rv) ? rv : rv2;
}

nsresult
nsXULPrototypeCache::GetInputStream(nsIURI* uri, nsIObjectInputStream** stream) 
{
    nsCAutoString spec(kXULCachePrefix);
    nsresult rv = PathifyURI(uri, spec);
    if (NS_FAILED(rv)) 
        return NS_ERROR_NOT_AVAILABLE;
    
    nsAutoArrayPtr<char> buf;
    PRUint32 len;
    nsCOMPtr<nsIObjectInputStream> ois;
    if (!gStartupCache)
        return NS_ERROR_NOT_AVAILABLE;
    
    rv = gStartupCache->GetBuffer(spec.get(), getter_Transfers(buf), &len);
    if (NS_FAILED(rv)) 
        return NS_ERROR_NOT_AVAILABLE;

    rv = NewObjectInputStreamFromBuffer(buf, len, getter_AddRefs(ois));
    NS_ENSURE_SUCCESS(rv, rv);
    buf.forget();

    mInputStreamTable.Put(uri, ois);
    
    NS_ADDREF(*stream = ois);
    return NS_OK;
}

nsresult
nsXULPrototypeCache::FinishInputStream(nsIURI* uri) {
    mInputStreamTable.Remove(uri);
    return NS_OK;
}

nsresult
nsXULPrototypeCache::GetOutputStream(nsIURI* uri, nsIObjectOutputStream** stream)
{
    nsresult rv;
    nsCOMPtr<nsIObjectOutputStream> objectOutput;
    nsCOMPtr<nsIStorageStream> storageStream;
    bool found = mOutputStreamTable.Get(uri, getter_AddRefs(storageStream));
    if (found) {
        objectOutput = do_CreateInstance("mozilla.org/binaryoutputstream;1");
        if (!objectOutput) return NS_ERROR_OUT_OF_MEMORY;
        nsCOMPtr<nsIOutputStream> outputStream
            = do_QueryInterface(storageStream);
        objectOutput->SetOutputStream(outputStream);
    } else {
        rv = NewObjectOutputWrappedStorageStream(getter_AddRefs(objectOutput), 
                                                 getter_AddRefs(storageStream),
                                                 false);
        NS_ENSURE_SUCCESS(rv, rv);
        mOutputStreamTable.Put(uri, storageStream);
    }
    NS_ADDREF(*stream = objectOutput);
    return NS_OK;
}

nsresult
nsXULPrototypeCache::FinishOutputStream(nsIURI* uri) 
{
    nsresult rv;
    if (!gStartupCache)
        return NS_ERROR_NOT_AVAILABLE;
    
    nsCOMPtr<nsIStorageStream> storageStream;
    bool found = mOutputStreamTable.Get(uri, getter_AddRefs(storageStream));
    if (!found)
        return NS_ERROR_UNEXPECTED;
    nsCOMPtr<nsIOutputStream> outputStream
        = do_QueryInterface(storageStream);
    outputStream->Close();
    
    nsAutoArrayPtr<char> buf;
    PRUint32 len;
    rv = NewBufferFromStorageStream(storageStream, getter_Transfers(buf), 
                                    &len);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCAutoString spec(kXULCachePrefix);
    rv = PathifyURI(uri, spec);
    if (NS_FAILED(rv))
        return NS_ERROR_NOT_AVAILABLE;
    rv = gStartupCache->PutBuffer(spec.get(), buf, len);
    if (NS_SUCCEEDED(rv))
        mOutputStreamTable.Remove(uri);
    
    return rv;
}

// We have data if we're in the middle of writing it or we already
// have it in the cache.
nsresult
nsXULPrototypeCache::HasData(nsIURI* uri, bool* exists)
{
    if (mOutputStreamTable.Get(uri, nsnull)) {
        *exists = true;
        return NS_OK;
    }
    nsCAutoString spec(kXULCachePrefix);
    nsresult rv = PathifyURI(uri, spec);
    if (NS_FAILED(rv)) {
        *exists = false;
        return NS_OK;
    }
    nsAutoArrayPtr<char> buf;
    PRUint32 len;
    if (gStartupCache)
        rv = gStartupCache->GetBuffer(spec.get(), getter_Transfers(buf), 
                                      &len);
    else {
        // We don't have everything we need to call BeginCaching and set up
        // gStartupCache right now, but we just need to check the cache for 
        // this URI.
        StartupCache* sc = StartupCache::GetSingleton();
        if (!sc) {
            *exists = false;
            return NS_OK;
        }
        rv = sc->GetBuffer(spec.get(), getter_Transfers(buf), &len);
    }
    *exists = NS_SUCCEEDED(rv);
    return NS_OK;
}

static int
CachePrefChangedCallback(const char* aPref, void* aClosure)
{
    bool wasEnabled = !gDisableXULDiskCache;
    gDisableXULDiskCache =
        Preferences::GetBool(kDisableXULCachePref,
                             gDisableXULDiskCache);

    if (wasEnabled && gDisableXULDiskCache) {
        nsXULPrototypeCache* cache = nsXULPrototypeCache::GetInstance();

        if (cache)
            cache->AbortCaching();
    }
    return 0;
}

nsresult
nsXULPrototypeCache::BeginCaching(nsIURI* aURI)
{
    nsresult rv;

    nsCAutoString path;
    aURI->GetPath(path);
    if (!StringEndsWith(path, NS_LITERAL_CSTRING(".xul")))
        return NS_ERROR_NOT_AVAILABLE;

    // Test gStartupCache to decide whether this is the first nsXULDocument
    // participating in the serialization.  If gStartupCache is non-null, this document
    // must not be first, but it can join the process.  Examples of
    // multiple master documents participating include hiddenWindow.xul and
    // navigator.xul on the Mac, and multiple-app-component (e.g., mailnews
    // and browser) startup due to command-line arguments.
    //
    if (gStartupCache) {
        mCacheURITable.Put(aURI, 1);

        return NS_OK;
    }

    // Use a local to refer to the service till we're sure we succeeded, then
    // commit to gStartupCache.
    StartupCache* startupCache = StartupCache::GetSingleton();
    if (!startupCache)
        return NS_ERROR_FAILURE;

    gDisableXULDiskCache =
        Preferences::GetBool(kDisableXULCachePref, gDisableXULDiskCache);

    Preferences::RegisterCallback(CachePrefChangedCallback,
                                  kDisableXULCachePref);

    if (gDisableXULDiskCache)
        return NS_ERROR_NOT_AVAILABLE;

    // Get the chrome directory to validate against the one stored in the
    // cache file, or to store there if we're generating a new file.
    nsCOMPtr<nsIFile> chromeDir;
    rv = NS_GetSpecialDirectory(NS_APP_CHROME_DIR, getter_AddRefs(chromeDir));
    if (NS_FAILED(rv))
        return rv;
    nsCAutoString chromePath;
    rv = chromeDir->GetNativePath(chromePath);
    if (NS_FAILED(rv))
        return rv;

    // XXXbe we assume the first package's locale is the same as the locale of
    // all subsequent packages of cached chrome URIs....
    nsCAutoString package;
    rv = aURI->GetHost(package);
    if (NS_FAILED(rv))
        return rv;
    nsCOMPtr<nsIXULChromeRegistry> chromeReg
        = do_GetService(NS_CHROMEREGISTRY_CONTRACTID, &rv);
    nsCAutoString locale;
    rv = chromeReg->GetSelectedLocale(package, locale);
    if (NS_FAILED(rv))
        return rv;

    nsCAutoString fileChromePath, fileLocale;
    
    nsAutoArrayPtr<char> buf;
    PRUint32 len, amtRead;
    nsCOMPtr<nsIObjectInputStream> objectInput;

    rv = startupCache->GetBuffer(kXULCacheInfoKey, getter_Transfers(buf), 
                                 &len);
    if (NS_SUCCEEDED(rv))
        rv = NewObjectInputStreamFromBuffer(buf, len, getter_AddRefs(objectInput));
    
    if (NS_SUCCEEDED(rv)) {
        buf.forget();
        rv = objectInput->ReadCString(fileLocale);
        rv |= objectInput->ReadCString(fileChromePath);
        if (NS_FAILED(rv) ||
            (!fileChromePath.Equals(chromePath) ||
             !fileLocale.Equals(locale))) {
            // Our cache won't be valid in this case, we'll need to rewrite.
            // XXX This blows away work that other consumers (like
            // mozJSComponentLoader) have done, need more fine-grained control.
            startupCache->InvalidateCache();
            rv = NS_ERROR_UNEXPECTED;
        }
    } else if (rv != NS_ERROR_NOT_AVAILABLE)
        // NS_ERROR_NOT_AVAILABLE is normal, usually if there's no cachefile.
        return rv;

    if (NS_FAILED(rv)) {
        // Either the cache entry was invalid or it didn't exist, so write it now.
        nsCOMPtr<nsIObjectOutputStream> objectOutput;
        nsCOMPtr<nsIInputStream> inputStream;
        nsCOMPtr<nsIStorageStream> storageStream;
        rv = NewObjectOutputWrappedStorageStream(getter_AddRefs(objectOutput),
                                                 getter_AddRefs(storageStream),
                                                 false);
        if (NS_SUCCEEDED(rv)) {
            rv = objectOutput->WriteStringZ(locale.get());
            rv |= objectOutput->WriteStringZ(chromePath.get());
            rv |= objectOutput->Close();
            rv |= storageStream->NewInputStream(0, getter_AddRefs(inputStream));
        }
        if (NS_SUCCEEDED(rv))
            rv = inputStream->Available(&len);
        
        if (NS_SUCCEEDED(rv)) {
            buf = new char[len];
            rv = inputStream->Read(buf, len, &amtRead);
            if (NS_SUCCEEDED(rv) && len == amtRead)
                rv = startupCache->PutBuffer(kXULCacheInfoKey, buf, len);
            else {
                rv = NS_ERROR_UNEXPECTED;
            }
        }

        // Failed again, just bail.
        if (NS_FAILED(rv)) {
            startupCache->InvalidateCache();
            return NS_ERROR_FAILURE;
        }
    }

    // Success!  Insert this URI into the mCacheURITable
    // and commit locals to globals.
    mCacheURITable.Put(aURI, 1);

    gStartupCache = startupCache;
    return NS_OK;
}

static PLDHashOperator
MarkXBLInCCGeneration(nsIURI* aKey, nsRefPtr<nsXBLDocumentInfo> &aDocInfo,
                      void* aClosure)
{
    PRUint32* gen = static_cast<PRUint32*>(aClosure);
    aDocInfo->MarkInCCGeneration(*gen);
    return PL_DHASH_NEXT;
}

static PLDHashOperator
MarkXULInCCGeneration(nsIURI* aKey, nsRefPtr<nsXULPrototypeDocument> &aDoc,
                      void* aClosure)
{
    PRUint32* gen = static_cast<PRUint32*>(aClosure);
    aDoc->MarkInCCGeneration(*gen);
    return PL_DHASH_NEXT;
}

void
nsXULPrototypeCache::MarkInCCGeneration(PRUint32 aGeneration)
{
    mXBLDocTable.Enumerate(MarkXBLInCCGeneration, &aGeneration);
    mPrototypeTable.Enumerate(MarkXULInCCGeneration, &aGeneration);
}
