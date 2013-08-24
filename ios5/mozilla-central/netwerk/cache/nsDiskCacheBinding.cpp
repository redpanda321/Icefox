/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <limits.h>

#include "nscore.h"
#include "nsDiskCacheBinding.h"
#include "nsCacheService.h"



/******************************************************************************
 *  static hash table callback functions
 *
 *****************************************************************************/
struct HashTableEntry : PLDHashEntryHdr {
    nsDiskCacheBinding *  mBinding;
};


static PLDHashNumber
HashKey( PLDHashTable *table, const void *key)
{
    return (PLDHashNumber) NS_PTR_TO_INT32(key);
}


static bool
MatchEntry(PLDHashTable *              /* table */,
            const PLDHashEntryHdr *       header,
            const void *                  key)
{
    HashTableEntry * hashEntry = (HashTableEntry *) header;
    return (hashEntry->mBinding->mRecord.HashNumber() == (PLDHashNumber) NS_PTR_TO_INT32(key));
}

static void
MoveEntry(PLDHashTable *           /* table */,
          const PLDHashEntryHdr *     src,
          PLDHashEntryHdr       *     dst)
{
    ((HashTableEntry *)dst)->mBinding = ((HashTableEntry *)src)->mBinding;
}


static void
ClearEntry(PLDHashTable *      /* table */,
           PLDHashEntryHdr *      header)
{
    ((HashTableEntry *)header)->mBinding = nsnull;
}


/******************************************************************************
 *  Utility Functions
 *****************************************************************************/
nsDiskCacheBinding *
GetCacheEntryBinding(nsCacheEntry * entry)
{
    return (nsDiskCacheBinding *) entry->Data();
}


/******************************************************************************
 *  nsDiskCacheBinding
 *****************************************************************************/

NS_IMPL_THREADSAFE_ISUPPORTS0(nsDiskCacheBinding)

nsDiskCacheBinding::nsDiskCacheBinding(nsCacheEntry* entry, nsDiskCacheRecord * record)
    :   mCacheEntry(entry)
    ,   mStreamIO(nsnull)
    ,   mDeactivateEvent(nsnull)
{
    NS_ASSERTION(record->ValidRecord(), "bad record");
    PR_INIT_CLIST(this);
    mRecord     = *record;
    mDoomed     = entry->IsDoomed();
    mGeneration = record->Generation();    // 0 == uninitialized, or data & meta using block files
}

nsDiskCacheBinding::~nsDiskCacheBinding()
{
    // Grab the cache lock since the binding is stored in nsCacheEntry::mData
    // and it is released using nsCacheService::ReleaseObject_Locked() which
    // releases the object outside the cache lock.
    nsCacheServiceAutoLock lock(LOCK_TELEM(NSDISKCACHEBINDING_DESTRUCTOR));

    NS_ASSERTION(PR_CLIST_IS_EMPTY(this), "binding deleted while still on list");
    if (!PR_CLIST_IS_EMPTY(this))
        PR_REMOVE_LINK(this);       // XXX why are we still on a list?
    
    // sever streamIO/binding link
    if (mStreamIO) {
        if (NS_FAILED(mStreamIO->ClearBinding()))
            nsCacheService::DoomEntry(mCacheEntry);
        NS_RELEASE(mStreamIO);
    }
}

nsresult
nsDiskCacheBinding::EnsureStreamIO()
{
    if (!mStreamIO) {
        mStreamIO = new nsDiskCacheStreamIO(this);
        if (!mStreamIO)  return NS_ERROR_OUT_OF_MEMORY;
        NS_ADDREF(mStreamIO);
    }
    return NS_OK;
}


/******************************************************************************
 *  nsDiskCacheBindery
 *
 *  Keeps track of bound disk cache entries to detect for collisions.
 *
 *****************************************************************************/

PLDHashTableOps nsDiskCacheBindery::ops =
{
    PL_DHashAllocTable,
    PL_DHashFreeTable,
    HashKey,
    MatchEntry,
    MoveEntry,
    ClearEntry,
    PL_DHashFinalizeStub
};


nsDiskCacheBindery::nsDiskCacheBindery()
    : initialized(false)
{
}


nsDiskCacheBindery::~nsDiskCacheBindery()
{
    Reset();
}


nsresult
nsDiskCacheBindery::Init()
{
    nsresult rv = NS_OK;
    initialized = PL_DHashTableInit(&table, &ops, nsnull, sizeof(HashTableEntry), 0);

    if (!initialized) rv = NS_ERROR_OUT_OF_MEMORY;
    
    return rv;
}

void
nsDiskCacheBindery::Reset()
{
    if (initialized) {
        PL_DHashTableFinish(&table);
        initialized = false;
    }
}


nsDiskCacheBinding *
nsDiskCacheBindery::CreateBinding(nsCacheEntry *       entry,
                                  nsDiskCacheRecord *  record)
{
    NS_ASSERTION(initialized, "nsDiskCacheBindery not initialized");
    nsCOMPtr<nsISupports> data = entry->Data();
    if (data) {
        NS_ERROR("cache entry already has bind data");
        return nsnull;
    }
    
    nsDiskCacheBinding * binding = new nsDiskCacheBinding(entry, record);
    if (!binding)  return nsnull;
        
    // give ownership of the binding to the entry
    entry->SetData(binding);
    
    // add binding to collision detection system
    nsresult rv = AddBinding(binding);
    if (NS_FAILED(rv)) {
        entry->SetData(nsnull);
        return nsnull;
    }

    return binding;
}


/**
 *  FindActiveEntry :  to find active colliding entry so we can doom it
 */
