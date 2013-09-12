/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set cindent tabstop=4 expandtab shiftwidth=4: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//
// This file implements a garbage-cycle collector based on the paper
//
//   Concurrent Cycle Collection in Reference Counted Systems
//   Bacon & Rajan (2001), ECOOP 2001 / Springer LNCS vol 2072
//
// We are not using the concurrent or acyclic cases of that paper; so
// the green, red and orange colors are not used.
//
// The collector is based on tracking pointers of four colors:
//
// Black nodes are definitely live. If we ever determine a node is
// black, it's ok to forget about, drop from our records.
//
// White nodes are definitely garbage cycles. Once we finish with our
// scanning, we unlink all the white nodes and expect that by
// unlinking them they will self-destruct (since a garbage cycle is
// only keeping itself alive with internal links, by definition).
//
// Grey nodes are being scanned. Nodes that turn grey will turn
// either black if we determine that they're live, or white if we
// determine that they're a garbage cycle. After the main collection
// algorithm there should be no grey nodes.
//
// Purple nodes are *candidates* for being scanned. They are nodes we
// haven't begun scanning yet because they're not old enough, or we're
// still partway through the algorithm.
//
// XPCOM objects participating in garbage-cycle collection are obliged
// to inform us when they ought to turn purple; that is, when their
// refcount transitions from N+1 -> N, for nonzero N. Furthermore we
// require that *after* an XPCOM object has informed us of turning
// purple, they will tell us when they either transition back to being
// black (incremented refcount) or are ultimately deleted.


// Safety:
//
// An XPCOM object is either scan-safe or scan-unsafe, purple-safe or
// purple-unsafe.
//
// An object is scan-safe if:
//
//  - It can be QI'ed to |nsXPCOMCycleCollectionParticipant|, though this
//    operation loses ISupports identity (like nsIClassInfo).
//  - The operation |traverse| on the resulting
//    nsXPCOMCycleCollectionParticipant does not cause *any* refcount
//    adjustment to occur (no AddRef / Release calls).
//
// An object is purple-safe if it satisfies the following properties:
//
//  - The object is scan-safe.  
//  - If the object calls |nsCycleCollector::suspect(this)|, 
//    it will eventually call |nsCycleCollector::forget(this)|, 
//    exactly once per call to |suspect|, before being destroyed.
//
// When we receive a pointer |ptr| via
// |nsCycleCollector::suspect(ptr)|, we assume it is purple-safe. We
// can check the scan-safety, but have no way to ensure the
// purple-safety; objects must obey, or else the entire system falls
// apart. Don't involve an object in this scheme if you can't
// guarantee its purple-safety.
//
// When we have a scannable set of purple nodes ready, we begin
// our walks. During the walks, the nodes we |traverse| should only
// feed us more scan-safe nodes, and should not adjust the refcounts
// of those nodes. 
//
// We do not |AddRef| or |Release| any objects during scanning. We
// rely on purple-safety of the roots that call |suspect| and
// |forget| to hold, such that we will forget about a purple pointer
// before it is destroyed.  The pointers that are merely scan-safe,
// we hold only for the duration of scanning, and there should be no
// objects released from the scan-safe set during the scan (there
// should be no threads involved).
//
// We *do* call |AddRef| and |Release| on every white object, on
// either side of the calls to |Unlink|. This keeps the set of white
// objects alive during the unlinking.
// 

#if !defined(__MINGW32__)
#ifdef WIN32
#include <crtdbg.h>
#include <errno.h>
#endif
#endif

#include "base/process_util.h"

/* This must occur *after* base/process_util.h to avoid typedefs conflicts. */
#include "mozilla/Util.h"

#include "nsCycleCollectionParticipant.h"
#include "nsCycleCollectorUtils.h"
#include "nsIProgrammingLanguage.h"
#include "nsBaseHashtable.h"
#include "nsHashKeys.h"
#include "nsDeque.h"
#include "nsCycleCollector.h"
#include "nsThreadUtils.h"
#include "prenv.h"
#include "prprf.h"
#include "plstr.h"
#include "nsPrintfCString.h"
#include "nsTArray.h"
#include "nsIObserverService.h"
#include "nsIConsoleService.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nsTArray.h"
#include "mozilla/Services.h"
#include "mozilla/Attributes.h"
#include "nsICycleCollectorListener.h"
#include "nsIXPConnect.h"
#include "nsIJSRuntimeService.h"
#include "nsIMemoryReporter.h"
#include "nsIFile.h"
#include "nsDirectoryServiceDefs.h"
#include "xpcpublic.h"
#include "nsXPCOMPrivate.h"
#include "sampler.h"
#include <stdio.h>
#include <string.h>
#ifdef WIN32
#include <io.h>
#include <process.h>
#endif
#ifdef ANDROID
#include <sys/stat.h>
#endif

#ifdef XP_WIN
#include <windows.h>
#endif

#include "mozilla/CondVar.h"
#include "mozilla/Likely.h"
#include "mozilla/Mutex.h"
#include "mozilla/StandardInteger.h"
#include "mozilla/Telemetry.h"

using namespace mozilla;

//#define COLLECT_TIME_DEBUG

#define DEFAULT_SHUTDOWN_COLLECTIONS 5
#ifdef DEBUG_CC
#define SHUTDOWN_COLLECTIONS(params) params.mShutdownCollections
#else
#define SHUTDOWN_COLLECTIONS(params) DEFAULT_SHUTDOWN_COLLECTIONS
#endif

#if defined(XP_WIN)
// Defined in nsThreadManager.cpp.
extern DWORD gTLSThreadIDIndex;
#elif defined(NS_TLS)
// Defined in nsThreadManager.cpp.
extern NS_TLS mozilla::threads::ID gTLSThreadID;
#else
PRThread* gCycleCollectorThread = nullptr;
#endif

// If true, always log cycle collector graphs.
const bool gAlwaysLogCCGraphs = false;

// If true, log the cycle collector graphs during shutdown.
const bool gLogShutdown = false;

// If true, any logging done at shutdown will be AllTraces.
const bool gAllTracesAtShutdown = false;

MOZ_NEVER_INLINE void
CC_AbortIfNull(void *ptr)
{
    if (!ptr)
        MOZ_CRASH();
}

// Various parameters of this collector can be tuned using environment
// variables.

struct nsCycleCollectorParams
{
    bool mDoNothing;
    bool mLogGraphs;
#ifdef DEBUG_CC
    bool mReportStats;
    bool mLogPointers;
    uint32_t mShutdownCollections;
#endif
    
    nsCycleCollectorParams() :
#ifdef DEBUG_CC
        mDoNothing     (PR_GetEnv("XPCOM_CC_DO_NOTHING") != NULL),
        mLogGraphs     (gAlwaysLogCCGraphs ||
                        PR_GetEnv("XPCOM_CC_DRAW_GRAPHS") != NULL),
        mReportStats   (PR_GetEnv("XPCOM_CC_REPORT_STATS") != NULL),
        mLogPointers   (PR_GetEnv("XPCOM_CC_LOG_POINTERS") != NULL),

        mShutdownCollections(DEFAULT_SHUTDOWN_COLLECTIONS)
#else
        mDoNothing     (false),
        mLogGraphs     (gAlwaysLogCCGraphs)
#endif
    {
#ifdef DEBUG_CC
        char *s = PR_GetEnv("XPCOM_CC_SHUTDOWN_COLLECTIONS");
        if (s)
            PR_sscanf(s, "%d", &mShutdownCollections);
#endif
    }
};

#ifdef DEBUG_CC
// Various operations involving the collector are recorded in a
// statistics table. These are for diagnostics.

struct nsCycleCollectorStats
{
    uint32_t mFailedQI;
    uint32_t mSuccessfulQI;

    uint32_t mVisitedNode;
    uint32_t mWalkedGraph;
    uint32_t mFreedBytes;

    uint32_t mSetColorBlack;
    uint32_t mSetColorWhite;

    uint32_t mFailedUnlink;
    uint32_t mCollectedNode;

    uint32_t mSuspectNode;
    uint32_t mForgetNode;
  
    uint32_t mCollection;

    nsCycleCollectorStats()
    {
        memset(this, 0, sizeof(nsCycleCollectorStats));
    }
  
    void Dump()
    {
        fprintf(stderr, "\f\n");
#define DUMP(entry) fprintf(stderr, "%30.30s: %-20.20d\n", #entry, entry)
        DUMP(mFailedQI);
        DUMP(mSuccessfulQI);
    
        DUMP(mVisitedNode);
        DUMP(mWalkedGraph);
        DUMP(mFreedBytes);
    
        DUMP(mSetColorBlack);
        DUMP(mSetColorWhite);
    
        DUMP(mFailedUnlink);
        DUMP(mCollectedNode);
    
        DUMP(mSuspectNode);
        DUMP(mForgetNode);
    
        DUMP(mCollection);
#undef DUMP
    }
};
#endif

#ifdef DEBUG_CC
static bool nsCycleCollector_shouldSuppress(nsISupports *s);
#endif

#ifdef COLLECT_TIME_DEBUG
class TimeLog
{
public:
    TimeLog() : mLastCheckpoint(TimeStamp::Now()) {}

    void
    Checkpoint(const char* aEvent)
    {
        TimeStamp now = TimeStamp::Now();
        uint32_t dur = (uint32_t) ((now - mLastCheckpoint).ToMilliseconds());
        if (dur > 0) {
            printf("cc: %s took %dms\n", aEvent, dur);
        }
        mLastCheckpoint = now;
    }

private:
    TimeStamp mLastCheckpoint;
};
#else
class TimeLog
{
public:
    TimeLog() {}
    void Checkpoint(const char* aEvent) {}
};
#endif


////////////////////////////////////////////////////////////////////////
// Base types
////////////////////////////////////////////////////////////////////////

struct PtrInfo;

class EdgePool
{
public:
    // EdgePool allocates arrays of void*, primarily to hold PtrInfo*.
    // However, at the end of a block, the last two pointers are a null
    // and then a void** pointing to the next block.  This allows
    // EdgePool::Iterators to be a single word but still capable of crossing
    // block boundaries.

    EdgePool()
    {
        mSentinelAndBlocks[0].block = nullptr;
        mSentinelAndBlocks[1].block = nullptr;
    }

    ~EdgePool()
    {
        MOZ_ASSERT(!mSentinelAndBlocks[0].block &&
                   !mSentinelAndBlocks[1].block,
                   "Didn't call Clear()?");
    }

    void Clear()
    {
        Block *b = Blocks();
        while (b) {
            Block *next = b->Next();
            delete b;
            b = next;
        }

        mSentinelAndBlocks[0].block = nullptr;
        mSentinelAndBlocks[1].block = nullptr;
    }

private:
    struct Block;
    union PtrInfoOrBlock {
        // Use a union to avoid reinterpret_cast and the ensuing
        // potential aliasing bugs.
        PtrInfo *ptrInfo;
        Block *block;
    };
    struct Block {
        enum { BlockSize = 16 * 1024 };

        PtrInfoOrBlock mPointers[BlockSize];
        Block() {
            mPointers[BlockSize - 2].block = nullptr; // sentinel
            mPointers[BlockSize - 1].block = nullptr; // next block pointer
        }
        Block*& Next()          { return mPointers[BlockSize - 1].block; }
        PtrInfoOrBlock* Start() { return &mPointers[0]; }
        PtrInfoOrBlock* End()   { return &mPointers[BlockSize - 2]; }
    };

    // Store the null sentinel so that we can have valid iterators
    // before adding any edges and without adding any blocks.
    PtrInfoOrBlock mSentinelAndBlocks[2];

    Block*& Blocks()       { return mSentinelAndBlocks[1].block; }
    Block*  Blocks() const { return mSentinelAndBlocks[1].block; }

public:
    class Iterator
    {
    public:
        Iterator() : mPointer(nullptr) {}
        Iterator(PtrInfoOrBlock *aPointer) : mPointer(aPointer) {}
        Iterator(const Iterator& aOther) : mPointer(aOther.mPointer) {}

        Iterator& operator++()
        {
            if (mPointer->ptrInfo == nullptr) {
                // Null pointer is a sentinel for link to the next block.
                mPointer = (mPointer + 1)->block->mPointers;
            }
            ++mPointer;
            return *this;
        }

        PtrInfo* operator*() const
        {
            if (mPointer->ptrInfo == nullptr) {
                // Null pointer is a sentinel for link to the next block.
                return (mPointer + 1)->block->mPointers->ptrInfo;
            }
            return mPointer->ptrInfo;
        }
        bool operator==(const Iterator& aOther) const
            { return mPointer == aOther.mPointer; }
        bool operator!=(const Iterator& aOther) const
            { return mPointer != aOther.mPointer; }

    private:
        PtrInfoOrBlock *mPointer;
    };

    class Builder;
    friend class Builder;
    class Builder {
    public:
        Builder(EdgePool &aPool)
            : mCurrent(&aPool.mSentinelAndBlocks[0]),
              mBlockEnd(&aPool.mSentinelAndBlocks[0]),
              mNextBlockPtr(&aPool.Blocks())
        {
        }

        Iterator Mark() { return Iterator(mCurrent); }

        void Add(PtrInfo* aEdge) {
            if (mCurrent == mBlockEnd) {
                Block *b = new Block();
                if (!b) {
                    // This means we just won't collect (some) cycles.
                    NS_NOTREACHED("out of memory, ignoring edges");
                    return;
                }
                *mNextBlockPtr = b;
                mCurrent = b->Start();
                mBlockEnd = b->End();
                mNextBlockPtr = &b->Next();
            }
            (mCurrent++)->ptrInfo = aEdge;
        }
    private:
        // mBlockEnd points to space for null sentinel
        PtrInfoOrBlock *mCurrent, *mBlockEnd;
        Block **mNextBlockPtr;
    };

