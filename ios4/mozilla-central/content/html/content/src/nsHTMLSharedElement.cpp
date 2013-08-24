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
#include "nsIDOMHTMLIsIndexElement.h"
#include "nsIDOMHTMLParamElement.h"
#include "nsIDOMHTMLBaseElement.h"
#include "nsIDOMHTMLDirectoryElement.h"
#include "nsIDOMHTMLMenuElement.h"
#include "nsIDOMHTMLQuoteElement.h"
#include "nsIDOMHTMLHeadElement.h"
#include "nsIDOMHTMLHtmlElement.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsStyleConsts.h"
#include "nsRuleData.h"
#include "nsMappedAttributes.h"
#include "nsNetUtil.h"
#include "nsHTMLFormElement.h"

// XXX nav4 has type= start= (same as OL/UL)
extern nsAttrValue::EnumTable kListTypeTable[];

class nsHTMLSharedElement : public nsGenericHTMLElement,
                            public nsIDOMHTMLIsIndexElement,
                            public nsIDOMHTMLParamElement,
                            public nsIDOMHTMLBaseElement,
                            public nsIDOMHTMLDirectoryElement,
                            public nsIDOMHTMLMenuElement,
                            public nsIDOMHTMLQuoteElement,
                            public nsIDOMHTMLHeadElement,
                            public nsIDOMHTMLHtmlElement
{
public:
  nsHTMLSharedElement(already_AddRefed<nsINodeInfo> aNodeInfo);
  virtual ~nsHTMLSharedElement();

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE(nsGenericHTMLElement::)

  // nsIDOMElement
  NS_FORWARD_NSIDOMELEMENT(nsGenericHTMLElement::)

  // nsIDOMHTMLElement
  NS_FORWARD_NSIDOMHTMLELEMENT(nsGenericHTMLElement::)

  // nsIDOMHTMLIsIndexElement
  NS_DECL_NSIDOMHTMLISINDEXELEMENT

  // nsIDOMHTMLParamElement
  NS_DECL_NSIDOMHTMLPARAMELEMENT

  // nsIDOMHTMLBaseElement
  NS_DECL_NSIDOMHTMLBASEELEMENT

  // nsIDOMHTMLDirectoryElement
  NS_DECL_NSIDOMHTMLDIRECTORYELEMENT

  // nsIDOMHTMLMenuElement
  // Same as directoryelement

  // nsIDOMHTMLQuoteElement
  NS_DECL_NSIDOMHTMLQUOTEELEMENT

  // nsIDOMHTMLHeadElement
  NS_DECL_NSIDOMHTMLHEADELEMENT

  // nsIDOMHTMLHtmlElement
  NS_DECL_NSIDOMHTMLHTMLELEMENT

  // nsIContent
  virtual PRBool ParseAttribute(PRInt32 aNamespaceID,
                                nsIAtom* aAttribute,
                                const nsAString& aValue,
                                nsAttrValue& aResult);
  nsresult SetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                   const nsAString& aValue, PRBool aNotify)
  {
    return SetAttr(aNameSpaceID, aName, nsnull, aValue, aNotify);
  }
  virtual nsresult SetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                           nsIAtom* aPrefix, const nsAString& aValue,
                           PRBool aNotify);

  virtual nsresult UnsetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                             PRBool aNotify);

  virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              PRBool aCompileEventHandlers);

  virtual void UnbindFromTree(PRBool aDeep = PR_TRUE,
                              PRBool aNullParent = PR_TRUE);

  virtual nsMapRuleToAttributesFunc GetAttributeMappingFunction() const;
  NS_IMETHOD_(PRBool) IsAttributeMapped(const nsIAtom* aAttribute) const;

  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;

  virtual nsXPCClassInfo* GetClassInfo()
  {
    return static_cast<nsXPCClassInfo*>(GetClassInfoInternal());
  }
  nsIClassInfo* GetClassInfoInternal();
};

NS_IMPL_NS_NEW_HTML_ELEMENT(Shared)


nsHTMLSharedElement::nsHTMLSharedElement(already_AddRefed<nsINodeInfo> aNodeInfo)
  : nsGenericHTMLElement(aNodeInfo)
{
}

nsHTMLSharedElement::~nsHTMLSharedElement()
{
}


NS_IMPL_ADDREF_INHERITED(nsHTMLSharedElement, nsGenericElement)
NS_IMPL_RELEASE_INHERITED(nsHTMLSharedElement, nsGenericElement)


