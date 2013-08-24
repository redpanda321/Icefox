/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This Original Code has been modified by IBM Corporation.
 * Modifications made by IBM described herein are
 * Copyright (c) International Business Machines
 * Corporation, 2000
 *
 * Modifications to Mozilla code or documentation
 * identified per MPL Section 3.3
 *
 * Date             Modified by     Description of modification
 * 04/20/2000       IBM Corp.      Added PR_CALLBACK for Optlink use in OS2
 */

#include <stdlib.h>
#include "nscore.h"
#include "nsISupports.h"
#include "nspr.h"
#include "nsCRT.h" // for atoll

// Arena used by component manager for storing contractid string, dll
// location strings and small objects
// CAUTION: Arena align mask needs to be defined before including plarena.h
//          currently from nsComponentManager.h
#define PL_ARENA_CONST_ALIGN_MASK 7
#define NS_CM_BLOCK_SIZE (1024 * 8)

#include "nsCategoryManager.h"
#include "nsCOMPtr.h"
#include "nsComponentManager.h"
#include "nsDirectoryService.h"
#include "nsDirectoryServiceDefs.h"
#include "nsCategoryManager.h"
#include "nsCategoryManagerUtils.h"
#include "nsIEnumerator.h"
#include "xptiprivate.h"
#include "nsIConsoleService.h"
#include "nsIObserverService.h"
#include "nsISimpleEnumerator.h"
#include "nsIStringEnumerator.h"
#include "nsXPCOM.h"
#include "nsXPCOMPrivate.h"
#include "nsISupportsPrimitives.h"
#include "nsIClassInfo.h"
#include "nsLocalFile.h"
#include "nsReadableUtils.h"
#include "nsStaticComponents.h"
#include "nsString.h"
#include "nsXPIDLString.h"
#include "prcmon.h"
#include "xptinfo.h" // this after nsISupports, to pick up IID so that xpt stuff doesn't try to define it itself...
#include "nsThreadUtils.h"
#include "prthread.h"
#include "private/pprthred.h"
#include "nsTArray.h"
#include "prio.h"
#include "mozilla/FunctionTimer.h"
#include "ManifestParser.h"
#include "mozilla/Services.h"

#include "nsManifestLineReader.h"
#include "mozilla/GenericFactory.h"
#include "nsSupportsPrimitives.h"
#include "nsArrayEnumerator.h"
#include "nsStringEnumerator.h"
#include "mozilla/FileUtils.h"

#include NEW_H     // for placement new

#include "mozilla/Omnijar.h"

#include "prlog.h"

using namespace mozilla;

PRLogModuleInfo* nsComponentManagerLog = nsnull;

#if 0 || defined (DEBUG_timeless)
 #define SHOW_DENIED_ON_SHUTDOWN
 #define SHOW_CI_ON_EXISTING_SERVICE
#endif

// Bloated registry buffer size to improve startup performance -- needs to
// be big enough to fit the entire file into memory or it'll thrash.
// 512K is big enough to allow for some future growth in the registry.
#define BIG_REGISTRY_BUFLEN   (512*1024)

// Common Key Names
const char classIDKeyName[]="classID";
const char classesKeyName[]="contractID";
const char componentsKeyName[]="components";
const char xpcomComponentsKeyName[]="software/mozilla/XPCOM/components";
const char xpcomKeyName[]="software/mozilla/XPCOM";

// Common Value Names
const char classIDValueName[]="ClassID";
const char classNameValueName[]="ClassName";
const char componentCountValueName[]="ComponentsCount";
const char componentTypeValueName[]="ComponentType";
const char contractIDValueName[]="ContractID";
const char fileSizeValueName[]="FileSize";
const char inprocServerValueName[]="InprocServer";
const char lastModValueName[]="LastModTimeStamp";
const char nativeComponentType[]="application/x-mozilla-native";
const char staticComponentType[]="application/x-mozilla-static";
const char jarComponentType[]="application/x-mozilla-jarjs";
const char versionValueName[]="VersionString";

const static char XPCOM_ABSCOMPONENT_PREFIX[] = "abs:";
const static char XPCOM_RELCOMPONENT_PREFIX[] = "rel:";
const static char XPCOM_GRECOMPONENT_PREFIX[] = "gre:";

static const char gIDFormat[] =
  "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}";


#define NS_EMPTY_IID                                 \
{                                                    \
    0x00000000,                                      \
    0x0000,                                          \
    0x0000,                                          \
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} \
}

NS_DEFINE_CID(kEmptyCID, NS_EMPTY_IID);
NS_DEFINE_CID(kCategoryManagerCID, NS_CATEGORYMANAGER_CID);

#define UID_STRING_LENGTH 39

#ifdef NS_FUNCTION_TIMER
#define COMPMGR_TIME_FUNCTION_CID(cid)                                          \
  char cid_buf__[NSID_LENGTH] = { '\0' };                                      \
  cid.ToProvidedString(cid_buf__);                                             \
  NS_TIME_FUNCTION_MIN_FMT(5, "%s (line %d) (cid: %s)", MOZ_FUNCTION_NAME, \
                           __LINE__, cid_buf__)
#define COMPMGR_TIME_FUNCTION_CONTRACTID(cid)                                  \
  NS_TIME_FUNCTION_MIN_FMT(5, "%s (line %d) (contractid: %s)", MOZ_FUNCTION_NAME, \
                           __LINE__, (cid))
#else
#define COMPMGR_TIME_FUNCTION_CID(cid) do {} while (0)
#define COMPMGR_TIME_FUNCTION_CONTRACTID(cid) do {} while (0)
#endif

nsresult
nsGetServiceFromCategory::operator()(const nsIID& aIID, void** aInstancePtr) const
{
    nsresult rv;
    nsXPIDLCString value;
    nsCOMPtr<nsICategoryManager> catman;
    nsComponentManagerImpl *compMgr = nsComponentManagerImpl::gComponentManager;
    if (!compMgr) {
        rv = NS_ERROR_NOT_INITIALIZED;
        goto error;
    }

    if (!mCategory || !mEntry) {
        // when categories have defaults, use that for null mEntry
        rv = NS_ERROR_NULL_POINTER;
        goto error;
    }

    rv = compMgr->nsComponentManagerImpl::GetService(kCategoryManagerCID,
                                                     NS_GET_IID(nsICategoryManager),
                                                     getter_AddRefs(catman));
    if (NS_FAILED(rv)) goto error;

    /* find the contractID for category.entry */
    rv = catman->GetCategoryEntry(mCategory, mEntry,
                                  getter_Copies(value));
    if (NS_FAILED(rv)) goto error;
    if (!value) {
        rv = NS_ERROR_SERVICE_NOT_AVAILABLE;
        goto error;
    }

    rv = compMgr->
        nsComponentManagerImpl::GetServiceByContractID(value,
                                                       aIID, aInstancePtr);
    if (NS_FAILED(rv)) {
    error:
        *aInstancePtr = 0;
    }
    if (mErrorPtr)
        *mErrorPtr = rv;
    return rv;
}

////////////////////////////////////////////////////////////////////////////////
// Arena helper functions
////////////////////////////////////////////////////////////////////////////////
char *
ArenaStrndup(const char *s, PRUint32 len, PLArenaPool *arena)
{
    void *mem;
    // Include trailing null in the len
    PL_ARENA_ALLOCATE(mem, arena, len+1);
    if (mem)
        memcpy(mem, s, len+1);
    return static_cast<char *>(mem);
}

char*
ArenaStrdup(const char *s, PLArenaPool *arena)
{
    return ArenaStrndup(s, strlen(s), arena);
}