    size_t SizeOfExcludingThis(nsMallocSizeOfFun aMallocSizeOf) const {
        size_t n = 0;
        Block *b = Blocks();
        while (b) {
            n += aMallocSizeOf(b);
            b = b->Next();
        }
        return n;
    }
};

enum NodeColor { black, white, grey };

// This structure should be kept as small as possible; we may expect
// hundreds of thousands of them to be allocated and touched
// repeatedly during each cycle collection.

struct PtrInfo
{
    void *mPointer;
    nsCycleCollectionParticipant *mParticipant;
    uint32_t mColor : 2;
    uint32_t mInternalRefs : 30;
    uint32_t mRefCount;
private:
    EdgePool::Iterator mFirstChild;

public:
#ifdef DEBUG_CC
    char *mName;
#endif

    PtrInfo(void *aPointer, nsCycleCollectionParticipant *aParticipant)
        : mPointer(aPointer),
          mParticipant(aParticipant),
          mColor(grey),
          mInternalRefs(0),
          mRefCount(0),
          mFirstChild()
#ifdef DEBUG_CC
        , mName(nullptr)
#endif
    {
        MOZ_ASSERT(aParticipant);
    }

#ifdef DEBUG_CC
    void Destroy() {
        PL_strfree(mName);
    }
#endif

    // Allow NodePool::Block's constructor to compile.
    PtrInfo() {
        NS_NOTREACHED("should never be called");
    }

    EdgePool::Iterator FirstChild()
    {
        return mFirstChild;
    }

    // this PtrInfo must be part of a NodePool
    EdgePool::Iterator LastChild()
    {
        return (this + 1)->mFirstChild;
    }

    void SetFirstChild(EdgePool::Iterator aFirstChild)
    {
        mFirstChild = aFirstChild;
    }

    // this PtrInfo must be part of a NodePool
    void SetLastChild(EdgePool::Iterator aLastChild)
    {
        (this + 1)->mFirstChild = aLastChild;
    }
};

/**
 * A structure designed to be used like a linked list of PtrInfo, except
 * that allocates the PtrInfo 32K-at-a-time.
 */
class NodePool
{
private:
    enum { BlockSize = 8 * 1024 }; // could be int template parameter

    struct Block {
        // We create and destroy Block using NS_Alloc/NS_Free rather
        // than new and delete to avoid calling its constructor and
        // destructor.
        Block()  { NS_NOTREACHED("should never be called"); }
        ~Block() { NS_NOTREACHED("should never be called"); }

        Block* mNext;
        PtrInfo mEntries[BlockSize + 1]; // +1 to store last child of last node
    };

public:
    NodePool()
        : mBlocks(nullptr),
          mLast(nullptr)
    {
    }

    ~NodePool()
    {
        MOZ_ASSERT(!mBlocks, "Didn't call Clear()?");
    }

    void Clear()
    {
#ifdef DEBUG_CC
        {
            Enumerator queue(*this);
            while (!queue.IsDone()) {
                queue.GetNext()->Destroy();
            }
        }
#endif
        Block *b = mBlocks;
        while (b) {
            Block *n = b->mNext;
            NS_Free(b);
            b = n;
        }

        mBlocks = nullptr;
        mLast = nullptr;
    }

    class Builder;
    friend class Builder;
    class Builder {
    public:
        Builder(NodePool& aPool)
            : mNextBlock(&aPool.mBlocks),
              mNext(aPool.mLast),
              mBlockEnd(nullptr)
        {
            MOZ_ASSERT(aPool.mBlocks == nullptr && aPool.mLast == nullptr,
                       "pool not empty");
        }
        PtrInfo *Add(void *aPointer, nsCycleCollectionParticipant *aParticipant)
        {
            if (mNext == mBlockEnd) {
                Block *block;
                if (!(*mNextBlock = block =
                        static_cast<Block*>(NS_Alloc(sizeof(Block)))))
                    return nullptr;
                mNext = block->mEntries;
                mBlockEnd = block->mEntries + BlockSize;
                block->mNext = nullptr;
                mNextBlock = &block->mNext;
            }
            return new (mNext++) PtrInfo(aPointer, aParticipant);
        }
    private:
        Block **mNextBlock;
        PtrInfo *&mNext;
        PtrInfo *mBlockEnd;
    };

    class Enumerator;
    friend class Enumerator;
    class Enumerator {
    public:
        Enumerator(NodePool& aPool)
            : mFirstBlock(aPool.mBlocks),
              mCurBlock(nullptr),
              mNext(nullptr),
              mBlockEnd(nullptr),
              mLast(aPool.mLast)
        {
        }

        bool IsDone() const
        {
            return mNext == mLast;
        }

        bool AtBlockEnd() const
        {
            return mNext == mBlockEnd;
        }

        PtrInfo* GetNext()
        {
            MOZ_ASSERT(!IsDone(), "calling GetNext when done");
            if (mNext == mBlockEnd) {
                Block *nextBlock = mCurBlock ? mCurBlock->mNext : mFirstBlock;
                mNext = nextBlock->mEntries;
                mBlockEnd = mNext + BlockSize;
                mCurBlock = nextBlock;
            }
            return mNext++;
        }
    private:
        Block *mFirstBlock, *mCurBlock;
        // mNext is the next value we want to return, unless mNext == mBlockEnd
        // NB: mLast is a reference to allow enumerating while building!
        PtrInfo *mNext, *mBlockEnd, *&mLast;
    };

    size_t SizeOfExcludingThis(nsMallocSizeOfFun aMallocSizeOf) const {
        // We don't measure the things pointed to by mEntries[] because those
        // pointers are non-owning.
        size_t n = 0;
        Block *b = mBlocks;
        while (b) {
            n += aMallocSizeOf(b);
            b = b->mNext;
        }
        return n;
    }

private:
    Block *mBlocks;
    PtrInfo *mLast;
};


struct WeakMapping
{
    // map and key will be null if the corresponding objects are GC marked
    PtrInfo *mMap;
    PtrInfo *mKey;
    PtrInfo *mKeyDelegate;
    PtrInfo *mVal;
};

class GCGraphBuilder;

struct GCGraph
{
    NodePool mNodes;
    EdgePool mEdges;
    nsTArray<WeakMapping> mWeakMaps;
    uint32_t mRootCount;

    GCGraph() : mRootCount(0) {
    }
    ~GCGraph() {
    }

    void SizeOfExcludingThis(nsMallocSizeOfFun aMallocSizeOf,
                             size_t *aNodesSize, size_t *aEdgesSize) const {
        *aNodesSize = mNodes.SizeOfExcludingThis(aMallocSizeOf);
        *aEdgesSize = mEdges.SizeOfExcludingThis(aMallocSizeOf);

        // These fields are deliberately not measured:
        // - mWeakMaps entries, because the pointers are non-owning
    }
};

// XXX Would be nice to have an nsHashSet<KeyType> API that has
// Add/Remove/Has rather than PutEntry/RemoveEntry/GetEntry.
typedef nsTHashtable<nsPtrHashKey<const void> > PointerSet;

static nsISupports *
CanonicalizeXPCOMParticipant(nsISupports *in)
{
    nsISupports* out;
    in->QueryInterface(NS_GET_IID(nsCycleCollectionISupports),
                       reinterpret_cast<void**>(&out));
    return out;
}

static inline void
ToParticipant(nsISupports *s, nsXPCOMCycleCollectionParticipant **cp);

static void
CanonicalizeParticipant(void **parti, nsCycleCollectionParticipant **cp)
{
    // If the participant is null, this is an nsISupports participant,
    // so we must QI to get the real participant.

    if (!*cp) {
        nsISupports *nsparti = static_cast<nsISupports*>(*parti);
        nsparti = CanonicalizeXPCOMParticipant(nsparti);
        NS_ASSERTION(nsparti,
                     "Don't add objects that don't participate in collection!");
        nsXPCOMCycleCollectionParticipant *xcp;
        ToParticipant(nsparti, &xcp);
        *parti = nsparti;
        *cp = xcp;
    }
}

struct nsPurpleBuffer
{
private:
    struct Block {
        Block *mNext;
        // Try to match the size of a jemalloc bucket, to minimize slop bytes.
        // - On 32-bit platforms sizeof(nsPurpleBufferEntry) is 12, so mEntries
        //   is 16,380 bytes, which leaves 4 bytes for mNext.
        // - On 64-bit platforms sizeof(nsPurpleBufferEntry) is 24, so mEntries
        //   is 32,544 bytes, which leaves 8 bytes for mNext.
        nsPurpleBufferEntry mEntries[1365];

        Block() : mNext(nullptr) {
#ifndef DEBUG_CC
            // Ensure Block is the right size (see above).
            MOZ_STATIC_ASSERT(
                sizeof(Block) == 16384 ||       // 32-bit
                sizeof(Block) == 32768,         // 64-bit
                "ill-sized nsPurpleBuffer::Block"
            );
#endif
        }
        void StaticAsserts();
    };
public:
    // This class wraps a linked list of the elements in the purple
    // buffer.

    nsCycleCollectorParams &mParams;
    uint32_t mCount;
    Block mFirstBlock;
    nsPurpleBufferEntry *mFreeList;

#ifdef DEBUG_CC
    PointerSet mNormalObjects; // duplicates our blocks
    nsCycleCollectorStats &mStats;
#endif

#ifdef DEBUG_CC
    nsPurpleBuffer(nsCycleCollectorParams &params,
                   nsCycleCollectorStats &stats)
        : mParams(params),
          mStats(stats)
    {
        InitBlocks();
        mNormalObjects.Init();
    }
#else
    nsPurpleBuffer(nsCycleCollectorParams &params)
        : mParams(params)
    {
        InitBlocks();
    }
#endif

    ~nsPurpleBuffer()
    {
        FreeBlocks();
    }

    void InitBlocks()
    {
        mCount = 0;
        mFreeList = nullptr;
        StartBlock(&mFirstBlock);
    }

    void StartBlock(Block *aBlock)
    {
        NS_ABORT_IF_FALSE(!mFreeList, "should not have free list");

        // Put all the entries in the block on the free list.
        nsPurpleBufferEntry *entries = aBlock->mEntries;
        mFreeList = entries;
        for (uint32_t i = 1; i < ArrayLength(aBlock->mEntries); ++i) {
            entries[i - 1].mNextInFreeList =
                (nsPurpleBufferEntry*)(uintptr_t(entries + i) | 1);
        }
        entries[ArrayLength(aBlock->mEntries) - 1].mNextInFreeList =
            (nsPurpleBufferEntry*)1;
    }

    void FreeBlocks()
    {
        if (mCount > 0)
            UnmarkRemainingPurple(&mFirstBlock);
        Block *b = mFirstBlock.mNext;
        while (b) {
            if (mCount > 0)
                UnmarkRemainingPurple(b);
            Block *next = b->mNext;
            delete b;
            b = next;
        }
        mFirstBlock.mNext = nullptr;
    }

    void UnmarkRemainingPurple(Block *b)
    {
        for (nsPurpleBufferEntry *e = b->mEntries,
                              *eEnd = ArrayEnd(b->mEntries);
             e != eEnd; ++e) {
            if (!(uintptr_t(e->mObject) & uintptr_t(1))) {
                // This is a real entry (rather than something on the
                // free list).
                if (e->mObject) {
                    void *obj = e->mObject;
                    nsCycleCollectionParticipant *cp = e->mParticipant;
                    CanonicalizeParticipant(&obj, &cp);
                    cp->UnmarkIfPurple(obj);
                }

                if (--mCount == 0)
                    break;
            }
        }
    }

    void SelectPointers(GCGraphBuilder &builder);

    // RemoveSkippable removes entries from the purple buffer if
    // nsPurpleBufferEntry::mObject is null or if the object's
    // nsXPCOMCycleCollectionParticipant::CanSkip() returns true or
    // if nsPurpleBufferEntry::mNotPurple is true.
    // If removeChildlessNodes is true, then any nodes in the purple buffer
    // that will have no children in the cycle collector graph will also be
    // removed. CanSkip() may be run on these children.
    void RemoveSkippable(bool removeChildlessNodes);

#ifdef DEBUG_CC
    bool Exists(void *p) const
    {
        return mNormalObjects.GetEntry(p);
    }
#endif

    nsPurpleBufferEntry* NewEntry()
    {
        if (!mFreeList) {
            Block *b = new Block;
            if (!b) {
                return nullptr;
            }
            StartBlock(b);

            // Add the new block as the second block in the list.
            b->mNext = mFirstBlock.mNext;
            mFirstBlock.mNext = b;
        }

        nsPurpleBufferEntry *e = mFreeList;
        mFreeList = (nsPurpleBufferEntry*)
            (uintptr_t(mFreeList->mNextInFreeList) & ~uintptr_t(1));
        return e;
    }

    nsPurpleBufferEntry* Put(void *p, nsCycleCollectionParticipant *cp)
    {
        nsPurpleBufferEntry *e = NewEntry();
        if (!e) {
            return nullptr;
        }

        ++mCount;

        e->mObject = p;
        e->mParticipant = cp;
        e->mNotPurple = false;

        // Caller is responsible for filling in result's mRefCnt.
        return e;
    }

