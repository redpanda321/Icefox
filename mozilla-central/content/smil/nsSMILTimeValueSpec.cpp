/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsSMILTimeValueSpec.h"
#include "nsSMILInterval.h"
#include "nsSMILTimeContainer.h"
#include "nsSMILTimeValue.h"
#include "nsSMILTimedElement.h"
#include "nsSMILInstanceTime.h"
#include "nsSMILParserUtils.h"
#include "nsISMILAnimationElement.h"
#include "nsEventListenerManager.h"
#include "nsGUIEvent.h"
#include "nsIDOMTimeEvent.h"
#include "nsString.h"
#include <limits>

using namespace mozilla::dom;

//----------------------------------------------------------------------
// Nested class: EventListener

NS_IMPL_ISUPPORTS1(nsSMILTimeValueSpec::EventListener, nsIDOMEventListener)

NS_IMETHODIMP
nsSMILTimeValueSpec::EventListener::HandleEvent(nsIDOMEvent* aEvent)
{
  if (mSpec) {
    mSpec->HandleEvent(aEvent);
  }
  return NS_OK;
}

//----------------------------------------------------------------------
// Implementation

#ifdef _MSC_VER
// Disable "warning C4355: 'this' : used in base member initializer list".
// We can ignore that warning because we know that mReferencedElement's
// constructor doesn't dereference the pointer passed to it.
#pragma warning(push)
#pragma warning(disable:4355)
#endif
nsSMILTimeValueSpec::nsSMILTimeValueSpec(nsSMILTimedElement& aOwner,
                                         bool aIsBegin)
  : mOwner(&aOwner),
    mIsBegin(aIsBegin),
    mReferencedElement(this)
#ifdef _MSC_VER
#pragma warning(pop)
#endif
{
}

nsSMILTimeValueSpec::~nsSMILTimeValueSpec()
{
  UnregisterFromReferencedElement(mReferencedElement.get());
  if (mEventListener) {
    mEventListener->Disconnect();
    mEventListener = nullptr;
  }
}

nsresult
nsSMILTimeValueSpec::SetSpec(const nsAString& aStringSpec,
                             Element* aContextNode)
{
  nsSMILTimeValueSpecParams params;
  nsresult rv =
    nsSMILParserUtils::ParseTimeValueSpecParams(aStringSpec, params);

  if (NS_FAILED(rv))
    return rv;

  mParams = params;

  // According to SMIL 3.0:
  //   The special value "indefinite" does not yield an instance time in the
  //   begin list. It will, however yield a single instance with the value
  //   "indefinite" in an end list. This value is not removed by a reset.
  if (mParams.mType == nsSMILTimeValueSpecParams::OFFSET ||
      (!mIsBegin && mParams.mType == nsSMILTimeValueSpecParams::INDEFINITE)) {
    mOwner->AddInstanceTime(new nsSMILInstanceTime(mParams.mOffset), mIsBegin);
  }

  // Fill in the event symbol to simplify handling later
  if (mParams.mType == nsSMILTimeValueSpecParams::REPEAT) {
    mParams.mEventSymbol = nsGkAtoms::repeatEvent;
  } else if (mParams.mType == nsSMILTimeValueSpecParams::ACCESSKEY) {
    mParams.mEventSymbol = nsGkAtoms::keypress;
  }

  ResolveReferences(aContextNode);

  return rv;
}

void
nsSMILTimeValueSpec::ResolveReferences(nsIContent* aContextNode)
{
  if (mParams.mType != nsSMILTimeValueSpecParams::SYNCBASE && !IsEventBased())
    return;

  NS_ABORT_IF_FALSE(aContextNode,
      "null context node for resolving timing references against");

  // If we're not bound to the document yet, don't worry, we'll get called again
  // when that happens
  if (!aContextNode->IsInDoc())
    return;

  // Hold ref to the old element so that it isn't destroyed in between resetting
  // the referenced element and using the pointer to update the referenced
  // element.
  nsRefPtr<Element> oldReferencedElement = mReferencedElement.get();

  if (mParams.mDependentElemID) {
    mReferencedElement.ResetWithID(aContextNode,
        nsDependentAtomString(mParams.mDependentElemID));
  } else if (mParams.mType == nsSMILTimeValueSpecParams::EVENT) {
    Element* target = mOwner->GetTargetElement();
    mReferencedElement.ResetWithElement(target);
  } else if (mParams.mType == nsSMILTimeValueSpecParams::ACCESSKEY) {
    nsIDocument* doc = aContextNode->GetCurrentDoc();
    NS_ABORT_IF_FALSE(doc, "We are in the document but current doc is null");
    mReferencedElement.ResetWithElement(doc->GetRootElement());
  } else {
    NS_ABORT_IF_FALSE(false, "Syncbase or repeat spec without ID");
  }
  UpdateReferencedElement(oldReferencedElement, mReferencedElement.get());
}

