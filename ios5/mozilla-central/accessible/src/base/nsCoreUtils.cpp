/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCoreUtils.h"

#include "nsIAccessibleTypes.h"

#include "nsAccessNode.h"

#include "nsIDocument.h"
#include "nsIDOMDocument.h"
#include "nsIDOMHTMLDocument.h"
#include "nsIDOMHTMLElement.h"
#include "nsRange.h"
#include "nsIDOMWindow.h"
#include "nsIDOMXULElement.h"
#include "nsIDocShell.h"
#include "nsIContentViewer.h"
#include "nsEventListenerManager.h"
#include "nsIPresShell.h"
#include "nsPresContext.h"
#include "nsIScrollableFrame.h"
#include "nsEventStateManager.h"
#include "nsISelectionPrivate.h"
#include "nsISelectionController.h"
#include "nsPIDOMWindow.h"
#include "nsGUIEvent.h"
#include "nsIView.h"
#include "nsLayoutUtils.h"

#include "nsComponentManagerUtils.h"
#include "nsIInterfaceRequestorUtils.h"
#include "mozilla/dom/Element.h"

////////////////////////////////////////////////////////////////////////////////
// nsCoreUtils
////////////////////////////////////////////////////////////////////////////////

bool
nsCoreUtils::HasClickListener(nsIContent *aContent)
{
  NS_ENSURE_TRUE(aContent, false);
  nsEventListenerManager* listenerManager =
    aContent->GetListenerManager(false);

  return listenerManager &&
    (listenerManager->HasListenersFor(NS_LITERAL_STRING("click")) ||
     listenerManager->HasListenersFor(NS_LITERAL_STRING("mousedown")) ||
     listenerManager->HasListenersFor(NS_LITERAL_STRING("mouseup")));
}

void
nsCoreUtils::DispatchClickEvent(nsITreeBoxObject *aTreeBoxObj,
                                PRInt32 aRowIndex, nsITreeColumn *aColumn,
                                const nsCString& aPseudoElt)
{
  nsCOMPtr<nsIDOMElement> tcElm;
  aTreeBoxObj->GetTreeBody(getter_AddRefs(tcElm));
  if (!tcElm)
    return;

  nsCOMPtr<nsIContent> tcContent(do_QueryInterface(tcElm));
  nsIDocument *document = tcContent->GetCurrentDoc();
  if (!document)
    return;

  nsIPresShell *presShell = nsnull;
  presShell = document->GetShell();
  if (!presShell)
    return;

  // Ensure row is visible.
  aTreeBoxObj->EnsureRowIsVisible(aRowIndex);

  // Calculate x and y coordinates.
  PRInt32 x = 0, y = 0, width = 0, height = 0;
  nsresult rv = aTreeBoxObj->GetCoordsForCellItem(aRowIndex, aColumn,
                                                  aPseudoElt,
                                                  &x, &y, &width, &height);
  if (NS_FAILED(rv))
    return;

  nsCOMPtr<nsIDOMXULElement> tcXULElm(do_QueryInterface(tcElm));
  nsCOMPtr<nsIBoxObject> tcBoxObj;
  tcXULElm->GetBoxObject(getter_AddRefs(tcBoxObj));

  PRInt32 tcX = 0;
  tcBoxObj->GetX(&tcX);

  PRInt32 tcY = 0;
  tcBoxObj->GetY(&tcY);

  // Dispatch mouse events.
  nsIFrame* tcFrame = tcContent->GetPrimaryFrame();
  nsIFrame* rootFrame = presShell->GetRootFrame();

  nsPoint offset;
  nsIWidget *rootWidget =
    rootFrame->GetViewExternal()->GetNearestWidget(&offset);

  nsPresContext* presContext = presShell->GetPresContext();

  PRInt32 cnvdX = presContext->CSSPixelsToDevPixels(tcX + x + 1) +
    presContext->AppUnitsToDevPixels(offset.x);
  PRInt32 cnvdY = presContext->CSSPixelsToDevPixels(tcY + y + 1) +
    presContext->AppUnitsToDevPixels(offset.y);

  DispatchMouseEvent(NS_MOUSE_BUTTON_DOWN, cnvdX, cnvdY,
                     tcContent, tcFrame, presShell, rootWidget);

  DispatchMouseEvent(NS_MOUSE_BUTTON_UP, cnvdX, cnvdY,
                     tcContent, tcFrame, presShell, rootWidget);
}

