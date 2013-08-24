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
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Alexander Surkov <surkov.alexander@gmail.com> (original author)
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

#include "nsEventShell.h"

#include "nsAccUtils.h"
#include "nsCoreUtils.h"
#include "nsDocAccessible.h"

////////////////////////////////////////////////////////////////////////////////
// nsEventShell
////////////////////////////////////////////////////////////////////////////////

void
nsEventShell::FireEvent(AccEvent* aEvent)
{
  if (!aEvent)
    return;

  nsAccessible *accessible = aEvent->GetAccessible();
  NS_ENSURE_TRUE(accessible,);

  nsINode* node = aEvent->GetNode();
  if (node) {
    sEventTargetNode = node;
    sEventFromUserInput = aEvent->IsFromUserInput();
  }

  accessible->HandleAccEvent(aEvent);

  sEventTargetNode = nsnull;
}

void
nsEventShell::FireEvent(PRUint32 aEventType, nsAccessible *aAccessible,
                        PRBool aIsAsynch, EIsFromUserInput aIsFromUserInput)
{
  NS_ENSURE_TRUE(aAccessible,);

  nsRefPtr<AccEvent> event = new AccEvent(aEventType, aAccessible,
                                          aIsAsynch, aIsFromUserInput);

  FireEvent(event);
}

void 
nsEventShell::GetEventAttributes(nsINode *aNode,
                                 nsIPersistentProperties *aAttributes)
{
  if (aNode != sEventTargetNode)
    return;

  nsAccUtils::SetAccAttr(aAttributes, nsAccessibilityAtoms::eventFromInput,
                         sEventFromUserInput ? NS_LITERAL_STRING("true") :
                                               NS_LITERAL_STRING("false"));
}

////////////////////////////////////////////////////////////////////////////////
// nsEventShell: private

PRBool nsEventShell::sEventFromUserInput = PR_FALSE;
nsCOMPtr<nsINode> nsEventShell::sEventTargetNode;


////////////////////////////////////////////////////////////////////////////////
// nsAccEventQueue
////////////////////////////////////////////////////////////////////////////////

nsAccEventQueue::nsAccEventQueue(nsDocAccessible *aDocument):
  mObservingRefresh(PR_FALSE), mDocument(aDocument)
{
}

nsAccEventQueue::~nsAccEventQueue()
{
  NS_ASSERTION(!mDocument, "Queue wasn't shut down!");
}

////////////////////////////////////////////////////////////////////////////////
// nsAccEventQueue: nsISupports and cycle collection

NS_IMPL_CYCLE_COLLECTION_CLASS(nsAccEventQueue)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsAccEventQueue)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsAccEventQueue)
  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mDocument");
  cb.NoteXPCOMChild(static_cast<nsIAccessible*>(tmp->mDocument.get()));
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSTARRAY_MEMBER(mEvents, AccEvent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsAccEventQueue)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mDocument)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSTARRAY(mEvents)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsAccEventQueue)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsAccEventQueue)

////////////////////////////////////////////////////////////////////////////////
// nsAccEventQueue: public

void
nsAccEventQueue::Push(AccEvent* aEvent)
{
  mEvents.AppendElement(aEvent);

  // Filter events.
  CoalesceEvents();

  // Associate text change with hide event if it wasn't stolen from hiding
  // siblings during coalescence.
  AccHideEvent* hideEvent = downcast_accEvent(aEvent);
  if (hideEvent && !hideEvent->mTextChangeEvent)
    CreateTextChangeEventFor(hideEvent);

  // Process events.
  PrepareFlush();
}

void
nsAccEventQueue::Shutdown()
{
  if (mObservingRefresh) {
    nsCOMPtr<nsIPresShell> shell = mDocument->GetPresShell();
    if (!shell ||
        shell->RemoveRefreshObserver(this, Flush_Display)) {
      mObservingRefresh = PR_FALSE;
    }
  }
  mDocument = nsnull;
  mEvents.Clear();
}

////////////////////////////////////////////////////////////////////////////////
// nsAccEventQueue: private

void
nsAccEventQueue::PrepareFlush()
{
  // If there are pending events in the queue and events flush isn't planed
  // yet start events flush asynchronously.
  if (mEvents.Length() > 0 && !mObservingRefresh) {
    nsCOMPtr<nsIPresShell> shell = mDocument->GetPresShell();
    // Use a Flush_Display observer so that it will get called after
    // style and ayout have been flushed.
    if (shell &&
        shell->AddRefreshObserver(this, Flush_Display)) {
      mObservingRefresh = PR_TRUE;
    }
  }
}