bool
nsSMILTimeValueSpec::IsEventBased() const
{
  return mParams.mType == nsSMILTimeValueSpecParams::EVENT ||
         mParams.mType == nsSMILTimeValueSpecParams::REPEAT ||
         mParams.mType == nsSMILTimeValueSpecParams::ACCESSKEY;
}

void
nsSMILTimeValueSpec::HandleNewInterval(nsSMILInterval& aInterval,
                                       const nsSMILTimeContainer* aSrcContainer)
{
  const nsSMILInstanceTime& baseInstance = mParams.mSyncBegin
    ? *aInterval.Begin() : *aInterval.End();
  nsSMILTimeValue newTime =
    ConvertBetweenTimeContainers(baseInstance.Time(), aSrcContainer);

  // Apply offset
  if (!ApplyOffset(newTime)) {
    NS_WARNING("New time overflows nsSMILTime, ignoring");
    return;
  }

  // Create the instance time and register it with the interval
  nsRefPtr<nsSMILInstanceTime> newInstance =
    new nsSMILInstanceTime(newTime, nsSMILInstanceTime::SOURCE_SYNCBASE, this,
                           &aInterval);
  mOwner->AddInstanceTime(newInstance, mIsBegin);
}

void
nsSMILTimeValueSpec::HandleTargetElementChange(Element* aNewTarget)
{
  if (!IsEventBased() || mParams.mDependentElemID)
    return;

  mReferencedElement.ResetWithElement(aNewTarget);
}

void
nsSMILTimeValueSpec::HandleChangedInstanceTime(
    const nsSMILInstanceTime& aBaseTime,
    const nsSMILTimeContainer* aSrcContainer,
    nsSMILInstanceTime& aInstanceTimeToUpdate,
    bool aObjectChanged)
{
  // If the instance time is fixed (e.g. because it's being used as the begin
  // time of an active or postactive interval) we just ignore the change.
  if (aInstanceTimeToUpdate.IsFixedTime())
    return;

  nsSMILTimeValue updatedTime =
    ConvertBetweenTimeContainers(aBaseTime.Time(), aSrcContainer);

  // Apply offset
  if (!ApplyOffset(updatedTime)) {
    NS_WARNING("Updated time overflows nsSMILTime, ignoring");
    return;
  }

  // The timed element that owns the instance time does the updating so it can
  // re-sort its array of instance times more efficiently
  if (aInstanceTimeToUpdate.Time() != updatedTime || aObjectChanged) {
    mOwner->UpdateInstanceTime(&aInstanceTimeToUpdate, updatedTime, mIsBegin);
  }
}

void
nsSMILTimeValueSpec::HandleDeletedInstanceTime(
    nsSMILInstanceTime &aInstanceTime)
{
  mOwner->RemoveInstanceTime(&aInstanceTime, mIsBegin);
}

bool
nsSMILTimeValueSpec::DependsOnBegin() const
{
  return mParams.mSyncBegin;
}

void
nsSMILTimeValueSpec::Traverse(nsCycleCollectionTraversalCallback* aCallback)
{
  mReferencedElement.Traverse(aCallback);
}

void
nsSMILTimeValueSpec::Unlink()
{
  UnregisterFromReferencedElement(mReferencedElement.get());
  mReferencedElement.Unlink();
}

//----------------------------------------------------------------------
// Implementation helpers

void
nsSMILTimeValueSpec::UpdateReferencedElement(Element* aFrom, Element* aTo)
{
  if (aFrom == aTo)
    return;

  UnregisterFromReferencedElement(aFrom);

  switch (mParams.mType)
  {
  case nsSMILTimeValueSpecParams::SYNCBASE:
    {
      nsSMILTimedElement* to = GetTimedElement(aTo);
      if (to) {
        to->AddDependent(*this);
      }
    }
    break;

  case nsSMILTimeValueSpecParams::EVENT:
  case nsSMILTimeValueSpecParams::REPEAT:
  case nsSMILTimeValueSpecParams::ACCESSKEY:
    RegisterEventListener(aTo);
    break;

  default:
    // not a referencing-type
    break;
  }
}

void
nsSMILTimeValueSpec::UnregisterFromReferencedElement(Element* aElement)
{
  if (!aElement)
    return;

  if (mParams.mType == nsSMILTimeValueSpecParams::SYNCBASE) {
    nsSMILTimedElement* timedElement = GetTimedElement(aElement);
    if (timedElement) {
      timedElement->RemoveDependent(*this);
    }
    mOwner->RemoveInstanceTimesForCreator(this, mIsBegin);
  } else if (IsEventBased()) {
    UnregisterEventListener(aElement);
  }
}

