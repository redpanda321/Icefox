/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


#ifndef nsISupportsImpl_h__
#define nsISupportsImpl_h__

#ifndef nscore_h___
#include "nscore.h"
#endif

#ifndef nsISupportsBase_h__
#include "nsISupportsBase.h"
#endif

#ifndef nsISupportsUtils_h__
#include "nsISupportsUtils.h"
#endif


#if !defined(XPCOM_GLUE_AVOID_NSPR)
#include "prthread.h" /* needed for thread-safety checks */
#include "nsAtomicRefcnt.h" /* for NS_Atomic{Increment,Decrement}Refcnt */
#endif

#include "nsDebug.h"
#include "nsTraceRefcnt.h"
#include "nsCycleCollector.h"
#include "nsCycleCollectorUtils.h"

////////////////////////////////////////////////////////////////////////////////
// Macros to help detect thread-safety:

#if defined(DEBUG) && !defined(XPCOM_GLUE_AVOID_NSPR)

class nsAutoOwningThread {
public:
    nsAutoOwningThread() { mThread = PR_GetCurrentThread(); }
    void *GetThread() const { return mThread; }

private:
    void *mThread;
};

#define NS_DECL_OWNINGTHREAD            nsAutoOwningThread _mOwningThread;
#define NS_ASSERT_OWNINGTHREAD(_class) \
  NS_CheckThreadSafe(_mOwningThread.GetThread(), #_class " not thread-safe")
#define NS_ASSERT_OWNINGTHREAD_AND_NOT_CCTHREAD(_class) \
  do { \
    if (NS_IsCycleCollectorThread()) { \
      NS_ERROR("Changing refcount of " #_class " object during Traverse is " \
               "not permitted!"); \
    } \
    else { \
      NS_ASSERT_OWNINGTHREAD(_class); \
    } \
  } while (0)

#else // !DEBUG

#define NS_DECL_OWNINGTHREAD            /* nothing */
#define NS_ASSERT_OWNINGTHREAD(_class)  ((void)0)
#define NS_ASSERT_OWNINGTHREAD_AND_NOT_CCTHREAD(_class)  ((void)0)

#endif // DEBUG

#define NS_CCAR_REFCNT_BIT 1
#define NS_CCAR_REFCNT_TO_TAGGED(rc_) \
  NS_INT32_TO_PTR((rc_ << 1) | NS_CCAR_REFCNT_BIT)
#define NS_CCAR_PURPLE_ENTRY_TO_TAGGED(pe_) \
  static_cast<void*>(pe_)
#define NS_CCAR_TAGGED_TO_REFCNT(tagged_) \
  nsrefcnt(NS_PTR_TO_INT32(tagged_) >> 1)
#define NS_CCAR_TAGGED_TO_PURPLE_ENTRY(tagged_) \
  static_cast<nsPurpleBufferEntry*>(tagged_)
#define NS_CCAR_TAGGED_STABILIZED_REFCNT NS_CCAR_PURPLE_ENTRY_TO_TAGGED(0)

// Support for ISupports classes which interact with cycle collector.

/**
 * This struct (once shipped) will be FROZEN with respect to the
 * NS_CycleCollectorSuspect2 and NS_CycleCollectorForget2 functions.  If
 * we need to change the struct, we'll need Suspect3 and Forget3 for the
 * new versions.
 */
struct nsPurpleBufferEntry {
  union {
    nsISupports *mObject;                 // when low bit unset
    nsPurpleBufferEntry *mNextInFreeList; // when low bit set
  };
  // When an object is in the purple buffer, it replaces its reference
  // count with a (tagged) pointer to this entry, so we store the
  // reference count for it.
  nsrefcnt mRefCnt;
};

class nsCycleCollectingAutoRefCnt {

public:
  nsCycleCollectingAutoRefCnt()
    : mTagged(NS_CCAR_REFCNT_TO_TAGGED(0))
  {}

  nsCycleCollectingAutoRefCnt(nsrefcnt aValue)
    : mTagged(NS_CCAR_REFCNT_TO_TAGGED(aValue))
  {
  }

  nsrefcnt incr(nsISupports *owner)
  {
    if (NS_UNLIKELY(mTagged == NS_CCAR_TAGGED_STABILIZED_REFCNT)) {
      // The sentinel value "purple bit alone, refcount 0" means
      // that we're stabilized, during finalization. In this
      // state we lie about our actual refcount if anyone asks
      // and say it's 2, which is basically true: the caller who
      // is incrementing has a reference, as does the decr() frame
      // that stabilized-and-is-deleting us.
      return 2;
    }

    nsrefcnt refcount;
    if (IsPurple()) {
      nsPurpleBufferEntry *e = NS_CCAR_TAGGED_TO_PURPLE_ENTRY(mTagged);
      NS_ASSERTION(e->mObject == owner, "wrong entry");
      refcount = e->mRefCnt;
      NS_ASSERTION(refcount != 0, "purple ISupports pointer with zero refcnt");

      if (NS_LIKELY(NS_CycleCollectorForget2(e))) {
        // |e| is now invalid
        ++refcount;
        mTagged = NS_CCAR_REFCNT_TO_TAGGED(refcount);
      } else {
        ++refcount;
        e->mRefCnt = refcount;
      }
    } else {
      refcount = NS_CCAR_TAGGED_TO_REFCNT(mTagged);
      ++refcount;
      mTagged = NS_CCAR_REFCNT_TO_TAGGED(refcount);
    }

    return refcount;
  }

  void stabilizeForDeletion(nsISupports*)
  {
    mTagged = NS_CCAR_TAGGED_STABILIZED_REFCNT;
  }

  nsrefcnt decr(nsISupports *owner)
  {
    if (NS_UNLIKELY(mTagged == NS_CCAR_TAGGED_STABILIZED_REFCNT))
      return 1;

    nsrefcnt refcount;
    if (IsPurple()) {
      nsPurpleBufferEntry *e = NS_CCAR_TAGGED_TO_PURPLE_ENTRY(mTagged);
      NS_ASSERTION(e->mObject == owner, "wrong entry");
      refcount = e->mRefCnt;
      --refcount;
      
      if (NS_UNLIKELY(refcount == 0)) {
        if (NS_UNLIKELY(!NS_CycleCollectorForget2(e))) {
          NS_NOTREACHED("forget should not fail when reference count hits 0");
          // Clear the entry's pointer to us.
          e->mObject = nsnull;
        }
        mTagged = NS_CCAR_REFCNT_TO_TAGGED(refcount);
      } else {
        e->mRefCnt = refcount;
      }
    } else {
      refcount = NS_CCAR_TAGGED_TO_REFCNT(mTagged);
      --refcount;

      nsPurpleBufferEntry *e;
      if (NS_LIKELY(refcount > 0) &&
          ((e = NS_CycleCollectorSuspect2(owner)))) {
        e->mRefCnt = refcount;
        mTagged = NS_CCAR_PURPLE_ENTRY_TO_TAGGED(e);
      } else {
        mTagged = NS_CCAR_REFCNT_TO_TAGGED(refcount);
      }
    }

    return refcount;
  }

  void unmarkPurple()
  {
    NS_ASSERTION(IsPurple(), "must be purple");
    nsrefcnt refcount = NS_CCAR_TAGGED_TO_PURPLE_ENTRY(mTagged)->mRefCnt;
    mTagged = NS_CCAR_REFCNT_TO_TAGGED(refcount);
  }

  void RemovePurple()
  {
    NS_ASSERTION(IsPurple(), "must be purple");
#ifdef DEBUG_CC
    nsCycleCollector_logPurpleRemoval(
      NS_CCAR_TAGGED_TO_PURPLE_ENTRY(mTagged)->mObject);
#endif
    // The entry will be added to the free list later. 
    NS_CCAR_TAGGED_TO_PURPLE_ENTRY(mTagged)->mObject = nsnull;
    unmarkPurple();
  }

  bool IsPurple() const
  {
    NS_ASSERTION(mTagged != NS_CCAR_TAGGED_STABILIZED_REFCNT,
                 "should have checked for stabilization first");
    return !(NS_PTR_TO_INT32(mTagged) & NS_CCAR_REFCNT_BIT);
  }

  nsrefcnt get() const
  {
    if (NS_UNLIKELY(mTagged == NS_CCAR_TAGGED_STABILIZED_REFCNT))
      return 1;

    return NS_UNLIKELY(IsPurple())
             ? NS_CCAR_TAGGED_TO_PURPLE_ENTRY(mTagged)->mRefCnt
             : NS_CCAR_TAGGED_TO_REFCNT(mTagged);
  }

  operator nsrefcnt() const
  {
    return get();
  }

 private:
  void *mTagged;
};

class nsAutoRefCnt {

 public:
    nsAutoRefCnt() : mValue(0) {}
    nsAutoRefCnt(nsrefcnt aValue) : mValue(aValue) {}

    // only support prefix increment/decrement
    nsrefcnt operator++() { return ++mValue; }
    nsrefcnt operator--() { return --mValue; }

    nsrefcnt operator=(nsrefcnt aValue) { return (mValue = aValue); }
    operator nsrefcnt() const { return mValue; }
    nsrefcnt get() const { return mValue; }
 private:
    // do not define these to enforce the faster prefix notation
    nsrefcnt operator++(int);
    nsrefcnt operator--(int);
    nsrefcnt mValue;
};

///////////////////////////////////////////////////////////////////////////////

/**
 * Declare the reference count variable and the implementations of the
 * AddRef and QueryInterface methods.
 */

#define NS_DECL_ISUPPORTS                                                     \
public:                                                                       \
  NS_IMETHOD QueryInterface(REFNSIID aIID,                                    \
                            void** aInstancePtr);                             \
  NS_IMETHOD_(nsrefcnt) AddRef(void);                                         \
  NS_IMETHOD_(nsrefcnt) Release(void);                                        \
protected:                                                                    \
  nsAutoRefCnt mRefCnt;                                                       \
  NS_DECL_OWNINGTHREAD                                                        \
public:

#define NS_DECL_CYCLE_COLLECTING_ISUPPORTS                                    \
public:                                                                       \
  NS_IMETHOD QueryInterface(REFNSIID aIID,                                    \
                            void** aInstancePtr);                             \
  NS_IMETHOD_(nsrefcnt) AddRef(void);                                         \
  NS_IMETHOD_(nsrefcnt) Release(void);                                        \
  void UnmarkIfPurple()                                                       \
  {                                                                           \
    if (NS_LIKELY(mRefCnt.IsPurple()))                                        \
      mRefCnt.unmarkPurple();                                                 \
  }                                                                           \
protected:                                                                    \
  nsCycleCollectingAutoRefCnt mRefCnt;                                        \
  NS_DECL_OWNINGTHREAD                                                        \
public:


///////////////////////////////////////////////////////////////////////////////

/**
 * Previously used to initialize the reference count, but no longer needed.
 *
 * DEPRECATED.
 */
#define NS_INIT_ISUPPORTS() ((void)0)

/**
 * Use this macro to declare and implement the AddRef & Release methods for a
 * given non-XPCOM <i>_class</i>.
 *
 * @param _class The name of the class implementing the method
 */
#define NS_INLINE_DECL_REFCOUNTING(_class)                                    \
public:                                                                       \
  NS_METHOD_(nsrefcnt) AddRef(void) {                                         \
    NS_PRECONDITION(PRInt32(mRefCnt) >= 0, "illegal refcnt");                 \
    NS_ASSERT_OWNINGTHREAD_AND_NOT_CCTHREAD(_class);                          \
    ++mRefCnt;                                                                \
    NS_LOG_ADDREF(this, mRefCnt, #_class, sizeof(*this));                     \
    return mRefCnt;                                                           \
  }                                                                           \
  NS_METHOD_(nsrefcnt) Release(void) {                                        \
    NS_PRECONDITION(0 != mRefCnt, "dup release");                             \
    NS_ASSERT_OWNINGTHREAD_AND_NOT_CCTHREAD(_class);                          \
    --mRefCnt;                                                                \
    NS_LOG_RELEASE(this, mRefCnt, #_class);                                   \
    if (mRefCnt == 0) {                                                       \
      NS_ASSERT_OWNINGTHREAD(_class);                                         \
      mRefCnt = 1; /* stabilize */                                            \
      delete this;                                                            \
      return 0;                                                               \
    }                                                                         \
    return mRefCnt;                                                           \
  }                                                                           \
protected:                                                                    \
  nsAutoRefCnt mRefCnt;                                                       \
  NS_DECL_OWNINGTHREAD                                                        \
public:

/**
 * Use this macro to declare and implement the AddRef & Release methods for a
 * given non-XPCOM <i>_class</i> in a threadsafe manner.
 *
 * DOES NOT DO REFCOUNT STABILIZATION!
 *
 * @param _class The name of the class implementing the method
 */
#define NS_INLINE_DECL_THREADSAFE_REFCOUNTING(_class)                         \
public:                                                                       \
  NS_METHOD_(nsrefcnt) AddRef(void) {                                         \
    NS_PRECONDITION(PRInt32(mRefCnt) >= 0, "illegal refcnt");                 \
    nsrefcnt count = NS_AtomicIncrementRefcnt(mRefCnt);                       \
    NS_LOG_ADDREF(this, count, #_class, sizeof(*this));                       \
    return (nsrefcnt) count;                                                  \
  }                                                                           \
  NS_METHOD_(nsrefcnt) Release(void) {                                        \
    NS_PRECONDITION(0 != mRefCnt, "dup release");                             \
    nsrefcnt count = NS_AtomicDecrementRefcnt(mRefCnt);                       \
    NS_LOG_RELEASE(this, count, #_class);                                     \
    if (count == 0) {                                                         \
      delete (this);                                                          \
      return 0;                                                               \
    }                                                                         \
    return count;                                                             \
  }                                                                           \
protected:                                                                    \
  nsAutoRefCnt mRefCnt;                                                       \
public:

/**
 * Use this macro to implement the AddRef method for a given <i>_class</i>
 * @param _class The name of the class implementing the method
 */
#define NS_IMPL_ADDREF(_class)                                                \
NS_IMETHODIMP_(nsrefcnt) _class::AddRef(void)                                 \
{                                                                             \
  NS_PRECONDITION(PRInt32(mRefCnt) >= 0, "illegal refcnt");                   \
  NS_ASSERT_OWNINGTHREAD_AND_NOT_CCTHREAD(_class);                            \
  ++mRefCnt;                                                                  \
  NS_LOG_ADDREF(this, mRefCnt, #_class, sizeof(*this));                       \
  return mRefCnt;                                                             \
}

/**
 * Use this macro to implement the AddRef method for a given <i>_class</i>
 * implemented as a wholly owned aggregated object intended to implement
 * interface(s) for its owner
 * @param _class The name of the class implementing the method
 * @param _aggregator the owning/containing object
 */
#define NS_IMPL_ADDREF_USING_AGGREGATOR(_class, _aggregator)                  \
NS_IMETHODIMP_(nsrefcnt) _class::AddRef(void)                                 \
{                                                                             \
  NS_PRECONDITION(_aggregator, "null aggregator");                            \
  return (_aggregator)->AddRef();                                             \
}

/**
 * Use this macro to implement the Release method for a given
 * <i>_class</i>.
 * @param _class The name of the class implementing the method
 * @param _destroy A statement that is executed when the object's
 *   refcount drops to zero.
 *
 * For example,
 *
 *   NS_IMPL_RELEASE_WITH_DESTROY(Foo, Destroy(this))
 *
 * will cause
 *
 *   Destroy(this);
 *
 * to be invoked when the object's refcount drops to zero. This
 * allows for arbitrary teardown activity to occur (e.g., deallocation
 * of object allocated with placement new).
 */
#define NS_IMPL_RELEASE_WITH_DESTROY(_class, _destroy)                        \
NS_IMETHODIMP_(nsrefcnt) _class::Release(void)                                \
{                                                                             \
  NS_PRECONDITION(0 != mRefCnt, "dup release");                               \
  NS_ASSERT_OWNINGTHREAD_AND_NOT_CCTHREAD(_class);                            \
  --mRefCnt;                                                                  \
  NS_LOG_RELEASE(this, mRefCnt, #_class);                                     \
  if (mRefCnt == 0) {                                                         \
    NS_ASSERT_OWNINGTHREAD(_class);                                           \
    mRefCnt = 1; /* stabilize */                                              \
    _destroy;                                                                 \
    return 0;                                                                 \
  }                                                                           \
  return mRefCnt;                                                             \
}

/**
 * Use this macro to implement the Release method for a given <i>_class</i>
 * @param _class The name of the class implementing the method
 *
 * A note on the 'stabilization' of the refcnt to one. At that point,
 * the object's refcount will have gone to zero. The object's
 * destructor may trigger code that attempts to QueryInterface() and
 * Release() 'this' again. Doing so will temporarily increment and
 * decrement the refcount. (Only a logic error would make one try to
 * keep a permanent hold on 'this'.)  To prevent re-entering the
 * destructor, we make sure that no balanced refcounting can return
 * the refcount to |0|.
 */
#define NS_IMPL_RELEASE(_class) \
  NS_IMPL_RELEASE_WITH_DESTROY(_class, delete (this))

/**
 * Use this macro to implement the Release method for a given <i>_class</i>
 * implemented as a wholly owned aggregated object intended to implement
 * interface(s) for its owner
 * @param _class The name of the class implementing the method
 * @param _aggregator the owning/containing object
 */
#define NS_IMPL_RELEASE_USING_AGGREGATOR(_class, _aggregator)                 \
NS_IMETHODIMP_(nsrefcnt) _class::Release(void)                                \
{                                                                             \
  NS_PRECONDITION(_aggregator, "null aggregator");                            \
  return (_aggregator)->Release();                                            \
}


#define NS_IMPL_CYCLE_COLLECTING_ADDREF(_class)                               \
NS_IMETHODIMP_(nsrefcnt) _class::AddRef(void)                                 \
{                                                                             \
  NS_PRECONDITION(PRInt32(mRefCnt) >= 0, "illegal refcnt");                   \
  NS_ASSERT_OWNINGTHREAD_AND_NOT_CCTHREAD(_class);                            \
  nsrefcnt count =                                                            \
    mRefCnt.incr(NS_CYCLE_COLLECTION_CLASSNAME(_class)::Upcast(this));        \
  NS_LOG_ADDREF(this, count, #_class, sizeof(*this));                         \
  return count;                                                               \
}

#define NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_DESTROY(_class, _destroy)       \
NS_IMETHODIMP_(nsrefcnt) _class::Release(void)                                \
{                                                                             \
  NS_PRECONDITION(0 != mRefCnt, "dup release");                               \
  NS_ASSERT_OWNINGTHREAD_AND_NOT_CCTHREAD(_class);                            \
  nsISupports *base = NS_CYCLE_COLLECTION_CLASSNAME(_class)::Upcast(this);    \
  nsrefcnt count = mRefCnt.decr(base);                                        \
  NS_LOG_RELEASE(this, count, #_class);                                       \
  if (count == 0) {                                                           \
    NS_ASSERT_OWNINGTHREAD(_class);                                           \
    mRefCnt.stabilizeForDeletion(base);                                       \
    _destroy;                                                                 \
    return 0;                                                                 \
  }                                                                           \
  return count;                                                               \
}

#define NS_IMPL_CYCLE_COLLECTING_RELEASE(_class)                              \
  NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_DESTROY(_class, delete (this))


///////////////////////////////////////////////////////////////////////////////

/**
 * There are two ways of implementing QueryInterface, and we use both:
 *
 * Table-driven QueryInterface uses a static table of IID->offset mappings
 * and a shared helper function. Using it tends to reduce codesize and improve
 * runtime performance (due to processor cache hits).
 *
 * Macro-driven QueryInterface generates a QueryInterface function directly
 * using common macros. This is necessary if special QueryInterface features
 * are being used (such as tearoffs and conditional interfaces).
 *
 * These methods can be combined into a table-driven function call followed
 * by custom code for tearoffs and conditionals.
 */

struct QITableEntry
{
  const nsIID *iid;     // null indicates end of the QITableEntry array
  PROffset32   offset;
};

NS_COM_GLUE nsresult NS_FASTCALL
NS_TableDrivenQI(void* aThis, const QITableEntry* entries,
                 REFNSIID aIID, void **aInstancePtr);

/**
 * Implement table-driven queryinterface
 */

#define NS_INTERFACE_TABLE_HEAD(_class)                                       \
NS_IMETHODIMP _class::QueryInterface(REFNSIID aIID, void** aInstancePtr)      \
{                                                                             \
  NS_ASSERTION(aInstancePtr,                                                  \
               "QueryInterface requires a non-NULL destination!");            \
  nsresult rv = NS_ERROR_FAILURE;

#define NS_INTERFACE_TABLE_BEGIN                                              \
  static const QITableEntry table[] = {

#define NS_INTERFACE_TABLE_ENTRY(_class, _interface)                          \
  { &_interface::COMTypeInfo<int>::kIID,                                      \
    PROffset32(reinterpret_cast<char*>(                                       \
                        static_cast<_interface*>((_class*) 0x1000)) -         \
               reinterpret_cast<char*>((_class*) 0x1000))                     \
  },

#define NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, _interface, _implClass)    \
  { &_interface::COMTypeInfo<int>::kIID,                                      \
    PROffset32(reinterpret_cast<char*>(                                       \
                        static_cast<_interface*>(                             \
                                       static_cast<_implClass*>(              \
                                                      (_class*) 0x1000))) -   \
               reinterpret_cast<char*>((_class*) 0x1000))                     \
  },

#define NS_INTERFACE_TABLE_END_WITH_PTR(_ptr)                                 \
  { nsnull, 0 } };                                                            \
  rv = NS_TableDrivenQI(static_cast<void*>(_ptr),                             \
                        table, aIID, aInstancePtr);

#define NS_INTERFACE_TABLE_END                                                \
  NS_INTERFACE_TABLE_END_WITH_PTR(this)

#define NS_INTERFACE_TABLE_TAIL                                               \
  return rv;                                                                  \
}

#define NS_INTERFACE_TABLE_TAIL_INHERITING(_baseclass)                        \
  if (NS_SUCCEEDED(rv))                                                       \
    return rv;                                                                \
  return _baseclass::QueryInterface(aIID, aInstancePtr);                      \
}

#define NS_INTERFACE_TABLE_TAIL_USING_AGGREGATOR(_aggregator)                 \
  if (NS_SUCCEEDED(rv))                                                       \
    return rv;                                                                \
  NS_ASSERTION(_aggregator, "null aggregator");                               \
  return _aggregator->QueryInterface(aIID, aInstancePtr)                      \
}

/**
 * This implements query interface with two assumptions: First, the
 * class in question implements nsISupports and its own interface and
 * nothing else. Second, the implementation of the class's primary
 * inheritance chain leads to its own interface.
 *
 * @param _class The name of the class implementing the method
 * @param _classiiddef The name of the #define symbol that defines the IID
 * for the class (e.g. NS_ISUPPORTS_IID)
 */

#define NS_IMPL_QUERY_HEAD(_class)                                            \
NS_IMETHODIMP _class::QueryInterface(REFNSIID aIID, void** aInstancePtr)      \
{                                                                             \
  NS_ASSERTION(aInstancePtr,                                                  \
               "QueryInterface requires a non-NULL destination!");            \
  nsISupports* foundInterface;

#define NS_IMPL_QUERY_BODY(_interface)                                        \
  if ( aIID.Equals(NS_GET_IID(_interface)) )                                  \
    foundInterface = static_cast<_interface*>(this);                          \
  else

#define NS_IMPL_QUERY_BODY_CONDITIONAL(_interface, condition)                 \
  if ( (condition) && aIID.Equals(NS_GET_IID(_interface)))                    \
    foundInterface = static_cast<_interface*>(this);                          \
  else

#define NS_IMPL_QUERY_BODY_AMBIGUOUS(_interface, _implClass)                  \
  if ( aIID.Equals(NS_GET_IID(_interface)) )                                  \
    foundInterface = static_cast<_interface*>(                                \
                                    static_cast<_implClass*>(this));          \
  else

#define NS_IMPL_QUERY_BODY_AGGREGATED(_interface, _aggregate)                 \
  if ( aIID.Equals(NS_GET_IID(_interface)) )                                  \
    foundInterface = static_cast<_interface*>(_aggregate);                    \
  else

#define NS_IMPL_QUERY_TAIL_GUTS                                               \
    foundInterface = 0;                                                       \
  nsresult status;                                                            \
  if ( !foundInterface )                                                      \
    status = NS_NOINTERFACE;                                                  \
  else                                                                        \
    {                                                                         \
      NS_ADDREF(foundInterface);                                              \
      status = NS_OK;                                                         \
    }                                                                         \
  *aInstancePtr = foundInterface;                                             \
  return status;                                                              \
}

#define NS_IMPL_QUERY_TAIL_INHERITING(_baseclass)                             \
    foundInterface = 0;                                                       \
  nsresult status;                                                            \
  if ( !foundInterface )                                                      \
    status = _baseclass::QueryInterface(aIID, (void**)&foundInterface);       \
  else                                                                        \
    {                                                                         \
      NS_ADDREF(foundInterface);                                              \
      status = NS_OK;                                                         \
    }                                                                         \
  *aInstancePtr = foundInterface;                                             \
  return status;                                                              \
}

#define NS_IMPL_QUERY_TAIL_USING_AGGREGATOR(_aggregator)                      \
    foundInterface = 0;                                                       \
  nsresult status;                                                            \
  if ( !foundInterface ) {                                                    \
    NS_ASSERTION(_aggregator, "null aggregator");                             \
    status = _aggregator->QueryInterface(aIID, (void**)&foundInterface);      \
  } else                                                                      \
    {                                                                         \
      NS_ADDREF(foundInterface);                                              \
      status = NS_OK;                                                         \
    }                                                                         \
  *aInstancePtr = foundInterface;                                             \
  return status;                                                              \
}

#define NS_IMPL_QUERY_TAIL(_supports_interface)                               \
  NS_IMPL_QUERY_BODY_AMBIGUOUS(nsISupports, _supports_interface)              \
  NS_IMPL_QUERY_TAIL_GUTS


  /*
    This is the new scheme.  Using this notation now will allow us to switch to
    a table driven mechanism when it's ready.  Note the difference between this
    and the (currently) underlying NS_IMPL_QUERY_INTERFACE mechanism.  You must
    explicitly mention |nsISupports| when using the interface maps.
  */
#define NS_INTERFACE_MAP_BEGIN(_implClass)      NS_IMPL_QUERY_HEAD(_implClass)
#define NS_INTERFACE_MAP_ENTRY(_interface)      NS_IMPL_QUERY_BODY(_interface)
#define NS_INTERFACE_MAP_ENTRY_CONDITIONAL(_interface, condition)             \
  NS_IMPL_QUERY_BODY_CONDITIONAL(_interface, condition)
#define NS_INTERFACE_MAP_ENTRY_AGGREGATED(_interface,_aggregate)              \
  NS_IMPL_QUERY_BODY_AGGREGATED(_interface,_aggregate)

#define NS_INTERFACE_MAP_END                    NS_IMPL_QUERY_TAIL_GUTS
#define NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(_interface, _implClass)              \
  NS_IMPL_QUERY_BODY_AMBIGUOUS(_interface, _implClass)
#define NS_INTERFACE_MAP_END_INHERITING(_baseClass)                           \
  NS_IMPL_QUERY_TAIL_INHERITING(_baseClass)
#define NS_INTERFACE_MAP_END_AGGREGATED(_aggregator)                          \
  NS_IMPL_QUERY_TAIL_USING_AGGREGATOR(_aggregator)

#define NS_INTERFACE_TABLE0(_class)                                           \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(_class, nsISupports)                             \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE1(_class, _i1)                                      \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(_class, _i1)                                     \
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, nsISupports, _i1)              \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE2(_class, _i1, _i2)                                 \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(_class, _i1)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i2)                                     \
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, nsISupports, _i1)              \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE3(_class, _i1, _i2, _i3)                            \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(_class, _i1)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i2)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i3)                                     \
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, nsISupports, _i1)              \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE4(_class, _i1, _i2, _i3, _i4)                       \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(_class, _i1)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i2)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i3)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i4)                                     \
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, nsISupports, _i1)              \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE5(_class, _i1, _i2, _i3, _i4, _i5)                  \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(_class, _i1)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i2)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i3)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i4)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i5)                                     \
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, nsISupports, _i1)              \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE6(_class, _i1, _i2, _i3, _i4, _i5, _i6)             \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(_class, _i1)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i2)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i3)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i4)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i5)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i6)                                     \
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, nsISupports, _i1)              \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE7(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7)        \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(_class, _i1)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i2)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i3)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i4)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i5)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i6)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i7)                                     \
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, nsISupports, _i1)              \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE8(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7, _i8)   \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(_class, _i1)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i2)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i3)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i4)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i5)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i6)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i7)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i8)                                     \
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, nsISupports, _i1)              \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE9(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7,        \
                            _i8, _i9)                                         \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(_class, _i1)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i2)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i3)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i4)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i5)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i6)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i7)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i8)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i9)                                     \
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, nsISupports, _i1)              \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE10(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7,       \
                             _i8, _i9, _i10)                                  \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(_class, _i1)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i2)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i3)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i4)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i5)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i6)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i7)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i8)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i9)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i10)                                    \
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, nsISupports, _i1)              \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE11(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7,       \
                             _i8, _i9, _i10, _i11)                            \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(_class, _i1)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i2)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i3)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i4)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i5)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i6)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i7)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i8)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i9)                                     \
    NS_INTERFACE_TABLE_ENTRY(_class, _i10)                                    \
    NS_INTERFACE_TABLE_ENTRY(_class, _i11)                                    \
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(_class, nsISupports, _i1)              \
  NS_INTERFACE_TABLE_END

