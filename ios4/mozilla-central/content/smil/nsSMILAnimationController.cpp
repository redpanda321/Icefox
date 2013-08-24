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
 *   Daniel Holbert <dholbert@mozilla.com>
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

#include "nsSMILAnimationController.h"
#include "nsSMILCompositor.h"
#include "nsSMILCSSProperty.h"
#include "nsCSSProps.h"
#include "nsComponentManagerUtils.h"
#include "nsITimer.h"
#include "nsIContent.h"
#include "mozilla/dom/Element.h"
#include "nsIDocument.h"
#include "nsISMILAnimationElement.h"
#include "nsIDOMSVGAnimationElement.h"
#include "nsSMILTimedElement.h"

using namespace mozilla::dom;

//----------------------------------------------------------------------
// nsSMILAnimationController implementation

// Helper method
static nsRefreshDriver*
GetRefreshDriverForDoc(nsIDocument* aDoc)
{
  nsIPresShell* shell = aDoc->GetShell();
  if (!shell) {
    return nsnull;
  }

  nsPresContext* context = shell->GetPresContext();
  return context ? context->RefreshDriver() : nsnull;
}


//----------------------------------------------------------------------
// ctors, dtors, factory methods

nsSMILAnimationController::nsSMILAnimationController()
  : mResampleNeeded(PR_FALSE),
    mDeferredStartSampling(PR_FALSE),
    mDocument(nsnull)
{
  mAnimationElementTable.Init();
  mChildContainerTable.Init();
}

nsSMILAnimationController::~nsSMILAnimationController()
{
  StopSampling(GetRefreshDriverForDoc(mDocument));
  NS_ASSERTION(mAnimationElementTable.Count() == 0,
               "Animation controller shouldn't be tracking any animation"
               " elements when it dies");
}

nsSMILAnimationController* NS_NewSMILAnimationController(nsIDocument* aDoc)
{
  nsSMILAnimationController* animationController =
    new nsSMILAnimationController();
  NS_ENSURE_TRUE(animationController, nsnull);

  nsresult rv = animationController->Init(aDoc);
  if (NS_FAILED(rv)) {
    delete animationController;
    animationController = nsnull;
  }

  return animationController;
}

nsresult
nsSMILAnimationController::Init(nsIDocument* aDoc)
{
  NS_ENSURE_ARG_POINTER(aDoc);

  // Keep track of document, so we can traverse its set of animation elements
  mDocument = aDoc;

  Begin();

  return NS_OK;
}

//----------------------------------------------------------------------
// nsSMILTimeContainer methods:

void
nsSMILAnimationController::Pause(PRUint32 aType)
{
  nsSMILTimeContainer::Pause(aType);

  if (mPauseState) {
    StopSampling(GetRefreshDriverForDoc(mDocument));
  }
}

void
nsSMILAnimationController::Resume(PRUint32 aType)
{
  PRBool wasPaused = (mPauseState != 0);

  nsSMILTimeContainer::Resume(aType);

  if (wasPaused && !mPauseState && mChildContainerTable.Count()) {
    Sample(); // Run the first sample manually
    if (mAnimationElementTable.Count()) {
      StartSampling(GetRefreshDriverForDoc(mDocument));
    } else {
      mDeferredStartSampling = PR_TRUE;
    }
  }
}

nsSMILTime
nsSMILAnimationController::GetParentTime() const
{
  // Our parent time is wallclock time
  return PR_Now() / PR_USEC_PER_MSEC;
}

//----------------------------------------------------------------------
// nsARefreshObserver methods:
NS_IMPL_ADDREF(nsSMILAnimationController)
NS_IMPL_RELEASE(nsSMILAnimationController)

// nsRefreshDriver Callback function
void
nsSMILAnimationController::WillRefresh(mozilla::TimeStamp aTime)
{
  // XXXdholbert Eventually we should be sampling based on aTime. For now,
  // though, we keep track of the time on our own, and we just use
  // nsRefreshDriver for scheduling samples.
  Sample();
}

//----------------------------------------------------------------------
// Animation element registration methods:

void
nsSMILAnimationController::RegisterAnimationElement(
                                  nsISMILAnimationElement* aAnimationElement)
{
  mAnimationElementTable.PutEntry(aAnimationElement);
  if (mDeferredStartSampling) {
    // mAnimationElementTable was empty until we just inserted its first element
    NS_ABORT_IF_FALSE(mAnimationElementTable.Count() == 1,
                      "we shouldn't have deferred sampling if we already had "
                      "animations registered");
    mDeferredStartSampling = PR_FALSE;
    StartSampling(GetRefreshDriverForDoc(mDocument));
  }
}

