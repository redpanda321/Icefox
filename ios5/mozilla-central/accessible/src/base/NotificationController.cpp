/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NotificationController.h"

#include "Accessible-inl.h"
#include "nsAccessibilityService.h"
#include "nsAccUtils.h"
#include "nsCoreUtils.h"
#include "DocAccessible.h"
#include "nsEventShell.h"
#include "FocusManager.h"
#include "Role.h"
#include "TextLeafAccessible.h"
#include "TextUpdater.h"

#ifdef DEBUG
#include "Logging.h"
#endif

#include "mozilla/dom/Element.h"

using namespace mozilla::a11y;

// Defines the number of selection add/remove events in the queue when they
// aren't packed into single selection within event.
const unsigned int kSelChangeCountToPack = 5;

////////////////////////////////////////////////////////////////////////////////
// NotificationCollector
////////////////////////////////////////////////////////////////////////////////

NotificationController::NotificationController(DocAccessible* aDocument,
                                               nsIPresShell* aPresShell) :
  mObservingState(eNotObservingRefresh), mDocument(aDocument),
  mPresShell(aPresShell)
{
  mTextHash.Init();

  // Schedule initial accessible tree construction.
  ScheduleProcessing();
}

NotificationController::~NotificationController()
{
  NS_ASSERTION(!mDocument, "Controller wasn't shutdown properly!");
  if (mDocument)
    Shutdown();
}

////////////////////////////////////////////////////////////////////////////////
// NotificationCollector: AddRef/Release and cycle collection

NS_IMPL_ADDREF(NotificationController)
NS_IMPL_RELEASE(NotificationController)

