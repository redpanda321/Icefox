/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsBaseHashtable_h__
#define nsBaseHashtable_h__

#include "nsTHashtable.h"
#include "prlock.h"
#include "nsDebug.h"

template<class KeyClass,class DataType,class UserDataType>
class nsBaseHashtable; // forward declaration

/**
 * the private nsTHashtable::EntryType class used by nsBaseHashtable
 * @see nsTHashtable for the specification of this class
 * @see nsBaseHashtable for template parameters
 */
template<class KeyClass,class DataType>
class nsBaseHashtableET : public KeyClass
{
public:
  DataType mData;
  friend class nsTHashtable< nsBaseHashtableET<KeyClass,DataType> >;

private:
  typedef typename KeyClass::KeyType KeyType;
  typedef typename KeyClass::KeyTypePointer KeyTypePointer;
  
  nsBaseHashtableET(KeyTypePointer aKey);
  nsBaseHashtableET(nsBaseHashtableET<KeyClass,DataType>& toCopy);
  ~nsBaseHashtableET();
};

/**
 * templated hashtable for simple data types
 * This class manages simple data types that do not need construction or
 * destruction.
 *
 * @param KeyClass a wrapper-class for the hashtable key, see nsHashKeys.h
 *   for a complete specification.
 * @param DataType the datatype stored in the hashtable,
 *   for example, uint32_t or nsCOMPtr.  If UserDataType is not the same,
 *   DataType must implicitly cast to UserDataType
 * @param UserDataType the user sees, for example uint32_t or nsISupports*
 */
