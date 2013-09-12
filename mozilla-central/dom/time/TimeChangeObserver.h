/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _mozilla_time_change_observer_h_
#define _mozilla_time_change_observer_h_

#include "mozilla/Hal.h"
#include "mozilla/Observer.h"
#include "mozilla/HalTypes.h"
#include "nsPIDOMWindow.h"
#include "nsWeakPtr.h"

typedef mozilla::Observer<int64_t> SystemClockChangeObserver;
typedef mozilla::Observer<mozilla::hal::SystemTimezoneChangeInformation> SystemTimezoneChangeObserver;

class nsSystemTimeChangeObserver : public SystemClockChangeObserver,
                                   public SystemTimezoneChangeObserver
{
public:
  static nsSystemTimeChangeObserver* GetInstance();
  virtual ~nsSystemTimeChangeObserver();

  // Implementing hal::SystemClockChangeObserver::Notify()
  void Notify(const int64_t& aClockDeltaMS);

  // Implementing hal::SystemTimezoneChangeObserver::Notify()
  void Notify(
    const mozilla::hal::SystemTimezoneChangeInformation& aSystemTimezoneChangeInfo);

  static nsresult AddWindowListener(nsIDOMWindow* aWindow);
  static nsresult RemoveWindowListener(nsIDOMWindow* aWindow);
private:
  nsresult AddWindowListenerImpl(nsIDOMWindow* aWindow);
  nsresult RemoveWindowListenerImpl(nsIDOMWindow* aWindow);
  nsSystemTimeChangeObserver() { };
  nsTArray<nsWeakPtr> mWindowListeners;
  void FireMozTimeChangeEvent();
};

#endif //_mozilla_time_change_observer_h_