bool
nsCoreUtils::DispatchMouseEvent(PRUint32 aEventType,
                                nsIPresShell *aPresShell,
                                nsIContent *aContent)
{
  nsIFrame *frame = aContent->GetPrimaryFrame();
  if (!frame)
    return false;

  // Compute x and y coordinates.
  nsPoint point;
  nsCOMPtr<nsIWidget> widget = frame->GetNearestWidget(point);
  if (!widget)
    return false;

  nsSize size = frame->GetSize();

  nsPresContext* presContext = aPresShell->GetPresContext();

  PRInt32 x = presContext->AppUnitsToDevPixels(point.x + size.width / 2);
  PRInt32 y = presContext->AppUnitsToDevPixels(point.y + size.height / 2);

  // Fire mouse event.
  DispatchMouseEvent(aEventType, x, y, aContent, frame, aPresShell, widget);
  return true;
}

void
nsCoreUtils::DispatchMouseEvent(PRUint32 aEventType, PRInt32 aX, PRInt32 aY,
                                nsIContent *aContent, nsIFrame *aFrame,
                                nsIPresShell *aPresShell, nsIWidget *aRootWidget)
{
  nsMouseEvent event(true, aEventType, aRootWidget,
                     nsMouseEvent::eReal, nsMouseEvent::eNormal);

  event.refPoint = nsIntPoint(aX, aY);

  event.clickCount = 1;
  event.button = nsMouseEvent::eLeftButton;
  event.time = PR_IntervalNow();

  nsEventStatus status = nsEventStatus_eIgnore;
  aPresShell->HandleEventWithTarget(&event, aFrame, aContent, &status);
}

PRUint32
nsCoreUtils::GetAccessKeyFor(nsIContent *aContent)
{
  if (!aContent)
    return 0;

  // Accesskeys are registered by @accesskey attribute only. At first check
  // whether it is presented on the given element to avoid the slow
  // nsEventStateManager::GetRegisteredAccessKey() method.
  if (!aContent->HasAttr(kNameSpaceID_None, nsGkAtoms::accesskey))
    return 0;

  nsIPresShell* presShell = aContent->OwnerDoc()->GetShell();
  if (!presShell)
    return 0;

  nsPresContext *presContext = presShell->GetPresContext();
  if (!presContext)
    return 0;

  nsEventStateManager *esm = presContext->EventStateManager();
  if (!esm)
    return 0;

  return esm->GetRegisteredAccessKey(aContent);
}

nsIContent *
nsCoreUtils::GetDOMElementFor(nsIContent *aContent)
{
  if (aContent->IsElement())
    return aContent;

  if (aContent->IsNodeOfType(nsINode::eTEXT))
    return aContent->GetParent();

  return nsnull;
}

nsINode *
nsCoreUtils::GetDOMNodeFromDOMPoint(nsINode *aNode, PRUint32 aOffset)
{
  if (aNode && aNode->IsElement()) {
    PRUint32 childCount = aNode->GetChildCount();
    NS_ASSERTION(aOffset >= 0 && aOffset <= childCount,
                 "Wrong offset of the DOM point!");

    // The offset can be after last child of container node that means DOM point
    // is placed immediately after the last child. In this case use the DOM node
    // from the given DOM point is used as result node.
    if (aOffset != childCount)
      return aNode->GetChildAt(aOffset);
  }

  return aNode;
}

