/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_ipc_bluetoothservicechildprocess_h__
#define mozilla_dom_bluetooth_ipc_bluetoothservicechildprocess_h__

#include "BluetoothService.h"

namespace mozilla {
namespace ipc {
class UnixSocketConsumer;
}
namespace dom {
namespace bluetooth {

class BluetoothChild;

} // namespace bluetooth
} // namespace dom
} // namespace mozilla


BEGIN_BLUETOOTH_NAMESPACE

class BluetoothServiceChildProcess : public BluetoothService
{
  friend class mozilla::dom::bluetooth::BluetoothChild;

public:
  static BluetoothServiceChildProcess*
  Create();

  virtual void
  RegisterBluetoothSignalHandler(const nsAString& aNodeName,
                                 BluetoothSignalObserver* aMsgHandler)
                                 MOZ_OVERRIDE;

  virtual void
  UnregisterBluetoothSignalHandler(const nsAString& aNodeName,
                                   BluetoothSignalObserver* aMsgHandler)
                                   MOZ_OVERRIDE;

  virtual nsresult
  GetDefaultAdapterPathInternal(BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual nsresult
  GetPairedDevicePropertiesInternal(const nsTArray<nsString>& aDeviceAddresses,
                                    BluetoothReplyRunnable* aRunnable)
                                    MOZ_OVERRIDE;

  virtual nsresult
  StopDiscoveryInternal(const nsAString& aAdapterPath,
                        BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual nsresult
  StartDiscoveryInternal(const nsAString& aAdapterPath,
                         BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual nsresult
  GetProperties(BluetoothObjectType aType,
                const nsAString& aPath,
                BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual nsresult
  SetProperty(BluetoothObjectType aType,
              const nsAString& aPath,
              const BluetoothNamedValue& aValue,
              BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual bool
  GetDevicePath(const nsAString& aAdapterPath,
                const nsAString& aDeviceAddress,
                nsAString& aDevicePath) MOZ_OVERRIDE;

  virtual nsresult
  CreatePairedDeviceInternal(const nsAString& aAdapterPath,
                             const nsAString& aAddress,
                             int aTimeout,
                             BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual nsresult
  RemoveDeviceInternal(const nsAString& aAdapterPath,
                       const nsAString& aObjectPath,
                       BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual nsresult
  GetScoSocket(const nsAString& aObjectPath,
               bool aAuth,
               bool aEncrypt,
               mozilla::ipc::UnixSocketConsumer* aConsumer) MOZ_OVERRIDE;

  virtual nsresult
  GetSocketViaService(const nsAString& aObjectPath,
                      const nsAString& aService,
                      BluetoothSocketType aType,
                      bool aAuth,
                      bool aEncrypt,
                      mozilla::ipc::UnixSocketConsumer* aConsumer,
                      BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual nsresult
  ListenSocketViaService(int aChannel,
                         BluetoothSocketType aType,
                         bool aAuth,
                         bool aEncrypt,
                         mozilla::ipc::UnixSocketConsumer* aConsumer) MOZ_OVERRIDE;

  virtual bool
  SetPinCodeInternal(const nsAString& aDeviceAddress,
                     const nsAString& aPinCode,
                     BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual bool
  SetPasskeyInternal(const nsAString& aDeviceAddress,
                     uint32_t aPasskey,
                     BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual bool
  SetPairingConfirmationInternal(const nsAString& aDeviceAddress,
                                 bool aConfirm,
                                 BluetoothReplyRunnable* aRunnable)
                                 MOZ_OVERRIDE;

  virtual bool
  SetAuthorizationInternal(const nsAString& aDeviceAddress,
                           bool aAllow,
                           BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual void
  Connect(const nsAString& aDeviceAddress,
          const nsAString& aAdapterPath,
          const uint16_t aProfileId,
          BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual void
  Disconnect(const uint16_t aProfileId,
             BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual bool
  IsConnected(uint16_t aProfileId) MOZ_OVERRIDE;

  virtual void
  SendFile(const nsAString& aDeviceAddress,
           BlobParent* aBlobParent,
           BlobChild* aBlobChild,
           BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual void
  StopSendingFile(const nsAString& aDeviceAddress,
                  BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;

  virtual void
  ConfirmReceivingFile(const nsAString& aDeviceAddress,
                       bool aConfirm,
                       BluetoothReplyRunnable* aRunnable) MOZ_OVERRIDE;
protected:
  BluetoothServiceChildProcess();
  virtual ~BluetoothServiceChildProcess();

  void
  NoteDeadActor();

  void
  NoteShutdownInitiated();

  virtual nsresult
  HandleStartup() MOZ_OVERRIDE;

  virtual nsresult
  HandleShutdown() MOZ_OVERRIDE;

private:
  // This method should never be called.
  virtual nsresult
  StartInternal() MOZ_OVERRIDE;

  // This method should never be called.
  virtual nsresult
  StopInternal() MOZ_OVERRIDE;

  // Should never be called from the child
  virtual nsresult
  GetDevicePropertiesInternal(const BluetoothSignal& aSignal) MOZ_OVERRIDE;

  // This method should never be called from the child.
  virtual nsresult
  PrepareAdapterInternal(const nsAString& aPath) MOZ_OVERRIDE;
};

END_BLUETOOTH_NAMESPACE

#endif // mozilla_dom_bluetooth_ipc_bluetoothservicechildprocess_h__