nsSMILTimedElement*
nsSMILTimeValueSpec::GetTimedElement(Element* aElement)
{
  if (!aElement)
    return nullptr;

  nsCOMPtr<nsISMILAnimationElement> animElement = do_QueryInterface(aElement);
  if (!animElement)
    return nullptr;

  return &animElement->TimedElement();
}

// Indicates whether we're allowed to register an event-listener
// when scripting is disabled.
bool
nsSMILTimeValueSpec::IsWhitelistedEvent()
{
  // The category of (SMIL-specific) "repeat(n)" events are allowed.
  if (mParams.mType == nsSMILTimeValueSpecParams::REPEAT) {
    return true;
  }

  // A specific list of other SMIL-related events are allowed, too.
  if (mParams.mType == nsSMILTimeValueSpecParams::EVENT &&
      (mParams.mEventSymbol == nsGkAtoms::repeat ||
       mParams.mEventSymbol == nsGkAtoms::repeatEvent ||
       mParams.mEventSymbol == nsGkAtoms::beginEvent ||
       mParams.mEventSymbol == nsGkAtoms::endEvent)) {
    return true;
  }

  return false;
}

void
nsSMILTimeValueSpec::RegisterEventListener(Element* aTarget)
{
  NS_ABORT_IF_FALSE(IsEventBased(),
    "Attempting to register event-listener for unexpected nsSMILTimeValueSpec"
    " type");
  NS_ABORT_IF_FALSE(mParams.mEventSymbol,
    "Attempting to register event-listener but there is no event name");

  if (!aTarget)
    return;

  // When script is disabled, only allow registration for whitelisted events.
  if (!aTarget->GetOwnerDocument()->IsScriptEnabled() &&
      !IsWhitelistedEvent()) {
    return;
  }

  if (!mEventListener) {
    mEventListener = new EventListener(this);
  }

  nsEventListenerManager* elm = GetEventListenerManager(aTarget);
  if (!elm)
    return;

  elm->AddEventListenerByType(mEventListener,
                              nsDependentAtomString(mParams.mEventSymbol),
                              AllEventsAtSystemGroupBubble());
}

void
nsSMILTimeValueSpec::UnregisterEventListener(Element* aTarget)
{
  if (!aTarget || !mEventListener)
    return;

  nsEventListenerManager* elm = GetEventListenerManager(aTarget);
  if (!elm)
    return;

  elm->RemoveEventListenerByType(mEventListener,
                                 nsDependentAtomString(mParams.mEventSymbol),
                                 AllEventsAtSystemGroupBubble());
}

nsEventListenerManager*
nsSMILTimeValueSpec::GetEventListenerManager(Element* aTarget)
{
  NS_ABORT_IF_FALSE(aTarget, "null target; can't get EventListenerManager");

  nsCOMPtr<nsIDOMEventTarget> target;

  if (mParams.mType == nsSMILTimeValueSpecParams::ACCESSKEY) {
    nsIDocument* doc = aTarget->GetCurrentDoc();
    if (!doc)
      return nullptr;
    nsPIDOMWindow* win = doc->GetWindow();
    if (!win)
      return nullptr;
    target = do_QueryInterface(win);
  } else {
    target = aTarget;
  }
  if (!target)
    return nullptr;

  return target->GetListenerManager(true);
}

void
nsSMILTimeValueSpec::HandleEvent(nsIDOMEvent* aEvent)
{
  NS_ABORT_IF_FALSE(mEventListener, "Got event without an event listener");
  NS_ABORT_IF_FALSE(IsEventBased(),
                    "Got event for non-event nsSMILTimeValueSpec");
  NS_ABORT_IF_FALSE(aEvent, "No event supplied");

  // XXX In the long run we should get the time from the event itself which will
  // store the time in global document time which we'll need to convert to our
  // time container
  nsSMILTimeContainer* container = mOwner->GetTimeContainer();
  if (!container)
    return;

  if (!CheckEventDetail(aEvent))
    return;

  nsSMILTime currentTime = container->GetCurrentTime();
  nsSMILTimeValue newTime(currentTime);
  if (!ApplyOffset(newTime)) {
    NS_WARNING("New time generated from event overflows nsSMILTime, ignoring");
    return;
  }

  nsRefPtr<nsSMILInstanceTime> newInstance =
    new nsSMILInstanceTime(newTime, nsSMILInstanceTime::SOURCE_EVENT);
  mOwner->AddInstanceTime(newInstance, mIsBegin);
}

bool
nsSMILTimeValueSpec::CheckEventDetail(nsIDOMEvent *aEvent)
{
  switch (mParams.mType)
  {
  case nsSMILTimeValueSpecParams::REPEAT:
    return CheckRepeatEventDetail(aEvent);

  case nsSMILTimeValueSpecParams::ACCESSKEY:
    return CheckAccessKeyEventDetail(aEvent);

  default:
    // nothing to check
    return true;
  }
}

