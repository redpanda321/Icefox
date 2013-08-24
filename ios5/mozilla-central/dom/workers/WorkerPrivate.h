/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_workers_workerprivate_h__
#define mozilla_dom_workers_workerprivate_h__

#include "Workers.h"

#include "nsIRunnable.h"
#include "nsIThread.h"
#include "nsIThreadInternal.h"

#include "jsapi.h"
#include "mozilla/CondVar.h"
#include "mozilla/Mutex.h"
#include "mozilla/TimeStamp.h"
#include "nsAutoPtr.h"
#include "nsCOMPtr.h"
#include "nsEventQueue.h"
#include "nsStringGlue.h"
#include "nsTArray.h"
#include "nsTPriorityQueue.h"
#include "StructuredCloneTags.h"

#include "EventTarget.h"
#include "Queue.h"
#include "WorkerFeature.h"

class JSAutoStructuredCloneBuffer;
class nsIDocument;
class nsIPrincipal;
class nsIScriptContext;
class nsIURI;
class nsPIDOMWindow;
class nsITimer;
class nsIXPCScriptNotify;

BEGIN_WORKERS_NAMESPACE

class WorkerMemoryReporter;
class WorkerPrivate;

class WorkerRunnable : public nsIRunnable
{
public:
  enum Target { ParentThread, WorkerThread };
  enum BusyBehavior { ModifyBusyCount, UnchangedBusyCount };
  enum ClearingBehavior { SkipWhenClearing, RunWhenClearing };

protected:
  WorkerPrivate* mWorkerPrivate;
  Target mTarget;
  BusyBehavior mBusyBehavior;
  ClearingBehavior mClearingBehavior;

public:
  NS_DECL_ISUPPORTS

  bool
  Dispatch(JSContext* aCx);

  static bool
  DispatchToMainThread(nsIRunnable*);

  bool
  WantsToRunDuringClear()
  {
    return mClearingBehavior == RunWhenClearing;
  }

protected:
  WorkerRunnable(WorkerPrivate* aWorkerPrivate, Target aTarget,
                 BusyBehavior aBusyBehavior,
                 ClearingBehavior aClearingBehavior)
#ifdef DEBUG
  ;
#else
  : mWorkerPrivate(aWorkerPrivate), mTarget(aTarget),
    mBusyBehavior(aBusyBehavior), mClearingBehavior(aClearingBehavior)
  { }
#endif

  virtual ~WorkerRunnable()
  { }

  virtual bool
  PreDispatch(JSContext* aCx, WorkerPrivate* aWorkerPrivate);

  virtual void
  PostDispatch(JSContext* aCx, WorkerPrivate* aWorkerPrivate,
               bool aDispatchResult);

  virtual bool
  DispatchInternal();

  virtual bool
  WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) = 0;

  virtual void
  PostRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate, bool aRunResult);

  void NotifyScriptExecutedIfNeeded() const;

private:
  NS_DECL_NSIRUNNABLE
};

class WorkerSyncRunnable : public WorkerRunnable
{
protected:
  PRUint32 mSyncQueueKey;
  bool mBypassSyncQueue;

protected:
  friend class WorkerPrivate;

  WorkerSyncRunnable(WorkerPrivate* aWorkerPrivate, PRUint32 aSyncQueueKey,
                     bool aBypassSyncQueue = false,
                     ClearingBehavior aClearingBehavior = SkipWhenClearing)
  : WorkerRunnable(aWorkerPrivate, WorkerThread, UnchangedBusyCount,
                   aClearingBehavior),
    mSyncQueueKey(aSyncQueueKey), mBypassSyncQueue(aBypassSyncQueue)
  { }

  virtual ~WorkerSyncRunnable()
  { }

  virtual bool
  DispatchInternal();
};

class WorkerControlRunnable : public WorkerRunnable
{
protected:
  WorkerControlRunnable(WorkerPrivate* aWorkerPrivate, Target aTarget,
                        BusyBehavior aBusyBehavior)
  : WorkerRunnable(aWorkerPrivate, aTarget, aBusyBehavior, SkipWhenClearing)
  { }

