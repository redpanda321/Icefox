/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this file,
* You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "BluetoothServiceChildProcess.h"

#include "mozilla/Assertions.h"
#include "mozilla/dom/ContentChild.h"

#include "BluetoothChild.h"

USING_BLUETOOTH_NAMESPACE

namespace {

BluetoothChild* gBluetoothChild;

inline
void
SendRequest(BluetoothReplyRunnable* aRunnable, const Request& aRequest)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aRunnable);

  NS_WARN_IF_FALSE(gBluetoothChild,
                   "Calling methods on BluetoothServiceChildProcess during "
                   "shutdown!");

  if (gBluetoothChild) {
    BluetoothRequestChild* actor = new BluetoothRequestChild(aRunnable);
    gBluetoothChild->SendPBluetoothRequestConstructor(actor, aRequest);
  }
}

} // anonymous namespace

// static
BluetoothServiceChildProcess*
BluetoothServiceChildProcess::Create()
{
  MOZ_ASSERT(!gBluetoothChild);

  mozilla::dom::ContentChild* contentChild =
    mozilla::dom::ContentChild::GetSingleton();
  MOZ_ASSERT(contentChild);

  BluetoothServiceChildProcess* btService = new BluetoothServiceChildProcess();

  gBluetoothChild = new BluetoothChild(btService);
  contentChild->SendPBluetoothConstructor(gBluetoothChild);

  return btService;
}

BluetoothServiceChildProcess::BluetoothServiceChildProcess()
{
}

BluetoothServiceChildProcess::~BluetoothServiceChildProcess()
{
  gBluetoothChild = nullptr;
}

void
BluetoothServiceChildProcess::NoteDeadActor()
{
  MOZ_ASSERT(gBluetoothChild);
  gBluetoothChild = nullptr;
}

void
BluetoothServiceChildProcess::RegisterBluetoothSignalHandler(
                                              const nsAString& aNodeName,
                                              BluetoothSignalObserver* aHandler)
{
  if (gBluetoothChild) {
    gBluetoothChild->SendRegisterSignalHandler(nsString(aNodeName));
  }
  BluetoothService::RegisterBluetoothSignalHandler(aNodeName, aHandler);
}

void
BluetoothServiceChildProcess::UnregisterBluetoothSignalHandler(
                                              const nsAString& aNodeName,
                                              BluetoothSignalObserver* aHandler)
{
  if (gBluetoothChild) {
    gBluetoothChild->SendUnregisterSignalHandler(nsString(aNodeName));
  }
  BluetoothService::UnregisterBluetoothSignalHandler(aNodeName, aHandler);
}

nsresult
BluetoothServiceChildProcess::GetDefaultAdapterPathInternal(
                                              BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, DefaultAdapterPathRequest());
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::GetDevicePropertiesInternal(
                                                 const BluetoothSignal& aSignal)
{
  MOZ_NOT_REACHED("Should never be called from child");
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
BluetoothServiceChildProcess::GetPairedDevicePropertiesInternal(
                                     const nsTArray<nsString>& aDeviceAddresses,
                                     BluetoothReplyRunnable* aRunnable)
{
  DevicePropertiesRequest request;
  request.addresses().AppendElements(aDeviceAddresses);

  SendRequest(aRunnable, request);
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::StopDiscoveryInternal(
                                              const nsAString& aAdapterPath,
                                              BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, StopDiscoveryRequest(nsString(aAdapterPath)));
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::StartDiscoveryInternal(
                                              const nsAString& aAdapterPath,
                                              BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, StartDiscoveryRequest(nsString(aAdapterPath)));
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::GetProperties(BluetoothObjectType aType,
                                            const nsAString& aPath,
                                            BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, GetPropertyRequest(aType, nsString(aPath)));
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::SetProperty(BluetoothObjectType aType,
                                          const nsAString& aPath,
                                          const BluetoothNamedValue& aValue,
                                          BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, SetPropertyRequest(aType, nsString(aPath), aValue));
  return NS_OK;
}

bool
BluetoothServiceChildProcess::GetDevicePath(const nsAString& aAdapterPath,
                                            const nsAString& aDeviceAddress,
                                            nsAString& aDevicePath)
{
  // XXXbent Right now this is adapted from BluetoothDBusService's
  //         GetObjectPathFromAddress. This is basically a sync call that cannot
  //         be forwarded to the parent process without blocking. Hopefully this
  //         can be reworked.
  nsAutoString path(aAdapterPath);
  path.AppendLiteral("/dev_");
  path.Append(aDeviceAddress);
  path.ReplaceChar(':', '_');

  aDevicePath = path;

  return true;
}

nsresult
BluetoothServiceChildProcess::CreatePairedDeviceInternal(
                                              const nsAString& aAdapterPath,
                                              const nsAString& aAddress,
                                              int aTimeout,
                                              BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
              PairRequest(nsString(aAdapterPath), nsString(aAddress), aTimeout));
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::RemoveDeviceInternal(
                                              const nsAString& aAdapterPath,
                                              const nsAString& aObjectPath,
                                              BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
              UnpairRequest(nsString(aAdapterPath), nsString(aObjectPath)));
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::GetScoSocket(
                                    const nsAString& aObjectPath,
                                    bool aAuth,
                                    bool aEncrypt,
                                    mozilla::ipc::UnixSocketConsumer* aConsumer)
{
  MOZ_NOT_REACHED("This should never be called!");
  return NS_ERROR_FAILURE;
}

nsresult
BluetoothServiceChildProcess::GetSocketViaService(
                                       const nsAString& aObjectPath,
                                       const nsAString& aService,
                                       BluetoothSocketType aType,
                                       bool aAuth,
                                       bool aEncrypt,
                                       mozilla::ipc::UnixSocketConsumer* aConsumer,
                                       BluetoothReplyRunnable* aRunnable)
{
  MOZ_NOT_REACHED("This should never be called!");
  return NS_ERROR_FAILURE;
}


nsresult
BluetoothServiceChildProcess::ListenSocketViaService(
  int aChannel,
  BluetoothSocketType aType,
  bool aAuth,
  bool aEncrypt,
  mozilla::ipc::UnixSocketConsumer* aConsumer)
{
  MOZ_NOT_REACHED("This should never be called!");
  return NS_ERROR_FAILURE;
}

bool
BluetoothServiceChildProcess::SetPinCodeInternal(
                                                const nsAString& aDeviceAddress,
                                                const nsAString& aPinCode,
                                                BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
              SetPinCodeRequest(nsString(aDeviceAddress), nsString(aPinCode)));
  return true;
}