// this is safe to call during InitXPCOM
static already_AddRefed<nsIFile>
GetLocationFromDirectoryService(const char* prop)
{
    nsCOMPtr<nsIProperties> directoryService;
    nsDirectoryService::Create(nsnull,
                               NS_GET_IID(nsIProperties),
                               getter_AddRefs(directoryService));

    if (!directoryService)
        return NULL;

    nsCOMPtr<nsIFile> file;
    nsresult rv = directoryService->Get(prop,
                                        NS_GET_IID(nsIFile),
                                        getter_AddRefs(file));
    if (NS_FAILED(rv))
        return NULL;

    return file.forget();
}

static already_AddRefed<nsIFile>
CloneAndAppend(nsIFile* aBase, const nsACString& append)
{
    nsCOMPtr<nsIFile> f;
    aBase->Clone(getter_AddRefs(f));
    if (!f)
        return NULL;

    f->AppendNative(append);
    return f.forget();
}

////////////////////////////////////////////////////////////////////////////////
// nsComponentManagerImpl
////////////////////////////////////////////////////////////////////////////////

nsresult
nsComponentManagerImpl::Create(nsISupports* aOuter, REFNSIID aIID, void** aResult)
{
    if (aOuter)
        return NS_ERROR_NO_AGGREGATION;

    if (!gComponentManager)
        return NS_ERROR_FAILURE;

    return gComponentManager->QueryInterface(aIID, aResult);
}

nsComponentManagerImpl::nsComponentManagerImpl()
    : mMon("nsComponentManagerImpl.mMon")
    , mStatus(NOT_INITIALIZED)
{
}

#define CONTRACTID_HASHTABLE_INITIAL_SIZE   2048

nsTArray<const mozilla::Module*>* nsComponentManagerImpl::sStaticModules;

/* static */ void
nsComponentManagerImpl::InitializeStaticModules()
{
    if (sStaticModules)
        return;

    sStaticModules = new nsTArray<const mozilla::Module*>;
    for (const mozilla::Module *const *const *staticModules = kPStaticModules;
         *staticModules; ++staticModules)
        sStaticModules->AppendElement(**staticModules);
}

nsTArray<nsComponentManagerImpl::ComponentLocation>*
nsComponentManagerImpl::sModuleLocations;

/* static */ void
nsComponentManagerImpl::InitializeModuleLocations()
{
    if (sModuleLocations)
        return;

    sModuleLocations = new nsTArray<ComponentLocation>;
}

nsresult nsComponentManagerImpl::Init()
{
    NS_TIME_FUNCTION;

    PR_ASSERT(NOT_INITIALIZED == mStatus);

    if (nsComponentManagerLog == nsnull)
    {
        nsComponentManagerLog = PR_NewLogModule("nsComponentManager");
    }

    // Initialize our arena
    NS_TIME_FUNCTION_MARK("Next: init component manager arena");
    PL_INIT_ARENA_POOL(&mArena, "ComponentManagerArena", NS_CM_BLOCK_SIZE);

    mFactories.Init(CONTRACTID_HASHTABLE_INITIAL_SIZE);
    mContractIDs.Init(CONTRACTID_HASHTABLE_INITIAL_SIZE);
    mLoaderMap.Init();
    mKnownModules.Init();

    nsCOMPtr<nsIFile> greDir =
        GetLocationFromDirectoryService(NS_GRE_DIR);
    nsCOMPtr<nsIFile> appDir =
        GetLocationFromDirectoryService(NS_XPCOM_CURRENT_PROCESS_DIR);

    InitializeStaticModules();
    InitializeModuleLocations();

    ComponentLocation* cl = sModuleLocations->InsertElementAt(0);
    nsCOMPtr<nsIFile> lf = CloneAndAppend(appDir, NS_LITERAL_CSTRING("chrome.manifest"));
    cl->type = NS_COMPONENT_LOCATION;
    cl->location.Init(lf);

    bool equals = false;
    appDir->Equals(greDir, &equals);
    if (!equals) {
        cl = sModuleLocations->InsertElementAt(0);
        cl->type = NS_COMPONENT_LOCATION;
        lf = CloneAndAppend(greDir, NS_LITERAL_CSTRING("chrome.manifest"));
        cl->location.Init(lf);
    }

    PR_LOG(nsComponentManagerLog, PR_LOG_DEBUG,
           ("nsComponentManager: Initialized."));

    NS_TIME_FUNCTION_MARK("Next: init native module loader");
    nsresult rv = mNativeModuleLoader.Init();
    if (NS_FAILED(rv))
        return rv;

    nsCategoryManager::GetSingleton()->SuppressNotifications(true);

    RegisterModule(&kXPCOMModule, NULL);

    for (PRUint32 i = 0; i < sStaticModules->Length(); ++i)
        RegisterModule((*sStaticModules)[i], NULL);

    nsRefPtr<nsZipArchive> appOmnijar = mozilla::Omnijar::GetReader(mozilla::Omnijar::APP);
    if (appOmnijar) {
        cl = sModuleLocations->InsertElementAt(1); // Insert after greDir
        cl->type = NS_COMPONENT_LOCATION;
        cl->location.Init(appOmnijar, "chrome.manifest");
    }
    nsRefPtr<nsZipArchive> greOmnijar = mozilla::Omnijar::GetReader(mozilla::Omnijar::GRE);
    if (greOmnijar) {
        cl = sModuleLocations->InsertElementAt(0);
        cl->type = NS_COMPONENT_LOCATION;
        cl->location.Init(greOmnijar, "chrome.manifest");
    }

    RereadChromeManifests(false);

    nsCategoryManager::GetSingleton()->SuppressNotifications(false);

    mStatus = NORMAL;

    return NS_OK;
}

void
nsComponentManagerImpl::RegisterModule(const mozilla::Module* aModule,
                                       FileLocation* aFile)
{
    ReentrantMonitorAutoEnter mon(mMon);

    KnownModule* m;
    if (aFile) {
        nsCString uri;
        aFile->GetURIString(uri);
        NS_ASSERTION(!mKnownModules.Get(uri),
                     "Must not register a binary module twice.");

        m = new KnownModule(aModule, *aFile);
        mKnownModules.Put(uri, m);
    } else {
        m = new KnownModule(aModule);
        mKnownStaticModules.AppendElement(m);
    }

    if (aModule->mCIDs) {
        const mozilla::Module::CIDEntry* entry;
        for (entry = aModule->mCIDs; entry->cid; ++entry)
            RegisterCIDEntry(entry, m);
    }

    if (aModule->mContractIDs) {
        const mozilla::Module::ContractIDEntry* entry;
        for (entry = aModule->mContractIDs; entry->contractid; ++entry)
            RegisterContractID(entry);
        NS_ASSERTION(!entry->cid, "Incorrectly terminated contract list");
    }
            
    if (aModule->mCategoryEntries) {
        const mozilla::Module::CategoryEntry* entry;
        for (entry = aModule->mCategoryEntries; entry->category; ++entry)
            nsCategoryManager::GetSingleton()->
                AddCategoryEntry(entry->category,
                                 entry->entry,
                                 entry->value);
    }
}

void
nsComponentManagerImpl::RegisterCIDEntry(const mozilla::Module::CIDEntry* aEntry,
                                         KnownModule* aModule)
{
    mMon.AssertCurrentThreadIn();

    nsFactoryEntry* f = mFactories.Get(*aEntry->cid);
    if (f) {
        NS_WARNING("Re-registering a CID?");

        char idstr[NSID_LENGTH];
        aEntry->cid->ToProvidedString(idstr);

        nsCString existing;
        if (f->mModule)
            existing = f->mModule->Description();
        else
            existing = "<unknown module>";

        LogMessage("While registering XPCOM module %s, trying to re-register CID '%s' already registered by %s.",
                   aModule->Description().get(),
                   idstr,
                   existing.get());
        return;
    }

    f = new nsFactoryEntry(aEntry, aModule);
    mFactories.Put(*aEntry->cid, f);
}