  virtual ~WorkerControlRunnable()
  { }

  virtual bool
  DispatchInternal();
};

template <class Derived>
class WorkerPrivateParent : public EventTarget
{
public:
  struct LocationInfo
  {
    nsCString mHref;
    nsCString mProtocol;
    nsCString mHost;
    nsCString mHostname;
    nsCString mPort;
    nsCString mPathname;
    nsCString mSearch;
    nsCString mHash;
  };

protected:
  mozilla::Mutex mMutex;
  mozilla::CondVar mCondVar;

private:
  JSObject* mJSObject;
  WorkerPrivate* mParent;
  JSContext* mParentJSContext;
  nsString mScriptURL;
  nsCString mDomain;
  LocationInfo mLocationInfo;

  // Main-thread things.
  nsCOMPtr<nsPIDOMWindow> mWindow;
  nsCOMPtr<nsIScriptContext> mScriptContext;
  nsCOMPtr<nsIXPCScriptNotify> mScriptNotify;
  nsCOMPtr<nsIURI> mBaseURI;
  nsCOMPtr<nsIURI> mScriptURI;
  nsCOMPtr<nsIPrincipal> mPrincipal;
  nsCOMPtr<nsIDocument> mDocument;

  // Only used for top level workers.
  nsTArray<nsRefPtr<WorkerRunnable> > mQueuedRunnables;

  PRUint64 mBusyCount;
  Status mParentStatus;
  PRUint32 mJSContextOptions;
  PRUint32 mJSRuntimeHeapSize;
  PRUint8 mGCZeal;
  bool mJSObjectRooted;
  bool mParentSuspended;
  bool mIsChromeWorker;
  bool mPrincipalIsSystem;
  bool mMainThreadObjectsForgotten;

protected:
  WorkerPrivateParent(JSContext* aCx, JSObject* aObject, WorkerPrivate* aParent,
                      JSContext* aParentJSContext, const nsAString& aScriptURL,
                      bool aIsChromeWorker, const nsACString& aDomain,
                      nsCOMPtr<nsPIDOMWindow>& aWindow,
                      nsCOMPtr<nsIScriptContext>& aScriptContext,
                      nsCOMPtr<nsIURI>& aBaseURI,
                      nsCOMPtr<nsIPrincipal>& aPrincipal,
                      nsCOMPtr<nsIDocument>& aDocument);

  ~WorkerPrivateParent();

private:
  Derived*
  ParentAsWorkerPrivate() const
  {
    return static_cast<Derived*>(const_cast<WorkerPrivateParent*>(this));
  }

  // aCx is null when called from the finalizer
  bool
  NotifyPrivate(JSContext* aCx, Status aStatus);

  // aCx is null when called from the finalizer
  bool
  TerminatePrivate(JSContext* aCx)
  {
    return NotifyPrivate(aCx, Terminating);
  }

public:
  // May be called on any thread...
  bool
  Start();

  // Called on the parent thread.
  bool
  Notify(JSContext* aCx, Status aStatus)
  {
    return NotifyPrivate(aCx, aStatus);
  }

  bool
  Cancel(JSContext* aCx)
  {
    return Notify(aCx, Canceling);
  }

  bool
  Kill(JSContext* aCx)
  {
    return Notify(aCx, Killing);
  }

  bool
  Suspend(JSContext* aCx);

  bool
  Resume(JSContext* aCx);

  virtual void
  _trace(JSTracer* aTrc) MOZ_OVERRIDE;

  virtual void
  _finalize(JSFreeOp* aFop) MOZ_OVERRIDE;

  void
  Finish(JSContext* aCx)
  {
    RootJSObject(aCx, false);
  }

  bool
  Terminate(JSContext* aCx)
  {
    AssertIsOnParentThread();
    RootJSObject(aCx, false);
    return TerminatePrivate(aCx);
  }

  bool
  Close(JSContext* aCx);

  bool
  ModifyBusyCount(JSContext* aCx, bool aIncrease);

  bool
  RootJSObject(JSContext* aCx, bool aRoot);

  void
  ForgetMainThreadObjects(nsTArray<nsCOMPtr<nsISupports> >& aDoomed);

