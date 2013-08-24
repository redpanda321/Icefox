/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set ts=4 et sw=4 tw=80: */
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
 * The Original Code is this file as it was released on May 1 2001.
 *
 * The Initial Developer of the Original Code is
 * Jonas Sicking.
 * Portions created by the Initial Developer are Copyright (C) 2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Jonas Sicking <sicking@bigfoot.com> (Original Author)
 *   Craig Topper  <craig.topper@gmail.com>
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

/*
 * Implementation of DOM Traversal's nsIDOMTreeWalker
 */

#include "nsTreeWalker.h"

#include "nsIDOMNode.h"
#include "nsIDOMNodeFilter.h"
#include "nsDOMError.h"

#include "nsContentUtils.h"

/*
 * Factories, constructors and destructors
 */

nsTreeWalker::nsTreeWalker(nsINode *aRoot,
                           PRUint32 aWhatToShow,
                           nsIDOMNodeFilter *aFilter,
                           PRBool aExpandEntityReferences) :
    nsTraversal(aRoot, aWhatToShow, aFilter, aExpandEntityReferences),
    mCurrentNode(aRoot)
{
}

nsTreeWalker::~nsTreeWalker()
{
    /* destructor code */
}

/*
 * nsISupports and cycle collection stuff
 */

NS_IMPL_CYCLE_COLLECTION_3(nsTreeWalker, mFilter, mCurrentNode, mRoot)

DOMCI_DATA(TreeWalker, nsTreeWalker)

// QueryInterface implementation for nsTreeWalker
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsTreeWalker)
    NS_INTERFACE_MAP_ENTRY(nsIDOMTreeWalker)
    NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMTreeWalker)
    NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(TreeWalker)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsTreeWalker)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsTreeWalker)



/*
 * nsIDOMTreeWalker Getters/Setters
 */

/* readonly attribute nsIDOMNode root; */
NS_IMETHODIMP nsTreeWalker::GetRoot(nsIDOMNode * *aRoot)
{
    if (mRoot) {
        return CallQueryInterface(mRoot, aRoot);
    }

    *aRoot = nsnull;

    return NS_OK;
}

/* readonly attribute unsigned long whatToShow; */
NS_IMETHODIMP nsTreeWalker::GetWhatToShow(PRUint32 *aWhatToShow)
{
    *aWhatToShow = mWhatToShow;
    return NS_OK;
}

/* readonly attribute nsIDOMNodeFilter filter; */
NS_IMETHODIMP nsTreeWalker::GetFilter(nsIDOMNodeFilter * *aFilter)
{
    NS_ENSURE_ARG_POINTER(aFilter);

    NS_IF_ADDREF(*aFilter = mFilter);

    return NS_OK;
}

/* readonly attribute boolean expandEntityReferences; */
NS_IMETHODIMP
nsTreeWalker::GetExpandEntityReferences(PRBool *aExpandEntityReferences)
{
    *aExpandEntityReferences = mExpandEntityReferences;
    return NS_OK;
}

/* attribute nsIDOMNode currentNode; */
NS_IMETHODIMP nsTreeWalker::GetCurrentNode(nsIDOMNode * *aCurrentNode)
{
    if (mCurrentNode) {
        return CallQueryInterface(mCurrentNode, aCurrentNode);
    }

    *aCurrentNode = nsnull;

    return NS_OK;
}
NS_IMETHODIMP nsTreeWalker::SetCurrentNode(nsIDOMNode * aCurrentNode)
{
    NS_ENSURE_TRUE(aCurrentNode, NS_ERROR_DOM_NOT_SUPPORTED_ERR);

    nsresult rv = nsContentUtils::CheckSameOrigin(mRoot, aCurrentNode);
    NS_ENSURE_SUCCESS(rv, rv);

    mCurrentNode = do_QueryInterface(aCurrentNode);

    return NS_OK;
}

/*
 * nsIDOMTreeWalker functions
 */