NS_IMPL_CYCLE_COLLECTION_NATIVE_CLASS(NotificationController)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_NATIVE(NotificationController)
  if (tmp->mDocument)
    tmp->Shutdown();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NATIVE_BEGIN(NotificationController)
  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mDocument");
  cb.NoteXPCOMChild(static_cast<nsIAccessible*>(tmp->mDocument.get()));
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSTARRAY_MEMBER(mHangingChildDocuments,
                                                    DocAccessible)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSTARRAY_MEMBER(mContentInsertions,
                                                    ContentInsertion)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSTARRAY_MEMBER(mEvents, AccEvent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(NotificationController, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(NotificationController, Release)

////////////////////////////////////////////////////////////////////////////////
// NotificationCollector: public

void
NotificationController::Shutdown()
{
  if (mObservingState != eNotObservingRefresh &&
      mPresShell->RemoveRefreshObserver(this, Flush_Display)) {
    mObservingState = eNotObservingRefresh;
  }

  // Shutdown handling child documents.
  PRInt32 childDocCount = mHangingChildDocuments.Length();
  for (PRInt32 idx = childDocCount - 1; idx >= 0; idx--) {
    if (!mHangingChildDocuments[idx]->IsDefunct())
      mHangingChildDocuments[idx]->Shutdown();
  }

  mHangingChildDocuments.Clear();

  mDocument = nsnull;
  mPresShell = nsnull;

  mTextHash.Clear();
  mContentInsertions.Clear();
  mNotifications.Clear();
  mEvents.Clear();
}

void
NotificationController::QueueEvent(AccEvent* aEvent)
{
  if (!mEvents.AppendElement(aEvent))
    return;

  // Filter events.
  CoalesceEvents();

  // Associate text change with hide event if it wasn't stolen from hiding
  // siblings during coalescence.
  AccMutationEvent* showOrHideEvent = downcast_accEvent(aEvent);
  if (showOrHideEvent && !showOrHideEvent->mTextChangeEvent)
    CreateTextChangeEventFor(showOrHideEvent);

  ScheduleProcessing();
}

void
NotificationController::ScheduleChildDocBinding(DocAccessible* aDocument)
{
  // Schedule child document binding to the tree.
  mHangingChildDocuments.AppendElement(aDocument);
  ScheduleProcessing();
}

void
NotificationController::ScheduleContentInsertion(Accessible* aContainer,
                                                 nsIContent* aStartChildNode,
                                                 nsIContent* aEndChildNode)
{
  nsRefPtr<ContentInsertion> insertion = new ContentInsertion(mDocument,
                                                              aContainer);
  if (insertion && insertion->InitChildList(aStartChildNode, aEndChildNode) &&
      mContentInsertions.AppendElement(insertion)) {
    ScheduleProcessing();
  }
}

////////////////////////////////////////////////////////////////////////////////
// NotificationCollector: protected

void
NotificationController::ScheduleProcessing()
{
  // If notification flush isn't planed yet start notification flush
  // asynchronously (after style and layout).
  if (mObservingState == eNotObservingRefresh) {
    if (mPresShell->AddRefreshObserver(this, Flush_Display))
      mObservingState = eRefreshObserving;
  }
}

bool
NotificationController::IsUpdatePending()
{
  return mPresShell->IsLayoutFlushObserver() ||
    mObservingState == eRefreshProcessingForUpdate ||
    mContentInsertions.Length() != 0 || mNotifications.Length() != 0 ||
    mTextHash.Count() != 0 ||
    !mDocument->HasLoadState(DocAccessible::eTreeConstructed);
}

////////////////////////////////////////////////////////////////////////////////
// NotificationCollector: private

void
NotificationController::WillRefresh(mozilla::TimeStamp aTime)
{
  // If the document accessible that notification collector was created for is
  // now shut down, don't process notifications anymore.
  NS_ASSERTION(mDocument,
               "The document was shut down while refresh observer is attached!");
  if (!mDocument)
    return;

  // Any generic notifications should be queued if we're processing content
  // insertions or generic notifications.
  mObservingState = eRefreshProcessingForUpdate;

  // Initial accessible tree construction.
  if (!mDocument->HasLoadState(DocAccessible::eTreeConstructed)) {
    // If document is not bound to parent at this point then the document is not
    // ready yet (process notifications later).
    if (!mDocument->IsBoundToParent()) {
      mObservingState = eRefreshObserving;
      return;
    }

#ifdef DEBUG
    if (logging::IsEnabled(logging::eTree)) {
      logging::MsgBegin("TREE", "initial tree created");
      logging::Address("document", mDocument);
      logging::MsgEnd();
    }
#endif

    mDocument->DoInitialUpdate();

    NS_ASSERTION(mContentInsertions.Length() == 0,
                 "Pending content insertions while initial accessible tree isn't created!");
  }

  // Process content inserted notifications to update the tree. Process other
  // notifications like DOM events and then flush event queue. If any new
  // notifications are queued during this processing then they will be processed
  // on next refresh. If notification processing queues up new events then they
  // are processed in this refresh. If events processing queues up new events
  // then new events are processed on next refresh.
  // Note: notification processing or event handling may shut down the owning
  // document accessible.

  // Process only currently queued content inserted notifications.
  nsTArray<nsRefPtr<ContentInsertion> > contentInsertions;
  contentInsertions.SwapElements(mContentInsertions);

  PRUint32 insertionCount = contentInsertions.Length();
  for (PRUint32 idx = 0; idx < insertionCount; idx++) {
    contentInsertions[idx]->Process();
    if (!mDocument)
      return;
  }

  // Process rendered text change notifications.
  mTextHash.EnumerateEntries(TextEnumerator, mDocument);
  mTextHash.Clear();

  // Bind hanging child documents.
  PRUint32 hangingDocCnt = mHangingChildDocuments.Length();
  for (PRUint32 idx = 0; idx < hangingDocCnt; idx++) {
    DocAccessible* childDoc = mHangingChildDocuments[idx];
    if (childDoc->IsDefunct())
      continue;

    nsIContent* ownerContent = mDocument->GetDocumentNode()->
      FindContentForSubDocument(childDoc->GetDocumentNode());
    if (ownerContent) {
      Accessible* outerDocAcc = mDocument->GetAccessible(ownerContent);
      if (outerDocAcc && outerDocAcc->AppendChild(childDoc)) {
        if (mDocument->AppendChildDocument(childDoc))
          continue;

        outerDocAcc->RemoveChild(childDoc);
      }

      // Failed to bind the child document, destroy it.
      childDoc->Shutdown();
    }
  }
  mHangingChildDocuments.Clear();

  // If the document is ready and all its subdocuments are completely loaded
  // then process the document load.
  if (mDocument->HasLoadState(DocAccessible::eReady) &&
      !mDocument->HasLoadState(DocAccessible::eCompletelyLoaded) &&
      hangingDocCnt == 0) {
    PRUint32 childDocCnt = mDocument->ChildDocumentCount(), childDocIdx = 0;
    for (; childDocIdx < childDocCnt; childDocIdx++) {
      DocAccessible* childDoc = mDocument->GetChildDocumentAt(childDocIdx);
      if (!childDoc->HasLoadState(DocAccessible::eCompletelyLoaded))
        break;
    }

    if (childDocIdx == childDocCnt) {
      mDocument->ProcessLoad();
      if (!mDocument)
        return;
    }
  }

  // Process only currently queued generic notifications.
  nsTArray < nsRefPtr<Notification> > notifications;
  notifications.SwapElements(mNotifications);

  PRUint32 notificationCount = notifications.Length();
  for (PRUint32 idx = 0; idx < notificationCount; idx++) {
    notifications[idx]->Process();
    if (!mDocument)
      return;
  }

  // Process invalidation list of the document after all accessible tree
  // modification are done.
  mDocument->ProcessInvalidationList();

  // If a generic notification occurs after this point then we may be allowed to
  // process it synchronously.
  mObservingState = eRefreshObserving;

  // Process only currently queued events.
  nsTArray<nsRefPtr<AccEvent> > events;
  events.SwapElements(mEvents);

  PRUint32 eventCount = events.Length();
#ifdef DEBUG
  if (eventCount > 0 && logging::IsEnabled(logging::eEvents)) {
    logging::MsgBegin("EVENTS", "events processing");
    logging::Address("document", mDocument);
    logging::MsgEnd();
  }
#endif

  for (PRUint32 idx = 0; idx < eventCount; idx++) {
    AccEvent* accEvent = events[idx];
    if (accEvent->mEventRule != AccEvent::eDoNotEmit) {
      Accessible* target = accEvent->GetAccessible();
      if (!target || target->IsDefunct())
        continue;

      // Dispatch the focus event if target is still focused.
      if (accEvent->mEventType == nsIAccessibleEvent::EVENT_FOCUS) {
        FocusMgr()->ProcessFocusEvent(accEvent);
        continue;
      }

      mDocument->ProcessPendingEvent(accEvent);

      // Fire text change event caused by tree mutation.
      AccMutationEvent* showOrHideEvent = downcast_accEvent(accEvent);
      if (showOrHideEvent) {
        if (showOrHideEvent->mTextChangeEvent)
          mDocument->ProcessPendingEvent(showOrHideEvent->mTextChangeEvent);
      }
    }
    if (!mDocument)
      return;
  }

  // Stop further processing if there are no new notifications of any kind or
  // events and document load is processed.
  if (mContentInsertions.Length() == 0 && mNotifications.Length() == 0 &&
      mEvents.Length() == 0 && mTextHash.Count() == 0 &&
      mHangingChildDocuments.Length() == 0 &&
      mDocument->HasLoadState(DocAccessible::eCompletelyLoaded) &&
      mPresShell->RemoveRefreshObserver(this, Flush_Display)) {
    mObservingState = eNotObservingRefresh;
  }
}

////////////////////////////////////////////////////////////////////////////////
// NotificationController: event queue

void
NotificationController::CoalesceEvents()
{
  PRUint32 numQueuedEvents = mEvents.Length();
  PRInt32 tail = numQueuedEvents - 1;
  AccEvent* tailEvent = mEvents[tail];

  switch(tailEvent->mEventRule) {
    case AccEvent::eCoalesceFromSameSubtree:
    {
      // No node means this is application accessible (which is a subject of
      // reorder events), we do not coalesce events for it currently.
      if (!tailEvent->mNode)
        return;

      for (PRInt32 index = tail - 1; index >= 0; index--) {
        AccEvent* thisEvent = mEvents[index];

        if (thisEvent->mEventType != tailEvent->mEventType)
          continue; // Different type

        // Skip event for application accessible since no coalescence for it
        // is supported. Ignore events from different documents since we don't
        // coalesce them.
        if (!thisEvent->mNode ||
            thisEvent->mNode->OwnerDoc() != tailEvent->mNode->OwnerDoc())
          continue;

        // Coalesce earlier event for the same target.
        if (thisEvent->mNode == tailEvent->mNode) {
          thisEvent->mEventRule = AccEvent::eDoNotEmit;
          return;
        }

        // If event queue contains an event of the same type and having target
        // that is sibling of target of newly appended event then apply its
        // event rule to the newly appended event.

        // Coalesce hide and show events for sibling targets.
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
        } else if (tailEvent->mEventType == nsIAccessibleEvent::EVENT_SHOW) {
          if (thisEvent->mAccessible->Parent() ==
              tailEvent->mAccessible->Parent()) {
            tailEvent->mEventRule = thisEvent->mEventRule;

            // Coalesce text change events for show events.
            if (tailEvent->mEventRule != AccEvent::eDoNotEmit) {
              AccShowEvent* tailShowEvent = downcast_accEvent(tailEvent);
              AccShowEvent* thisShowEvent = downcast_accEvent(thisEvent);
              CoalesceTextChangeEventsFor(tailShowEvent, thisShowEvent);
            }

            return;
          }
        }

        // Ignore events unattached from DOM since we don't coalesce them.
        if (!thisEvent->mNode->IsInDoc())
          continue;

        // Coalesce events by sibling targets (this is a case for reorder
        // events).
        if (thisEvent->mNode->GetNodeParent() ==
            tailEvent->mNode->GetNodeParent()) {
          tailEvent->mEventRule = thisEvent->mEventRule;
          return;
        }

        // This and tail events can be anywhere in the tree, make assumptions
        // for mutation events.

        // Coalesce tail event if tail node is descendant of this node. Stop
        // processing if tail event is coalesced since all possible descendants
        // of this node was coalesced before.
        // Note: more older hide event target (thisNode) can't contain recent
        // hide event target (tailNode), i.e. be ancestor of tailNode. Skip
        // this check for hide events.
        if (tailEvent->mEventType != nsIAccessibleEvent::EVENT_HIDE &&
            nsCoreUtils::IsAncestorOf(thisEvent->mNode, tailEvent->mNode)) {
          tailEvent->mEventRule = AccEvent::eDoNotEmit;
          return;
        }

        // If this node is a descendant of tail node then coalesce this event,
        // check other events in the queue. Do not emit thisEvent, also apply
        // this result to sibling nodes of thisNode.
        if (nsCoreUtils::IsAncestorOf(tailEvent->mNode, thisEvent->mNode)) {
          thisEvent->mEventRule = AccEvent::eDoNotEmit;
          ApplyToSiblings(0, index, thisEvent->mEventType,
                          thisEvent->mNode, AccEvent::eDoNotEmit);
          continue;
        }

      } // for (index)

    } break; // case eCoalesceFromSameSubtree

    case AccEvent::eCoalesceOfSameType:
    {
      // Coalesce old events by newer event.
      for (PRInt32 index = tail - 1; index >= 0; index--) {
        AccEvent* accEvent = mEvents[index];
        if (accEvent->mEventType == tailEvent->mEventType &&
            accEvent->mEventRule == tailEvent->mEventRule) {
          accEvent->mEventRule = AccEvent::eDoNotEmit;
          return;
        }
      }
    } break; // case eCoalesceOfSameType

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

    case AccEvent::eCoalesceSelectionChange:
    {
      AccSelChangeEvent* tailSelChangeEvent = downcast_accEvent(tailEvent);
      PRInt32 index = tail - 1;
      for (; index >= 0; index--) {
        AccEvent* thisEvent = mEvents[index];
        if (thisEvent->mEventRule == tailEvent->mEventRule) {
          AccSelChangeEvent* thisSelChangeEvent =
            downcast_accEvent(thisEvent);

          // Coalesce selection change events within same control.
          if (tailSelChangeEvent->mWidget == thisSelChangeEvent->mWidget) {
            CoalesceSelChangeEvents(tailSelChangeEvent, thisSelChangeEvent, index);
            return;
          }
        }
      }

    } break; // eCoalesceSelectionChange

    default:
      break; // case eAllowDupes, eDoNotEmit
  } // switch
}

void
NotificationController::ApplyToSiblings(PRUint32 aStart, PRUint32 aEnd,
                                        PRUint32 aEventType, nsINode* aNode,
                                        AccEvent::EEventRule aEventRule)
{
  for (PRUint32 index = aStart; index < aEnd; index ++) {
    AccEvent* accEvent = mEvents[index];
    if (accEvent->mEventType == aEventType &&
        accEvent->mEventRule != AccEvent::eDoNotEmit && accEvent->mNode &&
        accEvent->mNode->GetNodeParent() == aNode->GetNodeParent()) {
      accEvent->mEventRule = aEventRule;
    }
  }
}

void
NotificationController::CoalesceSelChangeEvents(AccSelChangeEvent* aTailEvent,
                                                AccSelChangeEvent* aThisEvent,
                                                PRInt32 aThisIndex)
{
  aTailEvent->mPreceedingCount = aThisEvent->mPreceedingCount + 1;

  // Pack all preceding events into single selection within event
  // when we receive too much selection add/remove events.
  if (aTailEvent->mPreceedingCount >= kSelChangeCountToPack) {
    aTailEvent->mEventType = nsIAccessibleEvent::EVENT_SELECTION_WITHIN;
    aTailEvent->mAccessible = aTailEvent->mWidget;
    aThisEvent->mEventRule = AccEvent::eDoNotEmit;

    // Do not emit any preceding selection events for same widget if they
    // weren't coalesced yet.
    if (aThisEvent->mEventType != nsIAccessibleEvent::EVENT_SELECTION_WITHIN) {
      for (PRInt32 jdx = aThisIndex - 1; jdx >= 0; jdx--) {
        AccEvent* prevEvent = mEvents[jdx];
        if (prevEvent->mEventRule == aTailEvent->mEventRule) {
          AccSelChangeEvent* prevSelChangeEvent =
            downcast_accEvent(prevEvent);
          if (prevSelChangeEvent->mWidget == aTailEvent->mWidget)
            prevSelChangeEvent->mEventRule = AccEvent::eDoNotEmit;
        }
      }
    }
    return;
  }

  // Pack sequential selection remove and selection add events into
  // single selection change event.
  if (aTailEvent->mPreceedingCount == 1 &&
      aTailEvent->mItem != aThisEvent->mItem) {
    if (aTailEvent->mSelChangeType == AccSelChangeEvent::eSelectionAdd &&
        aThisEvent->mSelChangeType == AccSelChangeEvent::eSelectionRemove) {
      aThisEvent->mEventRule = AccEvent::eDoNotEmit;
      aTailEvent->mEventType = nsIAccessibleEvent::EVENT_SELECTION;
      aTailEvent->mPackedEvent = aThisEvent;
      return;
    }

    if (aThisEvent->mSelChangeType == AccSelChangeEvent::eSelectionAdd &&
        aTailEvent->mSelChangeType == AccSelChangeEvent::eSelectionRemove) {
      aTailEvent->mEventRule = AccEvent::eDoNotEmit;
      aThisEvent->mEventType = nsIAccessibleEvent::EVENT_SELECTION;
      aThisEvent->mPackedEvent = aThisEvent;
      return;
    }
  }

  // Unpack the packed selection change event because we've got one
  // more selection add/remove.
  if (aThisEvent->mEventType == nsIAccessibleEvent::EVENT_SELECTION) {
    if (aThisEvent->mPackedEvent) {
      aThisEvent->mPackedEvent->mEventType =
        aThisEvent->mPackedEvent->mSelChangeType == AccSelChangeEvent::eSelectionAdd ?
          nsIAccessibleEvent::EVENT_SELECTION_ADD :
          nsIAccessibleEvent::EVENT_SELECTION_REMOVE;

      aThisEvent->mPackedEvent->mEventRule =
        AccEvent::eCoalesceSelectionChange;

      aThisEvent->mPackedEvent = nsnull;
    }

    aThisEvent->mEventType =
      aThisEvent->mSelChangeType == AccSelChangeEvent::eSelectionAdd ?
        nsIAccessibleEvent::EVENT_SELECTION_ADD :
        nsIAccessibleEvent::EVENT_SELECTION_REMOVE;

    return;
  }

  // Convert into selection add since control has single selection but other
  // selection events for this control are queued.
  if (aTailEvent->mEventType == nsIAccessibleEvent::EVENT_SELECTION)
    aTailEvent->mEventType = nsIAccessibleEvent::EVENT_SELECTION_ADD;
}

void
NotificationController::CoalesceTextChangeEventsFor(AccHideEvent* aTailEvent,
                                                    AccHideEvent* aThisEvent)
{
  // XXX: we need a way to ignore SplitNode and JoinNode() when they do not
  // affect the text within the hypertext.

  AccTextChangeEvent* textEvent = aThisEvent->mTextChangeEvent;
  if (!textEvent)
    return;

  if (aThisEvent->mNextSibling == aTailEvent->mAccessible) {
    aTailEvent->mAccessible->AppendTextTo(textEvent->mModifiedText);

  } else if (aThisEvent->mPrevSibling == aTailEvent->mAccessible) {
    PRUint32 oldLen = textEvent->GetLength();
    aTailEvent->mAccessible->AppendTextTo(textEvent->mModifiedText);
    textEvent->mStart -= textEvent->GetLength() - oldLen;
  }

  aTailEvent->mTextChangeEvent.swap(aThisEvent->mTextChangeEvent);
}

void
NotificationController::CoalesceTextChangeEventsFor(AccShowEvent* aTailEvent,
                                                    AccShowEvent* aThisEvent)
{
  AccTextChangeEvent* textEvent = aThisEvent->mTextChangeEvent;
  if (!textEvent)
    return;

  if (aTailEvent->mAccessible->IndexInParent() ==
      aThisEvent->mAccessible->IndexInParent() + 1) {
    // If tail target was inserted after this target, i.e. tail target is next
    // sibling of this target.
    aTailEvent->mAccessible->AppendTextTo(textEvent->mModifiedText);

  } else if (aTailEvent->mAccessible->IndexInParent() ==
             aThisEvent->mAccessible->IndexInParent() -1) {
    // If tail target was inserted before this target, i.e. tail target is
    // previous sibling of this target.
    nsAutoString startText;
    aTailEvent->mAccessible->AppendTextTo(startText);
    textEvent->mModifiedText = startText + textEvent->mModifiedText;
    textEvent->mStart -= startText.Length();
  }

  aTailEvent->mTextChangeEvent.swap(aThisEvent->mTextChangeEvent);
}

void
NotificationController::CreateTextChangeEventFor(AccMutationEvent* aEvent)
{
  DocAccessible* document = aEvent->GetDocAccessible();
  Accessible* container = document->GetContainerAccessible(aEvent->mNode);
  if (!container)
    return;

  HyperTextAccessible* textAccessible = container->AsHyperText();
  if (!textAccessible)
    return;

  // Don't fire event for the first html:br in an editor.
  if (aEvent->mAccessible->Role() == roles::WHITESPACE) {
    nsCOMPtr<nsIEditor> editor = textAccessible->GetEditor();
    if (editor) {
      bool isEmpty = false;
      editor->GetDocumentIsEmpty(&isEmpty);
      if (isEmpty)
        return;
    }
  }

  PRInt32 offset = textAccessible->GetChildOffset(aEvent->mAccessible);

  nsAutoString text;
  aEvent->mAccessible->AppendTextTo(text);
  if (text.IsEmpty())
    return;

  aEvent->mTextChangeEvent =
    new AccTextChangeEvent(textAccessible, offset, text, aEvent->IsShow(),
                           aEvent->mIsFromUserInput ? eFromUserInput : eNoUserInput);
}

////////////////////////////////////////////////////////////////////////////////
// Notification controller: text leaf accessible text update

PLDHashOperator
NotificationController::TextEnumerator(nsCOMPtrHashKey<nsIContent>* aEntry,
                                       void* aUserArg)
{
  DocAccessible* document = static_cast<DocAccessible*>(aUserArg);
  nsIContent* textNode = aEntry->GetKey();
  Accessible* textAcc = document->GetAccessible(textNode);

  // If the text node is not in tree or doesn't have frame then this case should
  // have been handled already by content removal notifications.
  nsINode* containerNode = textNode->GetNodeParent();
  if (!containerNode) {
    NS_ASSERTION(!textAcc,
                 "Text node was removed but accessible is kept alive!");
    return PL_DHASH_NEXT;
  }

  nsIFrame* textFrame = textNode->GetPrimaryFrame();
  if (!textFrame) {
    NS_ASSERTION(!textAcc,
                 "Text node isn't rendered but accessible is kept alive!");
    return PL_DHASH_NEXT;
  }

  nsIContent* containerElm = containerNode->IsElement() ?
    containerNode->AsElement() : nsnull;

  nsAutoString text;
  textFrame->GetRenderedText(&text);

  // Remove text accessible if rendered text is empty.
  if (textAcc) {
    if (text.IsEmpty()) {
#ifdef DEBUG
      if (logging::IsEnabled(logging::eTree | logging::eText)) {
        logging::MsgBegin("TREE", "text node lost its content");
        logging::Node("container", containerElm);
        logging::Node("content", textNode);
        logging::MsgEnd();
      }
#endif

      document->ContentRemoved(containerElm, textNode);
      return PL_DHASH_NEXT;
    }

    // Update text of the accessible and fire text change events.
#ifdef DEBUG
    if (logging::IsEnabled(logging::eText)) {
      logging::MsgBegin("TEXT", "text may be changed");
      logging::Node("container", containerElm);
      logging::Node("content", textNode);
      logging::MsgEntry("old text '%s'",
                        NS_ConvertUTF16toUTF8(textAcc->AsTextLeaf()->Text()).get());
      logging::MsgEntry("new text: '%s'",
                        NS_ConvertUTF16toUTF8(text).get());
      logging::MsgEnd();
    }
#endif

    TextUpdater::Run(document, textAcc->AsTextLeaf(), text);
    return PL_DHASH_NEXT;
  }

  // Append an accessible if rendered text is not empty.
  if (!text.IsEmpty()) {
#ifdef DEBUG
    if (logging::IsEnabled(logging::eTree | logging::eText)) {
      logging::MsgBegin("TREE", "text node gains new content");
      logging::Node("container", containerElm);
      logging::Node("content", textNode);
      logging::MsgEnd();
    }
#endif

    // Make sure the text node is in accessible document still.
    Accessible* container = document->GetAccessibleOrContainer(containerNode);
    NS_ASSERTION(container,
                 "Text node having rendered text hasn't accessible document!");
    if (container) {
      nsTArray<nsCOMPtr<nsIContent> > insertedContents;
      insertedContents.AppendElement(textNode);
      document->ProcessContentInserted(container, &insertedContents);
    }
  }

  return PL_DHASH_NEXT;
}


////////////////////////////////////////////////////////////////////////////////
// NotificationController: content inserted notification

NotificationController::ContentInsertion::
  ContentInsertion(DocAccessible* aDocument, Accessible* aContainer) :
  mDocument(aDocument), mContainer(aContainer)
{
}

bool
NotificationController::ContentInsertion::
  InitChildList(nsIContent* aStartChildNode, nsIContent* aEndChildNode)
{
  bool haveToUpdate = false;

  nsIContent* node = aStartChildNode;
  while (node != aEndChildNode) {
    // Notification triggers for content insertion even if no content was
    // actually inserted, check if the given content has a frame to discard
    // this case early.
    if (node->GetPrimaryFrame()) {
      if (mInsertedContent.AppendElement(node))
        haveToUpdate = true;
    }

    node = node->GetNextSibling();
  }

  return haveToUpdate;
}

NS_IMPL_CYCLE_COLLECTION_NATIVE_CLASS(NotificationController::ContentInsertion)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_NATIVE(NotificationController::ContentInsertion)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mContainer)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NATIVE_BEGIN(NotificationController::ContentInsertion)
  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mContainer");
  cb.NoteXPCOMChild(static_cast<nsIAccessible*>(tmp->mContainer.get()));
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(NotificationController::ContentInsertion,
                                     AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(NotificationController::ContentInsertion,
                                       Release)

void
NotificationController::ContentInsertion::Process()
{
  mDocument->ProcessContentInserted(mContainer, &mInsertedContent);

  mDocument = nsnull;
  mContainer = nsnull;
  mInsertedContent.Clear();
}