nsIContent*
nsCoreUtils::GetRoleContent(nsINode *aNode)
{
  nsCOMPtr<nsIContent> content(do_QueryInterface(aNode));
  if (!content) {
    nsCOMPtr<nsIDOMDocument> domDoc(do_QueryInterface(aNode));
    if (domDoc) {
      nsCOMPtr<nsIDOMHTMLDocument> htmlDoc(do_QueryInterface(aNode));
      if (htmlDoc) {
        nsCOMPtr<nsIDOMHTMLElement> bodyElement;
        htmlDoc->GetBody(getter_AddRefs(bodyElement));
        content = do_QueryInterface(bodyElement);
      }
      else {
        nsCOMPtr<nsIDOMElement> docElement;
        domDoc->GetDocumentElement(getter_AddRefs(docElement));
        content = do_QueryInterface(docElement);
      }
    }
  }

  return content;
}

bool
nsCoreUtils::IsAncestorOf(nsINode *aPossibleAncestorNode,
                          nsINode *aPossibleDescendantNode,
                          nsINode *aRootNode)
{
  NS_ENSURE_TRUE(aPossibleAncestorNode && aPossibleDescendantNode, false);

  nsINode *parentNode = aPossibleDescendantNode;
  while ((parentNode = parentNode->GetNodeParent()) &&
         parentNode != aRootNode) {
    if (parentNode == aPossibleAncestorNode)
      return true;
  }

  return false;
}

nsresult
nsCoreUtils::ScrollSubstringTo(nsIFrame* aFrame, nsRange* aRange,
                               PRUint32 aScrollType)
{
  nsIPresShell::ScrollAxis vertical, horizontal;
  ConvertScrollTypeToPercents(aScrollType, &vertical, &horizontal);

  return ScrollSubstringTo(aFrame, aRange, vertical, horizontal);
}

nsresult
nsCoreUtils::ScrollSubstringTo(nsIFrame* aFrame, nsRange* aRange,
                               nsIPresShell::ScrollAxis aVertical,
                               nsIPresShell::ScrollAxis aHorizontal)
{
  if (!aFrame)
    return NS_ERROR_FAILURE;

  nsPresContext *presContext = aFrame->PresContext();

  nsCOMPtr<nsISelectionController> selCon;
  aFrame->GetSelectionController(presContext, getter_AddRefs(selCon));
  NS_ENSURE_TRUE(selCon, NS_ERROR_FAILURE);

  nsCOMPtr<nsISelection> selection;
  selCon->GetSelection(nsISelectionController::SELECTION_ACCESSIBILITY,
                       getter_AddRefs(selection));

  nsCOMPtr<nsISelectionPrivate> privSel(do_QueryInterface(selection));
  selection->RemoveAllRanges();
  selection->AddRange(aRange);

  privSel->ScrollIntoViewInternal(
    nsISelectionController::SELECTION_ANCHOR_REGION,
    true, aVertical, aHorizontal);

  selection->CollapseToStart();

  return NS_OK;
}

void
nsCoreUtils::ScrollFrameToPoint(nsIFrame *aScrollableFrame,
                                nsIFrame *aFrame,
                                const nsIntPoint& aPoint)
{
  nsIScrollableFrame *scrollableFrame = do_QueryFrame(aScrollableFrame);
  if (!scrollableFrame)
    return;

  nsPresContext *presContext = aFrame->PresContext();

  nsIntRect frameRect = aFrame->GetScreenRectExternal();
  PRInt32 devDeltaX = aPoint.x - frameRect.x;
  PRInt32 devDeltaY = aPoint.y - frameRect.y;

  nsPoint deltaPoint;
  deltaPoint.x = presContext->DevPixelsToAppUnits(devDeltaX);
  deltaPoint.y = presContext->DevPixelsToAppUnits(devDeltaY);

  nsPoint scrollPoint = scrollableFrame->GetScrollPosition();
  scrollPoint -= deltaPoint;

  scrollableFrame->ScrollTo(scrollPoint, nsIScrollableFrame::INSTANT);
}