void
nsComponentManagerImpl::RegisterContractID(const mozilla::Module::ContractIDEntry* aEntry)
{
    mMon.AssertCurrentThreadIn();

    nsFactoryEntry* f = mFactories.Get(*aEntry->cid);
    if (!f) {
        NS_ERROR("No CID found when attempting to map contract ID");

        char idstr[NSID_LENGTH];
        aEntry->cid->ToProvidedString(idstr);

        LogMessage("Could not map contract ID '%s' to CID %s because no implementation of the CID is registered.",
                   aEntry->contractid,
                   idstr);
                   
        return;
    }

    mContractIDs.Put(nsDependentCString(aEntry->contractid), f);
}

static void
CutExtension(nsCString& path)
{
    PRInt32 dotPos = path.RFindChar('.');
    if (kNotFound == dotPos)
        path.Truncate();
    else
        path.Cut(0, dotPos + 1);
}

void
nsComponentManagerImpl::RegisterManifest(NSLocationType aType,
                                         FileLocation &aFile,
                                         bool aChromeOnly)
{
    PRUint32 len;
    FileLocation::Data data;
    nsAutoArrayPtr<char> buf;
    nsresult rv = aFile.GetData(data);
    if (NS_SUCCEEDED(rv)) {
        rv = data.GetSize(&len);
    }
    if (NS_SUCCEEDED(rv)) {
        buf = new char[len + 1];
        rv = data.Copy(buf, len);
    }
    if (NS_SUCCEEDED(rv)) {
        buf[len] = '\0';
        ParseManifest(aType, aFile, buf, aChromeOnly);
    } else if (NS_BOOTSTRAPPED_LOCATION != aType) {
        nsCString uri;
        aFile.GetURIString(uri);
        LogMessage("Could not read chrome manifest '%s'.", uri.get());
    }
}

void
nsComponentManagerImpl::ManifestManifest(ManifestProcessingContext& cx, int lineno, char *const * argv)
{
    char* file = argv[0];
    FileLocation f(cx.mFile, file);
    RegisterManifest(cx.mType, f, cx.mChromeOnly);
}

void
nsComponentManagerImpl::ManifestBinaryComponent(ManifestProcessingContext& cx, int lineno, char *const * argv)
{
    if (cx.mFile.IsZip()) {
        NS_WARNING("Cannot load binary components from a jar.");
        LogMessageWithContext(cx.mFile, lineno,
                              "Cannot load binary components from a jar.");
        return;
    }

    FileLocation f(cx.mFile, argv[0]);
    nsCString uri;
    f.GetURIString(uri);

    if (mKnownModules.Get(uri)) {
        NS_WARNING("Attempting to register a binary component twice.");
        LogMessageWithContext(cx.mFile, lineno,
                              "Attempting to register a binary component twice.");
        return;
    }

    const mozilla::Module* m = mNativeModuleLoader.LoadModule(f);
    // The native module loader should report an error here, we don't have to
    if (!m)
        return;

    RegisterModule(m, &f);
}

void
nsComponentManagerImpl::ManifestXPT(ManifestProcessingContext& cx, int lineno, char *const * argv)
{
    FileLocation f(cx.mFile, argv[0]);
    PRUint32 len;
    FileLocation::Data data;
    nsAutoArrayPtr<char> buf;
    nsresult rv = f.GetData(data);
    if (NS_SUCCEEDED(rv)) {
        rv = data.GetSize(&len);
    }
    if (NS_SUCCEEDED(rv)) {
        buf = new char[len];
        rv = data.Copy(buf, len);
    }
    if (NS_SUCCEEDED(rv)) {
        xptiInterfaceInfoManager::GetSingleton()
            ->RegisterBuffer(buf, len);
    } else {
        nsCString uri;
        f.GetURIString(uri);
        LogMessage("Could not read '%s'.", uri.get());
    }
}

void
nsComponentManagerImpl::ManifestComponent(ManifestProcessingContext& cx, int lineno, char *const * argv)
{
    char* id = argv[0];
    char* file = argv[1];

    nsID cid;
    if (!cid.Parse(id)) {
        LogMessageWithContext(cx.mFile, lineno,
                              "Malformed CID: '%s'.", id);
        return;
    }

    ReentrantMonitorAutoEnter mon(mMon);
    nsFactoryEntry* f = mFactories.Get(cid);
    if (f) {
        char idstr[NSID_LENGTH];
        cid.ToProvidedString(idstr);

        nsCString existing;
        if (f->mModule)
            existing = f->mModule->Description();
        else
            existing = "<unknown module>";

        LogMessageWithContext(cx.mFile, lineno,
                              "Trying to re-register CID '%s' already registered by %s.",
                              idstr,
                              existing.get());
        return;
    }

    KnownModule* km;
    FileLocation fl(cx.mFile, file);

    nsCString hash;
    fl.GetURIString(hash);
    km = mKnownModules.Get(hash);
    if (!km) {
        km = new KnownModule(fl);
        mKnownModules.Put(hash, km);
    }

    void* place;

    PL_ARENA_ALLOCATE(place, &mArena, sizeof(nsCID));
    nsID* permanentCID = static_cast<nsID*>(place);
    *permanentCID = cid;

    PL_ARENA_ALLOCATE(place, &mArena, sizeof(mozilla::Module::CIDEntry));
    mozilla::Module::CIDEntry* e = new (place) mozilla::Module::CIDEntry();
    e->cid = permanentCID;

    f = new nsFactoryEntry(e, km);
    mFactories.Put(cid, f);
}

void
nsComponentManagerImpl::ManifestContract(ManifestProcessingContext& cx, int lineno, char *const * argv)
{
    char* contract = argv[0];
    char* id = argv[1];

    nsID cid;
    if (!cid.Parse(id)) {
        LogMessageWithContext(cx.mFile, lineno,
                              "Malformed CID: '%s'.", id);
        return;
    }

    ReentrantMonitorAutoEnter mon(mMon);
    nsFactoryEntry* f = mFactories.Get(cid);
    if (!f) {
        LogMessageWithContext(cx.mFile, lineno,
                              "Could not map contract ID '%s' to CID %s because no implementation of the CID is registered.",
                              contract, id);
        return;
    }

    mContractIDs.Put(nsDependentCString(contract), f);
}

void
nsComponentManagerImpl::ManifestCategory(ManifestProcessingContext& cx, int lineno, char *const * argv)
{
    char* category = argv[0];
    char* key = argv[1];
    char* value = argv[2];

    nsCategoryManager::GetSingleton()->
        AddCategoryEntry(category, key, value);
}

void
nsComponentManagerImpl::RereadChromeManifests(bool aChromeOnly)
{
    for (PRUint32 i = 0; i < sModuleLocations->Length(); ++i) {
        ComponentLocation& l = sModuleLocations->ElementAt(i);
        RegisterManifest(l.type, l.location, aChromeOnly);
    }
}

bool
nsComponentManagerImpl::KnownModule::EnsureLoader()
{
    if (!mLoader) {
        nsCString extension;
        mFile.GetURIString(extension);
        CutExtension(extension);
        mLoader = nsComponentManagerImpl::gComponentManager->LoaderForExtension(extension);
    }
    return !!mLoader;
}

bool
nsComponentManagerImpl::KnownModule::Load()
{
    if (mFailed)
        return false;
    if (!mModule) {
        if (!EnsureLoader())
            return false;

        mModule = mLoader->LoadModule(mFile);

        if (!mModule) {
            mFailed = true;
            return false;
        }
    }
    if (!mLoaded) {
        if (mModule->loadProc) {
            nsresult rv = mModule->loadProc();
            if (NS_FAILED(rv)) {
                mFailed = true;
                return false;
            }
        }
        mLoaded = true;
    }
    return true;
}

