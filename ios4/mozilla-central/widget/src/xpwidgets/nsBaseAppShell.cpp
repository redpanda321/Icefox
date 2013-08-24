/* -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Widget code.
 *
 * The Initial Developer of the Original Code is Google Inc.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Darin Fisher <darin@meer.net> (original author)
 *   Mats Palmgren <mats.palmgren@bredband.net>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsBaseAppShell.h"
#include "nsThreadUtils.h"
#include "nsIObserverService.h"
#include "nsServiceManagerUtils.h"
#include "mozilla/Services.h"

#ifdef MOZ_IPC
#include "base/message_loop.h"
#endif

// When processing the next thread event, the appshell may process native
// events (if not in performance mode), which can result in suppressing the
// next thread event for at most this many ticks:
#define THREAD_EVENT_STARVATION_LIMIT PR_MillisecondsToInterval(20)

NS_IMPL_THREADSAFE_ISUPPORTS3(nsBaseAppShell, nsIAppShell, nsIThreadObserver,
                              nsIObserver)

nsBaseAppShell::nsBaseAppShell()
  : mSuspendNativeCount(0)
  , mBlockedWait(nsnull)
  , mFavorPerf(0)
  , mNativeEventPending(0)
  , mEventloopNestingLevel(0)
  , mStarvationDelay(0)
  , mSwitchTime(0)
  , mLastNativeEventTime(0)
  , mEventloopNestingState(eEventloopNone)
  , mRunning(PR_FALSE)
  , mExiting(PR_FALSE)
  , mBlockNativeEvent(PR_FALSE)
{
}

nsresult
nsBaseAppShell::Init()
{
  // Configure ourselves as an observer for the current thread:

  nsCOMPtr<nsIThreadInternal> threadInt =
      do_QueryInterface(NS_GetCurrentThread());
  NS_ENSURE_STATE(threadInt);

  threadInt->SetObserver(this);

  nsCOMPtr<nsIObserverService> obsSvc =
    mozilla::services::GetObserverService();
  if (obsSvc)
    obsSvc->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, PR_FALSE);
  return NS_OK;
}

void
nsBaseAppShell::NativeEventCallback()
{
  PRInt32 hasPending = PR_AtomicSet(&mNativeEventPending, 0);
  if (hasPending == 0)
    return;

  // If DoProcessNextNativeEvent is on the stack, then we assume that we can
  // just unwind and let nsThread::ProcessNextEvent process the next event.
  // However, if we are called from a nested native event loop (maybe via some
  // plug-in or library function), then go ahead and process Gecko events now.
  if (mEventloopNestingState == eEventloopXPCOM) {
    mEventloopNestingState = eEventloopOther;
    // XXX there is a tiny risk we will never get a new NativeEventCallback,
    // XXX see discussion in bug 389931.
    return;
  }

  // nsBaseAppShell::Run is not being used to pump events, so this may be
  // our only opportunity to process pending gecko events.

  nsIThread *thread = NS_GetCurrentThread();
  PRBool prevBlockNativeEvent = mBlockNativeEvent;
  if (mEventloopNestingState == eEventloopOther) {
    if (!NS_HasPendingEvents(thread))
      return;
    // We're in a nested native event loop and have some gecko events to
    // process.  While doing that we block processing native events from the
    // appshell - instead, we want to get back to the nested native event
    // loop ASAP (bug 420148).
    mBlockNativeEvent = PR_TRUE;
  }

  ++mEventloopNestingLevel;
  EventloopNestingState prevVal = mEventloopNestingState;
  NS_ProcessPendingEvents(thread, THREAD_EVENT_STARVATION_LIMIT);
  mEventloopNestingState = prevVal;
  mBlockNativeEvent = prevBlockNativeEvent;

  // Continue processing pending events later (we don't want to starve the
  // embedders event loop).
  if (NS_HasPendingEvents(thread))
    OnDispatchedEvent(nsnull);

  --mEventloopNestingLevel;
}

PRBool
nsBaseAppShell::DoProcessNextNativeEvent(PRBool mayWait)
{
  // The next native event to be processed may trigger our NativeEventCallback,
  // in which case we do not want it to process any thread events since we'll
  // do that when this function returns.
  //
  // If the next native event is not our NativeEventCallback, then we may end
  // up recursing into this function.
  //
  // However, if the next native event is not our NativeEventCallback, but it
  // results in another native event loop, then our NativeEventCallback could
  // fire and it will see mEventloopNestingState as eEventloopOther.
  //
  EventloopNestingState prevVal = mEventloopNestingState;
  mEventloopNestingState = eEventloopXPCOM;

  ++mEventloopNestingLevel;
  PRBool result = ProcessNextNativeEvent(mayWait);
  --mEventloopNestingLevel;

  mEventloopNestingState = prevVal;
  return result;
}

//-------------------------------------------------------------------------
// nsIAppShell methods:

NS_IMETHODIMP
nsBaseAppShell::Run(void)
{
  NS_ENSURE_STATE(!mRunning);  // should not call Run twice
  mRunning = PR_TRUE;

  nsIThread *thread = NS_GetCurrentThread();

#ifdef MOZ_IPC
  MessageLoop::current()->Run();
#else
  while (!mExiting)
    NS_ProcessNextEvent(thread);
#endif

  NS_ProcessPendingEvents(thread);

  mRunning = PR_FALSE;
  return NS_OK;
}

NS_IMETHODIMP
nsBaseAppShell::Exit(void)
{
#ifdef MOZ_IPC
  if (mRunning && !mExiting) {
    MessageLoop::current()->Quit();
  }
#endif
  mExiting = PR_TRUE;
  return NS_OK;
}

NS_IMETHODIMP
nsBaseAppShell::FavorPerformanceHint(PRBool favorPerfOverStarvation,
                                     PRUint32 starvationDelay)
{
  mStarvationDelay = PR_MillisecondsToInterval(starvationDelay);
  if (favorPerfOverStarvation) {
    ++mFavorPerf;
  } else {
    --mFavorPerf;
    mSwitchTime = PR_IntervalNow();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsBaseAppShell::SuspendNative()
{
  ++mSuspendNativeCount;
  return NS_OK;
}

NS_IMETHODIMP
nsBaseAppShell::ResumeNative()
{
  --mSuspendNativeCount;
  NS_ASSERTION(mSuspendNativeCount >= 0, "Unbalanced call to nsBaseAppShell::ResumeNative!");
  return NS_OK;
}

NS_IMETHODIMP
nsBaseAppShell::GetEventloopNestingLevel(PRUint32* aNestingLevelResult)
{
  NS_ENSURE_ARG_POINTER(aNestingLevelResult);

  *aNestingLevelResult = mEventloopNestingLevel;

  return NS_OK;
}

//-------------------------------------------------------------------------
// nsIThreadObserver methods:

// Called from any thread
NS_IMETHODIMP
nsBaseAppShell::OnDispatchedEvent(nsIThreadInternal *thr)
{
  if (mBlockNativeEvent)
    return NS_OK;

  PRInt32 lastVal = PR_AtomicSet(&mNativeEventPending, 1);
  if (lastVal == 1)
    return NS_OK;

  ScheduleNativeEventCallback();
  return NS_OK;
}

// Called from the main thread
NS_IMETHODIMP
nsBaseAppShell::OnProcessNextEvent(nsIThreadInternal *thr, PRBool mayWait,
                                   PRUint32 recursionDepth)
{
  if (mBlockNativeEvent) {
    if (!mayWait)
      return NS_OK;
    // Hmm, we're in a nested native event loop and would like to get
    // back to it ASAP, but it seems a gecko event has caused us to
    // spin up a nested XPCOM event loop (eg. modal window), so we
    // really must start processing native events here again.
    mBlockNativeEvent = PR_FALSE;
    if (NS_HasPendingEvents(thr))
      OnDispatchedEvent(thr); // in case we blocked it earlier
  }

  PRIntervalTime start = PR_IntervalNow();
  PRIntervalTime limit = THREAD_EVENT_STARVATION_LIMIT;

  // Unblock outer nested wait loop (below).
  if (mBlockedWait)
    *mBlockedWait = PR_FALSE;

  PRBool *oldBlockedWait = mBlockedWait;
  mBlockedWait = &mayWait;

  // When mayWait is true, we need to make sure that there is an event in the
  // thread's event queue before we return.  Otherwise, the thread will block
  // on its event queue waiting for an event.
  PRBool needEvent = mayWait;

  if (mFavorPerf <= 0 && start > mSwitchTime + mStarvationDelay) {
    // Favor pending native events
    PRIntervalTime now = start;
    PRBool keepGoing;
    do {
      mLastNativeEventTime = now;
      keepGoing = DoProcessNextNativeEvent(PR_FALSE);
    } while (keepGoing && ((now = PR_IntervalNow()) - start) < limit);
  } else {
    // Avoid starving native events completely when in performance mode
    if (start - mLastNativeEventTime > limit) {
      mLastNativeEventTime = start;
      DoProcessNextNativeEvent(PR_FALSE);
    }
  }

  while (!NS_HasPendingEvents(thr)) {
    // If we have been asked to exit from Run, then we should not wait for
    // events to process.  Note that an inner nested event loop causes
    // 'mayWait' to become false too, through 'mBlockedWait'.
    if (mExiting)
      mayWait = PR_FALSE;

    mLastNativeEventTime = PR_IntervalNow();
    if (!DoProcessNextNativeEvent(mayWait) || !mayWait)
      break;
  }

  mBlockedWait = oldBlockedWait;

  // Make sure that the thread event queue does not block on its monitor, as
  // it normally would do if it did not have any pending events.  To avoid
  // that, we simply insert a dummy event into its queue during shutdown.
  if (needEvent && !NS_HasPendingEvents(thr)) {  
    if (!mDummyEvent)
      mDummyEvent = new nsRunnable();
    thr->Dispatch(mDummyEvent, NS_DISPATCH_NORMAL);
  }

  return NS_OK;
}

// Called from the main thread
NS_IMETHODIMP
nsBaseAppShell::AfterProcessNextEvent(nsIThreadInternal *thr,
                                      PRUint32 recursionDepth)
{
  return NS_OK;
}

NS_IMETHODIMP
nsBaseAppShell::Observe(nsISupports *subject, const char *topic,
                        const PRUnichar *data)
{
  NS_ASSERTION(!strcmp(topic, NS_XPCOM_SHUTDOWN_OBSERVER_ID), "oops");
  Exit();
  return NS_OK;
}
