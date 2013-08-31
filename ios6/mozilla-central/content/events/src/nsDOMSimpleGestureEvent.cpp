/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDOMClassInfoID.h"
#include "nsDOMSimpleGestureEvent.h"

nsDOMSimpleGestureEvent::nsDOMSimpleGestureEvent(nsPresContext* aPresContext, nsSimpleGestureEvent* aEvent)
  : nsDOMMouseEvent(aPresContext, aEvent ? aEvent : new nsSimpleGestureEvent(false, 0, nullptr, 0, 0.0))
{
  NS_ASSERTION(mEvent->eventStructType == NS_SIMPLE_GESTURE_EVENT, "event type mismatch");

  if (aEvent) {
    mEventIsInternal = false;
  } else {
    mEventIsInternal = true;
    mEvent->time = PR_Now();
    mEvent->refPoint.x = mEvent->refPoint.y = 0;
    static_cast<nsMouseEvent*>(mEvent)->inputSource = nsIDOMMouseEvent::MOZ_SOURCE_UNKNOWN;
  }
}

nsDOMSimpleGestureEvent::~nsDOMSimpleGestureEvent()
{
  if (mEventIsInternal) {
    delete static_cast<nsSimpleGestureEvent*>(mEvent);
    mEvent = nullptr;
  }
}

NS_IMPL_ADDREF_INHERITED(nsDOMSimpleGestureEvent, nsDOMUIEvent)
NS_IMPL_RELEASE_INHERITED(nsDOMSimpleGestureEvent, nsDOMUIEvent)

DOMCI_DATA(SimpleGestureEvent, nsDOMSimpleGestureEvent)

NS_INTERFACE_MAP_BEGIN(nsDOMSimpleGestureEvent)
  NS_INTERFACE_MAP_ENTRY(nsIDOMSimpleGestureEvent)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(SimpleGestureEvent)
NS_INTERFACE_MAP_END_INHERITING(nsDOMMouseEvent)

/* readonly attribute unsigned long direction; */
NS_IMETHODIMP
nsDOMSimpleGestureEvent::GetDirection(uint32_t *aDirection)
{
  NS_ENSURE_ARG_POINTER(aDirection);
  *aDirection = static_cast<nsSimpleGestureEvent*>(mEvent)->direction;
  return NS_OK;
}

/* readonly attribute float delta; */
NS_IMETHODIMP
nsDOMSimpleGestureEvent::GetDelta(double *aDelta)
{
  NS_ENSURE_ARG_POINTER(aDelta);
  *aDelta = static_cast<nsSimpleGestureEvent*>(mEvent)->delta;
  return NS_OK;
}

/* readonly attribute unsigned long clickCount; */
NS_IMETHODIMP
nsDOMSimpleGestureEvent::GetClickCount(uint32_t *aClickCount)
{
  NS_ENSURE_ARG_POINTER(aClickCount);
  *aClickCount = static_cast<nsSimpleGestureEvent*>(mEvent)->clickCount;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMSimpleGestureEvent::InitSimpleGestureEvent(const nsAString& aTypeArg,
                                                bool aCanBubbleArg,
                                                bool aCancelableArg,
                                                nsIDOMWindow* aViewArg,
                                                int32_t aDetailArg,
                                                int32_t aScreenX, 
                                                int32_t aScreenY,
                                                int32_t aClientX,
                                                int32_t aClientY,
                                                bool aCtrlKeyArg,
                                                bool aAltKeyArg,
                                                bool aShiftKeyArg,
                                                bool aMetaKeyArg,
                                                uint16_t aButton,
                                                nsIDOMEventTarget* aRelatedTarget,
                                                uint32_t aDirectionArg,
                                                double aDeltaArg,
                                                uint32_t aClickCountArg)
{
  nsresult rv = nsDOMMouseEvent::InitMouseEvent(aTypeArg,
                                                aCanBubbleArg,
                                                aCancelableArg,
                                                aViewArg,
                                                aDetailArg,
                                                aScreenX, 
                                                aScreenY,
                                                aClientX,
                                                aClientY,
                                                aCtrlKeyArg,
                                                aAltKeyArg,
                                                aShiftKeyArg,
                                                aMetaKeyArg,
                                                aButton,
                                                aRelatedTarget);
  NS_ENSURE_SUCCESS(rv, rv);

  nsSimpleGestureEvent* simpleGestureEvent = static_cast<nsSimpleGestureEvent*>(mEvent);
  simpleGestureEvent->direction = aDirectionArg;
  simpleGestureEvent->delta = aDeltaArg;
  simpleGestureEvent->clickCount = aClickCountArg;

  return NS_OK;
}

nsresult NS_NewDOMSimpleGestureEvent(nsIDOMEvent** aInstancePtrResult,
                                     nsPresContext* aPresContext,
                                     nsSimpleGestureEvent *aEvent)
{
  nsDOMSimpleGestureEvent *it = new nsDOMSimpleGestureEvent(aPresContext, aEvent);
  if (nullptr == it) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  return CallQueryInterface(it, aInstancePtrResult);
}