#define NS_IMPL_QUERY_INTERFACE0(_class)                                      \
  NS_INTERFACE_TABLE_HEAD(_class)                                             \
  NS_INTERFACE_TABLE0(_class)                                                 \
  NS_INTERFACE_TABLE_TAIL

#define NS_IMPL_QUERY_INTERFACE1(_class, _i1)                                 \
  NS_INTERFACE_TABLE_HEAD(_class)                                             \
  NS_INTERFACE_TABLE1(_class, _i1)                                            \
  NS_INTERFACE_TABLE_TAIL

#define NS_IMPL_QUERY_INTERFACE2(_class, _i1, _i2)                            \
  NS_INTERFACE_TABLE_HEAD(_class)                                             \
  NS_INTERFACE_TABLE2(_class, _i1, _i2)                                       \
  NS_INTERFACE_TABLE_TAIL

#define NS_IMPL_QUERY_INTERFACE3(_class, _i1, _i2, _i3)                       \
  NS_INTERFACE_TABLE_HEAD(_class)                                             \
  NS_INTERFACE_TABLE3(_class, _i1, _i2, _i3)                                  \
  NS_INTERFACE_TABLE_TAIL

#define NS_IMPL_QUERY_INTERFACE4(_class, _i1, _i2, _i3, _i4)                  \
  NS_INTERFACE_TABLE_HEAD(_class)                                             \
  NS_INTERFACE_TABLE4(_class, _i1, _i2, _i3, _i4)                             \
  NS_INTERFACE_TABLE_TAIL

