/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDOMStorageMemoryDB_h___
#define nsDOMStorageMemoryDB_h___

#include "nscore.h"
#include "nsDOMStorageBaseDB.h"
#include "nsClassHashtable.h"
#include "nsDataHashtable.h"

class nsDOMStoragePersistentDB;

class nsDOMStorageMemoryDB : public nsDOMStorageBaseDB
{
public:
  nsDOMStorageMemoryDB() : mPreloading(false) {}
  ~nsDOMStorageMemoryDB() {}

  class nsInMemoryItem
  {
  public:
    bool mSecure;
    nsString mValue;
  };

  typedef nsClassHashtable<nsStringHashKey, nsInMemoryItem> nsStorageItemsTable;

  class nsInMemoryStorage
  {
  public:
    nsStorageItemsTable mTable;
    int32_t mUsageDelta;

    nsInMemoryStorage() : mUsageDelta(0) {}
  };

  /**
   * @param aPreloadDB
   *    If non-null, data for a domain/origin will be preloaded from
   *    the provided database. Used for session-only cookies mode to
   *    provide existing data from the persistent database.
   */
  nsresult
  Init(nsDOMStoragePersistentDB* aPreloadDB = nullptr);

  /**
   *
   */
  nsresult
  GetItemsTable(DOMStorageImpl* aStorage,
                nsInMemoryStorage** aMemoryStorage);

  /**
   * Retrieve a list of all the keys associated with a particular domain.
   */
  nsresult
  GetAllKeys(DOMStorageImpl* aStorage,
             nsTHashtable<nsSessionStorageEntry>* aKeys);

  /**
   * Retrieve a value and secure flag for a key from storage.
   *
   * @throws NS_ERROR_DOM_NOT_FOUND_ERR if key not found
   */
  nsresult
  GetKeyValue(DOMStorageImpl* aStorage,
              const nsAString& aKey,
              nsAString& aValue,
              bool* aSecure);

  /**
   * Set the value and secure flag for a key in storage.
   */
  nsresult
  SetKey(DOMStorageImpl* aStorage,
         const nsAString& aKey,
         const nsAString& aValue,
         bool aSecure);

  /**
   * Set the secure flag for a key in storage. Does nothing if the key was
   * not found.
   */
  nsresult
  SetSecure(DOMStorageImpl* aStorage,
            const nsAString& aKey,
            const bool aSecure);

  /**
   * Removes a key from storage.
   */
  nsresult
  RemoveKey(DOMStorageImpl* aStorage,
            const nsAString& aKey);

  /**
    * Remove all keys belonging to this storage.
    */
  nsresult
  ClearStorage(DOMStorageImpl* aStorage);

  /**
   * If we have changed the persistent storage, drop any potential session storages
   */
  nsresult
  DropStorage(DOMStorageImpl* aStorage);

  /**
   * Removes all keys added by a given domain.
   */
  nsresult
  RemoveOwner(const nsACString& aOwner);

  /**
   * Removes all keys from storage. Used when clearing storage.
   */
  nsresult
  RemoveAll();

  /**
    * Returns usage for a storage using its GetQuotaDBKey() as a key.
    */
  nsresult
  GetUsage(DOMStorageImpl* aStorage, int32_t *aUsage);

  /**
    * Returns usage of the domain and optionaly by any subdomain.
    */
  nsresult
  GetUsage(const nsACString& aDomain, int32_t *aUsage);

protected:

  nsClassHashtable<nsCStringHashKey, nsInMemoryStorage> mData;
  nsDOMStoragePersistentDB* mPreloadDB;
  bool mPreloading;

  nsresult
  GetUsageInternal(const nsACString& aQuotaDBKey, int32_t *aUsage);
};

#endif