    void Remove(nsPurpleBufferEntry *e)
    {
        MOZ_ASSERT(mCount != 0, "must have entries");

#ifdef DEBUG_CC
        mNormalObjects.RemoveEntry(e->mObject);
#endif

        e->mNextInFreeList =
            (nsPurpleBufferEntry*)(uintptr_t(mFreeList) | uintptr_t(1));
        mFreeList = e;

        --mCount;
    }

    uint32_t Count() const
    {
        return mCount;
    }

    size_t SizeOfExcludingThis(nsMallocSizeOfFun aMallocSizeOf) const
    {
        size_t n = 0;

        // Don't measure mFirstBlock because it's within |this|.
        const Block *block = mFirstBlock.mNext;
        while (block) {
            n += aMallocSizeOf(block);
            block = block->mNext;
        }

        // These fields are deliberately not measured:
        // - mParams: because it only contains scalars.
        // - mFreeList: because it points into the purple buffer, which is
        //   within mFirstBlock and thus within |this|.
        // - mNormalObjects, mStats: because they're DEBUG_CC-only.
        //
        // We also don't measure the things pointed to by mEntries[] because
        // those pointers are non-owning.

        return n;
    }
};

static bool
AddPurpleRoot(GCGraphBuilder &builder, void *root, nsCycleCollectionParticipant *cp);

void
nsPurpleBuffer::SelectPointers(GCGraphBuilder &aBuilder)
{
#ifdef DEBUG_CC
    // Can't use mCount here, since it may include null entries.
    uint32_t realCount = 0;
    for (Block *b = &mFirstBlock; b; b = b->mNext) {
        for (nsPurpleBufferEntry *e = b->mEntries,
                              *eEnd = ArrayEnd(b->mEntries);
            e != eEnd; ++e) {
            if (!(uintptr_t(e->mObject) & uintptr_t(1))) {
                if (e->mObject && !e->mNotPurple) {
                    ++realCount;
                }
            }
        }
    }

    NS_ABORT_IF_FALSE(mNormalObjects.Count() == realCount,
                      "count out of sync");
#endif

    // Walk through all the blocks.
    for (Block *b = &mFirstBlock; b; b = b->mNext) {
        for (nsPurpleBufferEntry *e = b->mEntries,
                              *eEnd = ArrayEnd(b->mEntries);
            e != eEnd; ++e) {
            if (!(uintptr_t(e->mObject) & uintptr_t(1))) {
                // This is a real entry (rather than something on the
                // free list).
                if (e->mObject && e->mNotPurple) {
                    void* o = e->mObject;
                    nsCycleCollectionParticipant* cp = e->mParticipant;
                    CanonicalizeParticipant(&o, &cp);
                    cp->UnmarkIfPurple(o);
                    Remove(e);
                } else if (!e->mObject || AddPurpleRoot(aBuilder, e->mObject,
                                                        e->mParticipant)) {
                    Remove(e);
                }
            }
        }
    }

    NS_WARN_IF_FALSE(mCount == 0, "AddPurpleRoot failed");
    if (mCount == 0) {
        FreeBlocks();
        InitBlocks();
    }
}

////////////////////////////////////////////////////////////////////////
// Top level structure for the cycle collector.
////////////////////////////////////////////////////////////////////////

struct nsCycleCollector
{
    bool mCollectionInProgress;
    bool mScanInProgress;
    bool mFollowupCollection;
    nsCycleCollectorResults *mResults;
    TimeStamp mCollectionStart;

    nsCycleCollectionJSRuntime *mJSRuntime;

    GCGraph mGraph;

    nsCycleCollectorParams mParams;

    nsTArray<PtrInfo*> *mWhiteNodes;
    uint32_t mWhiteNodeCount;

    // mVisitedRefCounted and mVisitedGCed are only used for telemetry
    uint32_t mVisitedRefCounted;
    uint32_t mVisitedGCed;

    CC_BeforeUnlinkCallback mBeforeUnlinkCB;
    CC_ForgetSkippableCallback mForgetSkippableCB;

    nsPurpleBuffer mPurpleBuf;

    void RegisterJSRuntime(nsCycleCollectionJSRuntime *aJSRuntime);
    void ForgetJSRuntime();

    void SelectPurple(GCGraphBuilder &builder);
    void MarkRoots(GCGraphBuilder &builder);
    void ScanRoots();
    void ScanWeakMaps();

    void ForgetSkippable(bool removeChildlessNodes);

    // returns whether anything was collected
    bool CollectWhite(nsICycleCollectorListener *aListener);

    nsCycleCollector();
    ~nsCycleCollector();

    nsPurpleBufferEntry* Suspect2(void *n, nsCycleCollectionParticipant *cp);
    bool Forget2(nsPurpleBufferEntry *e);

    void Collect(bool aMergeCompartments,
                 nsCycleCollectorResults *aResults,
                 uint32_t aTryCollections,
                 nsICycleCollectorListener *aListener);

    // Prepare for and cleanup after one or more collection(s).
    bool PrepareForCollection(nsCycleCollectorResults *aResults,
                              nsTArray<PtrInfo*> *aWhiteNodes);
    void GCIfNeeded(bool aForceGC);
    void CleanupAfterCollection();

    // Start and finish an individual collection.
    bool BeginCollection(bool aMergeCompartments, nsICycleCollectorListener *aListener);
    bool FinishCollection(nsICycleCollectorListener *aListener);

    uint32_t SuspectedCount();
    void Shutdown();

    void ClearGraph()
    {
        mGraph.mNodes.Clear();
        mGraph.mEdges.Clear();
        mGraph.mWeakMaps.Clear();
        mGraph.mRootCount = 0;
    }

    void SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf,
                             size_t *aObjectSize,
                             size_t *aGraphNodesSize,
                             size_t *aGraphEdgesSize,
                             size_t *aWhiteNodeSize,
                             size_t *aPurpleBufferSize) const;

#ifdef DEBUG_CC
    nsCycleCollectorStats mStats;
    FILE *mPtrLog;
    PointerSet mExpectedGarbage;

    bool LogPurpleAddition(void* aObject, nsCycleCollectionParticipant *cp);
    void LogPurpleRemoval(void* aObject);

    void ShouldBeFreed(nsISupports *n);
    void WasFreed(nsISupports *n);
#endif
};


/**
 * GraphWalker is templatized over a Visitor class that must provide
 * the following two methods:
 *
 * bool ShouldVisitNode(PtrInfo const *pi);
 * void VisitNode(PtrInfo *pi);
 */
template <class Visitor>
class GraphWalker
{
private:
    Visitor mVisitor;

    void DoWalk(nsDeque &aQueue);

public:
    void Walk(PtrInfo *s0);
    void WalkFromRoots(GCGraph &aGraph);
    // copy-constructing the visitor should be cheap, and less
    // indirection than using a reference
    GraphWalker(const Visitor aVisitor) : mVisitor(aVisitor) {}
};


////////////////////////////////////////////////////////////////////////
// The static collector object
////////////////////////////////////////////////////////////////////////


static nsCycleCollector *sCollector = nullptr;
static nsIMemoryMultiReporter *sCollectorReporter = nullptr;


////////////////////////////////////////////////////////////////////////
// Utility functions
////////////////////////////////////////////////////////////////////////

MOZ_NEVER_INLINE static void
Fault(const char *msg, const void *ptr=nullptr)
{
    if (ptr)
        printf("Fault in cycle collector: %s (ptr: %p)\n", msg, ptr);
    else
        printf("Fault in cycle collector: %s\n", msg);

    NS_RUNTIMEABORT("cycle collector fault");
}

#ifdef DEBUG_CC
static void
Fault(const char *msg, PtrInfo *pi)
{
    printf("Fault in cycle collector: %s\n"
           "  while operating on pointer %p %s\n",
           msg, pi->mPointer, pi->mName);
    if (pi->mInternalRefs) {
        printf("  which has internal references from:\n");
        NodePool::Enumerator queue(sCollector->mGraph.mNodes);
        while (!queue.IsDone()) {
            PtrInfo *ppi = queue.GetNext();
            for (EdgePool::Iterator e = ppi->FirstChild(),
                                e_end = ppi->LastChild();
                 e != e_end; ++e) {
                if (*e == pi) {
                    printf("    %p %s\n", ppi->mPointer, ppi->mName);
                }
            }
        }
    }

    Fault(msg, pi->mPointer);
}
#else
static void
Fault(const char *msg, PtrInfo *pi)
{
    Fault(msg, pi->mPointer);
}
#endif

static inline void
AbortIfOffMainThreadIfCheckFast()
{
#if defined(XP_WIN) || defined(NS_TLS)
    if (!NS_IsMainThread() && !NS_IsCycleCollectorThread()) {
        NS_RUNTIMEABORT("Main-thread-only object used off the main thread");
    }
#endif
}

static inline void
ToParticipant(nsISupports *s, nsXPCOMCycleCollectionParticipant **cp)
{
    // We use QI to move from an nsISupports to an
    // nsXPCOMCycleCollectionParticipant, which is a per-class singleton helper
    // object that implements traversal and unlinking logic for the nsISupports
    // in question.
    CallQueryInterface(s, cp);
#ifdef DEBUG_CC
    if (cp)
        ++sCollector->mStats.mSuccessfulQI;
    else
        ++sCollector->mStats.mFailedQI;
#endif
}

template <class Visitor>
MOZ_NEVER_INLINE void
GraphWalker<Visitor>::Walk(PtrInfo *s0)
{
    nsDeque queue;
    CC_AbortIfNull(s0);
    queue.Push(s0);
    DoWalk(queue);
}

template <class Visitor>
MOZ_NEVER_INLINE void
GraphWalker<Visitor>::WalkFromRoots(GCGraph& aGraph)
{
    nsDeque queue;
    NodePool::Enumerator etor(aGraph.mNodes);
    for (uint32_t i = 0; i < aGraph.mRootCount; ++i) {
        PtrInfo *pi = etor.GetNext();
        CC_AbortIfNull(pi);
        queue.Push(pi);
    }
    DoWalk(queue);
}

template <class Visitor>
MOZ_NEVER_INLINE void
GraphWalker<Visitor>::DoWalk(nsDeque &aQueue)
{
    // Use a aQueue to match the breadth-first traversal used when we
    // built the graph, for hopefully-better locality.
    while (aQueue.GetSize() > 0) {
        PtrInfo *pi = static_cast<PtrInfo*>(aQueue.PopFront());
        CC_AbortIfNull(pi);

        if (mVisitor.ShouldVisitNode(pi)) {
            mVisitor.VisitNode(pi);
            for (EdgePool::Iterator child = pi->FirstChild(),
                                child_end = pi->LastChild();
                 child != child_end; ++child) {
                CC_AbortIfNull(*child);
                aQueue.Push(*child);
            }
        }
    };

#ifdef DEBUG_CC
    sCollector->mStats.mWalkedGraph++;
#endif
}

struct CCGraphDescriber
{
  CCGraphDescriber()
  : mAddress("0x"), mToAddress("0x"), mCnt(0), mType(eUnknown) {}

  enum Type
  {
    eRefCountedObject,
    eGCedObject,
    eGCMarkedObject,
    eEdge,
    eRoot,
    eGarbage,
    eUnknown
  };

  nsCString mAddress;
  nsCString mToAddress;
  nsCString mName;
  uint32_t mCnt;
  Type mType;
};

class nsCycleCollectorLogger MOZ_FINAL : public nsICycleCollectorListener
{
public:
    nsCycleCollectorLogger() :
      mStream(nullptr), mWantAllTraces(false),
      mDisableLog(false), mWantAfterProcessing(false),
      mNextIndex(0)
    {
    }
    ~nsCycleCollectorLogger()
    {
        if (mStream) {
            fclose(mStream);
        }
    }
    NS_DECL_ISUPPORTS

    void SetAllTraces()
    {
        mWantAllTraces = true;
    }

    NS_IMETHOD AllTraces(nsICycleCollectorListener** aListener)
    {
        SetAllTraces();
        NS_ADDREF(*aListener = this);
        return NS_OK;
    }

    NS_IMETHOD GetWantAllTraces(bool* aAllTraces)
    {
        *aAllTraces = mWantAllTraces;
        return NS_OK;
    }

    NS_IMETHOD GetDisableLog(bool* aDisableLog)
    {
        *aDisableLog = mDisableLog;
        return NS_OK;
    }

    NS_IMETHOD SetDisableLog(bool aDisableLog)
    {
        mDisableLog = aDisableLog;
        return NS_OK;
    }

    NS_IMETHOD GetWantAfterProcessing(bool* aWantAfterProcessing)
    {
        *aWantAfterProcessing = mWantAfterProcessing;
        return NS_OK;
    }

    NS_IMETHOD SetWantAfterProcessing(bool aWantAfterProcessing)
    {
        mWantAfterProcessing = aWantAfterProcessing;
        return NS_OK;
    }

    NS_IMETHOD GetFilenameIdentifier(nsAString& aIdentifier)
    {
        aIdentifier = mFilenameIdentifier;
        return NS_OK;
    }

    NS_IMETHOD SetFilenameIdentifier(const nsAString& aIdentifier)
    {
        mFilenameIdentifier = aIdentifier;
        return NS_OK;
    }

