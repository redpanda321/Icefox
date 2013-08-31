/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Hal.h"
#include "mozilla/HalWakeLock.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "nsIDOMWakeLockListener.h"
#include "nsIDOMWindow.h"
#include "nsIObserverService.h"
#include "PowerManagerService.h"
#include "WakeLock.h"

// For _exit().
#ifdef XP_WIN
#include <process.h>
#else
#include <unistd.h>
#endif

namespace mozilla {
namespace dom {
namespace power {

using namespace hal;

NS_IMPL_ISUPPORTS1(PowerManagerService, nsIPowerManagerService)

/* static */ StaticRefPtr<PowerManagerService> PowerManagerService::sSingleton;

/* static */ already_AddRefed<PowerManagerService>
PowerManagerService::GetInstance()
{
  if (!sSingleton) {
    sSingleton = new PowerManagerService();
    sSingleton->Init();
    ClearOnShutdown(&sSingleton);
  }

  nsRefPtr<PowerManagerService> service = sSingleton.get();
  return service.forget();
}

void
PowerManagerService::Init()
{
  RegisterWakeLockObserver(this);

  // NB: default to *enabling* the watchdog even when the pref is
  // absent, in case the profile might be damaged and we need to
  // restart to repair it.
  mWatchdogTimeoutSecs =
    Preferences::GetInt("shutdown.watchdog.timeoutSecs", 5);
}

PowerManagerService::~PowerManagerService()
{
  UnregisterWakeLockObserver(this);
}

void
PowerManagerService::ComputeWakeLockState(const WakeLockInformation& aWakeLockInfo,
                                          nsAString &aState)
{
  WakeLockState state = hal::ComputeWakeLockState(aWakeLockInfo.numLocks(),
                                                  aWakeLockInfo.numHidden());
  switch (state) {
  case WAKE_LOCK_STATE_UNLOCKED:
    aState.AssignLiteral("unlocked");
    break;
  case WAKE_LOCK_STATE_HIDDEN:
    aState.AssignLiteral("locked-background");
    break;
  case WAKE_LOCK_STATE_VISIBLE:
    aState.AssignLiteral("locked-foreground");
    break;
  }
}

void
PowerManagerService::Notify(const WakeLockInformation& aWakeLockInfo)
{
  nsAutoString state;
  ComputeWakeLockState(aWakeLockInfo, state);

  /**
   * Copy the listeners list before we walk through the callbacks
   * because the callbacks may install new listeners. We expect no
   * more than one listener per window, so it shouldn't be too long.
   */
  nsAutoTArray<nsCOMPtr<nsIDOMMozWakeLockListener>, 2> listeners(mWakeLockListeners);

  for (uint32_t i = 0; i < listeners.Length(); ++i) {
    listeners[i]->Callback(aWakeLockInfo.topic(), state);
  }
}

void
PowerManagerService::SyncProfile()
{
  nsCOMPtr<nsIObserverService> obsServ = services::GetObserverService();
  if (obsServ) {
    NS_NAMED_LITERAL_STRING(context, "shutdown-persist");
    obsServ->NotifyObservers(nullptr, "profile-change-net-teardown", context.get());
    obsServ->NotifyObservers(nullptr, "profile-change-teardown", context.get());
    obsServ->NotifyObservers(nullptr, "profile-before-change", context.get());
  }
}

NS_IMETHODIMP
PowerManagerService::Reboot()
{
  StartForceQuitWatchdog(eHalShutdownMode_Reboot, mWatchdogTimeoutSecs);
  // To synchronize any unsaved user data before rebooting.
  SyncProfile();
  hal::Reboot();
  MOZ_NOT_REACHED();
  return NS_OK;
}

NS_IMETHODIMP
PowerManagerService::PowerOff()
{
  StartForceQuitWatchdog(eHalShutdownMode_PowerOff, mWatchdogTimeoutSecs);
  // To synchronize any unsaved user data before powering off.
  SyncProfile();
  hal::PowerOff();
  MOZ_NOT_REACHED();
  return NS_OK;
}

NS_IMETHODIMP
PowerManagerService::Restart()
{
  // FIXME/bug 796826 this implementation is currently gonk-specific,
  // because it relies on the Gonk to initialize the Gecko processes to
  // restart B2G. It's better to do it here to have a real "restart".
  StartForceQuitWatchdog(eHalShutdownMode_Restart, mWatchdogTimeoutSecs);
  // To synchronize any unsaved user data before restarting.
  SyncProfile();
#ifdef XP_UNIX
  sync();
#endif
  _exit(0);
  MOZ_NOT_REACHED();
  return NS_OK;
}

NS_IMETHODIMP
PowerManagerService::AddWakeLockListener(nsIDOMMozWakeLockListener *aListener)
{
  if (mWakeLockListeners.Contains(aListener))
    return NS_OK;

  mWakeLockListeners.AppendElement(aListener);
  return NS_OK;
}

NS_IMETHODIMP
PowerManagerService::RemoveWakeLockListener(nsIDOMMozWakeLockListener *aListener)
{
  mWakeLockListeners.RemoveElement(aListener);
  return NS_OK;
}

NS_IMETHODIMP
PowerManagerService::GetWakeLockState(const nsAString &aTopic, nsAString &aState)
{
  WakeLockInformation info;
  GetWakeLockInfo(aTopic, &info);

  ComputeWakeLockState(info, aState);

  return NS_OK;
}

NS_IMETHODIMP
PowerManagerService::NewWakeLock(const nsAString &aTopic,
                                 nsIDOMWindow *aWindow,
                                 nsIDOMMozWakeLock **aWakeLock)
{
  nsRefPtr<WakeLock> wakelock = new WakeLock();
  nsresult rv = wakelock->Init(aTopic, aWindow);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDOMMozWakeLock> wl(wakelock);
  wl.forget(aWakeLock);

  return NS_OK;
}

} // power
} // dom
} // mozilla
