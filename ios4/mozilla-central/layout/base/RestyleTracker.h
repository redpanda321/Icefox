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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *     Boris Zbarsky <bzbarsky@mit.edu> (original author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

/**
 * A class which manages pending restyles.  This handles keeping track
 * of what nodes restyles need to happen on and so forth.
 */

#ifndef mozilla_css_RestyleTracker_h
#define mozilla_css_RestyleTracker_h

#include "mozilla/dom/Element.h"
#include "nsDataHashtable.h"
#include "nsIFrame.h"

class nsCSSFrameConstructor;

namespace mozilla {
namespace css {

class RestyleTracker {
public:
  typedef mozilla::dom::Element Element;

  RestyleTracker(PRUint32 aRestyleBits,
                 nsCSSFrameConstructor* aFrameConstructor) :
    mRestyleBits(aRestyleBits), mFrameConstructor(aFrameConstructor),
    mHaveLaterSiblingRestyles(PR_FALSE)
  {
    NS_PRECONDITION((mRestyleBits & ~ELEMENT_ALL_RESTYLE_FLAGS) == 0,
                    "Why do we have these bits set?");
    NS_PRECONDITION((mRestyleBits & ELEMENT_PENDING_RESTYLE_FLAGS) != 0,
                    "Must have a restyle flag");
    NS_PRECONDITION((mRestyleBits & ELEMENT_PENDING_RESTYLE_FLAGS) !=
                      ELEMENT_PENDING_RESTYLE_FLAGS,
                    "Shouldn't have both restyle flags set");
    NS_PRECONDITION((mRestyleBits & ~ELEMENT_PENDING_RESTYLE_FLAGS) != 0,
                    "Must have root flag");
    NS_PRECONDITION((mRestyleBits & ~ELEMENT_PENDING_RESTYLE_FLAGS) !=
                    (ELEMENT_ALL_RESTYLE_FLAGS & ~ELEMENT_PENDING_RESTYLE_FLAGS),
                    "Shouldn't have both root flags");
  }

  PRBool Init() {
    return mPendingRestyles.Init();
  }

  PRUint32 Count() const {
    return mPendingRestyles.Count();
  }

  /**
   * Add a restyle for the given element to the tracker.  Returns true
   * if the element already had eRestyle_LaterSiblings set on it.
   */
  PRBool AddPendingRestyle(Element* aElement, nsRestyleHint aRestyleHint,
                           nsChangeHint aMinChangeHint);

  /**
   * Process the restyles we've been tracking.
   */
  void ProcessRestyles();

  // Return our ELEMENT_HAS_PENDING_(ANIMATION_)RESTYLE bit
  PRUint32 RestyleBit() const {
    return mRestyleBits & ELEMENT_PENDING_RESTYLE_FLAGS;
  }

  // Return our ELEMENT_IS_POTENTIAL_(ANIMATION_)RESTYLE_ROOT bit
  PRUint32 RootBit() const {
    return mRestyleBits & ~ELEMENT_PENDING_RESTYLE_FLAGS;
  }
  
  struct RestyleData {
    nsRestyleHint mRestyleHint;  // What we want to restyle
    nsChangeHint  mChangeHint;   // The minimal change hint for "self"
  };

  /**
   * If the given Element has a restyle pending for it, return the
   * relevant restyle data.  This function will clear everything other
   * than a possible eRestyle_LaterSiblings hint for aElement out of
   * our hashtable.  The returned aData will never have an
   * eRestyle_LaterSiblings hint in it.
   *
   * The return value indicates whether any restyle data was found for
   * the element.  If false is returned, then the state of *aData is
   * undefined.
   */
  PRBool GetRestyleData(Element* aElement, RestyleData* aData);

  /**
   * The document we're associated with.
   */
  inline nsIDocument* Document() const;

  struct RestyleEnumerateData : public RestyleData {
    nsRefPtr<Element> mElement;
  };

private:
  /**
   * Handle a single mPendingRestyles entry.  aRestyleHint must not
   * include eRestyle_LaterSiblings; that needs to be dealt with
   * before calling this function.
   */
  inline void ProcessOneRestyle(Element* aElement,
                                nsRestyleHint aRestyleHint,
                                nsChangeHint aChangeHint);

  typedef nsDataHashtable<nsISupportsHashKey, RestyleData> PendingRestyleTable;
  typedef nsAutoTArray< nsRefPtr<Element>, 32> RestyleRootArray;
  // Our restyle bits.  These will be a subset of ELEMENT_ALL_RESTYLE_FLAGS, and
  // will include one flag from ELEMENT_PENDING_RESTYLE_FLAGS and one flag
  // that's not in ELEMENT_PENDING_RESTYLE_FLAGS.
  PRUint32 mRestyleBits;
  nsCSSFrameConstructor* mFrameConstructor; // Owns us
  // A hashtable that maps elements to RestyleData structs.  The
  // values only make sense if the element's current document is our
  // document and it has our RestyleBit() flag set.  In particular,
  // said bit might not be set if the element had a restyle posted and
  // then was moved around in the DOM.
  PendingRestyleTable mPendingRestyles;
  // An array that keeps track of our possible restyle roots.  This
  // maintains the invariant that if A and B are both restyle roots
  // and A is an ancestor of B then A will come after B in the array.
  // We maintain this invariant by checking whether an element has an
  // ancestor with the restyle root bit set before appending it to the
  // array.
  RestyleRootArray mRestyleRoots;
  // True if we have some entries with the eRestyle_LaterSiblings
  // flag.  We need this to avoid enumerating the hashtable looking
  // for such entries when we can't possibly have any.
  PRBool mHaveLaterSiblingRestyles;
};

inline PRBool RestyleTracker::AddPendingRestyle(Element* aElement,
                                                nsRestyleHint aRestyleHint,
                                                nsChangeHint aMinChangeHint)
{
  RestyleData existingData;
  existingData.mRestyleHint = nsRestyleHint(0);
  existingData.mChangeHint = NS_STYLE_HINT_NONE;

  // Check the RestyleBit() flag before doing the hashtable Get, since
  // it's possible that the data in the hashtable isn't actually
  // relevant anymore (if the flag is not set).
  if (aElement->HasFlag(RestyleBit())) {
    mPendingRestyles.Get(aElement, &existingData);
  } else {
    aElement->SetFlags(RestyleBit());
  }

  PRBool hadRestyleLaterSiblings =
    (existingData.mRestyleHint & eRestyle_LaterSiblings) != 0;
  existingData.mRestyleHint =
    nsRestyleHint(existingData.mRestyleHint | aRestyleHint);
  NS_UpdateHint(existingData.mChangeHint, aMinChangeHint);

  mPendingRestyles.Put(aElement, existingData);

  // We can only treat this element as a restyle root if we would
  // actually restyle its descendants (so either call
  // ReResolveStyleContext on it or just reframe it).
  if ((aRestyleHint & (eRestyle_Self | eRestyle_Subtree)) ||
      (aMinChangeHint & nsChangeHint_ReconstructFrame)) {
    for (const Element* cur = aElement; !cur->HasFlag(RootBit()); ) {
      nsIContent* parent = cur->GetFlattenedTreeParent();
      // Stop if we have no parent or the parent is not an element or
      // we're part of the viewport scrollbars (because those are not
      // frametree descendants of the primary frame of the root
      // element).
      // XXXbz maybe the primary frame of the root should be the root scrollframe?
      if (!parent || !parent->IsElement() ||
          // If we've hit the root via a native anonymous kid and that
          // this native anonymous kid is not obviously a descendant
          // of the root's primary frame, assume we're under the root
          // scrollbars.  Since those don't get reresolved when
          // reresolving the root, we need to make sure to add the
          // element to mRestyleRoots.
          (cur->IsInNativeAnonymousSubtree() && !parent->GetParent() &&
           cur->GetPrimaryFrame() &&
           cur->GetPrimaryFrame()->GetParent() != parent->GetPrimaryFrame())) {
        mRestyleRoots.AppendElement(aElement);
        break;
      }
      cur = parent->AsElement();
    }
    // At this point some ancestor of aElement (possibly aElement
    // itself) is in mRestyleRoots.  Set the root bit on aElement, to
    // speed up searching for an existing root on its descendants.
    aElement->SetFlags(RootBit());
  }

  mHaveLaterSiblingRestyles =
    mHaveLaterSiblingRestyles || (aRestyleHint & eRestyle_LaterSiblings) != 0;
  return hadRestyleLaterSiblings;
}

} // namespace css
} // namespace mozilla

#endif /* mozilla_css_RestyleTracker_h */
