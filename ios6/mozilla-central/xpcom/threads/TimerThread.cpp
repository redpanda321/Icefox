/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTimerImpl.h"
#include "TimerThread.h"

#include "nsThreadUtils.h"
#include "pratom.h"

#include "nsIObserverService.h"
#include "nsIServiceManager.h"
#include "mozilla/Services.h"

#include <math.h>

using namespace mozilla;

NS_IMPL_THREADSAFE_ISUPPORTS2(TimerThread, nsIRunnable, nsIObserver)

TimerThread::TimerThread() :
  mInitInProgress(0),
  mInitialized(false),
  mMonitor("TimerThread.mMonitor"),
  mShutdown(false),
  mWaiting(false),
  mSleeping(false),
  mDelayLineCounter(0),
  mMinTimerPeriod(0)
{
}

TimerThread::~TimerThread()
{
  mThread = nullptr;

  NS_ASSERTION(mTimers.IsEmpty(), "Timers remain in TimerThread::~TimerThread");
}

nsresult
TimerThread::InitLocks()
{
  return NS_OK;
}

namespace {

class TimerObserverRunnable : public nsRunnable
{
public:
  TimerObserverRunnable(nsIObserver* observer)
    : mObserver(observer)
  { }

  NS_DECL_NSIRUNNABLE

private:
  nsCOMPtr<nsIObserver> mObserver;
};

NS_IMETHODIMP
TimerObserverRunnable::Run()
{
  nsCOMPtr<nsIObserverService> observerService =
    mozilla::services::GetObserverService();
  if (observerService) {
    observerService->AddObserver(mObserver, "sleep_notification", false);
    observerService->AddObserver(mObserver, "wake_notification", false);
    observerService->AddObserver(mObserver, "suspend_process_notification", false);
    observerService->AddObserver(mObserver, "resume_process_notification", false);
  }
  return NS_OK;
}

} // anonymous namespace

nsresult TimerThread::Init()
{
  PR_LOG(GetTimerLog(), PR_LOG_DEBUG, ("TimerThread::Init [%d]\n", mInitialized));

  if (mInitialized) {
    if (!mThread)
      return NS_ERROR_FAILURE;

    return NS_OK;
  }

  if (PR_ATOMIC_SET(&mInitInProgress, 1) == 0) {
    // We hold on to mThread to keep the thread alive.
    nsresult rv = NS_NewThread(getter_AddRefs(mThread), this);
    if (NS_FAILED(rv)) {
      mThread = nullptr;
    }
    else {
      nsRefPtr<TimerObserverRunnable> r = new TimerObserverRunnable(this);
      if (NS_IsMainThread()) {
        r->Run();
      }
      else {
        NS_DispatchToMainThread(r);
      }
    }

    {
      MonitorAutoLock lock(mMonitor);
      mInitialized = true;
      mMonitor.NotifyAll();
    }
  }
  else {
    MonitorAutoLock lock(mMonitor);
    while (!mInitialized) {
      mMonitor.Wait();
    }
  }

  if (!mThread)
    return NS_ERROR_FAILURE;

  return NS_OK;
}

nsresult TimerThread::Shutdown()
{
  PR_LOG(GetTimerLog(), PR_LOG_DEBUG, ("TimerThread::Shutdown begin\n"));

  if (!mThread)
    return NS_ERROR_NOT_INITIALIZED;

  nsTArray<nsTimerImpl*> timers;
  {   // lock scope
    MonitorAutoLock lock(mMonitor);

    mShutdown = true;

    // notify the cond var so that Run() can return
    if (mWaiting)
      mMonitor.Notify();

    // Need to copy content of mTimers array to a local array
    // because call to timers' ReleaseCallback() (and release its self)
    // must not be done under the lock. Destructor of a callback
    // might potentially call some code reentering the same lock
    // that leads to unexpected behavior or deadlock.
    // See bug 422472.
    timers.AppendElements(mTimers);
    mTimers.Clear();
  }

  uint32_t timersCount = timers.Length();
  for (uint32_t i = 0; i < timersCount; i++) {
    nsTimerImpl *timer = timers[i];
    timer->ReleaseCallback();
    ReleaseTimerInternal(timer);
  }

  mThread->Shutdown();    // wait for the thread to die

  PR_LOG(GetTimerLog(), PR_LOG_DEBUG, ("TimerThread::Shutdown end\n"));
  return NS_OK;
}

