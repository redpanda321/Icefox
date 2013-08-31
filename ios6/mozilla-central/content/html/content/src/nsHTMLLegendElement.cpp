/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "nsHTMLLegendElement.h"
#include "nsIDOMHTMLFormElement.h"
#include "nsIDOMEventTarget.h"
#include "nsGkAtoms.h"
#include "nsStyleConsts.h"
#include "nsIForm.h"
#include "nsIFormControl.h"
#include "nsIDocument.h"
#include "nsPIDOMWindow.h"
#include "nsFocusManager.h"
#include "nsIFrame.h"

using namespace mozilla;
using namespace mozilla::dom;

NS_IMPL_NS_NEW_HTML_ELEMENT(Legend)


nsHTMLLegendElement::nsHTMLLegendElement(already_AddRefed<nsINodeInfo> aNodeInfo)
  : nsGenericHTMLElement(aNodeInfo)
{
}

nsHTMLLegendElement::~nsHTMLLegendElement()
{
}


NS_IMPL_ADDREF_INHERITED(nsHTMLLegendElement, Element)
NS_IMPL_RELEASE_INHERITED(nsHTMLLegendElement, Element)


DOMCI_NODE_DATA(HTMLLegendElement, nsHTMLLegendElement)

// QueryInterface implementation for nsHTMLLegendElement
NS_INTERFACE_TABLE_HEAD(nsHTMLLegendElement)
  NS_HTML_CONTENT_INTERFACE_TABLE1(nsHTMLLegendElement, nsIDOMHTMLLegendElement)
  NS_HTML_CONTENT_INTERFACE_TABLE_TO_MAP_SEGUE(nsHTMLLegendElement,
                                               nsGenericHTMLElement)
NS_HTML_CONTENT_INTERFACE_TABLE_TAIL_CLASSINFO(HTMLLegendElement)


// nsIDOMHTMLLegendElement


NS_IMPL_ELEMENT_CLONE(nsHTMLLegendElement)


NS_IMETHODIMP
nsHTMLLegendElement::GetForm(nsIDOMHTMLFormElement** aForm)
{
  Element* form = GetFormElement();

  return form ? CallQueryInterface(form, aForm) : NS_OK;
}


NS_IMPL_STRING_ATTR(nsHTMLLegendElement, Align, align)

// this contains center, because IE4 does
static const nsAttrValue::EnumTable kAlignTable[] = {
  { "left", NS_STYLE_TEXT_ALIGN_LEFT },
  { "right", NS_STYLE_TEXT_ALIGN_RIGHT },
  { "center", NS_STYLE_TEXT_ALIGN_CENTER },
  { "bottom", NS_STYLE_VERTICAL_ALIGN_BOTTOM },
  { "top", NS_STYLE_VERTICAL_ALIGN_TOP },
  { 0 }
};

nsIContent*
nsHTMLLegendElement::GetFieldSet()
{
  nsIContent* parent = GetParent();

  if (parent && parent->IsHTML(nsGkAtoms::fieldset)) {
    return parent;
  }

  return nullptr;
}

bool
nsHTMLLegendElement::ParseAttribute(int32_t aNamespaceID,
                                    nsIAtom* aAttribute,
                                    const nsAString& aValue,
                                    nsAttrValue& aResult)
{
  if (aAttribute == nsGkAtoms::align && aNamespaceID == kNameSpaceID_None) {
    return aResult.ParseEnumValue(aValue, kAlignTable, false);
  }

  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aResult);
}

nsChangeHint
nsHTMLLegendElement::GetAttributeChangeHint(const nsIAtom* aAttribute,
                                            int32_t aModType) const
{
  nsChangeHint retval =
      nsGenericHTMLElement::GetAttributeChangeHint(aAttribute, aModType);
  if (aAttribute == nsGkAtoms::align) {
    NS_UpdateHint(retval, NS_STYLE_HINT_REFLOW);
  }
  return retval;
}

nsresult
nsHTMLLegendElement::SetAttr(int32_t aNameSpaceID, nsIAtom* aAttribute,
                             nsIAtom* aPrefix, const nsAString& aValue,
                             bool aNotify)
{
  return nsGenericHTMLElement::SetAttr(aNameSpaceID, aAttribute,
                                       aPrefix, aValue, aNotify);
}
nsresult
nsHTMLLegendElement::UnsetAttr(int32_t aNameSpaceID, nsIAtom* aAttribute,
                               bool aNotify)
{
  return nsGenericHTMLElement::UnsetAttr(aNameSpaceID, aAttribute, aNotify);
}

nsresult
nsHTMLLegendElement::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                                nsIContent* aBindingParent,
                                bool aCompileEventHandlers)
{
  return nsGenericHTMLElement::BindToTree(aDocument, aParent,
                                          aBindingParent,
                                          aCompileEventHandlers);
}

void
nsHTMLLegendElement::UnbindFromTree(bool aDeep, bool aNullParent)
{
  nsGenericHTMLElement::UnbindFromTree(aDeep, aNullParent);
}

void
nsHTMLLegendElement::Focus(ErrorResult& aError)
{
  nsIFrame* frame = GetPrimaryFrame();
  if (!frame) {
    return;
  }

  int32_t tabIndex;
  if (frame->IsFocusable(&tabIndex, false)) {
    nsGenericHTMLElement::Focus(aError);
    return;
  }

  // If the legend isn't focusable, focus whatever is focusable following
  // the legend instead, bug 81481.
  nsIFocusManager* fm = nsFocusManager::GetFocusManager();
  if (!fm) {
    return;
  }

  nsCOMPtr<nsIDOMElement> result;
  aError = fm->MoveFocus(nullptr, this, nsIFocusManager::MOVEFOCUS_FORWARD,
                         nsIFocusManager::FLAG_NOPARENTFRAME,
                         getter_AddRefs(result));
}

void
nsHTMLLegendElement::PerformAccesskey(bool aKeyCausesActivation,
                                      bool aIsTrustedEvent)
{
  // just use the same behaviour as the focus method
  mozilla::ErrorResult rv;
  Focus(rv);
}

