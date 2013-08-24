/* vim: set shiftwidth=2 tabstop=8 autoindent cindent expandtab: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDOMTouchEvent.h"
#include "nsGUIEvent.h"
#include "nsDOMClassInfoID.h"
#include "nsIClassInfo.h"
#include "nsIXPCScriptable.h"
#include "nsContentUtils.h"
#include "mozilla/Preferences.h"
#include "nsPresContext.h"

using namespace mozilla;

DOMCI_DATA(Touch, nsDOMTouch)

NS_IMPL_CYCLE_COLLECTION_1(nsDOMTouch, mTarget)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsDOMTouch)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMTouch)
  NS_INTERFACE_MAP_ENTRY(nsIDOMTouch)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(Touch)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsDOMTouch)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsDOMTouch)

NS_IMETHODIMP
nsDOMTouch::GetIdentifier(PRInt32* aIdentifier)
{
  *aIdentifier = mIdentifier;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouch::GetTarget(nsIDOMEventTarget** aTarget)
{
  NS_IF_ADDREF(*aTarget = mTarget);
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouch::GetScreenX(PRInt32* aScreenX)
{
  *aScreenX = mScreenPoint.x;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouch::GetScreenY(PRInt32* aScreenY)
{
  *aScreenY = mScreenPoint.y;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouch::GetClientX(PRInt32* aClientX)
{
  *aClientX = mClientPoint.x;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouch::GetClientY(PRInt32* aClientY)
{
  *aClientY = mClientPoint.y;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouch::GetPageX(PRInt32* aPageX)
{
  *aPageX = mPagePoint.x;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouch::GetPageY(PRInt32* aPageY)
{
  *aPageY = mPagePoint.y;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouch::GetRadiusX(PRInt32* aRadiusX)
{
  *aRadiusX = mRadius.x;
  return NS_OK;
}
                                             
NS_IMETHODIMP
nsDOMTouch::GetRadiusY(PRInt32* aRadiusY)
{
  *aRadiusY = mRadius.y;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouch::GetRotationAngle(float* aRotationAngle)
{
  *aRotationAngle = mRotationAngle;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouch::GetForce(float* aForce)
{
  *aForce = mForce;
  return NS_OK;
}

bool
nsDOMTouch::Equals(nsIDOMTouch* aTouch)
{
  float force;
  float orientation;
  PRInt32 radiusX, radiusY;
  aTouch->GetForce(&force);
  aTouch->GetRotationAngle(&orientation);
  aTouch->GetRadiusX(&radiusX);
  aTouch->GetRadiusY(&radiusY);
  return mRefPoint != aTouch->mRefPoint ||
         (mForce != force) ||
         (mRotationAngle != orientation) ||
         (mRadius.x != radiusX) || (mRadius.y != radiusY);
}

// TouchList

DOMCI_DATA(TouchList, nsDOMTouchList)

NS_IMPL_CYCLE_COLLECTION_CLASS(nsDOMTouchList)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsDOMTouchList)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsIDOMTouchList)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(TouchList)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsDOMTouchList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSTARRAY_OF_NSCOMPTR(mPoints)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mParent)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(nsDOMTouchList)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsDOMTouchList)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSTARRAY(mPoints)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mParent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsDOMTouchList)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsDOMTouchList)

NS_IMETHODIMP
nsDOMTouchList::GetLength(PRUint32* aLength)
{
  *aLength = mPoints.Length();
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouchList::Item(PRUint32 aIndex, nsIDOMTouch** aRetVal)
{
  NS_IF_ADDREF(*aRetVal = nsDOMTouchList::GetItemAt(aIndex));
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouchList::IdentifiedTouch(PRInt32 aIdentifier, nsIDOMTouch** aRetVal)
{
  *aRetVal = nsnull;
  for (PRUint32 i = 0; i < mPoints.Length(); ++i) {
    nsCOMPtr<nsIDOMTouch> point = mPoints[i];
    PRInt32 identifier;
    if (point && NS_SUCCEEDED(point->GetIdentifier(&identifier)) &&
        aIdentifier == identifier) {
      point.swap(*aRetVal);
      break;
    }
  }
  return NS_OK;
}

nsIDOMTouch*
nsDOMTouchList::GetItemAt(PRUint32 aIndex)
{
  return mPoints.SafeElementAt(aIndex, nsnull);
}

// TouchEvent

nsDOMTouchEvent::nsDOMTouchEvent(nsPresContext* aPresContext,
                                 nsTouchEvent* aEvent)
  : nsDOMUIEvent(aPresContext, aEvent ? aEvent :
                                        new nsTouchEvent(false, 0, nsnull))
{
  if (aEvent) {
    mEventIsInternal = false;

    for (PRUint32 i = 0; i < aEvent->touches.Length(); ++i) {
      nsIDOMTouch *touch = aEvent->touches[i];
      nsDOMTouch *domtouch = static_cast<nsDOMTouch*>(touch);
      domtouch->InitializePoints(mPresContext, aEvent);
    }
  } else {
    mEventIsInternal = true;
    mEvent->time = PR_Now();
  }
}

nsDOMTouchEvent::~nsDOMTouchEvent()
{
  if (mEventIsInternal && mEvent) {
    delete static_cast<nsTouchEvent*>(mEvent);
    mEvent = nsnull;
  }
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsDOMTouchEvent)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(nsDOMTouchEvent, nsDOMUIEvent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mTouches)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mTargetTouches)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mChangedTouches)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsDOMTouchEvent, nsDOMUIEvent)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mTouches)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mTargetTouches)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mChangedTouches)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

DOMCI_DATA(TouchEvent, nsDOMTouchEvent)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(nsDOMTouchEvent)
  NS_INTERFACE_MAP_ENTRY(nsIDOMTouchEvent)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(TouchEvent)
NS_INTERFACE_MAP_END_INHERITING(nsDOMUIEvent)

NS_IMPL_ADDREF_INHERITED(nsDOMTouchEvent, nsDOMUIEvent)
NS_IMPL_RELEASE_INHERITED(nsDOMTouchEvent, nsDOMUIEvent)


NS_IMETHODIMP
nsDOMTouchEvent::InitTouchEvent(const nsAString& aType,
                                bool aCanBubble,
                                bool aCancelable,
                                nsIDOMWindow* aView,
                                PRInt32 aDetail,
                                bool aCtrlKey,
                                bool aAltKey,
                                bool aShiftKey,
                                bool aMetaKey,
                                nsIDOMTouchList* aTouches,
                                nsIDOMTouchList* aTargetTouches,
                                nsIDOMTouchList* aChangedTouches)
{
  nsresult rv = nsDOMUIEvent::InitUIEvent(aType,
                                          aCanBubble,
                                          aCancelable,
                                          aView,
                                          aDetail);
  NS_ENSURE_SUCCESS(rv, rv);

  static_cast<nsInputEvent*>(mEvent)->InitBasicModifiers(aCtrlKey, aAltKey,
                                                         aShiftKey, aMetaKey);
  mTouches = aTouches;
  mTargetTouches = aTargetTouches;
  mChangedTouches = aChangedTouches;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouchEvent::GetTouches(nsIDOMTouchList** aTouches)
{
  NS_ENSURE_ARG_POINTER(aTouches);
  NS_ENSURE_STATE(mEvent);
  nsRefPtr<nsDOMTouchList> t;

  if (mTouches) {
    return CallQueryInterface(mTouches, aTouches);
  }

  nsTouchEvent* touchEvent = static_cast<nsTouchEvent*>(mEvent);
  if (mEvent->message == NS_TOUCH_END || mEvent->message == NS_TOUCH_CANCEL) {
    // for touchend events, remove any changed touches from the touches array
    nsTArray<nsCOMPtr<nsIDOMTouch> > unchangedTouches;
    nsTArray<nsCOMPtr<nsIDOMTouch> > touches = touchEvent->touches;
    for (PRUint32 i = 0; i < touches.Length(); ++i) {
      if (!touches[i]->mChanged) {
        unchangedTouches.AppendElement(touches[i]);
      }
    }
    t = new nsDOMTouchList(static_cast<nsIDOMTouchEvent*>(this),
                           unchangedTouches);
  } else {
    t = new nsDOMTouchList(static_cast<nsIDOMTouchEvent*>(this),
                           touchEvent->touches);
  }
  mTouches = t;
  return CallQueryInterface(mTouches, aTouches);
}

NS_IMETHODIMP
nsDOMTouchEvent::GetTargetTouches(nsIDOMTouchList** aTargetTouches)
{
  NS_ENSURE_ARG_POINTER(aTargetTouches);
  NS_ENSURE_STATE(mEvent);

  if (mTargetTouches) {
    return CallQueryInterface(mTargetTouches, aTargetTouches);
  }

  nsTArray<nsCOMPtr<nsIDOMTouch> > targetTouches;
  nsTouchEvent* touchEvent = static_cast<nsTouchEvent*>(mEvent);
  nsTArray<nsCOMPtr<nsIDOMTouch> > touches = touchEvent->touches;
  for (PRUint32 i = 0; i < touches.Length(); ++i) {
    // for touchend/cancel events, don't append to the target list if this is a
    // touch that is ending
    if ((mEvent->message != NS_TOUCH_END &&
         mEvent->message != NS_TOUCH_CANCEL) || !touches[i]->mChanged) {
      nsIDOMEventTarget* targetPtr = touches[i]->GetTarget();
      if (targetPtr == mEvent->target) {
        targetTouches.AppendElement(touches[i]);
      }
    }
  }
  mTargetTouches = new nsDOMTouchList(static_cast<nsIDOMTouchEvent*>(this),
                                      targetTouches);
  return CallQueryInterface(mTargetTouches, aTargetTouches);
}

NS_IMETHODIMP
nsDOMTouchEvent::GetChangedTouches(nsIDOMTouchList** aChangedTouches)
{
  NS_ENSURE_ARG_POINTER(aChangedTouches);
  NS_ENSURE_STATE(mEvent);

  if (mChangedTouches) {
    return CallQueryInterface(mChangedTouches, aChangedTouches);
  }

  nsTArray<nsCOMPtr<nsIDOMTouch> > changedTouches;
  nsTouchEvent* touchEvent = static_cast<nsTouchEvent*>(mEvent);
  nsTArray<nsCOMPtr<nsIDOMTouch> > touches = touchEvent->touches;
  for (PRUint32 i = 0; i < touches.Length(); ++i) {
    if (touches[i]->mChanged) {
      changedTouches.AppendElement(touches[i]);
    }
  }
  mChangedTouches = new nsDOMTouchList(static_cast<nsIDOMTouchEvent*>(this),
                                       changedTouches);
  return CallQueryInterface(mChangedTouches, aChangedTouches);
}

NS_IMETHODIMP
nsDOMTouchEvent::GetAltKey(bool* aAltKey)
{
  *aAltKey = static_cast<nsInputEvent*>(mEvent)->IsAlt();
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouchEvent::GetMetaKey(bool* aMetaKey)
{
  *aMetaKey = static_cast<nsInputEvent*>(mEvent)->IsMeta();
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouchEvent::GetCtrlKey(bool* aCtrlKey)
{
  *aCtrlKey = static_cast<nsInputEvent*>(mEvent)->IsControl();
  return NS_OK;
}

NS_IMETHODIMP
nsDOMTouchEvent::GetShiftKey(bool* aShiftKey)
{
  *aShiftKey = static_cast<nsInputEvent*>(mEvent)->IsShift();
  return NS_OK;
}

bool
nsDOMTouchEvent::PrefEnabled()
{
  static bool sDidCheckPref = false;
  static bool sPrefValue = false;
  if (!sDidCheckPref) {
    sDidCheckPref = true;
    sPrefValue = Preferences::GetBool("dom.w3c_touch_events.enabled", false);
    if (sPrefValue) {
      nsContentUtils::InitializeTouchEventTable();
    }
  }
  return sPrefValue;
}

nsresult
NS_NewDOMTouchEvent(nsIDOMEvent** aInstancePtrResult,
                    nsPresContext* aPresContext,
                    nsTouchEvent *aEvent)
{
  nsDOMTouchEvent* it = new nsDOMTouchEvent(aPresContext, aEvent);

  return CallQueryInterface(it, aInstancePtrResult);
}