nsCString
nsComponentManagerImpl::KnownModule::Description() const
{
    nsCString s;
    if (mFile)
        mFile.GetURIString(s);
    else
        s = "<static module>";
    return s;
}

nsresult nsComponentManagerImpl::Shutdown(void)
{
    NS_TIME_FUNCTION;

    PR_ASSERT(NORMAL == mStatus);

    mStatus = SHUTDOWN_IN_PROGRESS;

    // Shutdown the component manager
    PR_LOG(nsComponentManagerLog, PR_LOG_DEBUG, ("nsComponentManager: Beginning Shutdown."));

    // Release all cached factories
    mContractIDs.Clear();
    mFactories.Clear(); // XXX release the objects, don't just clear
    mLoaderMap.Clear();
    mKnownModules.Clear();
    mKnownStaticModules.Clear();

    mLoaderData.Clear();

    delete sStaticModules;
    delete sModuleLocations;

    // Unload libraries
    mNativeModuleLoader.UnloadLibraries();

    // delete arena for strings and small objects
    PL_FinishArenaPool(&mArena);

    mStatus = SHUTDOWN_COMPLETE;

    PR_LOG(nsComponentManagerLog, PR_LOG_DEBUG, ("nsComponentManager: Shutdown complete."));

    return NS_OK;
}

nsComponentManagerImpl::~nsComponentManagerImpl()
{
    PR_LOG(nsComponentManagerLog, PR_LOG_DEBUG, ("nsComponentManager: Beginning destruction."));

    if (SHUTDOWN_COMPLETE != mStatus)
        Shutdown();

    PR_LOG(nsComponentManagerLog, PR_LOG_DEBUG, ("nsComponentManager: Destroyed."));
}

NS_IMPL_THREADSAFE_ISUPPORTS5(nsComponentManagerImpl,
                              nsIComponentManager,
                              nsIServiceManager,
                              nsIComponentRegistrar,
                              nsISupportsWeakReference,
                              nsIInterfaceRequestor)


nsresult
nsComponentManagerImpl::GetInterface(const nsIID & uuid, void **result)
{
    NS_WARNING("This isn't supported");
    // fall through to QI as anything QIable is a superset of what can be
    // got via the GetInterface()
    return  QueryInterface(uuid, result);
}

nsFactoryEntry *
nsComponentManagerImpl::GetFactoryEntry(const char *aContractID,
                                        PRUint32 aContractIDLen)
{
    ReentrantMonitorAutoEnter mon(mMon);
    return mContractIDs.Get(nsDependentCString(aContractID, aContractIDLen));
}


nsFactoryEntry *
nsComponentManagerImpl::GetFactoryEntry(const nsCID &aClass)
{
    ReentrantMonitorAutoEnter mon(mMon);
    return mFactories.Get(aClass);
}

already_AddRefed<nsIFactory>
nsComponentManagerImpl::FindFactory(const nsCID& aClass)
{
    nsFactoryEntry* e = GetFactoryEntry(aClass);
    if (!e)
        return NULL;

    return e->GetFactory();
}

already_AddRefed<nsIFactory>
nsComponentManagerImpl::FindFactory(const char *contractID,
                                    PRUint32 aContractIDLen)
{
    nsFactoryEntry *entry = GetFactoryEntry(contractID, aContractIDLen);
    if (!entry)
        return NULL;

    return entry->GetFactory();
}

/**
 * GetClassObject()
 *
 * Given a classID, this finds the singleton ClassObject that implements the CID.
 * Returns an interface of type aIID off the singleton classobject.
 */
NS_IMETHODIMP
nsComponentManagerImpl::GetClassObject(const nsCID &aClass, const nsIID &aIID,
                                       void **aResult)
{
    nsresult rv;

#ifdef PR_LOGGING
    if (PR_LOG_TEST(nsComponentManagerLog, PR_LOG_DEBUG))
    {
        char *buf = aClass.ToString();
        PR_LogPrint("nsComponentManager: GetClassObject(%s)", buf);
        if (buf)
            NS_Free(buf);
    }
#endif

    PR_ASSERT(aResult != nsnull);

    nsCOMPtr<nsIFactory> factory = FindFactory(aClass);
    if (!factory)
        return NS_ERROR_FACTORY_NOT_REGISTERED;

    rv = factory->QueryInterface(aIID, aResult);

    PR_LOG(nsComponentManagerLog, PR_LOG_WARNING,
           ("\t\tGetClassObject() %s", NS_SUCCEEDED(rv) ? "succeeded" : "FAILED"));

    return rv;
}


NS_IMETHODIMP
nsComponentManagerImpl::GetClassObjectByContractID(const char *contractID,
                                                   const nsIID &aIID,
                                                   void **aResult)
{
    NS_ENSURE_ARG_POINTER(aResult);
    NS_ENSURE_ARG_POINTER(contractID);

    nsresult rv;


#ifdef PR_LOGGING
    if (PR_LOG_TEST(nsComponentManagerLog, PR_LOG_DEBUG))
    {
        PR_LogPrint("nsComponentManager: GetClassObject(%s)", contractID);
    }
#endif

    nsCOMPtr<nsIFactory> factory = FindFactory(contractID, strlen(contractID));
    if (!factory)
        return NS_ERROR_FACTORY_NOT_REGISTERED;

    rv = factory->QueryInterface(aIID, aResult);

    PR_LOG(nsComponentManagerLog, PR_LOG_WARNING,
           ("\t\tGetClassObject() %s", NS_SUCCEEDED(rv) ? "succeeded" : "FAILED"));

    return rv;
}

/**
 * CreateInstance()
 *
 * Create an instance of an object that implements an interface and belongs
 * to the implementation aClass using the factory. The factory is immediately
 * released and not held onto for any longer.
 */
NS_IMETHODIMP
nsComponentManagerImpl::CreateInstance(const nsCID &aClass,
                                       nsISupports *aDelegate,
                                       const nsIID &aIID,
                                       void **aResult)
{
    COMPMGR_TIME_FUNCTION_CID(aClass);

    // test this first, since there's no point in creating a component during
    // shutdown -- whether it's available or not would depend on the order it
    // occurs in the list
    if (gXPCOMShuttingDown) {
        // When processing shutdown, don't process new GetService() requests
#ifdef SHOW_DENIED_ON_SHUTDOWN
        nsXPIDLCString cid, iid;
        cid.Adopt(aClass.ToString());
        iid.Adopt(aIID.ToString());
        fprintf(stderr, "Creating new instance on shutdown. Denied.\n"
               "         CID: %s\n         IID: %s\n", cid.get(), iid.get());
#endif /* SHOW_DENIED_ON_SHUTDOWN */
        return NS_ERROR_UNEXPECTED;
    }

    if (aResult == nsnull)
    {
        return NS_ERROR_NULL_POINTER;
    }
    *aResult = nsnull;

    nsFactoryEntry *entry = GetFactoryEntry(aClass);

    if (!entry)
        return NS_ERROR_FACTORY_NOT_REGISTERED;

#ifdef SHOW_CI_ON_EXISTING_SERVICE
    if (entry->mServiceObject) {
        nsXPIDLCString cid;
        cid.Adopt(aClass.ToString());
        nsCAutoString message;
        message = NS_LITERAL_CSTRING("You are calling CreateInstance \"") +
                  cid + NS_LITERAL_CSTRING("\" when a service for this CID already exists!");
        NS_ERROR(message.get());
    }
#endif

    nsresult rv;
    nsCOMPtr<nsIFactory> factory = entry->GetFactory();
    if (factory)
    {
        rv = factory->CreateInstance(aDelegate, aIID, aResult);
        if (NS_SUCCEEDED(rv) && !*aResult) {
            NS_ERROR("Factory did not return an object but returned success!");
            rv = NS_ERROR_SERVICE_NOT_FOUND;
        }
    }
    else {
        // Translate error values
        rv = NS_ERROR_FACTORY_NOT_REGISTERED;
    }

#ifdef PR_LOGGING
    if (PR_LOG_TEST(nsComponentManagerLog, PR_LOG_WARNING))
    {
        char *buf = aClass.ToString();
        PR_LOG(nsComponentManagerLog, PR_LOG_WARNING,
               ("nsComponentManager: CreateInstance(%s) %s", buf,
                NS_SUCCEEDED(rv) ? "succeeded" : "FAILED"));
        if (buf)
            NS_Free(buf);
    }
#endif

    return rv;
}

