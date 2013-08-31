/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothmanager_h__
#define mozilla_dom_bluetooth_bluetoothmanager_h__

#include "BluetoothCommon.h"
#include "BluetoothPropertyContainer.h"
#include "nsDOMEventTargetHelper.h"
#include "nsIDOMBluetoothManager.h"
#include "mozilla/Observer.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothNamedValue;

class BluetoothManager : public nsDOMEventTargetHelper
                       , public nsIDOMBluetoothManager
                       , public BluetoothSignalObserver
                       , public BluetoothPropertyContainer
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMBLUETOOTHMANAGER

  NS_FORWARD_NSIDOMEVENTTARGET(nsDOMEventTargetHelper::)

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(BluetoothManager,
                                           nsDOMEventTargetHelper)

  static already_AddRefed<BluetoothManager>
  Create(nsPIDOMWindow* aWindow);
  void Notify(const BluetoothSignal& aData);
  virtual void SetPropertyByValue(const BluetoothNamedValue& aValue);
  nsresult FireEnabledDisabledEvent(bool aEnabled);
private:
  BluetoothManager(nsPIDOMWindow* aWindow);
  ~BluetoothManager();
};

END_BLUETOOTH_NAMESPACE

nsresult NS_NewBluetoothManager(nsPIDOMWindow* aWindow,
                                nsIDOMBluetoothManager** aBluetoothManager);

#endif
