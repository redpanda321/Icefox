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
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Aaron Leventhal (aaronl@netscape.com)
 *   Kyle Yuan (kyle.yuan@sun.com)
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
#ifndef __nsHTMLSelectAccessible_h__
#define __nsHTMLSelectAccessible_h__

#include "nsIAccessibleSelectable.h"
#include "nsAccessibilityAtoms.h"
#include "nsHTMLFormControlAccessible.h"
#include "nsIDOMHTMLOptionsCollection.h"
#include "nsIDOMHTMLOptionElement.h"
#include "nsIDOMNode.h"

class nsIMutableArray;

/**
  *  Selects, Listboxes and Comboboxes, are made up of a number of different
  *  widgets, some of which are shared between the two. This file contains
	*  all of the widgets for both of the Selects, for HTML only.
  *
  *  Listbox:
  *     - nsHTMLSelectListAccessible
  *        - nsHTMLSelectOptionAccessible
  *
  *  Comboboxes:
  *     - nsHTMLComboboxAccessible
  *        - nsHTMLComboboxListAccessible  [ inserted in accessible tree ]
  *           - nsHTMLSelectOptionAccessible(s)
  */

/** ------------------------------------------------------ */
/**  First, the common widgets                             */
/** ------------------------------------------------------ */

/*
 * The HTML implementation of nsIAccessibleSelectable.
 */
class nsHTMLSelectableAccessible : public nsAccessibleWrap
{
public:

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIACCESSIBLESELECTABLE

  nsHTMLSelectableAccessible(nsIContent *aContent, nsIWeakReference *aShell);
  virtual ~nsHTMLSelectableAccessible() {}

protected:

  NS_IMETHOD ChangeSelection(PRInt32 aIndex, PRUint8 aMethod, PRBool *aSelState);

  class iterator 
  {
  protected:
    PRUint32 mLength;
    PRUint32 mIndex;
    PRInt32 mSelCount;
    nsCOMPtr<nsIDOMHTMLOptionsCollection> mOptions;
    nsCOMPtr<nsIDOMHTMLOptionElement> mOption;
    nsCOMPtr<nsIWeakReference> mWeakShell;
    nsHTMLSelectableAccessible *mParentSelect;

  public:
    iterator(nsHTMLSelectableAccessible *aParent, nsIWeakReference *aWeakShell);

    void CalcSelectionCount(PRInt32 *aSelectionCount);
    void Select(PRBool aSelect);
    void AddAccessibleIfSelected(nsIMutableArray *aSelectedAccessibles, 
                                 nsPresContext *aContext);
    PRBool GetAccessibleIfSelected(PRInt32 aIndex, nsPresContext *aContext,
                                   nsIAccessible **aAccessible);

    PRBool Advance();
  };

  friend class iterator;
};

/*
 * The list that contains all the options in the select.
 */
class nsHTMLSelectListAccessible : public nsHTMLSelectableAccessible
{
public:
  
  nsHTMLSelectListAccessible(nsIContent *aContent, nsIWeakReference *aShell);
  virtual ~nsHTMLSelectListAccessible() {}

  // nsAccessible
  virtual nsresult GetRoleInternal(PRUint32 *aRole);
  virtual nsresult GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState);

protected:

  // nsAccessible
  virtual void CacheChildren();

  // nsHTMLSelectListAccessible

  /**
   * Recursive helper for CacheChildren().
   */
  void CacheOptSiblings(nsIContent *aParentContent);
};

/*
 * Options inside the select, contained within the list
 */
class nsHTMLSelectOptionAccessible : public nsHyperTextAccessibleWrap
{
public:
  enum { eAction_Select = 0 };  
  
  nsHTMLSelectOptionAccessible(nsIContent *aContent, nsIWeakReference *aShell);
  virtual ~nsHTMLSelectOptionAccessible() {}

  // nsIAccessible
  NS_IMETHOD DoAction(PRUint8 index);
  NS_IMETHOD GetActionName(PRUint8 aIndex, nsAString& aName);
  NS_IMETHOD GetNumActions(PRUint8 *_retval);

