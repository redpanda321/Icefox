/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsIdleService_h__
#define nsIdleService_h__

#include "nsIIdleServiceInternal.h"
#include "nsCOMPtr.h"
#include "nsITimer.h"
#include "nsTArray.h"
#include "nsIObserver.h"
#include "nsIIdleService.h"
#include "nsCategoryCache.h"
#include "nsWeakReference.h"

/**
 * Class we can use to store an observer with its associated idle time
 * requirement and whether or not the observer thinks it's "idle".
 */
class IdleListener {
public:
  nsCOMPtr<nsIObserver> observer;
  PRUint32 reqIdleTime;
  bool isIdle;

  IdleListener(nsIObserver* obs, PRUint32 reqIT, bool aIsIdle = false) :
    observer(obs), reqIdleTime(reqIT), isIdle(aIsIdle) {}
  ~IdleListener() {}
};

// This one will be declared later.
class nsIdleService;

/**
 * Class to handle the daily idle timer.
 */
class nsIdleServiceDaily : public nsIObserver,
                           public nsSupportsWeakReference
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  nsIdleServiceDaily(nsIIdleService* aIdleService);

  /**
   * Initializes the daily idle observer.
   * Keep this separated from the constructor, since it could cause pointer
   * corruption due to AddRef/Release of "this".
   */
  void Init();

  virtual ~nsIdleServiceDaily();

private:
  /**
   * @note This is a normal pointer, part to avoid creating a cycle with the
   * idle service, part to avoid potential pointer corruption due to this class
   * being instantiated in the constructor of the service itself.
   */
  nsIIdleService* mIdleService;

  /**
   * Set to true when the instantiated object has a idle observer.
   */
  bool mObservesIdle;

  /**
   * Place to hold the timer used by this class to determine when a day has
   * passed, after that it will wait for idle time to be detected.
   */
  nsCOMPtr<nsITimer> mTimer;

  /**
   * Function that is called back once a day.
   */
  static void DailyCallback(nsITimer* aTimer, void* aClosure);

  /**
   * Cache of observers for the "idle-daily" category.
   */
  nsCategoryCache<nsIObserver> mCategoryObservers;

  /**
   * Boolean set to true when daily idle notifications should be disabled.
   */
  bool mShutdownInProgress;

  /**
   * Real time we fired off the one-day timer, in case timers aren't
   * very reliable.
   */
  PRTime mDailyTimerStart;
};

class nsIdleService : public nsIIdleServiceInternal
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIIDLESERVICE
  NS_DECL_NSIIDLESERVICEINTERNAL

protected:
  static already_AddRefed<nsIdleService> GetInstance();

  nsIdleService();
  virtual ~nsIdleService();

  /**
   * If there is a platform specific function to poll the system idel time
   * then that must be returned in this function, and the function MUST return
   * true, otherwise then the function should be left unimplemented or made
   * to return false (this can also be used for systems where it depends on
   * the configuration of the system if the idle time can be determined)
   *
   * @param aIdleTime
   *        The idle time in ms.
   *
   * @return true if the idle time could be polled, false otherwise.
   *
   * @note The time returned by this function can be different than the one
   *       returned by GetIdleTime, as that is corrected by any calls to
   *       ResetIdleTimeOut(), unless you overwrite that function too...
   */
  virtual bool PollIdleTime(PRUint32* aIdleTime);

  /**
   * Function that determines if we are in poll mode or not.
   *
   * @return true if polling is supported, false otherwise.
   */
  virtual bool UsePollMode();

private:
  /**
   * Ensure that the timer is expiring at least at the given time
   *
   * The function might not restart the timer if there is one running currently
   *
   * @param aNextTimeoutInPR
   *        The last absolute time the timer should expire
   */
  void SetTimerExpiryIfBefore(PRTime aNextTimeoutInPR);

  /**
   * Stores the next timeout time, 0 means timer not running
   */
  PRTime mCurrentlySetToTimeoutAtInPR;

  /**
   * mTimer holds the internal timer used by this class to detect when to poll
   * for idle time, when to check if idle timers should expire etc.
   */
  nsCOMPtr<nsITimer> mTimer;

  /**
   * Array of listeners that wants to be notified about idle time.
   */
  nsTArray<IdleListener> mArrayListeners;

  /**
   * Object keeping track of the daily idle thingy.
   */
  nsRefPtr<nsIdleServiceDaily> mDailyIdle;

  /**
   * Boolean indicating if any observers are in idle mode
   */
  bool mAnyObserverIdle;

  /**
   * Delta time from last non idle time to when the next observer should switch
   * to idle mode
   *
   * Time in seconds
   *
   * If this value is 0 it means there are no active observers
   */
  PRUint32 mDeltaToNextIdleSwitchInS;

  /**
   * Absolute value for when the last user interaction took place.
   */
  PRTime mLastUserInteractionInPR;


  /**
   * Function that ensures the timer is running with at least the minimum time
   * needed.  It will kill the timer if there are no active observers.
   */
  void ReconfigureTimer(void);

  /**
   * Callback function that is called when the internal timer expires.
   *
   * This in turn calls the IdleTimerCallback that does the real processing
   */
  static void StaticIdleTimerCallback(nsITimer* aTimer, void* aClosure);

  /**
   * Function that handles when a timer has expired
   */
  void IdleTimerCallback(void);
};

#endif // nsIdleService_h__