void
nsCoreUtils::ConvertScrollTypeToPercents(PRUint32 aScrollType,
                                         nsIPresShell::ScrollAxis *aVertical,
                                         nsIPresShell::ScrollAxis *aHorizontal)
{
  PRInt16 whereY, whereX;
  nsIPresShell::WhenToScroll whenY, whenX;
  switch (aScrollType)
  {
    case nsIAccessibleScrollType::SCROLL_TYPE_TOP_LEFT:
      whereY = nsIPresShell::SCROLL_TOP;
      whenY  = nsIPresShell::SCROLL_ALWAYS;
      whereX = nsIPresShell::SCROLL_LEFT;
      whenX  = nsIPresShell::SCROLL_ALWAYS;
      break;
    case nsIAccessibleScrollType::SCROLL_TYPE_BOTTOM_RIGHT:
      whereY = nsIPresShell::SCROLL_BOTTOM;
      whenY  = nsIPresShell::SCROLL_ALWAYS;
      whereX = nsIPresShell::SCROLL_RIGHT;
      whenX  = nsIPresShell::SCROLL_ALWAYS;
      break;
    case nsIAccessibleScrollType::SCROLL_TYPE_TOP_EDGE:
      whereY = nsIPresShell::SCROLL_TOP;
      whenY  = nsIPresShell::SCROLL_ALWAYS;
      whereX = nsIPresShell::SCROLL_MINIMUM;
      whenX  = nsIPresShell::SCROLL_IF_NOT_FULLY_VISIBLE;
      break;
    case nsIAccessibleScrollType::SCROLL_TYPE_BOTTOM_EDGE:
      whereY = nsIPresShell::SCROLL_BOTTOM;
      whenY  = nsIPresShell::SCROLL_ALWAYS;
      whereX = nsIPresShell::SCROLL_MINIMUM;
      whenX  = nsIPresShell::SCROLL_IF_NOT_FULLY_VISIBLE;
      break;
    case nsIAccessibleScrollType::SCROLL_TYPE_LEFT_EDGE:
      whereY = nsIPresShell::SCROLL_MINIMUM;
      whenY  = nsIPresShell::SCROLL_IF_NOT_FULLY_VISIBLE;
      whereX = nsIPresShell::SCROLL_LEFT;
      whenX  = nsIPresShell::SCROLL_ALWAYS;
      break;
    case nsIAccessibleScrollType::SCROLL_TYPE_RIGHT_EDGE:
      whereY = nsIPresShell::SCROLL_MINIMUM;
      whenY  = nsIPresShell::SCROLL_IF_NOT_FULLY_VISIBLE;
      whereX = nsIPresShell::SCROLL_RIGHT;
      whenX  = nsIPresShell::SCROLL_ALWAYS;
      break;
    default:
      whereY = nsIPresShell::SCROLL_MINIMUM;
      whenY  = nsIPresShell::SCROLL_IF_NOT_FULLY_VISIBLE;
      whereX = nsIPresShell::SCROLL_MINIMUM;
      whenX  = nsIPresShell::SCROLL_IF_NOT_FULLY_VISIBLE;
  }
  *aVertical = nsIPresShell::ScrollAxis(whereY, whenY);
  *aHorizontal = nsIPresShell::ScrollAxis(whereX, whenX);
}

nsIntPoint
nsCoreUtils::GetScreenCoordsForWindow(nsINode *aNode)
{
  nsIntPoint coords(0, 0);
  nsCOMPtr<nsIDocShellTreeItem> treeItem(GetDocShellTreeItemFor(aNode));
  if (!treeItem)
    return coords;

  nsCOMPtr<nsIDocShellTreeItem> rootTreeItem;
  treeItem->GetRootTreeItem(getter_AddRefs(rootTreeItem));
  nsCOMPtr<nsIDOMDocument> domDoc = do_GetInterface(rootTreeItem);
  if (!domDoc)
    return coords;

  nsCOMPtr<nsIDOMWindow> window;
  domDoc->GetDefaultView(getter_AddRefs(window));
  if (!window)
    return coords;

  window->GetScreenX(&coords.x);
  window->GetScreenY(&coords.y);
  return coords;
}

