/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIdleService.h"
#include "nsString.h"
#include "nsIObserverService.h"
#include "nsIServiceManager.h"
#include "nsDebug.h"
#include "nsCOMArray.h"
#include "prinrval.h"
#include "prlog.h"
#include "mozilla/Services.h"
#include "mozilla/Preferences.h"
#include "mozilla/Telemetry.h"

#ifdef ANDROID
#include <android/log.h>
#endif

using namespace mozilla;

// interval in milliseconds between internal idle time requests.
#define MIN_IDLE_POLL_INTERVAL_MSEC (5 * PR_MSEC_PER_SEC) /* 5 sec */

// Time used by the daily idle serivce to determine a significant idle time.
#define DAILY_SIGNIFICANT_IDLE_SERVICE_SEC 300 /* 5 min */
// Pref for last time (seconds since epoch) daily notification was sent.
#define PREF_LAST_DAILY "idle.lastDailyNotification"
// Number of seconds in a day.
#define SECONDS_PER_DAY 86400

#ifdef PR_LOGGING
static PRLogModuleInfo *sLog = NULL;
#endif

// Use this to find previously added observers in our array:
class IdleListenerComparator
{
public:
  bool Equals(IdleListener a, IdleListener b) const
  {
    return (a.observer == b.observer) &&
           (a.reqIdleTime == b.reqIdleTime);
  }
};

////////////////////////////////////////////////////////////////////////////////
//// nsIdleServiceDaily

NS_IMPL_ISUPPORTS2(nsIdleServiceDaily, nsIObserver, nsISupportsWeakReference)

NS_IMETHODIMP
nsIdleServiceDaily::Observe(nsISupports *,
                            const char *aTopic,
                            const PRUnichar *)
{
  if (strcmp(aTopic, "profile-after-change") == 0) {
    // We are back. Start sending notifications again.
    mShutdownInProgress = false;
    return NS_OK;
  }

  if (strcmp(aTopic, "xpcom-will-shutdown") == 0 ||
      strcmp(aTopic, "profile-change-teardown") == 0) {
    mShutdownInProgress = true;
  }

  if (mShutdownInProgress || strcmp(aTopic, OBSERVER_TOPIC_ACTIVE) == 0) {
    return NS_OK;
  }
  MOZ_ASSERT(strcmp(aTopic, OBSERVER_TOPIC_IDLE) == 0);

#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "IdleService", "Notifying idle-daily observers");
#endif

  // Notify anyone who cares.
  nsCOMPtr<nsIObserverService> observerService =
    mozilla::services::GetObserverService();
  NS_ENSURE_STATE(observerService);
  (void)observerService->NotifyObservers(nsnull,
                                         OBSERVER_TOPIC_IDLE_DAILY,
                                         nsnull);

  // Notify the category observers.
  const nsCOMArray<nsIObserver> &entries = mCategoryObservers.GetEntries();
  for (PRInt32 i = 0; i < entries.Count(); ++i) {
    (void)entries[i]->Observe(nsnull, OBSERVER_TOPIC_IDLE_DAILY, nsnull);
  }

  // Stop observing idle for today.
  (void)mIdleService->RemoveIdleObserver(this,
                                         DAILY_SIGNIFICANT_IDLE_SERVICE_SEC);

  // Set the last idle-daily time pref.
  PRInt32 nowSec = static_cast<PRInt32>(PR_Now() / PR_USEC_PER_SEC);
  Preferences::SetInt(PREF_LAST_DAILY, nowSec);

  // Force that to be stored so we don't retrigger twice a day under
  // any circumstances.
  nsIPrefService* prefs = Preferences::GetService();
  if (prefs) {
    prefs->SavePrefFile(nsnull);
  }

#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "IdleService", "Storing last idle time as %d",
                      nowSec);
#endif

  // Note the moment we started our timer.
  mDailyTimerStart  = PR_Now();

  // Start timer for the next check in one day.
  (void)mTimer->InitWithFuncCallback(DailyCallback,
                                     this,
                                     SECONDS_PER_DAY * PR_MSEC_PER_SEC,
                                     nsITimer::TYPE_ONE_SHOT);

  return NS_OK;
}