/**
 * CreateInstanceByContractID()
 *
 * A variant of CreateInstance() that creates an instance of the object that
 * implements the interface aIID and whose implementation has a contractID aContractID.
 *
 * This is only a convenience routine that turns around can calls the
 * CreateInstance() with classid and iid.
 */
NS_IMETHODIMP
nsComponentManagerImpl::CreateInstanceByContractID(const char *aContractID,
                                                   nsISupports *aDelegate,
                                                   const nsIID &aIID,
                                                   void **aResult)
{
    COMPMGR_TIME_FUNCTION_CONTRACTID(aContractID);

    NS_ENSURE_ARG_POINTER(aContractID);

    // test this first, since there's no point in creating a component during
    // shutdown -- whether it's available or not would depend on the order it
    // occurs in the list
    if (gXPCOMShuttingDown) {
        // When processing shutdown, don't process new GetService() requests
#ifdef SHOW_DENIED_ON_SHUTDOWN
        nsXPIDLCString iid;
        iid.Adopt(aIID.ToString());
        fprintf(stderr, "Creating new instance on shutdown. Denied.\n"
               "  ContractID: %s\n         IID: %s\n", aContractID, iid.get());
#endif /* SHOW_DENIED_ON_SHUTDOWN */
        return NS_ERROR_UNEXPECTED;
    }

    if (aResult == nsnull)
    {
        return NS_ERROR_NULL_POINTER;
    }
    *aResult = nsnull;

    nsFactoryEntry *entry = GetFactoryEntry(aContractID, strlen(aContractID));

    if (!entry)
        return NS_ERROR_FACTORY_NOT_REGISTERED;

#ifdef SHOW_CI_ON_EXISTING_SERVICE
    if (entry->mServiceObject) {
        nsCAutoString message;
        message =
          NS_LITERAL_CSTRING("You are calling CreateInstance \"") +
          nsDependentCString(aContractID) +
          NS_LITERAL_CSTRING("\" when a service for this CID already exists! "
            "Add it to abusedContracts to track down the service consumer.");
        NS_ERROR(message.get());
    }
#endif

    nsresult rv;
    nsCOMPtr<nsIFactory> factory = entry->GetFactory();
    if (factory)
    {

        rv = factory->CreateInstance(aDelegate, aIID, aResult);
        if (NS_SUCCEEDED(rv) && !*aResult) {
            NS_ERROR("Factory did not return an object but returned success!");
            rv = NS_ERROR_SERVICE_NOT_FOUND;
        }
    }
    else {
        // Translate error values
        rv = NS_ERROR_FACTORY_NOT_REGISTERED;
    }

    PR_LOG(nsComponentManagerLog, PR_LOG_WARNING,
           ("nsComponentManager: CreateInstanceByContractID(%s) %s", aContractID,
            NS_SUCCEEDED(rv) ? "succeeded" : "FAILED"));

    return rv;
}

static PLDHashOperator
FreeFactoryEntries(const nsID& aCID,
                   nsFactoryEntry* aEntry,
                   void* arg)
{
    aEntry->mFactory = NULL;
    aEntry->mServiceObject = NULL;
    return PL_DHASH_NEXT;
}

nsresult
nsComponentManagerImpl::FreeServices()
{
    NS_ASSERTION(gXPCOMShuttingDown, "Must be shutting down in order to free all services");

    if (!gXPCOMShuttingDown)
        return NS_ERROR_FAILURE;

    mFactories.EnumerateRead(FreeFactoryEntries, NULL);
    return NS_OK;
}

// This should only ever be called within the monitor!
nsComponentManagerImpl::PendingServiceInfo*
nsComponentManagerImpl::AddPendingService(const nsCID& aServiceCID,
                                          PRThread* aThread)
{
  PendingServiceInfo* newInfo = mPendingServices.AppendElement();
  if (newInfo) {
    newInfo->cid = &aServiceCID;
    newInfo->thread = aThread;
  }
  return newInfo;
}

// This should only ever be called within the monitor!
void
nsComponentManagerImpl::RemovePendingService(const nsCID& aServiceCID)
{
  PRUint32 pendingCount = mPendingServices.Length();
  for (PRUint32 index = 0; index < pendingCount; ++index) {
    const PendingServiceInfo& info = mPendingServices.ElementAt(index);
    if (info.cid->Equals(aServiceCID)) {
      mPendingServices.RemoveElementAt(index);
      return;
    }
  }
}

// This should only ever be called within the monitor!
PRThread*
nsComponentManagerImpl::GetPendingServiceThread(const nsCID& aServiceCID) const
{
  PRUint32 pendingCount = mPendingServices.Length();
  for (PRUint32 index = 0; index < pendingCount; ++index) {
    const PendingServiceInfo& info = mPendingServices.ElementAt(index);
    if (info.cid->Equals(aServiceCID)) {
      return info.thread;
    }
  }
  return nsnull;
}

// GetService() wants to manually Exit()/Enter() a monitor which is
// wrapped in ReentrantMonitorAutoEnter, which nsAutoReentrantMonitor used to allow.
// One use is block-scoped Exit()/Enter(), which could be supported
// with something like a MonitoAutoExit, but that's not a well-defined
// operation in general so that helper doesn't exist.  The other use
// is early-Exit() for perf reasons.  This code is probably hot enough
// to warrant special considerations.
//
// We could use bare mozilla::ReentrantMonitor, but that's error prone.
// Instead, we just add a hacky wrapper here that acts like the old
// nsAutoReentrantMonitor.
struct NS_STACK_CLASS AutoReentrantMonitor
{
    AutoReentrantMonitor(ReentrantMonitor& aReentrantMonitor) : mReentrantMonitor(&aReentrantMonitor), mEnterCount(0)
    {
        Enter();
    }

    ~AutoReentrantMonitor()
    {
        if (mEnterCount) {
            Exit();
        }
    }

    void Enter()
    {
        mReentrantMonitor->Enter();
        ++mEnterCount;
    }

    void Exit()
    {
        --mEnterCount;
        mReentrantMonitor->Exit();
    }

    ReentrantMonitor* mReentrantMonitor;
    PRInt32 mEnterCount;
};

