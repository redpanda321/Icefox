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
#include "nsTransactionItem.h"
#include "nsTransactionStack.h"
#include "nsCOMPtr.h"
#include "nsAutoPtr.h"
#include "nsCycleCollectionParticipant.h"

nsTransactionStack::nsTransactionStack()
  : mQue(0)
{
} 

nsTransactionStack::~nsTransactionStack()
{
  Clear();
}

nsresult
nsTransactionStack::Push(nsTransactionItem *aTransaction)
{
  NS_ENSURE_TRUE(aTransaction, NS_ERROR_NULL_POINTER);

  /* nsDeque's Push() method adds new items at the back
   * of the deque.
   */
  NS_ADDREF(aTransaction);
  mQue.Push(aTransaction);

  return NS_OK;
}

nsresult
nsTransactionStack::Pop(nsTransactionItem **aTransaction)
{
  NS_ENSURE_TRUE(aTransaction, NS_ERROR_NULL_POINTER);

  /* nsDeque is a FIFO, so the top of our stack is actually
   * the back of the deque.
   */
  *aTransaction = (nsTransactionItem *)mQue.Pop();

  return NS_OK;
}

nsresult
nsTransactionStack::PopBottom(nsTransactionItem **aTransaction)
{
  NS_ENSURE_TRUE(aTransaction, NS_ERROR_NULL_POINTER);

  /* nsDeque is a FIFO, so the bottom of our stack is actually
   * the front of the deque.
   */
  *aTransaction = (nsTransactionItem *)mQue.PopFront();

  return NS_OK;
}

nsresult
nsTransactionStack::Peek(nsTransactionItem **aTransaction)
{
  NS_ENSURE_TRUE(aTransaction, NS_ERROR_NULL_POINTER);

  if (!mQue.GetSize()) {
    *aTransaction = 0;
    return NS_OK;
  }

  NS_IF_ADDREF(*aTransaction = static_cast<nsTransactionItem*>(mQue.Last()));

  return NS_OK;
}

nsresult
nsTransactionStack::GetItem(PRInt32 aIndex, nsTransactionItem **aTransaction)
{
  NS_ENSURE_TRUE(aTransaction, NS_ERROR_NULL_POINTER);

  if (aIndex < 0 || aIndex >= mQue.GetSize())
    return NS_ERROR_FAILURE;

  NS_IF_ADDREF(*aTransaction =
               static_cast<nsTransactionItem*>(mQue.ObjectAt(aIndex)));

  return NS_OK;
}

nsresult
nsTransactionStack::Clear(void)
{
  nsRefPtr<nsTransactionItem> tx;
  nsresult result    = NS_OK;

  /* Pop all transactions off the stack and release them. */

  result = Pop(getter_AddRefs(tx));

  NS_ENSURE_SUCCESS(result, result);

  while (tx) {
    result = Pop(getter_AddRefs(tx));

    NS_ENSURE_SUCCESS(result, result);
  }

  return NS_OK;
}

nsresult
nsTransactionStack::GetSize(PRInt32 *aStackSize)
{
  NS_ENSURE_TRUE(aStackSize, NS_ERROR_NULL_POINTER);

  *aStackSize = mQue.GetSize();

  return NS_OK;
}

void
nsTransactionStack::DoTraverse(nsCycleCollectionTraversalCallback &cb)
{
  for (PRInt32 i = 0, qcount = mQue.GetSize(); i < qcount; ++i) {
    nsTransactionItem *item =
      static_cast<nsTransactionItem*>(mQue.ObjectAt(i));
    if (item) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "transaction stack mQue[i]");
      cb.NoteNativeChild(item, &NS_CYCLE_COLLECTION_NAME(nsTransactionItem));
    }
  }
}

nsTransactionRedoStack::~nsTransactionRedoStack()
{
  Clear();
}

nsresult
nsTransactionRedoStack::Clear(void)
{
  nsRefPtr<nsTransactionItem> tx;
  nsresult result       = NS_OK;

  /* When clearing a Redo stack, we have to clear from the
   * bottom of the stack towards the top!
   */

  result = PopBottom(getter_AddRefs(tx));

  NS_ENSURE_SUCCESS(result, result);

  while (tx) {
    result = PopBottom(getter_AddRefs(tx));

    NS_ENSURE_SUCCESS(result, result);
  }

  return NS_OK;
}