nsIdleServiceDaily::nsIdleServiceDaily(nsIIdleService* aIdleService)
  : mIdleService(aIdleService)
  , mTimer(do_CreateInstance(NS_TIMER_CONTRACTID))
  , mCategoryObservers(OBSERVER_TOPIC_IDLE_DAILY)
  , mShutdownInProgress(false)
{
}

void
nsIdleServiceDaily::Init()
{
  // Check time of the last idle-daily notification.  If it was more than 24
  // hours ago listen for idle, otherwise set a timer for 24 hours from now.
  PRInt32 nowSec = static_cast<PRInt32>(PR_Now() / PR_USEC_PER_SEC);
  PRInt32 lastDaily = Preferences::GetInt(PREF_LAST_DAILY, 0);
  if (lastDaily < 0 || lastDaily > nowSec) {
    // The time is bogus, use default.
    lastDaily = 0;
  }

  // Check if it has been a day since the last notification.
  if (nowSec - lastDaily > SECONDS_PER_DAY) {
#ifdef ANDROID
    __android_log_print(ANDROID_LOG_INFO, "IdleService", "DailyCallback started");
#endif
    // Wait for the user to become idle, so we can do todays idle tasks.
    DailyCallback(nsnull, this);
  }
  else {
#ifdef ANDROID
    __android_log_print(ANDROID_LOG_INFO, "IdleService", "Setting timer a day from now");
#endif

    // Note the moment we started our timer.
    mDailyTimerStart  = PR_Now();

    // Start timer for the next check in one day.
    (void)mTimer->InitWithFuncCallback(DailyCallback,
                                       this,
                                       SECONDS_PER_DAY * PR_MSEC_PER_SEC,
                                       nsITimer::TYPE_ONE_SHOT);
  }

  // Register for when we should terminate/pause
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->AddObserver(this, "xpcom-will-shutdown", true);
    obs->AddObserver(this, "profile-change-teardown", true);
    obs->AddObserver(this, "profile-after-change", true);
  }
}

nsIdleServiceDaily::~nsIdleServiceDaily()
{
  if (mTimer) {
    mTimer->Cancel();
    mTimer = nsnull;
  }
}

// static
void
nsIdleServiceDaily::DailyCallback(nsITimer* aTimer, void* aClosure)
{
#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "IdleService", "DailyCallback running");
#endif

  nsIdleServiceDaily* me = static_cast<nsIdleServiceDaily*>(aClosure);

  PRTime now = PR_Now();
  PRTime launchTime = me->mDailyTimerStart + ((PRTime)SECONDS_PER_DAY * PR_USEC_PER_SEC);

  // Check if it has been a day since we launched this timer.
  if (now < launchTime) {
      // Timer returned early, reschedule.
      PRTime newTime = launchTime;

      // Add 10 ms to ensure we don't undershoot, and never get a "0" timer.
      newTime += 10 * PR_USEC_PER_MSEC;

#ifdef ANDROID
      __android_log_print(ANDROID_LOG_INFO, "IdleService",
                          "DailyCallback resetting timer to %lld msec",
                          (newTime - now) / PR_USEC_PER_MSEC);
#endif

      // Refire the timer.
      (void)me->mTimer->InitWithFuncCallback(DailyCallback,
                                             me,
                                             (newTime - now) / PR_USEC_PER_MSEC,
                                             nsITimer::TYPE_ONE_SHOT);
      return;
  }

#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "IdleService", "DailyCallback registering Idle observer");
#endif

  // The one thing we do every day is to start waiting for the user to "have
  // a significant idle time".
  (void)me->mIdleService->AddIdleObserver(me,
                                          DAILY_SIGNIFICANT_IDLE_SERVICE_SEC);
}


