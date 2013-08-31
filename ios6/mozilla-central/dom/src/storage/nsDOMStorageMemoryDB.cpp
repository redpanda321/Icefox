/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCOMPtr.h"
#include "nsError.h"
#include "nsDOMStorage.h"
#include "nsDOMStorageMemoryDB.h"
#include "nsNetUtil.h"

nsresult
nsDOMStorageMemoryDB::Init(nsDOMStoragePersistentDB* aPreloadDB)
{
  mData.Init(20);

  mPreloadDB = aPreloadDB;
  return NS_OK;
}

static PLDHashOperator
AllKeyEnum(nsSessionStorageEntry* aEntry, void* userArg)
{
  nsDOMStorageMemoryDB::nsStorageItemsTable *target =
      (nsDOMStorageMemoryDB::nsStorageItemsTable *)userArg;

  nsDOMStorageMemoryDB::nsInMemoryItem* item =
      new nsDOMStorageMemoryDB::nsInMemoryItem();
  if (!item)
    return PL_DHASH_STOP;

  aEntry->mItem->GetValue(item->mValue);
  nsresult rv = aEntry->mItem->GetSecure(&item->mSecure);
  if (NS_FAILED(rv))
    item->mSecure = false;

  target->Put(aEntry->GetKey(), item);
  return PL_DHASH_NEXT;
}

nsresult
nsDOMStorageMemoryDB::GetItemsTable(DOMStorageImpl* aStorage,
                                    nsInMemoryStorage** aMemoryStorage)
{
  if (mData.Get(aStorage->GetScopeDBKey(), aMemoryStorage))
    return NS_OK;

  *aMemoryStorage = nullptr;

  nsInMemoryStorage* storageData = new nsInMemoryStorage();
  if (!storageData)
    return NS_ERROR_OUT_OF_MEMORY;

  storageData->mTable.Init();

  if (mPreloadDB) {
    nsresult rv;

    nsTHashtable<nsSessionStorageEntry> keys;
    keys.Init();

    rv = mPreloadDB->GetAllKeys(aStorage, &keys);
    NS_ENSURE_SUCCESS(rv, rv);

    mPreloading = true;
    keys.EnumerateEntries(AllKeyEnum, &storageData->mTable);
    mPreloading = false;
  }

  mData.Put(aStorage->GetScopeDBKey(), storageData);
  *aMemoryStorage = storageData;

  return NS_OK;
}

struct GetAllKeysEnumStruc
{
  nsTHashtable<nsSessionStorageEntry>* mTarget;
  DOMStorageImpl* mStorage;
};

static PLDHashOperator
GetAllKeysEnum(const nsAString& keyname,
               nsDOMStorageMemoryDB::nsInMemoryItem* item,
               void *closure)
{
  GetAllKeysEnumStruc* struc = (GetAllKeysEnumStruc*)closure;

  nsSessionStorageEntry* entry = struc->mTarget->PutEntry(keyname);
  if (!entry)
    return PL_DHASH_STOP;

  entry->mItem = new nsDOMStorageItem(struc->mStorage,
                                      keyname,
                                      EmptyString(),
                                      item->mSecure);
  if (!entry->mItem)
    return PL_DHASH_STOP;

  return PL_DHASH_NEXT;
}

nsresult
nsDOMStorageMemoryDB::GetAllKeys(DOMStorageImpl* aStorage,
                                 nsTHashtable<nsSessionStorageEntry>* aKeys)
{
  nsresult rv;

  nsInMemoryStorage* storage;
  rv = GetItemsTable(aStorage, &storage);
  NS_ENSURE_SUCCESS(rv, rv);

  GetAllKeysEnumStruc struc;
  struc.mTarget = aKeys;
  struc.mStorage = aStorage;
  storage->mTable.EnumerateRead(GetAllKeysEnum, &struc);

  return NS_OK;
}