DOMCI_DATA(HTMLParamElement, nsHTMLSharedElement)
DOMCI_DATA(HTMLIsIndexElement, nsHTMLSharedElement)
DOMCI_DATA(HTMLBaseElement, nsHTMLSharedElement)
DOMCI_DATA(HTMLDirectoryElement, nsHTMLSharedElement)
DOMCI_DATA(HTMLMenuElement, nsHTMLSharedElement)
DOMCI_DATA(HTMLQuoteElement, nsHTMLSharedElement)
DOMCI_DATA(HTMLHeadElement, nsHTMLSharedElement)
DOMCI_DATA(HTMLHtmlElement, nsHTMLSharedElement)

nsIClassInfo*
nsHTMLSharedElement::GetClassInfoInternal()
{
  if (mNodeInfo->Equals(nsGkAtoms::param)) {
    return NS_GetDOMClassInfoInstance(eDOMClassInfo_HTMLParamElement_id);
  }
  if (mNodeInfo->Equals(nsGkAtoms::isindex)) {
    return NS_GetDOMClassInfoInstance(eDOMClassInfo_HTMLIsIndexElement_id);
  }
  if (mNodeInfo->Equals(nsGkAtoms::base)) {
    return NS_GetDOMClassInfoInstance(eDOMClassInfo_HTMLBaseElement_id);
  }
  if (mNodeInfo->Equals(nsGkAtoms::dir)) {
    return NS_GetDOMClassInfoInstance(eDOMClassInfo_HTMLDirectoryElement_id);
  }
  if (mNodeInfo->Equals(nsGkAtoms::menu)) {
    return NS_GetDOMClassInfoInstance(eDOMClassInfo_HTMLMenuElement_id);
  }
  if (mNodeInfo->Equals(nsGkAtoms::q)) {
    return NS_GetDOMClassInfoInstance(eDOMClassInfo_HTMLQuoteElement_id);
  }
  if (mNodeInfo->Equals(nsGkAtoms::blockquote)) {
    return NS_GetDOMClassInfoInstance(eDOMClassInfo_HTMLQuoteElement_id);
  }
  if (mNodeInfo->Equals(nsGkAtoms::head)) {
    return NS_GetDOMClassInfoInstance(eDOMClassInfo_HTMLHeadElement_id);
  }
  if (mNodeInfo->Equals(nsGkAtoms::html)) {
    return NS_GetDOMClassInfoInstance(eDOMClassInfo_HTMLHtmlElement_id);
  }
  return nsnull;
}

// QueryInterface implementation for nsHTMLSharedElement
NS_INTERFACE_TABLE_HEAD(nsHTMLSharedElement)
  NS_HTML_CONTENT_INTERFACE_TABLE_AMBIGUOUS_BEGIN(nsHTMLSharedElement,
                                                  nsIDOMHTMLParamElement)
  NS_OFFSET_AND_INTERFACE_TABLE_END
  NS_HTML_CONTENT_INTERFACE_TABLE_TO_MAP_SEGUE_AMBIGUOUS(nsHTMLSharedElement,
                                                         nsGenericHTMLElement,
                                                         nsIDOMHTMLParamElement)
  NS_INTERFACE_MAP_ENTRY_IF_TAG(nsIDOMHTMLParamElement, param)
  NS_INTERFACE_MAP_ENTRY_IF_TAG(nsIDOMHTMLIsIndexElement, isindex)
  NS_INTERFACE_MAP_ENTRY_IF_TAG(nsIDOMHTMLBaseElement, base)
  NS_INTERFACE_MAP_ENTRY_IF_TAG(nsIDOMHTMLDirectoryElement, dir)
  NS_INTERFACE_MAP_ENTRY_IF_TAG(nsIDOMHTMLMenuElement, menu)
  NS_INTERFACE_MAP_ENTRY_IF_TAG(nsIDOMHTMLQuoteElement, q)
  NS_INTERFACE_MAP_ENTRY_IF_TAG(nsIDOMHTMLQuoteElement, blockquote)
  NS_INTERFACE_MAP_ENTRY_IF_TAG(nsIDOMHTMLHeadElement, head)
  NS_INTERFACE_MAP_ENTRY_IF_TAG(nsIDOMHTMLHtmlElement, html)

  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO_GETTER(GetClassInfoInternal)
NS_HTML_CONTENT_INTERFACE_MAP_END


NS_IMPL_ELEMENT_CLONE(nsHTMLSharedElement)

// nsIDOMHTMLParamElement
NS_IMPL_STRING_ATTR(nsHTMLSharedElement, Name, name)
NS_IMPL_STRING_ATTR(nsHTMLSharedElement, Type, type)
NS_IMPL_STRING_ATTR(nsHTMLSharedElement, Value, value)
NS_IMPL_STRING_ATTR(nsHTMLSharedElement, ValueType, valuetype)