/**
 * The idle services goal is to notify subscribers when a certain time has
 * passed since the last user interaction with the system.
 *
 * On some platforms this is defined as the last time user events reached this
 * application, on other platforms it is a system wide thing - the preferred
 * implementation is to use the system idle time, rather than the application
 * idle time, as the things depending on the idle service are likely to use
 * significant resources (network, disk, memory, cpu, etc.).
 *
 * When the idle service needs to use the system wide idle timer, it typically
 * needs to poll the idle time value by the means of a timer.  It needs to
 * poll fast when it is in active idle mode (when it has a listener in the idle
 * mode) as it needs to detect if the user is active in other applications.
 *
 * When the service is waiting for the first listener to become idle, or when
 * it is only monitoring application idle time, it only needs to have the timer
 * expire at the time the next listener goes idle.
 *
 * The core state of the service is determined by:
 *
 * - A list of listeners.
 *
 * - A boolean that tells if any listeners are in idle mode.
 *
 * - A delta value that indicates when, measured from the last non-idle time,
 *   the next listener should switch to idle mode.
 *
 * - An absolute time of the last time idle mode was detected (this is used to
 *   judge if we have been out of idle mode since the last invocation of the
 *   service.
 *
 * There are four entry points into the system:
 *
 * - A new listener is registered.
 *
 * - An existing listener is deregistered.
 *
 * - User interaction is detected.
 *
 * - The timer expires.
 *
 * When a new listener is added its idle timeout, is compared with the next idle
 * timeout, and if lower, that time is stored as the new timeout, and the timer
 * is reconfigured to ensure a timeout around the time the new listener should
 * timeout.
 *
 * If the next idle time is above the idle time requested by the new listener
 * it won't be informed until the timer expires, this is to avoid recursive
 * behavior and to simplify the code.  In this case the timer will be set to
 * about 10 ms.
 *
 * When an existing listener is deregistered, it is just removed from the list
 * of active listeners, we don't stop the timer, we just let it expire.
 *
 * When user interaction is detected, either because it was directly detected or
 * because we polled the system timer and found it to be unexpected low, then we
 * check the flag that tells us if any listeners are in idle mode, if there are
 * they are removed from idle mode and told so, and we reset our state
 * caculating the next timeout and restart the timer if needed.
 *
 * ---- Build in logic
 *
 * In order to avoid restarting the timer endlessly, the timer function has
 * logic that will only restart the timer, if the requested timeout is before
 * the current timeout.
 *
 */


////////////////////////////////////////////////////////////////////////////////
//// nsIdleService

namespace { 
nsIdleService* gIdleService;
}

already_AddRefed<nsIdleService>
nsIdleService::GetInstance()
{
  nsRefPtr<nsIdleService> instance(gIdleService);
  return instance.forget();
}

nsIdleService::nsIdleService() : mCurrentlySetToTimeoutAtInPR(0),
                                 mAnyObserverIdle(false),
                                 mDeltaToNextIdleSwitchInS(PR_UINT32_MAX),
                                 mLastUserInteractionInPR(PR_Now())
{
#ifdef PR_LOGGING
  if (sLog == NULL)
    sLog = PR_NewLogModule("idleService");
#endif
  MOZ_ASSERT(!gIdleService);
  gIdleService = this;
  mDailyIdle = new nsIdleServiceDaily(this);
  mDailyIdle->Init();
}

nsIdleService::~nsIdleService()
{
  if(mTimer) {
    mTimer->Cancel();
  }


  MOZ_ASSERT(gIdleService == this);
  gIdleService = nsnull;
}

NS_IMPL_ISUPPORTS2(nsIdleService, nsIIdleService, nsIIdleServiceInternal)

NS_IMETHODIMP
nsIdleService::AddIdleObserver(nsIObserver* aObserver, PRUint32 aIdleTimeInS)
{
  PR_LOG(sLog, PR_LOG_DEBUG,
         ("idleService: Register idle observer %x for %d seconds",
          aObserver, aIdleTimeInS));
#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "IdleService", "Register idle observer %x for %d seconds",
                      aObserver, aIdleTimeInS);
