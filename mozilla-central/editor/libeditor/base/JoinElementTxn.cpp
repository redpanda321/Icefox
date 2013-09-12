/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>                      // for printf

#include "JoinElementTxn.h"
#include "nsAString.h"
#include "nsDebug.h"                    // for NS_ASSERTION, etc
#include "nsEditor.h"                   // for nsEditor
#include "nsError.h"                    // for NS_ERROR_NULL_POINTER, etc
#include "nsIDOMCharacterData.h"        // for nsIDOMCharacterData
#include "nsIEditor.h"                  // for nsEditor::IsModifiableNode
#include "nsINode.h"                    // for nsINode
#include "nsISupportsImpl.h"            // for EditTxn::QueryInterface, etc

#ifdef DEBUG
static bool gNoisy = false;
#endif

JoinElementTxn::JoinElementTxn()
  : EditTxn()
{
}

NS_IMPL_CYCLE_COLLECTION_CLASS(JoinElementTxn)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(JoinElementTxn, EditTxn)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLeftNode)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mRightNode)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mParent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(JoinElementTxn, EditTxn)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLeftNode)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRightNode)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(JoinElementTxn)
NS_INTERFACE_MAP_END_INHERITING(EditTxn)

NS_IMETHODIMP JoinElementTxn::Init(nsEditor   *aEditor,
                                   nsIDOMNode *aLeftNode,
                                   nsIDOMNode *aRightNode)
{
  NS_PRECONDITION((aEditor && aLeftNode && aRightNode), "null arg");
  if (!aEditor || !aLeftNode || !aRightNode) { return NS_ERROR_NULL_POINTER; }
  mEditor = aEditor;
  mLeftNode = do_QueryInterface(aLeftNode);
  nsCOMPtr<nsIDOMNode>leftParent;
  nsresult result = mLeftNode->GetParentNode(getter_AddRefs(leftParent));
  NS_ENSURE_SUCCESS(result, result);
  if (!mEditor->IsModifiableNode(leftParent)) {
    return NS_ERROR_FAILURE;
  }
  mRightNode = do_QueryInterface(aRightNode);
  mOffset=0;
  return NS_OK;
}

// After DoTransaction() and RedoTransaction(), the left node is removed from the content tree and right node remains.
NS_IMETHODIMP JoinElementTxn::DoTransaction(void)
{
#ifdef DEBUG
  if (gNoisy)
  {
    printf("%p Do Join of %p and %p\n",
           static_cast<void*>(this),
           static_cast<void*>(mLeftNode.get()),
           static_cast<void*>(mRightNode.get()));
  }
#endif

  NS_PRECONDITION((mEditor && mLeftNode && mRightNode), "null arg");
  if (!mEditor || !mLeftNode || !mRightNode) { return NS_ERROR_NOT_INITIALIZED; }

  nsCOMPtr<nsINode> leftNode = do_QueryInterface(mLeftNode);
  NS_ENSURE_STATE(leftNode);

  nsCOMPtr<nsINode> rightNode = do_QueryInterface(mRightNode);
  NS_ENSURE_STATE(rightNode);

  // get the parent node
  nsCOMPtr<nsINode> leftParent = leftNode->GetParentNode();
  NS_ENSURE_TRUE(leftParent, NS_ERROR_NULL_POINTER);

  // verify that mLeftNode and mRightNode have the same parent
  nsCOMPtr<nsINode> rightParent = rightNode->GetParentNode();
  NS_ENSURE_TRUE(rightParent, NS_ERROR_NULL_POINTER);

  if (leftParent != rightParent) {
    NS_ASSERTION(false, "2 nodes do not have same parent");
    return NS_ERROR_INVALID_ARG;
  }

  // set this instance mParent. 
  // Other methods will see a non-null mParent and know all is well
  mParent = leftParent->AsDOMNode();
  mOffset = leftNode->Length();

  nsresult rv = mEditor->JoinNodesImpl(mRightNode, mLeftNode, mParent, false);

#ifdef DEBUG
  if (NS_SUCCEEDED(rv) && gNoisy) {
    printf("  left node = %p removed\n", static_cast<void*>(mLeftNode.get()));
  }
#endif

  return rv;
}

//XXX: what if instead of split, we just deleted the unneeded children of mRight
//     and re-inserted mLeft?
NS_IMETHODIMP JoinElementTxn::UndoTransaction(void)
{
#ifdef DEBUG
  if (gNoisy)
  {
    printf("%p Undo Join, right node = %p\n",
           static_cast<void*>(this),
           static_cast<void*>(mRightNode.get()));
  }
#endif

  NS_ASSERTION(mRightNode && mLeftNode && mParent, "bad state");
  if (!mRightNode || !mLeftNode || !mParent) { return NS_ERROR_NOT_INITIALIZED; }
  nsresult result;
  nsCOMPtr<nsIDOMNode>resultNode;
  // first, massage the existing node so it is in its post-split state
  nsCOMPtr<nsIDOMCharacterData>rightNodeAsText = do_QueryInterface(mRightNode);
  if (rightNodeAsText)
  {
    result = rightNodeAsText->DeleteData(0, mOffset);
  }
  else
  {
    nsCOMPtr<nsIDOMNode>child;
    result = mRightNode->GetFirstChild(getter_AddRefs(child));
    nsCOMPtr<nsIDOMNode>nextSibling;
    uint32_t i;
    for (i=0; i<mOffset; i++)
    {
      if (NS_FAILED(result)) {return result;}
      if (!child) {return NS_ERROR_NULL_POINTER;}
      child->GetNextSibling(getter_AddRefs(nextSibling));
      result = mLeftNode->AppendChild(child, getter_AddRefs(resultNode));
      child = do_QueryInterface(nextSibling);
    }
  }
  // second, re-insert the left node into the tree 
  result = mParent->InsertBefore(mLeftNode, mRightNode, getter_AddRefs(resultNode));
  return result;

}

NS_IMETHODIMP JoinElementTxn::GetTxnDescription(nsAString& aString)
{
  aString.AssignLiteral("JoinElementTxn");
  return NS_OK;
}