void
nsSMILAnimationController::UnregisterAnimationElement(
                                  nsISMILAnimationElement* aAnimationElement)
{
  mAnimationElementTable.RemoveEntry(aAnimationElement);
}

//----------------------------------------------------------------------
// Page show/hide

void
nsSMILAnimationController::OnPageShow()
{
  Resume(nsSMILTimeContainer::PAUSE_PAGEHIDE);
}

void
nsSMILAnimationController::OnPageHide()
{
  Pause(nsSMILTimeContainer::PAUSE_PAGEHIDE);
}

//----------------------------------------------------------------------
// Cycle-collection support

void
nsSMILAnimationController::Traverse(
    nsCycleCollectionTraversalCallback* aCallback)
{
  // Traverse last compositor table
  if (mLastCompositorTable) {
    mLastCompositorTable->EnumerateEntries(CompositorTableEntryTraverse,
                                           aCallback);
  }
}

/*static*/ PR_CALLBACK PLDHashOperator
nsSMILAnimationController::CompositorTableEntryTraverse(
                                      nsSMILCompositor* aCompositor,
                                      void* aArg)
{
  nsCycleCollectionTraversalCallback* cb =
    static_cast<nsCycleCollectionTraversalCallback*>(aArg);
  aCompositor->Traverse(cb);
  return PL_DHASH_NEXT;
}

void
nsSMILAnimationController::Unlink()
{
  mLastCompositorTable = nsnull;
}

//----------------------------------------------------------------------
// Timer-related implementation helpers

void
nsSMILAnimationController::StartSampling(nsRefreshDriver* aRefreshDriver)
{
  NS_ASSERTION(mPauseState == 0, "Starting sampling but controller is paused");
  if (aRefreshDriver) {
    NS_ABORT_IF_FALSE(!GetRefreshDriverForDoc(mDocument) ||
                      aRefreshDriver == GetRefreshDriverForDoc(mDocument),
                      "Starting sampling with wrong refresh driver");
    aRefreshDriver->AddRefreshObserver(this, Flush_Style);
  }
}

void
nsSMILAnimationController::StopSampling(nsRefreshDriver* aRefreshDriver)
{
  if (aRefreshDriver) {
    // NOTE: The document might already have been detached from its PresContext
    // (and RefreshDriver), which would make GetRefreshDriverForDoc return null.
    NS_ABORT_IF_FALSE(!GetRefreshDriverForDoc(mDocument) ||
                      aRefreshDriver == GetRefreshDriverForDoc(mDocument),
                      "Stopping sampling with wrong refresh driver");
    aRefreshDriver->RemoveRefreshObserver(this, Flush_Style);
  }
}

//----------------------------------------------------------------------
// Sample-related methods and callbacks

PR_CALLBACK PLDHashOperator
TransferCachedBaseValue(nsSMILCompositor* aCompositor,
                        void* aData)
{
  nsSMILCompositorTable* lastCompositorTable =
    static_cast<nsSMILCompositorTable*>(aData);
  nsSMILCompositor* lastCompositor =
    lastCompositorTable->GetEntry(aCompositor->GetKey());

  if (lastCompositor) {
    aCompositor->StealCachedBaseValue(lastCompositor);
  }

  return PL_DHASH_NEXT;  
}

PR_CALLBACK PLDHashOperator
RemoveCompositorFromTable(nsSMILCompositor* aCompositor,
                          void* aData)
{
  nsSMILCompositorTable* lastCompositorTable =
    static_cast<nsSMILCompositorTable*>(aData);
  lastCompositorTable->RemoveEntry(aCompositor->GetKey());
  return PL_DHASH_NEXT;
}

PR_CALLBACK PLDHashOperator
DoClearAnimationEffects(nsSMILCompositor* aCompositor,
                        void* /*aData*/)
{
  aCompositor->ClearAnimationEffects();
  return PL_DHASH_NEXT;
}

PR_CALLBACK PLDHashOperator
DoComposeAttribute(nsSMILCompositor* aCompositor,
                   void* /*aData*/)
{
  aCompositor->ComposeAttribute();
  return PL_DHASH_NEXT;
}

