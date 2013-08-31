/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Hal.h"
#include "mozilla/HalSensor.h"

#include "nsDeviceSensors.h"

#include "nsAutoPtr.h"
#include "nsIDOMEvent.h"
#include "nsIDOMWindow.h"
#include "nsPIDOMWindow.h"
#include "nsIDOMDocument.h"
#include "nsIDOMEventTarget.h"
#include "nsIServiceManager.h"
#include "nsIServiceManager.h"
#include "GeneratedEvents.h"
#include "mozilla/Preferences.h"
#include "mozilla/Attributes.h"
#include "nsIPermissionManager.h"

using namespace mozilla;
using namespace hal;

#undef near

// also see sDefaultSensorHint in mobile/android/base/GeckoAppShell.java
#define DEFAULT_SENSOR_POLL 100

static const nsTArray<nsIDOMWindow*>::index_type NoIndex =
  nsTArray<nsIDOMWindow*>::NoIndex;

class nsDeviceSensorData MOZ_FINAL : public nsIDeviceSensorData
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIDEVICESENSORDATA

  nsDeviceSensorData(unsigned long type, double x, double y, double z);

private:
  ~nsDeviceSensorData();

protected:
  unsigned long mType;
  double mX, mY, mZ;
};

nsDeviceSensorData::nsDeviceSensorData(unsigned long type, double x, double y, double z)
  : mType(type), mX(x), mY(y), mZ(z)
{
}

NS_INTERFACE_MAP_BEGIN(nsDeviceSensorData)
NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDeviceSensorData)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(nsDeviceSensorData)
NS_IMPL_RELEASE(nsDeviceSensorData)

nsDeviceSensorData::~nsDeviceSensorData()
{
}

NS_IMETHODIMP nsDeviceSensorData::GetType(uint32_t *aType)
{
  NS_ENSURE_ARG_POINTER(aType);
  *aType = mType;
  return NS_OK;
}

NS_IMETHODIMP nsDeviceSensorData::GetX(double *aX)
{
  NS_ENSURE_ARG_POINTER(aX);
  *aX = mX;
  return NS_OK;
}

NS_IMETHODIMP nsDeviceSensorData::GetY(double *aY)
{
  NS_ENSURE_ARG_POINTER(aY);
  *aY = mY;
  return NS_OK;
}

NS_IMETHODIMP nsDeviceSensorData::GetZ(double *aZ)
{
  NS_ENSURE_ARG_POINTER(aZ);
  *aZ = mZ;
  return NS_OK;
}

NS_IMPL_ISUPPORTS1(nsDeviceSensors, nsIDeviceSensors)

nsDeviceSensors::nsDeviceSensors()
{
  mIsUserProximityNear = false;
  mLastDOMMotionEventTime = TimeStamp::Now();
  mEnabled = Preferences::GetBool("device.sensors.enabled", true);

  for (int i = 0; i < NUM_SENSOR_TYPE; i++) {
    nsTArray<nsIDOMWindow*> *windows = new nsTArray<nsIDOMWindow*>();
    mWindowListeners.AppendElement(windows);
  }

  mLastDOMMotionEventTime = TimeStamp::Now();
}

nsDeviceSensors::~nsDeviceSensors()
{
  for (int i = 0; i < NUM_SENSOR_TYPE; i++) {
    if (IsSensorEnabled(i))  
      UnregisterSensorObserver((SensorType)i, this);
  }

  for (int i = 0; i < NUM_SENSOR_TYPE; i++) {
    delete mWindowListeners[i];
  }
}

NS_IMETHODIMP nsDeviceSensors::HasWindowListener(uint32_t aType, nsIDOMWindow *aWindow, bool *aRetVal)
{
  if (!mEnabled)
    *aRetVal = false;
  else
    *aRetVal = mWindowListeners[aType]->IndexOf(aWindow) != NoIndex;

  return NS_OK;
}

NS_IMETHODIMP nsDeviceSensors::AddWindowListener(uint32_t aType, nsIDOMWindow *aWindow)
{
  if (!mEnabled)
    return NS_OK;

  if (mWindowListeners[aType]->IndexOf(aWindow) != NoIndex)
    return NS_OK;

  if (!IsSensorEnabled(aType)) {
    RegisterSensorObserver((SensorType)aType, this);
  }

  mWindowListeners[aType]->AppendElement(aWindow);
  return NS_OK;
}