// Keep track of how early (positive slack) or late (negative slack) timers
// are running, and use the filtered slack number to adaptively estimate how
// early timers should fire to be "on time".
void TimerThread::UpdateFilter(uint32_t aDelay, TimeStamp aTimeout,
                               TimeStamp aNow)
{
  TimeDuration slack = aTimeout - aNow;
  double smoothSlack = 0;
  uint32_t i, filterLength;
  static TimeDuration kFilterFeedbackMaxTicks =
    TimeDuration::FromMilliseconds(FILTER_FEEDBACK_MAX);
  static TimeDuration kFilterFeedbackMinTicks =
    TimeDuration::FromMilliseconds(-FILTER_FEEDBACK_MAX);

  if (slack > kFilterFeedbackMaxTicks)
    slack = kFilterFeedbackMaxTicks;
  else if (slack < kFilterFeedbackMinTicks)
    slack = kFilterFeedbackMinTicks;

  mDelayLine[mDelayLineCounter & DELAY_LINE_LENGTH_MASK] =
    slack.ToMilliseconds();
  if (++mDelayLineCounter < DELAY_LINE_LENGTH) {
    // Startup mode: accumulate a full delay line before filtering.
    PR_ASSERT(mTimeoutAdjustment.ToSeconds() == 0);
    filterLength = 0;
  } else {
    // Past startup: compute number of filter taps based on mMinTimerPeriod.
    if (mMinTimerPeriod == 0) {
      mMinTimerPeriod = (aDelay != 0) ? aDelay : 1;
    } else if (aDelay != 0 && aDelay < mMinTimerPeriod) {
      mMinTimerPeriod = aDelay;
    }

    filterLength = (uint32_t) (FILTER_DURATION / mMinTimerPeriod);
    if (filterLength > DELAY_LINE_LENGTH)
      filterLength = DELAY_LINE_LENGTH;
    else if (filterLength < 4)
      filterLength = 4;

    for (i = 1; i <= filterLength; i++)
      smoothSlack += mDelayLine[(mDelayLineCounter-i) & DELAY_LINE_LENGTH_MASK];
    smoothSlack /= filterLength;

    // XXXbe do we need amplification?  hacking a fudge factor, need testing...
    mTimeoutAdjustment = TimeDuration::FromMilliseconds(smoothSlack * 1.5);
  }

#ifdef DEBUG_TIMERS
  PR_LOG(GetTimerLog(), PR_LOG_DEBUG,
         ("UpdateFilter: smoothSlack = %g, filterLength = %u\n",
          smoothSlack, filterLength));
#endif
}