already_AddRefed<nsIDocShellTreeItem>
nsCoreUtils::GetDocShellTreeItemFor(nsINode *aNode)
{
  if (!aNode)
    return nsnull;

  nsCOMPtr<nsISupports> container = aNode->OwnerDoc()->GetContainer();
  nsIDocShellTreeItem *docShellTreeItem = nsnull;
  if (container)
    CallQueryInterface(container, &docShellTreeItem);

  return docShellTreeItem;
}

bool
nsCoreUtils::IsRootDocument(nsIDocument *aDocument)
{
  nsCOMPtr<nsISupports> container = aDocument->GetContainer();
  nsCOMPtr<nsIDocShellTreeItem> docShellTreeItem =
    do_QueryInterface(container);
  NS_ASSERTION(docShellTreeItem, "No document shell for document!");

  nsCOMPtr<nsIDocShellTreeItem> parentTreeItem;
  docShellTreeItem->GetParent(getter_AddRefs(parentTreeItem));

  return !parentTreeItem;
}

bool
nsCoreUtils::IsContentDocument(nsIDocument *aDocument)
{
  nsCOMPtr<nsISupports> container = aDocument->GetContainer();
  nsCOMPtr<nsIDocShellTreeItem> docShellTreeItem =
    do_QueryInterface(container);
  NS_ASSERTION(docShellTreeItem, "No document shell tree item for document!");

  PRInt32 contentType;
  docShellTreeItem->GetItemType(&contentType);
  return (contentType == nsIDocShellTreeItem::typeContent);
}

bool
nsCoreUtils::IsTabDocument(nsIDocument* aDocumentNode)
{
  nsCOMPtr<nsISupports> container = aDocumentNode->GetContainer();
  nsCOMPtr<nsIDocShellTreeItem> treeItem(do_QueryInterface(container));

  nsCOMPtr<nsIDocShellTreeItem> parentTreeItem;
  treeItem->GetParent(getter_AddRefs(parentTreeItem));

  // Tab document running in own process doesn't have parent.
  if (XRE_GetProcessType() == GeckoProcessType_Content)
    return !parentTreeItem;

  // Parent of docshell for tab document running in chrome process is root.
  nsCOMPtr<nsIDocShellTreeItem> rootTreeItem;
  treeItem->GetRootTreeItem(getter_AddRefs(rootTreeItem));

  return parentTreeItem == rootTreeItem;
}

bool
nsCoreUtils::IsErrorPage(nsIDocument *aDocument)
{
  nsIURI *uri = aDocument->GetDocumentURI();
  bool isAboutScheme = false;
  uri->SchemeIs("about", &isAboutScheme);
  if (!isAboutScheme)
    return false;

  nsCAutoString path;
  uri->GetPath(path);

  NS_NAMED_LITERAL_CSTRING(neterror, "neterror");
  NS_NAMED_LITERAL_CSTRING(certerror, "certerror");

  return StringBeginsWith(path, neterror) || StringBeginsWith(path, certerror);
}

already_AddRefed<nsIDOMNode>
nsCoreUtils::GetDOMNodeForContainer(nsIDocShellTreeItem *aContainer)
{
  nsCOMPtr<nsIDocShell> shell = do_QueryInterface(aContainer);

  nsCOMPtr<nsIContentViewer> cv;
  shell->GetContentViewer(getter_AddRefs(cv));

  if (!cv)
    return nsnull;

  nsIDocument* doc = cv->GetDocument();
  if (!doc)
    return nsnull;

  nsIDOMNode* node = nsnull;
  CallQueryInterface(doc, &node);
  return node;
}