nsresult
nsDOMStorageMemoryDB::GetKeyValue(DOMStorageImpl* aStorage,
                                  const nsAString& aKey,
                                  nsAString& aValue,
                                  bool* aSecure)
{
  if (mPreloading) {
    NS_PRECONDITION(mPreloadDB, "Must have a preload DB set when preloading");
    return mPreloadDB->GetKeyValue(aStorage, aKey, aValue, aSecure);
  }

  nsresult rv;

  nsInMemoryStorage* storage;
  rv = GetItemsTable(aStorage, &storage);
  NS_ENSURE_SUCCESS(rv, rv);

  nsInMemoryItem* item;
  if (!storage->mTable.Get(aKey, &item))
    return NS_ERROR_DOM_NOT_FOUND_ERR;

  aValue = item->mValue;
  *aSecure = item->mSecure;
  return NS_OK;
}

nsresult
nsDOMStorageMemoryDB::SetKey(DOMStorageImpl* aStorage,
                             const nsAString& aKey,
                             const nsAString& aValue,
                             bool aSecure)
{
  nsresult rv;

  nsInMemoryStorage* storage;
  rv = GetItemsTable(aStorage, &storage);
  NS_ENSURE_SUCCESS(rv, rv);

  int32_t usage = 0;
  if (!aStorage->GetQuotaDBKey().IsEmpty()) {
    rv = GetUsage(aStorage, &usage);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  usage += aKey.Length() + aValue.Length();

  nsInMemoryItem* item;
  if (!storage->mTable.Get(aKey, &item)) {
    if (usage > GetQuota()) {
      return NS_ERROR_DOM_QUOTA_REACHED;
    }

    item = new nsInMemoryItem();
    if (!item)
      return NS_ERROR_OUT_OF_MEMORY;

    storage->mTable.Put(aKey, item);
    storage->mUsageDelta += aKey.Length();
  }
  else
  {
    if (!aSecure && item->mSecure)
      return NS_ERROR_DOM_SECURITY_ERR;
    usage -= aKey.Length() + item->mValue.Length();
    if (usage > GetQuota()) {
      return NS_ERROR_DOM_QUOTA_REACHED;
    }
  }

  storage->mUsageDelta += aValue.Length() - item->mValue.Length();

  item->mValue = aValue;
  item->mSecure = aSecure;

  MarkScopeDirty(aStorage);

  return NS_OK;
}

nsresult
nsDOMStorageMemoryDB::SetSecure(DOMStorageImpl* aStorage,
                                const nsAString& aKey,
                                const bool aSecure)
{
  nsresult rv;

  nsInMemoryStorage* storage;
  rv = GetItemsTable(aStorage, &storage);
  NS_ENSURE_SUCCESS(rv, rv);

  nsInMemoryItem* item;
  if (!storage->mTable.Get(aKey, &item))
    return NS_ERROR_DOM_NOT_FOUND_ERR;

  item->mSecure = aSecure;

  MarkScopeDirty(aStorage);

  return NS_OK;
}

nsresult
nsDOMStorageMemoryDB::RemoveKey(DOMStorageImpl* aStorage,
                                const nsAString& aKey)
{
  nsresult rv;

  nsInMemoryStorage* storage;
  rv = GetItemsTable(aStorage, &storage);
  NS_ENSURE_SUCCESS(rv, rv);

  nsInMemoryItem* item;
  if (!storage->mTable.Get(aKey, &item))
    return NS_ERROR_DOM_NOT_FOUND_ERR;

  storage->mUsageDelta -= aKey.Length() + item->mValue.Length();
  storage->mTable.Remove(aKey);

  MarkScopeDirty(aStorage);

  return NS_OK;
}

static PLDHashOperator
RemoveAllKeysEnum(const nsAString& keyname,
                  nsAutoPtr<nsDOMStorageMemoryDB::nsInMemoryItem>& item,
                  void *closure)
{
  nsDOMStorageMemoryDB::nsInMemoryStorage* storage =
      (nsDOMStorageMemoryDB::nsInMemoryStorage*)closure;

  storage->mUsageDelta -= keyname.Length() + item->mValue.Length();
  return PL_DHASH_REMOVE;
}

nsresult
nsDOMStorageMemoryDB::ClearStorage(DOMStorageImpl* aStorage)
{
  nsresult rv;

  nsInMemoryStorage* storage;
  rv = GetItemsTable(aStorage, &storage);
  NS_ENSURE_SUCCESS(rv, rv);

  storage->mTable.Enumerate(RemoveAllKeysEnum, storage);

  MarkScopeDirty(aStorage);

  return NS_OK;
}

nsresult
nsDOMStorageMemoryDB::DropStorage(DOMStorageImpl* aStorage)
{
  mData.Remove(aStorage->GetScopeDBKey());
  MarkScopeDirty(aStorage);
  return NS_OK;
}

struct RemoveOwnersStruc
{
  nsCString* mSubDomain;
  bool mMatch;
};

static PLDHashOperator
RemoveOwnersEnum(const nsACString& key,
                 nsAutoPtr<nsDOMStorageMemoryDB::nsInMemoryStorage>& storage,
                 void *closure)
{
  RemoveOwnersStruc* struc = (RemoveOwnersStruc*)closure;

  if (StringBeginsWith(key, *(struc->mSubDomain)) == struc->mMatch)
    return PL_DHASH_REMOVE;

  return PL_DHASH_NEXT;
}

nsresult
nsDOMStorageMemoryDB::RemoveOwner(const nsACString& aOwner)
{
  nsAutoCString subdomainsDBKey;
  nsDOMStorageDBWrapper::CreateReversedDomain(aOwner, subdomainsDBKey);

  RemoveOwnersStruc struc;
  struc.mSubDomain = &subdomainsDBKey;
  struc.mMatch = true;
  mData.Enumerate(RemoveOwnersEnum, &struc);

  MarkAllScopesDirty();

  return NS_OK;
}

nsresult
nsDOMStorageMemoryDB::RemoveAll()
{
  mData.Clear(); // XXX Check this releases all instances

  MarkAllScopesDirty();

  return NS_OK;
}

nsresult
nsDOMStorageMemoryDB::GetUsage(DOMStorageImpl* aStorage, int32_t *aUsage)
{
  return GetUsageInternal(aStorage->GetQuotaDBKey(), aUsage);
}

nsresult
nsDOMStorageMemoryDB::GetUsage(const nsACString& aDomain,
                               int32_t *aUsage)
{
  nsresult rv;

  nsAutoCString quotaDBKey;
  rv = nsDOMStorageDBWrapper::CreateQuotaDBKey(aDomain, quotaDBKey);
  NS_ENSURE_SUCCESS(rv, rv);

  return GetUsageInternal(quotaDBKey, aUsage);
}

struct GetUsageEnumStruc
{
  int32_t mUsage;
  nsCString mSubdomain;
};

static PLDHashOperator
GetUsageEnum(const nsACString& key,
             nsDOMStorageMemoryDB::nsInMemoryStorage* storageData,
             void *closure)
{
  GetUsageEnumStruc* struc = (GetUsageEnumStruc*)closure;

  if (StringBeginsWith(key, struc->mSubdomain)) {
    struc->mUsage += storageData->mUsageDelta;
  }

  return PL_DHASH_NEXT;
}

nsresult
nsDOMStorageMemoryDB::GetUsageInternal(const nsACString& aQuotaDBKey,
                                       int32_t *aUsage)
{
  GetUsageEnumStruc struc;
  struc.mUsage = 0;
  struc.mSubdomain = aQuotaDBKey;

  if (mPreloadDB) {
    nsresult rv;

    rv = mPreloadDB->GetUsageInternal(aQuotaDBKey, &struc.mUsage);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  mData.EnumerateRead(GetUsageEnum, &struc);

  *aUsage = struc.mUsage;
  return NS_OK;
}
