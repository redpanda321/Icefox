/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <limits>
#include "mozilla/Hal.h"
#include "BatteryManager.h"
#include "nsIDOMClassInfo.h"
#include "Constants.h"
#include "nsDOMEvent.h"
#include "mozilla/Preferences.h"
#include "nsDOMEventTargetHelper.h"

/**
 * We have to use macros here because our leak analysis tool things we are
 * leaking strings when we have |static const nsString|. Sad :(
 */
#define LEVELCHANGE_EVENT_NAME           NS_LITERAL_STRING("levelchange")
#define CHARGINGCHANGE_EVENT_NAME        NS_LITERAL_STRING("chargingchange")
#define DISCHARGINGTIMECHANGE_EVENT_NAME NS_LITERAL_STRING("dischargingtimechange")
#define CHARGINGTIMECHANGE_EVENT_NAME    NS_LITERAL_STRING("chargingtimechange")

DOMCI_DATA(BatteryManager, mozilla::dom::battery::BatteryManager)

namespace mozilla {
namespace dom {
namespace battery {

NS_IMPL_CYCLE_COLLECTION_INHERITED_0(BatteryManager, nsDOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(BatteryManager)
  NS_INTERFACE_MAP_ENTRY(nsIDOMBatteryManager)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(BatteryManager)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(BatteryManager, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(BatteryManager, nsDOMEventTargetHelper)

NS_IMPL_EVENT_HANDLER(BatteryManager, levelchange)
NS_IMPL_EVENT_HANDLER(BatteryManager, chargingchange)
NS_IMPL_EVENT_HANDLER(BatteryManager, chargingtimechange)
NS_IMPL_EVENT_HANDLER(BatteryManager, dischargingtimechange)

BatteryManager::BatteryManager()
  : mLevel(kDefaultLevel)
  , mCharging(kDefaultCharging)
  , mRemainingTime(kDefaultRemainingTime)
{
}

void
BatteryManager::Init(nsPIDOMWindow *aWindow)
{
  BindToOwner(aWindow);

  hal::RegisterBatteryObserver(this);

  hal::BatteryInformation batteryInfo;
  hal::GetCurrentBatteryInformation(&batteryInfo);

  UpdateFromBatteryInfo(batteryInfo);
}

void
BatteryManager::Shutdown()
{
  hal::UnregisterBatteryObserver(this);
}

NS_IMETHODIMP
BatteryManager::GetCharging(bool* aCharging)
{
  *aCharging = mCharging;

  return NS_OK;
}

NS_IMETHODIMP
BatteryManager::GetLevel(double* aLevel)
{
  *aLevel = mLevel;

  return NS_OK;
}

NS_IMETHODIMP
BatteryManager::GetDischargingTime(double* aDischargingTime)
{
  if (mCharging || mRemainingTime == kUnknownRemainingTime) {
    *aDischargingTime = std::numeric_limits<double>::infinity();
    return NS_OK;
  }

  *aDischargingTime = mRemainingTime;

  return NS_OK;
}

NS_IMETHODIMP
BatteryManager::GetChargingTime(double* aChargingTime)
{
  if (!mCharging || mRemainingTime == kUnknownRemainingTime) {
    *aChargingTime = std::numeric_limits<double>::infinity();
    return NS_OK;
  }

  *aChargingTime = mRemainingTime;

  return NS_OK;
}

void
BatteryManager::UpdateFromBatteryInfo(const hal::BatteryInformation& aBatteryInfo)
{
  mLevel = aBatteryInfo.level();
  mCharging = aBatteryInfo.charging();
  mRemainingTime = aBatteryInfo.remainingTime();

  // Add some guards to make sure the values are coherent.
  if (mLevel == 1.0 && mCharging == true &&
      mRemainingTime != kDefaultRemainingTime) {
    mRemainingTime = kDefaultRemainingTime;
    NS_ERROR("Battery API: When charging and level at 1.0, remaining time "
             "should be 0. Please fix your backend!");
  }
}

void
BatteryManager::Notify(const hal::BatteryInformation& aBatteryInfo)
{
  double previousLevel = mLevel;
  bool previousCharging = mCharging;
  double previousRemainingTime = mRemainingTime;

  UpdateFromBatteryInfo(aBatteryInfo);

  if (previousCharging != mCharging) {
    DispatchTrustedEvent(CHARGINGCHANGE_EVENT_NAME);
  }

  if (previousLevel != mLevel) {
    DispatchTrustedEvent(LEVELCHANGE_EVENT_NAME);
  }

  /*
   * There are a few situations that could happen here:
   * 1. Charging state changed:
   *   a. Previous remaining time wasn't unkwonw, we have to fire an event for
   *      the change.
   *   b. New remaining time isn't unkwonw, we have to fire an event for it.
   * 2. Charging state didn't change but remainingTime did, we have to fire
   *    the event that correspond to the current charging state.
   */
  if (mCharging != previousCharging) {
    if (previousRemainingTime != kUnknownRemainingTime) {
      DispatchTrustedEvent(previousCharging ? CHARGINGTIMECHANGE_EVENT_NAME
                                            : DISCHARGINGTIMECHANGE_EVENT_NAME);
    }
    if (mRemainingTime != kUnknownRemainingTime) {
      DispatchTrustedEvent(mCharging ? CHARGINGTIMECHANGE_EVENT_NAME
                                     : DISCHARGINGTIMECHANGE_EVENT_NAME);
    }
  } else if (previousRemainingTime != mRemainingTime) {
    DispatchTrustedEvent(mCharging ? CHARGINGTIMECHANGE_EVENT_NAME
                                   : DISCHARGINGTIMECHANGE_EVENT_NAME);
  }
}

/* static */ bool
BatteryManager::HasSupport()
{
  return Preferences::GetBool("dom.battery.enabled", true);
}

} // namespace battery
} // namespace dom
} // namespace mozilla