/* void Run(); */
NS_IMETHODIMP TimerThread::Run()
{
  PR_SetCurrentThreadName("Timer");

  MonitorAutoLock lock(mMonitor);

  // We need to know how many microseconds give a positive PRIntervalTime. This
  // is platform-dependent, we calculate it at runtime now.
  // First we find a value such that PR_MicrosecondsToInterval(high) = 1
  int32_t low = 0, high = 1;
  while (PR_MicrosecondsToInterval(high) == 0)
    high <<= 1;
  // We now have
  //    PR_MicrosecondsToInterval(low)  = 0
  //    PR_MicrosecondsToInterval(high) = 1
  // and we can proceed to find the critical value using binary search
  while (high-low > 1) {
    int32_t mid = (high+low) >> 1;
    if (PR_MicrosecondsToInterval(mid) == 0)
      low = mid;
    else
      high = mid;
  }

  // Half of the amount of microseconds needed to get positive PRIntervalTime.
  // We use this to decide how to round our wait times later
  int32_t halfMicrosecondsIntervalResolution = high >> 1;

  while (!mShutdown) {
    // Have to use PRIntervalTime here, since PR_WaitCondVar takes it
    PRIntervalTime waitFor;

    if (mSleeping) {
      // Sleep for 0.1 seconds while not firing timers.
      waitFor = PR_MillisecondsToInterval(100);
    } else {
      waitFor = PR_INTERVAL_NO_TIMEOUT;
      TimeStamp now = TimeStamp::Now();
      nsTimerImpl *timer = nullptr;

      if (!mTimers.IsEmpty()) {
        timer = mTimers[0];

        if (now >= timer->mTimeout + mTimeoutAdjustment) {
    next:
          // NB: AddRef before the Release under RemoveTimerInternal to avoid
          // mRefCnt passing through zero, in case all other refs than the one
          // from mTimers have gone away (the last non-mTimers[i]-ref's Release
          // must be racing with us, blocked in gThread->RemoveTimer waiting
          // for TimerThread::mMonitor, under nsTimerImpl::Release.

          NS_ADDREF(timer);
          RemoveTimerInternal(timer);

          {
            // We release mMonitor around the Fire call to avoid deadlock.
            MonitorAutoUnlock unlock(mMonitor);

#ifdef DEBUG_TIMERS
            if (PR_LOG_TEST(GetTimerLog(), PR_LOG_DEBUG)) {
              PR_LOG(GetTimerLog(), PR_LOG_DEBUG,
                     ("Timer thread woke up %fms from when it was supposed to\n",
                      fabs((now - timer->mTimeout).ToMilliseconds())));
            }
#endif

            // We are going to let the call to PostTimerEvent here handle the
            // release of the timer so that we don't end up releasing the timer
            // on the TimerThread instead of on the thread it targets.
            if (NS_FAILED(timer->PostTimerEvent())) {
              nsrefcnt rc;
              NS_RELEASE2(timer, rc);
            
              // The nsITimer interface requires that its users keep a reference
              // to the timers they use while those timers are initialized but
              // have not yet fired.  If this ever happens, it is a bug in the
              // code that created and used the timer.
              //
              // Further, note that this should never happen even with a
              // misbehaving user, because nsTimerImpl::Release checks for a
              // refcount of 1 with an armed timer (a timer whose only reference
              // is from the timer thread) and when it hits this will remove the
              // timer from the timer thread and thus destroy the last reference,
              // preventing this situation from occurring.
              NS_ASSERTION(rc != 0, "destroyed timer off its target thread!");
            }
            timer = nullptr;
          }

          if (mShutdown)
            break;

          // Update now, as PostTimerEvent plus the locking may have taken a
          // tick or two, and we may goto next below.
          now = TimeStamp::Now();
        }
      }

      if (!mTimers.IsEmpty()) {
        timer = mTimers[0];

        TimeStamp timeout = timer->mTimeout + mTimeoutAdjustment;

        // Don't wait at all (even for PR_INTERVAL_NO_WAIT) if the next timer
        // is due now or overdue.
        //
        // Note that we can only sleep for integer values of a certain
        // resolution. We use halfMicrosecondsIntervalResolution, calculated
        // before, to do the optimal rounding (i.e., of how to decide what
        // interval is so small we should not wait at all).
        double microseconds = (timeout - now).ToMilliseconds()*1000;
        if (microseconds < halfMicrosecondsIntervalResolution)
          goto next; // round down; execute event now
        waitFor = PR_MicrosecondsToInterval(microseconds);
        if (waitFor == 0)
          waitFor = 1; // round up, wait the minimum time we can wait
      }

#ifdef DEBUG_TIMERS
      if (PR_LOG_TEST(GetTimerLog(), PR_LOG_DEBUG)) {
        if (waitFor == PR_INTERVAL_NO_TIMEOUT)
          PR_LOG(GetTimerLog(), PR_LOG_DEBUG,
                 ("waiting for PR_INTERVAL_NO_TIMEOUT\n"));
        else
          PR_LOG(GetTimerLog(), PR_LOG_DEBUG,
                 ("waiting for %u\n", PR_IntervalToMilliseconds(waitFor)));
      }
#endif
    }

    mWaiting = true;
    mMonitor.Wait(waitFor);
    mWaiting = false;
  }

  return NS_OK;
}

nsresult TimerThread::AddTimer(nsTimerImpl *aTimer)
{
  MonitorAutoLock lock(mMonitor);

  // Add the timer to our list.
  int32_t i = AddTimerInternal(aTimer);
  if (i < 0)
    return NS_ERROR_OUT_OF_MEMORY;

  // Awaken the timer thread.
  if (mWaiting && i == 0)
    mMonitor.Notify();

  return NS_OK;
}