void
nsSMILAnimationController::DoSample()
{
  DoSample(PR_TRUE); // Skip unchanged time containers
}

void
nsSMILAnimationController::DoSample(PRBool aSkipUnchangedContainers)
{
  // Reset resample flag
  mResampleNeeded = PR_FALSE;

  // STEP 1: Bring model up to date
  // (i)  Rewind elements where necessary
  // (ii) Run milestone samples
  RewindElements();
  DoMilestoneSamples();

  // STEP 2: Sample the child time containers
  //
  // When we sample the child time containers they will simply record the sample
  // time in document time.
  TimeContainerHashtable activeContainers;
  activeContainers.Init(mChildContainerTable.Count());
  SampleTimeContainerParams tcParams = { &activeContainers,
                                         aSkipUnchangedContainers };
  mChildContainerTable.EnumerateEntries(SampleTimeContainer, &tcParams);

  // STEP 3: (i)  Sample the timed elements AND
  //         (ii) Create a table of compositors
  //
  // (i) Here we sample the timed elements (fetched from the
  // nsISMILAnimationElements) which determine from the active time if the
  // element is active and what its simple time etc. is. This information is
  // then passed to its time client (nsSMILAnimationFunction).
  //
  // (ii) During the same loop we also build up a table that contains one
  // compositor for each animated attribute and which maps animated elements to
  // the corresponding compositor for their target attribute.
  //
  // Note that this compositor table needs to be allocated on the heap so we can
  // store it until the next sample. This lets us find out which elements were
  // animated in sample 'n-1' but not in sample 'n' (and hence need to have
  // their animation effects removed in sample 'n').
  //
  // Parts (i) and (ii) are not functionally related but we combine them here to
  // save iterating over the animation elements twice.

  // Create the compositor table
  nsAutoPtr<nsSMILCompositorTable>
    currentCompositorTable(new nsSMILCompositorTable());
  if (!currentCompositorTable)
    return;
  currentCompositorTable->Init(0);

  SampleAnimationParams saParams = { &activeContainers,
                                     currentCompositorTable };
  mAnimationElementTable.EnumerateEntries(SampleAnimation,
                                          &saParams);
  activeContainers.Clear();

  // STEP 4: Compare previous sample's compositors against this sample's.
  // (Transfer cached base values across, & remove animation effects from 
  // no-longer-animated targets.)
  if (mLastCompositorTable) {
    // * Transfer over cached base values, from last sample's compositors
    currentCompositorTable->EnumerateEntries(TransferCachedBaseValue,
                                             mLastCompositorTable);

    // * For each compositor in current sample's hash table, remove entry from
    // prev sample's hash table -- we don't need to clear animation
    // effects of those compositors, since they're still being animated.
    currentCompositorTable->EnumerateEntries(RemoveCompositorFromTable,
                                             mLastCompositorTable);

    // * For each entry that remains in prev sample's hash table (i.e. for
    // every target that's no longer animated), clear animation effects.
    mLastCompositorTable->EnumerateEntries(DoClearAnimationEffects, nsnull);
  }

  // STEP 5: Compose currently-animated attributes.
  // XXXdholbert: This step traverses our animation targets in an effectively
  // random order. For animation from/to 'inherit' values to work correctly
  // when the inherited value is *also* being animated, we really should be
  // traversing our animated nodes in an ancestors-first order (bug 501183)
  currentCompositorTable->EnumerateEntries(DoComposeAttribute, nsnull);

  // Update last compositor table
  mLastCompositorTable = currentCompositorTable.forget();

  NS_ASSERTION(!mResampleNeeded, "Resample dirty flag set during sample!");
}

void
nsSMILAnimationController::RewindElements()
{
  PRBool rewindNeeded = PR_FALSE;
  mChildContainerTable.EnumerateEntries(RewindNeeded, &rewindNeeded);
  if (!rewindNeeded)
    return;

  mAnimationElementTable.EnumerateEntries(RewindAnimation, nsnull);
  mChildContainerTable.EnumerateEntries(ClearRewindNeeded, nsnull);
}

/*static*/ PR_CALLBACK PLDHashOperator
nsSMILAnimationController::RewindNeeded(TimeContainerPtrKey* aKey,
                                        void* aData)
{
  NS_ABORT_IF_FALSE(aData,
      "Null data pointer during time container enumeration");
  PRBool* rewindNeeded = static_cast<PRBool*>(aData);

  nsSMILTimeContainer* container = aKey->GetKey();
  if (container->NeedsRewind()) {
    *rewindNeeded = PR_TRUE;
    return PL_DHASH_STOP;
  }

  return PL_DHASH_NEXT;
}