#define NS_IMPL_QUERY_INTERFACE5(_class, _i1, _i2, _i3, _i4, _i5)             \
  NS_INTERFACE_TABLE_HEAD(_class)                                             \
  NS_INTERFACE_TABLE5(_class, _i1, _i2, _i3, _i4, _i5)                        \
  NS_INTERFACE_TABLE_TAIL

#define NS_IMPL_QUERY_INTERFACE6(_class, _i1, _i2, _i3, _i4, _i5, _i6)        \
  NS_INTERFACE_TABLE_HEAD(_class)                                             \
  NS_INTERFACE_TABLE6(_class, _i1, _i2, _i3, _i4, _i5, _i6)                   \
  NS_INTERFACE_TABLE_TAIL

#define NS_IMPL_QUERY_INTERFACE7(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7)   \
  NS_INTERFACE_TABLE_HEAD(_class)                                             \
  NS_INTERFACE_TABLE7(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7)              \
  NS_INTERFACE_TABLE_TAIL

#define NS_IMPL_QUERY_INTERFACE8(_class, _i1, _i2, _i3, _i4, _i5, _i6,        \
                                 _i7, _i8)                                    \
  NS_INTERFACE_TABLE_HEAD(_class)                                             \
  NS_INTERFACE_TABLE8(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7, _i8)         \
  NS_INTERFACE_TABLE_TAIL