void
nsAccEventQueue::WillRefresh(mozilla::TimeStamp aTime)
{
  // If the document accessible is now shut down, don't fire events in it
  // anymore.
  if (!mDocument)
    return;

  // Process only currently queued events. Newly appended events during events
  // flushing won't be processed.
  nsTArray < nsRefPtr<AccEvent> > events;
  events.SwapElements(mEvents);
  PRUint32 length = events.Length();
  NS_ASSERTION(length, "How did we get here without events to fire?");

  for (PRUint32 index = 0; index < length; index ++) {

    AccEvent* accEvent = events[index];
    if (accEvent->mEventRule != AccEvent::eDoNotEmit) {
      mDocument->ProcessPendingEvent(accEvent);

      AccHideEvent* hideEvent = downcast_accEvent(accEvent);
      if (hideEvent) {
        if (hideEvent->mTextChangeEvent)
          mDocument->ProcessPendingEvent(hideEvent->mTextChangeEvent);
      }
    }

    // No document means it was shut down during event handling by AT
    if (!mDocument)
      return;
  }

  if (mEvents.Length() == 0) {
    nsCOMPtr<nsIPresShell> shell = mDocument->GetPresShell();
    if (!shell ||
        shell->RemoveRefreshObserver(this, Flush_Display)) {
      mObservingRefresh = PR_FALSE;
    }
  }
}

