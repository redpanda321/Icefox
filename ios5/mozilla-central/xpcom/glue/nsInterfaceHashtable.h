/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsInterfaceHashtable_h__
#define nsInterfaceHashtable_h__

#include "nsBaseHashtable.h"
#include "nsHashKeys.h"
#include "nsCOMPtr.h"

/**
 * templated hashtable class maps keys to interface pointers.
 * See nsBaseHashtable for complete declaration.
 * @param KeyClass a wrapper-class for the hashtable key, see nsHashKeys.h
 *   for a complete specification.
 * @param Interface the interface-type being wrapped
 * @see nsDataHashtable, nsClassHashtable
 */
template<class KeyClass,class Interface>
class nsInterfaceHashtable :
  public nsBaseHashtable< KeyClass, nsCOMPtr<Interface> , Interface* >
{
public:
  typedef typename KeyClass::KeyType KeyType;
  typedef Interface* UserDataType;
  typedef nsBaseHashtable< KeyClass, nsCOMPtr<Interface> , Interface* >
          base_type;

  /**
   * @copydoc nsBaseHashtable::Get
   * @param pData This is an XPCOM getter, so pData is already_addrefed.
   *   If the key doesn't exist, pData will be set to nsnull.
   */
  bool Get(KeyType aKey, UserDataType* pData NS_OUTPARAM) const;

  /**
   * @copydoc nsBaseHashtable::Get
   */
  already_AddRefed<Interface> Get(KeyType aKey) const;

  /**
   * Gets a weak reference to the hashtable entry.
   * @param aFound If not nsnull, will be set to true if the entry is found,
   *               to false otherwise.
   * @return The entry, or nsnull if not found. Do not release this pointer!
   */
  Interface* GetWeak(KeyType aKey, bool* aFound = nsnull) const;
};

/**
 * Thread-safe version of nsInterfaceHashtable
 * @param KeyClass a wrapper-class for the hashtable key, see nsHashKeys.h
 *   for a complete specification.
 * @param Interface the interface-type being wrapped
 */
template<class KeyClass,class Interface>
class nsInterfaceHashtableMT :
  public nsBaseHashtableMT< KeyClass, nsCOMPtr<Interface> , Interface* >
{
public:
  typedef typename KeyClass::KeyType KeyType;
  typedef Interface* UserDataType;
  typedef nsBaseHashtableMT< KeyClass, nsCOMPtr<Interface> , Interface* >
          base_type;

  /**
   * @copydoc nsBaseHashtable::Get
   * @param pData This is an XPCOM getter, so pData is already_addrefed.
   *   If the key doesn't exist, pData will be set to nsnull.
   */
  bool Get(KeyType aKey, UserDataType* pData NS_OUTPARAM) const;

  // GetWeak does not make sense on a multi-threaded hashtable, where another
  // thread may remove the entry (and hence release it) as soon as GetWeak
  // returns
};


//
// nsInterfaceHashtable definitions
//

template<class KeyClass,class Interface>
bool
nsInterfaceHashtable<KeyClass,Interface>::Get
  (KeyType aKey, UserDataType* pInterface) const
{
  typename base_type::EntryType* ent = this->GetEntry(aKey);

  if (ent)
  {
    if (pInterface)
    {
      *pInterface = ent->mData;

      NS_IF_ADDREF(*pInterface);
    }

    return true;
  }

  // if the key doesn't exist, set *pInterface to null
  // so that it is a valid XPCOM getter
  if (pInterface)
    *pInterface = nsnull;

  return false;
}

template<class KeyClass, class Interface>
already_AddRefed<Interface>
nsInterfaceHashtable<KeyClass,Interface>::Get(KeyType aKey) const
{
  typename base_type::EntryType* ent = this->GetEntry(aKey);
  if (!ent)
    return NULL;

  nsCOMPtr<Interface> copy = ent->mData;
  return copy.forget();
}

template<class KeyClass,class Interface>
Interface*
nsInterfaceHashtable<KeyClass,Interface>::GetWeak
  (KeyType aKey, bool* aFound) const
{
  typename base_type::EntryType* ent = this->GetEntry(aKey);

  if (ent)
  {
    if (aFound)
      *aFound = true;

    return ent->mData;
  }

  // Key does not exist, return nsnull and set aFound to false
  if (aFound)
    *aFound = false;
  return nsnull;
}

//
// nsInterfaceHashtableMT definitions
//

template<class KeyClass,class Interface>
bool
nsInterfaceHashtableMT<KeyClass,Interface>::Get
  (KeyType aKey, UserDataType* pInterface) const
{
  PR_Lock(this->mLock);

  typename base_type::EntryType* ent = this->GetEntry(aKey);

  if (ent)
  {
    if (pInterface)
    {
      *pInterface = ent->mData;

      NS_IF_ADDREF(*pInterface);
    }

    PR_Unlock(this->mLock);

    return true;
  }

  // if the key doesn't exist, set *pInterface to null
  // so that it is a valid XPCOM getter
  if (pInterface)
    *pInterface = nsnull;

  PR_Unlock(this->mLock);

  return false;
}

#endif // nsInterfaceHashtable_h__
