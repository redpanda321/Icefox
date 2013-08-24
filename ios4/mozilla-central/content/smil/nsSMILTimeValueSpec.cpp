/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Mozilla SMIL module.
 *
 * The Initial Developer of the Original Code is Brian Birtles.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Brian Birtles <birtles@gmail.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsSMILTimeValueSpec.h"
#include "nsSMILInterval.h"
#include "nsSMILTimeContainer.h"
#include "nsSMILTimeValue.h"
#include "nsSMILTimedElement.h"
#include "nsSMILInstanceTime.h"
#include "nsSMILParserUtils.h"
#include "nsISMILAnimationElement.h"
#include "nsContentUtils.h"
#include "nsIEventListenerManager.h"
#include "nsIDOMEventGroup.h"
#include "nsGUIEvent.h"
#include "nsIDOMTimeEvent.h"
#include "nsString.h"

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
                                         PRBool aIsBegin)
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
    mEventListener = nsnull;
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
    NS_ABORT_IF_FALSE(PR_FALSE, "Syncbase or repeat spec without ID");
  }
  UpdateReferencedElement(oldReferencedElement, mReferencedElement.get());
}

PRBool
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
  if (newTime.IsResolved()) {
    newTime.SetMillis(newTime.GetMillis() + mParams.mOffset.GetMillis());
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
    PRBool aObjectChanged)
{
  // If the instance time is fixed (e.g. because it's being used as the begin
  // time of an active or postactive interval) we just ignore the change.
  if (aInstanceTimeToUpdate.IsFixedTime())
    return;

  nsSMILTimeValue updatedTime =
    ConvertBetweenTimeContainers(aBaseTime.Time(), aSrcContainer);

  // Apply offset
  if (updatedTime.IsResolved()) {
    updatedTime.SetMillis(updatedTime.GetMillis() +
                          mParams.mOffset.GetMillis());
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

PRBool
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
    return nsnull;

  nsCOMPtr<nsISMILAnimationElement> animElement = do_QueryInterface(aElement);
  if (!animElement)
    return nsnull;

  return &animElement->TimedElement();
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

  if (!mEventListener) {
    mEventListener = new EventListener(this);
  }

  nsCOMPtr<nsIDOMEventGroup> sysGroup;
  nsIEventListenerManager* elm =
    GetEventListenerManager(aTarget, getter_AddRefs(sysGroup));
  if (!elm)
    return;
  
  elm->AddEventListenerByType(mEventListener,
                              nsDependentAtomString(mParams.mEventSymbol),
                              NS_EVENT_FLAG_BUBBLE |
                              NS_PRIV_EVENT_UNTRUSTED_PERMITTED,
                              sysGroup);
}

void
nsSMILTimeValueSpec::UnregisterEventListener(Element* aTarget)
{
  if (!aTarget || !mEventListener)
    return;

  nsCOMPtr<nsIDOMEventGroup> sysGroup;
  nsIEventListenerManager* elm =
    GetEventListenerManager(aTarget, getter_AddRefs(sysGroup));
  if (!elm)
    return;

  elm->RemoveEventListenerByType(mEventListener,
                                 nsDependentAtomString(mParams.mEventSymbol),
                                 NS_EVENT_FLAG_BUBBLE |
                                 NS_PRIV_EVENT_UNTRUSTED_PERMITTED,
                                 sysGroup);
}

nsIEventListenerManager*
nsSMILTimeValueSpec::GetEventListenerManager(Element* aTarget,
                                             nsIDOMEventGroup** aSystemGroup)
{
  NS_ABORT_IF_FALSE(aTarget, "null target; can't get EventListenerManager");
  NS_ABORT_IF_FALSE(aSystemGroup && !*aSystemGroup,
      "Bad out param for system group");

  nsCOMPtr<nsPIDOMEventTarget> piTarget;

  if (mParams.mType == nsSMILTimeValueSpecParams::ACCESSKEY) {
    nsIDocument* doc = aTarget->GetCurrentDoc();
    if (!doc)
      return nsnull;
    nsPIDOMWindow* win = doc->GetWindow();
    if (!win)
      return nsnull;
    piTarget = do_QueryInterface(win);
  } else {
    piTarget = aTarget;
  }
  if (!piTarget)
    return nsnull;

  nsIEventListenerManager* elm = piTarget->GetListenerManager(PR_TRUE);
  if (!elm)
    return nsnull;

  aTarget->GetSystemEventGroup(aSystemGroup);
  if (!*aSystemGroup)
    return nsnull;

  return elm;
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
  nsSMILTimeValue newTime(currentTime + mParams.mOffset.GetMillis());

  nsRefPtr<nsSMILInstanceTime> newInstance =
    new nsSMILInstanceTime(newTime, nsSMILInstanceTime::SOURCE_EVENT);
  mOwner->AddInstanceTime(newInstance, mIsBegin);
}

PRBool
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
    return PR_TRUE;
  }
}

PRBool
nsSMILTimeValueSpec::CheckRepeatEventDetail(nsIDOMEvent *aEvent)
{
  nsCOMPtr<nsIDOMTimeEvent> timeEvent = do_QueryInterface(aEvent);
  if (!timeEvent) {
    NS_WARNING("Received a repeat event that was not a DOMTimeEvent");
    return PR_FALSE;
  }

  PRInt32 detail;
  timeEvent->GetDetail(&detail);
  return detail > 0 && (PRUint32)detail == mParams.mRepeatIterationOrAccessKey;
}

PRBool
nsSMILTimeValueSpec::CheckAccessKeyEventDetail(nsIDOMEvent *aEvent)
{
  nsCOMPtr<nsIDOMKeyEvent> keyEvent = do_QueryInterface(aEvent);
  if (!keyEvent) {
    NS_WARNING("Received an accesskey event that was not a DOMKeyEvent");
    return PR_FALSE;
  }

  // Ignore the key event if any modifier keys are pressed UNLESS we're matching
  // on the charCode in which case we ignore the state of the shift and alt keys
  // since they might be needed to generate the character in question.
  PRBool isCtrl;
  PRBool isMeta;
  keyEvent->GetCtrlKey(&isCtrl);
  keyEvent->GetMetaKey(&isMeta);
  if (isCtrl || isMeta)
    return PR_FALSE;

  PRUint32 code;
  keyEvent->GetCharCode(&code);
  if (code)
    return code == mParams.mRepeatIterationOrAccessKey;

  // Only match on the keyCode if it corresponds to some ASCII character that
  // does not produce a charCode.
  // In this case we can safely bail out if either alt or shift is pressed since
  // they won't already be incorporated into the keyCode unlike the charCode.
  PRBool isAlt;
  PRBool isShift;
  keyEvent->GetAltKey(&isAlt);
  keyEvent->GetShiftKey(&isShift);
  if (isAlt || isShift)
    return PR_FALSE;

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
    return PR_FALSE;
  }
}

nsSMILTimeValue
nsSMILTimeValueSpec::ConvertBetweenTimeContainers(
    const nsSMILTimeValue& aSrcTime,
    const nsSMILTimeContainer* aSrcContainer)
{
  // If the source time is either indefinite or unresolved the result is going
  // to be the same
  if (!aSrcTime.IsResolved())
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

   NS_ABORT_IF_FALSE(docTime.IsResolved(),
       "ContainerToParentTime gave us an unresolved time");

  return dstContainer->ParentToContainerTime(docTime.GetMillis());
}