#endif

  NS_ENSURE_ARG_POINTER(aObserver);
  // We don't accept idle time at 0, and we can't handle idle time that are too
  // high either - no more than ~136 years.
  NS_ENSURE_ARG_RANGE(aIdleTimeInS, 1, (PR_UINT32_MAX / 10) - 1);

  // Put the time + observer in a struct we can keep:
  IdleListener listener(aObserver, aIdleTimeInS);

  if (!mArrayListeners.AppendElement(listener)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // Create our timer callback if it's not there already.
  if (!mTimer) {
    nsresult rv;
    mTimer = do_CreateInstance(NS_TIMER_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Check if the newly added observer has a smaller wait time than what we
  // are waiting for now.
  if (mDeltaToNextIdleSwitchInS > aIdleTimeInS) {
    // If it is, then this is the next to move to idle (at this point we
    // don't care if it should have switched already).
    PR_LOG(sLog, PR_LOG_DEBUG,
          ("idleService: Register: adjusting next switch from %d to %d seconds",
           mDeltaToNextIdleSwitchInS, aIdleTimeInS));
#ifdef ANDROID
    __android_log_print(ANDROID_LOG_INFO, "IdleService",
                        "Register: adjusting next switch from %d to %d seconds",
                        mDeltaToNextIdleSwitchInS, aIdleTimeInS);
#endif

    mDeltaToNextIdleSwitchInS = aIdleTimeInS;
  }

  // Ensure timer is running.
  ReconfigureTimer();

  return NS_OK;
}

NS_IMETHODIMP
nsIdleService::RemoveIdleObserver(nsIObserver* aObserver, PRUint32 aTimeInS)
{

  NS_ENSURE_ARG_POINTER(aObserver);
  NS_ENSURE_ARG(aTimeInS);
  IdleListener listener(aObserver, aTimeInS);

  // Find the entry and remove it, if it was the last entry, we just let the
  // existing timer run to completion (there might be a new registration in a
  // little while.
  IdleListenerComparator c;
  if (mArrayListeners.RemoveElement(listener, c)) {
    PR_LOG(sLog, PR_LOG_DEBUG,
           ("idleService: Remove idle observer %x (%d seconds)",
            aObserver, aTimeInS));
#ifdef ANDROID
    __android_log_print(ANDROID_LOG_INFO, "IdleService",
                        "Remove idle observer %x (%d seconds)",
                        aObserver, aTimeInS);
#endif
    return NS_OK;
  }

  // If we get here, we haven't removed anything:
  PR_LOG(sLog, PR_LOG_WARNING, 
         ("idleService: Failed to remove idle observer %x (%d seconds)",
          aObserver, aTimeInS));
#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "IdleService",
                      "Failed to remove idle observer %x (%d seconds)",
                      aObserver, aTimeInS);
#endif
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsIdleService::ResetIdleTimeOut(PRUint32 idleDeltaInMS)
{
  PR_LOG(sLog, PR_LOG_DEBUG,
         ("idleService: Reset idle timeout (last interaction %u msec)",
          idleDeltaInMS));

  // Store the time
  mLastUserInteractionInPR = PR_Now() - (idleDeltaInMS * PR_USEC_PER_MSEC);

  // If no one is idle, then we are done, any existing timers can keep running.
  if (!mAnyObserverIdle) {
    PR_LOG(sLog, PR_LOG_DEBUG,
           ("idleService: Reset idle timeout: no idle observers"));
    return NS_OK;
  }

  // Mark all idle services as non-idle, and calculate the next idle timeout.
  Telemetry::AutoTimer<Telemetry::IDLE_NOTIFY_BACK_MS> timer;
  nsCOMArray<nsIObserver> notifyList;
  mDeltaToNextIdleSwitchInS = PR_UINT32_MAX;

  // Loop through all listeners, and find any that have detected idle.
  for (PRUint32 i = 0; i < mArrayListeners.Length(); i++) {
    IdleListener& curListener = mArrayListeners.ElementAt(i);

    // If the listener was idle, then he shouldn't be any longer.
    if (curListener.isIdle) {
      notifyList.AppendObject(curListener.observer);
      curListener.isIdle = false;
    }

    // Check if the listener is the next one to timeout.
    mDeltaToNextIdleSwitchInS = PR_MIN(mDeltaToNextIdleSwitchInS,
                                       curListener.reqIdleTime);
  }

  // When we are done, then we wont have anyone idle.
  mAnyObserverIdle = false;

  // Restart the idle timer, and do so before anyone can delay us.
  ReconfigureTimer();

  PRInt32 numberOfPendingNotifications = notifyList.Count();
  Telemetry::Accumulate(Telemetry::IDLE_NOTIFY_BACK_LISTENERS,
                        numberOfPendingNotifications);

  // Bail if nothing to do.
  if (!numberOfPendingNotifications) {
    return NS_OK;
  }

  // Now send "back" events to all, if any should have timed out allready, then
  // they will be reawaken by the timer that is already running.

  // We need a text string to send with any state change events.
  nsAutoString timeStr;

  timeStr.AppendInt((PRInt32)(idleDeltaInMS / PR_MSEC_PER_SEC));

  // Send the "non-idle" events.
  while (numberOfPendingNotifications--) {
    PR_LOG(sLog, PR_LOG_DEBUG,
           ("idleService: Reset idle timeout: tell observer %x user is back",
            notifyList[numberOfPendingNotifications]));
#ifdef ANDROID
    __android_log_print(ANDROID_LOG_INFO, "IdleService",
                        "Reset idle timeout: tell observer %x user is back",
                        notifyList[numberOfPendingNotifications]);
#endif
    notifyList[numberOfPendingNotifications]->Observe(this,
                                                      OBSERVER_TOPIC_ACTIVE,
                                                      timeStr.get());
  }
  return NS_OK;
}

NS_IMETHODIMP
nsIdleService::GetIdleTime(PRUint32* idleTime)
{
  // Check sanity of in parameter.
  if (!idleTime) {
    return NS_ERROR_NULL_POINTER;
  }

  // Polled idle time in ms.
  PRUint32 polledIdleTimeMS;

  bool polledIdleTimeIsValid = PollIdleTime(&polledIdleTimeMS);

  PR_LOG(sLog, PR_LOG_DEBUG,
         ("idleService: Get idle time: polled %u msec, valid = %d",
          polledIdleTimeMS, polledIdleTimeIsValid));
  
  // timeSinceReset is in milliseconds.
  PRUint32 timeSinceResetInMS = (PR_Now() - mLastUserInteractionInPR) /
                                PR_USEC_PER_MSEC;

  PR_LOG(sLog, PR_LOG_DEBUG,
         ("idleService: Get idle time: time since reset %u msec",
          timeSinceResetInMS));
#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "IdleService",
                      "Get idle time: time since reset %u msec",
                      timeSinceResetInMS);
#endif

  // If we did't get pulled data, return the time since last idle reset.
  if (!polledIdleTimeIsValid) {
    // We need to convert to ms before returning the time.
    *idleTime = timeSinceResetInMS;
    return NS_OK;
  }

  // Otherwise return the shortest time detected (in ms).
  *idleTime = NS_MIN(timeSinceResetInMS, polledIdleTimeMS);

  return NS_OK;
}


bool
nsIdleService::PollIdleTime(PRUint32* /*aIdleTime*/)
{
  // Default behavior is not to have the ability to poll an idle time.
  return false;
}

bool
nsIdleService::UsePollMode()
{
  PRUint32 dummy;
  return PollIdleTime(&dummy);
}

void
nsIdleService::StaticIdleTimerCallback(nsITimer* aTimer, void* aClosure)
{
  static_cast<nsIdleService*>(aClosure)->IdleTimerCallback();
}

void
nsIdleService::IdleTimerCallback(void)
{
  // Remember that we no longer have a timer running.
  mCurrentlySetToTimeoutAtInPR = 0;

  // Get the current idle time.
  PRUint32 currentIdleTimeInMS;

  if (NS_FAILED(GetIdleTime(&currentIdleTimeInMS))) {
    PR_LOG(sLog, PR_LOG_ALWAYS,
           ("idleService: Idle timer callback: failed to get idle time"));
#ifdef ANDROID
    __android_log_print(ANDROID_LOG_INFO, "IdleService",
                        "Idle timer callback: failed to get idle time");
#endif
    return;
  }

  PR_LOG(sLog, PR_LOG_DEBUG,
         ("idleService: Idle timer callback: current idle time %u msec",
          currentIdleTimeInMS));
#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "IdleService",
                      "Idle timer callback: current idle time %u msec",
                      currentIdleTimeInMS);
#endif

  // Check if we have had some user interaction we didn't handle previously
  // we do the calculation in ms to lessen the chance for rounding errors to
  // trigger wrong results, it is also very important that we call PR_Now AFTER
  // the call to GetIdleTime().
  if (((PR_Now() - mLastUserInteractionInPR) / PR_USEC_PER_MSEC) >
      currentIdleTimeInMS)
  {
    // We had user activity, so handle that part first (to ensure the listeners
    // don't risk getting an non-idle after they get a new idle indication.
    ResetIdleTimeOut(currentIdleTimeInMS);

    // NOTE: We can't bail here, as we might have something already timed out.
  }

  // Find the idle time in S.
  PRUint32 currentIdleTimeInS = currentIdleTimeInMS / PR_MSEC_PER_SEC;

  // Restart timer and bail if no-one are expected to be in idle
  if (mDeltaToNextIdleSwitchInS > currentIdleTimeInS) {
    // If we didn't expect anyone to be idle, then just re-start the timer.
    ReconfigureTimer();
    return;
  }

  // Tell expired listeners they are expired,and find the next timeout
  Telemetry::AutoTimer<Telemetry::IDLE_NOTIFY_IDLE_MS> timer;

  // We need to initialise the time to the next idle switch.
  mDeltaToNextIdleSwitchInS = PR_UINT32_MAX;

  // Create list of observers that should be notified.
  nsCOMArray<nsIObserver> notifyList;

  for (PRUint32 i = 0; i < mArrayListeners.Length(); i++) {
    IdleListener& curListener = mArrayListeners.ElementAt(i);

    // We are only interested in items, that are not in the idle state.
    if (!curListener.isIdle) {
      // If they have an idle time smaller than the actual idle time.
      if (curListener.reqIdleTime <= currentIdleTimeInS) {
        // Then add the listener to the list of listeners that should be
        // notified.
        notifyList.AppendObject(curListener.observer);
        // This listener is now idle.
        curListener.isIdle = true;
      } else {
        // Listeners that are not timed out yet are candidates for timing out.
        mDeltaToNextIdleSwitchInS = PR_MIN(mDeltaToNextIdleSwitchInS,
                                           curListener.reqIdleTime);
      }
    }
  }

  // Restart the timer before any notifications that could slow us down are
  // done.
  ReconfigureTimer();

  PRInt32 numberOfPendingNotifications = notifyList.Count();
  Telemetry::Accumulate(Telemetry::IDLE_NOTIFY_IDLE_LISTENERS,
                        numberOfPendingNotifications);

  // Bail if nothing to do.
  if (!numberOfPendingNotifications) {
    return;
  }

  // Remember we have someone idle.
  mAnyObserverIdle = true;

  // We need a text string to send with any state change events.
  nsAutoString timeStr;
  timeStr.AppendInt(currentIdleTimeInS);

  // Notify all listeners that just timed out.
  while (numberOfPendingNotifications--) {
    PR_LOG(sLog, PR_LOG_DEBUG,
           ("idleService: Idle timer callback: tell observer %x user is idle",
            notifyList[numberOfPendingNotifications]));
#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "IdleService",
                      "Idle timer callback: tell observer %x user is idle",
                      notifyList[numberOfPendingNotifications]);
#endif
    notifyList[numberOfPendingNotifications]->Observe(this,
                                                      OBSERVER_TOPIC_IDLE,
                                                      timeStr.get());
  }
}

