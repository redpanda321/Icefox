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
 * Portions created by the Initial Developer are Copyright (C) 2008
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

#ifndef NS_SMILANIMATIONCONTROLLER_H_
#define NS_SMILANIMATIONCONTROLLER_H_

#include "nsAutoPtr.h"
#include "nsCOMPtr.h"
#include "nsTArray.h"
#include "nsITimer.h"
#include "nsTHashtable.h"
#include "nsHashKeys.h"
#include "nsSMILTimeContainer.h"
#include "nsSMILCompositorTable.h"
#include "nsSMILMilestone.h"
#include "nsRefreshDriver.h"

struct nsSMILTargetIdentifier;
class nsISMILAnimationElement;
class nsIDocument;

//----------------------------------------------------------------------
// nsSMILAnimationController
//
// The animation controller maintains the animation timer and determines the
// sample times and sample rate for all SMIL animations in a document. There is
// at most one animation controller per nsDocument so that frame-rate tuning can
// be performed at a document-level.
//
// The animation controller can contain many child time containers (timed
// document root objects) which may correspond to SVG document fragments within
// a compound document. These time containers can be paused individually or
// here, at the document level.
//
class nsSMILAnimationController : public nsSMILTimeContainer,
                                  public nsARefreshObserver
{
public:
  nsSMILAnimationController();
  ~nsSMILAnimationController();

  // nsSMILContainer
  virtual void Pause(PRUint32 aType);
  virtual void Resume(PRUint32 aType);
  virtual nsSMILTime GetParentTime() const;

  // nsARefreshObserver
  NS_IMETHOD_(nsrefcnt) AddRef();
  NS_IMETHOD_(nsrefcnt) Release();

  virtual void WillRefresh(mozilla::TimeStamp aTime);

  // Methods for registering and enumerating animation elements
  void RegisterAnimationElement(nsISMILAnimationElement* aAnimationElement);
  void UnregisterAnimationElement(nsISMILAnimationElement* aAnimationElement);

  // Methods for resampling all animations
  // (A resample performs the same operations as a sample but doesn't advance
  // the current time and doesn't check if the container is paused)
  void Resample() { DoSample(PR_FALSE); }
  void SetResampleNeeded() { mResampleNeeded = PR_TRUE; }
  void FlushResampleRequests()
  {
    if (!mResampleNeeded)
      return;

    Resample();
  }

  // Methods for handling page transitions
  void OnPageShow();
  void OnPageHide();

  // Methods for supporting cycle-collection
  void Traverse(nsCycleCollectionTraversalCallback* aCallback);
  void Unlink();

  // Methods for controlling whether we're sampling
  // (Use to register/unregister us with the given nsRefreshDriver)
  void StartSampling(nsRefreshDriver* aRefreshDriver);
  void StopSampling(nsRefreshDriver* aRefreshDriver);

protected:
  // Typedefs
  typedef nsPtrHashKey<nsSMILTimeContainer> TimeContainerPtrKey;
  typedef nsTHashtable<TimeContainerPtrKey> TimeContainerHashtable;
  typedef nsPtrHashKey<nsISMILAnimationElement> AnimationElementPtrKey;
  typedef nsTHashtable<AnimationElementPtrKey> AnimationElementHashtable;

  struct SampleTimeContainerParams
  {
    TimeContainerHashtable* mActiveContainers;
    PRBool                  mSkipUnchangedContainers;
  };

  struct SampleAnimationParams
  {
    TimeContainerHashtable* mActiveContainers;
    nsSMILCompositorTable*  mCompositorTable;
  };

  struct GetMilestoneElementsParams
  {
    nsTArray<nsRefPtr<nsISMILAnimationElement> > mElements;
    nsSMILMilestone                              mMilestone;
  };

  // Factory methods
  friend nsSMILAnimationController*
  NS_NewSMILAnimationController(nsIDocument* aDoc);
  nsresult    Init(nsIDocument* aDoc);

  // Cycle-collection implementation helpers
  PR_STATIC_CALLBACK(PLDHashOperator) CompositorTableEntryTraverse(
      nsSMILCompositor* aCompositor, void* aArg);

  // Sample-related callbacks and implementation helpers
  virtual void DoSample();
  void DoSample(PRBool aSkipUnchangedContainers);

  void RewindElements();
  PR_STATIC_CALLBACK(PLDHashOperator) RewindNeeded(
      TimeContainerPtrKey* aKey, void* aData);
  PR_STATIC_CALLBACK(PLDHashOperator) RewindAnimation(
      AnimationElementPtrKey* aKey, void* aData);
  PR_STATIC_CALLBACK(PLDHashOperator) ClearRewindNeeded(
      TimeContainerPtrKey* aKey, void* aData);

  void DoMilestoneSamples();
  PR_STATIC_CALLBACK(PLDHashOperator) GetNextMilestone(
      TimeContainerPtrKey* aKey, void* aData);
  PR_STATIC_CALLBACK(PLDHashOperator) GetMilestoneElements(
      TimeContainerPtrKey* aKey, void* aData);

  PR_STATIC_CALLBACK(PLDHashOperator) SampleTimeContainer(
      TimeContainerPtrKey* aKey, void* aData);
  PR_STATIC_CALLBACK(PLDHashOperator) SampleAnimation(
      AnimationElementPtrKey* aKey, void* aData);
  static void SampleTimedElement(nsISMILAnimationElement* aElement,
                                 TimeContainerHashtable* aActiveContainers);
  static void AddAnimationToCompositorTable(
    nsISMILAnimationElement* aElement, nsSMILCompositorTable* aCompositorTable);
  static PRBool GetTargetIdentifierForAnimation(
      nsISMILAnimationElement* aAnimElem, nsSMILTargetIdentifier& aResult);

  // Methods for adding/removing time containers
  virtual nsresult AddChild(nsSMILTimeContainer& aChild);
  virtual void     RemoveChild(nsSMILTimeContainer& aChild);

  // Members
  nsAutoRefCnt mRefCnt;
  NS_DECL_OWNINGTHREAD

  static const PRUint32      kTimerInterval;
  AnimationElementHashtable  mAnimationElementTable;
  TimeContainerHashtable     mChildContainerTable;
  PRPackedBool               mResampleNeeded;
  PRPackedBool               mDeferredStartSampling;

  // Store raw ptr to mDocument.  It owns the controller, so controller
  // shouldn't outlive it
  nsIDocument* mDocument;

  // Contains compositors used in our last sample.  We keep this around
  // so we can detect when an element/attribute used to be animated,
  // but isn't anymore for some reason. (e.g. if its <animate> element is
  // removed or retargeted)
  nsAutoPtr<nsSMILCompositorTable> mLastCompositorTable;
};

nsSMILAnimationController* NS_NewSMILAnimationController(nsIDocument *doc);

#endif // NS_SMILANIMATIONCONTROLLER_H_