NS_IMETHODIMP nsDeviceSensors::RemoveWindowListener(uint32_t aType, nsIDOMWindow *aWindow)
{
  if (mWindowListeners[aType]->IndexOf(aWindow) == NoIndex)
    return NS_OK;

  mWindowListeners[aType]->RemoveElement(aWindow);

  if (mWindowListeners[aType]->Length() == 0)
    UnregisterSensorObserver((SensorType)aType, this);

  return NS_OK;
}

NS_IMETHODIMP nsDeviceSensors::RemoveWindowAsListener(nsIDOMWindow *aWindow)
{
  for (int i = 0; i < NUM_SENSOR_TYPE; i++) {
    RemoveWindowListener((SensorType)i, aWindow);
  }
  return NS_OK;
}

static bool
WindowCannotReceiveSensorEvent (nsPIDOMWindow* aWindow)
{
  // Check to see if this window is in the background.  If
  // it is and it does not have the "background-sensors" permission,
  // don't send any device motion events to it.
  if (!aWindow || !aWindow->GetOuterWindow()) {
    return true;
  }

  if (aWindow->GetOuterWindow()->IsBackground()) {
    nsCOMPtr<nsIPermissionManager> permMgr =
      do_GetService(NS_PERMISSIONMANAGER_CONTRACTID);
    NS_ENSURE_TRUE(permMgr, false);
    uint32_t permission = nsIPermissionManager::DENY_ACTION;
    permMgr->TestPermissionFromWindow(aWindow, "background-sensors", &permission);
    return permission != nsIPermissionManager::ALLOW_ACTION;
  }

  return false;
}

void
nsDeviceSensors::Notify(const mozilla::hal::SensorData& aSensorData)
{
  uint32_t type = aSensorData.sensor();

  const InfallibleTArray<float>& values = aSensorData.values();
  size_t len = values.Length();
  double x = len > 0 ? values[0] : 0.0;
  double y = len > 1 ? values[1] : 0.0;
  double z = len > 2 ? values[2] : 0.0;

  nsCOMArray<nsIDOMWindow> windowListeners;
  for (uint32_t i = 0; i < mWindowListeners[type]->Length(); i++) {
    windowListeners.AppendObject(mWindowListeners[type]->SafeElementAt(i));
  }

  for (uint32_t i = windowListeners.Count(); i > 0 ; ) {
    --i;

    nsCOMPtr<nsPIDOMWindow> pwindow = do_QueryInterface(windowListeners[i]);
    if (WindowCannotReceiveSensorEvent(pwindow)) {
        continue;
    }

    nsCOMPtr<nsIDOMDocument> domdoc;
    windowListeners[i]->GetDocument(getter_AddRefs(domdoc));

    if (domdoc) {
      nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(windowListeners[i]);
      if (type == nsIDeviceSensorData::TYPE_ACCELERATION || 
        type == nsIDeviceSensorData::TYPE_LINEAR_ACCELERATION || 
        type == nsIDeviceSensorData::TYPE_GYROSCOPE)
        FireDOMMotionEvent(domdoc, target, type, x, y, z);
      else if (type == nsIDeviceSensorData::TYPE_ORIENTATION)
        FireDOMOrientationEvent(domdoc, target, x, y, z);
      else if (type == nsIDeviceSensorData::TYPE_PROXIMITY)
        FireDOMProximityEvent(target, x, y, z);
      else if (type == nsIDeviceSensorData::TYPE_LIGHT)
        FireDOMLightEvent(target, x);

    }
  }
}

void
nsDeviceSensors::FireDOMLightEvent(nsIDOMEventTarget *aTarget,
                                  double aValue)
{
  nsCOMPtr<nsIDOMEvent> event;
  NS_NewDOMDeviceLightEvent(getter_AddRefs(event), nullptr, nullptr);

  nsCOMPtr<nsIDOMDeviceLightEvent> oe = do_QueryInterface(event);
  oe->InitDeviceLightEvent(NS_LITERAL_STRING("devicelight"),
                          true,
                          false,
                          aValue);

  event->SetTrusted(true);

  bool defaultActionEnabled;
  aTarget->DispatchEvent(event, &defaultActionEnabled);
}