  // nsAccessible
  virtual nsresult GetNameInternal(nsAString& aName);
  virtual nsresult GetRoleInternal(PRUint32 *aRole);
  virtual nsresult GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState);

  virtual PRInt32 GetLevelInternal();
  virtual void GetPositionAndSizeInternal(PRInt32 *aPosInSet,
                                          PRInt32 *aSetSize);

  /**
   * Return focused option if any.
   */
  static already_AddRefed<nsIContent> GetFocusedOption(nsIContent *aListNode);

  static void SelectionChangedIfOption(nsIContent *aPossibleOption);

protected:
  // nsAccessible
  virtual nsIFrame* GetBoundsFrame();

private:
  
  /**
   * Get Select element's accessible state
   * @param aState, Select element state
   * @param aExtraState, Select element extra state
   * @return Select element content, returns null if not avaliable
   */ 
  nsIContent* GetSelectState(PRUint32* aState, PRUint32* aExtraState = nsnull);
};

/*
 * Opt Groups inside the select, contained within the list
 */
class nsHTMLSelectOptGroupAccessible : public nsHTMLSelectOptionAccessible
{
public:

  nsHTMLSelectOptGroupAccessible(nsIContent *aContent, nsIWeakReference *aShell);
  virtual ~nsHTMLSelectOptGroupAccessible() {}

  // nsIAccessible
  NS_IMETHOD DoAction(PRUint8 index);  
  NS_IMETHOD GetActionName(PRUint8 aIndex, nsAString& aName);
  NS_IMETHOD GetNumActions(PRUint8 *_retval);

  // nsAccessible
  virtual nsresult GetRoleInternal(PRUint32 *aRole);
  virtual nsresult GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState);

protected:
  // nsAccessible
  virtual void CacheChildren();
};

/** ------------------------------------------------------ */
/**  Finally, the Combobox widgets                         */
/** ------------------------------------------------------ */

class nsHTMLComboboxListAccessible;

/*
 * A class the represents the HTML Combobox widget.
 */
class nsHTMLComboboxAccessible : public nsAccessibleWrap
{
public:
  enum { eAction_Click = 0 };

  nsHTMLComboboxAccessible(nsIContent *aContent, nsIWeakReference *aShell);
  virtual ~nsHTMLComboboxAccessible() {}

  // nsIAccessible
  NS_IMETHOD GetValue(nsAString& _retval);
  NS_IMETHOD GetDescription(nsAString& aDescription);
  NS_IMETHOD DoAction(PRUint8 index);
  NS_IMETHOD GetNumActions(PRUint8 *aNumActions);
  NS_IMETHOD GetActionName(PRUint8 aIndex, nsAString& aName);

  // nsAccessNode
  virtual void Shutdown();

  // nsAccessible
  virtual nsresult GetRoleInternal(PRUint32 *aRole);
  virtual nsresult GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState);

protected:
  // nsAccessible
  virtual void CacheChildren();

  // nsHTMLComboboxAccessible

  /**
   * Return focused option accessible.
   */
  nsAccessible *GetFocusedOptionAccessible();

private:
  nsRefPtr<nsHTMLComboboxListAccessible> mListAccessible;
};

/*
 * A class that represents the window that lives to the right
 * of the drop down button inside the Select. This is the window
 * that is made visible when the button is pressed.
 */
class nsHTMLComboboxListAccessible : public nsHTMLSelectListAccessible
{
public:

  nsHTMLComboboxListAccessible(nsIAccessible *aParent, 
                               nsIContent *aContent, 
                               nsIWeakReference* aShell);
  virtual ~nsHTMLComboboxListAccessible() {}

  // nsIAccessible
  NS_IMETHOD GetUniqueID(void **aUniqueID);

  // nsAccessNode
  virtual nsIFrame* GetFrame();

  // nsAccessible
  virtual nsresult GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState);
  virtual void GetBoundsRect(nsRect& aBounds, nsIFrame** aBoundingFrame);
};

#endif
