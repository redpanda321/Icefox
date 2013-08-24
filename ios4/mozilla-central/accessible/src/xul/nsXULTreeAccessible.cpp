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
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Author: Kyle Yuan (kyle.yuan@sun.com)
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

#include "nsXULTreeAccessible.h"

#include "nsAccCache.h"
#include "nsAccUtils.h"
#include "nsCoreUtils.h"
#include "nsDocAccessible.h"
#include "nsRelUtils.h"

#include "nsIDOMXULElement.h"
#include "nsIDOMXULMultSelectCntrlEl.h"
#include "nsIDOMXULTreeElement.h"
#include "nsITreeSelection.h"
#include "nsIMutableArray.h"
#include "nsComponentManagerUtils.h"

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeAccessible
////////////////////////////////////////////////////////////////////////////////

nsXULTreeAccessible::
  nsXULTreeAccessible(nsIContent *aContent, nsIWeakReference *aShell) :
  nsXULSelectableAccessible(aContent, aShell)
{
  mTree = nsCoreUtils::GetTreeBoxObject(aContent);
  if (mTree)
    mTree->GetView(getter_AddRefs(mTreeView));

  NS_ASSERTION(mTree && mTreeView, "Can't get mTree or mTreeView!\n");

  mAccessibleCache.Init(kDefaultTreeCacheSize);
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeAccessible: nsISupports and cycle collection implementation

NS_IMPL_CYCLE_COLLECTION_CLASS(nsXULTreeAccessible)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsXULTreeAccessible,
                                                  nsAccessible)
CycleCollectorTraverseCache(tmp->mAccessibleCache, &cb);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(nsXULTreeAccessible,
                                                nsAccessible)
ClearCache(tmp->mAccessibleCache);
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(nsXULTreeAccessible)
NS_INTERFACE_MAP_STATIC_AMBIGUOUS(nsXULTreeAccessible)
NS_INTERFACE_MAP_END_INHERITING(nsXULSelectableAccessible)

NS_IMPL_ADDREF_INHERITED(nsXULTreeAccessible, nsXULSelectableAccessible)
NS_IMPL_RELEASE_INHERITED(nsXULTreeAccessible, nsXULSelectableAccessible)

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeAccessible: nsAccessible implementation

nsresult
nsXULTreeAccessible::GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState)
{
  // Get focus status from base class.
  nsresult rv = nsAccessible::GetStateInternal(aState, aExtraState);
  NS_ENSURE_A11Y_SUCCESS(rv, rv);

  // readonly state
  *aState |= nsIAccessibleStates::STATE_READONLY;

  // remove focusable and focused states since tree items are focusable for AT
  *aState &= ~nsIAccessibleStates::STATE_FOCUSABLE;
  *aState &= ~nsIAccessibleStates::STATE_FOCUSED;

  // multiselectable state.
  nsCOMPtr<nsITreeSelection> selection;
  mTreeView->GetSelection(getter_AddRefs(selection));
  NS_ENSURE_STATE(selection);

  PRBool isSingle = PR_FALSE;
  rv = selection->GetSingle(&isSingle);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!isSingle)
    *aState |= nsIAccessibleStates::STATE_MULTISELECTABLE;

  return NS_OK;
}

