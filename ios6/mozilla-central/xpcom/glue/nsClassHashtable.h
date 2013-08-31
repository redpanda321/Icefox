/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsClassHashtable_h__
#define nsClassHashtable_h__

#include "nsBaseHashtable.h"
#include "nsHashKeys.h"
#include "nsAutoPtr.h"

/**
 * templated hashtable class maps keys to C++ object pointers.
 * See nsBaseHashtable for complete declaration.
 * @param KeyClass a wrapper-class for the hashtable key, see nsHashKeys.h
 *   for a complete specification.
 * @param Class the class-type being wrapped
 * @see nsInterfaceHashtable, nsClassHashtable
 */
template<class KeyClass,class T>
class nsClassHashtable :
  public nsBaseHashtable< KeyClass, nsAutoPtr<T>, T* >
{
public:
  typedef typename KeyClass::KeyType KeyType;
  typedef T* UserDataType;
  typedef nsBaseHashtable< KeyClass, nsAutoPtr<T>, T* > base_type;

  /**
   * @copydoc nsBaseHashtable::Get
   * @param pData if the key doesn't exist, pData will be set to nullptr.
   */
  bool Get(KeyType aKey, UserDataType* pData) const;

  /**
   * @copydoc nsBaseHashtable::Get
   * @returns NULL if the key is not present.
   */
  UserDataType Get(KeyType aKey) const;

  /**
   * Remove the entry for the given key from the hashtable and return it in
   * aOut.  If the key is not in the hashtable, aOut's pointer is set to NULL.
   *
   * Normally, an entry is deleted when it's removed from an nsClassHashtable,
   * but this function transfers ownership of the entry back to the caller
   * through aOut -- the entry will be deleted when aOut goes out of scope.
   *
   * @param aKey the key to get and remove from the hashtable
   */
  void RemoveAndForget(KeyType aKey, nsAutoPtr<T> &aOut);
};


/**
 * Thread-safe version of nsClassHashtable
 * @param KeyClass a wrapper-class for the hashtable key, see nsHashKeys.h
 *   for a complete specification.
 * @param Class the class-type being wrapped
 * @see nsInterfaceHashtable, nsClassHashtable
 */
template<class KeyClass,class T>
class nsClassHashtableMT :
  public nsBaseHashtableMT< KeyClass, nsAutoPtr<T>, T* >
{
public:
  typedef typename KeyClass::KeyType KeyType;
  typedef T* UserDataType;
  typedef nsBaseHashtableMT< KeyClass, nsAutoPtr<T>, T* > base_type;

  /**
   * @copydoc nsBaseHashtable::Get
   * @param pData if the key doesn't exist, pData will be set to nullptr.
   */
  bool Get(KeyType aKey, UserDataType* pData) const;
};


//
// nsClassHashtable definitions
//

template<class KeyClass,class T>
bool
nsClassHashtable<KeyClass,T>::Get(KeyType aKey, T** retVal) const
{
  typename base_type::EntryType* ent = this->GetEntry(aKey);

  if (ent)
  {
    if (retVal)
      *retVal = ent->mData;

    return true;
  }

  if (retVal)
    *retVal = nullptr;

  return false;
}

template<class KeyClass,class T>
T*
nsClassHashtable<KeyClass,T>::Get(KeyType aKey) const
{
  typename base_type::EntryType* ent = this->GetEntry(aKey);

  if (!ent)
    return NULL;

  return ent->mData;
}

template<class KeyClass,class T>
void
nsClassHashtable<KeyClass,T>::RemoveAndForget(KeyType aKey, nsAutoPtr<T> &aOut)
{
  aOut = nullptr;
  nsAutoPtr<T> ptr;

  typename base_type::EntryType *ent = this->GetEntry(aKey);
  if (!ent)
    return;

  // Transfer ownership from ent->mData into aOut.
  aOut = ent->mData;

  this->Remove(aKey);
}


//
// nsClassHashtableMT definitions
//

template<class KeyClass,class T>
bool
nsClassHashtableMT<KeyClass,T>::Get(KeyType aKey, T** retVal) const
{
  PR_Lock(this->mLock);

  typename base_type::EntryType* ent = this->GetEntry(aKey);

  if (ent)
  {
    if (retVal)
      *retVal = ent->mData;

    PR_Unlock(this->mLock);

    return true;
  }

  if (retVal)
    *retVal = nullptr;

  PR_Unlock(this->mLock);

  return false;
}

#endif // nsClassHashtable_h__