nsDiskCacheBinding *
nsDiskCacheBindery::FindActiveBinding(PRUint32  hashNumber)
{
    NS_ASSERTION(initialized, "nsDiskCacheBindery not initialized");
    // find hash entry for key
    HashTableEntry * hashEntry;
    hashEntry = (HashTableEntry *) PL_DHashTableOperate(&table, (void*) hashNumber, PL_DHASH_LOOKUP);
    if (PL_DHASH_ENTRY_IS_FREE(hashEntry)) return nsnull;
    
    // walk list looking for active entry
    NS_ASSERTION(hashEntry->mBinding, "hash entry left with no binding");
    nsDiskCacheBinding * binding = hashEntry->mBinding;    
    while (binding->mCacheEntry->IsDoomed()) {
        binding = (nsDiskCacheBinding *)PR_NEXT_LINK(binding);
        if (binding == hashEntry->mBinding)  return nsnull;
    }
    return binding;
}


/**
 *  AddBinding
 *
 *  Called from FindEntry() if we read an entry off of disk
 *      - it may already have a generation number
 *      - a generation number conflict is an error
 *
 *  Called from BindEntry()
 *      - a generation number needs to be assigned
 */
nsresult
nsDiskCacheBindery::AddBinding(nsDiskCacheBinding * binding)
{
    NS_ENSURE_ARG_POINTER(binding);
    NS_ASSERTION(initialized, "nsDiskCacheBindery not initialized");

    // find hash entry for key
    HashTableEntry * hashEntry;
    hashEntry = (HashTableEntry *) PL_DHashTableOperate(&table,
                                                        (void*) binding->mRecord.HashNumber(),
                                                        PL_DHASH_ADD);
    if (!hashEntry) return NS_ERROR_OUT_OF_MEMORY;
    
    if (hashEntry->mBinding == nsnull) {
        hashEntry->mBinding = binding;
        if (binding->mGeneration == 0)
            binding->mGeneration = 1;   // if generation uninitialized, set it to 1
            
        return NS_OK;
    }
    
    
    // insert binding in generation order
    nsDiskCacheBinding * p  = hashEntry->mBinding;
    bool     calcGeneration = (binding->mGeneration == 0);  // do we need to calculate generation?
    if (calcGeneration)  binding->mGeneration = 1;          // initialize to 1 if uninitialized
    while (1) {
    
        if (binding->mGeneration < p->mGeneration) {
            // here we are
            PR_INSERT_BEFORE(binding, p);
            if (hashEntry->mBinding == p)
                hashEntry->mBinding = binding;
            break;
        }
        
        if (binding->mGeneration == p->mGeneration) {
            if (calcGeneration)  ++binding->mGeneration;    // try the next generation
            else {
                NS_ERROR("### disk cache: generations collide!");
                return NS_ERROR_UNEXPECTED;
            }
        }

        p = (nsDiskCacheBinding *)PR_NEXT_LINK(p);
        if (p == hashEntry->mBinding) {
            // end of line: insert here or die
            p = (nsDiskCacheBinding *)PR_PREV_LINK(p);  // back up and check generation
            if (p->mGeneration == 255) {
                NS_WARNING("### disk cache: generation capacity at full");
                return NS_ERROR_UNEXPECTED;
            }
            PR_INSERT_BEFORE(binding, hashEntry->mBinding);
            break;
        }
    }
    return NS_OK;
}


/**
 *  RemoveBinding :  remove binding from collision detection on deactivation
 */
void
nsDiskCacheBindery::RemoveBinding(nsDiskCacheBinding * binding)
{
    NS_ASSERTION(initialized, "nsDiskCacheBindery not initialized");
    if (!initialized)   return;
    
    HashTableEntry * hashEntry;
    void *           key = (void *)binding->mRecord.HashNumber();

    hashEntry = (HashTableEntry*) PL_DHashTableOperate(&table,
                                                       (void*) key,
                                                       PL_DHASH_LOOKUP);
    if (!PL_DHASH_ENTRY_IS_BUSY(hashEntry)) {
        NS_WARNING("### disk cache: binding not in hashtable!");
        return;
    }
    
    if (binding == hashEntry->mBinding) {
        if (PR_CLIST_IS_EMPTY(binding)) {
            // remove this hash entry
            (void) PL_DHashTableOperate(&table,
                                        (void*) binding->mRecord.HashNumber(),
                                        PL_DHASH_REMOVE);
            return;
            
        } else {
            // promote next binding to head, and unlink this binding
            hashEntry->mBinding = (nsDiskCacheBinding *)PR_NEXT_LINK(binding);
        }
    }
    PR_REMOVE_AND_INIT_LINK(binding);
}


/**
 *  ActiveBinding : PLDHashTable enumerate function to verify active bindings
 */

PLDHashOperator
ActiveBinding(PLDHashTable *    table,
              PLDHashEntryHdr * hdr,
              PRUint32          number,
              void *            arg)
{
    nsDiskCacheBinding * binding = ((HashTableEntry *)hdr)->mBinding;
    NS_ASSERTION(binding, "### disk cache binding = nsnull!");
    
    nsDiskCacheBinding * head = binding;
    do {   
        if (binding->IsActive()) {
           *((bool *)arg) = true;
            return PL_DHASH_STOP;
        }

        binding = (nsDiskCacheBinding *)PR_NEXT_LINK(binding);
    } while (binding != head);

    return PL_DHASH_NEXT;
}


/**
 *  ActiveBindings : return true if any bindings have open descriptors
 */
bool
nsDiskCacheBindery::ActiveBindings()
{
    NS_ASSERTION(initialized, "nsDiskCacheBindery not initialized");
    if (!initialized) return false;

    bool    activeBinding = false;
    PL_DHashTableEnumerate(&table, ActiveBinding, &activeBinding);

    return activeBinding;
}
