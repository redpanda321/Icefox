/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsSVGElement.h"
#include "nsGkAtoms.h"
#include "nsIDOMSVGStyleElement.h"
#include "nsUnicharUtils.h"
#include "nsIDocument.h"
#include "nsStyleLinkElement.h"
#include "nsContentUtils.h"

typedef nsSVGElement nsSVGStyleElementBase;

class nsSVGStyleElement : public nsSVGStyleElementBase,
                          public nsIDOMSVGStyleElement,
                          public nsStyleLinkElement,
                          public nsStubMutationObserver
{
protected:
  friend nsresult NS_NewSVGStyleElement(nsIContent **aResult,
                                        already_AddRefed<nsINodeInfo> aNodeInfo);
  nsSVGStyleElement(already_AddRefed<nsINodeInfo> aNodeInfo);
  
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMSVGSTYLEELEMENT

  // xxx I wish we could use virtual inheritance
  NS_FORWARD_NSIDOMNODE(nsSVGStyleElementBase::)
  NS_FORWARD_NSIDOMELEMENT(nsSVGStyleElementBase::)
  NS_FORWARD_NSIDOMSVGELEMENT(nsSVGStyleElementBase::)

  // nsIContent
  virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              bool aCompileEventHandlers);
  virtual void UnbindFromTree(bool aDeep = true,
                              bool aNullParent = true);
  nsresult SetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                   const nsAString& aValue, bool aNotify)
  {
    return SetAttr(aNameSpaceID, aName, nsnull, aValue, aNotify);
  }
  virtual nsresult SetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                           nsIAtom* aPrefix, const nsAString& aValue,
                           bool aNotify);
  virtual nsresult UnsetAttr(PRInt32 aNameSpaceID, nsIAtom* aAttribute,
                             bool aNotify);

  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  // nsIMutationObserver
  NS_DECL_NSIMUTATIONOBSERVER_CHARACTERDATACHANGED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED

  virtual nsXPCClassInfo* GetClassInfo();

  virtual nsIDOMNode* AsDOMNode() { return this; }
protected:
  // Dummy init method to make the NS_IMPL_NS_NEW_SVG_ELEMENT and
  // NS_IMPL_ELEMENT_CLONE_WITH_INIT usable with this class. This should be
  // completely optimized away.
  inline nsresult Init()
  {
    return NS_OK;
  }

  // nsStyleLinkElement overrides
  already_AddRefed<nsIURI> GetStyleSheetURL(bool* aIsInline);

  void GetStyleSheetInfo(nsAString& aTitle,
                         nsAString& aType,
                         nsAString& aMedia,
                         bool* aIsAlternate);
  /**
   * Common method to call from the various mutation observer methods.
   * aContent is a content node that's either the one that changed or its
   * parent; we should only respond to the change if aContent is non-anonymous.
   */
  void ContentChanged(nsIContent* aContent);
};


NS_IMPL_NS_NEW_SVG_ELEMENT(Style)


//----------------------------------------------------------------------
// nsISupports methods

NS_IMPL_ADDREF_INHERITED(nsSVGStyleElement,nsSVGStyleElementBase)
NS_IMPL_RELEASE_INHERITED(nsSVGStyleElement,nsSVGStyleElementBase)

DOMCI_NODE_DATA(SVGStyleElement, nsSVGStyleElement)

NS_INTERFACE_TABLE_HEAD(nsSVGStyleElement)
  NS_NODE_INTERFACE_TABLE7(nsSVGStyleElement, nsIDOMNode, nsIDOMElement,
                           nsIDOMSVGElement, nsIDOMSVGStyleElement,
                           nsIDOMLinkStyle, nsIStyleSheetLinkingElement,
                           nsIMutationObserver)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(SVGStyleElement)
NS_INTERFACE_MAP_END_INHERITING(nsSVGStyleElementBase)

//----------------------------------------------------------------------
// Implementation

nsSVGStyleElement::nsSVGStyleElement(already_AddRefed<nsINodeInfo> aNodeInfo)
  : nsSVGStyleElementBase(aNodeInfo)
{
  AddMutationObserver(this);
}


//----------------------------------------------------------------------
// nsIDOMNode methods


NS_IMPL_ELEMENT_CLONE_WITH_INIT(nsSVGStyleElement)


//----------------------------------------------------------------------
// nsIContent methods

nsresult
nsSVGStyleElement::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              bool aCompileEventHandlers)
{
  nsresult rv = nsSVGStyleElementBase::BindToTree(aDocument, aParent,
                                                  aBindingParent,
                                                  aCompileEventHandlers);
  NS_ENSURE_SUCCESS(rv, rv);

  void (nsSVGStyleElement::*update)() = &nsSVGStyleElement::UpdateStyleSheetInternal;
  nsContentUtils::AddScriptRunner(NS_NewRunnableMethod(this, update));

  return rv;  
}

void
nsSVGStyleElement::UnbindFromTree(bool aDeep, bool aNullParent)
{
  nsCOMPtr<nsIDocument> oldDoc = GetCurrentDoc();

  nsSVGStyleElementBase::UnbindFromTree(aDeep, aNullParent);
  UpdateStyleSheetInternal(oldDoc);
}