bool
BluetoothServiceChildProcess::SetPasskeyInternal(
                                                const nsAString& aDeviceAddress,
                                                uint32_t aPasskey,
                                                BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
              SetPasskeyRequest(nsString(aDeviceAddress), aPasskey));
  return true;
}

bool
BluetoothServiceChildProcess::SetPairingConfirmationInternal(
                                                const nsAString& aDeviceAddress,
                                                bool aConfirm,
                                                BluetoothReplyRunnable* aRunnable)
{
  if(aConfirm) {
    SendRequest(aRunnable,
                ConfirmPairingConfirmationRequest(nsString(aDeviceAddress)));
  } else {
    SendRequest(aRunnable,
                DenyPairingConfirmationRequest(nsString(aDeviceAddress)));
  }
  return true;
}

bool
BluetoothServiceChildProcess::SetAuthorizationInternal(
                                                const nsAString& aDeviceAddress,
                                                bool aAllow,
                                                BluetoothReplyRunnable* aRunnable)
{
  if(aAllow) {
    SendRequest(aRunnable,
                ConfirmAuthorizationRequest(nsString(aDeviceAddress)));
  } else {
    SendRequest(aRunnable,
                DenyAuthorizationRequest(nsString(aDeviceAddress)));
  }
  return true;
}

nsresult
BluetoothServiceChildProcess::PrepareAdapterInternal(const nsAString& aPath)
{
  MOZ_NOT_REACHED("Should never be called from child");
  return NS_ERROR_NOT_IMPLEMENTED;
}

void
BluetoothServiceChildProcess::Connect(
  const nsAString& aDeviceAddress,
  const nsAString& aAdapterPath,
  const uint16_t aProfileId,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
              ConnectRequest(nsString(aDeviceAddress),
                             nsString(aAdapterPath),
                             aProfileId));
}

void
BluetoothServiceChildProcess::Disconnect(
  const uint16_t aProfileId,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable, DisconnectRequest(aProfileId));
}

void
BluetoothServiceChildProcess::SendFile(
  const nsAString& aDeviceAddress,
  BlobParent* aBlobParent,
  BlobChild* aBlobChild,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
              SendFileRequest(nsString(aDeviceAddress), nullptr, aBlobChild));
}

void
BluetoothServiceChildProcess::StopSendingFile(
  const nsAString& aDeviceAddress,
  BluetoothReplyRunnable* aRunnable)
{
  SendRequest(aRunnable,
              StopSendingFileRequest(nsString(aDeviceAddress)));
}

void
BluetoothServiceChildProcess::ConfirmReceivingFile(
  const nsAString& aDeviceAddress,
  bool aConfirm,
  BluetoothReplyRunnable* aRunnable)
{
  if(aConfirm) {
    SendRequest(aRunnable,
                ConfirmReceivingFileRequest(nsString(aDeviceAddress)));
    return;
  }
  
  SendRequest(aRunnable,
              DenyReceivingFileRequest(nsString(aDeviceAddress)));
}

nsresult
BluetoothServiceChildProcess::HandleStartup()
{
  // Don't need to do anything here for startup since our Create function takes
  // care of the actor machinery.
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::HandleShutdown()
{
  // If this process is shutting down then we need to disconnect ourselves from
  // the parent.
  if (gBluetoothChild) {
    gBluetoothChild->BeginShutdown();
  }
  return NS_OK;
}

nsresult
BluetoothServiceChildProcess::StartInternal()
{
  MOZ_NOT_REACHED("This should never be called!");
  return NS_ERROR_FAILURE;
}

nsresult
BluetoothServiceChildProcess::StopInternal()
{
  MOZ_NOT_REACHED("This should never be called!");
  return NS_ERROR_FAILURE;
}

bool
BluetoothServiceChildProcess::IsConnected(uint16_t aProfileId)
{
  MOZ_NOT_REACHED("This should never be called!");
  return false;
}
