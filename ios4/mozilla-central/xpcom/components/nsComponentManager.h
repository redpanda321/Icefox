/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#ifndef nsComponentManager_h__
#define nsComponentManager_h__

#include "nsXPCOM.h"

#include "xpcom-private.h"
#include "nsIComponentManager.h"
#include "nsIComponentRegistrar.h"
#include "nsIServiceManager.h"
#include "nsILocalFile.h"
#include "mozilla/Module.h"
#include "mozilla/ModuleLoader.h"
#include "nsXULAppAPI.h"
#include "nsNativeComponentLoader.h"
#include "nsIFactory.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "pldhash.h"
#include "prtime.h"
#include "prmon.h"
#include "nsCOMPtr.h"
#include "nsAutoPtr.h"
#include "nsWeakReference.h"
#include "nsIFile.h"
#include "plarena.h"
#include "nsCOMArray.h"
#include "nsDataHashtable.h"
#include "nsInterfaceHashtable.h"
#include "nsClassHashtable.h"
#include "nsTArray.h"

#ifdef MOZ_OMNIJAR
#include "mozilla/Omnijar.h"
#include "nsManifestZIPLoader.h"
#endif

struct nsFactoryEntry;
class nsIServiceManager;
struct PRThread;

#define NS_COMPONENTMANAGER_CID                      \
{ /* 91775d60-d5dc-11d2-92fb-00e09805570f */         \
    0x91775d60,                                      \
    0xd5dc,                                          \
    0x11d2,                                          \
    {0x92, 0xfb, 0x00, 0xe0, 0x98, 0x05, 0x57, 0x0f} \
}

/* keys for registry use */
extern const char xpcomKeyName[];
extern const char xpcomComponentsKeyName[];
extern const char lastModValueName[];
extern const char fileSizeValueName[];
extern const char nativeComponentType[];
extern const char staticComponentType[];

typedef int LoaderType;

// Predefined loader types.
#define NS_LOADER_TYPE_NATIVE  -1
#define NS_LOADER_TYPE_STATIC  -2
#define NS_LOADER_TYPE_JAR     -3
#define NS_LOADER_TYPE_INVALID -4

#ifdef DEBUG
#define XPCOM_CHECK_PENDING_CIDS
#endif
////////////////////////////////////////////////////////////////////////////////

extern const mozilla::Module kXPCOMModule;

// Array of Loaders and their type strings
struct nsLoaderdata {
    nsCOMPtr<mozilla::ModuleLoader> loader;
    nsCString                 type;
};

class nsComponentManagerImpl
    : public nsIComponentManager
    , public nsIServiceManager
    , public nsSupportsWeakReference
    , public nsIComponentRegistrar
    , public nsIInterfaceRequestor
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIINTERFACEREQUESTOR
    NS_DECL_NSICOMPONENTMANAGER
    NS_DECL_NSICOMPONENTREGISTRAR

    static nsresult Create(nsISupports* aOuter, REFNSIID aIID, void** aResult);

    nsresult RegistryLocationForFile(nsIFile* aFile,
                                     nsCString& aResult);
    nsresult FileForRegistryLocation(const nsCString &aLocation,
                                     nsILocalFile **aSpec);

    NS_DECL_NSISERVICEMANAGER

    // nsComponentManagerImpl methods:
    nsComponentManagerImpl();

    static nsComponentManagerImpl* gComponentManager;
    nsresult Init();

    nsresult Shutdown(void);

    nsresult FreeServices();

    already_AddRefed<mozilla::ModuleLoader> LoaderForExtension(const nsACString& aExt);
    nsInterfaceHashtable<nsCStringHashKey, mozilla::ModuleLoader> mLoaderMap;

    already_AddRefed<nsIFactory> FindFactory(const nsCID& aClass);
    already_AddRefed<nsIFactory> FindFactory(const char *contractID,
                                             PRUint32 aContractIDLen);

    already_AddRefed<nsIFactory> LoadFactory(nsFactoryEntry *aEntry);

    nsFactoryEntry *GetFactoryEntry(const char *aContractID,
                                    PRUint32 aContractIDLen);
    nsFactoryEntry *GetFactoryEntry(const nsCID &aClass);

    nsDataHashtable<nsIDHashKey, nsFactoryEntry*> mFactories;
    nsDataHashtable<nsCStringHashKey, nsFactoryEntry*> mContractIDs;

    PRMonitor*          mMon;

    static void InitializeStaticModules();
    static void InitializeModuleLocations();

    struct ComponentLocation
    {
        NSLocationType type;
        nsCOMPtr<nsILocalFile> location;
    };

    static nsTArray<const mozilla::Module*>* sStaticModules;
    static nsTArray<ComponentLocation>* sModuleLocations;

    nsNativeModuleLoader mNativeModuleLoader;

    class KnownModule
    {
    public:
        /**
         * Static or binary module.
         */
        KnownModule(const mozilla::Module* aModule, nsILocalFile* aFile)
            : mModule(aModule)
            , mFile(aFile)
            , mLoaded(false)
            , mFailed(false)
        { }

        KnownModule(nsILocalFile* aFile)
            : mModule(NULL)
            , mFile(aFile)
            , mLoader(NULL)
            , mLoaded(false)
            , mFailed(false)
        { }

#ifdef MOZ_OMNIJAR
        KnownModule(const nsACString& aPath)
            : mModule(NULL)
            , mFile(NULL)
            , mPath(aPath)
            , mLoader(NULL)
            , mLoaded(false)
            , mFailed(false)
        { }
#endif

        ~KnownModule()
        {
            if (mLoaded && mModule->unloadProc)
                mModule->unloadProc();
        }

        bool EnsureLoader();
        bool Load();

        const mozilla::Module* Module() const
        {
            return mModule;
        }

        /**
         * For error logging, get a description of this module, either the
         * file path, or <static module>.
         */
        nsCString Description() const;

    private:
        const mozilla::Module* mModule;
        nsCOMPtr<nsILocalFile> mFile;
#ifdef MOZ_OMNIJAR
        nsCString mPath;
#endif
        nsCOMPtr<mozilla::ModuleLoader> mLoader;
        bool mLoaded;
        bool mFailed;
    };

    // The KnownModule is kept alive by these members, it is
    // referenced by pointer from the factory entries.
    nsTArray< nsAutoPtr<KnownModule> > mKnownStaticModules;
    nsClassHashtable<nsHashableHashKey, KnownModule> mKnownFileModules;