template<class KeyClass,class DataType,class UserDataType>
class nsBaseHashtable :
  protected nsTHashtable< nsBaseHashtableET<KeyClass,DataType> >
{
  typedef mozilla::fallible_t fallible_t;

public:
  typedef typename KeyClass::KeyType KeyType;
  typedef nsBaseHashtableET<KeyClass,DataType> EntryType;

  // default constructor+destructor are fine

  /**
   * Initialize the object.
   * @param initSize the initial number of buckets in the hashtable,
   *        default 16
   * locking on all class methods
   * @return    true if the object was initialized properly.
   */
  void Init(uint32_t initSize = PL_DHASH_MIN_SIZE)
  { nsTHashtable<EntryType>::Init(initSize); }

  bool Init(const fallible_t&) NS_WARN_UNUSED_RESULT
  { return Init(PL_DHASH_MIN_SIZE, fallible_t()); }

  bool Init(uint32_t initSize, const fallible_t&) NS_WARN_UNUSED_RESULT
  { return nsTHashtable<EntryType>::Init(initSize, fallible_t()); }



  /**
   * Check whether the table has been initialized.
   * This function is especially useful for static hashtables.
   * @return true if the table has been initialized.
   */
  bool IsInitialized() const { return !!this->mTable.entrySize; }

  /**
   * Return the number of entries in the table.
   * @return    number of entries
   */
  uint32_t Count() const
  { return nsTHashtable<EntryType>::Count(); }

  /**
   * retrieve the value for a key.
   * @param aKey the key to retreive
   * @param pData data associated with this key will be placed at this
   *   pointer.  If you only need to check if the key exists, pData
   *   may be null.
   * @return true if the key exists. If key does not exist, pData is not
   *   modified.
   */
  bool Get(KeyType aKey, UserDataType* pData) const
  {
    EntryType* ent = this->GetEntry(aKey);

    if (!ent)
      return false;

    if (pData)
      *pData = ent->mData;

    return true;
  }

  /**
   * For pointer types, get the value, returning nullptr if the entry is not
   * present in the table.
   *
   * @param aKey the key to retrieve
   * @return The found value, or nullptr if no entry was found with the given key.
   * @note If nullptr values are stored in the table, it is not possible to
   *       distinguish between a nullptr value and a missing entry.
   */
  UserDataType Get(KeyType aKey) const
  {
    EntryType* ent = this->GetEntry(aKey);
    if (!ent)
      return 0;

    return ent->mData;
  }

  /**
   * put a new value for the associated key
   * @param aKey the key to put
   * @param aData the new data
   * @return always true, unless memory allocation failed
   */
  void Put(KeyType aKey, const UserDataType& aData)
  {
    if (!Put(aKey, aData, fallible_t()))
      NS_RUNTIMEABORT("OOM");
  }

  bool Put(KeyType aKey, const UserDataType& aData, const fallible_t&) NS_WARN_UNUSED_RESULT
  {
    EntryType* ent = this->PutEntry(aKey);

    if (!ent)
      return false;

    ent->mData = aData;

    return true;
  }

  /**
   * remove the data for the associated key
   * @param aKey the key to remove from the hashtable
   */
  void Remove(KeyType aKey) { this->RemoveEntry(aKey); }

  /**
   * function type provided by the application for enumeration.
   * @param aKey the key being enumerated
   * @param aData data being enumerated
   * @parm userArg passed unchanged from Enumerate
   * @return either
   *   @link PLDHashOperator::PL_DHASH_NEXT PL_DHASH_NEXT @endlink or
   *   @link PLDHashOperator::PL_DHASH_STOP PL_DHASH_STOP @endlink
   */
  typedef PLDHashOperator
    (* EnumReadFunction)(KeyType      aKey,
                         UserDataType aData,
                         void*        userArg);

  /**
   * enumerate entries in the hashtable, without allowing changes
   * @param enumFunc enumeration callback
   * @param userArg passed unchanged to the EnumReadFunction
   */
  uint32_t EnumerateRead(EnumReadFunction enumFunc, void* userArg) const
  {
    NS_ASSERTION(this->mTable.entrySize,
                 "nsBaseHashtable was not initialized properly.");

    s_EnumReadArgs enumData = { enumFunc, userArg };
    return PL_DHashTableEnumerate(const_cast<PLDHashTable*>(&this->mTable),
                                  s_EnumReadStub,
                                  &enumData);
  }

  /**
   * function type provided by the application for enumeration.
   * @param aKey the key being enumerated
   * @param aData Reference to data being enumerated, may be altered. e.g. for
   *        nsInterfaceHashtable this is an nsCOMPtr reference...
   * @parm userArg passed unchanged from Enumerate
   * @return bitflag combination of
   *   @link PLDHashOperator::PL_DHASH_REMOVE @endlink,
   *   @link PLDHashOperator::PL_DHASH_NEXT PL_DHASH_NEXT @endlink, or
   *   @link PLDHashOperator::PL_DHASH_STOP PL_DHASH_STOP @endlink
   */
  typedef PLDHashOperator
    (* EnumFunction)(KeyType       aKey,
                     DataType&     aData,
                     void*         userArg);

  /**
   * enumerate entries in the hashtable, allowing changes. This
   * functions write-locks the hashtable.
   * @param enumFunc enumeration callback
   * @param userArg passed unchanged to the EnumFunction
   */
  uint32_t Enumerate(EnumFunction enumFunc, void* userArg)
  {
    NS_ASSERTION(this->mTable.entrySize,
                 "nsBaseHashtable was not initialized properly.");

    s_EnumArgs enumData = { enumFunc, userArg };
    return PL_DHashTableEnumerate(&this->mTable,
                                  s_EnumStub,
                                  &enumData);
  }

  /**
   * reset the hashtable, removing all entries
   */
  void Clear() { nsTHashtable<EntryType>::Clear(); }

  /**
   * client must provide a SizeOfEntryExcludingThisFun function for
   *   SizeOfExcludingThis.
   * @param     aKey the key being enumerated
   * @param     aData Reference to data being enumerated.
   * @param     mallocSizeOf the function used to measure heap-allocated blocks
   * @param     userArg passed unchanged from SizeOf{In,Ex}cludingThis
   * @return    summed size of the things pointed to by the entries
   */
  typedef size_t
    (* SizeOfEntryExcludingThisFun)(KeyType           aKey,
                                    const DataType    &aData,
                                    nsMallocSizeOfFun mallocSizeOf,
                                    void*             userArg);

  /**
   * Measure the size of the table's entry storage and the table itself.
   * If |sizeOfEntryExcludingThis| is non-nullptr, measure the size of things
   * pointed to by entries.
   *
   * @param    sizeOfEntryExcludingThis
   *           the <code>SizeOfEntryExcludingThisFun</code> function to call
   * @param    mallocSizeOf the function used to meeasure heap-allocated blocks
   * @param    userArg a point to pass to the
   *           <code>SizeOfEntryExcludingThisFun</code> function
   * @return   the summed size of the entries, the table, and the table's storage
   */
  size_t SizeOfIncludingThis(SizeOfEntryExcludingThisFun sizeOfEntryExcludingThis,
                             nsMallocSizeOfFun mallocSizeOf, void *userArg = nullptr)
  {
    return mallocSizeOf(this) + this->SizeOfExcludingThis(sizeOfEntryExcludingThis,
                                                          mallocSizeOf, userArg);
  }

  /**
   * Measure the size of the table's entry storage, and if
   * |sizeOfEntryExcludingThis| is non-nullptr, measure the size of things pointed
   * to by entries.
   * 
   * @param     sizeOfEntryExcludingThis the
   *            <code>SizeOfEntryExcludingThisFun</code> function to call
   * @param     mallocSizeOf the function used to measure heap-allocated blocks
   * @param     userArg a pointer to pass to the
   *            <code>SizeOfEntryExcludingThisFun</code> function
   * @return    the summed size of all the entries
   */
  size_t SizeOfExcludingThis(SizeOfEntryExcludingThisFun sizeOfEntryExcludingThis,
                             nsMallocSizeOfFun mallocSizeOf, void *userArg = nullptr) const
  {
    if (!IsInitialized()) {
      return 0;
    }
    if (sizeOfEntryExcludingThis) {
      s_SizeOfArgs args = { sizeOfEntryExcludingThis, userArg };
      return PL_DHashTableSizeOfExcludingThis(&this->mTable, s_SizeOfStub,
                                              mallocSizeOf, &args);
    }
    return PL_DHashTableSizeOfExcludingThis(&this->mTable, NULL, mallocSizeOf);
  }

protected:
  /**
   * used internally during EnumerateRead.  Allocated on the stack.
   * @param func the enumerator passed to EnumerateRead
   * @param userArg the userArg passed to EnumerateRead
   */
  struct s_EnumReadArgs
  {
    EnumReadFunction func;
    void* userArg;
  };

  static PLDHashOperator s_EnumReadStub(PLDHashTable    *table,
                                        PLDHashEntryHdr *hdr,
                                        uint32_t         number,
                                        void            *arg);

  struct s_EnumArgs
  {
    EnumFunction func;
    void* userArg;
  };

  static PLDHashOperator s_EnumStub(PLDHashTable      *table,
                                    PLDHashEntryHdr   *hdr,
                                    uint32_t           number,
                                    void              *arg);

  struct s_SizeOfArgs
  {
    SizeOfEntryExcludingThisFun func;
    void* userArg;
  };
  
  static size_t s_SizeOfStub(PLDHashEntryHdr *entry,
                             nsMallocSizeOfFun mallocSizeOf,
                             void *arg);
};