  bool
  PostMessage(JSContext* aCx, jsval aMessage);

  PRUint64
  GetInnerWindowId();

  void
  UpdateJSContextOptions(JSContext* aCx, PRUint32 aOptions);

  void
  UpdateJSRuntimeHeapSize(JSContext* aCx, PRUint32 aJSRuntimeHeapSize);

#ifdef JS_GC_ZEAL
  void
  UpdateGCZeal(JSContext* aCx, PRUint8 aGCZeal);
#endif

  void
  GarbageCollect(JSContext* aCx, bool aShrinking);

  void
  QueueRunnable(WorkerRunnable* aRunnable)
  {
    AssertIsOnMainThread();
    mQueuedRunnables.AppendElement(aRunnable);
  }

  WorkerPrivate*
  GetParent() const
  {
    return mParent;
  }

  bool
  IsSuspended() const
  {
    AssertIsOnParentThread();
    return mParentSuspended;
  }

  bool
  IsAcceptingEvents()
  {
    AssertIsOnParentThread();
    bool acceptingEvents;
    mMutex.Lock();
    acceptingEvents = mParentStatus < Terminating;
    mMutex.Unlock();
    return acceptingEvents;
  }

  Status
  ParentStatus() const
  {
    mMutex.AssertCurrentThreadOwns();
    return mParentStatus;
  }

  JSContext*
  ParentJSContext() const;

  nsIScriptContext*
  GetScriptContext() const
  {
    AssertIsOnMainThread();
    return mScriptContext;
  }

  nsIXPCScriptNotify*
  GetScriptNotify() const
  {
    AssertIsOnMainThread();
    return mScriptNotify;
  }

  JSObject*
  GetJSObject() const
  {
    return mJSObject;
  }

  const nsString&
  ScriptURL() const
  {
    return mScriptURL;
  }

  const nsCString&
  Domain() const
  {
    return mDomain;
  }

  nsIURI*
  GetBaseURI() const
  {
    AssertIsOnMainThread();
    return mBaseURI;
  }

  void
  SetBaseURI(nsIURI* aBaseURI);

  nsIURI*
  GetScriptURI() const
  {
    AssertIsOnMainThread();
    return mScriptURI;
  }

  void
  SetScriptURI(nsIURI* aScriptURI)
  {
    AssertIsOnMainThread();
    mScriptURI = aScriptURI;
  }

  nsIPrincipal*
  GetPrincipal() const
  {
    AssertIsOnMainThread();
    return mPrincipal;
  }

  void
  SetPrincipal(nsIPrincipal* aPrincipal);

  bool
  UsesSystemPrincipal() const
  {
    return mPrincipalIsSystem;
  }

  nsIDocument*
  GetDocument() const
  {
    AssertIsOnMainThread();
    return mDocument;
  }

  void
  SetDocument(nsIDocument* aDocument)
  {
    AssertIsOnMainThread();
    mDocument = aDocument;
  }

  nsPIDOMWindow*
  GetWindow()
  {
    AssertIsOnMainThread();
    return mWindow;
  }

  LocationInfo&
  GetLocationInfo()
  {
    return mLocationInfo;
  }

  PRUint32
  GetJSContextOptions() const
  {
    return mJSContextOptions;
  }

  PRUint32
  GetJSRuntimeHeapSize() const
  {
    return mJSRuntimeHeapSize;
  }

#ifdef JS_GC_ZEAL
  PRUint8
  GetGCZeal() const
  {
    return mGCZeal;
  }
#endif

  bool
  IsChromeWorker() const
  {
    return mIsChromeWorker;
  }

#ifdef DEBUG
  void
  AssertIsOnParentThread() const;

  void
  AssertInnerWindowIsCorrect() const;
#else
  void
  AssertIsOnParentThread() const
  { }

  void
  AssertInnerWindowIsCorrect() const
  { }
#endif
};

class WorkerPrivate : public WorkerPrivateParent<WorkerPrivate>
{
  friend class WorkerPrivateParent<WorkerPrivate>;
  friend class WorkerMemoryReporter;
  typedef WorkerPrivateParent<WorkerPrivate> ParentType;

