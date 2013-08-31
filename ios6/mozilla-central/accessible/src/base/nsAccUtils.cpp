/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAccUtils.h"

#include "Accessible-inl.h"
#include "nsAccessibilityService.h"
#include "nsARIAMap.h"
#include "nsCoreUtils.h"
#include "DocAccessible.h"
#include "HyperTextAccessible.h"
#include "nsIAccessibleTypes.h"
#include "Role.h"
#include "States.h"
#include "TextLeafAccessible.h"
#include "nsIMutableArray.h"

#include "nsIDOMXULContainerElement.h"
#include "nsIDOMXULSelectCntrlEl.h"
#include "nsIDOMXULSelectCntrlItemEl.h"
#include "nsWhitespaceTokenizer.h"
#include "nsComponentManagerUtils.h"

using namespace mozilla;
using namespace mozilla::a11y;

void
nsAccUtils::GetAccAttr(nsIPersistentProperties *aAttributes,
                       nsIAtom *aAttrName, nsAString& aAttrValue)
{
  aAttrValue.Truncate();

  aAttributes->GetStringProperty(nsAtomCString(aAttrName), aAttrValue);
}

void
nsAccUtils::SetAccAttr(nsIPersistentProperties *aAttributes,
                       nsIAtom *aAttrName, const nsAString& aAttrValue)
{
  nsAutoString oldValue;
  nsAutoCString attrName;

  aAttributes->SetStringProperty(nsAtomCString(aAttrName), aAttrValue, oldValue);
}

void
nsAccUtils::SetAccGroupAttrs(nsIPersistentProperties *aAttributes,
                             int32_t aLevel, int32_t aSetSize,
                             int32_t aPosInSet)
{
  nsAutoString value;

  if (aLevel) {
    value.AppendInt(aLevel);
    SetAccAttr(aAttributes, nsGkAtoms::level, value);
  }

  if (aSetSize && aPosInSet) {
    value.Truncate();
    value.AppendInt(aPosInSet);
    SetAccAttr(aAttributes, nsGkAtoms::posinset, value);

    value.Truncate();
    value.AppendInt(aSetSize);
    SetAccAttr(aAttributes, nsGkAtoms::setsize, value);
  }
}

int32_t
nsAccUtils::GetDefaultLevel(Accessible* aAccessible)
{
  roles::Role role = aAccessible->Role();

  if (role == roles::OUTLINEITEM)
    return 1;

  if (role == roles::ROW) {
    Accessible* parent = aAccessible->Parent();
    // It is a row inside flatten treegrid. Group level is always 1 until it
    // is overriden by aria-level attribute.
    if (parent && parent->Role() == roles::TREE_TABLE) 
      return 1;
  }

  return 0;
}

int32_t
nsAccUtils::GetARIAOrDefaultLevel(Accessible* aAccessible)
{
  int32_t level = 0;
  nsCoreUtils::GetUIntAttr(aAccessible->GetContent(),
                           nsGkAtoms::aria_level, &level);

  if (level != 0)
    return level;

  return GetDefaultLevel(aAccessible);
}

int32_t
nsAccUtils::GetLevelForXULContainerItem(nsIContent *aContent)
{
  nsCOMPtr<nsIDOMXULContainerItemElement> item(do_QueryInterface(aContent));
  if (!item)
    return 0;

  nsCOMPtr<nsIDOMXULContainerElement> container;
  item->GetParentContainer(getter_AddRefs(container));
  if (!container)
    return 0;

  // Get level of the item.
  int32_t level = -1;
  while (container) {
    level++;

    nsCOMPtr<nsIDOMXULContainerElement> parentContainer;
    container->GetParentContainer(getter_AddRefs(parentContainer));
    parentContainer.swap(container);
  }

  return level;
}

void
nsAccUtils::SetLiveContainerAttributes(nsIPersistentProperties *aAttributes,
                                       nsIContent *aStartContent,
                                       nsIContent *aTopContent)
{
  nsAutoString atomic, live, relevant, busy;
  nsIContent *ancestor = aStartContent;
  while (ancestor) {

    // container-relevant attribute
    if (relevant.IsEmpty() &&
        nsAccUtils::HasDefinedARIAToken(ancestor, nsGkAtoms::aria_relevant) &&
        ancestor->GetAttr(kNameSpaceID_None, nsGkAtoms::aria_relevant, relevant))
      SetAccAttr(aAttributes, nsGkAtoms::containerRelevant, relevant);

    // container-live, and container-live-role attributes
    if (live.IsEmpty()) {
      nsRoleMapEntry* role = aria::GetRoleMap(ancestor);
      if (nsAccUtils::HasDefinedARIAToken(ancestor,
                                          nsGkAtoms::aria_live)) {
        ancestor->GetAttr(kNameSpaceID_None, nsGkAtoms::aria_live,
                          live);
      } else if (role) {
        GetLiveAttrValue(role->liveAttRule, live);
      }
      if (!live.IsEmpty()) {
        SetAccAttr(aAttributes, nsGkAtoms::containerLive, live);
        if (role) {
          nsAccUtils::SetAccAttr(aAttributes, nsGkAtoms::containerLiveRole,
                                 role->ARIARoleString());
        }
      }
    }

    // container-atomic attribute
    if (atomic.IsEmpty() &&
        nsAccUtils::HasDefinedARIAToken(ancestor, nsGkAtoms::aria_atomic) &&
        ancestor->GetAttr(kNameSpaceID_None, nsGkAtoms::aria_atomic, atomic))
      SetAccAttr(aAttributes, nsGkAtoms::containerAtomic, atomic);

    // container-busy attribute
    if (busy.IsEmpty() &&
        nsAccUtils::HasDefinedARIAToken(ancestor, nsGkAtoms::aria_busy) &&
        ancestor->GetAttr(kNameSpaceID_None, nsGkAtoms::aria_busy, busy))
      SetAccAttr(aAttributes, nsGkAtoms::containerBusy, busy);

    if (ancestor == aTopContent)
      break;

    ancestor = ancestor->GetParent();
    if (!ancestor)
      ancestor = aTopContent; // Use <body>/<frameset>
  }
}