bool
nsCoreUtils::GetID(nsIContent *aContent, nsAString& aID)
{
  nsIAtom *idAttribute = aContent->GetIDAttributeName();
  return idAttribute ? aContent->GetAttr(kNameSpaceID_None, idAttribute, aID) : false;
}

bool
nsCoreUtils::GetUIntAttr(nsIContent *aContent, nsIAtom *aAttr, PRInt32 *aUInt)
{
  nsAutoString value;
  aContent->GetAttr(kNameSpaceID_None, aAttr, value);
  if (!value.IsEmpty()) {
    PRInt32 error = NS_OK;
    PRInt32 integer = value.ToInteger(&error);
    if (NS_SUCCEEDED(error) && integer > 0) {
      *aUInt = integer;
      return true;
    }
  }

  return false;
}

bool
nsCoreUtils::IsXLink(nsIContent *aContent)
{
  if (!aContent)
    return false;

  return aContent->AttrValueIs(kNameSpaceID_XLink, nsGkAtoms::type,
                               nsGkAtoms::simple, eCaseMatters) &&
    aContent->HasAttr(kNameSpaceID_XLink, nsGkAtoms::href);
}

void
nsCoreUtils::GetLanguageFor(nsIContent *aContent, nsIContent *aRootContent,
                            nsAString& aLanguage)
{
  aLanguage.Truncate();

  nsIContent *walkUp = aContent;
  while (walkUp && walkUp != aRootContent &&
         !walkUp->GetAttr(kNameSpaceID_None, nsGkAtoms::lang, aLanguage))
    walkUp = walkUp->GetParent();
}

already_AddRefed<nsIBoxObject>
nsCoreUtils::GetTreeBodyBoxObject(nsITreeBoxObject *aTreeBoxObj)
{
  nsCOMPtr<nsIDOMElement> tcElm;
  aTreeBoxObj->GetTreeBody(getter_AddRefs(tcElm));
  nsCOMPtr<nsIDOMXULElement> tcXULElm(do_QueryInterface(tcElm));
  if (!tcXULElm)
    return nsnull;

  nsIBoxObject *boxObj = nsnull;
  tcXULElm->GetBoxObject(&boxObj);
  return boxObj;
}

already_AddRefed<nsITreeBoxObject>
nsCoreUtils::GetTreeBoxObject(nsIContent *aContent)
{
  // Find DOMNode's parents recursively until reach the <tree> tag
  nsIContent* currentContent = aContent;
  while (currentContent) {
    if (currentContent->NodeInfo()->Equals(nsGkAtoms::tree,
                                           kNameSpaceID_XUL)) {
      // We will get the nsITreeBoxObject from the tree node
      nsCOMPtr<nsIDOMXULElement> xulElement(do_QueryInterface(currentContent));
      if (xulElement) {
        nsCOMPtr<nsIBoxObject> box;
        xulElement->GetBoxObject(getter_AddRefs(box));
        nsCOMPtr<nsITreeBoxObject> treeBox(do_QueryInterface(box));
        if (treeBox)
          return treeBox.forget();
      }
    }
    currentContent = currentContent->GetParent();
  }

  return nsnull;
}

already_AddRefed<nsITreeColumn>
nsCoreUtils::GetFirstSensibleColumn(nsITreeBoxObject *aTree)
{
  nsCOMPtr<nsITreeColumns> cols;
  aTree->GetColumns(getter_AddRefs(cols));
  if (!cols)
    return nsnull;

  nsCOMPtr<nsITreeColumn> column;
  cols->GetFirstColumn(getter_AddRefs(column));
  if (column && IsColumnHidden(column))
    return GetNextSensibleColumn(column);

  return column.forget();
}

