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
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2007
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

#include "nsAccessibleRelation.h"

#include "nsArrayUtils.h"
#include "nsComponentManagerUtils.h"

nsAccessibleRelation::
  nsAccessibleRelation(PRUint32 aType, nsIAccessible *aTarget) :
  mType(aType)
{
  mTargets = do_CreateInstance(NS_ARRAY_CONTRACTID);
  if (aTarget)
    mTargets->AppendElement(aTarget, PR_FALSE);
}

// nsISupports
NS_IMPL_ISUPPORTS2(nsAccessibleRelation, nsAccessibleRelation,
                   nsIAccessibleRelation)

// nsIAccessibleRelation
NS_IMETHODIMP
nsAccessibleRelation::GetRelationType(PRUint32 *aType)
{
  NS_ENSURE_ARG_POINTER(aType);

  *aType = mType;
  return NS_OK;
}

NS_IMETHODIMP
nsAccessibleRelation::GetTargetsCount(PRUint32 *aCount)
{
  NS_ENSURE_ARG_POINTER(aCount);
  *aCount = 0;

  NS_ENSURE_TRUE(mTargets, NS_ERROR_NOT_INITIALIZED);

  return mTargets->GetLength(aCount);
}

NS_IMETHODIMP
nsAccessibleRelation::GetTarget(PRUint32 aIndex, nsIAccessible **aTarget)
{
  NS_ENSURE_ARG_POINTER(aTarget);
  *aTarget = nsnull;

  NS_ENSURE_TRUE(mTargets, NS_ERROR_NOT_INITIALIZED);

  nsresult rv = NS_OK;
  nsCOMPtr<nsIAccessible> target = do_QueryElementAt(mTargets, aIndex, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  target.swap(*aTarget);
  return NS_OK;
}

NS_IMETHODIMP
nsAccessibleRelation::GetTargets(nsIArray **aTargets)
{
  NS_ENSURE_ARG_POINTER(aTargets);
  *aTargets = nsnull;

  NS_ENSURE_TRUE(mTargets, NS_ERROR_NOT_INITIALIZED);

  NS_ADDREF(*aTargets = mTargets);
  return NS_OK;
}

// nsAccessibleRelation
nsresult
nsAccessibleRelation::AddTarget(nsIAccessible *aTarget)
{
  NS_ENSURE_ARG(aTarget);

  NS_ENSURE_TRUE(mTargets, NS_ERROR_NOT_INITIALIZED);

  return mTargets->AppendElement(aTarget, PR_FALSE);
}