    NS_IMETHOD Begin()
    {
        mCurrentAddress.AssignLiteral("0x");
        mDescribers.Clear();
        mNextIndex = 0;
        if (mDisableLog) {
            return NS_OK;
        }

        // Initially create the log in a file starting with
        // "incomplete-gc-edges".  We'll move the file and strip off the
        // "incomplete-" once the dump completes.  (We do this because we don't
        // want scripts which poll the filesystem looking for gc/cc dumps to
        // grab a file before we're finished writing to it.)
        nsCOMPtr<nsIFile> gcLogFile = CreateTempFile("incomplete-gc-edges");
        NS_ENSURE_STATE(gcLogFile);

        // Dump the JS heap.
        FILE* gcLogANSIFile = nullptr;
        gcLogFile->OpenANSIFileDesc("w", &gcLogANSIFile);
        NS_ENSURE_STATE(gcLogANSIFile);
        xpc::DumpJSHeap(gcLogANSIFile);
        fclose(gcLogANSIFile);

        // Strip off "incomplete-".
        nsCOMPtr<nsIFile> gcLogFileFinalDestination =
            CreateTempFile("gc-edges");
        NS_ENSURE_STATE(gcLogFileFinalDestination);

        nsAutoString gcLogFileFinalDestinationName;
        gcLogFileFinalDestination->GetLeafName(gcLogFileFinalDestinationName);
        NS_ENSURE_STATE(!gcLogFileFinalDestinationName.IsEmpty());

        gcLogFile->MoveTo(/* directory */ nullptr, gcLogFileFinalDestinationName);

        // Log to the error console.
        nsCOMPtr<nsIConsoleService> cs =
            do_GetService(NS_CONSOLESERVICE_CONTRACTID);
        if (cs) {
            nsAutoString gcLogPath;
            gcLogFileFinalDestination->GetPath(gcLogPath);

            nsString msg = NS_LITERAL_STRING("Garbage Collector log dumped to ") +
                           gcLogPath;
            cs->LogStringMessage(msg.get());
        }

        // Open a file for dumping the CC graph.  We again prefix with
        // "incomplete-".
        mOutFile = CreateTempFile("incomplete-cc-edges");
        NS_ENSURE_STATE(mOutFile);
        MOZ_ASSERT(!mStream);
        mOutFile->OpenANSIFileDesc("w", &mStream);
        NS_ENSURE_STATE(mStream);

        return NS_OK;
    }
    NS_IMETHOD NoteRefCountedObject(uint64_t aAddress, uint32_t refCount,
                                    const char *aObjectDescription)
    {
        if (!mDisableLog) {
            fprintf(mStream, "%p [rc=%u] %s\n", (void*)aAddress, refCount,
                    aObjectDescription);
        }                          
        if (mWantAfterProcessing) {
            CCGraphDescriber* d = mDescribers.AppendElement();
            NS_ENSURE_TRUE(d, NS_ERROR_OUT_OF_MEMORY);
            mCurrentAddress.AssignLiteral("0x");
            mCurrentAddress.AppendInt(aAddress, 16);
            d->mType = CCGraphDescriber::eRefCountedObject;
            d->mAddress = mCurrentAddress;
            d->mCnt = refCount;
            d->mName.Append(aObjectDescription);
        }                     
        return NS_OK;
    }
    NS_IMETHOD NoteGCedObject(uint64_t aAddress, bool aMarked,
                              const char *aObjectDescription)
    {
        if (!mDisableLog) {
            fprintf(mStream, "%p [gc%s] %s\n", (void*)aAddress,
                    aMarked ? ".marked" : "", aObjectDescription);
        }
        if (mWantAfterProcessing) {
            CCGraphDescriber* d = mDescribers.AppendElement();
            NS_ENSURE_TRUE(d, NS_ERROR_OUT_OF_MEMORY);
            mCurrentAddress.AssignLiteral("0x");
            mCurrentAddress.AppendInt(aAddress, 16);
            d->mType = aMarked ? CCGraphDescriber::eGCMarkedObject :
                                 CCGraphDescriber::eGCedObject;
            d->mAddress = mCurrentAddress;
            d->mName.Append(aObjectDescription);
        }
        return NS_OK;
    }
    NS_IMETHOD NoteEdge(uint64_t aToAddress, const char *aEdgeName)
    {
        if (!mDisableLog) {
            fprintf(mStream, "> %p %s\n", (void*)aToAddress, aEdgeName);
        }
        if (mWantAfterProcessing) {
            CCGraphDescriber* d = mDescribers.AppendElement();
            NS_ENSURE_TRUE(d, NS_ERROR_OUT_OF_MEMORY);
            d->mType = CCGraphDescriber::eEdge;
            d->mAddress = mCurrentAddress;
            d->mToAddress.AppendInt(aToAddress, 16);
            d->mName.Append(aEdgeName);
        }
        return NS_OK;
    }
    NS_IMETHOD BeginResults()
    {
        if (!mDisableLog) {
            fputs("==========\n", mStream);
        }                     
        return NS_OK;
    }
    NS_IMETHOD DescribeRoot(uint64_t aAddress, uint32_t aKnownEdges)
    {
        if (!mDisableLog) {
            fprintf(mStream, "%p [known=%u]\n", (void*)aAddress, aKnownEdges);
        }
        if (mWantAfterProcessing) {
            CCGraphDescriber* d = mDescribers.AppendElement();
            NS_ENSURE_TRUE(d, NS_ERROR_OUT_OF_MEMORY);
            d->mType = CCGraphDescriber::eRoot;
            d->mAddress.AppendInt(aAddress, 16);
            d->mCnt = aKnownEdges;
        }
        return NS_OK;
    }
    NS_IMETHOD DescribeGarbage(uint64_t aAddress)
    {
        if (!mDisableLog) {
            fprintf(mStream, "%p [garbage]\n", (void*)aAddress);
        }
        if (mWantAfterProcessing) {
            CCGraphDescriber* d = mDescribers.AppendElement();
            NS_ENSURE_TRUE(d, NS_ERROR_OUT_OF_MEMORY);
            d->mType = CCGraphDescriber::eGarbage;
            d->mAddress.AppendInt(aAddress, 16);
        }
        return NS_OK;
    }
    NS_IMETHOD End()
    {
        if (!mDisableLog) {
            MOZ_ASSERT(mStream);
            MOZ_ASSERT(mOutFile);

            fclose(mStream);
            mStream = nullptr;

            // Strip off "incomplete-" from the log file's name.
            nsCOMPtr<nsIFile> logFileFinalDestination =
                CreateTempFile("cc-edges");
            NS_ENSURE_STATE(logFileFinalDestination);

            nsAutoString logFileFinalDestinationName;
            logFileFinalDestination->GetLeafName(logFileFinalDestinationName);
            NS_ENSURE_STATE(!logFileFinalDestinationName.IsEmpty());

            mOutFile->MoveTo(/* directory = */ nullptr,
                             logFileFinalDestinationName);
            mOutFile = nullptr;

            // Log to the error console.
            nsCOMPtr<nsIConsoleService> cs =
                do_GetService(NS_CONSOLESERVICE_CONTRACTID);
            if (cs) {
                nsAutoString ccLogPath;
                logFileFinalDestination->GetPath(ccLogPath);

                nsString msg = NS_LITERAL_STRING("Cycle Collector log dumped to ") +
                               ccLogPath;
                cs->LogStringMessage(msg.get());
            }
        }
        return NS_OK;
    }
    NS_IMETHOD ProcessNext(nsICycleCollectorHandler* aHandler,
                           bool* aCanContinue)
    {
        NS_ENSURE_STATE(aHandler && mWantAfterProcessing);
        if (mNextIndex < mDescribers.Length()) {
            CCGraphDescriber& d = mDescribers[mNextIndex++];
            switch (d.mType) {
                case CCGraphDescriber::eRefCountedObject:
                    aHandler->NoteRefCountedObject(d.mAddress,
                                                   d.mCnt,
                                                   d.mName);
                    break;
                case CCGraphDescriber::eGCedObject:
                case CCGraphDescriber::eGCMarkedObject:
                    aHandler->NoteGCedObject(d.mAddress,
                                             d.mType ==
                                               CCGraphDescriber::eGCMarkedObject,
                                             d.mName);
                    break;
                case CCGraphDescriber::eEdge:
                    aHandler->NoteEdge(d.mAddress,
                                       d.mToAddress,
                                       d.mName);
                    break;
                case CCGraphDescriber::eRoot:
                    aHandler->DescribeRoot(d.mAddress,
                                           d.mCnt);
                    break;
                case CCGraphDescriber::eGarbage:
                    aHandler->DescribeGarbage(d.mAddress);
                    break;
                case CCGraphDescriber::eUnknown:
                    NS_NOTREACHED("CCGraphDescriber::eUnknown");
                    break;
            }
        }
        if (!(*aCanContinue = mNextIndex < mDescribers.Length())) {
            mCurrentAddress.AssignLiteral("0x");
            mDescribers.Clear();
            mNextIndex = 0;
        }
        return NS_OK;
    }
private:
    /**
     * Create a new file named something like aPrefix.$PID.$IDENTIFIER.log in
     * $MOZ_CC_LOG_DIRECTORY or in the system's temp directory.  No existing
     * file will be overwritten; if aPrefix.$PID.$IDENTIFIER.log exists, we'll
     * try a file named something like aPrefix.$PID.$IDENTIFIER-1.log, and so
     * on.
     */
    already_AddRefed<nsIFile>
    CreateTempFile(const char* aPrefix)
    {
        nsPrintfCString filename("%s.%d%s%s.log",
            aPrefix,
            base::GetCurrentProcId(),
            mFilenameIdentifier.IsEmpty() ? "" : ".",
            NS_ConvertUTF16toUTF8(mFilenameIdentifier).get());

        // Get the log directory either from $MOZ_CC_LOG_DIRECTORY or from our
        // platform's temp directory.
        nsCOMPtr<nsIFile> logFile;
        if (char* env = PR_GetEnv("MOZ_CC_LOG_DIRECTORY")) {
            NS_NewNativeLocalFile(nsCString(env), /* followLinks = */ true,
                                  getter_AddRefs(logFile));
        } else {
            // Ask NSPR to point us to the temp directory.
            NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(logFile));
        }
        NS_ENSURE_TRUE(logFile, nullptr);

        nsresult rv = logFile->AppendNative(filename);
        NS_ENSURE_SUCCESS(rv, nullptr);

        rv = logFile->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0644);
        NS_ENSURE_SUCCESS(rv, nullptr);
#ifdef ANDROID
        {
            // On android the default system umask is 0077 which makes these files
            // unreadable to the shell user. In order to pull the dumps off a non-rooted
            // device we need to chmod them to something world-readable.
            // XXX why not logFile->SetPermissions(0644);
            nsAutoCString path;
            rv = logFile->GetNativePath(path);
            if (NS_SUCCEEDED(rv)) {
                chmod(path.get(), 0644);
            }
        }
#endif

        return logFile.forget();
    }

    FILE *mStream;
    nsCOMPtr<nsIFile> mOutFile;
    bool mWantAllTraces;
    bool mDisableLog;
    bool mWantAfterProcessing;
    nsString mFilenameIdentifier;
    nsCString mCurrentAddress; 
    nsTArray<CCGraphDescriber> mDescribers;
    uint32_t mNextIndex;
};

NS_IMPL_ISUPPORTS1(nsCycleCollectorLogger, nsICycleCollectorListener)

nsresult
nsCycleCollectorLoggerConstructor(nsISupports* aOuter,
                                  const nsIID& aIID,
                                  void* *aInstancePtr)
{
    NS_ENSURE_TRUE(!aOuter, NS_ERROR_NO_AGGREGATION);

    nsISupports *logger = new nsCycleCollectorLogger();

    return logger->QueryInterface(aIID, aInstancePtr);
}

////////////////////////////////////////////////////////////////////////
// Bacon & Rajan's |MarkRoots| routine.
////////////////////////////////////////////////////////////////////////

struct PtrToNodeEntry : public PLDHashEntryHdr
{
    // The key is mNode->mPointer
    PtrInfo *mNode;
};

static bool
PtrToNodeMatchEntry(PLDHashTable *table,
                    const PLDHashEntryHdr *entry,
                    const void *key)
{
    const PtrToNodeEntry *n = static_cast<const PtrToNodeEntry*>(entry);
    return n->mNode->mPointer == key;
}

static PLDHashTableOps PtrNodeOps = {
    PL_DHashAllocTable,
    PL_DHashFreeTable,
    PL_DHashVoidPtrKeyStub,
    PtrToNodeMatchEntry,
    PL_DHashMoveEntryStub,
    PL_DHashClearEntryStub,
    PL_DHashFinalizeStub,
    nullptr
};

class GCGraphBuilder : public nsCycleCollectionTraversalCallback
{
private:
    NodePool::Builder mNodeBuilder;
    EdgePool::Builder mEdgeBuilder;
    nsTArray<WeakMapping> &mWeakMaps;
    PLDHashTable mPtrToNodeMap;
    PtrInfo *mCurrPi;
    nsCycleCollectionParticipant *mJSParticipant;
    nsCycleCollectionParticipant *mJSCompParticipant;
    nsCString mNextEdgeName;
    nsICycleCollectorListener *mListener;
    bool mMergeCompartments;

public:
    GCGraphBuilder(GCGraph &aGraph,
                   nsCycleCollectionJSRuntime *aJSRuntime,
                   nsICycleCollectorListener *aListener,
                   bool aMergeCompartments);
    ~GCGraphBuilder();
    bool Initialized();

    uint32_t Count() const { return mPtrToNodeMap.entryCount; }