#define NS_IMPL_QUERY_INTERFACE9(_class, _i1, _i2, _i3, _i4, _i5, _i6,        \
                                 _i7, _i8, _i9)                               \
  NS_INTERFACE_TABLE_HEAD(_class)                                             \
  NS_INTERFACE_TABLE9(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7, _i8, _i9)    \
  NS_INTERFACE_TABLE_TAIL

#define NS_IMPL_QUERY_INTERFACE10(_class, _i1, _i2, _i3, _i4, _i5, _i6,       \
                                  _i7, _i8, _i9, _i10)                        \
  NS_INTERFACE_TABLE_HEAD(_class)                                             \
  NS_INTERFACE_TABLE10(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7, _i8,        \
                       _i9, _i10)                                             \
  NS_INTERFACE_TABLE_TAIL

#define NS_IMPL_QUERY_INTERFACE11(_class, _i1, _i2, _i3, _i4, _i5, _i6,       \
                                  _i7, _i8, _i9, _i10, _i11)                  \
  NS_INTERFACE_TABLE_HEAD(_class)                                             \
  NS_INTERFACE_TABLE11(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7, _i8,        \
                       _i9, _i10, _i11)                                       \
  NS_INTERFACE_TABLE_TAIL


#define NS_IMPL_THREADSAFE_QUERY_INTERFACE0  NS_IMPL_QUERY_INTERFACE0
#define NS_IMPL_THREADSAFE_QUERY_INTERFACE1  NS_IMPL_QUERY_INTERFACE1
#define NS_IMPL_THREADSAFE_QUERY_INTERFACE2  NS_IMPL_QUERY_INTERFACE2
#define NS_IMPL_THREADSAFE_QUERY_INTERFACE3  NS_IMPL_QUERY_INTERFACE3
#define NS_IMPL_THREADSAFE_QUERY_INTERFACE4  NS_IMPL_QUERY_INTERFACE4
#define NS_IMPL_THREADSAFE_QUERY_INTERFACE5  NS_IMPL_QUERY_INTERFACE5
#define NS_IMPL_THREADSAFE_QUERY_INTERFACE6  NS_IMPL_QUERY_INTERFACE6
#define NS_IMPL_THREADSAFE_QUERY_INTERFACE7  NS_IMPL_QUERY_INTERFACE7
#define NS_IMPL_THREADSAFE_QUERY_INTERFACE8  NS_IMPL_QUERY_INTERFACE8
#define NS_IMPL_THREADSAFE_QUERY_INTERFACE9  NS_IMPL_QUERY_INTERFACE9
#define NS_IMPL_THREADSAFE_QUERY_INTERFACE10  NS_IMPL_QUERY_INTERFACE10
#define NS_IMPL_THREADSAFE_QUERY_INTERFACE11  NS_IMPL_QUERY_INTERFACE11

