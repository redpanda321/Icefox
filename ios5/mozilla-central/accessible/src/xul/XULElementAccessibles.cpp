/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "XULElementAccessibles.h"

#include "Accessible-inl.h"
#include "BaseAccessibles.h"
#include "nsAccUtils.h"
#include "nsCoreUtils.h"
#include "nsTextEquivUtils.h"
#include "Relation.h"
#include "Role.h"
#include "States.h"

#include "nsIAccessibleRelation.h"
#include "nsIDOMXULDescriptionElement.h"
#include "nsINameSpaceManager.h"
#include "nsString.h"
#include "nsNetUtil.h"

using namespace mozilla::a11y;

////////////////////////////////////////////////////////////////////////////////
// XULLabelAccessible
////////////////////////////////////////////////////////////////////////////////

XULLabelAccessible::
  XULLabelAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  HyperTextAccessibleWrap(aContent, aDoc)
{
}

nsresult
XULLabelAccessible::GetNameInternal(nsAString& aName)
{
  // if the value attr doesn't exist, the screen reader must get the accessible text
  // from the accessible text interface or from the children
  mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::value, aName);
  return NS_OK;
}

role
XULLabelAccessible::NativeRole()
{
  return roles::LABEL;
}

PRUint64
XULLabelAccessible::NativeState()
{
  // Labels and description have read only state
  // They are not focusable or selectable
  return HyperTextAccessibleWrap::NativeState() | states::READONLY;
}

Relation
XULLabelAccessible::RelationByType(PRUint32 aType)
{
  Relation rel = HyperTextAccessibleWrap::RelationByType(aType);
  if (aType == nsIAccessibleRelation::RELATION_LABEL_FOR) {
    // Caption is the label for groupbox
    nsIContent *parent = mContent->GetParent();
    if (parent && parent->Tag() == nsGkAtoms::caption) {
      Accessible* parent = Parent();
      if (parent && parent->Role() == roles::GROUPING)
        rel.AppendTarget(parent);
    }
  }

  return rel;
}


////////////////////////////////////////////////////////////////////////////////
// XULTooltipAccessible
////////////////////////////////////////////////////////////////////////////////

XULTooltipAccessible::
  XULTooltipAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  LeafAccessible(aContent, aDoc)
{
}

PRUint64
XULTooltipAccessible::NativeState()
{
  return LeafAccessible::NativeState() | states::READONLY;
}

role
XULTooltipAccessible::NativeRole()
{
  return roles::TOOLTIP;
}


////////////////////////////////////////////////////////////////////////////////
// XULLinkAccessible
////////////////////////////////////////////////////////////////////////////////

XULLinkAccessible::
  XULLinkAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  HyperTextAccessibleWrap(aContent, aDoc)
{
}

// Expose nsIAccessibleHyperLink unconditionally
NS_IMPL_ISUPPORTS_INHERITED1(XULLinkAccessible, HyperTextAccessibleWrap,
                             nsIAccessibleHyperLink)

////////////////////////////////////////////////////////////////////////////////
// XULLinkAccessible. nsIAccessible

void
XULLinkAccessible::Value(nsString& aValue)
{
  aValue.Truncate();

  mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::href, aValue);
}

nsresult
XULLinkAccessible::GetNameInternal(nsAString& aName)
{
  mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::value, aName);
  if (!aName.IsEmpty())
    return NS_OK;

  return nsTextEquivUtils::GetNameFromSubtree(this, aName);
}

role
XULLinkAccessible::NativeRole()
{
  return roles::LINK;
}


PRUint64
XULLinkAccessible::NativeLinkState() const
{
  return states::LINKED;
}

PRUint8
XULLinkAccessible::ActionCount()
{
  return 1;
}

NS_IMETHODIMP
XULLinkAccessible::GetActionName(PRUint8 aIndex, nsAString& aName)
{
  aName.Truncate();

  if (aIndex != eAction_Jump)
    return NS_ERROR_INVALID_ARG;

  aName.AssignLiteral("jump");
  return NS_OK;
}

NS_IMETHODIMP
XULLinkAccessible::DoAction(PRUint8 aIndex)
{
  if (aIndex != eAction_Jump)
    return NS_ERROR_INVALID_ARG;

  if (IsDefunct())
    return NS_ERROR_FAILURE;

  DoCommand();
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// XULLinkAccessible: HyperLinkAccessible

bool
XULLinkAccessible::IsLink()
{
  // Expose HyperLinkAccessible unconditionally.
  return true;
}

PRUint32
XULLinkAccessible::StartOffset()
{
  // If XUL link accessible is not contained by hypertext accessible then
  // start offset matches index in parent because the parent doesn't contains
  // a text.
  // XXX: accessible parent of XUL link accessible should be a hypertext
  // accessible.
  if (Accessible::IsLink())
    return Accessible::StartOffset();
  return IndexInParent();
}

PRUint32
XULLinkAccessible::EndOffset()
{
  if (Accessible::IsLink())
    return Accessible::EndOffset();
  return IndexInParent() + 1;
}

already_AddRefed<nsIURI>
XULLinkAccessible::AnchorURIAt(PRUint32 aAnchorIndex)
{
  if (aAnchorIndex != 0)
    return nsnull;

  nsAutoString href;
  mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::href, href);

  nsCOMPtr<nsIURI> baseURI = mContent->GetBaseURI();
  nsIDocument* document = mContent->OwnerDoc();

  nsIURI* anchorURI = nsnull;
  NS_NewURI(&anchorURI, href,
            document->GetDocumentCharacterSet().get(),
            baseURI);

  return anchorURI;
}