/*static*/ PR_CALLBACK PLDHashOperator
nsSMILAnimationController::RewindAnimation(AnimationElementPtrKey* aKey,
                                           void* aData)
{
  nsISMILAnimationElement* animElem = aKey->GetKey();
  nsSMILTimeContainer* timeContainer = animElem->GetTimeContainer();
  if (timeContainer && timeContainer->NeedsRewind()) {
    animElem->TimedElement().Rewind();
  }

  return PL_DHASH_NEXT;
}

/*static*/ PR_CALLBACK PLDHashOperator
nsSMILAnimationController::ClearRewindNeeded(TimeContainerPtrKey* aKey,
                                             void* aData)
{
  aKey->GetKey()->ClearNeedsRewind();
  return PL_DHASH_NEXT;
}

void
nsSMILAnimationController::DoMilestoneSamples()
{
  // We need to sample the timing model but because SMIL operates independently
  // of the frame-rate, we can get one sample at t=0s and the next at t=10min.
  //
  // In between those two sample times a whole string of significant events
  // might be expected to take place: events firing, new interdependencies
  // between animations resolved and dissolved, etc.
  //
  // Furthermore, at any given time, we want to sample all the intervals that
  // end at that time BEFORE any that begin. This behaviour is implied by SMIL's
  // endpoint-exclusive timing model.
  //
  // So we have the animations (specifically the timed elements) register the
  // next significant moment (called a milestone) in their lifetime and then we
  // step through the model at each of these moments and sample those animations
  // registered for those times. This way events can fire in the correct order,
  // dependencies can be resolved etc.

  nsSMILTime sampleTime = LL_MININT;

  while (PR_TRUE) {
    // We want to find any milestones AT OR BEFORE the current sample time so we
    // initialise the next milestone to the moment after (1ms after, to be
    // precise) the current sample time and see if there are any milestones
    // before that. Any other milestones will be dealt with in a subsequent
    // sample.
    nsSMILMilestone nextMilestone(GetCurrentTime() + 1, PR_TRUE);
    mChildContainerTable.EnumerateEntries(GetNextMilestone, &nextMilestone);

    if (nextMilestone.mTime > GetCurrentTime()) {
      break;
    }

    GetMilestoneElementsParams params;
    params.mMilestone = nextMilestone;
    mChildContainerTable.EnumerateEntries(GetMilestoneElements, &params);
    PRUint32 length = params.mElements.Length();

    // During the course of a sampling we don't want to actually go backwards.
    // Due to negative offsets, early ends and the like, a timed element might
    // register a milestone that is actually in the past. That's fine, but it's
    // still only going to get *sampled* with whatever time we're up to and no
    // earlier.
    //
    // Because we're only performing this clamping at the last moment, the
    // animations will still all get sampled in the correct order and
    // dependencies will be appropriately resolved.
    sampleTime = PR_MAX(nextMilestone.mTime, sampleTime);

    for (PRUint32 i = 0; i < length; ++i) {
      nsISMILAnimationElement* elem = params.mElements[i].get();
      NS_ABORT_IF_FALSE(elem, "NULL animation element in list");
      nsSMILTimeContainer* container = elem->GetTimeContainer();
      if (!container)
        // The container may be nsnull if the element has been detached from its
        // parent since registering a milestone.
        continue;

      nsSMILTimeValue containerTimeValue =
        container->ParentToContainerTime(sampleTime);
      if (!containerTimeValue.IsResolved())
        continue;

      // Clamp the converted container time to non-negative values.
      nsSMILTime containerTime = PR_MAX(0, containerTimeValue.GetMillis());

      if (nextMilestone.mIsEnd) {
        elem->TimedElement().SampleEndAt(containerTime);
      } else {
        elem->TimedElement().SampleAt(containerTime);
      }
    }
  }
}

