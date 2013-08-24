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

#include "nsITransaction.h"
#include "nsTransactionStack.h"
#include "nsTransactionManager.h"
#include "nsTransactionItem.h"
#include "nsCOMPtr.h"
#include "nsAutoPtr.h"

nsTransactionItem::nsTransactionItem(nsITransaction *aTransaction)
    : mTransaction(aTransaction), mUndoStack(0), mRedoStack(0)
{
}

nsTransactionItem::~nsTransactionItem()
{
  if (mRedoStack)
    delete mRedoStack;

  if (mUndoStack)
    delete mUndoStack;
}

nsrefcnt
nsTransactionItem::AddRef()
{
  ++mRefCnt;
  NS_LOG_ADDREF(this, mRefCnt, "nsTransactionItem",
                sizeof(nsTransactionItem));
  return mRefCnt;
}

nsrefcnt
nsTransactionItem::Release() {
  --mRefCnt;
  NS_LOG_RELEASE(this, mRefCnt, "nsTransactionItem");
  if (mRefCnt == 0) {
    mRefCnt = 1;
    delete this;
    return 0;
  }
  return mRefCnt;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsTransactionItem)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_NATIVE(nsTransactionItem)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mTransaction)
  if (tmp->mRedoStack) {
    tmp->mRedoStack->DoUnlink();
  }
  if (tmp->mUndoStack) {
    tmp->mUndoStack->DoUnlink();
  }
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NATIVE_BEGIN(nsTransactionItem)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mTransaction)
  if (tmp->mRedoStack) {
    tmp->mRedoStack->DoTraverse(cb);
  }
  if (tmp->mUndoStack) {
    tmp->mUndoStack->DoTraverse(cb);
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_ROOT_NATIVE(nsTransactionItem, AddRef)
NS_IMPL_CYCLE_COLLECTION_UNROOT_NATIVE(nsTransactionItem, Release)

nsresult
nsTransactionItem::AddChild(nsTransactionItem *aTransactionItem)
{
  NS_ENSURE_TRUE(aTransactionItem, NS_ERROR_NULL_POINTER);

  if (!mUndoStack) {
    mUndoStack = new nsTransactionStack();
    NS_ENSURE_TRUE(mUndoStack, NS_ERROR_OUT_OF_MEMORY);
  }

  mUndoStack->Push(aTransactionItem);

  return NS_OK;
}

nsresult
nsTransactionItem::GetTransaction(nsITransaction **aTransaction)
{
  NS_ENSURE_TRUE(aTransaction, NS_ERROR_NULL_POINTER);

  NS_IF_ADDREF(*aTransaction = mTransaction);

  return NS_OK;
}

nsresult
nsTransactionItem::GetIsBatch(PRBool *aIsBatch)
{
  NS_ENSURE_TRUE(aIsBatch, NS_ERROR_NULL_POINTER);

  *aIsBatch = !mTransaction;

  return NS_OK;
}

nsresult
nsTransactionItem::GetNumberOfChildren(PRInt32 *aNumChildren)
{
  nsresult result;

  NS_ENSURE_TRUE(aNumChildren, NS_ERROR_NULL_POINTER);

  *aNumChildren = 0;

  PRInt32 ui = 0;
  PRInt32 ri = 0;

  result = GetNumberOfUndoItems(&ui);

  NS_ENSURE_SUCCESS(result, result);

  result = GetNumberOfRedoItems(&ri);

  NS_ENSURE_SUCCESS(result, result);

  *aNumChildren = ui + ri;

  return NS_OK;
}

nsresult
nsTransactionItem::GetChild(PRInt32 aIndex, nsTransactionItem **aChild)
{
  NS_ENSURE_TRUE(aChild, NS_ERROR_NULL_POINTER);

  *aChild = 0;

  PRInt32 numItems = 0;
  nsresult result = GetNumberOfChildren(&numItems);

  NS_ENSURE_SUCCESS(result, result);

  if (aIndex < 0 || aIndex >= numItems)
    return NS_ERROR_FAILURE;

  // Children are expected to be in the order they were added,
  // so the child first added would be at the bottom of the undo
  // stack, or if there are no items on the undo stack, it would
  // be at the top of the redo stack.

  result = GetNumberOfUndoItems(&numItems);

  NS_ENSURE_SUCCESS(result, result);

  if (numItems > 0 && aIndex < numItems) {
    NS_ENSURE_TRUE(mUndoStack, NS_ERROR_FAILURE);

    return mUndoStack->GetItem(aIndex, aChild);
  }

  // Adjust the index for the redo stack:

  aIndex -=  numItems;

  result = GetNumberOfRedoItems(&numItems);

  NS_ENSURE_SUCCESS(result, result);

  NS_ENSURE_TRUE(mRedoStack && numItems != 0 && aIndex < numItems, NS_ERROR_FAILURE);

  return mRedoStack->GetItem(numItems - aIndex - 1, aChild);
}

nsresult
nsTransactionItem::DoTransaction()
{
  if (mTransaction)
    return mTransaction->DoTransaction();
  return NS_OK;
}

nsresult
nsTransactionItem::UndoTransaction(nsTransactionManager *aTxMgr)
{
  nsresult result = UndoChildren(aTxMgr);

  if (NS_FAILED(result)) {
    RecoverFromUndoError(aTxMgr);
    return result;
  }

  if (!mTransaction)
    return NS_OK;

  result = mTransaction->UndoTransaction();

  if (NS_FAILED(result)) {
    RecoverFromUndoError(aTxMgr);
    return result;
  }

  return NS_OK;
}

nsresult
nsTransactionItem::UndoChildren(nsTransactionManager *aTxMgr)
{
  nsRefPtr<nsTransactionItem> item;
  nsresult result = NS_OK;
  PRInt32 sz = 0;

  if (mUndoStack) {
    if (!mRedoStack && mUndoStack) {
      mRedoStack = new nsTransactionRedoStack();
      NS_ENSURE_TRUE(mRedoStack, NS_ERROR_OUT_OF_MEMORY);
    }

    /* Undo all of the transaction items children! */
    result = mUndoStack->GetSize(&sz);

    NS_ENSURE_SUCCESS(result, result);

    while (sz-- > 0) {
      result = mUndoStack->Peek(getter_AddRefs(item));

      if (NS_FAILED(result) || !item) {
        return result;
      }

      nsCOMPtr<nsITransaction> t;

      result = item->GetTransaction(getter_AddRefs(t));

      if (NS_FAILED(result)) {
        return result;
      }

      PRBool doInterrupt = PR_FALSE;

      result = aTxMgr->WillUndoNotify(t, &doInterrupt);

      if (NS_FAILED(result)) {
        return result;
      }

      if (doInterrupt) {
        return NS_OK;
      }

      result = item->UndoTransaction(aTxMgr);

      if (NS_SUCCEEDED(result)) {
        result = mUndoStack->Pop(getter_AddRefs(item));

        if (NS_SUCCEEDED(result)) {
          result = mRedoStack->Push(item);

          /* XXX: If we got an error here, I doubt we can recover!
           * XXX: Should we just push the item back on the undo stack?
           */
        }
      }

      nsresult result2 = aTxMgr->DidUndoNotify(t, result);

      if (NS_SUCCEEDED(result)) {
        result = result2;
      }
    }
  }

  return result;
}

nsresult
nsTransactionItem::RedoTransaction(nsTransactionManager *aTxMgr)
{
  nsresult result;

  nsCOMPtr<nsITransaction> kungfuDeathGrip(mTransaction);
  if (mTransaction) {
    result = mTransaction->RedoTransaction();

    NS_ENSURE_SUCCESS(result, result);
  }

  result = RedoChildren(aTxMgr);

  if (NS_FAILED(result)) {
    RecoverFromRedoError(aTxMgr);
    return result;
  }

  return NS_OK;
}

nsresult
nsTransactionItem::RedoChildren(nsTransactionManager *aTxMgr)
{
  nsRefPtr<nsTransactionItem> item;
  nsresult result = NS_OK;
  PRInt32 sz = 0;

  if (!mRedoStack)
    return NS_OK;

  /* Redo all of the transaction items children! */
  result = mRedoStack->GetSize(&sz);

  NS_ENSURE_SUCCESS(result, result);


  while (sz-- > 0) {
    result = mRedoStack->Peek(getter_AddRefs(item));

    if (NS_FAILED(result) || !item) {
      return result;
    }

    nsCOMPtr<nsITransaction> t;

    result = item->GetTransaction(getter_AddRefs(t));

    if (NS_FAILED(result)) {
      return result;
    }

    PRBool doInterrupt = PR_FALSE;

    result = aTxMgr->WillRedoNotify(t, &doInterrupt);

    if (NS_FAILED(result)) {
      return result;
    }

    if (doInterrupt) {
      return NS_OK;
    }

    result = item->RedoTransaction(aTxMgr);

    if (NS_SUCCEEDED(result)) {
      result = mRedoStack->Pop(getter_AddRefs(item));

      if (NS_SUCCEEDED(result)) {
        result = mUndoStack->Push(item);

        // XXX: If we got an error here, I doubt we can recover!
        // XXX: Should we just push the item back on the redo stack?
      }
    }

    nsresult result2 = aTxMgr->DidUndoNotify(t, result);

    if (NS_SUCCEEDED(result)) {
      result = result2;
    }
  }

  return result;
}

nsresult
nsTransactionItem::GetNumberOfUndoItems(PRInt32 *aNumItems)
{
  NS_ENSURE_TRUE(aNumItems, NS_ERROR_NULL_POINTER);

  if (!mUndoStack) {
    *aNumItems = 0;
    return NS_OK;
  }

  return mUndoStack->GetSize(aNumItems);
}

nsresult
nsTransactionItem::GetNumberOfRedoItems(PRInt32 *aNumItems)
{
  NS_ENSURE_TRUE(aNumItems, NS_ERROR_NULL_POINTER);

  if (!mRedoStack) {
    *aNumItems = 0;
    return NS_OK;
  }

  return mRedoStack->GetSize(aNumItems);
}

nsresult
nsTransactionItem::RecoverFromUndoError(nsTransactionManager *aTxMgr)
{
  //
  // If this method gets called, we never got to the point where we
  // successfully called UndoTransaction() for the transaction item itself.
  // Just redo any children that successfully called undo!
  //
  return RedoChildren(aTxMgr);
}

nsresult
nsTransactionItem::RecoverFromRedoError(nsTransactionManager *aTxMgr)
{
  //
  // If this method gets called, we already successfully called
  // RedoTransaction() for the transaction item itself. Undo all
  // the children that successfully called RedoTransaction(),
  // then undo the transaction item itself.
  //

  nsresult result;

  result = UndoChildren(aTxMgr);

  if (NS_FAILED(result)) {
    return result;
  }

  if (!mTransaction)
    return NS_OK;

  return mTransaction->UndoTransaction();
}