/**
 * Declare that you're going to inherit from something that already
 * implements nsISupports, but also implements an additional interface, thus
 * causing an ambiguity. In this case you don't need another mRefCnt, you
 * just need to forward the definitions to the appropriate superclass. E.g.
 *
 * class Bar : public Foo, public nsIBar {  // both provide nsISupports
 * public:
 *   NS_DECL_ISUPPORTS_INHERITED
 *   ...other nsIBar and Bar methods...
 * };
 */
#define NS_DECL_ISUPPORTS_INHERITED                                           \
public:                                                                       \
  NS_IMETHOD QueryInterface(REFNSIID aIID,                                    \
                            void** aInstancePtr);                             \
  NS_IMETHOD_(nsrefcnt) AddRef(void);                                         \
  NS_IMETHOD_(nsrefcnt) Release(void);                                        \

/**
 * These macros can be used in conjunction with NS_DECL_ISUPPORTS_INHERITED
 * to implement the nsISupports methods, forwarding the invocations to a
 * superclass that already implements nsISupports.
 *
 * Note that I didn't make these inlined because they're virtual methods.
 */

#define NS_IMPL_ADDREF_INHERITED(Class, Super)                                \
NS_IMETHODIMP_(nsrefcnt) Class::AddRef(void)                                  \
{                                                                             \
  nsrefcnt r = Super::AddRef();                                               \
  NS_LOG_ADDREF(this, r, #Class, sizeof(*this));                              \
  return r;                                                                   \
}

#define NS_IMPL_RELEASE_INHERITED(Class, Super)                               \
NS_IMETHODIMP_(nsrefcnt) Class::Release(void)                                 \
{                                                                             \
  nsrefcnt r = Super::Release();                                              \
  NS_LOG_RELEASE(this, r, #Class);                                            \
  return r;                                                                   \
}

/**
 * As above but not logging the addref/release; needed if the base
 * class might be aggregated.
 */
#define NS_IMPL_NONLOGGING_ADDREF_INHERITED(Class, Super)                     \
NS_IMETHODIMP_(nsrefcnt) Class::AddRef(void)                                  \
{                                                                             \
  return Super::AddRef();                                                     \
}

#define NS_IMPL_NONLOGGING_RELEASE_INHERITED(Class, Super)                    \
NS_IMETHODIMP_(nsrefcnt) Class::Release(void)                                 \
{                                                                             \
  return Super::Release();                                                    \
}

#define NS_INTERFACE_TABLE_INHERITED0(Class) /* Nothing to do here */

#define NS_INTERFACE_TABLE_INHERITED1(Class, i1)                              \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(Class, i1)                                       \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE_INHERITED2(Class, i1, i2)                          \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(Class, i1)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i2)                                       \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE_INHERITED3(Class, i1, i2, i3)                      \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(Class, i1)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i2)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i3)                                       \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE_INHERITED4(Class, i1, i2, i3, i4)                  \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(Class, i1)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i2)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i3)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i4)                                       \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE_INHERITED5(Class, i1, i2, i3, i4, i5)              \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(Class, i1)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i2)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i3)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i4)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i5)                                       \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE_INHERITED6(Class, i1, i2, i3, i4, i5, i6)          \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(Class, i1)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i2)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i3)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i4)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i5)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i6)                                       \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE_INHERITED7(Class, i1, i2, i3, i4, i5, i6, i7)      \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(Class, i1)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i2)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i3)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i4)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i5)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i6)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i7)                                       \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE_INHERITED8(Class, i1, i2, i3, i4, i5, i6, i7, i8)  \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(Class, i1)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i2)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i3)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i4)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i5)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i6)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i7)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i8)                                       \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE_INHERITED9(Class, i1, i2, i3, i4, i5, i6, i7,      \
                                      i8, i9)                                 \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(Class, i1)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i2)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i3)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i4)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i5)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i6)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i7)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i8)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i9)                                       \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE_INHERITED10(Class, i1, i2, i3, i4, i5, i6, i7,     \
                                      i8, i9, i10)                            \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(Class, i1)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i2)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i3)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i4)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i5)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i6)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i7)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i8)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i9)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i10)                                      \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE_INHERITED11(Class, i1, i2, i3, i4, i5, i6, i7,     \
                                      i8, i9, i10, i11)                       \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(Class, i1)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i2)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i3)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i4)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i5)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i6)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i7)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i8)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i9)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i10)                                      \
    NS_INTERFACE_TABLE_ENTRY(Class, i11)                                      \
  NS_INTERFACE_TABLE_END

