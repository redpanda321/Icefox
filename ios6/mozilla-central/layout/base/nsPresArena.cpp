/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=2 sw=2 et tw=78:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/* arena allocation for the frame tree and closely-related objects */

#include "nsPresArena.h"
#include "nsCRT.h"
#include "nsDebug.h"
#include "nsTArray.h"
#include "nsTHashtable.h"
#include "prinit.h"
#include "prlog.h"
#include "nsArenaMemoryStats.h"
#include "nsCOMPtr.h"
#include "nsServiceManagerUtils.h"
#include "nsPrintfCString.h"

#ifdef MOZ_CRASHREPORTER
#include "nsICrashReporter.h"
#endif

#include "mozilla/StandardInteger.h"

// Even on 32-bit systems, we allocate objects from the frame arena
// that require 8-byte alignment.  The cast to uintptr_t is needed
// because plarena isn't as careful about mask construction as it
// ought to be.
#define ALIGN_SHIFT 3
#define PL_ARENA_CONST_ALIGN_MASK ((uintptr_t(1) << ALIGN_SHIFT) - 1)
#include "plarena.h"

#ifdef _WIN32
# include <windows.h>
#elif !defined(__OS2__)
# include <unistd.h>
# include <sys/mman.h>
# ifndef MAP_ANON
#  ifdef MAP_ANONYMOUS
#   define MAP_ANON MAP_ANONYMOUS
#  else
#   error "Don't know how to get anonymous memory"
#  endif
# endif
#endif

// Size to use for PLArena block allocations.
static const size_t ARENA_PAGE_SIZE = 8192;

// Freed memory is filled with a poison value, which we arrange to
// form a pointer either to an always-unmapped region of the address
// space, or to a page that has been reserved and rendered
// inaccessible via OS primitives.  See tests/TestPoisonArea.cpp for
// extensive discussion of the requirements for this page.  The code
// from here to 'class FreeList' needs to be kept in sync with that
// file.

#ifdef _WIN32
static void *
ReserveRegion(uintptr_t region, uintptr_t size)
{
  return VirtualAlloc((void *)region, size, MEM_RESERVE, PAGE_NOACCESS);
}

static void
ReleaseRegion(void *region, uintptr_t size)
{
  VirtualFree(region, size, MEM_RELEASE);
}

static bool
ProbeRegion(uintptr_t region, uintptr_t size)
{
  SYSTEM_INFO sinfo;
  GetSystemInfo(&sinfo);
  if (region >= (uintptr_t)sinfo.lpMaximumApplicationAddress &&
      region + size >= (uintptr_t)sinfo.lpMaximumApplicationAddress) {
    return true;
  } else {
    return false;
  }
}

static uintptr_t
GetDesiredRegionSize()
{
  SYSTEM_INFO sinfo;
  GetSystemInfo(&sinfo);
  return sinfo.dwAllocationGranularity;
}

#define RESERVE_FAILED 0

#elif defined(__OS2__)
static void *
ReserveRegion(uintptr_t region, uintptr_t size)
{
  // OS/2 doesn't support allocation at an arbitrary address,
  // so return an address that is known to be invalid.
  return (void*)0xFFFD0000;
}

static void
ReleaseRegion(void *region, uintptr_t size)
{
  return;
}

static bool
ProbeRegion(uintptr_t region, uintptr_t size)
{
  // There's no reliable way to probe an address in the system
  // arena other than by touching it and seeing if a trap occurs.
  return false;
}

static uintptr_t
GetDesiredRegionSize()
{
  // Page size is fixed at 4k.
  return 0x1000;
}

#define RESERVE_FAILED 0

#else // Unix

static void *
ReserveRegion(uintptr_t region, uintptr_t size)
{
  return mmap(reinterpret_cast<void*>(region), size, PROT_NONE, MAP_PRIVATE|MAP_ANON, -1, 0);
}

static void
ReleaseRegion(void *region, uintptr_t size)
{
  munmap(region, size);
}

static bool
ProbeRegion(uintptr_t region, uintptr_t size)
{
  if (madvise(reinterpret_cast<void*>(region), size, MADV_NORMAL)) {
    return true;
  } else {
    return false;
  }
}

static uintptr_t
GetDesiredRegionSize()
{
  return sysconf(_SC_PAGESIZE);
}

