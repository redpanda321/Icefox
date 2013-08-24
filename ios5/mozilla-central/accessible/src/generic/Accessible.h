/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _Accessible_H_
#define _Accessible_H_

#include "mozilla/a11y/Role.h"
#include "mozilla/a11y/States.h"
#include "nsAccessNodeWrap.h"

#include "nsIAccessible.h"
#include "nsIAccessibleHyperLink.h"
#include "nsIAccessibleSelectable.h"
#include "nsIAccessibleValue.h"
#include "nsIAccessibleRole.h"
#include "nsIAccessibleStates.h"

#include "nsStringGlue.h"
#include "nsTArray.h"
#include "nsRefPtrHashtable.h"

class AccEvent;
class AccGroupInfo;
class EmbeddedObjCollector;
class KeyBinding;
class Accessible;
class HyperTextAccessible;
struct nsRoleMapEntry;

namespace mozilla {
namespace a11y {

class HTMLImageMapAccessible;
class HTMLLIAccessible;
class ImageAccessible;
class Relation;
class TableAccessible;
class TextLeafAccessible;
class XULTreeAccessible;

/**
 * Name type flags.
 */
enum ENameValueFlag {
  /**
   * Name either
   *  a) present (not empty): !name.IsEmpty()
   *  b) no name (was missed): name.IsVoid()
   *  c) was left empty by the author on demand: name.IsEmpty() && !name.IsVoid()
   */
 eNameOK,
 eNameFromTooltip // Tooltip was used as a name
};

/**
 * Group position (level, position in set and set size).
 */
struct GroupPos
{
  GroupPos() : level(0), posInSet(0), setSize(0) { }

  PRInt32 level;
  PRInt32 posInSet;
  PRInt32 setSize;
};

} // namespace a11y
} // namespace mozilla

struct nsRect;
class nsIContent;
class nsIFrame;
class nsIAtom;
class nsIView;

typedef nsRefPtrHashtable<nsPtrHashKey<const void>, Accessible>
  AccessibleHashtable;

// see Accessible::GetAttrValue
#define NS_OK_NO_ARIA_VALUE \
NS_ERROR_GENERATE_SUCCESS(NS_ERROR_MODULE_GENERAL, 0x21)

// see Accessible::GetNameInternal
#define NS_OK_EMPTY_NAME \
NS_ERROR_GENERATE_SUCCESS(NS_ERROR_MODULE_GENERAL, 0x23)

// see Accessible::GetNameInternal
#define NS_OK_NAME_FROM_TOOLTIP \
NS_ERROR_GENERATE_SUCCESS(NS_ERROR_MODULE_GENERAL, 0x25)


#define NS_ACCESSIBLE_IMPL_IID                          \
{  /* 133c8bf4-4913-4355-bd50-426bd1d6e1ad */           \
  0x133c8bf4,                                           \
  0x4913,                                               \
  0x4355,                                               \
  { 0xbd, 0x50, 0x42, 0x6b, 0xd1, 0xd6, 0xe1, 0xad }    \
}

