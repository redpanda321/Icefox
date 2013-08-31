/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "nsIDOMHTMLScriptElement.h"
#include "nsIDOMEventTarget.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsStyleConsts.h"
#include "nsIDocument.h"
#include "nsScriptElement.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "nsContentUtils.h"
#include "nsUnicharUtils.h"  // for nsCaseInsensitiveStringComparator()
#include "jsapi.h"
#include "nsIScriptContext.h"
#include "nsIScriptGlobalObject.h"
#include "nsIXPConnect.h"
#include "nsServiceManagerUtils.h"
#include "nsError.h"
#include "nsIArray.h"
#include "nsTArray.h"
#include "nsDOMJSUtils.h"

using namespace mozilla;
using namespace mozilla::dom;

class nsHTMLScriptElement : public nsGenericHTMLElement,
                            public nsIDOMHTMLScriptElement,
                            public nsScriptElement
{
public:
  using Element::GetText;
  using Element::SetText;

  nsHTMLScriptElement(already_AddRefed<nsINodeInfo> aNodeInfo,
                      FromParser aFromParser);
  virtual ~nsHTMLScriptElement();

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE_TO_NSINODE

  // nsIDOMElement
  NS_FORWARD_NSIDOMELEMENT_TO_GENERIC

  // nsIDOMHTMLElement
  NS_FORWARD_NSIDOMHTMLELEMENT_TO_GENERIC

  virtual void GetInnerHTML(nsAString& aInnerHTML,
                            mozilla::ErrorResult& aError) MOZ_OVERRIDE;
  virtual void SetInnerHTML(const nsAString& aInnerHTML,
                            mozilla::ErrorResult& aError) MOZ_OVERRIDE;

  // nsIDOMHTMLScriptElement
  NS_DECL_NSIDOMHTMLSCRIPTELEMENT

  // nsIScriptElement
  virtual void GetScriptType(nsAString& type);
  virtual void GetScriptText(nsAString& text);
  virtual void GetScriptCharset(nsAString& charset);
  virtual void FreezeUriAsyncDefer();
  virtual CORSMode GetCORSMode() const;

  // nsIContent
  virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              bool aCompileEventHandlers);
  virtual bool ParseAttribute(int32_t aNamespaceID,
                              nsIAtom* aAttribute,
                              const nsAString& aValue,
                              nsAttrValue& aResult);

  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  // Element
  virtual nsresult AfterSetAttr(int32_t aNamespaceID, nsIAtom* aName,
                                const nsAttrValue* aValue, bool aNotify);

  virtual nsXPCClassInfo* GetClassInfo();

  virtual nsIDOMNode* AsDOMNode() { return this; }
protected:
  // nsScriptElement
  virtual bool HasScriptContent();
};


NS_IMPL_NS_NEW_HTML_ELEMENT_CHECK_PARSER(Script)


nsHTMLScriptElement::nsHTMLScriptElement(already_AddRefed<nsINodeInfo> aNodeInfo,
                                         FromParser aFromParser)
  : nsGenericHTMLElement(aNodeInfo)
  , nsScriptElement(aFromParser)
{
  AddMutationObserver(this);
}

nsHTMLScriptElement::~nsHTMLScriptElement()
{
}


NS_IMPL_ADDREF_INHERITED(nsHTMLScriptElement, Element)
NS_IMPL_RELEASE_INHERITED(nsHTMLScriptElement, Element)

DOMCI_NODE_DATA(HTMLScriptElement, nsHTMLScriptElement)

// QueryInterface implementation for nsHTMLScriptElement
NS_INTERFACE_TABLE_HEAD(nsHTMLScriptElement)
  NS_HTML_CONTENT_INTERFACE_TABLE4(nsHTMLScriptElement,
                                   nsIDOMHTMLScriptElement,
                                   nsIScriptLoaderObserver,
                                   nsIScriptElement,
                                   nsIMutationObserver)
  NS_HTML_CONTENT_INTERFACE_TABLE_TO_MAP_SEGUE(nsHTMLScriptElement,
                                               nsGenericHTMLElement)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(HTMLScriptElement)
NS_HTML_CONTENT_INTERFACE_MAP_END


nsresult
nsHTMLScriptElement::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                                nsIContent* aBindingParent,
                                bool aCompileEventHandlers)
{
  nsresult rv = nsGenericHTMLElement::BindToTree(aDocument, aParent,
                                                 aBindingParent,
                                                 aCompileEventHandlers);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aDocument) {
    MaybeProcessScript();
  }

  return NS_OK;
}