#define NS_INTERFACE_TABLE_INHERITED12(Class, i1, i2, i3, i4, i5, i6, i7,     \
                                      i8, i9, i10, i11, i12)                  \
  NS_INTERFACE_TABLE_BEGIN                                                    \
    NS_INTERFACE_TABLE_ENTRY(Class, i1)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i2)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i3)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i4)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i5)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i6)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i7)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i8)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i9)                                       \
    NS_INTERFACE_TABLE_ENTRY(Class, i10)                                      \
    NS_INTERFACE_TABLE_ENTRY(Class, i11)                                      \
    NS_INTERFACE_TABLE_ENTRY(Class, i12)                                      \
  NS_INTERFACE_TABLE_END

#define NS_IMPL_QUERY_INTERFACE_INHERITED0(Class, Super)                      \
  NS_INTERFACE_TABLE_HEAD(Class)                                              \
  NS_INTERFACE_TABLE_INHERITED0(Class)                                        \
  NS_INTERFACE_TABLE_TAIL_INHERITING(Super)

#define NS_IMPL_QUERY_INTERFACE_INHERITED1(Class, Super, i1)                  \
  NS_INTERFACE_TABLE_HEAD(Class)                                              \
  NS_INTERFACE_TABLE_INHERITED1(Class, i1)                                    \
  NS_INTERFACE_TABLE_TAIL_INHERITING(Super)

#define NS_IMPL_QUERY_INTERFACE_INHERITED2(Class, Super, i1, i2)              \
  NS_INTERFACE_TABLE_HEAD(Class)                                              \
  NS_INTERFACE_TABLE_INHERITED2(Class, i1, i2)                                \
  NS_INTERFACE_TABLE_TAIL_INHERITING(Super)

#define NS_IMPL_QUERY_INTERFACE_INHERITED3(Class, Super, i1, i2, i3)          \
  NS_INTERFACE_TABLE_HEAD(Class)                                              \
  NS_INTERFACE_TABLE_INHERITED3(Class, i1, i2, i3)                            \
  NS_INTERFACE_TABLE_TAIL_INHERITING(Super)

#define NS_IMPL_QUERY_INTERFACE_INHERITED4(Class, Super, i1, i2, i3, i4)      \
  NS_INTERFACE_TABLE_HEAD(Class)                                              \
  NS_INTERFACE_TABLE_INHERITED4(Class, i1, i2, i3, i4)                        \
  NS_INTERFACE_TABLE_TAIL_INHERITING(Super)

#define NS_IMPL_QUERY_INTERFACE_INHERITED5(Class,Super,i1,i2,i3,i4,i5)        \
  NS_INTERFACE_TABLE_HEAD(Class)                                              \
  NS_INTERFACE_TABLE_INHERITED5(Class, i1, i2, i3, i4, i5)                    \
  NS_INTERFACE_TABLE_TAIL_INHERITING(Super)

#define NS_IMPL_QUERY_INTERFACE_INHERITED6(Class,Super,i1,i2,i3,i4,i5,i6)     \
  NS_INTERFACE_TABLE_HEAD(Class)                                              \
  NS_INTERFACE_TABLE_INHERITED6(Class, i1, i2, i3, i4, i5, i6)                \
  NS_INTERFACE_TABLE_TAIL_INHERITING(Super)

#define NS_IMPL_QUERY_INTERFACE_INHERITED7(Class,Super,i1,i2,i3,i4,i5,i6,i7)  \
  NS_INTERFACE_TABLE_HEAD(Class)                                              \
  NS_INTERFACE_TABLE_INHERITED7(Class, i1, i2, i3, i4, i5, i6, i7)            \
  NS_INTERFACE_TABLE_TAIL_INHERITING(Super)

#define NS_IMPL_QUERY_INTERFACE_INHERITED8(Class,Super,i1,i2,i3,i4,i5,i6,     \
                                           i7,i8)                             \
  NS_INTERFACE_TABLE_HEAD(Class)                                              \
  NS_INTERFACE_TABLE_INHERITED8(Class, i1, i2, i3, i4, i5, i6, i7, i8)        \
  NS_INTERFACE_TABLE_TAIL_INHERITING(Super)

#define NS_IMPL_QUERY_INTERFACE_INHERITED9(Class,Super,i1,i2,i3,i4,i5,i6,     \
                                           i7,i8,i9)                          \
  NS_INTERFACE_TABLE_HEAD(Class)                                              \
  NS_INTERFACE_TABLE_INHERITED9(Class, i1, i2, i3, i4, i5, i6, i7, i8, i9)    \
  NS_INTERFACE_TABLE_TAIL_INHERITING(Super)

#define NS_IMPL_QUERY_INTERFACE_INHERITED10(Class,Super,i1,i2,i3,i4,i5,i6,       \
                                            i7,i8,i9,i10)                        \
  NS_INTERFACE_TABLE_HEAD(Class)                                                 \
  NS_INTERFACE_TABLE_INHERITED10(Class, i1, i2, i3, i4, i5, i6, i7, i8, i9, i10) \
  NS_INTERFACE_TABLE_TAIL_INHERITING(Super)

/**
 * Convenience macros for implementing all nsISupports methods for
 * a simple class.
 * @param _class The name of the class implementing the method
 * @param _classiiddef The name of the #define symbol that defines the IID
 * for the class (e.g. NS_ISUPPORTS_IID)
 */

#define NS_IMPL_ISUPPORTS0(_class)                                            \
  NS_IMPL_ADDREF(_class)                                                      \
  NS_IMPL_RELEASE(_class)                                                     \
  NS_IMPL_QUERY_INTERFACE0(_class)

#define NS_IMPL_ISUPPORTS1(_class, _interface)                                \
  NS_IMPL_ADDREF(_class)                                                      \
  NS_IMPL_RELEASE(_class)                                                     \
  NS_IMPL_QUERY_INTERFACE1(_class, _interface)

#define NS_IMPL_ISUPPORTS2(_class, _i1, _i2)                                  \
  NS_IMPL_ADDREF(_class)                                                      \
  NS_IMPL_RELEASE(_class)                                                     \
  NS_IMPL_QUERY_INTERFACE2(_class, _i1, _i2)

#define NS_IMPL_ISUPPORTS3(_class, _i1, _i2, _i3)                             \
  NS_IMPL_ADDREF(_class)                                                      \
  NS_IMPL_RELEASE(_class)                                                     \
  NS_IMPL_QUERY_INTERFACE3(_class, _i1, _i2, _i3)

#define NS_IMPL_ISUPPORTS4(_class, _i1, _i2, _i3, _i4)                        \
  NS_IMPL_ADDREF(_class)                                                      \
  NS_IMPL_RELEASE(_class)                                                     \
  NS_IMPL_QUERY_INTERFACE4(_class, _i1, _i2, _i3, _i4)

#define NS_IMPL_ISUPPORTS5(_class, _i1, _i2, _i3, _i4, _i5)                   \
  NS_IMPL_ADDREF(_class)                                                      \
  NS_IMPL_RELEASE(_class)                                                     \
  NS_IMPL_QUERY_INTERFACE5(_class, _i1, _i2, _i3, _i4, _i5)

#define NS_IMPL_ISUPPORTS6(_class, _i1, _i2, _i3, _i4, _i5, _i6)              \
  NS_IMPL_ADDREF(_class)                                                      \
  NS_IMPL_RELEASE(_class)                                                     \
  NS_IMPL_QUERY_INTERFACE6(_class, _i1, _i2, _i3, _i4, _i5, _i6)

#define NS_IMPL_ISUPPORTS7(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7)         \
  NS_IMPL_ADDREF(_class)                                                      \
  NS_IMPL_RELEASE(_class)                                                     \
  NS_IMPL_QUERY_INTERFACE7(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7)

#define NS_IMPL_ISUPPORTS8(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7, _i8)    \
  NS_IMPL_ADDREF(_class)                                                      \
  NS_IMPL_RELEASE(_class)                                                     \
  NS_IMPL_QUERY_INTERFACE8(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7, _i8)