NS_IMETHODIMP
nsComponentManagerImpl::GetService(const nsCID& aClass,
                                   const nsIID& aIID,
                                   void* *result)
{
    // test this first, since there's no point in returning a service during
    // shutdown -- whether it's available or not would depend on the order it
    // occurs in the list
    if (gXPCOMShuttingDown) {
        // When processing shutdown, don't process new GetService() requests
#ifdef SHOW_DENIED_ON_SHUTDOWN
        nsXPIDLCString cid, iid;
        cid.Adopt(aClass.ToString());
        iid.Adopt(aIID.ToString());
        fprintf(stderr, "Getting service on shutdown. Denied.\n"
               "         CID: %s\n         IID: %s\n", cid.get(), iid.get());
#endif /* SHOW_DENIED_ON_SHUTDOWN */
        return NS_ERROR_UNEXPECTED;
    }

    AutoReentrantMonitor mon(mMon);

    nsFactoryEntry* entry = mFactories.Get(aClass);
    if (!entry)
        return NS_ERROR_FACTORY_NOT_REGISTERED;

    if (entry->mServiceObject) {
        nsCOMPtr<nsISupports> supports = entry->mServiceObject;
        mon.Exit();
        return supports->QueryInterface(aIID, result);
    }

    // We only care about time when we create the service.
    COMPMGR_TIME_FUNCTION_CID(aClass);

    PRThread* currentPRThread = PR_GetCurrentThread();
    NS_ASSERTION(currentPRThread, "This should never be null!");

    // Needed to optimize the event loop below.
    nsIThread* currentThread = nsnull;

    PRThread* pendingPRThread;
    while ((pendingPRThread = GetPendingServiceThread(aClass))) {
        if (pendingPRThread == currentPRThread) {
            NS_ERROR("Recursive GetService!");
            return NS_ERROR_NOT_AVAILABLE;
        }

        mon.Exit();

        if (!currentThread) {
            currentThread = NS_GetCurrentThread();
            NS_ASSERTION(currentThread, "This should never be null!");
        }

        // This will process a single event or yield the thread if no event is
        // pending.
        if (!NS_ProcessNextEvent(currentThread, false)) {
            PR_Sleep(PR_INTERVAL_NO_WAIT);
        }

        mon.Enter();
    }

    // It's still possible that the other thread failed to create the
    // service so we're not guaranteed to have an entry or service yet.
    if (entry->mServiceObject) {
        nsCOMPtr<nsISupports> supports = entry->mServiceObject;
        mon.Exit();
        return supports->QueryInterface(aIID, result);
    }

#ifdef DEBUG
    PendingServiceInfo* newInfo =
#endif
    AddPendingService(aClass, currentPRThread);
    NS_ASSERTION(newInfo, "Failed to add info to the array!");

    nsCOMPtr<nsISupports> service;
    // We need to not be holding the service manager's monitor while calling
    // CreateInstance, because it invokes user code which could try to re-enter
    // the service manager:
    mon.Exit();

    nsresult rv = CreateInstance(aClass, nsnull, aIID, getter_AddRefs(service));

    mon.Enter();

#ifdef DEBUG
    pendingPRThread = GetPendingServiceThread(aClass);
    NS_ASSERTION(pendingPRThread == currentPRThread,
                 "Pending service array has been changed!");
#endif
    RemovePendingService(aClass);

    if (NS_FAILED(rv))
        return rv;

    NS_ASSERTION(!entry->mServiceObject, "Created two instances of a service!");

    entry->mServiceObject = service;
    *result = service.get();
    if (!*result) {
        NS_ERROR("Factory did not return an object but returned success!");
        return NS_ERROR_SERVICE_NOT_FOUND;
    }
    NS_ADDREF(static_cast<nsISupports*>((*result)));
    return rv;
}

NS_IMETHODIMP
nsComponentManagerImpl::IsServiceInstantiated(const nsCID & aClass,
                                              const nsIID& aIID,
                                              bool *result)
{
    COMPMGR_TIME_FUNCTION_CID(aClass);

    // Now we want to get the service if we already got it. If not, we don't want
    // to create an instance of it. mmh!

    // test this first, since there's no point in returning a service during
    // shutdown -- whether it's available or not would depend on the order it
    // occurs in the list
    if (gXPCOMShuttingDown) {
        // When processing shutdown, don't process new GetService() requests
#ifdef SHOW_DENIED_ON_SHUTDOWN
        nsXPIDLCString cid, iid;
        cid.Adopt(aClass.ToString());
        iid.Adopt(aIID.ToString());
        fprintf(stderr, "Checking for service on shutdown. Denied.\n"
               "         CID: %s\n         IID: %s\n", cid.get(), iid.get());
#endif /* SHOW_DENIED_ON_SHUTDOWN */
        return NS_ERROR_UNEXPECTED;
    }

    nsresult rv = NS_ERROR_SERVICE_NOT_AVAILABLE;
    nsFactoryEntry* entry;

    {
        ReentrantMonitorAutoEnter mon(mMon);
        entry = mFactories.Get(aClass);
    }

    if (entry && entry->mServiceObject) {
        nsCOMPtr<nsISupports> service;
        rv = entry->mServiceObject->QueryInterface(aIID, getter_AddRefs(service));
        *result = (service!=nsnull);
    }

    return rv;
}

NS_IMETHODIMP nsComponentManagerImpl::IsServiceInstantiatedByContractID(const char *aContractID,
                                                                        const nsIID& aIID,
                                                                        bool *result)
{
    COMPMGR_TIME_FUNCTION_CONTRACTID(aContractID);

    // Now we want to get the service if we already got it. If not, we don't want
    // to create an instance of it. mmh!

    // test this first, since there's no point in returning a service during
    // shutdown -- whether it's available or not would depend on the order it
    // occurs in the list
    if (gXPCOMShuttingDown) {
        // When processing shutdown, don't process new GetService() requests
#ifdef SHOW_DENIED_ON_SHUTDOWN
        nsXPIDLCString iid;
        iid.Adopt(aIID.ToString());
        fprintf(stderr, "Checking for service on shutdown. Denied.\n"
               "  ContractID: %s\n         IID: %s\n", aContractID, iid.get());
#endif /* SHOW_DENIED_ON_SHUTDOWN */
        return NS_ERROR_UNEXPECTED;
    }

    nsresult rv = NS_ERROR_SERVICE_NOT_AVAILABLE;
    nsFactoryEntry *entry;
    {
        ReentrantMonitorAutoEnter mon(mMon);
        entry = mContractIDs.Get(nsDependentCString(aContractID));
    }

    if (entry && entry->mServiceObject) {
        nsCOMPtr<nsISupports> service;
        rv = entry->mServiceObject->QueryInterface(aIID, getter_AddRefs(service));
        *result = (service!=nsnull);
    }
    return rv;
}


