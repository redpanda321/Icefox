/* vim:set ts=2 sw=2 sts=2 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsOfflineCacheDevice_h__
#define nsOfflineCacheDevice_h__

#include "nsCacheDevice.h"
#include "nsIApplicationCache.h"
#include "nsIApplicationCacheService.h"
#include "nsIObserver.h"
#include "mozIStorageConnection.h"
#include "mozIStorageFunction.h"
#include "nsIFile.h"
#include "nsAutoPtr.h"
#include "nsCOMPtr.h"
#include "nsCOMArray.h"
#include "nsInterfaceHashtable.h"
#include "nsClassHashtable.h"
#include "nsWeakReference.h"
#include "mozilla/Attributes.h"

class nsIURI;
class nsOfflineCacheDevice;

class nsApplicationCacheNamespace MOZ_FINAL : public nsIApplicationCacheNamespace
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIAPPLICATIONCACHENAMESPACE

  nsApplicationCacheNamespace() : mItemType(0) {}

private:
  PRUint32 mItemType;
  nsCString mNamespaceSpec;
  nsCString mData;
};

class nsOfflineCacheEvictionFunction MOZ_FINAL : public mozIStorageFunction {
public:
  NS_DECL_ISUPPORTS
  NS_DECL_MOZISTORAGEFUNCTION

  nsOfflineCacheEvictionFunction(nsOfflineCacheDevice *device)
    : mDevice(device)
  {}

  void Reset() { mItems.Clear(); }
  void Apply();

private:
  nsOfflineCacheDevice *mDevice;
  nsCOMArray<nsIFile> mItems;

};

class nsOfflineCacheDevice : public nsCacheDevice
                           , public nsISupports
{
public:
  nsOfflineCacheDevice();

  NS_DECL_ISUPPORTS

  /**
   * nsCacheDevice methods
   */

  virtual nsresult        Init();
  virtual nsresult        Shutdown();

  virtual const char *    GetDeviceID(void);
  virtual nsCacheEntry *  FindEntry(nsCString * key, bool *collision);
  virtual nsresult        DeactivateEntry(nsCacheEntry * entry);
  virtual nsresult        BindEntry(nsCacheEntry * entry);
  virtual void            DoomEntry( nsCacheEntry * entry );

  virtual nsresult OpenInputStreamForEntry(nsCacheEntry *    entry,
                                           nsCacheAccessMode mode,
                                           PRUint32          offset,
                                           nsIInputStream ** result);

  virtual nsresult OpenOutputStreamForEntry(nsCacheEntry *     entry,
                                            nsCacheAccessMode  mode,
                                            PRUint32           offset,
                                            nsIOutputStream ** result);

  virtual nsresult        GetFileForEntry(nsCacheEntry *    entry,
                                          nsIFile **        result);

  virtual nsresult        OnDataSizeChange(nsCacheEntry * entry, PRInt32 deltaSize);
  
  virtual nsresult        Visit(nsICacheVisitor * visitor);

  virtual nsresult        EvictEntries(const char * clientID);

  /* Entry ownership */
  nsresult                GetOwnerDomains(const char *        clientID,
                                          PRUint32 *          count,
                                          char ***            domains);
  nsresult                GetOwnerURIs(const char *           clientID,
                                       const nsACString &     ownerDomain,
                                       PRUint32 *             count,
                                       char ***               uris);
  nsresult                SetOwnedKeys(const char *           clientID,
                                       const nsACString &     ownerDomain,
                                       const nsACString &     ownerUrl,
                                       PRUint32               count,
                                       const char **          keys);
  nsresult                GetOwnedKeys(const char *           clientID,
                                       const nsACString &     ownerDomain,
                                       const nsACString &     ownerUrl,
                                       PRUint32 *             count,
                                       char ***               keys);
  nsresult                AddOwnedKey(const char *            clientID,
                                      const nsACString &      ownerDomain,
                                      const nsACString &      ownerURI,
                                      const nsACString &      key);
  nsresult                RemoveOwnedKey(const char *         clientID,
                                         const nsACString &   ownerDomain,
                                         const nsACString &   ownerURI,
                                         const nsACString &   key);
  nsresult                KeyIsOwned(const char *             clientID,
                                     const nsACString &       ownerDomain,
                                     const nsACString &       ownerURI,
                                     const nsACString &       key,
                                     bool *                 isOwned);

  nsresult                ClearKeysOwnedByDomain(const char *clientID,
                                                 const nsACString &ownerDomain);
  nsresult                EvictUnownedEntries(const char *clientID);

  nsresult                ActivateCache(const nsCSubstring &group,
                                        const nsCSubstring &clientID);
  bool                    IsActiveCache(const nsCSubstring &group,
                                        const nsCSubstring &clientID);
  nsresult                GetGroupForCache(const nsCSubstring &clientID,
                                           nsCString &out);

  nsresult                CreateApplicationCache(const nsACString &group,
                                                 nsIApplicationCache **out);

  nsresult                GetApplicationCache(const nsACString &clientID,
                                              nsIApplicationCache **out);

  nsresult                GetActiveCache(const nsACString &group,
                                         nsIApplicationCache **out);

  nsresult                DeactivateGroup(const nsACString &group);

  nsresult                ChooseApplicationCache(const nsACString &key,
                                                 nsIApplicationCache **out);

  nsresult                CacheOpportunistically(nsIApplicationCache* cache,
                                                 const nsACString &key);

  nsresult                GetGroups(PRUint32 *count,char ***keys);

  nsresult                GetGroupsTimeOrdered(PRUint32 *count,
                                               char ***keys);

  /**
   * Preference accessors
   */

  void                    SetCacheParentDirectory(nsIFile * parentDir);
  void                    SetCapacity(PRUint32  capacity);
  void                    SetAutoShutdown() { mAutoShutdown = true; }
  bool                    AutoShutdown(nsIApplicationCache * aAppCache);

  nsIFile *               BaseDirectory() { return mBaseDirectory; }
  nsIFile *               CacheDirectory() { return mCacheDirectory; }
  PRUint32                CacheCapacity() { return mCacheCapacity; }
  PRUint32                CacheSize();
  PRUint32                EntryCount();
  