// nsIDOMHTMLIsIndexElement
NS_IMPL_STRING_ATTR(nsHTMLSharedElement, Prompt, prompt)
NS_IMETHODIMP
nsHTMLSharedElement::GetForm(nsIDOMHTMLFormElement** aForm)
{
  NS_IF_ADDREF(*aForm = FindAncestorForm());

  return NS_OK;
}

// nsIDOMHTMLDirectoryElement
NS_IMPL_BOOL_ATTR(nsHTMLSharedElement, Compact, compact)

// nsIDOMHTMLMenuElement
//NS_IMPL_BOOL_ATTR(nsHTMLSharedElement, Compact, compact)

// nsIDOMHTMLQuoteElement
NS_IMPL_URI_ATTR(nsHTMLSharedElement, Cite, cite)

// nsIDOMHTMLHeadElement
// Deprecated and not exposed to script, but has to be implemented in order to
// not break binary compat.
NS_IMETHODIMP
nsHTMLSharedElement::GetProfile(nsAString& aValue)
{
  return NS_ERROR_FAILURE;
}
NS_IMETHODIMP
nsHTMLSharedElement::SetProfile(const nsAString& aValue)
{
  return NS_ERROR_FAILURE;
}

// nsIDOMHTMLHtmlElement
NS_IMPL_STRING_ATTR(nsHTMLSharedElement, Version, version)

// nsIDOMHTMLBaseElement
NS_IMPL_URI_ATTR(nsHTMLSharedElement, Href, href)
NS_IMPL_STRING_ATTR(nsHTMLSharedElement, Target, target)

PRBool
nsHTMLSharedElement::ParseAttribute(PRInt32 aNamespaceID,
                                    nsIAtom* aAttribute,
                                    const nsAString& aValue,
                                    nsAttrValue& aResult)
{
  if (aNamespaceID == kNameSpaceID_None &&
      (mNodeInfo->Equals(nsGkAtoms::dir) ||
       mNodeInfo->Equals(nsGkAtoms::menu))) {
    if (aAttribute == nsGkAtoms::type) {
      return aResult.ParseEnumValue(aValue, kListTypeTable, PR_FALSE);
    }
    if (aAttribute == nsGkAtoms::start) {
      return aResult.ParseIntWithBounds(aValue, 1);
    }
  }

  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aResult);
}

static void
DirectoryMenuMapAttributesIntoRule(const nsMappedAttributes* aAttributes,
                               nsRuleData* aData)
{
  if (aData->mSIDs & NS_STYLE_INHERIT_BIT(List)) {
    if (aData->mListData->mType.GetUnit() == eCSSUnit_Null) {
      // type: enum
      const nsAttrValue* value = aAttributes->GetAttr(nsGkAtoms::type);
      if (value) {
        if (value->Type() == nsAttrValue::eEnum) {
          aData->mListData->mType.SetIntValue(value->GetEnumValue(), eCSSUnit_Enumerated);
        } else {
          aData->mListData->mType.SetIntValue(NS_STYLE_LIST_STYLE_DISC, eCSSUnit_Enumerated);
        }
      }
    }
  }

  nsGenericHTMLElement::MapCommonAttributesInto(aAttributes, aData);
}

NS_IMETHODIMP_(PRBool)
nsHTMLSharedElement::IsAttributeMapped(const nsIAtom* aAttribute) const
{
  if (mNodeInfo->Equals(nsGkAtoms::dir)) {
    static const MappedAttributeEntry attributes[] = {
      { &nsGkAtoms::type },
      // { &nsGkAtoms::compact }, // XXX
      { nsnull} 
    };
  
    static const MappedAttributeEntry* const map[] = {
      attributes,
      sCommonAttributeMap,
    };

    return FindAttributeDependence(aAttribute, map, NS_ARRAY_LENGTH(map));
  }

  return nsGenericHTMLElement::IsAttributeMapped(aAttribute);
}

void
SetBaseURIUsingFirstBaseWithHref(nsIContent* aHead, nsIContent* aMustMatch)
{
  NS_PRECONDITION(aHead && aHead->GetOwnerDoc() &&
                  aHead->GetOwnerDoc()->GetHeadElement() == aHead,
                  "Bad head");

  nsIDocument* doc = aHead->GetOwnerDoc();

  for (nsINode::ChildIterator iter(aHead); !iter.IsDone(); iter.Next()) {
    nsIContent* child = iter;
    if (child->NodeInfo()->Equals(nsGkAtoms::base, kNameSpaceID_XHTML) &&
        child->HasAttr(kNameSpaceID_None, nsGkAtoms::href)) {
      if (aMustMatch && child != aMustMatch) {
        return;
      }

      // Resolve the <base> element's href relative to our document URI
      nsAutoString href;
      child->GetAttr(kNameSpaceID_None, nsGkAtoms::href, href);

      nsCOMPtr<nsIURI> newBaseURI;
      nsContentUtils::NewURIWithDocumentCharset(
        getter_AddRefs(newBaseURI), href, doc, doc->GetDocumentURI());

      // Try to set our base URI.  If that fails, try to set base URI to null
      nsresult rv = doc->SetBaseURI(newBaseURI);
      if (NS_FAILED(rv)) {
        doc->SetBaseURI(nsnull);
      }
      return;
    }
  }

  doc->SetBaseURI(nsnull);
}

