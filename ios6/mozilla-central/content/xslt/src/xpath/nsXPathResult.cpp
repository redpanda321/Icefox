/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsXPathResult.h"
#include "txExprResult.h"
#include "txNodeSet.h"
#include "nsError.h"
#include "mozilla/dom/Element.h"
#include "nsIAttribute.h"
#include "nsDOMClassInfoID.h"
#include "nsIDOMNode.h"
#include "nsIDOMDocument.h"
#include "nsDOMString.h"
#include "txXPathTreeWalker.h"
#include "nsCycleCollectionParticipant.h"

using namespace mozilla::dom;

nsXPathResult::nsXPathResult() : mDocument(nullptr),
                                 mCurrentPos(0),
                                 mResultType(ANY_TYPE),
                                 mInvalidIteratorState(true),
                                 mBooleanResult(false),
                                 mNumberResult(0)
{
}

nsXPathResult::nsXPathResult(const nsXPathResult &aResult)
    : mResult(aResult.mResult),
      mResultNodes(aResult.mResultNodes),
      mDocument(aResult.mDocument),
      mCurrentPos(0),
      mResultType(aResult.mResultType),
      mContextNode(aResult.mContextNode),
      mInvalidIteratorState(aResult.mInvalidIteratorState)
{
    if (mDocument) {
        mDocument->AddMutationObserver(this);
    }
}

nsXPathResult::~nsXPathResult()
{
    RemoveObserver();
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsXPathResult)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsXPathResult)
    {
        tmp->RemoveObserver();
    }
    NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocument)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsXPathResult)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocument)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mResultNodes)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsXPathResult)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsXPathResult)

DOMCI_DATA(XPathResult, nsXPathResult)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsXPathResult)
    NS_INTERFACE_MAP_ENTRY(nsIDOMXPathResult)
    NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
    NS_INTERFACE_MAP_ENTRY(nsIXPathResult)
    NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMXPathResult)
    NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(XPathResult)
NS_INTERFACE_MAP_END

void
nsXPathResult::RemoveObserver()
{
    if (mDocument) {
        mDocument->RemoveMutationObserver(this);
    }
}