void
nsAccEventQueue::CoalesceEvents()
{
  PRUint32 numQueuedEvents = mEvents.Length();
  PRInt32 tail = numQueuedEvents - 1;
  AccEvent* tailEvent = mEvents[tail];

  // No node means this is application accessible (which can be a subject
  // of reorder events), we do not coalesce events for it currently.
  if (!tailEvent->mNode)
    return;

  switch(tailEvent->mEventRule) {
    case AccEvent::eCoalesceFromSameSubtree:
    {
      for (PRInt32 index = tail - 1; index >= 0; index--) {
        AccEvent* thisEvent = mEvents[index];

        if (thisEvent->mEventType != tailEvent->mEventType)
          continue; // Different type

        // Skip event for application accessible since no coalescence for it
        // is supported. Ignore events from different documents since we don't
        // coalesce them.
        if (!thisEvent->mNode ||
            thisEvent->mNode->GetOwnerDoc() != tailEvent->mNode->GetOwnerDoc())
          continue;

        // If event queue contains an event of the same type and having target
        // that is sibling of target of newly appended event then apply its
        // event rule to the newly appended event.

        // XXX: deal with show events separately because they can't be
        // coalesced by accessible tree the same as hide events since target
        // accessibles can't be created at this point because of lazy frame
        // construction (bug 570275).

        // Coalesce hide events for sibling targets.
        if (tailEvent->mEventType == nsIAccessibleEvent::EVENT_HIDE) {
          AccHideEvent* tailHideEvent = downcast_accEvent(tailEvent);
          AccHideEvent* thisHideEvent = downcast_accEvent(thisEvent);
          if (thisHideEvent->mParent == tailHideEvent->mParent) {
            tailEvent->mEventRule = thisEvent->mEventRule;

            // Coalesce text change events for hide events.
            if (tailEvent->mEventRule != AccEvent::eDoNotEmit)
              CoalesceTextChangeEventsFor(tailHideEvent, thisHideEvent);

            return;
          }
        }

        // Ignore events unattached from DOM since we don't coalesce them.
        if (!thisEvent->mNode->IsInDoc())
          continue;

        // Coalesce show and reorder events by sibling targets.
        if (thisEvent->mNode->GetNodeParent() ==
            tailEvent->mNode->GetNodeParent()) {
          tailEvent->mEventRule = thisEvent->mEventRule;
          return;
        }

        // Specifies if this event target can be descendant of tail node.
        PRBool thisCanBeDescendantOfTail = PR_FALSE;

        // Coalesce depending on whether this event was coalesced or not.
        if (thisEvent->mEventRule == AccEvent::eDoNotEmit) {
          // If this event was coalesced then do not emit tail event iff tail
          // event has the same target or its target is contained by this event
          // target. Note, we don't need to check whether tail event target
          // contains this event target since this event was coalesced already.

          // As well we don't need to apply the calculated rule for siblings of
          // tail node because tail event rule was applied to possible tail
          // node siblings while this event was coalesced.

          if (thisEvent->mNode == tailEvent->mNode) {
            thisEvent->mEventRule = AccEvent::eDoNotEmit;
            return;
          }

        } else {
          // If this event wasn't coalesced already then try to coalesce it or
          // tail event. If this event is coalesced by tail event then continue
          // search through events other events that can be coalesced by tail
          // event.

          // If tail and this events have the same target then coalesce tail
          // event because more early event we should fire early and then stop
          // processing.
          if (thisEvent->mNode == tailEvent->mNode) {
            // Coalesce reorder events by special way since reorder events can
            // be conditional events (be or not be fired in the end).
            if (thisEvent->mEventType == nsIAccessibleEvent::EVENT_REORDER) {
              CoalesceReorderEventsFromSameSource(thisEvent, tailEvent);
              if (tailEvent->mEventRule != AccEvent::eDoNotEmit)
                continue;
            }
            else {
              tailEvent->mEventRule = AccEvent::eDoNotEmit;
            }

            return;
          }

          // This and tail events can be anywhere in the tree, make assumptions
          // for mutation events.

          // More older show event target (thisNode) can't be contained by
          // recent.
          // show event target (tailNode), i.e be a descendant of tailNode.
          // XXX: target of older show event caused by DOM node appending can be
          // contained by target of recent show event caused by style change.
          // XXX: target of older show event caused by style change can be
          // contained by target of recent show event caused by style change.
          thisCanBeDescendantOfTail =
            tailEvent->mEventType != nsIAccessibleEvent::EVENT_SHOW ||
            tailEvent->mIsAsync;
        }

        // Coalesce tail event if tail node is descendant of this node. Stop
        // processing if tail event is coalesced since all possible descendants
        // of this node was coalesced before.
        // Note: more older hide event target (thisNode) can't contain recent
        // hide event target (tailNode), i.e. be ancestor of tailNode. Skip
        // this check for hide events.
        if (tailEvent->mEventType != nsIAccessibleEvent::EVENT_HIDE &&
            nsCoreUtils::IsAncestorOf(thisEvent->mNode, tailEvent->mNode)) {

          if (thisEvent->mEventType == nsIAccessibleEvent::EVENT_REORDER) {
            CoalesceReorderEventsFromSameTree(thisEvent, tailEvent);
            if (tailEvent->mEventRule != AccEvent::eDoNotEmit)
              continue;

            return;
          }

          tailEvent->mEventRule = AccEvent::eDoNotEmit;
          return;
        }

#ifdef DEBUG
        if (tailEvent->mEventType == nsIAccessibleEvent::EVENT_HIDE &&
            nsCoreUtils::IsAncestorOf(thisEvent->mNode, tailEvent->mNode)) {
          NS_NOTREACHED("More older hide event target is an ancestor of recent hide event target!");
        }
#endif

        // If this node is a descendant of tail node then coalesce this event,
        // check other events in the queue.
        if (thisCanBeDescendantOfTail &&
            nsCoreUtils::IsAncestorOf(tailEvent->mNode, thisEvent->mNode)) {

          if (thisEvent->mEventType == nsIAccessibleEvent::EVENT_REORDER) {
            CoalesceReorderEventsFromSameTree(tailEvent, thisEvent);
            if (tailEvent->mEventRule != AccEvent::eDoNotEmit)
              continue;

            return;
          }

          // Do not emit thisEvent, also apply this result to sibling nodes of
          // thisNode.
          thisEvent->mEventRule = AccEvent::eDoNotEmit;
          ApplyToSiblings(0, index, thisEvent->mEventType,
                          thisEvent->mNode, AccEvent::eDoNotEmit);
          continue;
        }

#ifdef DEBUG
        if (!thisCanBeDescendantOfTail &&
            nsCoreUtils::IsAncestorOf(tailEvent->mNode, thisEvent->mNode)) {
          NS_NOTREACHED("Older event target is a descendant of recent event target!");
        }
#endif

      } // for (index)

    } break; // case eCoalesceFromSameSubtree

    case AccEvent::eCoalesceFromSameDocument:
    {
      // Used for focus event, coalesce more older event since focus event
      // for accessible can be duplicated by event for its document, we are
      // interested in focus event for accessible.
      for (PRInt32 index = tail - 1; index >= 0; index--) {
        AccEvent* thisEvent = mEvents[index];
        if (thisEvent->mEventType == tailEvent->mEventType &&
            thisEvent->mEventRule == tailEvent->mEventRule &&
            thisEvent->GetDocAccessible() == tailEvent->GetDocAccessible()) {
          thisEvent->mEventRule = AccEvent::eDoNotEmit;
          return;
        }
      }
    } break; // case eCoalesceFromSameDocument

    case AccEvent::eRemoveDupes:
    {
      // Check for repeat events, coalesce newly appended event by more older
      // event.
      for (PRInt32 index = tail - 1; index >= 0; index--) {
        AccEvent* accEvent = mEvents[index];
        if (accEvent->mEventType == tailEvent->mEventType &&
            accEvent->mEventRule == tailEvent->mEventRule &&
            accEvent->mNode == tailEvent->mNode) {
          tailEvent->mEventRule = AccEvent::eDoNotEmit;
          return;
        }
      }
    } break; // case eRemoveDupes

    default:
      break; // case eAllowDupes, eDoNotEmit
  } // switch
}