  struct TimeoutInfo;

  typedef Queue<WorkerRunnable*, 50> EventQueue;
  EventQueue mQueue;
  EventQueue mControlQueue;

  struct SyncQueue
  {
    Queue<WorkerRunnable*, 10> mQueue;
    bool mComplete;
    bool mResult;

    SyncQueue()
    : mComplete(false), mResult(false)
    { }

    ~SyncQueue()
    {
      WorkerRunnable* event;
      while (mQueue.Pop(event)) {
        event->Release();
      }
    }
  };

  nsTArray<nsAutoPtr<SyncQueue> > mSyncQueues;

  // Touched on multiple threads, protected with mMutex.
  JSContext* mJSContext;
  nsRefPtr<WorkerCrossThreadDispatcher> mCrossThreadDispatcher;

  // Things touched on worker thread only.
  nsTArray<ParentType*> mChildWorkers;
  nsTArray<WorkerFeature*> mFeatures;
  nsTArray<nsAutoPtr<TimeoutInfo> > mTimeouts;

  nsCOMPtr<nsITimer> mTimer;
  nsRefPtr<WorkerMemoryReporter> mMemoryReporter;

  mozilla::TimeStamp mKillTime;
  PRUint32 mErrorHandlerRecursionCount;
  PRUint32 mNextTimeoutId;
  Status mStatus;
  bool mSuspended;
  bool mTimerRunning;
  bool mRunningExpiredTimeouts;
  bool mCloseHandlerStarted;
  bool mCloseHandlerFinished;
  bool mMemoryReporterRunning;

#ifdef DEBUG
  nsCOMPtr<nsIThread> mThread;
#endif

public:
  ~WorkerPrivate();

  static already_AddRefed<WorkerPrivate>
  Create(JSContext* aCx, JSObject* aObj, WorkerPrivate* aParent,
         JSString* aScriptURL, bool aIsChromeWorker);

  void
  DoRunLoop(JSContext* aCx);

  bool
  OperationCallback(JSContext* aCx);

  bool
  Dispatch(WorkerRunnable* aEvent)
  {
    return Dispatch(aEvent, &mQueue);
  }

  bool
  Dispatch(WorkerSyncRunnable* aEvent)
  {
    if (aEvent->mBypassSyncQueue) {
      return Dispatch(aEvent, &mQueue);
    }

    return DispatchToSyncQueue(aEvent);
  }

  bool
  Dispatch(WorkerControlRunnable* aEvent)
  {
    return Dispatch(aEvent, &mControlQueue);
  }

  bool
  CloseInternal(JSContext* aCx)
  {
    AssertIsOnWorkerThread();
    return NotifyInternal(aCx, Closing);
  }

  bool
  SuspendInternal(JSContext* aCx);

  bool
  ResumeInternal(JSContext* aCx);

  void
  TraceInternal(JSTracer* aTrc);

  bool
  ModifyBusyCountFromWorker(JSContext* aCx, bool aIncrease);

  bool
  AddChildWorker(JSContext* aCx, ParentType* aChildWorker);

  void
  RemoveChildWorker(JSContext* aCx, ParentType* aChildWorker);

  bool
  AddFeature(JSContext* aCx, WorkerFeature* aFeature);

  void
  RemoveFeature(JSContext* aCx, WorkerFeature* aFeature);

  void
  NotifyFeatures(JSContext* aCx, Status aStatus);

  bool
  HasActiveFeatures()
  {
    return !(mChildWorkers.IsEmpty() && mTimeouts.IsEmpty() &&
             mFeatures.IsEmpty());
  }

  PRUint32
  CreateNewSyncLoop();

  bool
  RunSyncLoop(JSContext* aCx, PRUint32 aSyncLoopKey);

  void
  StopSyncLoop(PRUint32 aSyncLoopKey, bool aSyncResult);

  bool
  PostMessageToParent(JSContext* aCx, jsval aMessage);

  bool
  NotifyInternal(JSContext* aCx, Status aStatus);

  void
  ReportError(JSContext* aCx, const char* aMessage, JSErrorReport* aReport);