private:
  friend class nsApplicationCache;

  static PLDHashOperator ShutdownApplicationCache(const nsACString &key,
                                                  nsIWeakReference *weakRef,
                                                  void *ctx);

  static bool GetStrictFileOriginPolicy();

  bool     Initialized() { return mDB != nsnull; }

  nsresult InitActiveCaches();
  nsresult UpdateEntry(nsCacheEntry *entry);
  nsresult UpdateEntrySize(nsCacheEntry *entry, PRUint32 newSize);
  nsresult DeleteEntry(nsCacheEntry *entry, bool deleteData);
  nsresult DeleteData(nsCacheEntry *entry);
  nsresult EnableEvictionObserver();
  nsresult DisableEvictionObserver();

  bool CanUseCache(nsIURI *keyURI, const nsCString &clientID);

  nsresult MarkEntry(const nsCString &clientID,
                     const nsACString &key,
                     PRUint32 typeBits);
  nsresult UnmarkEntry(const nsCString &clientID,
                       const nsACString &key,
                       PRUint32 typeBits);

  nsresult CacheOpportunistically(const nsCString &clientID,
                                  const nsACString &key);
  nsresult GetTypes(const nsCString &clientID,
                    const nsACString &key,
                    PRUint32 *typeBits);

  nsresult GetMatchingNamespace(const nsCString &clientID,
                                const nsACString &key,
                                nsIApplicationCacheNamespace **out);
  nsresult GatherEntries(const nsCString &clientID,
                         PRUint32 typeBits,
                         PRUint32 *count,
                         char *** values);
  nsresult AddNamespace(const nsCString &clientID,
                        nsIApplicationCacheNamespace *ns);

  nsresult GetUsage(const nsACString &clientID,
                    PRUint32 *usage);

  nsresult RunSimpleQuery(mozIStorageStatement *statment,
                          PRUint32 resultIndex,
                          PRUint32 * count,
                          char *** values);

  nsCOMPtr<mozIStorageConnection>          mDB;
  nsRefPtr<nsOfflineCacheEvictionFunction> mEvictionFunction;

  nsCOMPtr<mozIStorageStatement>  mStatement_CacheSize;
  nsCOMPtr<mozIStorageStatement>  mStatement_ApplicationCacheSize;
  nsCOMPtr<mozIStorageStatement>  mStatement_EntryCount;
  nsCOMPtr<mozIStorageStatement>  mStatement_UpdateEntry;
  nsCOMPtr<mozIStorageStatement>  mStatement_UpdateEntrySize;
  nsCOMPtr<mozIStorageStatement>  mStatement_UpdateEntryFlags;
  nsCOMPtr<mozIStorageStatement>  mStatement_DeleteEntry;
  nsCOMPtr<mozIStorageStatement>  mStatement_FindEntry;
  nsCOMPtr<mozIStorageStatement>  mStatement_BindEntry;
  nsCOMPtr<mozIStorageStatement>  mStatement_ClearDomain;
  nsCOMPtr<mozIStorageStatement>  mStatement_MarkEntry;
  nsCOMPtr<mozIStorageStatement>  mStatement_UnmarkEntry;
  nsCOMPtr<mozIStorageStatement>  mStatement_GetTypes;
  nsCOMPtr<mozIStorageStatement>  mStatement_FindNamespaceEntry;
  nsCOMPtr<mozIStorageStatement>  mStatement_InsertNamespaceEntry;
  nsCOMPtr<mozIStorageStatement>  mStatement_CleanupUnmarked;
  nsCOMPtr<mozIStorageStatement>  mStatement_GatherEntries;
  nsCOMPtr<mozIStorageStatement>  mStatement_ActivateClient;
  nsCOMPtr<mozIStorageStatement>  mStatement_DeactivateGroup;
  nsCOMPtr<mozIStorageStatement>  mStatement_FindClient;
  nsCOMPtr<mozIStorageStatement>  mStatement_FindClientByNamespace;
  nsCOMPtr<mozIStorageStatement>  mStatement_EnumerateGroups;
  nsCOMPtr<mozIStorageStatement>  mStatement_EnumerateGroupsTimeOrder;

  nsCOMPtr<nsIFile>               mBaseDirectory;
  nsCOMPtr<nsIFile>               mCacheDirectory;
  PRUint32                        mCacheCapacity; // in bytes
  PRInt32                         mDeltaCounter;
  bool                            mAutoShutdown;

  nsInterfaceHashtable<nsCStringHashKey, nsIWeakReference> mCaches;
  nsClassHashtable<nsCStringHashKey, nsCString> mActiveCachesByGroup;
  nsTHashtable<nsCStringHashKey> mActiveCaches;

  nsCOMPtr<nsIThread> mInitThread;
};

#endif // nsOfflineCacheDevice_h__