#define RESERVE_FAILED MAP_FAILED

#endif // system dependencies

PR_STATIC_ASSERT(sizeof(uintptr_t) == 4 || sizeof(uintptr_t) == 8);
PR_STATIC_ASSERT(sizeof(uintptr_t) == sizeof(void *));

static uintptr_t
ReservePoisonArea(uintptr_t rgnsize)
{
  if (sizeof(uintptr_t) == 8) {
    // Use the hardware-inaccessible region.
    // We have to avoid 64-bit constants and shifts by 32 bits, since this
    // code is compiled in 32-bit mode, although it is never executed there.
    return
      (((uintptr_t(0x7FFFFFFFu) << 31) << 1 | uintptr_t(0xF0DEAFFFu))
       & ~(rgnsize-1));

  } else {
    // First see if we can allocate the preferred poison address from the OS.
    uintptr_t candidate = (0xF0DEAFFF & ~(rgnsize-1));
    void *result = ReserveRegion(candidate, rgnsize);
    if (result == (void *)candidate) {
      // success - inaccessible page allocated
      return candidate;
    }

    // That didn't work, so see if the preferred address is within a range
    // of permanently inacessible memory.
    if (ProbeRegion(candidate, rgnsize)) {
      // success - selected page cannot be usable memory
      if (result != RESERVE_FAILED)
        ReleaseRegion(result, rgnsize);
      return candidate;
    }

    // The preferred address is already in use.  Did the OS give us a
    // consolation prize?
    if (result != RESERVE_FAILED) {
      return uintptr_t(result);
    }

    // It didn't, so try to allocate again, without any constraint on
    // the address.
    result = ReserveRegion(0, rgnsize);
    if (result != RESERVE_FAILED) {
      return uintptr_t(result);
    }

    NS_RUNTIMEABORT("no usable poison region identified");
    return 0;
  }
}

static uintptr_t ARENA_POISON;
static PRCallOnceType ARENA_POISON_guard;

static PRStatus
ARENA_POISON_init()
{
  uintptr_t rgnsize = GetDesiredRegionSize();
  uintptr_t rgnbase = ReservePoisonArea(rgnsize);

  if (rgnsize == 0) // can't happen
    return PR_FAILURE;

  ARENA_POISON = rgnbase + rgnsize/2 - 1;

#ifdef MOZ_CRASHREPORTER
  nsCOMPtr<nsICrashReporter> cr =
    do_GetService("@mozilla.org/toolkit/crash-reporter;1");
  bool enabled;
  if (cr && NS_SUCCEEDED(cr->GetEnabled(&enabled)) && enabled) {
    cr->AnnotateCrashReport(NS_LITERAL_CSTRING("FramePoisonBase"),
                            nsPrintfCString("%.16llx", uint64_t(rgnbase)));
    cr->AnnotateCrashReport(NS_LITERAL_CSTRING("FramePoisonSize"),
                            nsPrintfCString("%lu", uint32_t(rgnsize)));
  }
#endif
  return PR_SUCCESS;
}

#ifndef DEBUG_TRACEMALLOC_PRESARENA

// All keys to this hash table fit in 32 bits (see below) so we do not
// bother actually hashing them.

namespace {

class FreeList : public PLDHashEntryHdr
{
public:
  typedef uint32_t KeyType;
  nsTArray<void *> mEntries;
  size_t mEntrySize;
  size_t mEntriesEverAllocated;

  typedef const void* KeyTypePointer;
  KeyTypePointer mKey;

  FreeList(KeyTypePointer aKey)
  : mEntrySize(0), mEntriesEverAllocated(0), mKey(aKey) {}
  // Default copy constructor and destructor are ok.

  bool KeyEquals(KeyTypePointer const aKey) const
  { return mKey == aKey; }

  static KeyTypePointer KeyToPointer(KeyType aKey)
  { return NS_INT32_TO_PTR(aKey); }

  static PLDHashNumber HashKey(KeyTypePointer aKey)
  { return NS_PTR_TO_INT32(aKey); }

  enum { ALLOW_MEMMOVE = false };
};

}

struct nsPresArena::State {
  nsTHashtable<FreeList> mFreeLists;
  PLArenaPool mPool;

