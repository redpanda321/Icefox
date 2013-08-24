/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_workers_runtimeservice_h__
#define mozilla_dom_workers_runtimeservice_h__

#include "Workers.h"

#include "nsIObserver.h"

#include "jsapi.h"
#include "mozilla/Mutex.h"
#include "mozilla/TimeStamp.h"
#include "nsAutoPtr.h"
#include "nsClassHashtable.h"
#include "nsCOMPtr.h"
#include "nsHashKeys.h"
#include "nsStringGlue.h"
#include "nsTArray.h"
#include "mozilla/Attributes.h"

class nsIThread;
class nsITimer;
class nsPIDOMWindow;

BEGIN_WORKERS_NAMESPACE

class WorkerPrivate;

class RuntimeService MOZ_FINAL : public nsIObserver
{
  struct WorkerDomainInfo
  {
    nsCString mDomain;
    nsTArray<WorkerPrivate*> mActiveWorkers;
    nsTArray<WorkerPrivate*> mQueuedWorkers;
    PRUint32 mChildWorkerCount;

    WorkerDomainInfo() : mActiveWorkers(1), mChildWorkerCount(0) { }

    PRUint32
    ActiveWorkerCount() const
    {
      return mActiveWorkers.Length() + mChildWorkerCount;
    }
  };

  struct IdleThreadInfo
  {
    nsCOMPtr<nsIThread> mThread;
    mozilla::TimeStamp mExpirationTime;
  };

  mozilla::Mutex mMutex;

  // Protected by mMutex.
  nsClassHashtable<nsCStringHashKey, WorkerDomainInfo> mDomainMap;

  // Protected by mMutex.
  nsTArray<IdleThreadInfo> mIdleThreadArray;

  // *Not* protected by mMutex.
  nsClassHashtable<nsPtrHashKey<nsPIDOMWindow>, nsTArray<WorkerPrivate*> > mWindowMap;

  // Only used on the main thread.
  nsCOMPtr<nsITimer> mIdleThreadTimer;

  nsCString mDetectorName;
  nsCString mSystemCharset;

  static PRUint32 sDefaultJSContextOptions;
  static PRUint32 sDefaultJSRuntimeHeapSize;
  static PRInt32 sCloseHandlerTimeoutSeconds;

#ifdef JS_GC_ZEAL
  static PRUint8 sDefaultGCZeal;
#endif

public:
  struct NavigatorStrings
  {
    nsString mAppName;
    nsString mAppVersion;
    nsString mPlatform;
    nsString mUserAgent;
  };

private:
  NavigatorStrings mNavigatorStrings;

  // True when the observer service holds a reference to this object.
  bool mObserved;
  bool mShuttingDown;
  bool mNavigatorStringsLoaded;

public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  static RuntimeService*
  GetOrCreateService();

  static RuntimeService*
  GetService();

  bool
  RegisterWorker(JSContext* aCx, WorkerPrivate* aWorkerPrivate);

  void
  UnregisterWorker(JSContext* aCx, WorkerPrivate* aWorkerPrivate);

  void
  CancelWorkersForWindow(JSContext* aCx, nsPIDOMWindow* aWindow);

  void
  SuspendWorkersForWindow(JSContext* aCx, nsPIDOMWindow* aWindow);

  void
  ResumeWorkersForWindow(JSContext* aCx, nsPIDOMWindow* aWindow);

  const nsACString&
  GetDetectorName() const
  {
    return mDetectorName;
  }

  const nsACString&
  GetSystemCharset() const
  {
    return mSystemCharset;
  }

  const NavigatorStrings&
  GetNavigatorStrings() const
  {
    return mNavigatorStrings;
  }

  void
  NoteIdleThread(nsIThread* aThread);

  static PRUint32
  GetDefaultJSContextOptions()
  {
    AssertIsOnMainThread();
    return sDefaultJSContextOptions;
  }

  static void
  SetDefaultJSContextOptions(PRUint32 aOptions)
  {
    AssertIsOnMainThread();
    sDefaultJSContextOptions = aOptions;
  }

  void
  UpdateAllWorkerJSContextOptions();

  static PRUint32
  GetDefaultJSRuntimeHeapSize()
  {
    AssertIsOnMainThread();
    return sDefaultJSRuntimeHeapSize;
  }

  static void
  SetDefaultJSRuntimeHeapSize(PRUint32 aMaxBytes)
  {
    AssertIsOnMainThread();
    sDefaultJSRuntimeHeapSize = aMaxBytes;
  }

  void
  UpdateAllWorkerJSRuntimeHeapSize();

  static PRUint32
  GetCloseHandlerTimeoutSeconds()
  {
    return sCloseHandlerTimeoutSeconds > 0 ? sCloseHandlerTimeoutSeconds : 0;
  }

#ifdef JS_GC_ZEAL
  static PRUint8
  GetDefaultGCZeal()
  {
    AssertIsOnMainThread();
    return sDefaultGCZeal;
  }

  static void
  SetDefaultGCZeal(PRUint8 aGCZeal)
  {
    AssertIsOnMainThread();
    sDefaultGCZeal = aGCZeal;
  }

  void
  UpdateAllWorkerGCZeal();
#endif

  void
  GarbageCollectAllWorkers(bool aShrinking);

  class AutoSafeJSContext
  {
    JSContext* mContext;

  public:
    AutoSafeJSContext(JSContext* aCx = nsnull);
    ~AutoSafeJSContext();

    operator JSContext*() const
    {
      return mContext;
    }

    static JSContext*
    GetSafeContext();
  };

private:
  RuntimeService();
  ~RuntimeService();

  nsresult
  Init();

  void
  Cleanup();

  static PLDHashOperator
  AddAllTopLevelWorkersToArray(const nsACString& aKey,
                               WorkerDomainInfo* aData,
                               void* aUserArg);

  void
  GetWorkersForWindow(nsPIDOMWindow* aWindow,
                      nsTArray<WorkerPrivate*>& aWorkers);

  bool
  ScheduleWorker(JSContext* aCx, WorkerPrivate* aWorkerPrivate);

  static void
  ShutdownIdleThreads(nsITimer* aTimer, void* aClosure);
};

END_WORKERS_NAMESPACE

#endif /* mozilla_dom_workers_runtimeservice_h__ */