/*static*/ PR_CALLBACK PLDHashOperator
nsSMILAnimationController::GetNextMilestone(TimeContainerPtrKey* aKey,
                                            void* aData)
{
  NS_ABORT_IF_FALSE(aKey, "Null hash key for time container hash table");
  NS_ABORT_IF_FALSE(aKey->GetKey(), "Null time container key in hash table");
  NS_ABORT_IF_FALSE(aData,
      "Null data pointer during time container enumeration");

  nsSMILMilestone* nextMilestone = static_cast<nsSMILMilestone*>(aData);

  nsSMILTimeContainer* container = aKey->GetKey();
  if (container->IsPausedByType(nsSMILTimeContainer::PAUSE_BEGIN))
    return PL_DHASH_NEXT;

  nsSMILMilestone thisMilestone;
  PRBool didGetMilestone =
    container->GetNextMilestoneInParentTime(thisMilestone);
  if (didGetMilestone && thisMilestone < *nextMilestone) {
    *nextMilestone = thisMilestone;
  }

  return PL_DHASH_NEXT;
}

/*static*/ PR_CALLBACK PLDHashOperator
nsSMILAnimationController::GetMilestoneElements(TimeContainerPtrKey* aKey,
                                                void* aData)
{
  NS_ABORT_IF_FALSE(aKey, "Null hash key for time container hash table");
  NS_ABORT_IF_FALSE(aKey->GetKey(), "Null time container key in hash table");
  NS_ABORT_IF_FALSE(aData,
      "Null data pointer during time container enumeration");

  GetMilestoneElementsParams* params =
    static_cast<GetMilestoneElementsParams*>(aData);

  nsSMILTimeContainer* container = aKey->GetKey();
  if (container->IsPausedByType(nsSMILTimeContainer::PAUSE_BEGIN))
    return PL_DHASH_NEXT;

  container->PopMilestoneElementsAtMilestone(params->mMilestone,
                                             params->mElements);

  return PL_DHASH_NEXT;
}

/*static*/ PR_CALLBACK PLDHashOperator
nsSMILAnimationController::SampleTimeContainer(TimeContainerPtrKey* aKey,
                                               void* aData)
{
  NS_ENSURE_TRUE(aKey, PL_DHASH_NEXT);
  NS_ENSURE_TRUE(aKey->GetKey(), PL_DHASH_NEXT);
  NS_ENSURE_TRUE(aData, PL_DHASH_NEXT);

  SampleTimeContainerParams* params =
    static_cast<SampleTimeContainerParams*>(aData);

  nsSMILTimeContainer* container = aKey->GetKey();
  if (!container->IsPausedByType(nsSMILTimeContainer::PAUSE_BEGIN) &&
      (container->NeedsSample() || !params->mSkipUnchangedContainers)) {
    container->ClearMilestones();
    container->Sample();
    container->MarkSeekFinished();
    params->mActiveContainers->PutEntry(container);
  }

  return PL_DHASH_NEXT;
}

/*static*/ PR_CALLBACK PLDHashOperator
nsSMILAnimationController::SampleAnimation(AnimationElementPtrKey* aKey,
                                           void* aData)
{
  NS_ENSURE_TRUE(aKey, PL_DHASH_NEXT);
  NS_ENSURE_TRUE(aKey->GetKey(), PL_DHASH_NEXT);
  NS_ENSURE_TRUE(aData, PL_DHASH_NEXT);

  nsISMILAnimationElement* animElem = aKey->GetKey();
  SampleAnimationParams* params = static_cast<SampleAnimationParams*>(aData);

  SampleTimedElement(animElem, params->mActiveContainers);
  AddAnimationToCompositorTable(animElem, params->mCompositorTable);

  return PL_DHASH_NEXT;
}

/*static*/ void
nsSMILAnimationController::SampleTimedElement(
  nsISMILAnimationElement* aElement, TimeContainerHashtable* aActiveContainers)
{
  nsSMILTimeContainer* timeContainer = aElement->GetTimeContainer();
  if (!timeContainer)
    return;

  // We'd like to call timeContainer->NeedsSample() here and skip all timed
  // elements that belong to paused time containers that don't need a sample,
  // but that doesn't work because we've already called Sample() on all the time
  // containers so the paused ones don't need a sample any more and they'll
  // return false.
  //
  // Instead we build up a hashmap of active time containers during the previous
  // step (SampleTimeContainer) and then test here if the container for this
  // timed element is in the list.
  if (!aActiveContainers->GetEntry(timeContainer))
    return;

  nsSMILTime containerTime = timeContainer->GetCurrentTime();

  NS_ABORT_IF_FALSE(!timeContainer->IsSeeking(),
      "Doing a regular sample but the time container is still seeking");
  aElement->TimedElement().SampleAt(containerTime);
}