/**
 * This class is a thread-safe version of nsBaseHashtable. It only exposes
 * an infallible API.
 */
template<class KeyClass,class DataType,class UserDataType>
class nsBaseHashtableMT :
  protected nsBaseHashtable<KeyClass,DataType,UserDataType>
{
public:
  typedef typename
    nsBaseHashtable<KeyClass,DataType,UserDataType>::EntryType EntryType;
  typedef typename
    nsBaseHashtable<KeyClass,DataType,UserDataType>::KeyType KeyType;
  typedef typename
    nsBaseHashtable<KeyClass,DataType,UserDataType>::EnumFunction EnumFunction;
  typedef typename
    nsBaseHashtable<KeyClass,DataType,UserDataType>::EnumReadFunction EnumReadFunction;

  nsBaseHashtableMT() : mLock(nullptr) { }
  ~nsBaseHashtableMT();

  void Init(uint32_t initSize = PL_DHASH_MIN_SIZE);
  bool IsInitialized() const { return mLock != nullptr; }
  uint32_t Count() const;
  bool Get(KeyType aKey, UserDataType* pData) const;
  void Put(KeyType aKey, UserDataType aData);
  void Remove(KeyType aKey);

  uint32_t EnumerateRead(EnumReadFunction enumFunc, void* userArg) const;
  uint32_t Enumerate(EnumFunction enumFunc, void* userArg);
  void Clear();

protected:
  PRLock* mLock;
};
  

//
// nsBaseHashtableET definitions
//

template<class KeyClass,class DataType>
nsBaseHashtableET<KeyClass,DataType>::nsBaseHashtableET(KeyTypePointer aKey) :
  KeyClass(aKey)
{ }

template<class KeyClass,class DataType>
nsBaseHashtableET<KeyClass,DataType>::nsBaseHashtableET
  (nsBaseHashtableET<KeyClass,DataType>& toCopy) :
  KeyClass(toCopy),
  mData(toCopy.mData)
{ }

template<class KeyClass,class DataType>
nsBaseHashtableET<KeyClass,DataType>::~nsBaseHashtableET()
{ }


//
// nsBaseHashtable definitions
//

template<class KeyClass,class DataType,class UserDataType>
PLDHashOperator
nsBaseHashtable<KeyClass,DataType,UserDataType>::s_EnumReadStub
  (PLDHashTable *table, PLDHashEntryHdr *hdr, uint32_t number, void* arg)
{
  EntryType* ent = static_cast<EntryType*>(hdr);
  s_EnumReadArgs* eargs = (s_EnumReadArgs*) arg;

  PLDHashOperator res = (eargs->func)(ent->GetKey(), ent->mData, eargs->userArg);

  NS_ASSERTION( !(res & PL_DHASH_REMOVE ),
                "PL_DHASH_REMOVE return during const enumeration; ignoring.");

  if (res & PL_DHASH_STOP)
    return PL_DHASH_STOP;

  return PL_DHASH_NEXT;
}