void
nsAccEventQueue::ApplyToSiblings(PRUint32 aStart, PRUint32 aEnd,
                                 PRUint32 aEventType, nsINode* aNode,
                                 AccEvent::EEventRule aEventRule)
{
  for (PRUint32 index = aStart; index < aEnd; index ++) {
    AccEvent* accEvent = mEvents[index];
    if (accEvent->mEventType == aEventType &&
        accEvent->mEventRule != AccEvent::eDoNotEmit &&
        accEvent->mNode->GetNodeParent() == aNode->GetNodeParent()) {
      accEvent->mEventRule = aEventRule;
    }
  }
}

void
nsAccEventQueue::CoalesceReorderEventsFromSameSource(AccEvent* aAccEvent1,
                                                     AccEvent* aAccEvent2)
{
  // Do not emit event2 if event1 is unconditional.
  AccReorderEvent* reorderEvent1 = downcast_accEvent(aAccEvent1);
  if (reorderEvent1->IsUnconditionalEvent()) {
    aAccEvent2->mEventRule = AccEvent::eDoNotEmit;
    return;
  }

  // Do not emit event1 if event2 is unconditional.
  AccReorderEvent* reorderEvent2 = downcast_accEvent(aAccEvent2);
  if (reorderEvent2->IsUnconditionalEvent()) {
    aAccEvent1->mEventRule = AccEvent::eDoNotEmit;
    return;
  }

  // Do not emit event2 if event1 is valid, otherwise do not emit event1.
  if (reorderEvent1->HasAccessibleInReasonSubtree())
    aAccEvent2->mEventRule = AccEvent::eDoNotEmit;
  else
    aAccEvent1->mEventRule = AccEvent::eDoNotEmit;
}

void
nsAccEventQueue::CoalesceReorderEventsFromSameTree(AccEvent* aAccEvent,
                                                   AccEvent* aDescendantAccEvent)
{
  // Do not emit descendant event if this event is unconditional.
  AccReorderEvent* reorderEvent = downcast_accEvent(aAccEvent);
  if (reorderEvent->IsUnconditionalEvent())
    aDescendantAccEvent->mEventRule = AccEvent::eDoNotEmit;
}

void
nsAccEventQueue::CoalesceTextChangeEventsFor(AccHideEvent* aTailEvent,
                                             AccHideEvent* aThisEvent)
{
  // XXX: we need a way to ignore SplitNode and JoinNode() when they do not
  // affect the text within the hypertext.

  AccTextChangeEvent* textEvent = aThisEvent->mTextChangeEvent;
  if (!textEvent)
    return;

  if (aThisEvent->mNextSibling == aTailEvent->mAccessible) {
    aTailEvent->mAccessible->AppendTextTo(textEvent->mModifiedText,
                                          0, PR_UINT32_MAX);

  } else if (aThisEvent->mPrevSibling == aTailEvent->mAccessible) {
    PRUint32 oldLen = textEvent->GetLength();
    aTailEvent->mAccessible->AppendTextTo(textEvent->mModifiedText,
                                          0, PR_UINT32_MAX);
    textEvent->mStart -= textEvent->GetLength() - oldLen;
  }

  aTailEvent->mTextChangeEvent.swap(aThisEvent->mTextChangeEvent);
}

void
nsAccEventQueue::CreateTextChangeEventFor(AccHideEvent* aEvent)
{
  nsRefPtr<nsHyperTextAccessible> textAccessible = do_QueryObject(
    GetAccService()->GetContainerAccessible(aEvent->mNode,
                                            aEvent->mAccessible->GetWeakShell()));
  if (!textAccessible)
    return;

  // Don't fire event for the first html:br in an editor.
  if (nsAccUtils::Role(aEvent->mAccessible) ==
      nsIAccessibleRole::ROLE_WHITESPACE) {
    nsCOMPtr<nsIEditor> editor;
    textAccessible->GetAssociatedEditor(getter_AddRefs(editor));
    if (editor) {
      PRBool isEmpty = PR_FALSE;
      editor->GetDocumentIsEmpty(&isEmpty);
      if (isEmpty)
        return;
    }
  }

  PRInt32 offset = textAccessible->GetChildOffset(aEvent->mAccessible);

  nsAutoString text;
  aEvent->mAccessible->AppendTextTo(text, 0, PR_UINT32_MAX);
  if (text.IsEmpty())
    return;

  aEvent->mTextChangeEvent =
    new AccTextChangeEvent(textAccessible, offset, text, PR_FALSE,
                           aEvent->mIsAsync,
                           aEvent->mIsFromUserInput ? eFromUserInput : eNoUserInput);
}