bool
nsAccUtils::HasDefinedARIAToken(nsIContent *aContent, nsIAtom *aAtom)
{
  NS_ASSERTION(aContent, "aContent is null in call to HasDefinedARIAToken!");

  if (!aContent->HasAttr(kNameSpaceID_None, aAtom) ||
      aContent->AttrValueIs(kNameSpaceID_None, aAtom,
                            nsGkAtoms::_empty, eCaseMatters) ||
      aContent->AttrValueIs(kNameSpaceID_None, aAtom,
                            nsGkAtoms::_undefined, eCaseMatters)) {
        return false;
  }
  return true;
}

nsIAtom*
nsAccUtils::GetARIAToken(dom::Element* aElement, nsIAtom* aAttr)
{
  if (!nsAccUtils::HasDefinedARIAToken(aElement, aAttr))
    return nsGkAtoms::_empty;

  static nsIContent::AttrValuesArray tokens[] =
    { &nsGkAtoms::_false, &nsGkAtoms::_true,
      &nsGkAtoms::mixed, nullptr};

  int32_t idx = aElement->FindAttrValueIn(kNameSpaceID_None,
                                          aAttr, tokens, eCaseMatters);
  if (idx >= 0)
    return *(tokens[idx]);

  return nullptr;
}

Accessible*
nsAccUtils::GetAncestorWithRole(Accessible* aDescendant, uint32_t aRole)
{
  Accessible* document = aDescendant->Document();
  Accessible* parent = aDescendant;
  while ((parent = parent->Parent())) {
    uint32_t testRole = parent->Role();
    if (testRole == aRole)
      return parent;

    if (parent == document)
      break;
  }
  return nullptr;
}

Accessible*
nsAccUtils::GetSelectableContainer(Accessible* aAccessible, uint64_t aState)
{
  if (!aAccessible)
    return nullptr;

  if (!(aState & states::SELECTABLE))
    return nullptr;

  Accessible* parent = aAccessible;
  while ((parent = parent->Parent()) && !parent->IsSelect()) {
    if (Role(parent) == nsIAccessibleRole::ROLE_PANE)
      return nullptr;
  }
  return parent;
}

bool
nsAccUtils::IsARIASelected(Accessible* aAccessible)
{
  return aAccessible->GetContent()->
    AttrValueIs(kNameSpaceID_None, nsGkAtoms::aria_selected,
                nsGkAtoms::_true, eCaseMatters);
}

HyperTextAccessible*
nsAccUtils::GetTextAccessibleFromSelection(nsISelection* aSelection)
{
  // Get accessible from selection's focus DOM point (the DOM point where
  // selection is ended).

  nsCOMPtr<nsIDOMNode> focusDOMNode;
  aSelection->GetFocusNode(getter_AddRefs(focusDOMNode));
  if (!focusDOMNode)
    return nullptr;

  int32_t focusOffset = 0;
  aSelection->GetFocusOffset(&focusOffset);

  nsCOMPtr<nsINode> focusNode(do_QueryInterface(focusDOMNode));
  nsCOMPtr<nsINode> resultNode =
    nsCoreUtils::GetDOMNodeFromDOMPoint(focusNode, focusOffset);

  // Get text accessible containing the result node.
  DocAccessible* doc = 
    GetAccService()->GetDocAccessible(resultNode->OwnerDoc());
  Accessible* accessible = doc ? 
    doc->GetAccessibleOrContainer(resultNode) : nullptr;
  if (!accessible) {
    NS_NOTREACHED("No nsIAccessibleText for selection change event!");
    return nullptr;
  }

  do {
    HyperTextAccessible* textAcc = accessible->AsHyperText();
    if (textAcc)
      return textAcc;

    accessible = accessible->Parent();
  } while (accessible);

  NS_NOTREACHED("We must reach document accessible implementing nsIAccessibleText!");
  return nullptr;
}

