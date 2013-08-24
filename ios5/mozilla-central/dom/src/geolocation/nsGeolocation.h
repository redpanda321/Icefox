/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsGeoLocation_h
#define nsGeoLocation_h

#include "mozilla/dom/PContentPermissionRequestChild.h"
// Microsoft's API Name hackery sucks
#undef CreateEvent

#include "nsCOMPtr.h"
#include "nsAutoPtr.h"
#include "nsCOMArray.h"
#include "nsTArray.h"
#include "nsITimer.h"
#include "nsIObserver.h"
#include "nsIURI.h"

#include "nsWeakPtr.h"
#include "nsCycleCollectionParticipant.h"

#include "nsIDOMGeoGeolocation.h"
#include "nsIDOMGeoPosition.h"
#include "nsIDOMGeoPositionError.h"
#include "nsIDOMGeoPositionCallback.h"
#include "nsIDOMGeoPositionErrorCallback.h"
#include "nsIDOMNavigatorGeolocation.h"

#include "nsPIDOMWindow.h"

#include "nsIGeolocationProvider.h"
#include "nsIContentPermissionPrompt.h"
#include "DictionaryHelpers.h"
#include "PCOMContentPermissionRequestChild.h"
#include "mozilla/Attributes.h"

class nsGeolocationService;
class nsGeolocation;

class nsGeolocationRequest
 : public nsIContentPermissionRequest
 , public nsITimerCallback
 , public PCOMContentPermissionRequestChild
{
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSICONTENTPERMISSIONREQUEST
  NS_DECL_NSITIMERCALLBACK

  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsGeolocationRequest, nsIContentPermissionRequest)

  nsGeolocationRequest(nsGeolocation* locator,
                       nsIDOMGeoPositionCallback* callback,
                       nsIDOMGeoPositionErrorCallback* errorCallback,
                       bool watchPositionRequest = false);
  nsresult Init(JSContext* aCx, const jsval& aOptions);
  void Shutdown();

  // Called by the geolocation device to notify that a location has changed.
  bool Update(nsIDOMGeoPosition* aPosition);

  void SendLocation(nsIDOMGeoPosition* location);
  void MarkCleared();
  bool IsActive() {return !mCleared;}
  bool Allowed() {return mAllowed;}
  void SetTimeoutTimer();

  ~nsGeolocationRequest();

  bool Recv__delete__(const bool& allow);
  void IPDLRelease() { Release(); }

 private:

  void NotifyError(PRInt16 errorCode);
  bool mAllowed;
  bool mCleared;
  bool mIsWatchPositionRequest;

  nsCOMPtr<nsITimer> mTimeoutTimer;
  nsCOMPtr<nsIDOMGeoPositionCallback> mCallback;
  nsCOMPtr<nsIDOMGeoPositionErrorCallback> mErrorCallback;
  nsAutoPtr<mozilla::dom::GeoPositionOptions> mOptions;

  nsRefPtr<nsGeolocation> mLocator;
};

/**
 * Singleton that manages the geolocation provider
 */
class nsGeolocationService MOZ_FINAL : public nsIGeolocationUpdate, public nsIObserver
{
public:

  static nsGeolocationService* GetGeolocationService();
  static nsGeolocationService* GetInstance();  // Non-Addref'ing
  static nsGeolocationService* gService;

  NS_DECL_ISUPPORTS
  NS_DECL_NSIGEOLOCATIONUPDATE
  NS_DECL_NSIOBSERVER

  nsGeolocationService() {
      mHigherAccuracy = false;
  }

  nsresult Init();

  // Management of the nsGeolocation objects
  void AddLocator(nsGeolocation* locator);
  void RemoveLocator(nsGeolocation* locator);

  void SetCachedPosition(nsIDOMGeoPosition* aPosition);
  nsIDOMGeoPosition* GetCachedPosition();

  // Find and startup a geolocation device (gps, nmea, etc.)
  nsresult StartDevice();

  // Stop the started geolocation device (gps, nmea, etc.)
  void     StopDevice();
  
  // create, or reinitalize the callback timer
  void     SetDisconnectTimer();

  // request higher accuracy, if possible
  void     SetHigherAccuracy(bool aEnable);

private:

  ~nsGeolocationService();

  // Disconnect timer.  When this timer expires, it clears all pending callbacks
  // and closes down the provider, unless we are watching a point, and in that
  // case, we disable the disconnect timer.
  nsCOMPtr<nsITimer> mDisconnectTimer;

  // The object providing geo location information to us.
  nsCOMArray<nsIGeolocationProvider> mProviders;

  // mGeolocators are not owned here.  Their constructor
  // adds them to this list, and their destructor removes
  // them from this list.
  nsTArray<nsGeolocation*> mGeolocators;

  // This is the last geo position that we have seen.
  nsCOMPtr<nsIDOMGeoPosition> mLastPosition;

  // Current state of requests for higher accuracy
  bool mHigherAccuracy;
};


/**
 * Can return a geolocation info
 */ 
class nsGeolocation MOZ_FINAL : public nsIDOMGeoGeolocation
{
public:

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSIDOMGEOGEOLOCATION

  NS_DECL_CYCLE_COLLECTION_CLASS(nsGeolocation)

  nsGeolocation();

  nsresult Init(nsIDOMWindow* contentDom=nsnull);

  // Called by the geolocation device to notify that a location has changed.
  void Update(nsIDOMGeoPosition* aPosition);

  // Returns true if any of the callbacks are repeating
  bool HasActiveCallbacks();

  // Remove request from all callbacks arrays
  void RemoveRequest(nsGeolocationRequest* request);

  // Shutting down.
  void Shutdown();

  // Getter for the URI that this nsGeolocation was loaded from
  nsIURI* GetURI() { return mURI; }

  // Getter for the window that this nsGeolocation is owned by
  nsIWeakReference* GetOwner() { return mOwner; }

  // Check to see if the widnow still exists
  bool WindowOwnerStillExists();

private:

  ~nsGeolocation();

  bool RegisterRequestWithPrompt(nsGeolocationRequest* request);

  // Two callback arrays.  The first |mPendingCallbacks| holds objects for only
  // one callback and then they are released/removed from the array.  The second
  // |mWatchingCallbacks| holds objects until the object is explictly removed or
  // there is a page change.

  nsTArray<nsRefPtr<nsGeolocationRequest> > mPendingCallbacks;
  nsTArray<nsRefPtr<nsGeolocationRequest> > mWatchingCallbacks;

  // window that this was created for.  Weak reference.
  nsWeakPtr mOwner;

  // where the content was loaded from
  nsCOMPtr<nsIURI> mURI;

  // owning back pointer.
  nsRefPtr<nsGeolocationService> mService;
};

#endif /* nsGeoLocation_h */