template<class KeyClass,class DataType,class UserDataType>
PLDHashOperator
nsBaseHashtable<KeyClass,DataType,UserDataType>::s_EnumStub
  (PLDHashTable *table, PLDHashEntryHdr *hdr, uint32_t number, void* arg)
{
  EntryType* ent = static_cast<EntryType*>(hdr);
  s_EnumArgs* eargs = (s_EnumArgs*) arg;

  return (eargs->func)(ent->GetKey(), ent->mData, eargs->userArg);
}

template<class KeyClass,class DataType,class UserDataType>
size_t
nsBaseHashtable<KeyClass,DataType,UserDataType>::s_SizeOfStub
  (PLDHashEntryHdr *hdr, nsMallocSizeOfFun mallocSizeOf, void *arg)
{
  EntryType* ent = static_cast<EntryType*>(hdr);
  s_SizeOfArgs* eargs = static_cast<s_SizeOfArgs*>(arg);

  return (eargs->func)(ent->GetKey(), ent->mData, mallocSizeOf, eargs->userArg);
}

//
// nsBaseHashtableMT  definitions
//

template<class KeyClass,class DataType,class UserDataType>
nsBaseHashtableMT<KeyClass,DataType,UserDataType>::~nsBaseHashtableMT()
{
  if (this->mLock)
    PR_DestroyLock(this->mLock);
}

template<class KeyClass,class DataType,class UserDataType>
void
nsBaseHashtableMT<KeyClass,DataType,UserDataType>::Init(uint32_t initSize)
{
  if (!nsTHashtable<EntryType>::IsInitialized())
    nsTHashtable<EntryType>::Init(initSize);

  this->mLock = PR_NewLock();
  if (!this->mLock)
    NS_RUNTIMEABORT("OOM");
}

template<class KeyClass,class DataType,class UserDataType>
uint32_t
nsBaseHashtableMT<KeyClass,DataType,UserDataType>::Count() const
{
  PR_Lock(this->mLock);
  uint32_t count = nsTHashtable<EntryType>::Count();
  PR_Unlock(this->mLock);

  return count;
}

template<class KeyClass,class DataType,class UserDataType>
bool
nsBaseHashtableMT<KeyClass,DataType,UserDataType>::Get(KeyType       aKey,
                                                           UserDataType* pData) const
{
  PR_Lock(this->mLock);
  bool res =
    nsBaseHashtable<KeyClass,DataType,UserDataType>::Get(aKey, pData);
  PR_Unlock(this->mLock);

  return res;
}

template<class KeyClass,class DataType,class UserDataType>
void
nsBaseHashtableMT<KeyClass,DataType,UserDataType>::Put(KeyType      aKey,
                                                           UserDataType aData)
{
  PR_Lock(this->mLock);
  nsBaseHashtable<KeyClass,DataType,UserDataType>::Put(aKey, aData);
  PR_Unlock(this->mLock);
}

template<class KeyClass,class DataType,class UserDataType>
void
nsBaseHashtableMT<KeyClass,DataType,UserDataType>::Remove(KeyType aKey)
{
  PR_Lock(this->mLock);
  nsBaseHashtable<KeyClass,DataType,UserDataType>::Remove(aKey);
  PR_Unlock(this->mLock);
}

template<class KeyClass,class DataType,class UserDataType>
uint32_t
nsBaseHashtableMT<KeyClass,DataType,UserDataType>::EnumerateRead
  (EnumReadFunction fEnumCall, void* userArg) const
{
  PR_Lock(this->mLock);
  uint32_t count =
    nsBaseHashtable<KeyClass,DataType,UserDataType>::EnumerateRead(fEnumCall, userArg);
  PR_Unlock(this->mLock);

  return count;
}

template<class KeyClass,class DataType,class UserDataType>
uint32_t
nsBaseHashtableMT<KeyClass,DataType,UserDataType>::Enumerate
  (EnumFunction fEnumCall, void* userArg)
{
  PR_Lock(this->mLock);
  uint32_t count =
    nsBaseHashtable<KeyClass,DataType,UserDataType>::Enumerate(fEnumCall, userArg);
  PR_Unlock(this->mLock);

  return count;
}

template<class KeyClass,class DataType,class UserDataType>
void
nsBaseHashtableMT<KeyClass,DataType,UserDataType>::Clear()
{
  PR_Lock(this->mLock);
  nsBaseHashtable<KeyClass,DataType,UserDataType>::Clear();
  PR_Unlock(this->mLock);
}

#endif // nsBaseHashtable_h__
