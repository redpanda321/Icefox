/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLDataListElement.h"
#include "mozilla/dom/HTMLDataListElementBinding.h"

NS_IMPL_NS_NEW_HTML_ELEMENT(DataList)
DOMCI_NODE_DATA(HTMLDataListElement, mozilla::dom::HTMLDataListElement)

namespace mozilla {
namespace dom {

HTMLDataListElement::~HTMLDataListElement()
{
}

JSObject*
HTMLDataListElement::WrapNode(JSContext *aCx, JSObject *aScope,
                              bool *aTriedToWrap)
{
  return HTMLDataListElementBinding::Wrap(aCx, aScope, this, aTriedToWrap);
}

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(HTMLDataListElement,
                                                nsGenericHTMLElement)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOptions)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_CLASS(HTMLDataListElement)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(HTMLDataListElement,
                                                  nsGenericHTMLElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOptions)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_ADDREF_INHERITED(HTMLDataListElement, Element)
NS_IMPL_RELEASE_INHERITED(HTMLDataListElement, Element)

NS_INTERFACE_TABLE_HEAD_CYCLE_COLLECTION_INHERITED(HTMLDataListElement)
  NS_HTML_CONTENT_INTERFACE_TABLE1(HTMLDataListElement,
                                   nsIDOMHTMLDataListElement)
  NS_HTML_CONTENT_INTERFACE_TABLE_TO_MAP_SEGUE(HTMLDataListElement,
                                               nsGenericHTMLElement)
NS_HTML_CONTENT_INTERFACE_TABLE_TAIL_CLASSINFO(HTMLDataListElement)


NS_IMPL_ELEMENT_CLONE(HTMLDataListElement)

bool
HTMLDataListElement::MatchOptions(nsIContent* aContent, int32_t aNamespaceID,
                                  nsIAtom* aAtom, void* aData)
{
  return aContent->NodeInfo()->Equals(nsGkAtoms::option, kNameSpaceID_XHTML) &&
         !aContent->HasAttr(kNameSpaceID_None, nsGkAtoms::disabled);
}

NS_IMETHODIMP
HTMLDataListElement::GetOptions(nsIDOMHTMLCollection** aOptions)
{
  NS_ADDREF(*aOptions = Options());

  return NS_OK;
}

} // namespace dom
} // namespace mozilla