    PtrInfo* AddNode(void *s, nsCycleCollectionParticipant *aParticipant);
    PtrInfo* AddWeakMapNode(void* node);
    void Traverse(PtrInfo* aPtrInfo);
    void SetLastChild();

private:
    void DescribeNode(uint32_t refCount, const char *objName)
    {
        mCurrPi->mRefCount = refCount;
#ifdef DEBUG_CC
        mCurrPi->mName = PL_strdup(objName);
        sCollector->mStats.mVisitedNode++;
#endif
    }

public:
    // nsCycleCollectionTraversalCallback methods.
    NS_IMETHOD_(void) DescribeRefCountedNode(nsrefcnt refCount,
                                             const char *objName);
    NS_IMETHOD_(void) DescribeGCedNode(bool isMarked, const char *objName);

    NS_IMETHOD_(void) NoteXPCOMRoot(nsISupports *root);
    NS_IMETHOD_(void) NoteJSRoot(void *root);
    NS_IMETHOD_(void) NoteNativeRoot(void *root, nsCycleCollectionParticipant *participant);

    NS_IMETHOD_(void) NoteXPCOMChild(nsISupports *child);
    NS_IMETHOD_(void) NoteJSChild(void *child);
    NS_IMETHOD_(void) NoteNativeChild(void *child,
                                      nsCycleCollectionParticipant *participant);

    NS_IMETHOD_(void) NoteNextEdgeName(const char* name);
    NS_IMETHOD_(void) NoteWeakMapping(void *map, void *key, void *kdelegate, void *val);

private:
    NS_IMETHOD_(void) NoteRoot(void *root,
                               nsCycleCollectionParticipant *participant)
    {
        MOZ_ASSERT(root);
        MOZ_ASSERT(participant);

        if (!participant->CanSkipInCC(root) || MOZ_UNLIKELY(WantAllTraces())) {
            AddNode(root, participant);
        }
    }

    NS_IMETHOD_(void) NoteChild(void *child, nsCycleCollectionParticipant *cp,
                                nsCString edgeName)
    {
        PtrInfo *childPi = AddNode(child, cp);
        if (!childPi)
            return;
        mEdgeBuilder.Add(childPi);
        if (mListener) {
            mListener->NoteEdge((uint64_t)child, edgeName.get());
        }
        ++childPi->mInternalRefs;
    }

    JSCompartment *MergeCompartment(void *gcthing) {
        if (!mMergeCompartments) {
            return nullptr;
        }
        JSCompartment *comp = js::GetGCThingCompartment(gcthing);
        if (js::IsSystemCompartment(comp)) {
            return nullptr;
        }
        return comp;
    }
};

GCGraphBuilder::GCGraphBuilder(GCGraph &aGraph,
                               nsCycleCollectionJSRuntime *aJSRuntime,
                               nsICycleCollectorListener *aListener,
                               bool aMergeCompartments)
    : mNodeBuilder(aGraph.mNodes),
      mEdgeBuilder(aGraph.mEdges),
      mWeakMaps(aGraph.mWeakMaps),
      mJSParticipant(nullptr),
      mJSCompParticipant(xpc_JSCompartmentParticipant()),
      mListener(aListener),
      mMergeCompartments(aMergeCompartments)
{
    if (!PL_DHashTableInit(&mPtrToNodeMap, &PtrNodeOps, nullptr,
                           sizeof(PtrToNodeEntry), 32768))
        mPtrToNodeMap.ops = nullptr;

    if (aJSRuntime) {
        mJSParticipant = aJSRuntime->GetParticipant();
    }

    uint32_t flags = 0;
#ifdef DEBUG_CC
    flags = nsCycleCollectionTraversalCallback::WANT_DEBUG_INFO |
            nsCycleCollectionTraversalCallback::WANT_ALL_TRACES;
#endif
    if (!flags && mListener) {
        flags = nsCycleCollectionTraversalCallback::WANT_DEBUG_INFO;
        bool all = false;
        mListener->GetWantAllTraces(&all);
        if (all) {
            flags |= nsCycleCollectionTraversalCallback::WANT_ALL_TRACES;
        }
    }

    mFlags |= flags;

    mMergeCompartments = mMergeCompartments && MOZ_LIKELY(!WantAllTraces());
}

GCGraphBuilder::~GCGraphBuilder()
{
    if (mPtrToNodeMap.ops)
        PL_DHashTableFinish(&mPtrToNodeMap);
}

bool
GCGraphBuilder::Initialized()
{
    return !!mPtrToNodeMap.ops;
}

PtrInfo*
GCGraphBuilder::AddNode(void *s, nsCycleCollectionParticipant *aParticipant)
{
    PtrToNodeEntry *e = static_cast<PtrToNodeEntry*>(PL_DHashTableOperate(&mPtrToNodeMap, s, PL_DHASH_ADD));
    if (!e)
        return nullptr;

    PtrInfo *result;
    if (!e->mNode) {
        // New entry.
        result = mNodeBuilder.Add(s, aParticipant);
        if (!result) {
            PL_DHashTableRawRemove(&mPtrToNodeMap, e);
            return nullptr;
        }
        e->mNode = result;
    } else {
        result = e->mNode;
        MOZ_ASSERT(result->mParticipant == aParticipant,
                   "nsCycleCollectionParticipant shouldn't change!");
    }
    return result;
}

MOZ_NEVER_INLINE void
GCGraphBuilder::Traverse(PtrInfo* aPtrInfo)
{
    mCurrPi = aPtrInfo;

#ifdef DEBUG_CC
    if (!mCurrPi->mParticipant) {
        Fault("unknown pointer during walk", aPtrInfo);
        return;
    }
#endif

    mCurrPi->SetFirstChild(mEdgeBuilder.Mark());

    nsresult rv = aPtrInfo->mParticipant->Traverse(aPtrInfo->mPointer, *this);
    if (NS_FAILED(rv)) {
        Fault("script pointer traversal failed", aPtrInfo);
    }
}

void
GCGraphBuilder::SetLastChild()
{
    mCurrPi->SetLastChild(mEdgeBuilder.Mark());
}

NS_IMETHODIMP_(void)
GCGraphBuilder::NoteXPCOMRoot(nsISupports *root)
{
    root = CanonicalizeXPCOMParticipant(root);
    NS_ASSERTION(root,
                 "Don't add objects that don't participate in collection!");

#ifdef DEBUG_CC
    if (nsCycleCollector_shouldSuppress(root))
        return;
#endif

    nsXPCOMCycleCollectionParticipant *cp;
    ToParticipant(root, &cp);

    NoteRoot(root, cp);
}

NS_IMETHODIMP_(void)
GCGraphBuilder::NoteJSRoot(void *root)
{
    if (JSCompartment *comp = MergeCompartment(root)) {
        NoteRoot(comp, mJSCompParticipant);
    } else {
        NoteRoot(root, mJSParticipant);
    }
}

NS_IMETHODIMP_(void)
GCGraphBuilder::NoteNativeRoot(void *root, nsCycleCollectionParticipant *participant)
{
    NoteRoot(root, participant);
}

NS_IMETHODIMP_(void)
GCGraphBuilder::DescribeRefCountedNode(nsrefcnt refCount, const char *objName)
{
    if (refCount == 0)
        Fault("zero refcount", mCurrPi);
    if (refCount == UINT32_MAX)
        Fault("overflowing refcount", mCurrPi);
    sCollector->mVisitedRefCounted++;

    if (mListener) {
        mListener->NoteRefCountedObject((uint64_t)mCurrPi->mPointer, refCount,
                                        objName);
    }

    DescribeNode(refCount, objName);
}

NS_IMETHODIMP_(void)
GCGraphBuilder::DescribeGCedNode(bool isMarked, const char *objName)
{
    uint32_t refCount = isMarked ? UINT32_MAX : 0;
    sCollector->mVisitedGCed++;

    if (mListener) {
        mListener->NoteGCedObject((uint64_t)mCurrPi->mPointer, isMarked,
                                  objName);
    }

    DescribeNode(refCount, objName);
}

NS_IMETHODIMP_(void)
GCGraphBuilder::NoteXPCOMChild(nsISupports *child) 
{
    nsCString edgeName;
    if (WantDebugInfo()) {
        edgeName.Assign(mNextEdgeName);
        mNextEdgeName.Truncate();
    }
    if (!child || !(child = CanonicalizeXPCOMParticipant(child)))
        return;

#ifdef DEBUG_CC
    if (nsCycleCollector_shouldSuppress(child))
        return;
#endif

    nsXPCOMCycleCollectionParticipant *cp;
    ToParticipant(child, &cp);
    if (cp && (!cp->CanSkipThis(child) || WantAllTraces())) {
        NoteChild(child, cp, edgeName);
    }
}

NS_IMETHODIMP_(void)
GCGraphBuilder::NoteNativeChild(void *child,
                                nsCycleCollectionParticipant *participant)
{
    nsCString edgeName;
    if (WantDebugInfo()) {
        edgeName.Assign(mNextEdgeName);
        mNextEdgeName.Truncate();
    }
    if (!child)
        return;

    MOZ_ASSERT(participant, "Need a nsCycleCollectionParticipant!");
    NoteChild(child, participant, edgeName);
}

NS_IMETHODIMP_(void)
GCGraphBuilder::NoteJSChild(void *child) 
{
    if (!child) {
        return;
    }

    nsCString edgeName;
    if (MOZ_UNLIKELY(WantDebugInfo())) {
        edgeName.Assign(mNextEdgeName);
        mNextEdgeName.Truncate();
    }

    if (xpc_GCThingIsGrayCCThing(child) || MOZ_UNLIKELY(WantAllTraces())) {
        if (JSCompartment *comp = MergeCompartment(child)) {
            NoteChild(comp, mJSCompParticipant, edgeName);
        } else {
            NoteChild(child, mJSParticipant, edgeName);
        }
    }
}

NS_IMETHODIMP_(void)
GCGraphBuilder::NoteNextEdgeName(const char* name)
{
    if (WantDebugInfo()) {
        mNextEdgeName = name;
    }
}

PtrInfo*
GCGraphBuilder::AddWeakMapNode(void *node)
{
    MOZ_ASSERT(node, "Weak map node should be non-null.");

    if (!xpc_GCThingIsGrayCCThing(node) && !WantAllTraces())
        return nullptr;

    if (JSCompartment *comp = MergeCompartment(node)) {
        return AddNode(comp, mJSCompParticipant);
    } else {
        return AddNode(node, mJSParticipant);
    }
}

NS_IMETHODIMP_(void)
GCGraphBuilder::NoteWeakMapping(void *map, void *key, void *kdelegate, void *val)
{
    // Don't try to optimize away the entry here, as we've already attempted to
    // do that in TraceWeakMapping in nsXPConnect.
    WeakMapping *mapping = mWeakMaps.AppendElement();
    mapping->mMap = map ? AddWeakMapNode(map) : nullptr;
    mapping->mKey = key ? AddWeakMapNode(key) : nullptr;
    mapping->mKeyDelegate = kdelegate ? AddWeakMapNode(kdelegate) : mapping->mKey;
    mapping->mVal = val ? AddWeakMapNode(val) : nullptr;
}

static bool
AddPurpleRoot(GCGraphBuilder &builder, void *root, nsCycleCollectionParticipant *cp)
{
    CanonicalizeParticipant(&root, &cp);

    if (builder.WantAllTraces() || !cp->CanSkipInCC(root)) {
        PtrInfo *pinfo = builder.AddNode(root, cp);
        if (!pinfo) {
            return false;
        }
    }

    cp->UnmarkIfPurple(root);

    return true;
}

// MayHaveChild() will be false after a Traverse if the object does
// not have any children the CC will visit.
class ChildFinder : public nsCycleCollectionTraversalCallback
{
public:
    ChildFinder() : mMayHaveChild(false) {}

    // The logic of the Note*Child functions must mirror that of their
    // respective functions in GCGraphBuilder.
    NS_IMETHOD_(void) NoteXPCOMChild(nsISupports *child);
    NS_IMETHOD_(void) NoteNativeChild(void *child,
                                      nsCycleCollectionParticipant *helper);
    NS_IMETHOD_(void) NoteJSChild(void *child);

    NS_IMETHOD_(void) DescribeRefCountedNode(nsrefcnt refcount,
                                             const char *objname) {}
    NS_IMETHOD_(void) DescribeGCedNode(bool ismarked,
                                       const char *objname) {}
    NS_IMETHOD_(void) NoteXPCOMRoot(nsISupports *root) {}
    NS_IMETHOD_(void) NoteJSRoot(void *root) {}
    NS_IMETHOD_(void) NoteNativeRoot(void *root,
                                     nsCycleCollectionParticipant *helper) {}
    NS_IMETHOD_(void) NoteNextEdgeName(const char* name) {}
    NS_IMETHOD_(void) NoteWeakMapping(void *map, void *key, void *kdelegate, void *val) {}
    bool MayHaveChild() {
        return mMayHaveChild;
    }
private:
    bool mMayHaveChild;
};

NS_IMETHODIMP_(void)
ChildFinder::NoteXPCOMChild(nsISupports *child)
{
    if (!child || !(child = CanonicalizeXPCOMParticipant(child)))
        return;
    nsXPCOMCycleCollectionParticipant *cp;
    ToParticipant(child, &cp);
    if (cp && !cp->CanSkip(child, true))
        mMayHaveChild = true;
}

NS_IMETHODIMP_(void)
ChildFinder::NoteNativeChild(void *child,
                             nsCycleCollectionParticipant *helper)
{
    if (child)
        mMayHaveChild = true;
}

NS_IMETHODIMP_(void)
ChildFinder::NoteJSChild(void *child)
{
    if (child && xpc_GCThingIsGrayCCThing(child)) {
        mMayHaveChild = true;
    }
}

static bool
MayHaveChild(void *o, nsCycleCollectionParticipant* cp)
{
    ChildFinder cf;
    cp->Traverse(o, cf);
    return cf.MayHaveChild();
}