nsIntPoint
nsAccUtils::ConvertToScreenCoords(int32_t aX, int32_t aY,
                                  uint32_t aCoordinateType,
                                  Accessible* aAccessible)
{
  nsIntPoint coords(aX, aY);

  switch (aCoordinateType) {
    case nsIAccessibleCoordinateType::COORDTYPE_SCREEN_RELATIVE:
      break;

    case nsIAccessibleCoordinateType::COORDTYPE_WINDOW_RELATIVE:
    {
      coords += nsCoreUtils::GetScreenCoordsForWindow(aAccessible->GetNode());
      break;
    }

    case nsIAccessibleCoordinateType::COORDTYPE_PARENT_RELATIVE:
    {
      coords += GetScreenCoordsForParent(aAccessible);
      break;
    }

    default:
      NS_NOTREACHED("invalid coord type!");
  }

  return coords;
}

void
nsAccUtils::ConvertScreenCoordsTo(int32_t *aX, int32_t *aY,
                                  uint32_t aCoordinateType,
                                  Accessible* aAccessible)
{
  switch (aCoordinateType) {
    case nsIAccessibleCoordinateType::COORDTYPE_SCREEN_RELATIVE:
      break;

    case nsIAccessibleCoordinateType::COORDTYPE_WINDOW_RELATIVE:
    {
      nsIntPoint coords = nsCoreUtils::GetScreenCoordsForWindow(aAccessible->GetNode());
      *aX -= coords.x;
      *aY -= coords.y;
      break;
    }

    case nsIAccessibleCoordinateType::COORDTYPE_PARENT_RELATIVE:
    {
      nsIntPoint coords = GetScreenCoordsForParent(aAccessible);
      *aX -= coords.x;
      *aY -= coords.y;
      break;
    }

    default:
    NS_NOTREACHED("invalid coord type!");
  }
}

nsIntPoint
nsAccUtils::GetScreenCoordsForParent(Accessible* aAccessible)
{
  Accessible* parent = aAccessible->Parent();
  if (!parent)
    return nsIntPoint(0, 0);

  nsIFrame *parentFrame = parent->GetFrame();
  if (!parentFrame)
    return nsIntPoint(0, 0);

  nsRect rect = parentFrame->GetScreenRectInAppUnits();
  return nsPoint(rect.x, rect.y).
    ToNearestPixels(parentFrame->PresContext()->AppUnitsPerDevPixel());
}

uint8_t
nsAccUtils::GetAttributeCharacteristics(nsIAtom* aAtom)
{
    for (uint32_t i = 0; i < nsARIAMap::gWAIUnivAttrMapLength; i++)
      if (*nsARIAMap::gWAIUnivAttrMap[i].attributeName == aAtom)
        return nsARIAMap::gWAIUnivAttrMap[i].characteristics;

    return 0;
}

bool
nsAccUtils::GetLiveAttrValue(uint32_t aRule, nsAString& aValue)
{
  switch (aRule) {
    case eOffLiveAttr:
      aValue = NS_LITERAL_STRING("off");
      return true;
    case ePoliteLiveAttr:
      aValue = NS_LITERAL_STRING("polite");
      return true;
  }

  return false;
}

#ifdef DEBUG

bool
nsAccUtils::IsTextInterfaceSupportCorrect(Accessible* aAccessible)
{
  // Don't test for accessible docs, it makes us create accessibles too
  // early and fire mutation events before we need to
  if (aAccessible->IsDoc())
    return true;

  bool foundText = false;
  uint32_t childCount = aAccessible->ChildCount();
  for (uint32_t childIdx = 0; childIdx < childCount; childIdx++) {
    Accessible* child = aAccessible->GetChildAt(childIdx);
    if (IsText(child)) {
      foundText = true;
      break;
    }
  }

  if (foundText) {
    // found text child node
    nsCOMPtr<nsIAccessibleText> text = do_QueryObject(aAccessible);
    if (!text)
      return false;
  }

  return true;
}
#endif

uint32_t
nsAccUtils::TextLength(Accessible* aAccessible)
{
  if (!IsText(aAccessible))
    return 1;

  TextLeafAccessible* textLeaf = aAccessible->AsTextLeaf();
  if (textLeaf)
    return textLeaf->Text().Length();

  // For list bullets (or anything other accessible which would compute its own
  // text. They don't have their own frame.
  // XXX In the future, list bullets may have frame and anon content, so 
  // we should be able to remove this at that point
  nsAutoString text;
  aAccessible->AppendTextTo(text); // Get all the text
  return text.Length();
}

bool
nsAccUtils::MustPrune(Accessible* aAccessible)
{ 
  roles::Role role = aAccessible->Role();

  // We don't prune buttons any more however AT don't expect children inside of
  // button in general, we allow menu buttons to have children to make them
  // accessible.
  return role == roles::MENUITEM || 
    role == roles::COMBOBOX_OPTION ||
    role == roles::OPTION ||
    role == roles::ENTRY ||
    role == roles::FLAT_EQUATION ||
    role == roles::PASSWORD_TEXT ||
    role == roles::TOGGLE_BUTTON ||
    role == roles::GRAPHIC ||
    role == roles::SLIDER ||
    role == roles::PROGRESSBAR ||
    role == roles::SEPARATOR;
}
