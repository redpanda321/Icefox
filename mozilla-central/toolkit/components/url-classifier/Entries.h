//* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SBEntries_h__
#define SBEntries_h__

#include "nsTArray.h"
#include "nsString.h"
#include "nsICryptoHash.h"
#include "nsNetUtil.h"
#include "prlog.h"

extern PRLogModuleInfo *gUrlClassifierDbServiceLog;
#if defined(PR_LOGGING)
#define LOG(args) PR_LOG(gUrlClassifierDbServiceLog, PR_LOG_DEBUG, args)
#define LOG_ENABLED() PR_LOG_TEST(gUrlClassifierDbServiceLog, 4)
#else
#define LOG(args)
#define LOG_ENABLED() (false)
#endif

#if DEBUG
#include "plbase64.h"
#endif

namespace mozilla {
namespace safebrowsing {

#define PREFIX_SIZE   4
#define COMPLETE_SIZE 32

template <uint32_t S, class Comparator>
struct SafebrowsingHash
{
  static const uint32_t sHashSize = S;
  typedef SafebrowsingHash<S, Comparator> self_type;
  uint8_t buf[S];

  nsresult FromPlaintext(const nsACString& aPlainText, nsICryptoHash* aHash) {
    // From the protocol doc:
    // Each entry in the chunk is composed
    // of the SHA 256 hash of a suffix/prefix expression.

    nsresult rv = aHash->Init(nsICryptoHash::SHA256);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = aHash->Update
      (reinterpret_cast<const uint8_t*>(aPlainText.BeginReading()),
       aPlainText.Length());
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoCString hashed;
    rv = aHash->Finish(false, hashed);
    NS_ENSURE_SUCCESS(rv, rv);

    NS_ASSERTION(hashed.Length() >= sHashSize,
                 "not enough characters in the hash");

    memcpy(buf, hashed.BeginReading(), sHashSize);

    return NS_OK;
  }

  void Assign(const nsACString& aStr) {
    NS_ASSERTION(aStr.Length() >= sHashSize,
                 "string must be at least sHashSize characters long");
    memcpy(buf, aStr.BeginReading(), sHashSize);
  }

  int Compare(const self_type& aOther) const {
    return Comparator::Compare(buf, aOther.buf);
  }

  bool operator==(const self_type& aOther) const {
    return Comparator::Compare(buf, aOther.buf) == 0;
  }

  bool operator!=(const self_type& aOther) const {
    return Comparator::Compare(buf, aOther.buf) != 0;
  }

  bool operator<(const self_type& aOther) const {
    return Comparator::Compare(buf, aOther.buf) < 0;
  }

#ifdef DEBUG
  void ToString(nsACString& aStr) const {
    uint32_t len = ((sHashSize + 2) / 3) * 4;
    aStr.SetCapacity(len + 1);
    PL_Base64Encode((char*)buf, sHashSize, aStr.BeginWriting());
    aStr.BeginWriting()[len] = '\0';
  }
#endif
  uint32_t ToUint32() const {
      return *((uint32_t*)buf);
  }
  void FromUint32(uint32_t aHash) {
      *((uint32_t*)buf) = aHash;
  }
};

class PrefixComparator {
public:
  static int Compare(const uint8_t* a, const uint8_t* b) {
      uint32_t first = *((uint32_t*)a);
      uint32_t second = *((uint32_t*)b);
      if (first > second) {
          return 1;
      } else if (first == second) {
          return 0;
      } else {
          return -1;
      }
  }
};
typedef SafebrowsingHash<PREFIX_SIZE, PrefixComparator> Prefix;
typedef nsTArray<Prefix> PrefixArray;

class CompletionComparator {
public:
  static int Compare(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, COMPLETE_SIZE);
  }
};
typedef SafebrowsingHash<COMPLETE_SIZE, CompletionComparator> Completion;
typedef nsTArray<Completion> CompletionArray;

struct AddPrefix {
  Prefix prefix;
  uint32_t addChunk;

  AddPrefix() : addChunk(0) {}

  uint32_t Chunk() const { return addChunk; }
  const Prefix &PrefixHash() const { return prefix; }