void
nsIdleService::SetTimerExpiryIfBefore(PRTime aNextTimeoutInPR)
{
  PR_LOG(sLog, PR_LOG_DEBUG,
         ("idleService: SetTimerExpiryIfBefore: next timeout %lld usec",
          aNextTimeoutInPR));
#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "IdleService",
                      "SetTimerExpiryIfBefore: next timeout %lld usec",
                      aNextTimeoutInPR);
#endif

  // Bail if we don't have a timer service.
  if (!mTimer) {
    return;
  }

  // If the new timeout is before the old one or we don't have a timer running,
  // then restart the timer.
  if (mCurrentlySetToTimeoutAtInPR > aNextTimeoutInPR ||
      !mCurrentlySetToTimeoutAtInPR) {

#if defined(PR_LOGGING) || defined(ANDROID)
    PRTime oldTimeout = mCurrentlySetToTimeoutAtInPR;
#endif

    mCurrentlySetToTimeoutAtInPR = aNextTimeoutInPR ;

    // Stop the current timer (it's ok to try'n stop it, even it isn't running).
    mTimer->Cancel();

    // Check that the timeout is actually in the future, otherwise make it so.
    PRTime currentTimeInPR = PR_Now();
    if (currentTimeInPR > mCurrentlySetToTimeoutAtInPR) {
      mCurrentlySetToTimeoutAtInPR = currentTimeInPR;
    }

    // Add 10 ms to ensure we don't undershoot, and never get a "0" timer.
    mCurrentlySetToTimeoutAtInPR += 10 * PR_USEC_PER_MSEC;

    PR_LOG(sLog, PR_LOG_DEBUG,
           ("idleService: reset timer expiry from %lld usec to %lld usec",
            oldTimeout, mCurrentlySetToTimeoutAtInPR));
#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "IdleService",
                      "reset timer expiry from %lld usec to %lld usec",
                      oldTimeout, mCurrentlySetToTimeoutAtInPR);
