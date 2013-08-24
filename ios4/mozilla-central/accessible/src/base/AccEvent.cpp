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
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2003
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Aaron Leventhal <aaronl@netscape.com> <original author>
 *   Alexander Surkov <surkov.alexander@gmail.com>
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

#include "AccEvent.h"

#include "nsAccessibilityService.h"
#include "nsAccUtils.h"
#include "nsApplicationAccessibleWrap.h"
#include "nsDocAccessible.h"
#include "nsIAccessibleText.h"
#ifdef MOZ_XUL
#include "nsXULTreeAccessible.h"
#endif
#include "nsAccEvent.h"

#include "nsIDOMDocument.h"
#include "nsIEventStateManager.h"
#include "nsIServiceManager.h"
#ifdef MOZ_XUL
#include "nsIDOMXULMultSelectCntrlEl.h"
#endif

////////////////////////////////////////////////////////////////////////////////
// AccEvent
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// AccEvent constructors

AccEvent::AccEvent(PRUint32 aEventType, nsAccessible* aAccessible,
                   PRBool aIsAsync, EIsFromUserInput aIsFromUserInput,
                   EEventRule aEventRule) :
  mEventType(aEventType), mEventRule(aEventRule), mIsAsync(aIsAsync),
  mAccessible(aAccessible)
{
  CaptureIsFromUserInput(aIsFromUserInput);
}

AccEvent::AccEvent(PRUint32 aEventType, nsINode* aNode,
                   PRBool aIsAsync, EIsFromUserInput aIsFromUserInput,
                   EEventRule aEventRule) :
  mEventType(aEventType), mEventRule(aEventRule), mIsAsync(aIsAsync),
  mNode(aNode)
{
  CaptureIsFromUserInput(aIsFromUserInput);
}

////////////////////////////////////////////////////////////////////////////////
// AccEvent public methods

nsAccessible *
AccEvent::GetAccessible()
{
  if (!mAccessible)
    mAccessible = GetAccessibleForNode();

  return mAccessible;
}

nsINode*
AccEvent::GetNode()
{
  if (!mNode && mAccessible)
    mNode = mAccessible->GetNode();

  return mNode;
}

nsDocAccessible*
AccEvent::GetDocAccessible()
{
  nsINode *node = GetNode();
  if (node)
    return GetAccService()->GetDocAccessible(node->GetOwnerDoc());

  return nsnull;
}

already_AddRefed<nsAccEvent>
AccEvent::CreateXPCOMObject()
{
  nsAccEvent* event = new nsAccEvent(this);
  NS_IF_ADDREF(event);
  return event;
}

////////////////////////////////////////////////////////////////////////////////
// AccEvent cycle collection

