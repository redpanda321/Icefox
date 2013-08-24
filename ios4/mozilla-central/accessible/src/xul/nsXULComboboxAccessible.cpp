/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Aaron Leventhal <aaronl@netscape.com> (original author)
 *   Kyle Yuan <kyle.yuan@sun.com>
 *   Alexander Surkov <surkov.alexander@gmail.com>
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

#include "nsXULComboboxAccessible.h"

#include "nsAccessibilityService.h"
#include "nsCoreUtils.h"

#include "nsIDOMXULMenuListElement.h"
#include "nsIDOMXULSelectCntrlItemEl.h"

////////////////////////////////////////////////////////////////////////////////
// nsXULComboboxAccessible
////////////////////////////////////////////////////////////////////////////////

nsXULComboboxAccessible::
  nsXULComboboxAccessible(nsIContent *aContent, nsIWeakReference *aShell) :
  nsAccessibleWrap(aContent, aShell)
{
}

PRBool
nsXULComboboxAccessible::Init()
{
  if (!nsAccessibleWrap::Init())
    return PR_FALSE;

  nsCoreUtils::GeneratePopupTree(mContent);
  return PR_TRUE;
}

nsresult
nsXULComboboxAccessible::GetRoleInternal(PRUint32 *aRole)
{
  if (IsDefunct())
    return NS_ERROR_FAILURE;

  if (mContent->AttrValueIs(kNameSpaceID_None, nsAccessibilityAtoms::type,
                            nsAccessibilityAtoms::autocomplete, eIgnoreCase)) {
    *aRole = nsIAccessibleRole::ROLE_AUTOCOMPLETE;
  } else {
    *aRole = nsIAccessibleRole::ROLE_COMBOBOX;
  }

  return NS_OK;
}

nsresult
nsXULComboboxAccessible::GetStateInternal(PRUint32 *aState,
                                          PRUint32 *aExtraState)
{
  // As a nsComboboxAccessible we can have the following states:
  //     STATE_FOCUSED
  //     STATE_FOCUSABLE
  //     STATE_HASPOPUP
  //     STATE_EXPANDED
  //     STATE_COLLAPSED

  // Get focus status from base class
  nsresult rv = nsAccessible::GetStateInternal(aState, aExtraState);
  NS_ENSURE_A11Y_SUCCESS(rv, rv);

  nsCOMPtr<nsIDOMXULMenuListElement> menuList(do_QueryInterface(mContent));
  if (menuList) {
    PRBool isOpen;
    menuList->GetOpen(&isOpen);
    if (isOpen) {
      *aState |= nsIAccessibleStates::STATE_EXPANDED;
    }
    else {
      *aState |= nsIAccessibleStates::STATE_COLLAPSED;
    }
  }

  *aState |= nsIAccessibleStates::STATE_HASPOPUP |
             nsIAccessibleStates::STATE_FOCUSABLE;

  return NS_OK;
}

NS_IMETHODIMP
nsXULComboboxAccessible::GetValue(nsAString& aValue)
{
  aValue.Truncate();

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  // The value is the option or text shown entered in the combobox.
  nsCOMPtr<nsIDOMXULMenuListElement> menuList(do_QueryInterface(mContent));
  if (menuList)
    return menuList->GetLabel(aValue);

  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsXULComboboxAccessible::GetDescription(nsAString& aDescription)
{
  aDescription.Truncate();

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  // Use description of currently focused option
  nsCOMPtr<nsIDOMXULMenuListElement> menuListElm(do_QueryInterface(mContent));
  if (!menuListElm)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIDOMXULSelectControlItemElement> focusedOptionItem;
  menuListElm->GetSelectedItem(getter_AddRefs(focusedOptionItem));
  nsCOMPtr<nsIContent> focusedOptionContent =
    do_QueryInterface(focusedOptionItem);
  if (focusedOptionContent) {
    nsAccessible *focusedOption =
      GetAccService()->GetAccessibleInWeakShell(focusedOptionContent, mWeakShell);
    NS_ENSURE_TRUE(focusedOption, NS_ERROR_FAILURE);

    return focusedOption->GetDescription(aDescription);
  }

  return NS_OK;
}

PRBool
nsXULComboboxAccessible::GetAllowsAnonChildAccessibles()
{
  if (mContent->NodeInfo()->Equals(nsAccessibilityAtoms::textbox, kNameSpaceID_XUL) ||
      mContent->AttrValueIs(kNameSpaceID_None, nsAccessibilityAtoms::editable,
                            nsAccessibilityAtoms::_true, eIgnoreCase)) {
    // Both the XUL <textbox type="autocomplete"> and <menulist editable="true"> widgets
    // use nsXULComboboxAccessible. We need to walk the anonymous children for these
    // so that the entry field is a child
    return PR_TRUE;
  }

  // Argument of PR_FALSE indicates we don't walk anonymous children for
  // menuitems
  return PR_FALSE;
}

NS_IMETHODIMP
nsXULComboboxAccessible::GetNumActions(PRUint8 *aNumActions)
{
  NS_ENSURE_ARG_POINTER(aNumActions);

  // Just one action (click).
  *aNumActions = 1;
  return NS_OK;
}

NS_IMETHODIMP
nsXULComboboxAccessible::DoAction(PRUint8 aIndex)
{
  if (aIndex != nsXULComboboxAccessible::eAction_Click) {
    return NS_ERROR_INVALID_ARG;
  }

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  // Programmaticaly toggle the combo box.
  nsCOMPtr<nsIDOMXULMenuListElement> menuList(do_QueryInterface(mContent));
  if (!menuList) {
    return NS_ERROR_FAILURE;
  }
  PRBool isDroppedDown;
  menuList->GetOpen(&isDroppedDown);
  return menuList->SetOpen(!isDroppedDown);
}

NS_IMETHODIMP
nsXULComboboxAccessible::GetActionName(PRUint8 aIndex, nsAString& aName)
{
  if (aIndex != nsXULComboboxAccessible::eAction_Click) {
    return NS_ERROR_INVALID_ARG;
  }

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  // Our action name is the reverse of our state:
  //     if we are close -> open is our name.
  //     if we are open -> close is our name.
  // Uses the frame to get the state, updated on every click.

  nsCOMPtr<nsIDOMXULMenuListElement> menuList(do_QueryInterface(mContent));
  if (!menuList) {
    return NS_ERROR_FAILURE;
  }
  PRBool isDroppedDown;
  menuList->GetOpen(&isDroppedDown);
  if (isDroppedDown)
    aName.AssignLiteral("close"); 
  else
    aName.AssignLiteral("open"); 

  return NS_OK;
}
