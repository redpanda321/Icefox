/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This defines a common base class for nsITheme implementations, to reduce
// code duplication.

#include "prtypes.h"
#include "nsAlgorithm.h"
#include "nsIAtom.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsMargin.h"
#include "nsGkAtoms.h"
#include "nsEventStates.h"
#include "nsTArray.h"
#include "nsITimer.h"

class nsIContent;
class nsIFrame;
class nsIPresShell;
class nsPresContext;

class nsNativeTheme : public nsITimerCallback
{
 protected:

  NS_DECL_ISUPPORTS
  NS_DECL_NSITIMERCALLBACK

  enum ScrollbarButtonType {
    eScrollbarButton_UpTop   = 0,
    eScrollbarButton_Down    = 1 << 0,
    eScrollbarButton_Bottom  = 1 << 1
  };

  enum TreeSortDirection {
    eTreeSortDirection_Descending,
    eTreeSortDirection_Natural,
    eTreeSortDirection_Ascending
  };

  nsNativeTheme();
  virtual ~nsNativeTheme() {}

  // Returns the content state (hover, focus, etc), see nsEventStateManager.h
  nsEventStates GetContentState(nsIFrame* aFrame, PRUint8 aWidgetType);

  // Returns whether the widget is already styled by content
  // Normally called from ThemeSupportsWidget to turn off native theming
  // for elements that are already styled.
  bool IsWidgetStyled(nsPresContext* aPresContext, nsIFrame* aFrame,
                        PRUint8 aWidgetType);                                              

  // Accessors to widget-specific state information

  bool IsDisabled(nsIFrame* aFrame, nsEventStates aEventStates);

  // RTL chrome direction
  bool IsFrameRTL(nsIFrame* aFrame);

  // button:
  bool IsDefaultButton(nsIFrame* aFrame) {
    return CheckBooleanAttr(aFrame, nsGkAtoms::_default);
  }

  bool IsButtonTypeMenu(nsIFrame* aFrame);

  // checkbox:
  bool IsChecked(nsIFrame* aFrame) {
    return GetCheckedOrSelected(aFrame, false);
  }

  // radiobutton:
  bool IsSelected(nsIFrame* aFrame) {
    return GetCheckedOrSelected(aFrame, true);
  }
  
  bool IsFocused(nsIFrame* aFrame) {
    return CheckBooleanAttr(aFrame, nsGkAtoms::focused);
  }
  
  // scrollbar button:
  PRInt32 GetScrollbarButtonType(nsIFrame* aFrame);

  // tab:
  bool IsSelectedTab(nsIFrame* aFrame) {
    return CheckBooleanAttr(aFrame, nsGkAtoms::selected);
  }
  
  bool IsNextToSelectedTab(nsIFrame* aFrame, PRInt32 aOffset);
  
  bool IsBeforeSelectedTab(nsIFrame* aFrame) {
    return IsNextToSelectedTab(aFrame, -1);
  }
  
  bool IsAfterSelectedTab(nsIFrame* aFrame) {
    return IsNextToSelectedTab(aFrame, 1);
  }

  bool IsLeftToSelectedTab(nsIFrame* aFrame) {
    return IsFrameRTL(aFrame) ? IsAfterSelectedTab(aFrame) : IsBeforeSelectedTab(aFrame);
  }

  bool IsRightToSelectedTab(nsIFrame* aFrame) {
    return IsFrameRTL(aFrame) ? IsBeforeSelectedTab(aFrame) : IsAfterSelectedTab(aFrame);
  }

  // button / toolbarbutton:
  bool IsCheckedButton(nsIFrame* aFrame) {
    return CheckBooleanAttr(aFrame, nsGkAtoms::checked);
  }

  bool IsSelectedButton(nsIFrame* aFrame) {
    return CheckBooleanAttr(aFrame, nsGkAtoms::checked) ||
           CheckBooleanAttr(aFrame, nsGkAtoms::selected);
  }

  bool IsOpenButton(nsIFrame* aFrame) {
    return CheckBooleanAttr(aFrame, nsGkAtoms::open);
  }

  bool IsPressedButton(nsIFrame* aFrame);

  // treeheadercell:
  TreeSortDirection GetTreeSortDirection(nsIFrame* aFrame);
  bool IsLastTreeHeaderCell(nsIFrame* aFrame);

  // tab:
  bool IsBottomTab(nsIFrame* aFrame);
  bool IsFirstTab(nsIFrame* aFrame);
  
  bool IsHorizontal(nsIFrame* aFrame);

  // progressbar:
  bool IsIndeterminateProgress(nsIFrame* aFrame, nsEventStates aEventStates);
  bool IsVerticalProgress(nsIFrame* aFrame);

  // meter:
  bool IsVerticalMeter(nsIFrame* aFrame);

  // textfield:
  bool IsReadOnly(nsIFrame* aFrame) {
      return CheckBooleanAttr(aFrame, nsGkAtoms::readonly);
  }

  // menupopup:
  bool IsSubmenu(nsIFrame* aFrame, bool* aLeftOfParent);

  // True if it's not a menubar item or menulist item
  bool IsRegularMenuItem(nsIFrame *aFrame);

  bool IsMenuListEditable(nsIFrame *aFrame);

  nsIPresShell *GetPresShell(nsIFrame* aFrame);
  PRInt32 CheckIntAttr(nsIFrame* aFrame, nsIAtom* aAtom, PRInt32 defaultValue);
  bool CheckBooleanAttr(nsIFrame* aFrame, nsIAtom* aAtom);

  bool GetCheckedOrSelected(nsIFrame* aFrame, bool aCheckSelected);
  bool GetIndeterminate(nsIFrame* aFrame);

  bool QueueAnimatedContentForRefresh(nsIContent* aContent,
                                        PRUint32 aMinimumFrameRate);

  nsIFrame* GetAdjacentSiblingFrameWithSameAppearance(nsIFrame* aFrame,
                                                      bool aNextSibling);

 private:
  PRUint32 mAnimatedContentTimeout;
  nsCOMPtr<nsITimer> mAnimatedContentTimer;
  nsAutoTArray<nsCOMPtr<nsIContent>, 20> mAnimatedContentList;
};
