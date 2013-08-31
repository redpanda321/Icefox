/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=78: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHTMLOptionElement_h__
#define nsHTMLOptionElement_h__

#include "nsGenericHTMLElement.h"
#include "nsIDOMHTMLOptionElement.h"
#include "nsIJSNativeInitializer.h"

class nsHTMLSelectElement;

class nsHTMLOptionElement : public nsGenericHTMLElement,
                            public nsIDOMHTMLOptionElement,
                            public nsIJSNativeInitializer
{
public:
  nsHTMLOptionElement(already_AddRefed<nsINodeInfo> aNodeInfo);
  virtual ~nsHTMLOptionElement();

  NS_IMPL_FROMCONTENT_HTML_WITH_TAG(nsHTMLOptionElement, option)

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE_TO_NSINODE

  // nsIDOMElement
  NS_FORWARD_NSIDOMELEMENT_TO_GENERIC

  // nsIDOMHTMLElement
  NS_FORWARD_NSIDOMHTMLELEMENT_TO_GENERIC

  // nsIDOMHTMLOptionElement
  using mozilla::dom::Element::SetText;
  using mozilla::dom::Element::GetText;
  NS_DECL_NSIDOMHTMLOPTIONELEMENT

  bool Selected() const;
  bool DefaultSelected() const;

  // nsIJSNativeInitializer
  NS_IMETHOD Initialize(nsISupports* aOwner, JSContext* aContext,
                        JSObject *aObj, uint32_t argc, jsval *argv);

  virtual nsChangeHint GetAttributeChangeHint(const nsIAtom* aAttribute,
                                              int32_t aModType) const;

  virtual nsresult BeforeSetAttr(int32_t aNamespaceID, nsIAtom* aName,
                                 const nsAttrValueOrString* aValue,
                                 bool aNotify);

  void SetSelectedInternal(bool aValue, bool aNotify);

  virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              bool aCompileEventHandlers);
  virtual void UnbindFromTree(bool aDeep = true,
                              bool aNullParent = true);

  // nsIContent
  virtual nsEventStates IntrinsicState() const;

  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  nsresult CopyInnerTo(mozilla::dom::Element* aDest);

  virtual nsXPCClassInfo* GetClassInfo();

  virtual nsIDOMNode* AsDOMNode() { return this; }

  virtual bool IsDisabled() const {
    return HasAttr(kNameSpaceID_None, nsGkAtoms::disabled);
  }
protected:
  /**
   * Get the select content element that contains this option, this
   * intentionally does not return nsresult, all we care about is if
   * there's a select associated with this option or not.
   */
  nsHTMLSelectElement* GetSelect();

  bool mSelectedChanged;
  bool mIsSelected;

  // True only while we're under the SetOptionsSelectedByIndex call when our
  // "selected" attribute is changing and mSelectedChanged is false.
  bool mIsInSetDefaultSelected;
};

#endif
