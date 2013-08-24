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
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ms2ger <ms2ger@gmail.com>
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

/* DOM object for element.style */

#include "nsDOMCSSAttrDeclaration.h"

#include "mozilla/css/Declaration.h"
#include "mozilla/css/Loader.h"
#include "mozilla/dom/Element.h"
#include "nsICSSStyleRule.h"
#include "nsIDocument.h"
#include "nsIDOMMutationEvent.h"
#include "nsIPrincipal.h"
#include "nsIURI.h"
#include "nsNodeUtils.h"

namespace css = mozilla::css;
namespace dom = mozilla::dom;

nsDOMCSSAttributeDeclaration::nsDOMCSSAttributeDeclaration(dom::Element* aElement
#ifdef MOZ_SMIL
                                                           , PRBool aIsSMILOverride
#endif // MOZ_SMIL
                                                           )
  : mElement(aElement)
#ifdef MOZ_SMIL
  , mIsSMILOverride(aIsSMILOverride)
#endif // MOZ_SMIL
{
  MOZ_COUNT_CTOR(nsDOMCSSAttributeDeclaration);

  NS_ASSERTION(aElement, "Inline style for a NULL element?");
}

nsDOMCSSAttributeDeclaration::~nsDOMCSSAttributeDeclaration()
{
  MOZ_COUNT_DTOR(nsDOMCSSAttributeDeclaration);
}

NS_IMPL_CYCLE_COLLECTION_1(nsDOMCSSAttributeDeclaration, mElement)

NS_INTERFACE_MAP_BEGIN(nsDOMCSSAttributeDeclaration)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(nsDOMCSSAttributeDeclaration)
NS_IMPL_QUERY_TAIL_INHERITING(nsDOMCSSDeclaration)

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsDOMCSSAttributeDeclaration)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsDOMCSSAttributeDeclaration)

nsresult
nsDOMCSSAttributeDeclaration::SetCSSDeclaration(css::Declaration* aDecl)
{
  NS_ASSERTION(mElement, "Must have Element to set the declaration!");
  nsICSSStyleRule* oldRule =
#ifdef MOZ_SMIL
    mIsSMILOverride ? mElement->GetSMILOverrideStyleRule() :
#endif // MOZ_SMIL
    mElement->GetInlineStyleRule();
  NS_ASSERTION(oldRule, "Element must have rule");

  nsCOMPtr<nsICSSStyleRule> newRule =
    oldRule->DeclarationChanged(aDecl, PR_FALSE);
  if (!newRule) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return
#ifdef MOZ_SMIL
    mIsSMILOverride ? mElement->SetSMILOverrideStyleRule(newRule, PR_TRUE) :
#endif // MOZ_SMIL
    mElement->SetInlineStyleRule(newRule, PR_TRUE);
}

nsIDocument*
nsDOMCSSAttributeDeclaration::DocToUpdate()
{
  // XXXbz this is a bit of a hack, especially doing it before the
  // BeginUpdate(), but this is a good chokepoint where we know we
  // plan to modify the CSSDeclaration, so need to notify
  // AttributeWillChange if this is inline style.
#ifdef MOZ_SMIL
  if (!mIsSMILOverride)
#endif
  {
    nsNodeUtils::AttributeWillChange(mElement, kNameSpaceID_None,
                                     nsGkAtoms::style,
                                     nsIDOMMutationEvent::MODIFICATION);
  }
 
  // We need GetOwnerDoc() rather than GetCurrentDoc() because it might
  // be the BeginUpdate call that inserts mElement into the document.
  return mElement->GetOwnerDoc();
}

css::Declaration*
nsDOMCSSAttributeDeclaration::GetCSSDeclaration(PRBool aAllocate)
{
  if (!mElement)
    return nsnull;

  nsICSSStyleRule* cssRule;
#ifdef MOZ_SMIL
  if (mIsSMILOverride)
    cssRule = mElement->GetSMILOverrideStyleRule();
  else
#endif // MOZ_SMIL
    cssRule = mElement->GetInlineStyleRule();

  if (cssRule) {
    return cssRule->GetDeclaration();
  }
  if (!aAllocate) {
    return nsnull;
  }

  // cannot fail
  css::Declaration *decl = new css::Declaration();
  decl->InitializeEmpty();
  nsCOMPtr<nsICSSStyleRule> newRule = NS_NewCSSStyleRule(nsnull, decl);

  // this *can* fail (inside SetAttrAndNotify, at least).
  nsresult rv;
#ifdef MOZ_SMIL
  if (mIsSMILOverride)
    rv = mElement->SetSMILOverrideStyleRule(newRule, PR_FALSE);
  else
#endif // MOZ_SMIL
    rv = mElement->SetInlineStyleRule(newRule, PR_FALSE);

  if (NS_FAILED(rv)) {
    return nsnull; // the decl will be destroyed along with the style rule
  }

  return decl;
}

/*
 * This is a utility function.  It will only fail if it can't get a
 * parser.  This means it can return NS_OK without aURI or aCSSLoader
 * being initialized.
 */
nsresult
nsDOMCSSAttributeDeclaration::GetCSSParsingEnvironment(nsIURI** aSheetURI,
                                                       nsIURI** aBaseURI,
                                                       nsIPrincipal** aSheetPrincipal,
                                                       mozilla::css::Loader** aCSSLoader)
{
  NS_ASSERTION(mElement, "Something is severely broken -- there should be an Element here!");
  // null out the out params since some of them may not get initialized below
  *aSheetURI = nsnull;
  *aBaseURI = nsnull;
  *aSheetPrincipal = nsnull;
  *aCSSLoader = nsnull;

  nsIDocument* doc = mElement->GetOwnerDoc();
  if (!doc) {
    // document has been destroyed
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsIURI> baseURI = mElement->GetBaseURI();
  nsCOMPtr<nsIURI> sheetURI = doc->GetDocumentURI();

  NS_ADDREF(*aCSSLoader = doc->CSSLoader());

  baseURI.swap(*aBaseURI);
  sheetURI.swap(*aSheetURI);
  NS_ADDREF(*aSheetPrincipal = mElement->NodePrincipal());

  return NS_OK;
}

NS_IMETHODIMP
nsDOMCSSAttributeDeclaration::GetParentRule(nsIDOMCSSRule **aParent)
{
  NS_ENSURE_ARG_POINTER(aParent);

  *aParent = nsnull;
  return NS_OK;
}

/* virtual */ nsINode*
nsDOMCSSAttributeDeclaration::GetParentObject()
{
  return mElement;
}