bool
nsHTMLScriptElement::ParseAttribute(int32_t aNamespaceID,
                                    nsIAtom* aAttribute,
                                    const nsAString& aValue,
                                    nsAttrValue& aResult)
{
  if (aNamespaceID == kNameSpaceID_None &&
      aAttribute == nsGkAtoms::crossorigin) {
    ParseCORSValue(aValue, aResult);
    return true;
  }

  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aResult);
}

nsresult
nsHTMLScriptElement::Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const
{
  *aResult = nullptr;

  nsCOMPtr<nsINodeInfo> ni = aNodeInfo;
  nsHTMLScriptElement* it =
    new nsHTMLScriptElement(ni.forget(), NOT_FROM_PARSER);

  nsCOMPtr<nsINode> kungFuDeathGrip = it;
  nsresult rv = const_cast<nsHTMLScriptElement*>(this)->CopyInnerTo(it);
  NS_ENSURE_SUCCESS(rv, rv);

  // The clone should be marked evaluated if we are.
  it->mAlreadyStarted = mAlreadyStarted;
  it->mLineNumber = mLineNumber;
  it->mMalformed = mMalformed;

  kungFuDeathGrip.swap(*aResult);

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLScriptElement::GetText(nsAString& aValue)
{
  nsContentUtils::GetNodeTextContent(this, false, aValue);
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLScriptElement::SetText(const nsAString& aValue)
{
  return nsContentUtils::SetNodeTextContent(this, aValue, true);
}


NS_IMPL_STRING_ATTR(nsHTMLScriptElement, Charset, charset)
NS_IMPL_BOOL_ATTR(nsHTMLScriptElement, Defer, defer)
NS_IMPL_URI_ATTR(nsHTMLScriptElement, Src, src)
NS_IMPL_STRING_ATTR(nsHTMLScriptElement, Type, type)
NS_IMPL_STRING_ATTR(nsHTMLScriptElement, HtmlFor, _for)
NS_IMPL_STRING_ATTR(nsHTMLScriptElement, Event, event)
NS_IMPL_STRING_ATTR(nsHTMLScriptElement, CrossOrigin, crossorigin)

nsresult
nsHTMLScriptElement::GetAsync(bool* aValue)
{
  *aValue = mForceAsync || GetBoolAttr(nsGkAtoms::async);
  return NS_OK;
}

nsresult
nsHTMLScriptElement::SetAsync(bool aValue)
{
  mForceAsync = false;
  return SetBoolAttr(nsGkAtoms::async, aValue);
}

nsresult
nsHTMLScriptElement::AfterSetAttr(int32_t aNamespaceID, nsIAtom* aName,
                                  const nsAttrValue* aValue, bool aNotify)
{
  if (nsGkAtoms::async == aName && kNameSpaceID_None == aNamespaceID) {
    mForceAsync = false;
  }
  return nsGenericHTMLElement::AfterSetAttr(aNamespaceID, aName, aValue,
                                            aNotify);
}

void
nsHTMLScriptElement::GetInnerHTML(nsAString& aInnerHTML, ErrorResult& aError)
{
  nsContentUtils::GetNodeTextContent(this, false, aInnerHTML);
}

void
nsHTMLScriptElement::SetInnerHTML(const nsAString& aInnerHTML,
                                  ErrorResult& aError)
{
  aError = nsContentUtils::SetNodeTextContent(this, aInnerHTML, true);
}

// variation of this code in nsSVGScriptElement - check if changes
// need to be transfered when modifying

void
nsHTMLScriptElement::GetScriptType(nsAString& type)
{
  GetType(type);
}

void
nsHTMLScriptElement::GetScriptText(nsAString& text)
{
  GetText(text);
}

void
nsHTMLScriptElement::GetScriptCharset(nsAString& charset)
{
  GetCharset(charset);
}

void
nsHTMLScriptElement::FreezeUriAsyncDefer()
{
  if (mFrozen) {
    return;
  }
  
  // variation of this code in nsSVGScriptElement - check if changes
  // need to be transfered when modifying
  if (HasAttr(kNameSpaceID_None, nsGkAtoms::src)) {
    nsAutoString src;
    GetSrc(src);
    NS_NewURI(getter_AddRefs(mUri), src);
    // At this point mUri will be null for invalid URLs.
    mExternal = true;

    bool defer, async;
    GetAsync(&async);
    GetDefer(&defer);

    mDefer = !async && defer;
    mAsync = async;
  }
  
  mFrozen = true;
}

CORSMode
nsHTMLScriptElement::GetCORSMode() const
{
  return AttrValueToCORSMode(GetParsedAttr(nsGkAtoms::crossorigin));
}

bool
nsHTMLScriptElement::HasScriptContent()
{
  return (mFrozen ? mExternal : HasAttr(kNameSpaceID_None, nsGkAtoms::src)) ||
         nsContentUtils::HasNonEmptyTextContent(this);
}