/* nsIDOMNode parentNode (); */
NS_IMETHODIMP nsTreeWalker::ParentNode(nsIDOMNode **_retval)
{
    *_retval = nsnull;

    nsresult rv;

    nsCOMPtr<nsINode> node = mCurrentNode;

    while (node && node != mRoot) {
        node = node->GetNodeParent();

        if (node) {
            PRInt16 filtered;
            rv = TestNode(node, &filtered);
            NS_ENSURE_SUCCESS(rv, rv);
            if (filtered == nsIDOMNodeFilter::FILTER_ACCEPT) {
                mCurrentNode = node;
                return CallQueryInterface(node, _retval);
            }
        }
    }

    return NS_OK;
}

/* nsIDOMNode firstChild (); */
NS_IMETHODIMP nsTreeWalker::FirstChild(nsIDOMNode **_retval)
{
    return FirstChildInternal(PR_FALSE, _retval);
}

/* nsIDOMNode lastChild (); */
NS_IMETHODIMP nsTreeWalker::LastChild(nsIDOMNode **_retval)
{
    return FirstChildInternal(PR_TRUE, _retval);
}

/* nsIDOMNode previousSibling (); */
NS_IMETHODIMP nsTreeWalker::PreviousSibling(nsIDOMNode **_retval)
{
    return NextSiblingInternal(PR_TRUE, _retval);
}

/* nsIDOMNode nextSibling (); */
NS_IMETHODIMP nsTreeWalker::NextSibling(nsIDOMNode **_retval)
{
    return NextSiblingInternal(PR_FALSE, _retval);
}

/* nsIDOMNode previousNode (); */
NS_IMETHODIMP nsTreeWalker::PreviousNode(nsIDOMNode **_retval)
{
    nsresult rv;
    PRInt16 filtered;

    *_retval = nsnull;

    nsCOMPtr<nsINode> node = mCurrentNode;

    while (node != mRoot) {
        while (nsINode *previousSibling = node->GetPreviousSibling()) {
            node = previousSibling;

            rv = TestNode(node, &filtered);
            NS_ENSURE_SUCCESS(rv, rv);

            nsINode *lastChild;
            while (filtered != nsIDOMNodeFilter::FILTER_REJECT &&
                   (lastChild = node->GetLastChild())) {
                node = lastChild;
                rv = TestNode(node, &filtered);
                NS_ENSURE_SUCCESS(rv, rv);
            }

            if (filtered == nsIDOMNodeFilter::FILTER_ACCEPT) {
                mCurrentNode = node;
                return CallQueryInterface(node, _retval);
            }
        }

        if (node == mRoot)
            break;

        node = node->GetNodeParent();
        if (!node)
            break;

        rv = TestNode(node, &filtered);
        NS_ENSURE_SUCCESS(rv, rv);

        if (filtered == nsIDOMNodeFilter::FILTER_ACCEPT) {
            mCurrentNode = node;
            return CallQueryInterface(node, _retval);
        }
    }

    return NS_OK;
}

/* nsIDOMNode nextNode (); */
NS_IMETHODIMP nsTreeWalker::NextNode(nsIDOMNode **_retval)
{
    nsresult rv;
    PRInt16 filtered = nsIDOMNodeFilter::FILTER_ACCEPT; // pre-init for inner loop

    *_retval = nsnull;

    nsCOMPtr<nsINode> node = mCurrentNode;

    while (1) {

        nsINode *firstChild;
        while (filtered != nsIDOMNodeFilter::FILTER_REJECT &&
               (firstChild = node->GetFirstChild())) {
            node = firstChild;

            rv = TestNode(node, &filtered);
            NS_ENSURE_SUCCESS(rv, rv);

            if (filtered ==  nsIDOMNodeFilter::FILTER_ACCEPT) {
                // Node found
                mCurrentNode = node;
                return CallQueryInterface(node, _retval);
            }
        }

        nsINode *sibling = nsnull;
        nsINode *temp = node;
        do {
            if (temp == mRoot)
                break;

            sibling = temp->GetNextSibling();
            if (sibling)
                break;

            temp = temp->GetNodeParent();
        } while (temp);

        if (!sibling)
            break;

        node = sibling;

        // Found a sibling. Either ours or ancestor's
        rv = TestNode(node, &filtered);
        NS_ENSURE_SUCCESS(rv, rv);

        if (filtered ==  nsIDOMNodeFilter::FILTER_ACCEPT) {
            // Node found
            mCurrentNode = node;
            return CallQueryInterface(node, _retval);
        }
    }

    return NS_OK;
}