NS_IMETHODIMP
nsXULTreeAccessible::GetValue(nsAString& aValue)
{
  // Return the value is the first selected child.

  aValue.Truncate();

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsITreeSelection> selection;
  mTreeView->GetSelection(getter_AddRefs(selection));
  if (!selection)
    return NS_ERROR_FAILURE;

  PRInt32 currentIndex;
  nsCOMPtr<nsIDOMElement> selectItem;
  selection->GetCurrentIndex(&currentIndex);
  if (currentIndex >= 0) {
    nsCOMPtr<nsITreeColumn> keyCol;

    nsCOMPtr<nsITreeColumns> cols;
    mTree->GetColumns(getter_AddRefs(cols));
    if (cols)
      cols->GetKeyColumn(getter_AddRefs(keyCol));

    return mTreeView->GetCellText(currentIndex, keyCol, aValue);
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeAccessible: nsAccessNode implementation

PRBool
nsXULTreeAccessible::IsDefunct()
{
  return nsXULSelectableAccessible::IsDefunct() || !mTree || !mTreeView;
}

void
nsXULTreeAccessible::Shutdown()
{
  // XXX: we don't remove accessible from document cache if shutdown wasn't
  // initiated by document destroying. Note, we can't remove accessible from
  // document cache here while document is going to be shutdown. Note, this is
  // not unique place where we have similar problem.
  ClearCache(mAccessibleCache);

  mTree = nsnull;
  mTreeView = nsnull;

  nsXULSelectableAccessible::Shutdown();
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeAccessible: nsAccessible implementation (put methods here)

nsresult
nsXULTreeAccessible::GetRoleInternal(PRUint32 *aRole)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  // No primary column means we're in a list. In fact, history and mail turn off
  // the primary flag when switching to a flat view.

  nsCOMPtr<nsITreeColumns> cols;
  mTree->GetColumns(getter_AddRefs(cols));
  nsCOMPtr<nsITreeColumn> primaryCol;
  if (cols)
    cols->GetPrimaryColumn(getter_AddRefs(primaryCol));

  *aRole = primaryCol ?
    static_cast<PRUint32>(nsIAccessibleRole::ROLE_OUTLINE) :
    static_cast<PRUint32>(nsIAccessibleRole::ROLE_LIST);

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeAccessible: nsIAccessible implementation

NS_IMETHODIMP
nsXULTreeAccessible::GetFocusedChild(nsIAccessible **aFocusedChild) 
{
  NS_ENSURE_ARG_POINTER(aFocusedChild);
  *aFocusedChild = nsnull;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  if (gLastFocusedNode != mContent)
    return NS_OK;

  nsCOMPtr<nsIDOMXULMultiSelectControlElement> multiSelect =
    do_QueryInterface(mContent);
  if (multiSelect) {
    PRInt32 row = -1;
    multiSelect->GetCurrentIndex(&row);
    if (row >= 0)
      NS_IF_ADDREF(*aFocusedChild = GetTreeItemAccessible(row));
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeAccessible: nsAccessible implementation (DON'T put methods here)

nsresult
nsXULTreeAccessible::GetChildAtPoint(PRInt32 aX, PRInt32 aY,
                                     PRBool aDeepestChild,
                                     nsIAccessible **aChild)
{
  nsIFrame *frame = GetFrame();
  if (!frame)
    return NS_ERROR_FAILURE;

  nsPresContext *presContext = frame->PresContext();
  nsCOMPtr<nsIPresShell> presShell = presContext->PresShell();

  nsIFrame *rootFrame = presShell->GetRootFrame();
  NS_ENSURE_STATE(rootFrame);

  nsIntRect rootRect = rootFrame->GetScreenRectExternal();

  PRInt32 clientX = presContext->DevPixelsToIntCSSPixels(aX - rootRect.x);
  PRInt32 clientY = presContext->DevPixelsToIntCSSPixels(aY - rootRect.y);

  PRInt32 row = -1;
  nsCOMPtr<nsITreeColumn> column;
  nsCAutoString childEltUnused;
  mTree->GetCellAt(clientX, clientY, &row, getter_AddRefs(column),
                   childEltUnused);

  // If we failed to find tree cell for the given point then it might be
  // tree columns.
  if (row == -1 || !column)
    return nsXULSelectableAccessible::
      GetChildAtPoint(aX, aY, aDeepestChild, aChild);

  nsAccessible *child = GetTreeItemAccessible(row);
  if (aDeepestChild && child) {
    // Look for accessible cell for the found item accessible.
    nsRefPtr<nsXULTreeItemAccessibleBase> treeitem = do_QueryObject(child);

    nsAccessible *cell = treeitem->GetCellAccessible(column);
    if (cell)
      child = cell;
  }

  NS_IF_ADDREF(*aChild = child);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeAccessible: nsAccessibleSelectable implementation

NS_IMETHODIMP nsXULTreeAccessible::GetSelectedChildren(nsIArray **_retval)
{
  // Ask tree selection to get all selected children
  *_retval = nsnull;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsITreeSelection> selection;
  mTreeView->GetSelection(getter_AddRefs(selection));
  if (!selection)
    return NS_ERROR_FAILURE;
  nsCOMPtr<nsIMutableArray> selectedAccessibles =
    do_CreateInstance(NS_ARRAY_CONTRACTID);
  NS_ENSURE_STATE(selectedAccessibles);

  PRInt32 rowIndex, rowCount;
  PRBool isSelected;
  mTreeView->GetRowCount(&rowCount);
  for (rowIndex = 0; rowIndex < rowCount; rowIndex++) {
    selection->IsSelected(rowIndex, &isSelected);
    if (isSelected) {
      nsIAccessible *tempAccessible = GetTreeItemAccessible(rowIndex);
      NS_ENSURE_STATE(tempAccessible);

      selectedAccessibles->AppendElement(tempAccessible, PR_FALSE);
    }
  }

  PRUint32 length;
  selectedAccessibles->GetLength(&length);
  if (length != 0) {
    *_retval = selectedAccessibles;
    NS_IF_ADDREF(*_retval);
  }

  return NS_OK;
}

NS_IMETHODIMP nsXULTreeAccessible::GetSelectionCount(PRInt32 *aSelectionCount)
{
  *aSelectionCount = 0;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsITreeSelection> selection;
  mTreeView->GetSelection(getter_AddRefs(selection));
  if (selection)
    selection->GetCount(aSelectionCount);

  return NS_OK;
}

NS_IMETHODIMP nsXULTreeAccessible::ChangeSelection(PRInt32 aIndex, PRUint8 aMethod, PRBool *aSelState)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsITreeSelection> selection;
  mTreeView->GetSelection(getter_AddRefs(selection));
  if (selection) {
    selection->IsSelected(aIndex, aSelState);
    if ((!(*aSelState) && eSelection_Add == aMethod) || 
        ((*aSelState) && eSelection_Remove == aMethod))
      return selection->ToggleSelect(aIndex);
  }

  return NS_OK;
}

NS_IMETHODIMP nsXULTreeAccessible::AddChildToSelection(PRInt32 aIndex)
{
  PRBool isSelected;
  return ChangeSelection(aIndex, eSelection_Add, &isSelected);
}

NS_IMETHODIMP nsXULTreeAccessible::RemoveChildFromSelection(PRInt32 aIndex)
{
  PRBool isSelected;
  return ChangeSelection(aIndex, eSelection_Remove, &isSelected);
}

NS_IMETHODIMP nsXULTreeAccessible::IsChildSelected(PRInt32 aIndex, PRBool *_retval)
{
  return ChangeSelection(aIndex, eSelection_GetState, _retval);
}

NS_IMETHODIMP nsXULTreeAccessible::ClearSelection()
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsITreeSelection> selection;
  mTreeView->GetSelection(getter_AddRefs(selection));
  if (selection)
    selection->ClearSelection();

  return NS_OK;
}

NS_IMETHODIMP
nsXULTreeAccessible::RefSelection(PRInt32 aIndex, nsIAccessible **aAccessible)
{
  NS_ENSURE_ARG_POINTER(aAccessible);
  *aAccessible = nsnull;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsITreeSelection> selection;
  mTreeView->GetSelection(getter_AddRefs(selection));
  if (!selection)
    return NS_ERROR_FAILURE;

  PRInt32 rowIndex, rowCount;
  PRInt32 selCount = 0;
  PRBool isSelected;
  mTreeView->GetRowCount(&rowCount);
  for (rowIndex = 0; rowIndex < rowCount; rowIndex++) {
    selection->IsSelected(rowIndex, &isSelected);
    if (isSelected) {
      if (selCount == aIndex) {
        NS_IF_ADDREF(*aAccessible = GetTreeItemAccessible(rowIndex));
        return NS_OK;
      }
      selCount++;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsXULTreeAccessible::SelectAllSelection(PRBool *aIsMultiSelectable)
{
  NS_ENSURE_ARG_POINTER(aIsMultiSelectable);
  *aIsMultiSelectable = PR_FALSE;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  // see if we are multiple select if so set ourselves as such
  nsCOMPtr<nsITreeSelection> selection;
  mTreeView->GetSelection(getter_AddRefs(selection));
  if (selection) {
    PRBool single = PR_FALSE;
    selection->GetSingle(&single);
    if (!single) {
      *aIsMultiSelectable = PR_TRUE;
      selection->SelectAll();
    }
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeAccessible: nsAccessible implementation

nsAccessible*
nsXULTreeAccessible::GetChildAt(PRUint32 aIndex)
{
  PRInt32 childCount = nsAccessible::GetChildCount();
  if (childCount == -1)
    return nsnull;

  if (static_cast<PRInt32>(aIndex) < childCount)
    return nsAccessible::GetChildAt(aIndex);

  return GetTreeItemAccessible(aIndex - childCount);
}

PRInt32
nsXULTreeAccessible::GetChildCount()
{
  // tree's children count is row count + treecols count.
  PRInt32 childCount = nsAccessible::GetChildCount();
  if (childCount == -1)
    return -1;

  PRInt32 rowCount = 0;
  mTreeView->GetRowCount(&rowCount);
  childCount += rowCount;

  return childCount;
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeAccessible: public implementation

nsAccessible*
nsXULTreeAccessible::GetTreeItemAccessible(PRInt32 aRow)
{
  if (aRow < 0 || IsDefunct())
    return nsnull;

  PRInt32 rowCount = 0;
  nsresult rv = mTreeView->GetRowCount(&rowCount);
  if (NS_FAILED(rv) || aRow >= rowCount)
    return nsnull;

  void *key = reinterpret_cast<void*>(aRow);
  nsRefPtr<nsAccessible> accessible = mAccessibleCache.GetWeak(key);

  if (!accessible) {
    accessible = CreateTreeItemAccessible(aRow);
    if (!accessible)
      return nsnull;

    if (!accessible->Init()) {
      accessible->Shutdown();
      return nsnull;
    }

    if (!mAccessibleCache.Put(key, accessible))
      return nsnull;
  }

  return accessible;
}

void
nsXULTreeAccessible::InvalidateCache(PRInt32 aRow, PRInt32 aCount)
{
  if (IsDefunct())
    return;

  // Do not invalidate the cache if rows have been inserted.
  if (aCount > 0)
    return;

  // Fire destroy event for removed tree items and delete them from caches.
  for (PRInt32 rowIdx = aRow; rowIdx < aRow - aCount; rowIdx++) {

    void* key = reinterpret_cast<void*>(rowIdx);
    nsAccessible *accessible = mAccessibleCache.GetWeak(key);

    if (accessible) {
      nsRefPtr<AccEvent> event =
        new AccEvent(nsIAccessibleEvent::EVENT_HIDE, accessible, PR_FALSE);
      nsEventShell::FireEvent(event);

      accessible->Shutdown();

      // Remove accessible from document cache and tree cache.
      nsDocAccessible *docAccessible = GetDocAccessible();
      if (docAccessible)
        docAccessible->RemoveAccessNodeFromCache(accessible);

      mAccessibleCache.Remove(key);
    }
  }

  // We dealt with removed tree items already however we may keep tree items
  // having row indexes greater than row count. We should remove these dead tree
  // items silently from caches.
  PRInt32 newRowCount = 0;
  nsresult rv = mTreeView->GetRowCount(&newRowCount);
  if (NS_FAILED(rv))
    return;

  PRInt32 oldRowCount = newRowCount - aCount;

  for (PRInt32 rowIdx = newRowCount; rowIdx < oldRowCount; ++rowIdx) {

    void *key = reinterpret_cast<void*>(rowIdx);
    nsAccessible *accessible = mAccessibleCache.GetWeak(key);

    if (accessible) {
      accessible->Shutdown();

      // Remove accessible from document cache and tree cache.
      nsDocAccessible *docAccessible = GetDocAccessible();
      if (docAccessible)
        docAccessible->RemoveAccessNodeFromCache(accessible);

      mAccessibleCache.Remove(key);
    }
  }
}

void
nsXULTreeAccessible::TreeViewInvalidated(PRInt32 aStartRow, PRInt32 aEndRow,
                                         PRInt32 aStartCol, PRInt32 aEndCol)
{
  if (IsDefunct())
    return;

  PRInt32 endRow = aEndRow;

  nsresult rv;
  if (endRow == -1) {
    PRInt32 rowCount = 0;
    rv = mTreeView->GetRowCount(&rowCount);
    if (NS_FAILED(rv))
      return;

    endRow = rowCount - 1;
  }

  nsCOMPtr<nsITreeColumns> treeColumns;
  mTree->GetColumns(getter_AddRefs(treeColumns));
  if (!treeColumns)
    return;

  PRInt32 endCol = aEndCol;

  if (endCol == -1) {
    PRInt32 colCount = 0;
    rv = treeColumns->GetCount(&colCount);
    if (NS_FAILED(rv))
      return;

    endCol = colCount - 1;
  }

  for (PRInt32 rowIdx = aStartRow; rowIdx <= endRow; ++rowIdx) {

    void *key = reinterpret_cast<void*>(rowIdx);
    nsAccessible *accessible = mAccessibleCache.GetWeak(key);

    if (accessible) {
      nsRefPtr<nsXULTreeItemAccessibleBase> treeitemAcc = do_QueryObject(accessible);
      NS_ASSERTION(treeitemAcc, "Wrong accessible at the given key!");

      treeitemAcc->RowInvalidated(aStartCol, endCol);
    }
  }
}

void
nsXULTreeAccessible::TreeViewChanged()
{
  if (IsDefunct())
    return;

  // Fire only notification destroy/create events on accessible tree to lie to
  // AT because it should be expensive to fire destroy events for each tree item
  // in cache.
  nsRefPtr<AccEvent> eventDestroy =
    new AccEvent(nsIAccessibleEvent::EVENT_HIDE, this, PR_FALSE);
  if (!eventDestroy)
    return;

  FirePlatformEvent(eventDestroy);

  ClearCache(mAccessibleCache);

  mTree->GetView(getter_AddRefs(mTreeView));

  nsRefPtr<AccEvent> eventCreate =
    new AccEvent(nsIAccessibleEvent::EVENT_SHOW, this, PR_FALSE);
  if (!eventCreate)
    return;

  FirePlatformEvent(eventCreate);
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeAccessible: protected implementation

already_AddRefed<nsAccessible>
nsXULTreeAccessible::CreateTreeItemAccessible(PRInt32 aRow)
{
  nsRefPtr<nsAccessible> accessible =
    new nsXULTreeItemAccessible(mContent, mWeakShell, this, mTree, mTreeView,
                                aRow);

  return accessible.forget();
}
                             
////////////////////////////////////////////////////////////////////////////////
// nsXULTreeItemAccessibleBase
////////////////////////////////////////////////////////////////////////////////

nsXULTreeItemAccessibleBase::
  nsXULTreeItemAccessibleBase(nsIContent *aContent, nsIWeakReference *aShell,
                              nsAccessible *aParent, nsITreeBoxObject *aTree,
                              nsITreeView *aTreeView, PRInt32 aRow) :
  nsAccessibleWrap(aContent, aShell),
  mTree(aTree), mTreeView(aTreeView), mRow(aRow)
{
  mParent = aParent;
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeItemAccessibleBase: nsISupports implementation

NS_IMPL_ISUPPORTS_INHERITED1(nsXULTreeItemAccessibleBase,
                             nsAccessible,
                             nsXULTreeItemAccessibleBase)

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeItemAccessibleBase: nsIAccessNode implementation

NS_IMETHODIMP
nsXULTreeItemAccessibleBase::GetUniqueID(void **aUniqueID)
{
  // Since mContent is same for all tree items and tree itself, use |this|
  // pointer as the unique ID.
  *aUniqueID = static_cast<void*>(this);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeItemAccessibleBase: nsIAccessible implementation

NS_IMETHODIMP
nsXULTreeItemAccessibleBase::GetFocusedChild(nsIAccessible **aFocusedChild) 
{
  NS_ENSURE_ARG_POINTER(aFocusedChild);
  *aFocusedChild = nsnull;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  if (gLastFocusedNode != mContent)
    return NS_OK;

  nsCOMPtr<nsIDOMXULMultiSelectControlElement> multiSelect =
    do_QueryInterface(mContent);

  if (multiSelect) {
    PRInt32 row = -1;
    multiSelect->GetCurrentIndex(&row);
    if (row == mRow)
      NS_ADDREF(*aFocusedChild = this);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsXULTreeItemAccessibleBase::GetBounds(PRInt32 *aX, PRInt32 *aY,
                                       PRInt32 *aWidth, PRInt32 *aHeight)
{
  NS_ENSURE_ARG_POINTER(aX);
  *aX = 0;
  NS_ENSURE_ARG_POINTER(aY);
  *aY = 0;
  NS_ENSURE_ARG_POINTER(aWidth);
  *aWidth = 0;
  NS_ENSURE_ARG_POINTER(aHeight);
  *aHeight = 0;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  // Get x coordinate and width from treechildren element, get y coordinate and
  // height from tree cell.

  nsCOMPtr<nsIBoxObject> boxObj = nsCoreUtils::GetTreeBodyBoxObject(mTree);
  NS_ENSURE_STATE(boxObj);

  nsCOMPtr<nsITreeColumn> column = nsCoreUtils::GetFirstSensibleColumn(mTree);

  PRInt32 x = 0, y = 0, width = 0, height = 0;
  nsresult rv = mTree->GetCoordsForCellItem(mRow, column, EmptyCString(),
                                            &x, &y, &width, &height);
  NS_ENSURE_SUCCESS(rv, rv);

  boxObj->GetWidth(&width);

  PRInt32 tcX = 0, tcY = 0;
  boxObj->GetScreenX(&tcX);
  boxObj->GetScreenY(&tcY);

  x = tcX;
  y += tcY;

  nsPresContext *presContext = GetPresContext();
  *aX = presContext->CSSPixelsToDevPixels(x);
  *aY = presContext->CSSPixelsToDevPixels(y);
  *aWidth = presContext->CSSPixelsToDevPixels(width);
  *aHeight = presContext->CSSPixelsToDevPixels(height);

  return NS_OK;
}

NS_IMETHODIMP
nsXULTreeItemAccessibleBase::SetSelected(PRBool aSelect)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsITreeSelection> selection;
  mTreeView->GetSelection(getter_AddRefs(selection));
  if (selection) {
    PRBool isSelected;
    selection->IsSelected(mRow, &isSelected);
    if (isSelected != aSelect)
      selection->ToggleSelect(mRow);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsXULTreeItemAccessibleBase::TakeFocus()
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsITreeSelection> selection;
  mTreeView->GetSelection(getter_AddRefs(selection));
  if (selection)
    selection->SetCurrentIndex(mRow);

  // focus event will be fired here
  return nsAccessible::TakeFocus();
}

NS_IMETHODIMP
nsXULTreeItemAccessibleBase::GetRelationByType(PRUint32 aRelationType,
                                               nsIAccessibleRelation **aRelation)
{
  NS_ENSURE_ARG_POINTER(aRelation);
  *aRelation = nsnull;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  if (aRelationType == nsIAccessibleRelation::RELATION_NODE_CHILD_OF) {
    PRInt32 parentIndex;
    if (NS_SUCCEEDED(mTreeView->GetParentIndex(mRow, &parentIndex))) {
      if (parentIndex == -1)
        return nsRelUtils::AddTarget(aRelationType, aRelation, mParent);

      nsRefPtr<nsXULTreeAccessible> treeAcc = do_QueryObject(mParent);

      nsAccessible *logicalParent = treeAcc->GetTreeItemAccessible(parentIndex);
      return nsRelUtils::AddTarget(aRelationType, aRelation, logicalParent);
    }

    return NS_OK;
  }

  return nsAccessible::GetRelationByType(aRelationType, aRelation);
}

NS_IMETHODIMP
nsXULTreeItemAccessibleBase::GetNumActions(PRUint8 *aActionsCount)
{
  NS_ENSURE_ARG_POINTER(aActionsCount);
  *aActionsCount = 0;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  // "activate" action is available for all treeitems, "expand/collapse" action
  // is avaible for treeitem which is container.
  *aActionsCount = IsExpandable() ? 2 : 1;
  return NS_OK;
}

NS_IMETHODIMP
nsXULTreeItemAccessibleBase::GetActionName(PRUint8 aIndex, nsAString& aName)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  if (aIndex == eAction_Click) {
    aName.AssignLiteral("activate");
    return NS_OK;
  }

  if (aIndex == eAction_Expand && IsExpandable()) {
    PRBool isContainerOpen;
    mTreeView->IsContainerOpen(mRow, &isContainerOpen);
    if (isContainerOpen)
      aName.AssignLiteral("collapse");
    else
      aName.AssignLiteral("expand");

    return NS_OK;
  }

  return NS_ERROR_INVALID_ARG;
}

NS_IMETHODIMP
nsXULTreeItemAccessibleBase::DoAction(PRUint8 aIndex)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  if (aIndex != eAction_Click &&
      (aIndex != eAction_Expand || !IsExpandable()))
    return NS_ERROR_INVALID_ARG;

  DoCommand(nsnull, aIndex);
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeItemAccessibleBase: nsAccessNode implementation

PRBool
nsXULTreeItemAccessibleBase::IsDefunct()
{
  if (nsAccessibleWrap::IsDefunct() || !mTree || !mTreeView || mRow < 0)
    return PR_TRUE;

  PRInt32 rowCount = 0;
  nsresult rv = mTreeView->GetRowCount(&rowCount);
  return NS_FAILED(rv) || mRow >= rowCount;
}

void
nsXULTreeItemAccessibleBase::Shutdown()
{
  mTree = nsnull;
  mTreeView = nsnull;
  mRow = -1;

  nsAccessibleWrap::Shutdown();
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeItemAccessibleBase: nsAccessible public methods

// nsIAccessible::groupPosition
nsresult
nsXULTreeItemAccessibleBase::GroupPosition(PRInt32 *aGroupLevel,
                                           PRInt32 *aSimilarItemsInGroup,
                                           PRInt32 *aPositionInGroup)
{
  NS_ENSURE_ARG_POINTER(aGroupLevel);
  *aGroupLevel = 0;

  NS_ENSURE_ARG_POINTER(aSimilarItemsInGroup);
  *aSimilarItemsInGroup = 0;

  NS_ENSURE_ARG_POINTER(aPositionInGroup);
  *aPositionInGroup = 0;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  PRInt32 level;
  nsresult rv = mTreeView->GetLevel(mRow, &level);
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 topCount = 1;
  for (PRInt32 index = mRow - 1; index >= 0; index--) {
    PRInt32 lvl = -1;
    if (NS_SUCCEEDED(mTreeView->GetLevel(index, &lvl))) {
      if (lvl < level)
        break;

      if (lvl == level)
        topCount++;
    }
  }

  PRInt32 rowCount = 0;
  rv = mTreeView->GetRowCount(&rowCount);
  NS_ENSURE_SUCCESS(rv, rv);

  PRInt32 bottomCount = 0;
  for (PRInt32 index = mRow + 1; index < rowCount; index++) {
    PRInt32 lvl = -1;
    if (NS_SUCCEEDED(mTreeView->GetLevel(index, &lvl))) {
      if (lvl < level)
        break;

      if (lvl == level)
        bottomCount++;
    }
  }

  PRInt32 setSize = topCount + bottomCount;
  PRInt32 posInSet = topCount;

  *aGroupLevel = level + 1;
  *aSimilarItemsInGroup = setSize;
  *aPositionInGroup = posInSet;

  return NS_OK;
}

nsresult
nsXULTreeItemAccessibleBase::GetStateInternal(PRUint32 *aState,
                                              PRUint32 *aExtraState)
{
  NS_ENSURE_ARG_POINTER(aState);

  *aState = 0;
  if (aExtraState)
    *aExtraState = 0;

  if (IsDefunct()) {
    if (aExtraState)
      *aExtraState = nsIAccessibleStates::EXT_STATE_DEFUNCT;
    return NS_OK_DEFUNCT_OBJECT;
  }

  // focusable and selectable states
  *aState = nsIAccessibleStates::STATE_FOCUSABLE |
    nsIAccessibleStates::STATE_SELECTABLE;

  // expanded/collapsed state
  if (IsExpandable()) {
    PRBool isContainerOpen;
    mTreeView->IsContainerOpen(mRow, &isContainerOpen);
    *aState |= isContainerOpen ?
      static_cast<PRUint32>(nsIAccessibleStates::STATE_EXPANDED) :
      static_cast<PRUint32>(nsIAccessibleStates::STATE_COLLAPSED);
  }

  // selected state
  nsCOMPtr<nsITreeSelection> selection;
  mTreeView->GetSelection(getter_AddRefs(selection));
  if (selection) {
    PRBool isSelected;
    selection->IsSelected(mRow, &isSelected);
    if (isSelected)
      *aState |= nsIAccessibleStates::STATE_SELECTED;
  }

  // focused state
  nsCOMPtr<nsIDOMXULMultiSelectControlElement> multiSelect =
    do_QueryInterface(mContent);
  if (multiSelect) {
    PRInt32 currentIndex;
    multiSelect->GetCurrentIndex(&currentIndex);
    if (currentIndex == mRow) {
      *aState |= nsIAccessibleStates::STATE_FOCUSED;
    }
  }

  // invisible state
  PRInt32 firstVisibleRow, lastVisibleRow;
  mTree->GetFirstVisibleRow(&firstVisibleRow);
  mTree->GetLastVisibleRow(&lastVisibleRow);
  if (mRow < firstVisibleRow || mRow > lastVisibleRow)
    *aState |= nsIAccessibleStates::STATE_INVISIBLE;

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeItemAccessibleBase: nsAccessible protected methods

void
nsXULTreeItemAccessibleBase::DispatchClickEvent(nsIContent *aContent,
                                                PRUint32 aActionIndex)
{
  if (IsDefunct())
    return;

  nsCOMPtr<nsITreeColumns> columns;
  mTree->GetColumns(getter_AddRefs(columns));
  if (!columns)
    return;

  // Get column and pseudo element.
  nsCOMPtr<nsITreeColumn> column;
  nsCAutoString pseudoElm;

  if (aActionIndex == eAction_Click) {
    // Key column is visible and clickable.
    columns->GetKeyColumn(getter_AddRefs(column));
  } else {
    // Primary column contains a twisty we should click on.
    columns->GetPrimaryColumn(getter_AddRefs(column));
    pseudoElm = NS_LITERAL_CSTRING("twisty");
  }

  if (column)
    nsCoreUtils::DispatchClickEvent(mTree, mRow, column, pseudoElm);
}

nsAccessible*
nsXULTreeItemAccessibleBase::GetSiblingAtOffset(PRInt32 aOffset,
                                                nsresult* aError)
{
  if (IsDefunct()) {
    if (aError)
      *aError = NS_ERROR_FAILURE;

    return nsnull;
  }

  if (aError)
    *aError = NS_OK; // fail peacefully

  return mParent->GetChildAt(GetIndexInParent() + aOffset);
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeItemAccessibleBase: protected implementation

PRBool
nsXULTreeItemAccessibleBase::IsExpandable()
{
  PRBool isContainer = PR_FALSE;
  mTreeView->IsContainer(mRow, &isContainer);
  if (isContainer) {
    PRBool isEmpty = PR_FALSE;
    mTreeView->IsContainerEmpty(mRow, &isEmpty);
    if (!isEmpty) {
      nsCOMPtr<nsITreeColumns> columns;
      mTree->GetColumns(getter_AddRefs(columns));
      nsCOMPtr<nsITreeColumn> primaryColumn;
      if (columns) {
        columns->GetPrimaryColumn(getter_AddRefs(primaryColumn));
        if (primaryColumn &&
            !nsCoreUtils::IsColumnHidden(primaryColumn))
          return PR_TRUE;
      }
    }
  }

  return PR_FALSE;
}


////////////////////////////////////////////////////////////////////////////////
// nsXULTreeItemAccessible
////////////////////////////////////////////////////////////////////////////////

nsXULTreeItemAccessible::
  nsXULTreeItemAccessible(nsIContent *aContent, nsIWeakReference *aShell,
                          nsAccessible *aParent, nsITreeBoxObject *aTree,
                          nsITreeView *aTreeView, PRInt32 aRow) :
  nsXULTreeItemAccessibleBase(aContent, aShell, aParent, aTree, aTreeView, aRow)
{
  mColumn = nsCoreUtils::GetFirstSensibleColumn(mTree);
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeItemAccessible: nsIAccessible implementation

NS_IMETHODIMP
nsXULTreeItemAccessible::GetName(nsAString& aName)
{
  aName.Truncate();

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  mTreeView->GetCellText(mRow, mColumn, aName);

  // If there is still no name try the cell value:
  // This is for graphical cells. We need tree/table view implementors to implement
  // FooView::GetCellValue to return a meaningful string for cases where there is
  // something shown in the cell (non-text) such as a star icon; in which case
  // GetCellValue for that cell would return "starred" or "flagged" for example.
  if (aName.IsEmpty())
    mTreeView->GetCellValue(mRow, mColumn, aName);

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeItemAccessible: nsAccessNode implementation

PRBool
nsXULTreeItemAccessible::IsDefunct()
{
  return nsXULTreeItemAccessibleBase::IsDefunct() || !mColumn;
}

PRBool
nsXULTreeItemAccessible::Init()
{
  if (!nsXULTreeItemAccessibleBase::Init())
    return PR_FALSE;

  GetName(mCachedName);
  return PR_TRUE;
}

void
nsXULTreeItemAccessible::Shutdown()
{
  mColumn = nsnull;
  nsXULTreeItemAccessibleBase::Shutdown();
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeItemAccessible: nsAccessible implementation

nsresult
nsXULTreeItemAccessible::GetRoleInternal(PRUint32 *aRole)
{
  nsCOMPtr<nsITreeColumns> columns;
  mTree->GetColumns(getter_AddRefs(columns));
  NS_ENSURE_STATE(columns);

  nsCOMPtr<nsITreeColumn> primaryColumn;
  columns->GetPrimaryColumn(getter_AddRefs(primaryColumn));

  *aRole = primaryColumn ?
    static_cast<PRUint32>(nsIAccessibleRole::ROLE_OUTLINEITEM) :
    static_cast<PRUint32>(nsIAccessibleRole::ROLE_LISTITEM);

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeItemAccessible: nsXULTreeItemAccessibleBase implementation

void
nsXULTreeItemAccessible::RowInvalidated(PRInt32 aStartColIdx,
                                        PRInt32 aEndColIdx)
{
  nsAutoString name;
  GetName(name);

  if (name != mCachedName) {
    nsEventShell::FireEvent(nsIAccessibleEvent::EVENT_NAME_CHANGE, this);
    mCachedName = name;
  }
}

////////////////////////////////////////////////////////////////////////////////
// nsXULTreeItemAccessible: nsAccessible protected implementation

void
nsXULTreeItemAccessible::CacheChildren()
{
}


////////////////////////////////////////////////////////////////////////////////
//  nsXULTreeColumnsAccessible
////////////////////////////////////////////////////////////////////////////////

nsXULTreeColumnsAccessible::
  nsXULTreeColumnsAccessible(nsIContent *aContent, nsIWeakReference *aShell) :
  nsXULColumnsAccessible(aContent, aShell)
{
}

nsAccessible*
nsXULTreeColumnsAccessible::GetSiblingAtOffset(PRInt32 aOffset,
                                               nsresult* aError)
{
  if (aOffset < 0)
    return nsXULColumnsAccessible::GetSiblingAtOffset(aOffset, aError);

  if (IsDefunct()) {
    if (aError)
      *aError = NS_ERROR_FAILURE;

    return nsnull;
  }

  if (aError)
    *aError = NS_OK; // fail peacefully

  nsCOMPtr<nsITreeBoxObject> tree = nsCoreUtils::GetTreeBoxObject(mContent);
  if (tree) {
    nsCOMPtr<nsITreeView> treeView;
    tree->GetView(getter_AddRefs(treeView));
    if (treeView) {
      PRInt32 rowCount = 0;
      treeView->GetRowCount(&rowCount);
      if (rowCount > 0 && aOffset <= rowCount) {
        nsRefPtr<nsXULTreeAccessible> treeAcc = do_QueryObject(mParent);

        if (treeAcc)
          return treeAcc->GetTreeItemAccessible(aOffset - 1);
      }
    }
  }

  return nsnull;
}

