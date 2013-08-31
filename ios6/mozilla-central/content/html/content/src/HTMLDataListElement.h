/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef HTMLDataListElement_h___
#define HTMLDataListElement_h___

#include "nsGenericHTMLElement.h"
#include "nsIDOMHTMLDataListElement.h"
#include "nsContentList.h"

namespace mozilla {
namespace dom {

class HTMLDataListElement : public nsGenericHTMLElement,
                            public nsIDOMHTMLDataListElement
{
public:
  HTMLDataListElement(already_AddRefed<nsINodeInfo> aNodeInfo)
    : nsGenericHTMLElement(aNodeInfo)
  {
    SetIsDOMBinding();
  }
  virtual ~HTMLDataListElement();

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE_TO_NSINODE

  // nsIDOMElement
  NS_FORWARD_NSIDOMELEMENT_TO_GENERIC

  // nsIDOMHTMLElement
  NS_FORWARD_NSIDOMHTMLELEMENT_TO_GENERIC

  // nsIDOMHTMLDataListElement
  NS_DECL_NSIDOMHTMLDATALISTELEMENT

  nsContentList* Options()
  {
    if (!mOptions) {
      mOptions = new nsContentList(this, MatchOptions, nullptr, nullptr, true);
    }

    return mOptions;
  }


  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  // This function is used to generate the nsContentList (option elements).
  static bool MatchOptions(nsIContent* aContent, int32_t aNamespaceID,
                             nsIAtom* aAtom, void* aData);

  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(HTMLDataListElement,
                                           nsGenericHTMLElement)

  virtual nsXPCClassInfo* GetClassInfo();
  virtual nsIDOMNode* AsDOMNode() { return this; }
protected:
  virtual JSObject* WrapNode(JSContext *aCx, JSObject *aScope,
                             bool *aTriedToWrap) MOZ_OVERRIDE;

  // <option>'s list inside the datalist element.
  nsRefPtr<nsContentList> mOptions;
};

} // namespace dom
} // namespace mozilla

#endif /* HTMLDataListElement_h___ */