#ifdef MOZ_OMNIJAR
    nsClassHashtable<nsCStringHashKey, KnownModule> mKnownJARModules;
#endif

    void RegisterModule(const mozilla::Module* aModule,
                        nsILocalFile* aFile);
    void RegisterCIDEntry(const mozilla::Module::CIDEntry* aEntry,
                          KnownModule* aModule);
    void RegisterContractID(const mozilla::Module::ContractIDEntry* aEntry);

#ifdef MOZ_OMNIJAR
    void RegisterOmnijar(const char* aPath, bool aChromeOnly);
#endif

    void RegisterManifestFile(NSLocationType aType, nsILocalFile* aFile,
                              bool aChromeOnly);

    struct ManifestProcessingContext
    {
        ManifestProcessingContext(NSLocationType aType, nsILocalFile* aFile, bool aChromeOnly)
            : mType(aType)
            , mFile(aFile)
            , mPath(NULL)
            , mChromeOnly(aChromeOnly)
        { }

#ifdef MOZ_OMNIJAR
        ManifestProcessingContext(NSLocationType aType, const char* aPath, bool aChromeOnly)
            : mType(aType)
            , mFile(mozilla::OmnijarPath())
            , mPath(aPath)
            , mChromeOnly(aChromeOnly)
        { }
#endif

        ~ManifestProcessingContext() { }

        NSLocationType mType;
        nsILocalFile* mFile;
        const char* mPath;
        bool mChromeOnly;
    };

    void ManifestManifest(ManifestProcessingContext& cx, int lineno, char *const * argv);
    void ManifestBinaryComponent(ManifestProcessingContext& cx, int lineno, char *const * argv);
    void ManifestXPT(ManifestProcessingContext& cx, int lineno, char *const * argv);
    void ManifestComponent(ManifestProcessingContext& cx, int lineno, char *const * argv);
    void ManifestContract(ManifestProcessingContext& cx, int lineno, char* const * argv);
    void ManifestCategory(ManifestProcessingContext& cx, int lineno, char* const * argv);

    void RereadChromeManifests();

    // Shutdown
    enum {
        NOT_INITIALIZED,
        NORMAL,
        SHUTDOWN_IN_PROGRESS,
        SHUTDOWN_COMPLETE
    } mStatus;

    nsTArray<nsLoaderdata> mLoaderData;

    PLArenaPool   mArena;

    struct PendingServiceInfo {
      const nsCID* cid;
      PRThread* thread;
    };

    inline PendingServiceInfo* AddPendingService(const nsCID& aServiceCID,
                                                 PRThread* aThread);
    inline void RemovePendingService(const nsCID& aServiceCID);
    inline PRThread* GetPendingServiceThread(const nsCID& aServiceCID) const;

    nsTArray<PendingServiceInfo> mPendingServices;

private:
    ~nsComponentManagerImpl();

#ifdef MOZ_OMNIJAR
    nsAutoPtr<nsManifestZIPLoader> mManifestLoader;
#endif
};


#define NS_MAX_FILENAME_LEN     1024

#define NS_ERROR_IS_DIR NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_XPCOM, 24)

struct nsFactoryEntry
{
    nsFactoryEntry(const mozilla::Module::CIDEntry* entry,
                   nsComponentManagerImpl::KnownModule* module);

    // nsIComponentRegistrar.registerFactory support
    nsFactoryEntry(const nsCID& aClass, nsIFactory* factory);

    ~nsFactoryEntry();

    already_AddRefed<nsIFactory> GetFactory();

    const mozilla::Module::CIDEntry* mCIDEntry;
    nsComponentManagerImpl::KnownModule* mModule;

    nsCOMPtr<nsIFactory>   mFactory;
    nsCOMPtr<nsISupports>  mServiceObject;
};

#endif // nsComponentManager_h__