PRUint32
nsCoreUtils::GetSensibleColumnCount(nsITreeBoxObject *aTree)
{
  PRUint32 count = 0;

  nsCOMPtr<nsITreeColumns> cols;
  aTree->GetColumns(getter_AddRefs(cols));
  if (!cols)
    return count;

  nsCOMPtr<nsITreeColumn> column;
  cols->GetFirstColumn(getter_AddRefs(column));

  while (column) {
    if (!IsColumnHidden(column))
      count++;

    nsCOMPtr<nsITreeColumn> nextColumn;
    column->GetNext(getter_AddRefs(nextColumn));
    column.swap(nextColumn);
  }

  return count;
}

already_AddRefed<nsITreeColumn>
nsCoreUtils::GetSensibleColumnAt(nsITreeBoxObject *aTree, PRUint32 aIndex)
{
  PRUint32 idx = aIndex;

  nsCOMPtr<nsITreeColumn> column = GetFirstSensibleColumn(aTree);
  while (column) {
    if (idx == 0)
      return column.forget();

    idx--;
    column = GetNextSensibleColumn(column);
  }

  return nsnull;
}

already_AddRefed<nsITreeColumn>
nsCoreUtils::GetNextSensibleColumn(nsITreeColumn *aColumn)
{
  nsCOMPtr<nsITreeColumn> nextColumn;
  aColumn->GetNext(getter_AddRefs(nextColumn));

  while (nextColumn && IsColumnHidden(nextColumn)) {
    nsCOMPtr<nsITreeColumn> tempColumn;
    nextColumn->GetNext(getter_AddRefs(tempColumn));
    nextColumn.swap(tempColumn);
  }

  return nextColumn.forget();
}

already_AddRefed<nsITreeColumn>
nsCoreUtils::GetPreviousSensibleColumn(nsITreeColumn *aColumn)
{
  nsCOMPtr<nsITreeColumn> prevColumn;
  aColumn->GetPrevious(getter_AddRefs(prevColumn));

  while (prevColumn && IsColumnHidden(prevColumn)) {
    nsCOMPtr<nsITreeColumn> tempColumn;
    prevColumn->GetPrevious(getter_AddRefs(tempColumn));
    prevColumn.swap(tempColumn);
  }

  return prevColumn.forget();
}

bool
nsCoreUtils::IsColumnHidden(nsITreeColumn *aColumn)
{
  nsCOMPtr<nsIDOMElement> element;
  aColumn->GetElement(getter_AddRefs(element));
  nsCOMPtr<nsIContent> content = do_QueryInterface(element);
  return content->AttrValueIs(kNameSpaceID_None, nsGkAtoms::hidden,
                              nsGkAtoms::_true, eCaseMatters);
}

void
nsCoreUtils::ScrollTo(nsIPresShell* aPresShell, nsIContent* aContent,
                      PRUint32 aScrollType)
{
  nsIPresShell::ScrollAxis vertical, horizontal;
  ConvertScrollTypeToPercents(aScrollType, &vertical, &horizontal);
  aPresShell->ScrollContentIntoView(aContent, vertical, horizontal,
                                    nsIPresShell::SCROLL_OVERFLOW_HIDDEN);
}


////////////////////////////////////////////////////////////////////////////////
// nsAccessibleDOMStringList
////////////////////////////////////////////////////////////////////////////////

NS_IMPL_ISUPPORTS1(nsAccessibleDOMStringList, nsIDOMDOMStringList)

NS_IMETHODIMP
nsAccessibleDOMStringList::Item(PRUint32 aIndex, nsAString& aResult)
{
  if (aIndex >= mNames.Length())
    SetDOMStringToNull(aResult);
  else
    aResult = mNames.ElementAt(aIndex);

  return NS_OK;
}

NS_IMETHODIMP
nsAccessibleDOMStringList::GetLength(PRUint32* aLength)
{
  *aLength = mNames.Length();

  return NS_OK;
}

NS_IMETHODIMP
nsAccessibleDOMStringList::Contains(const nsAString& aString, bool* aResult)
{
  *aResult = mNames.Contains(aString);

  return NS_OK;
}

