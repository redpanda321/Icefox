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

#ifndef nsTreeWalker_h___
#define nsTreeWalker_h___

#include "nsIDOMTreeWalker.h"
#include "nsTraversal.h"
#include "nsCOMPtr.h"
#include "nsTArray.h"
#include "nsCycleCollectionParticipant.h"

class nsINode;
class nsIDOMNode;
class nsIDOMNodeFilter;

class nsTreeWalker : public nsIDOMTreeWalker, public nsTraversal
{
public:
    NS_DECL_CYCLE_COLLECTING_ISUPPORTS
    NS_DECL_NSIDOMTREEWALKER

    nsTreeWalker(nsINode *aRoot,
                 PRUint32 aWhatToShow,
                 nsIDOMNodeFilter *aFilter,
                 PRBool aExpandEntityReferences);
    virtual ~nsTreeWalker();

    NS_DECL_CYCLE_COLLECTION_CLASS(nsTreeWalker)

private:
    nsCOMPtr<nsINode> mCurrentNode;

    /*
     * Implements FirstChild and LastChild which only vary in which direction
     * they search.
     * @param aReversed Controls whether we search forwards or backwards
     * @param _retval   Returned node. Null if no child is found
     * @returns         Errorcode
     */
    nsresult FirstChildInternal(PRBool aReversed, nsIDOMNode **_retval);

    /*
     * Implements NextSibling and PreviousSibling which only vary in which
     * direction they search.
     * @param aReversed Controls whether we search forwards or backwards
     * @param _retval   Returned node. Null if no child is found
     * @returns         Errorcode
     */
    nsresult NextSiblingInternal(PRBool aReversed, nsIDOMNode **_retval);
};

#endif