  State()
  {
    mFreeLists.Init();
    PL_INIT_ARENA_POOL(&mPool, "PresArena", ARENA_PAGE_SIZE);
    PR_CallOnce(&ARENA_POISON_guard, ARENA_POISON_init);
  }

  ~State()
  {
    PL_FinishArenaPool(&mPool);
  }

  void* Allocate(uint32_t aCode, size_t aSize)
  {
    NS_ABORT_IF_FALSE(aSize > 0, "PresArena cannot allocate zero bytes");

    // We only hand out aligned sizes
    aSize = PL_ARENA_ALIGN(&mPool, aSize);

    // If there is no free-list entry for this type already, we have
    // to create one now, to record its size.
    FreeList* list = mFreeLists.PutEntry(aCode);

    nsTArray<void*>::index_type len = list->mEntries.Length();
    if (list->mEntrySize == 0) {
      NS_ABORT_IF_FALSE(len == 0, "list with entries but no recorded size");
      list->mEntrySize = aSize;
    } else {
      NS_ABORT_IF_FALSE(list->mEntrySize == aSize,
                        "different sizes for same object type code");
    }

    void* result;
    if (len > 0) {
      // LIFO behavior for best cache utilization
      result = list->mEntries.ElementAt(len - 1);
      list->mEntries.RemoveElementAt(len - 1);
#ifdef DEBUG
      {
        char* p = reinterpret_cast<char*>(result);
        char* limit = p + list->mEntrySize;
        for (; p < limit; p += sizeof(uintptr_t)) {
          uintptr_t val = *reinterpret_cast<uintptr_t*>(p);
          NS_ABORT_IF_FALSE(val == ARENA_POISON,
                            nsPrintfCString("PresArena: poison overwritten; "
                                            "wanted %.16llx "
                                            "found %.16llx "
                                            "errors in bits %.16llx",
                                            uint64_t(ARENA_POISON),
                                            uint64_t(val),
                                            uint64_t(ARENA_POISON ^ val)
                                            ).get());
        }
      }
#endif
      return result;
    }

    // Allocate a new chunk from the arena
    list->mEntriesEverAllocated++;
    PL_ARENA_ALLOCATE(result, &mPool, aSize);
    if (!result) {
      NS_RUNTIMEABORT("out of memory");
    }
    return result;
  }

  void Free(uint32_t aCode, void* aPtr)
  {
    // Try to recycle this entry.
    FreeList* list = mFreeLists.GetEntry(aCode);
    NS_ABORT_IF_FALSE(list, "no free list for pres arena object");
    NS_ABORT_IF_FALSE(list->mEntrySize > 0, "PresArena cannot free zero bytes");

    char* p = reinterpret_cast<char*>(aPtr);
    char* limit = p + list->mEntrySize;
    for (; p < limit; p += sizeof(uintptr_t)) {
      *reinterpret_cast<uintptr_t*>(p) = ARENA_POISON;
    }

    list->mEntries.AppendElement(aPtr);
  }

  static size_t SizeOfFreeListEntryExcludingThis(FreeList* aEntry,
                                                 nsMallocSizeOfFun aMallocSizeOf,
                                                 void *)
  {
    return aEntry->mEntries.SizeOfExcludingThis(aMallocSizeOf);
  }

  size_t SizeOfIncludingThisFromMalloc(nsMallocSizeOfFun aMallocSizeOf) const
  {
    size_t n = aMallocSizeOf(this);

    // The first PLArena is within the PLArenaPool, i.e. within |this|, so we
    // don't measure it.  Subsequent PLArenas are by themselves and must be
    // measured.
    const PLArena *arena = mPool.first.next;
    while (arena) {
      n += aMallocSizeOf(arena);
      arena = arena->next;
    }
    n += mFreeLists.SizeOfExcludingThis(SizeOfFreeListEntryExcludingThis,
                                        aMallocSizeOf);
    return n;
  }

  struct EnumerateData {
    nsArenaMemoryStats* stats;
    size_t total;
  };

