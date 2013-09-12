/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothuuid_h__
#define mozilla_dom_bluetooth_bluetoothuuid_h__

#include "BluetoothCommon.h"

BEGIN_BLUETOOTH_NAMESPACE

/*
 * Service classes and Profile Identifiers
 *
 * Supported Bluetooth services for v1 are listed as below.
 *
 * The value of each service class is defined in "AssignedNumbers/Service
 * Discovery Protocol (SDP)/Service classes and Profile Identifiers" in the
 * Bluetooth Core Specification.
 */
enum BluetoothServiceClass
{
  HEADSET       = 0x1108,
  HEADSET_AG    = 0x1112,
  HANDSFREE     = 0x111E,
  HANDSFREE_AG  = 0x111F,
  OBJECT_PUSH   = 0x1105
};

class BluetoothUuidHelper
{
public:
  /**
   * Get a 128-bit uuid string calculated from a 16-bit service class UUID and
   * BASE_UUID
   *
   * @param aServiceClassUuid  16-bit service class UUID
   * @param aRetUuidStr  out parameter, 128-bit uuid string
   */
  static void
  GetString(BluetoothServiceClass aServiceClassUuid, nsAString& aRetUuidStr);
};

// TODO/qdot: Move these back into gonk and make the service handler deal with
// it there.
//
// Gotten from reading the "u8" values in B2G/external/bluez/src/adapter.c
// These were hardcoded into android
enum BluetoothReservedChannels {
  CHANNEL_DIALUP_NETWORK = 1,
  CHANNEL_HANDSFREE_AG = 10,
  CHANNEL_HEADSET_AG = 11,
  CHANNEL_OPUSH = 12,
  CHANNEL_SIM_ACCESS = 15,
  CHANNEL_PBAP_PSE = 19,
  CHANNEL_FTP = 20,
};

END_BLUETOOTH_NAMESPACE

#endif
