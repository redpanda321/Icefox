/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDeviceStorage_h
#define nsDeviceStorage_h

class nsPIDOMWindow;
#include "PCOMContentPermissionRequestChild.h"

#include "DOMRequest.h"
#include "nsAutoPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsDOMClassInfoID.h"
#include "nsIClassInfo.h"
#include "nsIContentPermissionPrompt.h"
#include "nsIDOMDeviceStorageCursor.h"
#include "nsIDOMDeviceStorageStat.h"
#include "nsIDOMWindow.h"
#include "nsIURI.h"
#include "nsInterfaceHashtable.h"
#include "nsIPrincipal.h"
#include "nsString.h"
#include "nsWeakPtr.h"
#include "nsIDOMEventListener.h"
#include "nsIDOMEventTarget.h"
#include "nsIObserver.h"
#include "nsIStringBundle.h"
#include "mozilla/Mutex.h"
#include "prtime.h"
#include "DeviceStorage.h"

#include "DeviceStorageRequestChild.h"

#define POST_ERROR_EVENT_FILE_EXISTS                 "NoModificationAllowedError"
#define POST_ERROR_EVENT_FILE_DOES_NOT_EXIST         "NotFoundError"
#define POST_ERROR_EVENT_FILE_NOT_ENUMERABLE         "TypeMismatchError"
#define POST_ERROR_EVENT_PERMISSION_DENIED           "SecurityError"
#define POST_ERROR_EVENT_ILLEGAL_TYPE                "TypeMismatchError"
#define POST_ERROR_EVENT_UNKNOWN                     "Unknown"

enum DeviceStorageRequestType {
    DEVICE_STORAGE_REQUEST_READ,
    DEVICE_STORAGE_REQUEST_WRITE,
    DEVICE_STORAGE_REQUEST_CREATE,
    DEVICE_STORAGE_REQUEST_DELETE,
    DEVICE_STORAGE_REQUEST_WATCH,
    DEVICE_STORAGE_REQUEST_STAT
};

class DeviceStorageTypeChecker MOZ_FINAL
{
public:
  static DeviceStorageTypeChecker* CreateOrGet();

  DeviceStorageTypeChecker();
  ~DeviceStorageTypeChecker();

  void InitFromBundle(nsIStringBundle* aBundle);

  bool Check(const nsAString& aType, nsIDOMBlob* aBlob);
  bool Check(const nsAString& aType, nsIFile* aFile);

  static nsresult GetPermissionForType(const nsAString& aType, nsACString& aPermissionResult);
  static nsresult GetAccessForRequest(const DeviceStorageRequestType aRequestType, nsACString& aAccessResult);

private:
  nsString mPicturesExtensions;
  nsString mVideosExtensions;
  nsString mMusicExtensions;

  static nsAutoPtr<DeviceStorageTypeChecker> sDeviceStorageTypeChecker;
};

class ContinueCursorEvent MOZ_FINAL : public nsRunnable
{
public:
  ContinueCursorEvent(nsRefPtr<mozilla::dom::DOMRequest>& aRequest);
  ContinueCursorEvent(mozilla::dom::DOMRequest* aRequest);
  ~ContinueCursorEvent();
  void Continue();

  NS_IMETHOD Run();
private:
  already_AddRefed<DeviceStorageFile> GetNextFile();
  nsRefPtr<mozilla::dom::DOMRequest> mRequest;
};

class nsDOMDeviceStorageCursor MOZ_FINAL
  : public nsIDOMDeviceStorageCursor
  , public mozilla::dom::DOMRequest
  , public nsIContentPermissionRequest
  , public PCOMContentPermissionRequestChild
  , public mozilla::dom::devicestorage::DeviceStorageRequestChildCallback
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSICONTENTPERMISSIONREQUEST
  NS_DECL_NSIDOMDEVICESTORAGECURSOR

  nsDOMDeviceStorageCursor(nsIDOMWindow* aWindow,
                           nsIPrincipal* aPrincipal,
                           DeviceStorageFile* aFile,
                           PRTime aSince);


  nsTArray<nsRefPtr<DeviceStorageFile> > mFiles;
  bool mOkToCallContinue;
  PRTime mSince;

  virtual bool Recv__delete__(const bool& allow);
  virtual void IPDLRelease();

  void GetStorageType(nsAString & aType);

  void RequestComplete();

private:
  ~nsDOMDeviceStorageCursor();

  nsRefPtr<DeviceStorageFile> mFile;
  nsCOMPtr<nsIPrincipal> mPrincipal;
};

class nsDOMDeviceStorageStat MOZ_FINAL
  : public nsIDOMDeviceStorageStat
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMDEVICESTORAGESTAT

  nsDOMDeviceStorageStat(uint64_t aFreeBytes, uint64_t aTotalBytes, nsAString& aState);

private:
  ~nsDOMDeviceStorageStat();
  uint64_t mFreeBytes, mTotalBytes;
  nsString mState;
};

//helpers
jsval StringToJsval(nsPIDOMWindow* aWindow, nsAString& aString);
jsval nsIFileToJsval(nsPIDOMWindow* aWindow, DeviceStorageFile* aFile);
jsval InterfaceToJsval(nsPIDOMWindow* aWindow, nsISupports* aObject, const nsIID* aIID);

#ifdef MOZ_WIDGET_GONK
nsresult GetSDCardStatus(nsAString& aState);
#endif

#endif