class Accessible : public nsAccessNodeWrap,
                   public nsIAccessible,
                   public nsIAccessibleHyperLink,
                   public nsIAccessibleSelectable,
                   public nsIAccessibleValue
{
public:
  Accessible(nsIContent* aContent, DocAccessible* aDoc);
  virtual ~Accessible();

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(Accessible, nsAccessNode)

  NS_DECL_NSIACCESSIBLE
  NS_DECL_NSIACCESSIBLEHYPERLINK
  NS_DECL_NSIACCESSIBLESELECTABLE
  NS_DECL_NSIACCESSIBLEVALUE
  NS_DECLARE_STATIC_IID_ACCESSOR(NS_ACCESSIBLE_IMPL_IID)

  //////////////////////////////////////////////////////////////////////////////
  // nsAccessNode

  virtual void Shutdown();

  //////////////////////////////////////////////////////////////////////////////
  // Public methods

  /**
   * Initialize the accessible.
   */
  virtual bool Init();

  /**
   * Get the description of this accessible.
   */
  virtual void Description(nsString& aDescription);

  /**
   * Get the value of this accessible.
   */
  virtual void Value(nsString& aValue);

  /**
   * Get the name of this accessible.
   */
  virtual mozilla::a11y::ENameValueFlag Name(nsString& aName);

  /**
   * Return DOM node associated with this accessible.
   */
  inline already_AddRefed<nsIDOMNode> DOMNode() const
  {
    nsIDOMNode *DOMNode = nsnull;
    if (GetNode())
      CallQueryInterface(GetNode(), &DOMNode);
    return DOMNode;
  }

  /**
   * Returns the accessible name specified by ARIA.
   */
  nsresult GetARIAName(nsAString& aName);

  /**
   * Maps ARIA state attributes to state of accessible. Note the given state
   * argument should hold states for accessible before you pass it into this
   * method.
   *
   * @param  [in/out] where to fill the states into.
   */
  virtual void ApplyARIAState(PRUint64* aState) const;

  /**
   * Returns the accessible name provided by native markup. It doesn't take
   * into account ARIA markup used to specify the name.
   *
   * @param  aName             [out] the accessible name
   *
   * @return NS_OK_EMPTY_NAME  points empty name was specified by native markup
   *                           explicitly (see nsIAccessible::name attribute for
   *                           details)
   */
  virtual nsresult GetNameInternal(nsAString& aName);

  /**
   * Return enumerated accessible role (see constants in Role.h).
   */
  mozilla::a11y::role Role();

  /**
   * Return true if ARIA role is specified on the element.
   */
  bool HasARIARole() const
    { return mRoleMapEntry; }

  /**
   * Return accessible role specified by ARIA (see constants in
   * roles).
   */
  mozilla::a11y::role ARIARole();

  /**
   * Returns enumerated accessible role from native markup (see constants in
   * Role.h). Doesn't take into account ARIA roles.
   */
  virtual mozilla::a11y::role NativeRole();

  /**
   * Return all states of accessible (including ARIA states).
   */
  virtual PRUint64 State();

  /**
   * Return interactive states present on the accessible
   * (@see NativeInteractiveState).
   */
  PRUint64 InteractiveState() const
  {
    PRUint64 state = NativeInteractiveState();
    ApplyARIAState(&state);
    return state;
  }

  /**
   * Return link states present on the accessible.
   */
  PRUint64 LinkState() const
  {
    PRUint64 state = NativeLinkState();
    ApplyARIAState(&state);
    return state;
  }

  /**
   * Return the states of accessible, not taking into account ARIA states.
   * Use State() to get complete set of states.
   */
  virtual PRUint64 NativeState();

  /**
   * Return native interactice state (unavailable, focusable or selectable).
   */
  virtual PRUint64 NativeInteractiveState() const;

  /**
   * Return native link states present on the accessible.
   */
  virtual PRUint64 NativeLinkState() const;

  /**
   * Return bit set of invisible and offscreen states.
   */
  PRUint64 VisibilityState();

  /**
   * Return true if native unavailable state present.
   */
  virtual bool NativelyUnavailable() const;

  /**
   * Returns attributes for accessible without explicitly setted ARIA
   * attributes.
   */
  virtual nsresult GetAttributesInternal(nsIPersistentProperties *aAttributes);

  /**
   * Return group position (level, position in set and set size).
   */
  virtual mozilla::a11y::GroupPos GroupPosition();

  /**
   * Used by ChildAtPoint() method to get direct or deepest child at point.
   */
  enum EWhichChildAtPoint {
    eDirectChild,
    eDeepestChild
  };

  /**
   * Return direct or deepest child at the given point.
   *
   * @param  aX           [in] x coordinate relative screen
   * @param  aY           [in] y coordinate relative screen
   * @param  aWhichChild  [in] flag points if deepest or direct child
   *                        should be returned
   */
  virtual Accessible* ChildAtPoint(PRInt32 aX, PRInt32 aY,
                                   EWhichChildAtPoint aWhichChild);

  /**
   * Return the focused child if any.
   */
  virtual Accessible* FocusedChild();

  /**
   * Return calculated group level based on accessible hierarchy.
   */
  virtual PRInt32 GetLevelInternal();

  /**
   * Calculate position in group and group size ('posinset' and 'setsize') based
   * on accessible hierarchy.
   *
   * @param  aPosInSet  [out] accessible position in the group
   * @param  aSetSize   [out] the group size
   */
  virtual void GetPositionAndSizeInternal(PRInt32 *aPosInSet,
                                          PRInt32 *aSetSize);

  /**
   * Get the relation of the given type.
   */
  virtual mozilla::a11y::Relation RelationByType(PRUint32 aType);

  //////////////////////////////////////////////////////////////////////////////
  // Initializing methods

  /**
   * Set the ARIA role map entry for a new accessible.
   * For a newly created accessible, specify which role map entry should be used.
   *
   * @param aRoleMapEntry The ARIA nsRoleMapEntry* for the accessible, or
   *                      nsnull if none.
   */
  virtual void SetRoleMapEntry(nsRoleMapEntry* aRoleMapEntry);

  /**
   * Update the children cache.
   */
  inline bool UpdateChildren()
  {
    InvalidateChildren();
    return EnsureChildren();
  }

  /**
   * Cache children if necessary. Return true if the accessible is defunct.
   */
  bool EnsureChildren();

  /**
   * Set the child count to -1 (unknown) and null out cached child pointers.
   * Should be called when accessible tree is changed because document has
   * transformed. Note, if accessible cares about its parent relation chain
   * itself should override this method to do nothing.
   */
  virtual void InvalidateChildren();

  /**
   * Append/insert/remove a child. Return true if operation was successful.
   */
  virtual bool AppendChild(Accessible* aChild);
  virtual bool InsertChildAt(PRUint32 aIndex, Accessible* aChild);
  virtual bool RemoveChild(Accessible* aChild);

  //////////////////////////////////////////////////////////////////////////////
  // Accessible tree traverse methods

  /**
   * Return parent accessible.
   */
  Accessible* Parent() const { return mParent; }

  /**
   * Return child accessible at the given index.
   */
  virtual Accessible* GetChildAt(PRUint32 aIndex);

  /**
   * Return child accessible count.
   */
  virtual PRUint32 ChildCount() const;

  /**
   * Return index of the given child accessible.
   */
  virtual PRInt32 GetIndexOf(Accessible* aChild);

  /**
   * Return index in parent accessible.
   */
  virtual PRInt32 IndexInParent() const;

  /**
   * Return true if accessible has children;
   */
  bool HasChildren() { return !!GetChildAt(0); }

  /**
   * Return first/last/next/previous sibling of the accessible.
   */
  inline Accessible* NextSibling() const
    {  return GetSiblingAtOffset(1); }
  inline Accessible* PrevSibling() const
    { return GetSiblingAtOffset(-1); }
  inline Accessible* FirstChild()
    { return GetChildAt(0); }
  inline Accessible* LastChild()
  {
    PRUint32 childCount = ChildCount();
    return childCount != 0 ? GetChildAt(childCount - 1) : nsnull;
  }


  /**
   * Return embedded accessible children count.
   */
  PRUint32 EmbeddedChildCount();

  /**
   * Return embedded accessible child at the given index.
   */
  Accessible* GetEmbeddedChildAt(PRUint32 aIndex);

  /**
   * Return index of the given embedded accessible child.
   */
  PRInt32 GetIndexOfEmbeddedChild(Accessible* aChild);

  /**
   * Return number of content children/content child at index. The content
   * child is created from markup in contrast to it's never constructed by its
   * parent accessible (like treeitem accessibles for XUL trees).
   */
  PRUint32 ContentChildCount() const { return mChildren.Length(); }
  Accessible* ContentChildAt(PRUint32 aIndex) const
    { return mChildren.ElementAt(aIndex); }

  /**
   * Return true if children were initialized.
   */
  inline bool AreChildrenCached() const
    { return !IsChildrenFlag(eChildrenUninitialized); }

  /**
   * Return true if the accessible is attached to tree.
   */
  bool IsBoundToParent() const { return !!mParent; }

  //////////////////////////////////////////////////////////////////////////////
  // Miscellaneous methods

  /**
   * Handle accessible event, i.e. process it, notifies observers and fires
   * platform specific event.
   */
  virtual nsresult HandleAccEvent(AccEvent* aAccEvent);

  /**
   * Return true if this accessible allows accessible children from anonymous subtree.
   */
  virtual bool CanHaveAnonChildren();

  /**
   * Returns text of accessible if accessible has text role otherwise empty
   * string.
   *
   * @param aText         [in] returned text of the accessible
   * @param aStartOffset  [in, optional] start offset inside of the accessible,
   *                        if missed entire text is appended
   * @param aLength       [in, optional] required length of text, if missed
   *                        then text form start offset till the end is appended
   */
  virtual void AppendTextTo(nsAString& aText, PRUint32 aStartOffset = 0,
                            PRUint32 aLength = PR_UINT32_MAX);

  /**
   * Assert if child not in parent's cache if the cache was initialized at this
   * point.
   */
  void TestChildCache(Accessible* aCachedChild) const;

  /**
   * Return boundaries rect relative the bounding frame.
   */
  virtual void GetBoundsRect(nsRect& aRect, nsIFrame** aRelativeFrame);

  //////////////////////////////////////////////////////////////////////////////
  // Downcasting and types

  inline bool IsAbbreviation() const
  {
    return mContent->IsHTML() &&
      (mContent->Tag() == nsGkAtoms::abbr || mContent->Tag() == nsGkAtoms::acronym);
  }

  inline bool IsApplication() const { return mFlags & eApplicationAccessible; }

  bool IsAutoComplete() const { return mFlags & eAutoCompleteAccessible; }

  inline bool IsAutoCompletePopup() const { return mFlags & eAutoCompletePopupAccessible; }

  inline bool IsCombobox() const { return mFlags & eComboboxAccessible; }

  inline bool IsDoc() const { return mFlags & eDocAccessible; }
  DocAccessible* AsDoc();

  inline bool IsHyperText() const { return mFlags & eHyperTextAccessible; }
  HyperTextAccessible* AsHyperText();

  inline bool IsHTMLFileInput() const { return mFlags & eHTMLFileInputAccessible; }

  inline bool IsHTMLListItem() const { return mFlags & eHTMLListItemAccessible; }
  mozilla::a11y::HTMLLIAccessible* AsHTMLListItem();

  inline bool IsImage() const { return mFlags & eImageAccessible; }
  mozilla::a11y::ImageAccessible* AsImage();

  bool IsImageMapAccessible() const { return mFlags & eImageMapAccessible; }
  mozilla::a11y::HTMLImageMapAccessible* AsImageMap();

  inline bool IsXULTree() const { return mFlags & eXULTreeAccessible; }
  mozilla::a11y::XULTreeAccessible* AsXULTree();

  inline bool IsXULDeck() const { return mFlags & eXULDeckAccessible; }

  inline bool IsListControl() const { return mFlags & eListControlAccessible; }

  inline bool IsMenuButton() const { return mFlags & eMenuButtonAccessible; }

  inline bool IsMenuPopup() const { return mFlags & eMenuPopupAccessible; }

  inline bool IsRoot() const { return mFlags & eRootAccessible; }
  mozilla::a11y::RootAccessible* AsRoot();

  virtual mozilla::a11y::TableAccessible* AsTable() { return nsnull; }

  inline bool IsTextLeaf() const { return mFlags & eTextLeafAccessible; }
  mozilla::a11y::TextLeafAccessible* AsTextLeaf();

  //////////////////////////////////////////////////////////////////////////////
  // ActionAccessible

  /**
   * Return the number of actions that can be performed on this accessible.
   */
  virtual PRUint8 ActionCount();

  /**
   * Return access key, such as Alt+D.
   */
  virtual KeyBinding AccessKey() const;

  /**
   * Return global keyboard shortcut for default action, such as Ctrl+O for
   * Open file menuitem.
   */
  virtual KeyBinding KeyboardShortcut() const;

  //////////////////////////////////////////////////////////////////////////////
  // HyperLinkAccessible

  /**
   * Return true if the accessible is hyper link accessible.
   */
  virtual bool IsLink();

  /**
   * Return the start offset of the link within the parent accessible.
   */
  virtual PRUint32 StartOffset();

  /**
   * Return the end offset of the link within the parent accessible.
   */
  virtual PRUint32 EndOffset();

  /**
   * Return true if the link is valid (e. g. points to a valid URL).
   */
  inline bool IsLinkValid()
  {
    NS_PRECONDITION(IsLink(), "IsLinkValid is called on not hyper link!");

    // XXX In order to implement this we would need to follow every link
    // Perhaps we can get information about invalid links from the cache
    // In the mean time authors can use role="link" aria-invalid="true"
    // to force it for links they internally know to be invalid
    return (0 == (State() & mozilla::a11y::states::INVALID));
  }

  /**
   * Return true if the link currently has the focus.
   */
  bool IsLinkSelected();

  /**
   * Return the number of anchors within the link.
   */
  virtual PRUint32 AnchorCount();

  /**
   * Returns an anchor accessible at the given index.
   */
  virtual Accessible* AnchorAt(PRUint32 aAnchorIndex);

  /**
   * Returns an anchor URI at the given index.
   */
  virtual already_AddRefed<nsIURI> AnchorURIAt(PRUint32 aAnchorIndex);

  //////////////////////////////////////////////////////////////////////////////
  // SelectAccessible

  /**
   * Return true if the accessible is a select control containing selectable
   * items.
   */
  virtual bool IsSelect();

  /**
   * Return an array of selected items.
   */
  virtual already_AddRefed<nsIArray> SelectedItems();

  /**
   * Return the number of selected items.
   */
  virtual PRUint32 SelectedItemCount();

  /**
   * Return selected item at the given index.
   */
  virtual Accessible* GetSelectedItem(PRUint32 aIndex);

  /**
   * Determine if item at the given index is selected.
   */
  virtual bool IsItemSelected(PRUint32 aIndex);

  /**
   * Add item at the given index the selection. Return true if success.
   */
  virtual bool AddItemToSelection(PRUint32 aIndex);

  /**
   * Remove item at the given index from the selection. Return if success.
   */
  virtual bool RemoveItemFromSelection(PRUint32 aIndex);

  /**
   * Select all items. Return true if success.
   */
  virtual bool SelectAll();

  /**
   * Unselect all items. Return true if success.
   */
  virtual bool UnselectAll();

  //////////////////////////////////////////////////////////////////////////////
  // Widgets

  /**
   * Return true if accessible is a widget, i.e. control or accessible that
   * manages its items. Note, being a widget the accessible may be a part of
   * composite widget.
   */
  virtual bool IsWidget() const;

  /**
   * Return true if the widget is active, i.e. has a focus within it.
   */
  virtual bool IsActiveWidget() const;

  /**
   * Return true if the widget has items and items are operable by user and
   * can be activated.
   */
  virtual bool AreItemsOperable() const;

  /**
   * Return the current item of the widget, i.e. an item that has or will have
   * keyboard focus when widget gets active.
   */
  virtual Accessible* CurrentItem();

  /**
   * Set the current item of the widget.
   */
  virtual void SetCurrentItem(Accessible* aItem);

  /**
   * Return container widget this accessible belongs to.
   */
  virtual Accessible* ContainerWidget() const;

  /**
   * Return the localized string for the given key.
   */
  static void TranslateString(const nsString& aKey, nsAString& aStringOut);

  /**
   * Return true if the accessible is defunct.
   */
  bool IsDefunct() const { return mFlags & eIsDefunct; }

  /**
   * Return true if the accessible is no longer in the document.
   */
  bool IsInDocument() const { return !(mFlags & eIsNotInDocument); }

protected:

  //////////////////////////////////////////////////////////////////////////////
  // Initializing, cache and tree traverse methods

  /**
   * Cache accessible children.
   */
  virtual void CacheChildren();

  /**
   * Set accessible parent and index in parent.
   */
  virtual void BindToParent(Accessible* aParent, PRUint32 aIndexInParent);
  virtual void UnbindFromParent();

  /**
   * Return sibling accessible at the given offset.
   */
  virtual Accessible* GetSiblingAtOffset(PRInt32 aOffset,
                                         nsresult *aError = nsnull) const;

  /**
   * Flags used to describe the state and type of children.
   */
  enum ChildrenFlags {
    eChildrenUninitialized = 0, // children aren't initialized
    eMixedChildren = 1 << 0, // text leaf children are presented
    eEmbeddedChildren = 1 << 1 // all children are embedded objects
  };

  /**
   * Return true if the children flag is set.
   */
  inline bool IsChildrenFlag(ChildrenFlags aFlag) const
    { return static_cast<ChildrenFlags> (mFlags & kChildrenFlagsMask) == aFlag; }

  /**
   * Set children flag.
   */
  inline void SetChildrenFlag(ChildrenFlags aFlag)
    { mFlags = (mFlags & ~kChildrenFlagsMask) | aFlag; }

  /**
   * Flags used to describe the state of this accessible.
   * @note keep these flags in sync with ChildrenFlags
   */
  enum StateFlags {
    eIsDefunct = 1 << 2, // accessible is defunct
    eIsNotInDocument = 1 << 3 // accessible is not in document
  };

  /**
   * Flags describing the type of this accessible.
   * @note keep these flags in sync with ChildrenFlags and StateFlags
   */
  enum AccessibleTypes {
    eApplicationAccessible = 1 << 4,
    eAutoCompleteAccessible = 1 << 5,
    eAutoCompletePopupAccessible = 1 << 6,
    eComboboxAccessible = 1 << 7,
    eDocAccessible = 1 << 8,
    eHyperTextAccessible = 1 << 9,
    eHTMLFileInputAccessible = 1 << 10,
    eHTMLListItemAccessible = 1 << 11,
    eImageAccessible = 1 << 12,
    eImageMapAccessible = 1 << 13,
    eListControlAccessible = 1 << 14,
    eMenuButtonAccessible = 1 << 15,
    eMenuPopupAccessible = 1 << 16,
    eRootAccessible = 1 << 17,
    eTextLeafAccessible = 1 << 18,
    eXULDeckAccessible = 1 << 19,
    eXULTreeAccessible = 1 << 20
  };

  //////////////////////////////////////////////////////////////////////////////
  // Miscellaneous helpers

  /**
   * Return ARIA role (helper method).
   */
  mozilla::a11y::role ARIATransformRole(mozilla::a11y::role aRole);

  //////////////////////////////////////////////////////////////////////////////
  // Name helpers

  /**
   * Compute the name of HTML node.
   */
  nsresult GetHTMLName(nsAString& aName);

  /**
   * Compute the name for XUL node.
   */
  nsresult GetXULName(nsAString& aName);

  // helper method to verify frames
  static nsresult GetFullKeyName(const nsAString& aModifierName, const nsAString& aKeyName, nsAString& aStringOut);

  /**
   * Return an accessible for the given DOM node, or if that node isn't
   * accessible, return the accessible for the next DOM node which has one
   * (based on forward depth first search).
   *
   * @param  aStartNode  [in] the DOM node to start from
   * @return              the resulting accessible
   */
  Accessible* GetFirstAvailableAccessible(nsINode* aStartNode) const;

  //////////////////////////////////////////////////////////////////////////////
  // Action helpers

  /**
   * Prepares click action that will be invoked in timeout.
   *
   * @note  DoCommand() prepares an action in timeout because when action
   *  command opens a modal dialog/window, it won't return until the
   *  dialog/window is closed. If executing action command directly in
   *  nsIAccessible::DoAction() method, it will block AT tools (e.g. GOK) that
   *  invoke action of mozilla accessibles direclty (see bug 277888 for details).
   *
   * @param  aContent      [in, optional] element to click
   * @param  aActionIndex  [in, optional] index of accessible action
   */
  void DoCommand(nsIContent *aContent = nsnull, PRUint32 aActionIndex = 0);

  /**
   * Dispatch click event.
   */
  virtual void DispatchClickEvent(nsIContent *aContent, PRUint32 aActionIndex);

  NS_DECL_RUNNABLEMETHOD_ARG2(Accessible, DispatchClickEvent,
                              nsCOMPtr<nsIContent>, PRUint32)

  //////////////////////////////////////////////////////////////////////////////
  // Helpers

  /**
   *  Get the container node for an atomic region, defined by aria-atomic="true"
   *  @return the container node
   */
  nsIContent* GetAtomicRegion() const;

  /**
   * Get numeric value of the given ARIA attribute.
   *
   * @param aAriaProperty - the ARIA property we're using
   * @param aValue - value of the attribute
   *
   * @return - NS_OK_NO_ARIA_VALUE if there is no setted ARIA attribute
   */
  nsresult GetAttrValue(nsIAtom *aAriaProperty, double *aValue);

  /**
   * Return the action rule based on ARIA enum constants EActionRule
   * (see nsARIAMap.h). Used by ActionCount() and GetActionName().
   */
  PRUint32 GetActionRule();

  /**
   * Return group info.
   */
  AccGroupInfo* GetGroupInfo();

  /**
   * Fires platform accessible event. It's notification method only. It does
   * change nothing on Gecko side. Don't use it until you're sure what you do
   * (see example in XUL tree accessible), use nsEventShell::FireEvent()
   * instead. MUST be overridden in wrap classes.
   *
   * @param aEvent  the accessible event to fire.
   */
  virtual nsresult FirePlatformEvent(AccEvent* aEvent) = 0;

  // Data Members
  nsRefPtr<Accessible> mParent;
  nsTArray<nsRefPtr<Accessible> > mChildren;
  PRInt32 mIndexInParent;

  static const PRUint32 kChildrenFlagsMask =
    eChildrenUninitialized | eMixedChildren | eEmbeddedChildren;

  PRUint32 mFlags;
  friend class DocAccessible;

  nsAutoPtr<EmbeddedObjCollector> mEmbeddedObjCollector;
  PRInt32 mIndexOfEmbeddedChild;
  friend class EmbeddedObjCollector;

  nsAutoPtr<AccGroupInfo> mGroupInfo;
  friend class AccGroupInfo;

  /**
   * Non-null indicates author-supplied role; possibly state & value as well
   */
  nsRoleMapEntry* mRoleMapEntry;
};

