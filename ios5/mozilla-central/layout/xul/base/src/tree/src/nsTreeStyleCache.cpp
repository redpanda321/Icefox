/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTreeStyleCache.h"
#include "nsISupportsArray.h"
#include "nsStyleSet.h"
#include "mozilla/dom/Element.h"

// The style context cache impl
nsStyleContext*
nsTreeStyleCache::GetStyleContext(nsICSSPseudoComparator* aComparator,
                                  nsPresContext* aPresContext,
                                  nsIContent* aContent, 
                                  nsStyleContext* aContext,
                                  nsIAtom* aPseudoElement,
                                  nsISupportsArray* aInputWord)
{
  PRUint32 count;
  aInputWord->Count(&count);
  nsDFAState startState(0);
  nsDFAState* currState = &startState;

  // Go ahead and init the transition table.
  if (!mTransitionTable) {
    // Automatic miss. Build the table
    mTransitionTable =
      new nsObjectHashtable(nsnull, nsnull, DeleteDFAState, nsnull);
  }

  // The first transition is always made off the supplied pseudo-element.
  nsTransitionKey key(currState->GetStateID(), aPseudoElement);
  currState = static_cast<nsDFAState*>(mTransitionTable->Get(&key));

  if (!currState) {
    // We had a miss. Make a new state and add it to our hash.
    currState = new nsDFAState(mNextState);
    mNextState++;
    mTransitionTable->Put(&key, currState);
  }

  for (PRUint32 i = 0; i < count; i++) {
    nsCOMPtr<nsIAtom> pseudo = getter_AddRefs(static_cast<nsIAtom*>(aInputWord->ElementAt(i)));
    nsTransitionKey key(currState->GetStateID(), pseudo);
    currState = static_cast<nsDFAState*>(mTransitionTable->Get(&key));

    if (!currState) {
      // We had a miss. Make a new state and add it to our hash.
      currState = new nsDFAState(mNextState);
      mNextState++;
      mTransitionTable->Put(&key, currState);
    }
  }

  // We're in a final state.
  // Look up our style context for this state.
  nsStyleContext* result = nsnull;
  if (mCache)
    result = static_cast<nsStyleContext*>(mCache->Get(currState));
  if (!result) {
    // We missed the cache. Resolve this pseudo-style.
    result = aPresContext->StyleSet()->
      ResolveXULTreePseudoStyle(aContent->AsElement(), aPseudoElement,
                                aContext, aComparator).get();

    // Put the style context in our table, transferring the owning reference to the table.
    if (!mCache) {
      mCache = new nsObjectHashtable(nsnull, nsnull, ReleaseStyleContext, nsnull);
    }
    mCache->Put(currState, result);
  }

  return result;
}

bool
nsTreeStyleCache::DeleteDFAState(nsHashKey *aKey,
                                 void *aData,
                                 void *closure)
{
  nsDFAState* entry = static_cast<nsDFAState*>(aData);
  delete entry;
  return true;
}

bool
nsTreeStyleCache::ReleaseStyleContext(nsHashKey *aKey,
                                      void *aData,
                                      void *closure)
{
  nsStyleContext* context = static_cast<nsStyleContext*>(aData);
  context->Release();
  return true;
}