void
nsDeviceSensors::FireDOMProximityEvent(nsIDOMEventTarget *aTarget,
                                       double aValue,
                                       double aMin,
                                       double aMax)
{
  nsCOMPtr<nsIDOMEvent> event;
  NS_NewDOMDeviceProximityEvent(getter_AddRefs(event), nullptr, nullptr);
  nsCOMPtr<nsIDOMDeviceProximityEvent> oe = do_QueryInterface(event);

  oe->InitDeviceProximityEvent(NS_LITERAL_STRING("deviceproximity"),
                               true,
                               false,
                               aValue,
                               aMin,
                               aMax);

  event->SetTrusted(true);

  bool defaultActionEnabled;
  aTarget->DispatchEvent(event, &defaultActionEnabled);

  // Some proximity sensors only support a binary near or
  // far measurement. In this case, the sensor should report
  // its maximum range value in the far state and a lesser
  // value in the near state.

  bool near = (aValue < aMax);
  if (mIsUserProximityNear != near) {
    mIsUserProximityNear = near;
    FireDOMUserProximityEvent(aTarget, mIsUserProximityNear);
  }
}

void
nsDeviceSensors::FireDOMUserProximityEvent(nsIDOMEventTarget *aTarget, bool aNear)
{
  nsCOMPtr<nsIDOMEvent> event;
  NS_NewDOMUserProximityEvent(getter_AddRefs(event), nullptr, nullptr);
  nsCOMPtr<nsIDOMUserProximityEvent> pe = do_QueryInterface(event);

  pe->InitUserProximityEvent(NS_LITERAL_STRING("userproximity"),
                             true,
                             false,
                             aNear);

  event->SetTrusted(true);

  bool defaultActionEnabled;
  aTarget->DispatchEvent(event, &defaultActionEnabled);
}

void
nsDeviceSensors::FireDOMOrientationEvent(nsIDOMDocument *domdoc,
                                         nsIDOMEventTarget *target,
                                         double alpha,
                                         double beta,
                                         double gamma)
{
  nsCOMPtr<nsIDOMEvent> event;
  bool defaultActionEnabled = true;
  domdoc->CreateEvent(NS_LITERAL_STRING("DeviceOrientationEvent"), getter_AddRefs(event));

  nsCOMPtr<nsIDOMDeviceOrientationEvent> oe = do_QueryInterface(event);

  if (!oe) {
    return;
  }

  oe->InitDeviceOrientationEvent(NS_LITERAL_STRING("deviceorientation"),
                                 true,
                                 false,
                                 alpha,
                                 beta,
                                 gamma,
                                 true);

  event->SetTrusted(true);
  
  target->DispatchEvent(event, &defaultActionEnabled);
}


void
nsDeviceSensors::FireDOMMotionEvent(nsIDOMDocument *domdoc,
                                   nsIDOMEventTarget *target,
                                   uint32_t type,
                                   double x,
                                   double y,
                                   double z) {
  // Attempt to coalesce events
  bool fireEvent = TimeStamp::Now() > mLastDOMMotionEventTime + TimeDuration::FromMilliseconds(DEFAULT_SENSOR_POLL);

  switch (type) {
  case nsIDeviceSensorData::TYPE_LINEAR_ACCELERATION:
    mLastAcceleration = new nsDOMDeviceAcceleration(x, y, z);
    break;
  case nsIDeviceSensorData::TYPE_ACCELERATION:
    mLastAccelerationIncluduingGravity = new nsDOMDeviceAcceleration(x, y, z);
    break;
  case nsIDeviceSensorData::TYPE_GYROSCOPE:
    mLastRotationRate = new nsDOMDeviceRotationRate(x, y, z);
    break;
  }

  if (!fireEvent && (!mLastAcceleration || !mLastAccelerationIncluduingGravity || !mLastRotationRate)) {
    return;
  }

  nsCOMPtr<nsIDOMEvent> event;
  domdoc->CreateEvent(NS_LITERAL_STRING("DeviceMotionEvent"), getter_AddRefs(event));

  nsCOMPtr<nsIDOMDeviceMotionEvent> me = do_QueryInterface(event);

  if (!me)
    return;

  me->InitDeviceMotionEvent(NS_LITERAL_STRING("devicemotion"),
                            true,
                            false,
                            mLastAcceleration,
                            mLastAccelerationIncluduingGravity,
                            mLastRotationRate,
                            DEFAULT_SENSOR_POLL);

  event->SetTrusted(true);

  bool defaultActionEnabled = true;
  target->DispatchEvent(event, &defaultActionEnabled);

  mLastRotationRate = nullptr;
  mLastAccelerationIncluduingGravity = nullptr;
  mLastAcceleration = nullptr;
  mLastDOMMotionEventTime = TimeStamp::Now();
}