NS_DEFINE_STATIC_IID_ACCESSOR(Accessible,
                              NS_ACCESSIBLE_IMPL_IID)


/**
 * Represent key binding associated with accessible (such as access key and
 * global keyboard shortcuts).
 */
class KeyBinding
{
public:
  /**
   * Modifier mask values.
   */
  static const PRUint32 kShift = 1;
  static const PRUint32 kControl = 2;
  static const PRUint32 kAlt = 4;
  static const PRUint32 kMeta = 8;

  KeyBinding() : mKey(0), mModifierMask(0) {}
  KeyBinding(PRUint32 aKey, PRUint32 aModifierMask) :
    mKey(aKey), mModifierMask(aModifierMask) {};

  inline bool IsEmpty() const { return !mKey; }
  inline PRUint32 Key() const { return mKey; }
  inline PRUint32 ModifierMask() const { return mModifierMask; }

  enum Format {
    ePlatformFormat,
    eAtkFormat
  };

  /**
   * Return formatted string for this key binding depending on the given format.
   */
  inline void ToString(nsAString& aValue,
                       Format aFormat = ePlatformFormat) const
  {
    aValue.Truncate();
    AppendToString(aValue, aFormat);
  }
  inline void AppendToString(nsAString& aValue,
                             Format aFormat = ePlatformFormat) const
  {
    if (mKey) {
      if (aFormat == ePlatformFormat)
        ToPlatformFormat(aValue);
      else
        ToAtkFormat(aValue);
    }
  }

private:
  void ToPlatformFormat(nsAString& aValue) const;
  void ToAtkFormat(nsAString& aValue) const;

  PRUint32 mKey;
  PRUint32 mModifierMask;
};

#endif