NS_IMETHODIMP
nsXPathResult::GetResultType(uint16_t *aResultType)
{
    *aResultType = mResultType;

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::GetNumberValue(double *aNumberValue)
{
    if (mResultType != NUMBER_TYPE) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    *aNumberValue = mNumberResult;

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::GetStringValue(nsAString &aStringValue)
{
    if (mResultType != STRING_TYPE) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    aStringValue = mStringResult;

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::GetBooleanValue(bool *aBooleanValue)
{
    if (mResultType != BOOLEAN_TYPE) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    *aBooleanValue = mBooleanResult;

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::GetSingleNodeValue(nsIDOMNode **aSingleNodeValue)
{
    if (!isNode()) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    if (mResultNodes.Count() > 0) {
        NS_ADDREF(*aSingleNodeValue = mResultNodes[0]);
    }
    else {
        *aSingleNodeValue = nullptr;
    }

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::GetInvalidIteratorState(bool *aInvalidIteratorState)
{
    *aInvalidIteratorState = isIterator() && mInvalidIteratorState;

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::GetSnapshotLength(uint32_t *aSnapshotLength)
{
    if (!isSnapshot()) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    *aSnapshotLength = (uint32_t)mResultNodes.Count();

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::IterateNext(nsIDOMNode **aResult)
{
    if (!isIterator()) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    if (mDocument) {
        mDocument->FlushPendingNotifications(Flush_Content);
    }

    if (mInvalidIteratorState) {
        return NS_ERROR_DOM_INVALID_STATE_ERR;
    }

    if (mCurrentPos < (uint32_t)mResultNodes.Count()) {
        NS_ADDREF(*aResult = mResultNodes[mCurrentPos++]);
    }
    else {
        *aResult = nullptr;
    }

    return NS_OK;
}

NS_IMETHODIMP
nsXPathResult::SnapshotItem(uint32_t aIndex, nsIDOMNode **aResult)
{
    if (!isSnapshot()) {
        return NS_ERROR_DOM_TYPE_ERR;
    }

    NS_IF_ADDREF(*aResult = mResultNodes.SafeObjectAt(aIndex));

    return NS_OK;
}

void
nsXPathResult::NodeWillBeDestroyed(const nsINode* aNode)
{
    nsCOMPtr<nsIMutationObserver> kungFuDeathGrip(this);
    // Set to null to avoid unregistring unnecessarily
    mDocument = nullptr;
    Invalidate(aNode->IsNodeOfType(nsINode::eCONTENT) ?
               static_cast<const nsIContent*>(aNode) : nullptr);
}

void
nsXPathResult::CharacterDataChanged(nsIDocument* aDocument,
                                    nsIContent *aContent,
                                    CharacterDataChangeInfo* aInfo)
{
    Invalidate(aContent);
}

void
nsXPathResult::AttributeChanged(nsIDocument* aDocument,
                                Element* aElement,
                                int32_t aNameSpaceID,
                                nsIAtom* aAttribute,
                                int32_t aModType)
{
    Invalidate(aElement);
}

void
nsXPathResult::ContentAppended(nsIDocument* aDocument,
                               nsIContent* aContainer,
                               nsIContent* aFirstNewContent,
                               int32_t aNewIndexInContainer)
{
    Invalidate(aContainer);
}

void
nsXPathResult::ContentInserted(nsIDocument* aDocument,
                               nsIContent* aContainer,
                               nsIContent* aChild,
                               int32_t aIndexInContainer)
{
    Invalidate(aContainer);
}

void
nsXPathResult::ContentRemoved(nsIDocument* aDocument,
                              nsIContent* aContainer,
                              nsIContent* aChild,
                              int32_t aIndexInContainer,
                              nsIContent* aPreviousSibling)
{
    Invalidate(aContainer);
}

nsresult
nsXPathResult::SetExprResult(txAExprResult* aExprResult, uint16_t aResultType,
                             nsINode* aContextNode)
{
    if ((isSnapshot(aResultType) || isIterator(aResultType) ||
         isNode(aResultType)) &&
        aExprResult->getResultType() != txAExprResult::NODESET) {
        // The DOM spec doesn't really say what should happen when reusing an
        // XPathResult and an error is thrown. Let's not touch the XPathResult
        // in that case.
        return NS_ERROR_DOM_TYPE_ERR;
    }

    mResultType = aResultType;
    mContextNode = do_GetWeakReference(aContextNode);

    if (mDocument) {
        mDocument->RemoveMutationObserver(this);
        mDocument = nullptr;
    }
 
    mResultNodes.Clear();

    // XXX This will keep the recycler alive, should we clear it?
    mResult = aExprResult;
    mBooleanResult = mResult->booleanValue();
    mNumberResult = mResult->numberValue();
    mResult->stringValue(mStringResult);

    if (aExprResult && aExprResult->getResultType() == txAExprResult::NODESET) {
        txNodeSet *nodeSet = static_cast<txNodeSet*>(aExprResult);
        nsCOMPtr<nsIDOMNode> node;
        int32_t i, count = nodeSet->size();
        for (i = 0; i < count; ++i) {
            txXPathNativeNode::getNode(nodeSet->get(i), getter_AddRefs(node));
            if (node) {
                mResultNodes.AppendObject(node);
            }
        }

        if (count > 0) {
            mResult = nullptr;
        }
    }

    if (!isIterator()) {
        return NS_OK;
    }

    mInvalidIteratorState = false;

    if (mResultNodes.Count() > 0) {
        // If we support the document() function in DOM-XPath we need to
        // observe all documents that we have resultnodes in.
        nsCOMPtr<nsIDOMDocument> document;
        mResultNodes[0]->GetOwnerDocument(getter_AddRefs(document));
        if (document) {
            mDocument = do_QueryInterface(document);
        }
        else {
            mDocument = do_QueryInterface(mResultNodes[0]);
        }

        NS_ASSERTION(mDocument, "We need a document!");
        if (mDocument) {
            mDocument->AddMutationObserver(this);
        }
    }

    return NS_OK;
}

void
nsXPathResult::Invalidate(const nsIContent* aChangeRoot)
{
    nsCOMPtr<nsINode> contextNode = do_QueryReferent(mContextNode);
    if (contextNode && aChangeRoot && aChangeRoot->GetBindingParent()) {
        // If context node is in anonymous content, changes to
        // non-anonymous content need to invalidate the XPathResult. If
        // the changes are happening in a different anonymous trees, no
        // invalidation should happen.
        nsIContent* ctxBindingParent = nullptr;
        if (contextNode->IsNodeOfType(nsINode::eCONTENT)) {
            ctxBindingParent =
                static_cast<nsIContent*>(contextNode.get())
                    ->GetBindingParent();
        } else if (contextNode->IsNodeOfType(nsINode::eATTRIBUTE)) {
            nsIContent* parent =
              static_cast<nsIAttribute*>(contextNode.get())->GetContent();
            if (parent) {
                ctxBindingParent = parent->GetBindingParent();
            }
        }
        if (ctxBindingParent != aChangeRoot->GetBindingParent()) {
          return;
        }
    }

    mInvalidIteratorState = true;
    // Make sure nulling out mDocument is the last thing we do.
    if (mDocument) {
        mDocument->RemoveMutationObserver(this);
        mDocument = nullptr;
    }
}

nsresult
nsXPathResult::GetExprResult(txAExprResult** aExprResult)
{
    if (isIterator() && mInvalidIteratorState) {
        return NS_ERROR_DOM_INVALID_STATE_ERR;
    }

    if (mResult) {
        NS_ADDREF(*aExprResult = mResult);

        return NS_OK;
    }

    if (mResultNodes.Count() == 0) {
        return NS_ERROR_DOM_INVALID_STATE_ERR;
    }

    nsRefPtr<txNodeSet> nodeSet = new txNodeSet(nullptr);
    if (!nodeSet) {
        return NS_ERROR_OUT_OF_MEMORY;
    }

    uint32_t i, count = mResultNodes.Count();
    for (i = 0; i < count; ++i) {
        nsAutoPtr<txXPathNode> node(txXPathNativeNode::createXPathNode(mResultNodes[i]));
        if (!node) {
            return NS_ERROR_OUT_OF_MEMORY;
        }

        nodeSet->append(*node);
    }

    NS_ADDREF(*aExprResult = nodeSet);

    return NS_OK;
}

nsresult
nsXPathResult::Clone(nsIXPathResult **aResult)
{
    *aResult = nullptr;

    if (isIterator() && mInvalidIteratorState) {
        return NS_ERROR_DOM_INVALID_STATE_ERR;
    }

    nsCOMPtr<nsIXPathResult> result = new nsXPathResult(*this);
    if (!result) {
        return NS_ERROR_OUT_OF_MEMORY;
    }

    result.swap(*aResult);

    return NS_OK;
}