  static PLDHashOperator FreeListEnumerator(FreeList* aEntry, void* aData)
  {
    EnumerateData* data = static_cast<EnumerateData*>(aData);
    // Note that we're not measuring the size of the entries on the free
    // list here.  The free list knows how many objects we've allocated
    // ever (which includes any objects that may be on the FreeList's
    // |mEntries| at this point) and we're using that to determine the
    // total size of objects allocated with a given ID.
    size_t totalSize = aEntry->mEntrySize * aEntry->mEntriesEverAllocated;
    size_t* p;

    switch (NS_PTR_TO_INT32(aEntry->mKey)) {
#define FRAME_ID(classname)                                        \
      case nsQueryFrame::classname##_id:                           \
        p = &data->stats->FRAME_ID_STAT_FIELD(classname);          \
        break;
#include "nsFrameIdList.h"
#undef FRAME_ID
    case nsLineBox_id:
      p = &data->stats->mLineBoxes;
      break;
    case nsRuleNode_id:
      p = &data->stats->mRuleNodes;
      break;
    case nsStyleContext_id:
      p = &data->stats->mStyleContexts;
      break;
    default:
      return PL_DHASH_NEXT;
    }

    *p += totalSize;
    data->total += totalSize;

    return PL_DHASH_NEXT;
  }

  void SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf,
                           nsArenaMemoryStats* aArenaStats)
  {
    // We do a complicated dance here because we want to measure the
    // space taken up by the different kinds of objects in the arena,
    // but we don't have pointers to those objects.  And even if we did,
    // we wouldn't be able to use aMallocSizeOf on them, since they were
    // allocated out of malloc'd chunks of memory.  So we compute the
    // size of the arena as known by malloc and we add up the sizes of
    // all the objects that we care about.  Subtracting these two
    // quantities gives us a catch-all "other" number, which includes
    // slop in the arena itself as well as the size of objects that
    // we've not measured explicitly.

    size_t mallocSize = SizeOfIncludingThisFromMalloc(aMallocSizeOf);
    EnumerateData data = { aArenaStats, 0 };
    mFreeLists.EnumerateEntries(FreeListEnumerator, &data);
    aArenaStats->mOther = mallocSize - data.total;
  }
};

void
nsPresArena::SizeOfExcludingThis(nsMallocSizeOfFun aMallocSizeOf,
                                 nsArenaMemoryStats* aArenaStats)
{
  mState->SizeOfIncludingThis(aMallocSizeOf, aArenaStats);
}

#else
// Stub implementation that forwards everything to malloc and does not
// poison allocations (it still initializes the poison value though,
// for external use through GetPoisonValue()).

struct nsPresArena::State
{

  State()
  {
    PR_CallOnce(&ARENA_POISON_guard, ARENA_POISON_init);
  }

  void* Allocate(uint32_t /* unused */, size_t aSize)
  {
    return moz_malloc(aSize);
  }

  void Free(uint32_t /* unused */, void* aPtr)
  {
    moz_free(aPtr);
  }
};

void
nsPresArena::SizeOfExcludingThis(nsMallocSizeOfFun, nsArenaMemoryStats*)
{}

#endif // DEBUG_TRACEMALLOC_PRESARENA

// Public interface
nsPresArena::nsPresArena()
  : mState(new nsPresArena::State())
{}

nsPresArena::~nsPresArena()
{
  delete mState;
}

void*
nsPresArena::AllocateBySize(size_t aSize)
{
  return mState->Allocate(uint32_t(aSize) | uint32_t(NON_OBJECT_MARKER),
                          aSize);
}

void
nsPresArena::FreeBySize(size_t aSize, void* aPtr)
{
  mState->Free(uint32_t(aSize) | uint32_t(NON_OBJECT_MARKER), aPtr);
}

void*
nsPresArena::AllocateByFrameID(nsQueryFrame::FrameIID aID, size_t aSize)
{
  return mState->Allocate(aID, aSize);
}

void
nsPresArena::FreeByFrameID(nsQueryFrame::FrameIID aID, void* aPtr)
{
  mState->Free(aID, aPtr);
}

void*
nsPresArena::AllocateByObjectID(ObjectID aID, size_t aSize)
{
  return mState->Allocate(aID, aSize);
}

void
nsPresArena::FreeByObjectID(ObjectID aID, void* aPtr)
{
  mState->Free(aID, aPtr);
}

/* static */ uintptr_t
nsPresArena::GetPoisonValue()
{
  return ARENA_POISON;
}