  bool
  SetTimeout(JSContext* aCx, unsigned aArgc, jsval* aVp, bool aIsInterval);

  bool
  ClearTimeout(JSContext* aCx, uint32 aId);

  bool
  RunExpiredTimeouts(JSContext* aCx);

  bool
  RescheduleTimeoutTimer(JSContext* aCx);

  void
  CloseHandlerStarted()
  {
    AssertIsOnWorkerThread();
    mCloseHandlerStarted = true;
  }

  void
  CloseHandlerFinished()
  {
    AssertIsOnWorkerThread();
    mCloseHandlerFinished = true;
  }

  void
  UpdateJSContextOptionsInternal(JSContext* aCx, PRUint32 aOptions);

  void
  UpdateJSRuntimeHeapSizeInternal(JSContext* aCx, PRUint32 aJSRuntimeHeapSize);

  void
  ScheduleDeletion(bool aWasPending);

  bool
  BlockAndCollectRuntimeStats(bool isQuick, void* aData);

  bool
  DisableMemoryReporter();

#ifdef JS_GC_ZEAL
  void
  UpdateGCZealInternal(JSContext* aCx, PRUint8 aGCZeal);
#endif

  void
  GarbageCollectInternal(JSContext* aCx, bool aShrinking,
                         bool aCollectChildren);

  JSContext*
  GetJSContext() const
  {
    AssertIsOnWorkerThread();
    return mJSContext;
  }

#ifdef DEBUG
  void
  AssertIsOnWorkerThread() const;

  void
  SetThread(nsIThread* aThread)
  {
    mThread = aThread;
  }
#else
  void
  AssertIsOnWorkerThread() const
  { }
#endif

  WorkerCrossThreadDispatcher*
  GetCrossThreadDispatcher();

private:
  WorkerPrivate(JSContext* aCx, JSObject* aObject, WorkerPrivate* aParent,
                JSContext* aParentJSContext, const nsAString& aScriptURL,
                bool aIsChromeWorker, const nsACString& aDomain,
                nsCOMPtr<nsPIDOMWindow>& aWindow,
                nsCOMPtr<nsIScriptContext>& aScriptContext,
                nsCOMPtr<nsIURI>& aBaseURI, nsCOMPtr<nsIPrincipal>& aPrincipal,
                nsCOMPtr<nsIDocument>& aDocument);

  bool
  Dispatch(WorkerRunnable* aEvent, EventQueue* aQueue);

  bool
  DispatchToSyncQueue(WorkerSyncRunnable* aEvent);

  void
  ClearQueue(EventQueue* aQueue);

  bool
  MayContinueRunning()
  {
    AssertIsOnWorkerThread();

    Status status;
    {
      mozilla::MutexAutoLock lock(mMutex);
      status = mStatus;
    }

    if (status >= Killing) {
      return false;
    }
    if (status >= Running) {
      return mKillTime.IsNull() || RemainingRunTimeMS() > 0;
    }
    return true;
  }

  PRUint32
  RemainingRunTimeMS() const;

  void
  CancelAllTimeouts(JSContext* aCx);

  bool
  ScheduleKillCloseEventRunnable(JSContext* aCx);

  void
  StopAcceptingEvents()
  {
    AssertIsOnWorkerThread();

    mozilla::MutexAutoLock lock(mMutex);

    mStatus = Dead;
    mJSContext = nsnull;

    ClearQueue(&mControlQueue);
    ClearQueue(&mQueue);
  }

  bool
  ProcessAllControlRunnables();
};

WorkerPrivate*
GetWorkerPrivateFromContext(JSContext* aCx);

enum WorkerStructuredDataType
{
  DOMWORKER_SCTAG_FILE = SCTAG_DOM_MAX,
  DOMWORKER_SCTAG_BLOB,

  DOMWORKER_SCTAG_END
};

JSStructuredCloneCallbacks*
WorkerStructuredCloneCallbacks(bool aMainRuntime);

JSStructuredCloneCallbacks*
ChromeWorkerStructuredCloneCallbacks(bool aMainRuntime);

END_WORKERS_NAMESPACE

#endif /* mozilla_dom_workers_workerprivate_h__ */