/*
 * nsTreeWalker helper functions
 */

/*
 * Implements FirstChild and LastChild which only vary in which direction
 * they search.
 * @param aReversed Controls whether we search forwards or backwards
 * @param _retval   Returned node. Null if no child is found
 * @returns         Errorcode
 */
nsresult nsTreeWalker::FirstChildInternal(PRBool aReversed, nsIDOMNode **_retval)
{
    nsresult rv;
    PRInt16 filtered;

    *_retval = nsnull;

    nsCOMPtr<nsINode> node = aReversed ? mCurrentNode->GetLastChild()
                                       : mCurrentNode->GetFirstChild();

    while (node) {
        rv = TestNode(node, &filtered);
        NS_ENSURE_SUCCESS(rv, rv);

        switch (filtered) {
            case nsIDOMNodeFilter::FILTER_ACCEPT:
                // Node found
                mCurrentNode = node;
                return CallQueryInterface(node, _retval);
            case nsIDOMNodeFilter::FILTER_SKIP: {
                    nsINode *child = aReversed ? node->GetLastChild()
                                               : node->GetFirstChild();
                    if (child) {
                        node = child;
                        continue;
                    }
                    break;
                }
            case nsIDOMNodeFilter::FILTER_REJECT:
                // Keep searching
                break;
        }

        do {
            nsINode *sibling = aReversed ? node->GetPreviousSibling()
                                         : node->GetNextSibling();
            if (sibling) {
                node = sibling;
                break;
            }
            nsINode *parent = node->GetNodeParent();

            if (!parent || parent == mRoot || parent == mCurrentNode) {
                return NS_OK;
            }

        } while (node);
    }

    return NS_OK;
}

/*
 * Implements NextSibling and PreviousSibling which only vary in which
 * direction they search.
 * @param aReversed Controls whether we search forwards or backwards
 * @param _retval   Returned node. Null if no child is found
 * @returns         Errorcode
 */
nsresult nsTreeWalker::NextSiblingInternal(PRBool aReversed, nsIDOMNode **_retval)

{
    nsresult rv;
    PRInt16 filtered;

    *_retval = nsnull;

    nsCOMPtr<nsINode> node = mCurrentNode;

    if (node == mRoot)
        return NS_OK;

    while (1) {
        nsCOMPtr<nsINode> sibling = aReversed ? node->GetPreviousSibling()
                                              : node->GetNextSibling();

        while (sibling) {
            rv = TestNode(sibling, &filtered);
            NS_ENSURE_SUCCESS(rv, rv);

            switch (filtered) {
                case nsIDOMNodeFilter::FILTER_ACCEPT:
                    // Node found
                    mCurrentNode = sibling;
                    return CallQueryInterface(sibling, _retval);
                case nsIDOMNodeFilter::FILTER_SKIP: {
                        nsINode *firstChild = aReversed ? sibling->GetLastChild()
                                                        : sibling->GetFirstChild();
                        if (firstChild) {
                            sibling = firstChild;
                            continue;
                        }
                    }
                    break;
                case nsIDOMNodeFilter::FILTER_REJECT:
                    // Keep searching
                    break;
            }
            sibling = aReversed ? sibling->GetPreviousSibling()
                                : sibling->GetNextSibling();
        }

        node = node->GetNodeParent();

        if (!node || node == mRoot)
            break;

        // Is parent transparent in filtered view?
        rv = TestNode(node, &filtered);
        NS_ENSURE_SUCCESS(rv, rv);
        if (filtered == nsIDOMNodeFilter::FILTER_ACCEPT)
            break;
    }

    return NS_OK;
}
