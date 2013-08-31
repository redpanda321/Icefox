/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Declaration of HTML <label> elements.
 */
#ifndef HTMLLabelElement_h
#define HTMLLabelElement_h

#include "nsGenericHTMLElement.h"
#include "nsIDOMHTMLLabelElement.h"

namespace mozilla {
namespace dom {

class HTMLLabelElement : public nsGenericHTMLFormElement,
                         public nsIDOMHTMLLabelElement
{
public:
  HTMLLabelElement(already_AddRefed<nsINodeInfo> aNodeInfo)
    : nsGenericHTMLFormElement(aNodeInfo),
      mHandlingEvent(false)
  {
    SetIsDOMBinding();
  }
  virtual ~HTMLLabelElement();

  NS_IMPL_FROMCONTENT_HTML_WITH_TAG(HTMLLabelElement, label)

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE_TO_NSINODE

  // nsIDOMElement
  NS_FORWARD_NSIDOMELEMENT_TO_GENERIC

  // nsIDOMHTMLLabelElement
  NS_DECL_NSIDOMHTMLLABELELEMENT

  // nsIDOMHTMLElement
  NS_FORWARD_NSIDOMHTMLELEMENT_TO_GENERIC

  using nsGenericHTMLFormElement::GetForm;
  void GetHtmlFor(nsString& aHtmlFor)
  {
    GetHTMLAttr(nsGkAtoms::_for, aHtmlFor);
  }
  void SetHtmlFor(const nsAString& aHtmlFor, ErrorResult& aError)
  {
    SetHTMLAttr(nsGkAtoms::_for, aHtmlFor, aError);
  }
  nsGenericHTMLElement* GetControl() const
  {
    return GetLabeledElement();
  }

  virtual void Focus(mozilla::ErrorResult& aError) MOZ_OVERRIDE;

  // nsIFormControl
  NS_IMETHOD_(uint32_t) GetType() const { return NS_FORM_LABEL; }
  NS_IMETHOD Reset();
  NS_IMETHOD SubmitNamesValues(nsFormSubmission* aFormSubmission);

  virtual bool IsDisabled() const { return false; }

  // nsIContent
  virtual nsresult PostHandleEvent(nsEventChainPostVisitor& aVisitor);
  virtual void PerformAccesskey(bool aKeyCausesActivation,
                                bool aIsTrustedEvent);
  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  virtual nsXPCClassInfo* GetClassInfo();

  virtual nsIDOMNode* AsDOMNode() { return this; }

  nsGenericHTMLElement* GetLabeledElement() const;
protected:
  virtual JSObject* WrapNode(JSContext *aCx, JSObject *aScope,
                             bool *aTriedToWrap) MOZ_OVERRIDE;

  nsGenericHTMLElement* GetFirstLabelableDescendant() const;

  // XXX It would be nice if we could use an event flag instead.
  bool mHandlingEvent;
};

} // namespace dom
} // namespace mozilla

#endif /* HTMLLabelElement_h */