nsresult
nsSVGStyleElement::SetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                           nsIAtom* aPrefix, const nsAString& aValue,
                           bool aNotify)
{
  nsresult rv = nsSVGStyleElementBase::SetAttr(aNameSpaceID, aName, aPrefix,
                                               aValue, aNotify);
  if (NS_SUCCEEDED(rv)) {
    UpdateStyleSheetInternal(nsnull,
                             aNameSpaceID == kNameSpaceID_None &&
                             (aName == nsGkAtoms::title ||
                              aName == nsGkAtoms::media ||
                              aName == nsGkAtoms::type));
  }

  return rv;
}

nsresult
nsSVGStyleElement::UnsetAttr(PRInt32 aNameSpaceID, nsIAtom* aAttribute,
                              bool aNotify)
{
  nsresult rv = nsSVGStyleElementBase::UnsetAttr(aNameSpaceID, aAttribute,
                                                 aNotify);
  if (NS_SUCCEEDED(rv)) {
    UpdateStyleSheetInternal(nsnull,
                             aNameSpaceID == kNameSpaceID_None &&
                             (aAttribute == nsGkAtoms::title ||
                              aAttribute == nsGkAtoms::media ||
                              aAttribute == nsGkAtoms::type));
  }

  return rv;
}

//----------------------------------------------------------------------
// nsIMutationObserver methods

void
nsSVGStyleElement::CharacterDataChanged(nsIDocument* aDocument,
                                        nsIContent* aContent,
                                        CharacterDataChangeInfo* aInfo)
{
  ContentChanged(aContent);
}

void
nsSVGStyleElement::ContentAppended(nsIDocument* aDocument,
                                   nsIContent* aContainer,
                                   nsIContent* aFirstNewContent,
                                   PRInt32 aNewIndexInContainer)
{
  ContentChanged(aContainer);
}
 
void
nsSVGStyleElement::ContentInserted(nsIDocument* aDocument,
                                   nsIContent* aContainer,
                                   nsIContent* aChild,
                                   PRInt32 aIndexInContainer)
{
  ContentChanged(aChild);
}
 
void
nsSVGStyleElement::ContentRemoved(nsIDocument* aDocument,
                                  nsIContent* aContainer,
                                  nsIContent* aChild,
                                  PRInt32 aIndexInContainer,
                                  nsIContent* aPreviousSibling)
{
  ContentChanged(aChild);
}

void
nsSVGStyleElement::ContentChanged(nsIContent* aContent)
{
  if (nsContentUtils::IsInSameAnonymousTree(this, aContent)) {
    UpdateStyleSheetInternal(nsnull);
  }
}

//----------------------------------------------------------------------
// nsIDOMSVGStyleElement methods

/* attribute DOMString xmlspace; */
NS_IMETHODIMP nsSVGStyleElement::GetXmlspace(nsAString & aXmlspace)
{
  GetAttr(kNameSpaceID_XML, nsGkAtoms::space, aXmlspace);

  return NS_OK;
}
NS_IMETHODIMP nsSVGStyleElement::SetXmlspace(const nsAString & aXmlspace)
{
  return SetAttr(kNameSpaceID_XML, nsGkAtoms::space, aXmlspace, true);
}

/* attribute DOMString type; */
NS_IMETHODIMP nsSVGStyleElement::GetType(nsAString & aType)
{
  GetAttr(kNameSpaceID_None, nsGkAtoms::type, aType);

  return NS_OK;
}
NS_IMETHODIMP nsSVGStyleElement::SetType(const nsAString & aType)
{
  return SetAttr(kNameSpaceID_None, nsGkAtoms::type, aType, true);
}

/* attribute DOMString media; */
NS_IMETHODIMP nsSVGStyleElement::GetMedia(nsAString & aMedia)
{
  GetAttr(kNameSpaceID_None, nsGkAtoms::media, aMedia);

  return NS_OK;
}
NS_IMETHODIMP nsSVGStyleElement::SetMedia(const nsAString & aMedia)
{
  return SetAttr(kNameSpaceID_None, nsGkAtoms::media, aMedia, true);
}

/* attribute DOMString title; */
NS_IMETHODIMP nsSVGStyleElement::GetTitle(nsAString & aTitle)
{
  GetAttr(kNameSpaceID_None, nsGkAtoms::title, aTitle);

  return NS_OK;
}
NS_IMETHODIMP nsSVGStyleElement::SetTitle(const nsAString & aTitle)
{
  return SetAttr(kNameSpaceID_None, nsGkAtoms::title, aTitle, true);
}

//----------------------------------------------------------------------
// nsStyleLinkElement methods

already_AddRefed<nsIURI>
nsSVGStyleElement::GetStyleSheetURL(bool* aIsInline)
{
  *aIsInline = true;
  return nsnull;
}

void
nsSVGStyleElement::GetStyleSheetInfo(nsAString& aTitle,
                                     nsAString& aType,
                                     nsAString& aMedia,
                                     bool* aIsAlternate)
{
  *aIsAlternate = false;

  nsAutoString title;
  GetAttr(kNameSpaceID_None, nsGkAtoms::title, title);
  title.CompressWhitespace();
  aTitle.Assign(title);

  GetAttr(kNameSpaceID_None, nsGkAtoms::media, aMedia);
  // The SVG spec is formulated in terms of the CSS2 spec,
  // which specifies that media queries are case insensitive.
  nsContentUtils::ASCIIToLower(aMedia);

  GetAttr(kNameSpaceID_None, nsGkAtoms::type, aType);
  if (aType.IsEmpty()) {
    aType.AssignLiteral("text/css");
  }

  return;
}