NS_IMPL_CYCLE_COLLECTION_CLASS(AccEvent)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_NATIVE(AccEvent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mAccessible)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NATIVE_BEGIN(AccEvent)
  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mAccessible");
  cb.NoteXPCOMChild(static_cast<nsIAccessible*>(tmp->mAccessible));
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(AccEvent, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(AccEvent, Release)

////////////////////////////////////////////////////////////////////////////////
// AccEvent protected methods

nsAccessible *
AccEvent::GetAccessibleForNode() const
{
  if (!mNode)
    return nsnull;

  nsAccessible *accessible = GetAccService()->GetAccessible(mNode);

#ifdef MOZ_XUL
  // hack for xul tree table. We need a better way for firing delayed event
  // against xul tree table. see bug 386821.
  // There will be problem if some day we want to fire delayed event against
  // the xul tree itself or an unselected treeitem.
  nsCOMPtr<nsIContent> content(do_QueryInterface(mNode));
  if (content && content->NodeInfo()->Equals(nsAccessibilityAtoms::tree,
                                             kNameSpaceID_XUL)) {

    nsCOMPtr<nsIDOMXULMultiSelectControlElement> multiSelect =
      do_QueryInterface(mNode);

    if (multiSelect) {
      PRInt32 treeIndex = -1;
      multiSelect->GetCurrentIndex(&treeIndex);
      if (treeIndex >= 0) {
        nsRefPtr<nsXULTreeAccessible> treeAcc = do_QueryObject(accessible);
        if (treeAcc)
          return treeAcc->GetTreeItemAccessible(treeIndex);
      }
    }
  }
#endif

  return accessible;
}

void
AccEvent::CaptureIsFromUserInput(EIsFromUserInput aIsFromUserInput)
{
  nsINode *targetNode = GetNode();

#ifdef DEBUG
  if (!targetNode) {
    // XXX: remove this hack during reorganization of 506907. Meanwhile we
    // want to get rid an assertion for application accessible events which
    // don't have DOM node (see bug 506206).
    nsApplicationAccessible *applicationAcc =
      nsAccessNode::GetApplicationAccessible();

    if (mAccessible != static_cast<nsIAccessible*>(applicationAcc))
      NS_ASSERTION(targetNode, "There should always be a DOM node for an event");
  }
#endif

  if (aIsFromUserInput != eAutoDetect) {
    mIsFromUserInput = aIsFromUserInput == eFromUserInput ? PR_TRUE : PR_FALSE;
    return;
  }

  if (!targetNode)
    return;

  nsIPresShell *presShell = nsCoreUtils::GetPresShellFor(targetNode);
  if (!presShell) {
    NS_NOTREACHED("Threre should always be an pres shell for an event");
    return;
  }

  nsIEventStateManager *esm = presShell->GetPresContext()->EventStateManager();
  if (!esm) {
    NS_NOTREACHED("There should always be an ESM for an event");
    return;
  }

  mIsFromUserInput = esm->IsHandlingUserInputExternal();
}


////////////////////////////////////////////////////////////////////////////////
// AccReorderEvent
////////////////////////////////////////////////////////////////////////////////

AccReorderEvent::
  AccReorderEvent(nsAccessible* aAccTarget, PRBool aIsAsynch,
                  PRBool aIsUnconditional, nsINode* aReasonNode) :
  AccEvent(::nsIAccessibleEvent::EVENT_REORDER, aAccTarget,
           aIsAsynch, eAutoDetect, AccEvent::eCoalesceFromSameSubtree),
  mUnconditionalEvent(aIsUnconditional), mReasonNode(aReasonNode)
{
}

PRBool
AccReorderEvent::IsUnconditionalEvent()
{
  return mUnconditionalEvent;
}

PRBool
AccReorderEvent::HasAccessibleInReasonSubtree()
{
  if (!mReasonNode)
    return PR_FALSE;

  nsAccessible *accessible = GetAccService()->GetAccessible(mReasonNode);
  return accessible || nsAccUtils::HasAccessibleChildren(mReasonNode);
}


////////////////////////////////////////////////////////////////////////////////
// AccStateChangeEvent
////////////////////////////////////////////////////////////////////////////////

// Note: we pass in eAllowDupes to the base class because we don't currently
// support correct state change coalescence (XXX Bug 569356). Also we need to
// decide how to coalesce events created via accessible (instead of node).
AccStateChangeEvent::
  AccStateChangeEvent(nsAccessible* aAccessible,
                      PRUint32 aState, PRBool aIsExtraState,
                      PRBool aIsEnabled, PRBool aIsAsynch,
                      EIsFromUserInput aIsFromUserInput):
  AccEvent(nsIAccessibleEvent::EVENT_STATE_CHANGE, aAccessible, aIsAsynch,
           aIsFromUserInput, eAllowDupes),
  mState(aState), mIsExtraState(aIsExtraState), mIsEnabled(aIsEnabled)
{
}

AccStateChangeEvent::
  AccStateChangeEvent(nsINode* aNode, PRUint32 aState, PRBool aIsExtraState,
                      PRBool aIsEnabled):
  AccEvent(::nsIAccessibleEvent::EVENT_STATE_CHANGE, aNode),
  mState(aState), mIsExtraState(aIsExtraState), mIsEnabled(aIsEnabled)
{
}

AccStateChangeEvent::
  AccStateChangeEvent(nsINode* aNode, PRUint32 aState, PRBool aIsExtraState) :
  AccEvent(::nsIAccessibleEvent::EVENT_STATE_CHANGE, aNode),
  mState(aState), mIsExtraState(aIsExtraState)
{
  // Use GetAccessibleForNode() because we do not want to store an accessible
  // since it leads to problems with delayed events in the case when
  // an accessible gets reorder event before delayed event is processed.
  nsAccessible *accessible = GetAccessibleForNode();
  if (accessible) {
    PRUint32 state = 0, extraState = 0;
    accessible->GetState(&state, mIsExtraState ? &extraState : nsnull);
    mIsEnabled = ((mIsExtraState ? extraState : state) & mState) != 0;
  } else {
    mIsEnabled = PR_FALSE;
  }
}

already_AddRefed<nsAccEvent>
AccStateChangeEvent::CreateXPCOMObject()
{
  nsAccEvent* event = new nsAccStateChangeEvent(this);
  NS_IF_ADDREF(event);
  return event;
}


////////////////////////////////////////////////////////////////////////////////
// AccTextChangeEvent
////////////////////////////////////////////////////////////////////////////////

// Note: we pass in eAllowDupes to the base class because we don't support text
// events coalescence. We fire delayed text change events in nsDocAccessible but
// we continue to base the event off the accessible object rather than just the
// node. This means we won't try to create an accessible based on the node when
// we are ready to fire the event and so we will no longer assert at that point
// if the node was removed from the document. Either way, the AT won't work with
// a defunct accessible so the behaviour should be equivalent.
// XXX revisit this when coalescence is faster (eCoalesceFromSameSubtree)
AccTextChangeEvent::
  AccTextChangeEvent(nsAccessible* aAccessible, PRInt32 aStart,
                     nsAString& aModifiedText, PRBool aIsInserted,
                     PRBool aIsAsynch, EIsFromUserInput aIsFromUserInput)
  : AccEvent(aIsInserted ?
             static_cast<PRUint32>(nsIAccessibleEvent::EVENT_TEXT_INSERTED) :
             static_cast<PRUint32>(nsIAccessibleEvent::EVENT_TEXT_REMOVED),
             aAccessible, aIsAsynch, aIsFromUserInput, eAllowDupes)
  , mStart(aStart)
  , mIsInserted(aIsInserted)
  , mModifiedText(aModifiedText)
{
}

already_AddRefed<nsAccEvent>
AccTextChangeEvent::CreateXPCOMObject()
{
  nsAccEvent* event = new nsAccTextChangeEvent(this);
  NS_IF_ADDREF(event);
  return event;
}


////////////////////////////////////////////////////////////////////////////////
// AccHideEvent
////////////////////////////////////////////////////////////////////////////////

AccHideEvent::
  AccHideEvent(nsAccessible* aTarget, nsINode* aTargetNode,
               PRBool aIsAsynch, EIsFromUserInput aIsFromUserInput) :
  AccEvent(nsIAccessibleEvent::EVENT_HIDE, aTarget, aIsAsynch,
           aIsFromUserInput, eCoalesceFromSameSubtree)
{
  mNode = aTargetNode;
  mParent = mAccessible->GetCachedParent();
  mNextSibling = mAccessible->GetCachedNextSibling();
  mPrevSibling = mAccessible->GetCachedPrevSibling();
}


////////////////////////////////////////////////////////////////////////////////
// AccCaretMoveEvent
////////////////////////////////////////////////////////////////////////////////

AccCaretMoveEvent::
  AccCaretMoveEvent(nsAccessible* aAccessible, PRInt32 aCaretOffset) :
  AccEvent(::nsIAccessibleEvent::EVENT_TEXT_CARET_MOVED, aAccessible, PR_TRUE), // Currently always asynch
  mCaretOffset(aCaretOffset)
{
}

AccCaretMoveEvent::
  AccCaretMoveEvent(nsINode* aNode) :
  AccEvent(::nsIAccessibleEvent::EVENT_TEXT_CARET_MOVED, aNode, PR_TRUE), // Currently always asynch
  mCaretOffset(-1)
{
}

already_AddRefed<nsAccEvent>
AccCaretMoveEvent::CreateXPCOMObject()
{
  nsAccEvent* event = new nsAccCaretMoveEvent(this);
  NS_IF_ADDREF(event);
  return event;
}


////////////////////////////////////////////////////////////////////////////////
// AccTableChangeEvent
////////////////////////////////////////////////////////////////////////////////

AccTableChangeEvent::
  AccTableChangeEvent(nsAccessible* aAccessible, PRUint32 aEventType,
                      PRInt32 aRowOrColIndex, PRInt32 aNumRowsOrCols,
                      PRBool aIsAsynch) :
  AccEvent(aEventType, aAccessible, aIsAsynch),
  mRowOrColIndex(aRowOrColIndex), mNumRowsOrCols(aNumRowsOrCols)
{
}

already_AddRefed<nsAccEvent>
AccTableChangeEvent::CreateXPCOMObject()
{
  nsAccEvent* event = new nsAccTableChangeEvent(this);
  NS_IF_ADDREF(event);
  return event;
}