/*static*/ void
nsSMILAnimationController::AddAnimationToCompositorTable(
  nsISMILAnimationElement* aElement, nsSMILCompositorTable* aCompositorTable)
{
  // Add a compositor to the hash table if there's not already one there
  nsSMILTargetIdentifier key;
  if (!GetTargetIdentifierForAnimation(aElement, key))
    // Something's wrong/missing about animation's target; skip this animation
    return;

  nsSMILAnimationFunction& func = aElement->AnimationFunction();

  // Only add active animation functions. If there are no active animations
  // targeting an attribute, no compositor will be created and any previously
  // applied animations will be cleared.
  if (func.IsActiveOrFrozen()) {
    // Look up the compositor for our target, & add our animation function
    // to its list of animation functions.
    nsSMILCompositor* result = aCompositorTable->PutEntry(key);
    result->AddAnimationFunction(&func);

  } else if (func.HasChanged()) {
    // Look up the compositor for our target, and force it to skip the
    // "nothing's changed so don't bother compositing" optimization for this
    // sample. |func| is inactive, but it's probably *newly* inactive (since
    // it's got HasChanged() == PR_TRUE), so we need to make sure to recompose
    // its target.
    nsSMILCompositor* result = aCompositorTable->PutEntry(key);
    result->ToggleForceCompositing();

    // We've now made sure that |func|'s inactivity will be reflected as of
    // this sample. We need to clear its HasChanged() flag so that it won't
    // trigger this same clause in future samples (until it changes again).
    func.ClearHasChanged();
  }
}

// Helper function that, given a nsISMILAnimationElement, looks up its target
// element & target attribute and populates a nsSMILTargetIdentifier
// for this target.
/*static*/ PRBool
nsSMILAnimationController::GetTargetIdentifierForAnimation(
    nsISMILAnimationElement* aAnimElem, nsSMILTargetIdentifier& aResult)
{
  // Look up target (animated) element
  Element* targetElem = aAnimElem->GetTargetElementContent();
  if (!targetElem)
    // Animation has no target elem -- skip it.
    return PR_FALSE;

  // Look up target (animated) attribute
  //
  // XXXdholbert As mentioned in SMILANIM section 3.1, attributeName may
  // have an XMLNS prefix to indicate the XML namespace. Need to parse
  // that somewhere.
  nsIAtom* attributeName = aAnimElem->GetTargetAttributeName();
  if (!attributeName)
    // Animation has no target attr -- skip it.
    return PR_FALSE;

  // Look up target (animated) attribute-type
  nsSMILTargetAttrType attributeType = aAnimElem->GetTargetAttributeType();

  // Check if an 'auto' attributeType refers to a CSS property or XML attribute.
  // Note that SMIL requires we search for CSS properties first. So if they
  // overlap, 'auto' = 'CSS'. (SMILANIM 3.1)
  PRBool isCSS;
  if (attributeType == eSMILTargetAttrType_auto) {
    nsCSSProperty prop =
      nsCSSProps::LookupProperty(nsDependentAtomString(attributeName));
    isCSS = nsSMILCSSProperty::IsPropertyAnimatable(prop);
  } else {
    isCSS = (attributeType == eSMILTargetAttrType_CSS);
  }

  // Construct the key
  aResult.mElement = targetElem;
  aResult.mAttributeName = attributeName;
  aResult.mIsCSS = isCSS;

  return PR_TRUE;
}

//----------------------------------------------------------------------
// Add/remove child time containers

nsresult
nsSMILAnimationController::AddChild(nsSMILTimeContainer& aChild)
{
  TimeContainerPtrKey* key = mChildContainerTable.PutEntry(&aChild);
  NS_ENSURE_TRUE(key,NS_ERROR_OUT_OF_MEMORY);

  if (!mPauseState && mChildContainerTable.Count() == 1) {
    Sample(); // Run the first sample manually
    if (mAnimationElementTable.Count()) {
      StartSampling(GetRefreshDriverForDoc(mDocument));
    } else {
      mDeferredStartSampling = PR_TRUE;
    }
  }

  return NS_OK;
}

void
nsSMILAnimationController::RemoveChild(nsSMILTimeContainer& aChild)
{
  mChildContainerTable.RemoveEntry(&aChild);

  if (!mPauseState && mChildContainerTable.Count() == 0) {
    StopSampling(GetRefreshDriverForDoc(mDocument));
  }
}