void
nsPurpleBuffer::RemoveSkippable(bool removeChildlessNodes)
{
    // Walk through all the blocks.
    for (Block *b = &mFirstBlock; b; b = b->mNext) {
        for (nsPurpleBufferEntry *e = b->mEntries,
                              *eEnd = ArrayEnd(b->mEntries);
            e != eEnd; ++e) {
            if (!(uintptr_t(e->mObject) & uintptr_t(1))) {
                // This is a real entry (rather than something on the
                // free list).
                if (e->mObject) {
                    void *o = e->mObject;
                    nsCycleCollectionParticipant *cp = e->mParticipant;
                    CanonicalizeParticipant(&o, &cp);
                    if (!e->mNotPurple && !cp->CanSkip(o, false) &&
                        (!removeChildlessNodes || MayHaveChild(o, cp))) {
                        continue;
                    }
                    cp->UnmarkIfPurple(o);
                }
                Remove(e);
            }
        }
    }
}

void 
nsCycleCollector::SelectPurple(GCGraphBuilder &builder)
{
    mPurpleBuf.SelectPointers(builder);
}

void 
nsCycleCollector::ForgetSkippable(bool removeChildlessNodes)
{
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (obs) {
        obs->NotifyObservers(nullptr, "cycle-collector-forget-skippable", nullptr);
    }
    mPurpleBuf.RemoveSkippable(removeChildlessNodes);
    if (mForgetSkippableCB) {
        mForgetSkippableCB();
    }
}

MOZ_NEVER_INLINE void
nsCycleCollector::MarkRoots(GCGraphBuilder &builder)
{
    mGraph.mRootCount = builder.Count();

    // read the PtrInfo out of the graph that we are building
    NodePool::Enumerator queue(mGraph.mNodes);
    while (!queue.IsDone()) {
        PtrInfo *pi = queue.GetNext();
        CC_AbortIfNull(pi);
        builder.Traverse(pi);
        if (queue.AtBlockEnd())
            builder.SetLastChild();
    }
    if (mGraph.mRootCount > 0)
        builder.SetLastChild();
}


////////////////////////////////////////////////////////////////////////
// Bacon & Rajan's |ScanRoots| routine.
////////////////////////////////////////////////////////////////////////


struct ScanBlackVisitor
{
    ScanBlackVisitor(uint32_t &aWhiteNodeCount)
        : mWhiteNodeCount(aWhiteNodeCount)
    {
    }

    bool ShouldVisitNode(PtrInfo const *pi)
    { 
        return pi->mColor != black;
    }

    MOZ_NEVER_INLINE void VisitNode(PtrInfo *pi)
    {
        if (pi->mColor == white)
            --mWhiteNodeCount;
        pi->mColor = black;
#ifdef DEBUG_CC
        sCollector->mStats.mSetColorBlack++;
#endif
    }

    uint32_t &mWhiteNodeCount;
};


struct scanVisitor
{
    scanVisitor(uint32_t &aWhiteNodeCount) : mWhiteNodeCount(aWhiteNodeCount)
    {
    }

    bool ShouldVisitNode(PtrInfo const *pi)
    { 
        return pi->mColor == grey;
    }

    MOZ_NEVER_INLINE void VisitNode(PtrInfo *pi)
    {
        if (pi->mInternalRefs > pi->mRefCount && pi->mRefCount > 0)
            Fault("traversed refs exceed refcount", pi);

        if (pi->mInternalRefs == pi->mRefCount || pi->mRefCount == 0) {
            pi->mColor = white;
            ++mWhiteNodeCount;
#ifdef DEBUG_CC
            sCollector->mStats.mSetColorWhite++;
#endif
        } else {
            GraphWalker<ScanBlackVisitor>(ScanBlackVisitor(mWhiteNodeCount)).Walk(pi);
            MOZ_ASSERT(pi->mColor == black,
                       "Why didn't ScanBlackVisitor make pi black?");
        }
    }

    uint32_t &mWhiteNodeCount;
};

// Iterate over the WeakMaps.  If we mark anything while iterating
// over the WeakMaps, we must iterate over all of the WeakMaps again.
void
nsCycleCollector::ScanWeakMaps()
{
    bool anyChanged;
    do {
        anyChanged = false;
        for (uint32_t i = 0; i < mGraph.mWeakMaps.Length(); i++) {
            WeakMapping *wm = &mGraph.mWeakMaps[i];

            // If any of these are null, the original object was marked black.
            uint32_t mColor = wm->mMap ? wm->mMap->mColor : black;
            uint32_t kColor = wm->mKey ? wm->mKey->mColor : black;
            uint32_t kdColor = wm->mKeyDelegate ? wm->mKeyDelegate->mColor : black;
            uint32_t vColor = wm->mVal ? wm->mVal->mColor : black;

            // All non-null weak mapping maps, keys and values are
            // roots (in the sense of WalkFromRoots) in the cycle
            // collector graph, and thus should have been colored
            // either black or white in ScanRoots().
            MOZ_ASSERT(mColor != grey, "Uncolored weak map");
            MOZ_ASSERT(kColor != grey, "Uncolored weak map key");
            MOZ_ASSERT(kdColor != grey, "Uncolored weak map key delegate");
            MOZ_ASSERT(vColor != grey, "Uncolored weak map value");

            if (mColor == black && kColor != black && kdColor == black) {
                GraphWalker<ScanBlackVisitor>(ScanBlackVisitor(mWhiteNodeCount)).Walk(wm->mKey);
                anyChanged = true;
            }

            if (mColor == black && kColor == black && vColor != black) {
                GraphWalker<ScanBlackVisitor>(ScanBlackVisitor(mWhiteNodeCount)).Walk(wm->mVal);
                anyChanged = true;
            }
        }
    } while (anyChanged);
}

void
nsCycleCollector::ScanRoots()
{
    mWhiteNodeCount = 0;

    // On the assumption that most nodes will be black, it's
    // probably faster to use a GraphWalker than a
    // NodePool::Enumerator.
    GraphWalker<scanVisitor>(scanVisitor(mWhiteNodeCount)).WalkFromRoots(mGraph); 

    ScanWeakMaps();

#ifdef DEBUG_CC
    // Sanity check: scan should have colored all grey nodes black or
    // white. So we ensure we have no grey nodes at this point.
    NodePool::Enumerator etor(mGraph.mNodes);
    while (!etor.IsDone())
    {
        PtrInfo *pinfo = etor.GetNext();
        if (pinfo->mColor == grey) {
            Fault("valid grey node after scanning", pinfo);
        }
    }
#endif
}


////////////////////////////////////////////////////////////////////////
// Bacon & Rajan's |CollectWhite| routine, somewhat modified.
////////////////////////////////////////////////////////////////////////

bool
nsCycleCollector::CollectWhite(nsICycleCollectorListener *aListener)
{
    // Explanation of "somewhat modified": we have no way to collect the
    // set of whites "all at once", we have to ask each of them to drop
    // their outgoing links and assume this will cause the garbage cycle
    // to *mostly* self-destruct (except for the reference we continue
    // to hold). 
    // 
    // To do this "safely" we must make sure that the white nodes we're
    // operating on are stable for the duration of our operation. So we
    // make 3 sets of calls to language runtimes:
    //
    //   - Root(whites), which should pin the whites in memory.
    //   - Unlink(whites), which drops outgoing links on each white.
    //   - Unroot(whites), which returns the whites to normal GC.

    nsresult rv;
    TimeLog timeLog;

    MOZ_ASSERT(mWhiteNodes->IsEmpty(),
               "FinishCollection wasn't called?");

    mWhiteNodes->SetCapacity(mWhiteNodeCount);
    uint32_t numWhiteGCed = 0;

    NodePool::Enumerator etor(mGraph.mNodes);
    while (!etor.IsDone())
    {
        PtrInfo *pinfo = etor.GetNext();
        if (pinfo->mColor == white && mWhiteNodes->AppendElement(pinfo)) {
            rv = pinfo->mParticipant->Root(pinfo->mPointer);
            if (NS_FAILED(rv)) {
                Fault("Failed root call while unlinking", pinfo);
                mWhiteNodes->RemoveElementAt(mWhiteNodes->Length() - 1);
            } else if (pinfo->mRefCount == 0) {
                // only JS objects have a refcount of 0
                ++numWhiteGCed;
            }
        }
    }

    uint32_t count = mWhiteNodes->Length();
    MOZ_ASSERT(numWhiteGCed <= count,
               "More freed GCed nodes than total freed nodes.");
    if (mResults) {
        mResults->mFreedRefCounted += count - numWhiteGCed;
        mResults->mFreedGCed += numWhiteGCed;
    }

    timeLog.Checkpoint("CollectWhite::Root");

    if (mBeforeUnlinkCB) {
        mBeforeUnlinkCB();
        timeLog.Checkpoint("CollectWhite::BeforeUnlinkCB");
    }
#if defined(DEBUG_CC) && !defined(__MINGW32__) && defined(WIN32)
    struct _CrtMemState ms1, ms2;
    _CrtMemCheckpoint(&ms1);
#endif

    if (aListener) {
        for (uint32_t i = 0; i < count; ++i) {
            PtrInfo *pinfo = mWhiteNodes->ElementAt(i);
            aListener->DescribeGarbage((uint64_t)pinfo->mPointer);
        }
        aListener->End();
    }

    for (uint32_t i = 0; i < count; ++i) {
        PtrInfo *pinfo = mWhiteNodes->ElementAt(i);
#ifdef DEBUG
        if (mJSRuntime) {
            mJSRuntime->SetObjectToUnlink(pinfo->mPointer);
        }
#endif
        rv = pinfo->mParticipant->Unlink(pinfo->mPointer);
#ifdef DEBUG
        if (mJSRuntime) {
            mJSRuntime->SetObjectToUnlink(nullptr);
            mJSRuntime->AssertNoObjectsToTrace(pinfo->mPointer);
        }
#endif
        if (NS_FAILED(rv)) {
            Fault("Failed unlink call while unlinking", pinfo);
#ifdef DEBUG_CC
            mStats.mFailedUnlink++;
#endif
        }
        else {
#ifdef DEBUG_CC
            ++mStats.mCollectedNode;
#endif
        }
    }
    timeLog.Checkpoint("CollectWhite::Unlink");

    for (uint32_t i = 0; i < count; ++i) {
        PtrInfo *pinfo = mWhiteNodes->ElementAt(i);
        rv = pinfo->mParticipant->Unroot(pinfo->mPointer);
        if (NS_FAILED(rv))
            Fault("Failed unroot call while unlinking", pinfo);
    }
    timeLog.Checkpoint("CollectWhite::Unroot");

#if defined(DEBUG_CC) && !defined(__MINGW32__) && defined(WIN32)
    _CrtMemCheckpoint(&ms2);
    if (ms2.lTotalCount < ms1.lTotalCount)
        mStats.mFreedBytes += (ms1.lTotalCount - ms2.lTotalCount);
#endif

    return count > 0;
}


////////////////////////////////////////////////////////////////////////
// Collector implementation
////////////////////////////////////////////////////////////////////////

nsCycleCollector::nsCycleCollector() :
    mCollectionInProgress(false),
    mScanInProgress(false),
    mResults(nullptr),
    mJSRuntime(nullptr),
    mWhiteNodes(nullptr),
    mWhiteNodeCount(0),
    mVisitedRefCounted(0),
    mVisitedGCed(0),
    mBeforeUnlinkCB(nullptr),
    mForgetSkippableCB(nullptr),
#ifdef DEBUG_CC
    mPurpleBuf(mParams, mStats),
    mPtrLog(nullptr)
#else
    mPurpleBuf(mParams)
#endif
{
#ifdef DEBUG_CC
    mExpectedGarbage.Init();
#endif
}


nsCycleCollector::~nsCycleCollector()
{
}


void 
nsCycleCollector::RegisterJSRuntime(nsCycleCollectionJSRuntime *aJSRuntime)
{
    if (mParams.mDoNothing)
        return;

    if (mJSRuntime)
        Fault("multiple registrations of cycle collector JS runtime", aJSRuntime);

    mJSRuntime = aJSRuntime;
}

void 
nsCycleCollector::ForgetJSRuntime()
{
    if (mParams.mDoNothing)
        return;

    if (!mJSRuntime)
        Fault("forgetting non-registered cycle collector JS runtime");

    mJSRuntime = nullptr;
}

#ifdef DEBUG_CC

class Suppressor :
    public nsCycleCollectionTraversalCallback
{
protected:
    static char *sSuppressionList;
    static bool sInitialized;
    bool mSuppressThisNode;
public:
    Suppressor()
    {
    }

    bool shouldSuppress(nsISupports *s)
    {
        if (!sInitialized) {
            sSuppressionList = PR_GetEnv("XPCOM_CC_SUPPRESS");
            sInitialized = true;
        }
        if (sSuppressionList == nullptr) {
            mSuppressThisNode = false;
        } else {
            nsresult rv;
            nsXPCOMCycleCollectionParticipant *cp;
            rv = CallQueryInterface(s, &cp);
            if (NS_FAILED(rv)) {
                Fault("checking suppression on wrong type of pointer", s);
                return true;
            }
            cp->Traverse(s, *this);
        }
        return mSuppressThisNode;
    }

    NS_IMETHOD_(void) DescribeRefCountedNode(nsrefcnt refCount,
                                             const char *objName)
    {
        mSuppressThisNode = (PL_strstr(sSuppressionList, objName) != nullptr);
    }

    NS_IMETHOD_(void) DescribeGCedNode(bool isMarked, const char *objName)
    {
        mSuppressThisNode = (PL_strstr(sSuppressionList, objName) != nullptr);
    }

    NS_IMETHOD_(void) NoteXPCOMRoot(nsISupports *root) {}
    NS_IMETHOD_(void) NoteJSRoot(void *root) {}
    NS_IMETHOD_(void) NoteNativeRoot(void *root,
                                     nsCycleCollectionParticipant *participant) {}
    NS_IMETHOD_(void) NoteXPCOMChild(nsISupports *child) {}
    NS_IMETHOD_(void) NoteJSChild(void *child) {}
    NS_IMETHOD_(void) NoteNativeChild(void *child,
                                     nsCycleCollectionParticipant *participant) {}
    NS_IMETHOD_(void) NoteNextEdgeName(const char* name) {}
    NS_IMETHOD_(void) NoteWeakMapping(void *map, void *key, void *kdelegate, void *val) {}
};

