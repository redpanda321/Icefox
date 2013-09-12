/* -*- Mode: C++; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil -*- */
/* vim: set ts=4 sw=4 et tw=99: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Definitions for managing off-main-thread work using a shared, per runtime
 * worklist. Worklist items are engine internal, and are distinct from e.g.
 * web workers.
 */

#ifndef jsworkers_h___
#define jsworkers_h___

#include "jscntxt.h"
#include "jslock.h"

namespace js {

namespace ion {
  class IonBuilder;
}

#if defined(JS_THREADSAFE) && defined(JS_ION)
# define JS_PARALLEL_COMPILATION

struct WorkerThread;

/* Per-runtime state for off thread work items. */
class WorkerThreadState
{
  public:
    /* Available threads. */
    WorkerThread *threads;
    size_t numThreads;

    enum CondVar {
        MAIN,
        WORKER
    };

    /* Shared worklist for helper threads. */
    js::Vector<ion::IonBuilder*, 0, SystemAllocPolicy> ionWorklist;

    WorkerThreadState() { PodZero(this); }
    ~WorkerThreadState();

    bool init(JSRuntime *rt);

    void lock();
    void unlock();

# ifdef DEBUG
    bool isLocked();
# endif

    void wait(CondVar which, uint32_t timeoutMillis = 0);
    void notify(CondVar which);
    void notifyAll(CondVar which);

    bool canStartIonCompile();

  private:

    /*
     * Lock protecting all mutable shared state accessed by helper threads, and
     * used by all condition variables.
     */
    PRLock *workerLock;

# ifdef DEBUG
    PRThread *lockOwner;
# endif

    /* Condvar to notify the main thread that work has been completed. */
    PRCondVar *mainWakeup;

    /* Condvar to notify helper threads that they may be able to make progress. */
    PRCondVar *helperWakeup;
};

/* Individual helper thread, one allocated per core. */
struct WorkerThread
{
    JSRuntime *runtime;

    mozilla::Maybe<PerThreadData> threadData;
    PRThread *thread;

    /* Indicate to an idle thread that it should finish executing. */
    bool terminate;

    /* Any builder currently being compiled by Ion on this thread. */
    ion::IonBuilder *ionBuilder;

    void destroy();

    static void ThreadMain(void *arg);
    void threadLoop();
};

#endif /* JS_THREADSAFE && JS_ION */

/* Methods for interacting with worker threads. */

/*
 * Schedule an Ion compilation for a script, given a builder which has been
 * generated and read everything needed from the VM state.
 */
bool
StartOffThreadIonCompile(JSContext *cx, ion::IonBuilder *builder);

/*
 * Cancel a scheduled or in progress Ion compilation for script. If script is
 * NULL, all compilations for the compartment are cancelled.
 */
void
CancelOffThreadIonCompile(JSCompartment *compartment, JSScript *script);

/* Return true iff off-thread compilation is possible. */
bool
OffThreadCompilationAvailable(JSContext *cx);

class AutoLockWorkerThreadState
{
    JSRuntime *rt;
    JS_DECL_USE_GUARD_OBJECT_NOTIFIER

  public:

    AutoLockWorkerThreadState(JSRuntime *rt JS_GUARD_OBJECT_NOTIFIER_PARAM)
      : rt(rt)
    {
        JS_GUARD_OBJECT_NOTIFIER_INIT;
#ifdef JS_PARALLEL_COMPILATION
        JS_ASSERT(rt->workerThreadState);
        rt->workerThreadState->lock();
#else
        (void)this->rt;
#endif
    }

    ~AutoLockWorkerThreadState()
    {
#ifdef JS_PARALLEL_COMPILATION
        rt->workerThreadState->unlock();
#endif
    }
};

class AutoUnlockWorkerThreadState
{
    JSRuntime *rt;
    JS_DECL_USE_GUARD_OBJECT_NOTIFIER

  public:

    AutoUnlockWorkerThreadState(JSRuntime *rt JS_GUARD_OBJECT_NOTIFIER_PARAM)
      : rt(rt)
    {
        JS_GUARD_OBJECT_NOTIFIER_INIT;
#ifdef JS_PARALLEL_COMPILATION
        JS_ASSERT(rt->workerThreadState);
        rt->workerThreadState->unlock();
#else
        (void)this->rt;
#endif
    }

    ~AutoUnlockWorkerThreadState()
    {
#ifdef JS_PARALLEL_COMPILATION
        rt->workerThreadState->lock();
#endif
    }
};

} /* namespace js */

#endif // jsworkers_h___