#define NS_IMPL_ISUPPORTS9(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7, _i8,    \
                           _i9)                                               \
  NS_IMPL_ADDREF(_class)                                                      \
  NS_IMPL_RELEASE(_class)                                                     \
  NS_IMPL_QUERY_INTERFACE9(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7, _i8, _i9)

#define NS_IMPL_ISUPPORTS10(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7, _i8,   \
                            _i9, _i10)                                        \
  NS_IMPL_ADDREF(_class)                                                      \
  NS_IMPL_RELEASE(_class)                                                     \
  NS_IMPL_QUERY_INTERFACE10(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7, _i8,   \
                            _i9, _i10)

#define NS_IMPL_ISUPPORTS11(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7, _i8,   \
                            _i9, _i10, _i11)                                  \
  NS_IMPL_ADDREF(_class)                                                      \
  NS_IMPL_RELEASE(_class)                                                     \
  NS_IMPL_QUERY_INTERFACE11(_class, _i1, _i2, _i3, _i4, _i5, _i6, _i7, _i8,   \
                            _i9, _i10, _i11)

#define NS_IMPL_ISUPPORTS_INHERITED0(Class, Super)                            \
    NS_IMPL_QUERY_INTERFACE_INHERITED0(Class, Super)                          \
    NS_IMPL_ADDREF_INHERITED(Class, Super)                                    \
    NS_IMPL_RELEASE_INHERITED(Class, Super)                                   \

#define NS_IMPL_ISUPPORTS_INHERITED1(Class, Super, i1)                        \
    NS_IMPL_QUERY_INTERFACE_INHERITED1(Class, Super, i1)                      \
    NS_IMPL_ADDREF_INHERITED(Class, Super)                                    \
    NS_IMPL_RELEASE_INHERITED(Class, Super)                                   \

#define NS_IMPL_ISUPPORTS_INHERITED2(Class, Super, i1, i2)                    \
    NS_IMPL_QUERY_INTERFACE_INHERITED2(Class, Super, i1, i2)                  \
    NS_IMPL_ADDREF_INHERITED(Class, Super)                                    \
    NS_IMPL_RELEASE_INHERITED(Class, Super)                                   \

#define NS_IMPL_ISUPPORTS_INHERITED3(Class, Super, i1, i2, i3)                \
    NS_IMPL_QUERY_INTERFACE_INHERITED3(Class, Super, i1, i2, i3)              \
    NS_IMPL_ADDREF_INHERITED(Class, Super)                                    \
    NS_IMPL_RELEASE_INHERITED(Class, Super)                                   \

#define NS_IMPL_ISUPPORTS_INHERITED4(Class, Super, i1, i2, i3, i4)            \
    NS_IMPL_QUERY_INTERFACE_INHERITED4(Class, Super, i1, i2, i3, i4)          \
    NS_IMPL_ADDREF_INHERITED(Class, Super)                                    \
    NS_IMPL_RELEASE_INHERITED(Class, Super)                                   \

#define NS_IMPL_ISUPPORTS_INHERITED5(Class, Super, i1, i2, i3, i4, i5)        \
    NS_IMPL_QUERY_INTERFACE_INHERITED5(Class, Super, i1, i2, i3, i4, i5)      \
    NS_IMPL_ADDREF_INHERITED(Class, Super)                                    \
    NS_IMPL_RELEASE_INHERITED(Class, Super)                                   \

#define NS_IMPL_ISUPPORTS_INHERITED6(Class, Super, i1, i2, i3, i4, i5, i6)    \
    NS_IMPL_QUERY_INTERFACE_INHERITED6(Class, Super, i1, i2, i3, i4, i5, i6)  \
    NS_IMPL_ADDREF_INHERITED(Class, Super)                                    \
    NS_IMPL_RELEASE_INHERITED(Class, Super)                                   \

#define NS_IMPL_ISUPPORTS_INHERITED7(Class, Super, i1, i2, i3, i4, i5, i6, i7) \
    NS_IMPL_QUERY_INTERFACE_INHERITED7(Class, Super, i1, i2, i3, i4, i5, i6, i7) \
    NS_IMPL_ADDREF_INHERITED(Class, Super)                                    \
    NS_IMPL_RELEASE_INHERITED(Class, Super)                                   \

#define NS_IMPL_ISUPPORTS_INHERITED8(Class, Super, i1, i2, i3, i4, i5, i6, i7, i8) \
    NS_IMPL_QUERY_INTERFACE_INHERITED8(Class, Super, i1, i2, i3, i4, i5, i6, i7, i8) \
    NS_IMPL_ADDREF_INHERITED(Class, Super)                                    \
    NS_IMPL_RELEASE_INHERITED(Class, Super)                                   \

#define NS_IMPL_ISUPPORTS_INHERITED9(Class, Super, i1, i2, i3, i4, i5, i6, i7, i8, i9) \
    NS_IMPL_QUERY_INTERFACE_INHERITED9(Class, Super, i1, i2, i3, i4, i5, i6, i7, i8, i9) \
    NS_IMPL_ADDREF_INHERITED(Class, Super)                                    \
    NS_IMPL_RELEASE_INHERITED(Class, Super)                                   \

#define NS_IMPL_ISUPPORTS_INHERITED10(Class, Super, i1, i2, i3, i4, i5, i6, i7, i8, i9, i10) \
    NS_IMPL_QUERY_INTERFACE_INHERITED10(Class, Super, i1, i2, i3, i4, i5, i6, i7, i8, i9, i10) \
    NS_IMPL_ADDREF_INHERITED(Class, Super)                                    \
    NS_IMPL_RELEASE_INHERITED(Class, Super)                                   \
/*
 * Macro to glue together a QI that starts with an interface table
 * and segues into an interface map (e.g. it uses singleton classinfo
 * or tearoffs).
 */
#define NS_INTERFACE_TABLE_TO_MAP_SEGUE \
  if (rv == NS_OK) return rv; \
  nsISupports* foundInterface;


///////////////////////////////////////////////////////////////////////////////
/**
 *
 * Threadsafe implementations of the ISupports convenience macros.
 *
 * @note  These are not available when linking against the standalone glue,
 *        because the implementation requires PR_ symbols.
 */

#if !defined(XPCOM_GLUE_AVOID_NSPR)

/**
 * Use this macro to implement the AddRef method for a given <i>_class</i>
 * @param _class The name of the class implementing the method
 */