nsresult
nsHTMLSharedElement::SetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                             nsIAtom* aPrefix, const nsAString& aValue,
                             PRBool aNotify)
{
  nsresult rv =  nsGenericHTMLElement::SetAttr(aNameSpaceID, aName, aPrefix,
                                               aValue, aNotify);
  NS_ENSURE_SUCCESS(rv, rv);

  // If the href attribute of a <base> tag is changing, we may need to update
  // the document's base URI, which will cause all the links on the page to be
  // re-resolved given the new base.
  nsIContent* head;
  if (mNodeInfo->Equals(nsGkAtoms::base, kNameSpaceID_XHTML) &&
      aName == nsGkAtoms::href &&
      aNameSpaceID == kNameSpaceID_None &&
      IsInDoc() &&
      (head = GetParent()) &&
      head == GetOwnerDoc()->GetHeadElement()) {
    SetBaseURIUsingFirstBaseWithHref(head, this);
  }

  return NS_OK;
}

nsresult
nsHTMLSharedElement::UnsetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                               PRBool aNotify)
{
  nsresult rv = nsGenericHTMLElement::UnsetAttr(aNameSpaceID, aName, aNotify);
  NS_ENSURE_SUCCESS(rv, rv);

  // If we're the first <base> with an href and our href attribute is being
  // unset, then we're no longer the first <base> with an href, and we need to
  // find the new one.
  nsIContent* head;
  if (mNodeInfo->Equals(nsGkAtoms::base, kNameSpaceID_XHTML) &&
      aName == nsGkAtoms::href &&
      aNameSpaceID == kNameSpaceID_None &&
      IsInDoc() &&
      (head = GetParent()) &&
      head == GetOwnerDoc()->GetHeadElement()) {
    SetBaseURIUsingFirstBaseWithHref(head, nsnull);
  }

  return NS_OK;
}

nsresult
nsHTMLSharedElement::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                                nsIContent* aBindingParent,
                                PRBool aCompileEventHandlers)
{
  nsresult rv = nsGenericHTMLElement::BindToTree(aDocument, aParent,
                                                 aBindingParent,
                                                 aCompileEventHandlers);
  NS_ENSURE_SUCCESS(rv, rv);

  // The document stores a pointer to its first <base> element, which we may
  // need to update here.
  if (mNodeInfo->Equals(nsGkAtoms::base, kNameSpaceID_XHTML) &&
      HasAttr(kNameSpaceID_None, nsGkAtoms::href) &&
      aDocument && aParent &&
      aDocument->GetHeadElement() == aParent) {

    SetBaseURIUsingFirstBaseWithHref(aParent, this);
  }

  return NS_OK;
}

void
nsHTMLSharedElement::UnbindFromTree(PRBool aDeep, PRBool aNullParent)
{
  nsIDocument* doc;
  nsIContent* parent;
  PRBool inHeadBase = mNodeInfo->Equals(nsGkAtoms::base, kNameSpaceID_XHTML) &&
                      (doc = GetCurrentDoc()) &&
                      (parent = GetParent()) &&
                      parent->NodeInfo()->Equals(nsGkAtoms::head,
                                                 kNameSpaceID_XHTML);

  nsGenericHTMLElement::UnbindFromTree(aDeep, aNullParent);

  // If we're removing a <base> from a document, we may need to update the
  // document's record of the first base node.
  if (inHeadBase) {
    // We might have gotten here as a result of the <head> being removed
    // from the document. In that case we need to call SetBaseURI(nsnull)
    Element* head = doc->GetHeadElement();
    if (head) {
      SetBaseURIUsingFirstBaseWithHref(head, nsnull);
    }
    else {
      doc->SetBaseURI(nsnull);
    }
  }
}

nsMapRuleToAttributesFunc
nsHTMLSharedElement::GetAttributeMappingFunction() const
{
  if (mNodeInfo->Equals(nsGkAtoms::dir) || mNodeInfo->Equals(nsGkAtoms::menu)) {
    return &DirectoryMenuMapAttributesIntoRule;
  }

  return nsGenericHTMLElement::GetAttributeMappingFunction();
}