char *Suppressor::sSuppressionList = nullptr;
bool Suppressor::sInitialized = false;

static bool
nsCycleCollector_shouldSuppress(nsISupports *s)
{
    Suppressor supp;
    return supp.shouldSuppress(s);
}
#endif

#ifdef DEBUG
static bool
nsCycleCollector_isScanSafe(void *s, nsCycleCollectionParticipant *cp)
{
    if (!s)
        return false;

    if (cp)
        return true;

    nsXPCOMCycleCollectionParticipant *xcp;
    ToParticipant(static_cast<nsISupports*>(s), &xcp);

    return xcp != nullptr;
}
#endif

nsPurpleBufferEntry*
nsCycleCollector::Suspect2(void *n, nsCycleCollectionParticipant *cp)
{
    AbortIfOffMainThreadIfCheckFast();

    // Re-entering ::Suspect during collection used to be a fault, but
    // we are canonicalizing nsISupports pointers using QI, so we will
    // see some spurious refcount traffic here. 

    if (mScanInProgress)
        return nullptr;

    MOZ_ASSERT(nsCycleCollector_isScanSafe(n, cp),
               "suspected a non-scansafe pointer");

    if (mParams.mDoNothing)
        return nullptr;

#ifdef DEBUG_CC
    if (!LogPurpleAddition(n, cp))
        return nullptr;
#endif

    // Caller is responsible for filling in result's mRefCnt.
    return mPurpleBuf.Put(n, cp);
}


bool
nsCycleCollector::Forget2(nsPurpleBufferEntry *e)
{
    AbortIfOffMainThreadIfCheckFast();

    // Re-entering ::Forget during collection used to be a fault, but
    // we are canonicalizing nsISupports pointers using QI, so we will
    // see some spurious refcount traffic here. 

    if (mScanInProgress)
        return false;

#ifdef DEBUG_CC
    LogPurpleRemoval(e->mObject);
#endif

    mPurpleBuf.Remove(e);
    return true;
}

#ifdef DEBUG_CC
void
nsCycleCollector_logPurpleAddition(void* aObject,
                                   nsCycleCollectionParticipant *cp)
{
    if (sCollector) {
        sCollector->LogPurpleAddition(aObject, cp);
    }
}

bool
nsCycleCollector::LogPurpleAddition(void* aObject,
                                    nsCycleCollectionParticipant *cp)
{

    if (mScanInProgress)
        return false;

    if (mParams.mDoNothing)
        return false;

    mStats.mSuspectNode++;

    if (!cp &&
        nsCycleCollector_shouldSuppress(static_cast<nsISupports *>(aObject)))
        return false;

    if (mParams.mLogPointers) {
        if (!mPtrLog)
            mPtrLog = fopen("pointer_log", "w");
        fprintf(mPtrLog, "S %p\n", static_cast<void*>(aObject));
    }

    mPurpleBuf.mNormalObjects.PutEntry(aObject);
    return true;
}

void
nsCycleCollector_logPurpleRemoval(void* aObject)
{
    if (sCollector) {
        sCollector->LogPurpleRemoval(aObject);
    }
}

void
nsCycleCollector::LogPurpleRemoval(void* aObject)
{
    AbortIfOffMainThreadIfCheckFast();

    mStats.mForgetNode++;

    if (mParams.mLogPointers) {
        if (!mPtrLog)
            mPtrLog = fopen("pointer_log", "w");
        fprintf(mPtrLog, "F %p\n", aObject);
    }
    mPurpleBuf.mNormalObjects.RemoveEntry(aObject);
}
#endif

// The cycle collector uses the mark bitmap to discover what JS objects
// were reachable only from XPConnect roots that might participate in
// cycles. We ask the JS runtime whether we need to force a GC before
// this CC. It returns true on startup (before the mark bits have been set),
// and also when UnmarkGray has run out of stack.  We also force GCs on shut 
// down to collect cycles involving both DOM and JS.
void
nsCycleCollector::GCIfNeeded(bool aForceGC)
{
    MOZ_ASSERT(NS_IsMainThread(),
               "nsCycleCollector::GCIfNeeded() must be called on the main thread.");

    if (mParams.mDoNothing)
        return;

    if (!mJSRuntime)
        return;

    if (!aForceGC) {
        bool needGC = mJSRuntime->NeedCollect();
        // Only do a telemetry ping for non-shutdown CCs.
        Telemetry::Accumulate(Telemetry::CYCLE_COLLECTOR_NEED_GC, needGC);
        if (!needGC)
            return;
        if (mResults)
            mResults->mForcedGC = true;
    }

    TimeLog timeLog;

    // mJSRuntime->Collect() must be called from the main thread,
    // because it invokes XPCJSRuntime::GCCallback(cx, JSGC_BEGIN)
    // which returns false if not in the main thread.
    mJSRuntime->Collect(aForceGC ? js::gcreason::SHUTDOWN_CC : js::gcreason::CC_FORCED);
    timeLog.Checkpoint("GC()");
}

bool
nsCycleCollector::PrepareForCollection(nsCycleCollectorResults *aResults,
                                       nsTArray<PtrInfo*> *aWhiteNodes)
{
    // This can legitimately happen in a few cases. See bug 383651.
    if (mCollectionInProgress)
        return false;

    TimeLog timeLog;

    mCollectionStart = TimeStamp::Now();
    mVisitedRefCounted = 0;
    mVisitedGCed = 0;

    mCollectionInProgress = true;

    nsCOMPtr<nsIObserverService> obs =
        mozilla::services::GetObserverService();
    if (obs)
        obs->NotifyObservers(nullptr, "cycle-collector-begin", nullptr);

    mFollowupCollection = false;

    mResults = aResults;
    mWhiteNodes = aWhiteNodes;

    timeLog.Checkpoint("PrepareForCollection()");

    return true;
}

void
nsCycleCollector::CleanupAfterCollection()
{
    mWhiteNodes = nullptr;
    mCollectionInProgress = false;

#ifdef XP_OS2
    // Now that the cycle collector has freed some memory, we can try to
    // force the C library to give back as much memory to the system as
    // possible.
    _heapmin();
#endif

    uint32_t interval = (uint32_t) ((TimeStamp::Now() - mCollectionStart).ToMilliseconds());
#ifdef COLLECT_TIME_DEBUG
    printf("cc: total cycle collector time was %ums\n", interval);
    if (mResults) {
        printf("cc: visited %u ref counted and %u GCed objects, freed %d ref counted and %d GCed objects.\n",
               mVisitedRefCounted, mVisitedGCed,
               mResults->mFreedRefCounted, mResults->mFreedGCed);
    } else {
        printf("cc: visited %u ref counted and %u GCed objects, freed %d.\n",
               mVisitedRefCounted, mVisitedGCed, mWhiteNodeCount);
    }
    printf("cc: \n");
#endif
    if (mResults) {
        mResults->mVisitedRefCounted = mVisitedRefCounted;
        mResults->mVisitedGCed = mVisitedGCed;
        mResults = nullptr;
    }
    Telemetry::Accumulate(Telemetry::CYCLE_COLLECTOR, interval);
    Telemetry::Accumulate(Telemetry::CYCLE_COLLECTOR_VISITED_REF_COUNTED, mVisitedRefCounted);
    Telemetry::Accumulate(Telemetry::CYCLE_COLLECTOR_VISITED_GCED, mVisitedGCed);
    Telemetry::Accumulate(Telemetry::CYCLE_COLLECTOR_COLLECTED, mWhiteNodeCount);
}

void
nsCycleCollector::Collect(bool aMergeCompartments,
                          nsCycleCollectorResults *aResults,
                          uint32_t aTryCollections,
                          nsICycleCollectorListener *aListener)
{
    nsAutoTArray<PtrInfo*, 4000> whiteNodes;

    if (!PrepareForCollection(aResults, &whiteNodes))
        return;

    uint32_t totalCollections = 0;
    while (aTryCollections > totalCollections) {
        // Synchronous cycle collection. Always force a JS GC beforehand.
        GCIfNeeded(true);
        if (aListener && NS_FAILED(aListener->Begin()))
            aListener = nullptr;
        if (!(BeginCollection(aMergeCompartments, aListener) &&
              FinishCollection(aListener)))
            break;

        ++totalCollections;
    }

    CleanupAfterCollection();
}

bool
nsCycleCollector::BeginCollection(bool aMergeCompartments,
                                  nsICycleCollectorListener *aListener)
{
    // aListener should be Begin()'d before this
    TimeLog timeLog;

    if (mParams.mDoNothing)
        return false;

    GCGraphBuilder builder(mGraph, mJSRuntime, aListener, aMergeCompartments);
    if (!builder.Initialized())
        return false;

    if (mJSRuntime) {
        mJSRuntime->BeginCycleCollection(builder);
        timeLog.Checkpoint("mJSRuntime->BeginCycleCollection()");
    }

#ifdef DEBUG_CC
    uint32_t purpleStart = builder.Count();
#endif
    mScanInProgress = true;
    SelectPurple(builder);
#ifdef DEBUG_CC
    uint32_t purpleEnd = builder.Count();

    if (purpleStart != purpleEnd) {
        if (mParams.mLogPointers && !mPtrLog)
            mPtrLog = fopen("pointer_log", "w");

        uint32_t i = 0;
        NodePool::Enumerator queue(mGraph.mNodes);
        while (i++ < purpleStart) {
            queue.GetNext();
        }
        while (i++ < purpleEnd) {
            mStats.mForgetNode++;
            if (mParams.mLogPointers)
                fprintf(mPtrLog, "F %p\n", queue.GetNext()->mPointer);
        }
    }
#endif

    timeLog.Checkpoint("SelectPurple()");

    if (builder.Count() > 0) {
        // The main Bacon & Rajan collection algorithm.

        MarkRoots(builder);
        timeLog.Checkpoint("MarkRoots()");

        ScanRoots();
        timeLog.Checkpoint("ScanRoots()");

        mScanInProgress = false;

        if (aListener) {
            aListener->BeginResults();

            NodePool::Enumerator etor(mGraph.mNodes);
            while (!etor.IsDone()) {
                PtrInfo *pi = etor.GetNext();
                if (pi->mColor == black &&
                    pi->mRefCount > 0 && pi->mRefCount < UINT32_MAX &&
                    pi->mInternalRefs != pi->mRefCount) {
                    aListener->DescribeRoot((uint64_t)pi->mPointer,
                                            pi->mInternalRefs);
                }
            }
        }

#ifdef DEBUG_CC
        if (mFollowupCollection && purpleStart != purpleEnd) {
            uint32_t i = 0;
            NodePool::Enumerator queue(mGraph.mNodes);
            while (i++ < purpleStart) {
                queue.GetNext();
            }
            while (i++ < purpleEnd) {
                PtrInfo *pi = queue.GetNext();
                if (pi->mColor == white) {
                    printf("nsCycleCollector: a later shutdown collection collected the additional\n"
                           "  suspect %p %s\n"
                           "  (which could be fixed by improving traversal)\n",
                           pi->mPointer, pi->mName);
                }
            }
        }
#endif

        if (mJSRuntime) {
            mJSRuntime->FinishTraverse();
            timeLog.Checkpoint("mJSRuntime->FinishTraverse()");
        }
    } else {
        mScanInProgress = false;
    }

    return true;
}

bool
nsCycleCollector::FinishCollection(nsICycleCollectorListener *aListener)
{
    TimeLog timeLog;
    bool collected = CollectWhite(aListener);
    timeLog.Checkpoint("CollectWhite()");

#ifdef DEBUG_CC
    mStats.mCollection++;
    if (mParams.mReportStats)
        mStats.Dump();
#endif

    mFollowupCollection = true;

#ifdef DEBUG_CC
    uint32_t i, count = mWhiteNodes->Length();
    for (i = 0; i < count; ++i) {
        PtrInfo *pinfo = mWhiteNodes->ElementAt(i);
        if (mPurpleBuf.Exists(pinfo->mPointer)) {
            printf("nsCycleCollector: %s object @%p is still alive after\n"
                   "  calling RootAndUnlinkJSObjects, Unlink, and Unroot on"
                   " it!  This probably\n"
                   "  means the Unlink implementation was insufficient.\n",
                   pinfo->mName, pinfo->mPointer);
        }
    }
#endif

    mWhiteNodes->Clear();
    ClearGraph();
    timeLog.Checkpoint("ClearGraph()");

    mParams.mDoNothing = false;

    return collected;
}

uint32_t
nsCycleCollector::SuspectedCount()
{
    return mPurpleBuf.Count();
}