bool
nsSMILTimeValueSpec::CheckRepeatEventDetail(nsIDOMEvent *aEvent)
{
  nsCOMPtr<nsIDOMTimeEvent> timeEvent = do_QueryInterface(aEvent);
  if (!timeEvent) {
    NS_WARNING("Received a repeat event that was not a DOMTimeEvent");
    return false;
  }

  int32_t detail;
  timeEvent->GetDetail(&detail);
  return detail > 0 && (uint32_t)detail == mParams.mRepeatIterationOrAccessKey;
}

bool
nsSMILTimeValueSpec::CheckAccessKeyEventDetail(nsIDOMEvent *aEvent)
{
  nsCOMPtr<nsIDOMKeyEvent> keyEvent = do_QueryInterface(aEvent);
  if (!keyEvent) {
    NS_WARNING("Received an accesskey event that was not a DOMKeyEvent");
    return false;
  }

  // Ignore the key event if any modifier keys are pressed UNLESS we're matching
  // on the charCode in which case we ignore the state of the shift and alt keys
  // since they might be needed to generate the character in question.
  bool isCtrl;
  bool isMeta;
  keyEvent->GetCtrlKey(&isCtrl);
  keyEvent->GetMetaKey(&isMeta);
  if (isCtrl || isMeta)
    return false;

  uint32_t code;
  keyEvent->GetCharCode(&code);
  if (code)
    return code == mParams.mRepeatIterationOrAccessKey;

  // Only match on the keyCode if it corresponds to some ASCII character that
  // does not produce a charCode.
  // In this case we can safely bail out if either alt or shift is pressed since
  // they won't already be incorporated into the keyCode unlike the charCode.
  bool isAlt;
  bool isShift;
  keyEvent->GetAltKey(&isAlt);
  keyEvent->GetShiftKey(&isShift);
  if (isAlt || isShift)
    return false;

  keyEvent->GetKeyCode(&code);
  switch (code)
  {
  case nsIDOMKeyEvent::DOM_VK_BACK_SPACE:
    return mParams.mRepeatIterationOrAccessKey == 0x08;

  case nsIDOMKeyEvent::DOM_VK_RETURN:
  case nsIDOMKeyEvent::DOM_VK_ENTER:
    return mParams.mRepeatIterationOrAccessKey == 0x0A ||
           mParams.mRepeatIterationOrAccessKey == 0x0D;

  case nsIDOMKeyEvent::DOM_VK_ESCAPE:
    return mParams.mRepeatIterationOrAccessKey == 0x1B;

  case nsIDOMKeyEvent::DOM_VK_DELETE:
    return mParams.mRepeatIterationOrAccessKey == 0x7F;

  default:
    return false;
  }
}

nsSMILTimeValue
nsSMILTimeValueSpec::ConvertBetweenTimeContainers(
    const nsSMILTimeValue& aSrcTime,
    const nsSMILTimeContainer* aSrcContainer)
{
  // If the source time is either indefinite or unresolved the result is going
  // to be the same
  if (!aSrcTime.IsDefinite())
    return aSrcTime;

  // Convert from source time container to our parent time container
  const nsSMILTimeContainer* dstContainer = mOwner->GetTimeContainer();
  if (dstContainer == aSrcContainer)
    return aSrcTime;

  // If one of the elements is not attached to a time container then we can't do
  // any meaningful conversion
  if (!aSrcContainer || !dstContainer)
    return nsSMILTimeValue(); // unresolved

  nsSMILTimeValue docTime =
    aSrcContainer->ContainerToParentTime(aSrcTime.GetMillis());

  if (docTime.IsIndefinite())
    // This will happen if the source container is paused and we have a future
    // time. Just return the indefinite time.
    return docTime;

  NS_ABORT_IF_FALSE(docTime.IsDefinite(),
    "ContainerToParentTime gave us an unresolved or indefinite time");

  return dstContainer->ParentToContainerTime(docTime.GetMillis());
}

bool
nsSMILTimeValueSpec::ApplyOffset(nsSMILTimeValue& aTime) const
{
  // indefinite + offset = indefinite. Likewise for unresolved times.
  if (!aTime.IsDefinite()) {
    return true;
  }

  double resultAsDouble =
    (double)aTime.GetMillis() + mParams.mOffset.GetMillis();
  if (resultAsDouble > std::numeric_limits<nsSMILTime>::max() ||
      resultAsDouble < std::numeric_limits<nsSMILTime>::min()) {
    return false;
  }
  aTime.SetMillis(aTime.GetMillis() + mParams.mOffset.GetMillis());
  return true;
}
