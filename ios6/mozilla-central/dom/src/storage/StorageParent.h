/* -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 8 -*- */
/* vim: set sw=4 ts=8 et tw=80 ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_StorageParent_h
#define mozilla_dom_StorageParent_h

#include "mozilla/dom/PStorageParent.h"
#include "nsDOMStorage.h"

namespace mozilla {
namespace dom {

class StorageConstructData;

class StorageParent : public PStorageParent
{
public:
  StorageParent(const StorageConstructData& aData);

private:
  bool RecvGetKeys(const bool& aCallerSecure, InfallibleTArray<nsString>* aKeys);
  bool RecvGetLength(const bool& aCallerSecure, const bool& aSessionOnly,
                     uint32_t* aLength, nsresult* rv);
  bool RecvGetKey(const bool& aCallerSecure, const bool& aSessionOnly,
                  const uint32_t& aIndex,nsString* aKey, nsresult* rv);
  bool RecvGetValue(const bool& aCallerSecure, const bool& aSessionOnly,
                    const nsString& aKey, StorageItem* aItem, nsresult* rv);
  bool RecvSetValue(const bool& aCallerSecure, const bool& aSessionOnly,
                    const nsString& aKey, const nsString& aData,
                    nsString* aOldValue, nsresult* rv);
  bool RecvRemoveValue(const bool& aCallerSecure, const bool& aSessionOnly,
                       const nsString& aKey, nsString* aOldData, nsresult* rv);
  bool RecvClear(const bool& aCallerSecure, const bool& aSessionOnly,
                 int32_t* aOldCount, nsresult* rv);

  bool RecvGetDBValue(const nsString& aKey, nsString* aValue, bool* aSecure,
                      nsresult* rv);
  bool RecvSetDBValue(const nsString& aKey, const nsString& aValue,
                      const bool& aSecure, nsresult* rv);
  bool RecvSetSecure(const nsString& aKey, const bool& aSecure, nsresult* rv);

  bool RecvInit(const bool& aUseDB,
                const bool& aSessionOnly,
                const bool& aPrivate,
                const nsCString& aScopeDBKey,
                const nsCString& aQuotaDBKey,
                const uint32_t& aStorageType);

  bool RecvUpdatePrivateState(const bool& aEnabled);

  nsRefPtr<DOMStorageImpl> mStorage;
};

}
}

#endif