void
nsCycleCollector::Shutdown()
{
#ifndef DEBUG
#ifndef DEBUG_CC
    if (PR_GetEnv("XPCOM_CC_RUN_DURING_SHUTDOWN"))
#endif
#endif
    {
        nsCOMPtr<nsCycleCollectorLogger> listener;
        if (mParams.mLogGraphs || gLogShutdown) {
            listener = new nsCycleCollectorLogger();
            if (gAllTracesAtShutdown) {
                listener->SetAllTraces();
            }
        }
        Collect(false, nullptr,  SHUTDOWN_COLLECTIONS(mParams), listener);
    }

#ifdef DEBUG_CC
    GCGraphBuilder builder(mGraph, mJSRuntime, nullptr, false);
    mScanInProgress = true;
    SelectPurple(builder);
    mScanInProgress = false;
    if (builder.Count() != 0) {
        printf("Might have been able to release more cycles if the cycle collector would "
               "run once more at shutdown.\n");
    }
    ClearGraph();
#endif
    mParams.mDoNothing = true;
}

#ifdef DEBUG_CC
void
nsCycleCollector::ShouldBeFreed(nsISupports *n)
{
    if (n) {
        mExpectedGarbage.PutEntry(n);
    }
}

void
nsCycleCollector::WasFreed(nsISupports *n)
{
    if (n) {
        mExpectedGarbage.RemoveEntry(n);
    }
}
#endif


////////////////////////
// Memory reporter
////////////////////////

NS_MEMORY_REPORTER_MALLOC_SIZEOF_FUN(CycleCollectorMallocSizeOf,
                                     "cycle-collector")

void
nsCycleCollector::SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf,
                                      size_t *aObjectSize,
                                      size_t *aGraphNodesSize,
                                      size_t *aGraphEdgesSize,
                                      size_t *aWhiteNodeSize,
                                      size_t *aPurpleBufferSize) const
{
    *aObjectSize = aMallocSizeOf(this);

    mGraph.SizeOfExcludingThis(aMallocSizeOf, aGraphNodesSize, aGraphEdgesSize);

    // No need to measure what the entries point to; the pointers are
    // non-owning.
    *aWhiteNodeSize = mWhiteNodes
                    ? mWhiteNodes->SizeOfIncludingThis(aMallocSizeOf)
                    : 0;

    *aPurpleBufferSize = mPurpleBuf.SizeOfExcludingThis(aMallocSizeOf);

    // These fields are deliberately not measured:
    // - mResults: because it's tiny and only contains scalars.
    // - mJSRuntime: because it's non-owning and measured by JS reporters.
    // - mParams: because it only contains scalars.
    // - mStats, mPtrLog, mExpectedGarbage: because they're DEBUG_CC-only.
}

class CycleCollectorMultiReporter MOZ_FINAL : public nsIMemoryMultiReporter
{
  public:
    NS_DECL_ISUPPORTS

    NS_IMETHOD GetName(nsACString &name)
    {
        name.AssignLiteral("cycle-collector");
        return NS_OK;
    }

    NS_IMETHOD CollectReports(nsIMemoryMultiReporterCallback *aCb,
                              nsISupports *aClosure)
    {
        if (!sCollector)
            return NS_OK;

        size_t objectSize, graphNodesSize, graphEdgesSize, whiteNodesSize,
               purpleBufferSize;
        sCollector->SizeOfIncludingThis(CycleCollectorMallocSizeOf,
                                        &objectSize, &graphNodesSize,
                                        &graphEdgesSize, &whiteNodesSize,
                                        &purpleBufferSize);

    #define REPORT(_path, _amount, _desc)                                     \
        do {                                                                  \
            size_t amount = _amount;  /* evaluate |_amount| just once */      \
            if (amount > 0) {                                                 \
                nsresult rv;                                                  \
                rv = aCb->Callback(EmptyCString(), NS_LITERAL_CSTRING(_path), \
                                   nsIMemoryReporter::KIND_HEAP,              \
                                   nsIMemoryReporter::UNITS_BYTES, _amount,   \
                                   NS_LITERAL_CSTRING(_desc), aClosure);      \
                NS_ENSURE_SUCCESS(rv, rv);                                    \
            }                                                                 \
        } while (0)

        REPORT("explicit/cycle-collector/collector-object", objectSize,
               "Memory used for the cycle collector object itself.");

        REPORT("explicit/cycle-collector/graph-nodes", graphNodesSize,
               "Memory used for the nodes of the cycle collector's graph. "
               "This should be zero when the collector is idle.");

        REPORT("explicit/cycle-collector/graph-edges", graphEdgesSize,
               "Memory used for the edges of the cycle collector's graph. "
               "This should be zero when the collector is idle.");

        REPORT("explicit/cycle-collector/white-nodes", whiteNodesSize,
               "Memory used for the cycle collector's white nodes array. "
               "This should be zero when the collector is idle.");

        REPORT("explicit/cycle-collector/purple-buffer", purpleBufferSize,
               "Memory used for the cycle collector's purple buffer.");

        return NS_OK;
    }

    NS_IMETHOD GetExplicitNonHeap(int64_t *n)
    {
        // This reporter does neither "explicit" nor NONHEAP measurements.
        *n = 0;
        return NS_OK;
    }
};

NS_IMPL_ISUPPORTS1(CycleCollectorMultiReporter, nsIMemoryMultiReporter)

////////////////////////////////////////////////////////////////////////
// Module public API (exported in nsCycleCollector.h)
// Just functions that redirect into the singleton, once it's built.
////////////////////////////////////////////////////////////////////////

void
nsCycleCollector_registerJSRuntime(nsCycleCollectionJSRuntime *rt)
{
    static bool regMemReport = true;
    if (sCollector)
        sCollector->RegisterJSRuntime(rt);
    if (regMemReport) {
        regMemReport = false;
        sCollectorReporter = new CycleCollectorMultiReporter;
        NS_RegisterMemoryMultiReporter(sCollectorReporter);
    }
}

void
nsCycleCollector_forgetJSRuntime()
{
    if (sCollector)
        sCollector->ForgetJSRuntime();
    if (sCollectorReporter) {
        NS_UnregisterMemoryMultiReporter(sCollectorReporter);
        sCollectorReporter = nullptr;
    }
}

nsPurpleBufferEntry*
NS_CycleCollectorSuspect2(void *n, nsCycleCollectionParticipant *cp)
{
    if (sCollector)
        return sCollector->Suspect2(n, cp);
    return nullptr;
}

bool
NS_CycleCollectorForget2(nsPurpleBufferEntry *e)
{
    return sCollector ? sCollector->Forget2(e) : true;
}

uint32_t
nsCycleCollector_suspectedCount()
{
    return sCollector ? sCollector->SuspectedCount() : 0;
}

#ifdef DEBUG
void
nsCycleCollector_DEBUG_shouldBeFreed(nsISupports *n)
{
#ifdef DEBUG_CC
    if (sCollector)
        sCollector->ShouldBeFreed(n);
#endif
}

void
nsCycleCollector_DEBUG_wasFreed(nsISupports *n)
{
#ifdef DEBUG_CC
    if (sCollector)
        sCollector->WasFreed(n);
#endif
}
#endif

class nsCycleCollectorRunner : public nsRunnable
{
    nsCycleCollector *mCollector;
    nsICycleCollectorListener *mListener;
    Mutex mLock;
    CondVar mRequest;
    CondVar mReply;
    bool mRunning;
    bool mShutdown;
    bool mCollected;
    bool mMergeCompartments;

public:
    NS_IMETHOD Run()
    {
        PR_SetCurrentThreadName("XPCOM CC");

#ifdef XP_WIN
        TlsSetValue(gTLSThreadIDIndex,
                    (void*) mozilla::threads::CycleCollector);
#elif defined(NS_TLS)
        gTLSThreadID = mozilla::threads::CycleCollector;
#else
        gCycleCollectorThread = PR_GetCurrentThread();
#endif

        MOZ_ASSERT(NS_IsCycleCollectorThread() && !NS_IsMainThread(),
                   "Wrong thread!");

        MutexAutoLock autoLock(mLock);

        if (mShutdown)
            return NS_OK;

        mRunning = true;

        while (1) {
            mRequest.Wait();

            if (!mRunning) {
                mReply.Notify();
                return NS_OK;
            }

            mCollector->mJSRuntime->NotifyEnterCycleCollectionThread();
            mCollected = mCollector->BeginCollection(mMergeCompartments, mListener);
            mCollector->mJSRuntime->NotifyLeaveCycleCollectionThread();

            mReply.Notify();
        }

        return NS_OK;
    }

    nsCycleCollectorRunner(nsCycleCollector *collector)
        : mCollector(collector),
          mListener(nullptr),
          mLock("cycle collector lock"),
          mRequest(mLock, "cycle collector request condvar"),
          mReply(mLock, "cycle collector reply condvar"),
          mRunning(false),
          mShutdown(false),
          mCollected(false),
          mMergeCompartments(false)
    {
        MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
    }

    void Collect(bool aMergeCompartments,
                 nsCycleCollectorResults *aResults,
                 nsICycleCollectorListener *aListener)
    {
        MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");

        // On a WantAllTraces CC, force a synchronous global GC to prevent
        // hijinks from ForgetSkippable and compartmental GCs.
        bool wantAllTraces = false;
        if (aListener) {
            aListener->GetWantAllTraces(&wantAllTraces);
        }
        mCollector->GCIfNeeded(wantAllTraces);

        MutexAutoLock autoLock(mLock);

        if (!mRunning)
            return;

        nsAutoTArray<PtrInfo*, 4000> whiteNodes;
        if (!mCollector->PrepareForCollection(aResults, &whiteNodes))
            return;

        MOZ_ASSERT(!mListener, "Should have cleared this already!");
        if (aListener && NS_FAILED(aListener->Begin()))
            aListener = nullptr;
        mListener = aListener;
        mMergeCompartments = aMergeCompartments;

        if (mCollector->mJSRuntime->NotifyLeaveMainThread()) {
            mRequest.Notify();
            mReply.Wait();
            mCollector->mJSRuntime->NotifyEnterMainThread();
        } else {
            mCollected = mCollector->BeginCollection(aMergeCompartments, mListener);
        }

        mListener = nullptr;

        if (mCollected) {
            mCollector->FinishCollection(aListener);
            mCollector->CleanupAfterCollection();
        }
    }

    void Shutdown()
    {
        MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");

        MutexAutoLock autoLock(mLock);

        mShutdown = true;

        if (!mRunning)
            return;

        mRunning = false;
        mRequest.Notify();
        mReply.Wait();
    }
};

// Holds a reference.
static nsCycleCollectorRunner* sCollectorRunner;

// Holds a reference.
static nsIThread* sCollectorThread;

nsresult
nsCycleCollector_startup()
{
    MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
    MOZ_ASSERT(!sCollector, "Forgot to call nsCycleCollector_shutdown?");

    sCollector = new nsCycleCollector();

    nsRefPtr<nsCycleCollectorRunner> runner =
        new nsCycleCollectorRunner(sCollector);

    nsCOMPtr<nsIThread> thread;
    nsresult rv = NS_NewThread(getter_AddRefs(thread), runner);
    NS_ENSURE_SUCCESS(rv, rv);

    runner.swap(sCollectorRunner);
    thread.swap(sCollectorThread);

    return rv;
}

void
nsCycleCollector_setBeforeUnlinkCallback(CC_BeforeUnlinkCallback aCB)
{
    if (sCollector) {
        sCollector->mBeforeUnlinkCB = aCB;
    }
}

void
nsCycleCollector_setForgetSkippableCallback(CC_ForgetSkippableCallback aCB)
{
    if (sCollector) {
        sCollector->mForgetSkippableCB = aCB;
    }
}

void
nsCycleCollector_forgetSkippable(bool aRemoveChildlessNodes)
{
    if (sCollector) {
        SAMPLE_LABEL("CC", "nsCycleCollector_forgetSkippable");
        TimeLog timeLog;
        sCollector->ForgetSkippable(aRemoveChildlessNodes);
        timeLog.Checkpoint("ForgetSkippable()");
    }
}

void
nsCycleCollector_collect(bool aMergeCompartments,
                         nsCycleCollectorResults *aResults,
                         nsICycleCollectorListener *aListener)
{
    MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
    SAMPLE_LABEL("CC", "nsCycleCollector_collect");
    nsCOMPtr<nsICycleCollectorListener> listener(aListener);
    if (!aListener && sCollector && sCollector->mParams.mLogGraphs) {
        listener = new nsCycleCollectorLogger();
    }

    if (sCollectorRunner) {
        sCollectorRunner->Collect(aMergeCompartments, aResults, listener);
    } else if (sCollector) {
        sCollector->Collect(aMergeCompartments, aResults, 1, listener);
    }
}

void
nsCycleCollector_shutdownThreads()
{
    MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
    if (sCollectorRunner) {
        nsRefPtr<nsCycleCollectorRunner> runner;
        runner.swap(sCollectorRunner);
        runner->Shutdown();
    }

    if (sCollectorThread) {
        nsCOMPtr<nsIThread> thread;
        thread.swap(sCollectorThread);
        thread->Shutdown();
    }
}

void
nsCycleCollector_shutdown()
{
    MOZ_ASSERT(NS_IsMainThread(), "Wrong thread!");
    MOZ_ASSERT(!sCollectorRunner, "Should have finished before!");
    MOZ_ASSERT(!sCollectorThread, "Should have finished before!");

    if (sCollector) {
        SAMPLE_LABEL("CC", "nsCycleCollector_shutdown");
        sCollector->Shutdown();
        delete sCollector;
        sCollector = nullptr;
    }
}
