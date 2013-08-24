/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsXMLElement_h___
#define nsXMLElement_h___

#include "nsIDOMElement.h"
#include "nsGenericElement.h"

class nsXMLElement : public nsGenericElement,
                     public nsIDOMElement
{
public:
  nsXMLElement(already_AddRefed<nsINodeInfo> aNodeInfo)
    : nsGenericElement(aNodeInfo)
  {
  }

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE(nsGenericElement::)

  // nsIDOMElement
  NS_FORWARD_NSIDOMELEMENT(nsGenericElement::)

  // nsINode interface methods
  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  virtual nsXPCClassInfo* GetClassInfo();

  virtual nsIDOMNode* AsDOMNode() { return this; }

  // nsIContent interface methods
  virtual nsIAtom *GetIDAttributeName() const;
  virtual nsIAtom* DoGetID() const;
  virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              bool aCompileEventHandlers);
  virtual void UnbindFromTree(bool aDeep, bool aNullParent);
  virtual nsresult UnsetAttr(PRInt32 aNameSpaceID, nsIAtom* aAttribute,
                             bool aNotify);
  virtual bool ParseAttribute(PRInt32 aNamespaceID,
                                nsIAtom* aAttribute,
                                const nsAString& aValue,
                                nsAttrValue& aResult);

  // nsGenericElement overrides
  virtual void NodeInfoChanged(nsINodeInfo* aOldNodeInfo);


};

#endif // nsXMLElement_h___