#define NS_IMPL_THREADSAFE_ADDREF(_class)                                     \
NS_IMETHODIMP_(nsrefcnt) _class::AddRef(void)                                 \
{                                                                             \
  NS_PRECONDITION(PRInt32(mRefCnt) >= 0, "illegal refcnt");                   \
  nsrefcnt count = NS_AtomicIncrementRefcnt(mRefCnt);                         \
  NS_LOG_ADDREF(this, count, #_class, sizeof(*this));                         \
  return (nsrefcnt) count;                                                    \
}

/**
 * Use this macro to implement the Release method for a given <i>_class</i>
 * @param _class The name of the class implementing the method
 *
 * Note that we don't need to use an atomic operation to stabilize the refcnt.
 * If the refcnt is released to 0, only the current thread has a reference to
 * the object; we thus don't have to use an atomic set to inform other threads
 * that we've changed the refcnt.
 */

#define NS_IMPL_THREADSAFE_RELEASE(_class)                                    \
NS_IMETHODIMP_(nsrefcnt) _class::Release(void)                                \
{                                                                             \
  NS_PRECONDITION(0 != mRefCnt, "dup release");                               \
  nsrefcnt count = NS_AtomicDecrementRefcnt(mRefCnt);                         \
  NS_LOG_RELEASE(this, count, #_class);                                       \
  if (0 == count) {                                                           \
    mRefCnt = 1; /* stabilize */                                              \
    /* enable this to find non-threadsafe destructors: */                     \
    /* NS_ASSERT_OWNINGTHREAD(_class); */                                     \
    delete (this);                                                            \
    return 0;                                                                 \
  }                                                                           \
  return count;                                                               \
}

#else // XPCOM_GLUE_AVOID_NSPR

#define NS_IMPL_THREADSAFE_ADDREF(_class)                                     \
  THREADSAFE_ISUPPORTS_NOT_AVAILABLE_IN_STANDALONE_GLUE;

#define NS_IMPL_THREADSAFE_RELEASE(_class)                                    \
  THREADSAFE_ISUPPORTS_NOT_AVAILABLE_IN_STANDALONE_GLUE;

#endif

#define NS_IMPL_THREADSAFE_ISUPPORTS0(_class)                                 \
  NS_IMPL_THREADSAFE_ADDREF(_class)                                           \
  NS_IMPL_THREADSAFE_RELEASE(_class)                                          \
  NS_IMPL_THREADSAFE_QUERY_INTERFACE0(_class)

#define NS_IMPL_THREADSAFE_ISUPPORTS1(_class, _interface)                     \
  NS_IMPL_THREADSAFE_ADDREF(_class)                                           \
  NS_IMPL_THREADSAFE_RELEASE(_class)                                          \
  NS_IMPL_THREADSAFE_QUERY_INTERFACE1(_class, _interface)

#define NS_IMPL_THREADSAFE_ISUPPORTS2(_class, _i1, _i2)                       \
  NS_IMPL_THREADSAFE_ADDREF(_class)                                           \
  NS_IMPL_THREADSAFE_RELEASE(_class)                                          \
  NS_IMPL_THREADSAFE_QUERY_INTERFACE2(_class, _i1, _i2)

#define NS_IMPL_THREADSAFE_ISUPPORTS3(_class, _i1, _i2, _i3)                  \
  NS_IMPL_THREADSAFE_ADDREF(_class)                                           \
  NS_IMPL_THREADSAFE_RELEASE(_class)                                          \
  NS_IMPL_THREADSAFE_QUERY_INTERFACE3(_class, _i1, _i2, _i3)

#define NS_IMPL_THREADSAFE_ISUPPORTS4(_class, _i1, _i2, _i3, _i4)             \
  NS_IMPL_THREADSAFE_ADDREF(_class)                                           \
  NS_IMPL_THREADSAFE_RELEASE(_class)                                          \
  NS_IMPL_THREADSAFE_QUERY_INTERFACE4(_class, _i1, _i2, _i3, _i4)

#define NS_IMPL_THREADSAFE_ISUPPORTS5(_class, _i1, _i2, _i3, _i4, _i5)        \
  NS_IMPL_THREADSAFE_ADDREF(_class)                                           \
  NS_IMPL_THREADSAFE_RELEASE(_class)                                          \
  NS_IMPL_THREADSAFE_QUERY_INTERFACE5(_class, _i1, _i2, _i3, _i4, _i5)

#define NS_IMPL_THREADSAFE_ISUPPORTS6(_class, _i1, _i2, _i3, _i4, _i5, _i6)   \
  NS_IMPL_THREADSAFE_ADDREF(_class)                                           \
  NS_IMPL_THREADSAFE_RELEASE(_class)                                          \
  NS_IMPL_THREADSAFE_QUERY_INTERFACE6(_class, _i1, _i2, _i3, _i4, _i5, _i6)

#define NS_IMPL_THREADSAFE_ISUPPORTS7(_class, _i1, _i2, _i3, _i4, _i5, _i6,   \
                                      _i7)                                    \
  NS_IMPL_THREADSAFE_ADDREF(_class)                                           \
  NS_IMPL_THREADSAFE_RELEASE(_class)                                          \
  NS_IMPL_THREADSAFE_QUERY_INTERFACE7(_class, _i1, _i2, _i3, _i4, _i5, _i6,   \
                                      _i7)

#define NS_IMPL_THREADSAFE_ISUPPORTS8(_class, _i1, _i2, _i3, _i4, _i5, _i6,   \
                                      _i7, _i8)                               \
  NS_IMPL_THREADSAFE_ADDREF(_class)                                           \
  NS_IMPL_THREADSAFE_RELEASE(_class)                                          \
  NS_IMPL_THREADSAFE_QUERY_INTERFACE8(_class, _i1, _i2, _i3, _i4, _i5, _i6,   \
                                      _i7, _i8)

#define NS_IMPL_THREADSAFE_ISUPPORTS9(_class, _i1, _i2, _i3, _i4, _i5, _i6,   \
                                      _i7, _i8, _i9)                          \
  NS_IMPL_THREADSAFE_ADDREF(_class)                                           \
  NS_IMPL_THREADSAFE_RELEASE(_class)                                          \
  NS_IMPL_THREADSAFE_QUERY_INTERFACE9(_class, _i1, _i2, _i3, _i4, _i5, _i6,   \
                                      _i7, _i8, _i9)

#define NS_IMPL_THREADSAFE_ISUPPORTS10(_class, _i1, _i2, _i3, _i4, _i5, _i6,  \
                                       _i7, _i8, _i9, _i10)                   \
  NS_IMPL_THREADSAFE_ADDREF(_class)                                           \
  NS_IMPL_THREADSAFE_RELEASE(_class)                                          \
  NS_IMPL_THREADSAFE_QUERY_INTERFACE10(_class, _i1, _i2, _i3, _i4, _i5, _i6,  \
                                       _i7, _i8, _i9, _i10)

#define NS_IMPL_THREADSAFE_ISUPPORTS11(_class, _i1, _i2, _i3, _i4, _i5, _i6,  \
                                       _i7, _i8, _i9, _i10, _i11)             \
  NS_IMPL_THREADSAFE_ADDREF(_class)                                           \
  NS_IMPL_THREADSAFE_RELEASE(_class)                                          \
  NS_IMPL_THREADSAFE_QUERY_INTERFACE11(_class, _i1, _i2, _i3, _i4, _i5, _i6,  \
                                       _i7, _i8, _i9, _i10, _i11)

#define NS_INTERFACE_MAP_END_THREADSAFE NS_IMPL_QUERY_TAIL_GUTS

/**
 * Macro to generate nsIClassInfo methods for classes which do not have
 * corresponding nsIFactory implementations.
 */
#define NS_IMPL_THREADSAFE_CI(_class)                                         \
NS_IMETHODIMP                                                                 \
_class::GetInterfaces(PRUint32* _count, nsIID*** _array)                      \
{                                                                             \
  return NS_CI_INTERFACE_GETTER_NAME(_class)(_count, _array);                 \
}                                                                             \
                                                                              \
NS_IMETHODIMP                                                                 \
_class::GetHelperForLanguage(PRUint32 _language, nsISupports** _retval)       \
{                                                                             \
  *_retval = nsnull;                                                          \
  return NS_OK;                                                               \
}                                                                             \
                                                                              \
NS_IMETHODIMP                                                                 \
_class::GetContractID(char** _contractID)                                     \
{                                                                             \
  *_contractID = nsnull;                                                      \
  return NS_OK;                                                               \
}                                                                             \
                                                                              \
NS_IMETHODIMP                                                                 \
_class::GetClassDescription(char** _classDescription)                         \
{                                                                             \
  *_classDescription = nsnull;                                                \
  return NS_OK;                                                               \
}                                                                             \
                                                                              \
NS_IMETHODIMP                                                                 \
_class::GetClassID(nsCID** _classID)                                          \
{                                                                             \
  *_classID = nsnull;                                                         \
  return NS_OK;                                                               \
}                                                                             \
                                                                              \
NS_IMETHODIMP                                                                 \
_class::GetImplementationLanguage(PRUint32* _language)                        \
{                                                                             \
  *_language = nsIProgrammingLanguage::CPLUSPLUS;                             \
  return NS_OK;                                                               \
}                                                                             \
                                                                              \
NS_IMETHODIMP                                                                 \
_class::GetFlags(PRUint32* _flags)                                            \
{                                                                             \
  *_flags = nsIClassInfo::THREADSAFE;                                         \
  return NS_OK;                                                               \
}                                                                             \
                                                                              \
NS_IMETHODIMP                                                                 \
_class::GetClassIDNoAlloc(nsCID* _classIDNoAlloc)                             \
{                                                                             \
  return NS_ERROR_NOT_AVAILABLE;                                              \
}

#endif