nsresult TimerThread::TimerDelayChanged(nsTimerImpl *aTimer)
{
  MonitorAutoLock lock(mMonitor);

  // Our caller has a strong ref to aTimer, so it can't go away here under
  // ReleaseTimerInternal.
  RemoveTimerInternal(aTimer);

  int32_t i = AddTimerInternal(aTimer);
  if (i < 0)
    return NS_ERROR_OUT_OF_MEMORY;

  // Awaken the timer thread.
  if (mWaiting && i == 0)
    mMonitor.Notify();

  return NS_OK;
}

nsresult TimerThread::RemoveTimer(nsTimerImpl *aTimer)
{
  MonitorAutoLock lock(mMonitor);

  // Remove the timer from our array.  Tell callers that aTimer was not found
  // by returning NS_ERROR_NOT_AVAILABLE.  Unlike the TimerDelayChanged case
  // immediately above, our caller may be passing a (now-)weak ref in via the
  // aTimer param, specifically when nsTimerImpl::Release loses a race with
  // TimerThread::Run, must wait for the mMonitor auto-lock here, and during the
  // wait Run drops the only remaining ref to aTimer via RemoveTimerInternal.

  if (!RemoveTimerInternal(aTimer))
    return NS_ERROR_NOT_AVAILABLE;

  // Awaken the timer thread.
  if (mWaiting)
    mMonitor.Notify();

  return NS_OK;
}

// This function must be called from within a lock
int32_t TimerThread::AddTimerInternal(nsTimerImpl *aTimer)
{
  if (mShutdown)
    return -1;

  TimeStamp now = TimeStamp::Now();
  uint32_t count = mTimers.Length();
  uint32_t i = 0;
  for (; i < count; i++) {
    nsTimerImpl *timer = mTimers[i];

    // Don't break till we have skipped any overdue timers.

    // XXXbz why?  Given our definition of overdue in terms of
    // mTimeoutAdjustment, aTimer might be overdue already!  Why not
    // just fire timers in order?

    // XXX does this hold for TYPE_REPEATING_PRECISE?  /be

    if (now < timer->mTimeout + mTimeoutAdjustment &&
        aTimer->mTimeout < timer->mTimeout) {
      break;
    }
  }

  if (!mTimers.InsertElementAt(i, aTimer))
    return -1;

  aTimer->mArmed = true;
  NS_ADDREF(aTimer);
  return i;
}

bool TimerThread::RemoveTimerInternal(nsTimerImpl *aTimer)
{
  if (!mTimers.RemoveElement(aTimer))
    return false;

  ReleaseTimerInternal(aTimer);
  return true;
}

void TimerThread::ReleaseTimerInternal(nsTimerImpl *aTimer)
{
  // Order is crucial here -- see nsTimerImpl::Release.
  aTimer->mArmed = false;
  NS_RELEASE(aTimer);
}

void TimerThread::DoBeforeSleep()
{
  mSleeping = true;
}

void TimerThread::DoAfterSleep()
{
  mSleeping = true; // wake may be notified without preceding sleep notification
  for (uint32_t i = 0; i < mTimers.Length(); i ++) {
    nsTimerImpl *timer = mTimers[i];
    // get and set the delay to cause its timeout to be recomputed
    uint32_t delay;
    timer->GetDelay(&delay);
    timer->SetDelay(delay);
  }

  // nuke the stored adjustments, so they get recalibrated
  mTimeoutAdjustment = TimeDuration(0);
  mDelayLineCounter = 0;
  mSleeping = false;
}


/* void observe (in nsISupports aSubject, in string aTopic, in wstring aData); */
NS_IMETHODIMP
TimerThread::Observe(nsISupports* /* aSubject */, const char *aTopic, const PRUnichar* /* aData */)
{
  if (strcmp(aTopic, "sleep_notification") == 0 ||
      strcmp(aTopic, "suspend_process_notification") == 0)
    DoBeforeSleep();
  else if (strcmp(aTopic, "wake_notification") == 0 ||
           strcmp(aTopic, "resume_process_notification") == 0)
    DoAfterSleep();

  return NS_OK;
}
