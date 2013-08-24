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
 *   Author: Aaron Leventhal (aaronl@netscape.com)
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

#ifndef _nsXULMenuAccessible_H_
#define _nsXULMenuAccessible_H_

#include "nsAccessibleWrap.h"
#include "nsIAccessibleSelectable.h"
#include "nsIDOMXULSelectCntrlEl.h"

/**
 * The basic implementation of nsIAccessibleSelectable.
 */
class nsXULSelectableAccessible : public nsAccessibleWrap
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIACCESSIBLESELECTABLE

  nsXULSelectableAccessible(nsIContent *aContent, nsIWeakReference *aShell);
  virtual ~nsXULSelectableAccessible() {}

  // nsAccessNode
  virtual void Shutdown();

protected:
  nsresult ChangeSelection(PRInt32 aIndex, PRUint8 aMethod, PRBool *aSelState);

  // nsIDOMXULMultiSelectControlElement inherits from this, so we'll always have
  // one of these if the widget is valid and not defunct
  nsCOMPtr<nsIDOMXULSelectControlElement> mSelectControl;
};

/**
 * Used for XUL menu, menuitem elements.
 */
class nsXULMenuitemAccessible : public nsAccessibleWrap
{
public:
  enum { eAction_Click = 0 };

  nsXULMenuitemAccessible(nsIContent *aContent, nsIWeakReference *aShell);

  // nsIAccessible
  NS_IMETHOD GetDescription(nsAString& aDescription);
  NS_IMETHOD GetKeyboardShortcut(nsAString& _retval);
  NS_IMETHOD GetDefaultKeyBinding(nsAString& aKeyBinding);
  NS_IMETHOD DoAction(PRUint8 index);
  NS_IMETHOD GetActionName(PRUint8 aIndex, nsAString& aName);
  NS_IMETHOD GetNumActions(PRUint8 *_retval);

  // nsAccessNode
  virtual PRBool Init();

  // nsAccessible
  virtual nsresult GetNameInternal(nsAString& aName);
  virtual nsresult GetRoleInternal(PRUint32 *aRole);
  virtual nsresult GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState);
  virtual PRInt32 GetLevelInternal();
  virtual void GetPositionAndSizeInternal(PRInt32 *aPosInSet,
                                          PRInt32 *aSetSize);

  virtual PRBool GetAllowsAnonChildAccessibles();
};

/**
 * Used for XUL menuseparator element.
 */
class nsXULMenuSeparatorAccessible : public nsXULMenuitemAccessible
{
public:
  nsXULMenuSeparatorAccessible(nsIContent *aContent, nsIWeakReference *aShell);

  // nsIAccessible
  NS_IMETHOD DoAction(PRUint8 index);
  NS_IMETHOD GetActionName(PRUint8 aIndex, nsAString& aName);
  NS_IMETHOD GetNumActions(PRUint8 *_retval);

  // nsAccessible
  virtual nsresult GetNameInternal(nsAString& aName);
  virtual nsresult GetRoleInternal(PRUint32 *aRole);
  virtual nsresult GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState);
};


/**
 * Used for XUL menupopup and panel.
 */
class nsXULMenupopupAccessible : public nsXULSelectableAccessible
{
public:
  nsXULMenupopupAccessible(nsIContent *aContent, nsIWeakReference *aShell);

  // nsAccessible
  virtual nsresult GetNameInternal(nsAString& aName);
  virtual nsresult GetRoleInternal(PRUint32 *aRole);
  virtual nsresult GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState);
};

/**
 * Used for XUL menubar element.
 */
class nsXULMenubarAccessible : public nsAccessibleWrap
{
public:
  nsXULMenubarAccessible(nsIContent *aContent, nsIWeakReference *aShell);

  // nsAccessible
  virtual nsresult GetNameInternal(nsAString& aName);
  virtual nsresult GetRoleInternal(PRUint32 *aRole);
  virtual nsresult GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState);
};

#endif  