  template<class T>
  int Compare(const T& other) const {
    int cmp = prefix.Compare(other.PrefixHash());
    if (cmp != 0) {
      return cmp;
    }
    return addChunk - other.addChunk;
  }
};

struct AddComplete {
  union {
    Prefix prefix;
    Completion complete;
  } hash;
  uint32_t addChunk;

  AddComplete() : addChunk(0) {}

  uint32_t Chunk() const { return addChunk; }
  const Prefix &PrefixHash() const { return hash.prefix; }
  const Completion &CompleteHash() const { return hash.complete; }

  template<class T>
  int Compare(const T& other) const {
    int cmp = hash.complete.Compare(other.CompleteHash());
    if (cmp != 0) {
      return cmp;
    }
    return addChunk - other.addChunk;
  }
};

struct SubPrefix {
  Prefix prefix;
  uint32_t addChunk;
  uint32_t subChunk;

  SubPrefix(): addChunk(0), subChunk(0) {}

  uint32_t Chunk() const { return subChunk; }
  uint32_t AddChunk() const { return addChunk; }
  const Prefix &PrefixHash() const { return prefix; }

  template<class T>
  int Compare(const T& aOther) const {
    int cmp = prefix.Compare(aOther.PrefixHash());
    if (cmp != 0)
      return cmp;
    if (addChunk != aOther.addChunk)
      return addChunk - aOther.addChunk;
    return subChunk - aOther.subChunk;
  }

  template<class T>
  int CompareAlt(const T& aOther) const {
    int cmp = prefix.Compare(aOther.PrefixHash());
    if (cmp != 0)
      return cmp;
    return addChunk - aOther.addChunk;
  }
};

struct SubComplete {
  union {
    Prefix prefix;
    Completion complete;
  } hash;
  uint32_t addChunk;
  uint32_t subChunk;

  SubComplete() : addChunk(0), subChunk(0) {}

  uint32_t Chunk() const { return subChunk; }
  uint32_t AddChunk() const { return addChunk; }
  const Prefix &PrefixHash() const { return hash.prefix; }
  const Completion &CompleteHash() const { return hash.complete; }

  int Compare(const SubComplete& aOther) const {
    int cmp = hash.complete.Compare(aOther.hash.complete);
    if (cmp != 0)
      return cmp;
    if (addChunk != aOther.addChunk)
      return addChunk - aOther.addChunk;
    return subChunk - aOther.subChunk;
  }
};

typedef nsTArray<AddPrefix>   AddPrefixArray;
typedef nsTArray<AddComplete> AddCompleteArray;
typedef nsTArray<SubPrefix>   SubPrefixArray;
typedef nsTArray<SubComplete> SubCompleteArray;

/**
 * Compares chunks by their add chunk, then their prefix.
 */
template<class T>
class EntryCompare {
public:
  typedef T elem_type;
  static int Compare(const void* e1, const void* e2) {
    const elem_type* a = static_cast<const elem_type*>(e1);
    const elem_type* b = static_cast<const elem_type*>(e2);
    return a->Compare(*b);
  }
};

/**
 * Sort an array of store entries.  nsTArray::Sort uses Equal/LessThan
 * to sort, this does a single Compare so it's a bit quicker over the
 * large sorts we do.
 */
template<class T>
void
EntrySort(nsTArray<T>& aArray)
{
  qsort(aArray.Elements(), aArray.Length(), sizeof(T),
        EntryCompare<T>::Compare);
}

template<class T>
nsresult
ReadTArray(nsIInputStream* aStream, nsTArray<T>* aArray, uint32_t aNumElements)
{
  if (!aArray->SetLength(aNumElements))
    return NS_ERROR_OUT_OF_MEMORY;

  void *buffer = aArray->Elements();
  nsresult rv = NS_ReadInputStreamToBuffer(aStream, &buffer,
                                           (aNumElements * sizeof(T)));
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

template<class T>
nsresult
WriteTArray(nsIOutputStream* aStream, nsTArray<T>& aArray)
{
  uint32_t written;
  return aStream->Write(reinterpret_cast<char*>(aArray.Elements()),
                        aArray.Length() * sizeof(T),
                        &written);
}

}
}
#endif