#endif

    // Start the timer
    mTimer->InitWithFuncCallback(StaticIdleTimerCallback,
                                 this,
                                 (mCurrentlySetToTimeoutAtInPR -
                                  currentTimeInPR) / PR_USEC_PER_MSEC,
                                 nsITimer::TYPE_ONE_SHOT);

  }
}


void
nsIdleService::ReconfigureTimer(void)
{
  // Check if either someone is idle, or someone will become idle.
  if (!mAnyObserverIdle && PR_UINT32_MAX == mDeltaToNextIdleSwitchInS) {
    // If not, just let any existing timers run to completion
    // And bail out.
    PR_LOG(sLog, PR_LOG_DEBUG,
           ("idleService: ReconfigureTimer: no idle or waiting observers"));
#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "IdleService",
                      "ReconfigureTimer: no idle or waiting observers");
#endif
    return;
  }

  // Find the next timeout value, assuming we are not polling.

  // We need to store the current time, so we don't get artifacts from the time
  // ticking while we are processing.
  PRTime curTimeInPR = PR_Now();

  PRTime nextTimeoutAtInPR = mLastUserInteractionInPR +
                             (((PRTime)mDeltaToNextIdleSwitchInS) *
                              PR_USEC_PER_SEC);

  PR_LOG(sLog, PR_LOG_DEBUG,
         ("idleService: next timeout %lld usec (%u msec from now)",
          nextTimeoutAtInPR,
          (PRUint32)((nextTimeoutAtInPR - curTimeInPR) / PR_USEC_PER_MSEC)));
#ifdef ANDROID
  __android_log_print(ANDROID_LOG_INFO, "IdleService",
                      "next timeout %lld usec (%lld msec from now)",
                      nextTimeoutAtInPR,
                      ((nextTimeoutAtInPR - curTimeInPR) / PR_USEC_PER_MSEC));
#endif
  // Check if we should correct the timeout time because we should poll before.
  if (mAnyObserverIdle && UsePollMode()) {
    PRTime pollTimeout = curTimeInPR +
                         MIN_IDLE_POLL_INTERVAL_MSEC * PR_USEC_PER_MSEC;

    if (nextTimeoutAtInPR > pollTimeout) {
      PR_LOG(sLog, PR_LOG_DEBUG,
           ("idleService: idle observers, reducing timeout to %u msec from now",
            MIN_IDLE_POLL_INTERVAL_MSEC));
#ifdef ANDROID
      __android_log_print(ANDROID_LOG_INFO, "IdleService",
                          "idle observers, reducing timeout to %u msec from now",
                          MIN_IDLE_POLL_INTERVAL_MSEC);
#endif
      nextTimeoutAtInPR = pollTimeout;
    }
  }

  SetTimerExpiryIfBefore(nextTimeoutAtInPR);
}