NS_IMETHODIMP
nsComponentManagerImpl::GetServiceByContractID(const char* aContractID,
                                               const nsIID& aIID,
                                               void* *result)
{
    // test this first, since there's no point in returning a service during
    // shutdown -- whether it's available or not would depend on the order it
    // occurs in the list
    if (gXPCOMShuttingDown) {
        // When processing shutdown, don't process new GetService() requests
#ifdef SHOW_DENIED_ON_SHUTDOWN
        nsXPIDLCString iid;
        iid.Adopt(aIID.ToString());
        fprintf(stderr, "Getting service on shutdown. Denied.\n"
               "  ContractID: %s\n         IID: %s\n", aContractID, iid.get());
#endif /* SHOW_DENIED_ON_SHUTDOWN */
        return NS_ERROR_UNEXPECTED;
    }

    AutoReentrantMonitor mon(mMon);

    nsFactoryEntry *entry = mContractIDs.Get(nsDependentCString(aContractID));
    if (!entry)
        return NS_ERROR_FACTORY_NOT_REGISTERED;

    if (entry->mServiceObject) {
        nsCOMPtr<nsISupports> serviceObject = entry->mServiceObject;

        // We need to not be holding the service manager's monitor while calling
        // QueryInterface, because it invokes user code which could try to re-enter
        // the service manager, or try to grab some other lock/monitor/condvar
        // and deadlock, e.g. bug 282743.
        mon.Exit();
        return serviceObject->QueryInterface(aIID, result);
    }

    // We only care about time when we create the service.
    COMPMGR_TIME_FUNCTION_CONTRACTID(aContractID);

    PRThread* currentPRThread = PR_GetCurrentThread();
    NS_ASSERTION(currentPRThread, "This should never be null!");

    // Needed to optimize the event loop below.
    nsIThread* currentThread = nsnull;

    PRThread* pendingPRThread;
    while ((pendingPRThread = GetPendingServiceThread(*entry->mCIDEntry->cid))) {
        if (pendingPRThread == currentPRThread) {
            NS_ERROR("Recursive GetService!");
            return NS_ERROR_NOT_AVAILABLE;
        }

        mon.Exit();

        if (!currentThread) {
            currentThread = NS_GetCurrentThread();
            NS_ASSERTION(currentThread, "This should never be null!");
        }

        // This will process a single event or yield the thread if no event is
        // pending.
        if (!NS_ProcessNextEvent(currentThread, false)) {
            PR_Sleep(PR_INTERVAL_NO_WAIT);
        }

        mon.Enter();
    }

    if (currentThread && entry->mServiceObject) {
        // If we have a currentThread then we must have waited on another thread
        // to create the service. Grab it now if that succeeded.
        nsCOMPtr<nsISupports> serviceObject = entry->mServiceObject;
        mon.Exit();
        return serviceObject->QueryInterface(aIID, result);
    }

#ifdef DEBUG
    PendingServiceInfo* newInfo =
#endif
    AddPendingService(*entry->mCIDEntry->cid, currentPRThread);
    NS_ASSERTION(newInfo, "Failed to add info to the array!");

    nsCOMPtr<nsISupports> service;
    // We need to not be holding the service manager's monitor while calling
    // CreateInstance, because it invokes user code which could try to re-enter
    // the service manager:
    mon.Exit();

    nsresult rv = CreateInstanceByContractID(aContractID, nsnull, aIID,
                                             getter_AddRefs(service));

    mon.Enter();

#ifdef DEBUG
    pendingPRThread = GetPendingServiceThread(*entry->mCIDEntry->cid);
    NS_ASSERTION(pendingPRThread == currentPRThread,
                 "Pending service array has been changed!");
#endif
    RemovePendingService(*entry->mCIDEntry->cid);

    if (NS_FAILED(rv))
        return rv;

    NS_ASSERTION(!entry->mServiceObject, "Created two instances of a service!");

    entry->mServiceObject = service;
    *result = service.get();
    NS_ADDREF(static_cast<nsISupports*>(*result));
    return rv;
}

already_AddRefed<mozilla::ModuleLoader>
nsComponentManagerImpl::LoaderForExtension(const nsACString& aExt)
{
    nsCOMPtr<mozilla::ModuleLoader> loader = mLoaderMap.Get(aExt);
    if (!loader) {
        loader = do_GetServiceFromCategory("module-loader",
                                           PromiseFlatCString(aExt).get());
        if (!loader)
            return NULL;

        mLoaderMap.Put(aExt, loader);
    }

    return loader.forget();
}

NS_IMETHODIMP
nsComponentManagerImpl::RegisterFactory(const nsCID& aClass,
                                        const char* aName,
                                        const char* aContractID,
                                        nsIFactory* aFactory)
{
    if (!aFactory) {
        // If a null factory is passed in, this call just wants to reset
        // the contract ID to point to an existing CID entry.
        if (!aContractID)
            return NS_ERROR_INVALID_ARG;

        ReentrantMonitorAutoEnter mon(mMon);
        nsFactoryEntry* oldf = mFactories.Get(aClass);
        if (!oldf)
            return NS_ERROR_FACTORY_NOT_REGISTERED;

        mContractIDs.Put(nsDependentCString(aContractID), oldf);
        return NS_OK;
    }

    nsAutoPtr<nsFactoryEntry> f(new nsFactoryEntry(aClass, aFactory));

    ReentrantMonitorAutoEnter mon(mMon);
    nsFactoryEntry* oldf = mFactories.Get(aClass);
    if (oldf)
        return NS_ERROR_FACTORY_EXISTS;

    if (aContractID)
        mContractIDs.Put(nsDependentCString(aContractID), f);

    mFactories.Put(aClass, f.forget());

    return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::UnregisterFactory(const nsCID& aClass,
                                          nsIFactory* aFactory)
{
    // Don't release the dying factory or service object until releasing
    // the component manager monitor.
    nsCOMPtr<nsIFactory> dyingFactory;
    nsCOMPtr<nsISupports> dyingServiceObject;

    {
        ReentrantMonitorAutoEnter mon(mMon);
        nsFactoryEntry* f = mFactories.Get(aClass);
        if (!f || f->mFactory != aFactory)
            return NS_ERROR_FACTORY_NOT_REGISTERED;

        mFactories.Remove(aClass);

        // This might leave a stale contractid -> factory mapping in
        // place, so null out the factory entry (see
        // nsFactoryEntry::GetFactory)
        f->mFactory.swap(dyingFactory);
        f->mServiceObject.swap(dyingServiceObject);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::AutoRegister(nsIFile* aLocation)
{
    XRE_AddManifestLocation(NS_COMPONENT_LOCATION, aLocation);
    return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::AutoUnregister(nsIFile* aLocation)
{
    NS_ERROR("AutoUnregister not implemented.");
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsComponentManagerImpl::RegisterFactoryLocation(const nsCID& aCID,
                                                const char* aClassName,
                                                const char* aContractID,
                                                nsIFile* aFile,
                                                const char* aLoaderStr,
                                                const char* aType)
{
    NS_ERROR("RegisterFactoryLocation not implemented.");
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsComponentManagerImpl::UnregisterFactoryLocation(const nsCID& aCID,
                                                  nsIFile* aFile)
{
    NS_ERROR("UnregisterFactoryLocation not implemented.");
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsComponentManagerImpl::IsCIDRegistered(const nsCID & aClass,
                                        bool *_retval)
{
    *_retval = (nsnull != GetFactoryEntry(aClass));
    return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::IsContractIDRegistered(const char *aClass,
                                               bool *_retval)
{
    NS_ENSURE_ARG_POINTER(aClass);
    nsFactoryEntry *entry = GetFactoryEntry(aClass, strlen(aClass));

    if (entry)
        *_retval = true;
    else
        *_retval = false;
    return NS_OK;
}

static PLDHashOperator
EnumerateCIDHelper(const nsID& id, nsFactoryEntry* entry, void* closure)
{
    nsCOMArray<nsISupports> *array = static_cast<nsCOMArray<nsISupports>*>(closure);
    nsCOMPtr<nsISupportsID> wrapper = new nsSupportsIDImpl();
    wrapper->SetData(&id);
    array->AppendObject(wrapper);
    return PL_DHASH_NEXT;
}

NS_IMETHODIMP
nsComponentManagerImpl::EnumerateCIDs(nsISimpleEnumerator **aEnumerator)
{
    NS_TIME_FUNCTION;

    nsCOMArray<nsISupports> array;
    mFactories.EnumerateRead(EnumerateCIDHelper, &array);

    return NS_NewArrayEnumerator(aEnumerator, array);
}

static PLDHashOperator
EnumerateContractsHelper(const nsACString& contract, nsFactoryEntry* entry, void* closure)
{
    nsTArray<nsCString>* array = static_cast<nsTArray<nsCString>*>(closure);
    array->AppendElement(contract);
    return PL_DHASH_NEXT;
}

NS_IMETHODIMP
nsComponentManagerImpl::EnumerateContractIDs(nsISimpleEnumerator **aEnumerator)
{
    NS_TIME_FUNCTION;

    nsTArray<nsCString>* array = new nsTArray<nsCString>;
    mContractIDs.EnumerateRead(EnumerateContractsHelper, array);

    nsCOMPtr<nsIUTF8StringEnumerator> e;
    nsresult rv = NS_NewAdoptingUTF8StringEnumerator(getter_AddRefs(e), array);
    if (NS_FAILED(rv))
        return rv;

    return CallQueryInterface(e, aEnumerator);
}

NS_IMETHODIMP
nsComponentManagerImpl::CIDToContractID(const nsCID & aClass,
                                        char **_retval)
{
    NS_ERROR("CIDTOContractID not implemented");
    return NS_ERROR_FACTORY_NOT_REGISTERED;
}

NS_IMETHODIMP
nsComponentManagerImpl::ContractIDToCID(const char *aContractID,
                                        nsCID * *_retval)
{
    {
        ReentrantMonitorAutoEnter mon(mMon);
        nsFactoryEntry* entry = mContractIDs.Get(nsDependentCString(aContractID));
        if (entry) {
            *_retval = (nsCID*) NS_Alloc(sizeof(nsCID));
            **_retval = *entry->mCIDEntry->cid;
            return NS_OK;
        }
    }
    *_retval = NULL;
    return NS_ERROR_FACTORY_NOT_REGISTERED;
}

////////////////////////////////////////////////////////////////////////////////
// nsFactoryEntry
////////////////////////////////////////////////////////////////////////////////

nsFactoryEntry::nsFactoryEntry(const mozilla::Module::CIDEntry* entry,
                               nsComponentManagerImpl::KnownModule* module)
    : mCIDEntry(entry)
    , mModule(module)
{
}

nsFactoryEntry::nsFactoryEntry(const nsCID& aCID, nsIFactory* factory)
    : mCIDEntry(NULL)
    , mModule(NULL)
    , mFactory(factory)
{
    mozilla::Module::CIDEntry* e = new mozilla::Module::CIDEntry();
    nsCID* cid = new nsCID;
    *cid = aCID;
    e->cid = cid;
    mCIDEntry = e;
}        

nsFactoryEntry::~nsFactoryEntry()
{
    // If this was a RegisterFactory entry, we own the CIDEntry/CID
    if (!mModule) {
        delete mCIDEntry->cid;
        delete mCIDEntry;
    }
}

already_AddRefed<nsIFactory>
nsFactoryEntry::GetFactory()
{
    if (!mFactory) {
        // RegisterFactory then UnregisterFactory can leave an entry in mContractIDs
        // pointing to an unusable nsFactoryEntry.
        if (!mModule)
            return NULL;

        if (!mModule->Load())
            return NULL;

        if (mModule->Module()->getFactoryProc) {
            mFactory = mModule->Module()->getFactoryProc(*mModule->Module(),
                                                         *mCIDEntry);
        }
        else if (mCIDEntry->getFactoryProc) {
            mFactory = mCIDEntry->getFactoryProc(*mModule->Module(), *mCIDEntry);
        }
        else {
            NS_ASSERTION(mCIDEntry->constructorProc, "no getfactory or constructor");
            mFactory = new mozilla::GenericFactory(mCIDEntry->constructorProc);
        }
        if (!mFactory)
            return NULL;
    }
    nsIFactory* factory = mFactory.get();
    NS_ADDREF(factory);
    return factory;
}

////////////////////////////////////////////////////////////////////////////////
// Static Access Functions
////////////////////////////////////////////////////////////////////////////////

nsresult
NS_GetComponentManager(nsIComponentManager* *result)
{
    if (!nsComponentManagerImpl::gComponentManager)
        return NS_ERROR_NOT_INITIALIZED;

    NS_ADDREF(*result = nsComponentManagerImpl::gComponentManager);
    return NS_OK;
}

nsresult
NS_GetServiceManager(nsIServiceManager* *result)
{
    if (!nsComponentManagerImpl::gComponentManager)
        return NS_ERROR_NOT_INITIALIZED;

    NS_ADDREF(*result = nsComponentManagerImpl::gComponentManager);
    return NS_OK;
}


nsresult
NS_GetComponentRegistrar(nsIComponentRegistrar* *result)
{
    if (!nsComponentManagerImpl::gComponentManager)
        return NS_ERROR_NOT_INITIALIZED;

    NS_ADDREF(*result = nsComponentManagerImpl::gComponentManager);
    return NS_OK;
}

EXPORT_XPCOM_API(nsresult)
XRE_AddStaticComponent(const mozilla::Module* aComponent)
{
    nsComponentManagerImpl::InitializeStaticModules();
    nsComponentManagerImpl::sStaticModules->AppendElement(aComponent);

    if (nsComponentManagerImpl::gComponentManager &&
        nsComponentManagerImpl::NORMAL == nsComponentManagerImpl::gComponentManager->mStatus)
        nsComponentManagerImpl::gComponentManager->RegisterModule(aComponent, NULL);

    return NS_OK;
}

NS_IMETHODIMP
nsComponentManagerImpl::AddBootstrappedManifestLocation(nsIFile* aLocation)
{
  nsString path;
  nsresult rv = aLocation->GetPath(path);
  if (NS_FAILED(rv))
    return rv;

  if (Substring(path, path.Length() - 4).Equals(NS_LITERAL_STRING(".xpi"))) {
    return XRE_AddJarManifestLocation(NS_BOOTSTRAPPED_LOCATION, aLocation);
  }

  nsCOMPtr<nsIFile> manifest =
    CloneAndAppend(aLocation, NS_LITERAL_CSTRING("chrome.manifest"));
  return XRE_AddManifestLocation(NS_BOOTSTRAPPED_LOCATION, manifest);
}

NS_IMETHODIMP
nsComponentManagerImpl::RemoveBootstrappedManifestLocation(nsIFile* aLocation)
{
  nsCOMPtr<nsIChromeRegistry> cr = mozilla::services::GetChromeRegistryService();
  if (!cr)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIFile> manifest;
  nsString path;
  nsresult rv = aLocation->GetPath(path);
  if (NS_FAILED(rv))
    return rv;

  nsComponentManagerImpl::ComponentLocation elem;
  elem.type = NS_BOOTSTRAPPED_LOCATION;

  if (Substring(path, path.Length() - 4).Equals(NS_LITERAL_STRING(".xpi"))) {
    elem.location.Init(aLocation, "chrome.manifest");
  } else {
    nsCOMPtr<nsIFile> lf = CloneAndAppend(aLocation, NS_LITERAL_CSTRING("chrome.manifest"));
    elem.location.Init(lf);
  }

  // Remove reference.
  nsComponentManagerImpl::sModuleLocations->RemoveElement(elem, ComponentLocationComparator());

  rv = cr->CheckForNewChrome();
  return rv;
}

EXPORT_XPCOM_API(nsresult)
XRE_AddManifestLocation(NSLocationType aType, nsIFile* aLocation)
{
    nsComponentManagerImpl::InitializeModuleLocations();
    nsComponentManagerImpl::ComponentLocation* c = 
        nsComponentManagerImpl::sModuleLocations->AppendElement();
    c->type = aType;
    c->location.Init(aLocation);

    if (nsComponentManagerImpl::gComponentManager &&
        nsComponentManagerImpl::NORMAL == nsComponentManagerImpl::gComponentManager->mStatus)
        nsComponentManagerImpl::gComponentManager->RegisterManifest(aType, c->location, false);

    return NS_OK;
}

EXPORT_XPCOM_API(nsresult)
XRE_AddJarManifestLocation(NSLocationType aType, nsIFile* aLocation)
{
    nsComponentManagerImpl::InitializeModuleLocations();
    nsComponentManagerImpl::ComponentLocation* c = 
        nsComponentManagerImpl::sModuleLocations->AppendElement();

    c->type = aType;
    c->location.Init(aLocation, "chrome.manifest");

    if (nsComponentManagerImpl::gComponentManager &&
        nsComponentManagerImpl::NORMAL == nsComponentManagerImpl::gComponentManager->mStatus)
        nsComponentManagerImpl::gComponentManager->RegisterManifest(aType, c->location, false);

    return NS_OK;
}

