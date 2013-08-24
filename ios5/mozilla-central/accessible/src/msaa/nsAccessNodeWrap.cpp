/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAccessNodeWrap.h"

#include "AccessibleApplication.h"
#include "ApplicationAccessibleWrap.h"
#include "ISimpleDOMNode_i.c"

#include "Compatibility.h"
#include "nsAccessibilityService.h"
#include "nsCoreUtils.h"
#include "nsWinUtils.h"
#include "RootAccessible.h"
#include "Statistics.h"

#include "nsAttrName.h"
#include "nsIDOMNodeList.h"
#include "nsIDOMHTMLElement.h"
#include "nsIFrame.h"
#include "nsINameSpaceManager.h"
#include "nsPIDOMWindow.h"
#include "nsIServiceManager.h"

using namespace mozilla;
using namespace mozilla::a11y;

AccTextChangeEvent* nsAccessNodeWrap::gTextEvent = nsnull;

////////////////////////////////////////////////////////////////////////////////
// nsAccessNodeWrap
////////////////////////////////////////////////////////////////////////////////

nsAccessNodeWrap::
  nsAccessNodeWrap(nsIContent* aContent, DocAccessible* aDoc) :
  nsAccessNode(aContent, aDoc)
{
}

nsAccessNodeWrap::~nsAccessNodeWrap()
{
}

//-----------------------------------------------------
// nsISupports methods
//-----------------------------------------------------

NS_IMPL_ISUPPORTS_INHERITED1(nsAccessNodeWrap, nsAccessNode, nsIWinAccessNode);

//-----------------------------------------------------
// nsIWinAccessNode methods
//-----------------------------------------------------

NS_IMETHODIMP
nsAccessNodeWrap::QueryNativeInterface(REFIID aIID, void** aInstancePtr)
{
  return QueryInterface(aIID, aInstancePtr);
}

//-----------------------------------------------------
// IUnknown interface methods - see iunknown.h for documentation
//-----------------------------------------------------

STDMETHODIMP nsAccessNodeWrap::QueryInterface(REFIID iid, void** ppv)
{
  *ppv = nsnull;

  if (IID_IUnknown == iid) {
    *ppv = static_cast<ISimpleDOMNode*>(this);
  } else if (IID_ISimpleDOMNode == iid) {
    statistics::ISimpleDOMUsed();
    *ppv = static_cast<ISimpleDOMNode*>(this);
  } else {
    return E_NOINTERFACE;      //iid not supported.
  }
   
  (reinterpret_cast<IUnknown*>(*ppv))->AddRef(); 
  return S_OK;
}

STDMETHODIMP
nsAccessNodeWrap::QueryService(REFGUID guidService, REFIID iid, void** ppv)
{
  *ppv = nsnull;

  // Provide a special service ID for getting the accessible for the browser tab
  // document that contains this accessible object. If this accessible object
  // is not inside a browser tab then the service fails with E_NOINTERFACE.
  // A use case for this is for screen readers that need to switch context or
  // 'virtual buffer' when focus moves from one browser tab area to another.
  static const GUID SID_IAccessibleContentDocument =
    { 0xa5d8e1f3,0x3571,0x4d8f,0x95,0x21,0x07,0xed,0x28,0xfb,0x07,0x2e };
  if (guidService == SID_IAccessibleContentDocument) {
    if (iid != IID_IAccessible)
      return E_NOINTERFACE;

    nsCOMPtr<nsIDocShellTreeItem> docShellTreeItem = 
      nsCoreUtils::GetDocShellTreeItemFor(mContent);
    if (!docShellTreeItem)
      return E_UNEXPECTED;

    // Walk up the parent chain without crossing the boundary at which item
    // types change, preventing us from walking up out of tab content.
    nsCOMPtr<nsIDocShellTreeItem> root;
    docShellTreeItem->GetSameTypeRootTreeItem(getter_AddRefs(root));
    if (!root)
      return E_UNEXPECTED;


    // If the item type is typeContent, we assume we are in browser tab content.
    // Note this includes content such as about:addons, for consistency.
    PRInt32 itemType;
    root->GetItemType(&itemType);
    if (itemType != nsIDocShellTreeItem::typeContent)
      return E_NOINTERFACE;

    // Make sure this is a document.
    DocAccessible* docAcc = nsAccUtils::GetDocAccessibleFor(root);
    if (!docAcc)
      return E_UNEXPECTED;

    *ppv = static_cast<IAccessible*>(docAcc);

    (reinterpret_cast<IUnknown*>(*ppv))->AddRef();
    return NS_OK;
  }

  // Can get to IAccessibleApplication from any node via QS
  if (guidService == IID_IAccessibleApplication) {
    ApplicationAccessible* applicationAcc = GetApplicationAccessible();
    if (!applicationAcc)
      return E_NOINTERFACE;

    nsresult rv = applicationAcc->QueryNativeInterface(iid, ppv);
    return NS_SUCCEEDED(rv) ? S_OK : E_NOINTERFACE;
  }

  /**
   * To get an ISimpleDOMNode, ISimpleDOMDocument, ISimpleDOMText
   * or any IAccessible2 interface on should use IServiceProvider like this:
   * -----------------------------------------------------------------------
   * ISimpleDOMDocument *pAccDoc = NULL;
   * IServiceProvider *pServProv = NULL;
   * pAcc->QueryInterface(IID_IServiceProvider, (void**)&pServProv);
   * if (pServProv) {
   *   const GUID unused;
   *   pServProv->QueryService(unused, IID_ISimpleDOMDocument, (void**)&pAccDoc);
   *   pServProv->Release();
   * }
   */

  static const GUID IID_SimpleDOMDeprecated =
    { 0x0c539790,0x12e4,0x11cf,0xb6,0x61,0x00,0xaa,0x00,0x4c,0xd6,0xd8 };
  if (guidService == IID_ISimpleDOMNode ||
      guidService == IID_SimpleDOMDeprecated ||
      guidService == IID_IAccessible ||  guidService == IID_IAccessible2)
    return QueryInterface(iid, ppv);

  return E_INVALIDARG;
}

//-----------------------------------------------------
// ISimpleDOMNode methods
//-----------------------------------------------------

STDMETHODIMP nsAccessNodeWrap::get_nodeInfo( 
    /* [out] */ BSTR __RPC_FAR *aNodeName,
    /* [out] */ short __RPC_FAR *aNameSpaceID,
    /* [out] */ BSTR __RPC_FAR *aNodeValue,
    /* [out] */ unsigned int __RPC_FAR *aNumChildren,
    /* [out] */ unsigned int __RPC_FAR *aUniqueID,
    /* [out] */ unsigned short __RPC_FAR *aNodeType)
{
__try{
  *aNodeName = nsnull;
  *aNodeValue = nsnull;

  nsINode* node = GetNode();
  if (!node)
    return E_FAIL;

  nsCOMPtr<nsIDOMNode> DOMNode(do_QueryInterface(node));

  PRUint16 nodeType = 0;
  DOMNode->GetNodeType(&nodeType);
  *aNodeType=static_cast<unsigned short>(nodeType);

  if (*aNodeType !=  NODETYPE_TEXT) {
    nsAutoString nodeName;
    DOMNode->GetNodeName(nodeName);
    *aNodeName =   ::SysAllocString(nodeName.get());
  }

  nsAutoString nodeValue;

  DOMNode->GetNodeValue(nodeValue);
  *aNodeValue = ::SysAllocString(nodeValue.get());

  *aNameSpaceID = IsContent() ?
    static_cast<short>(mContent->GetNameSpaceID()) : 0;

  // This is a unique ID for every content node.  The 3rd party
  // accessibility application can compare this to the childID we
  // return for events such as focus events, to correlate back to
  // data nodes in their internal object model.
  *aUniqueID = - NS_PTR_TO_INT32(UniqueID());

  *aNumChildren = node->GetChildCount();

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return S_OK;
}


       
STDMETHODIMP nsAccessNodeWrap::get_attributes( 
    /* [in] */ unsigned short aMaxAttribs,
    /* [length_is][size_is][out] */ BSTR __RPC_FAR *aAttribNames,
    /* [length_is][size_is][out] */ short __RPC_FAR *aNameSpaceIDs,
    /* [length_is][size_is][out] */ BSTR __RPC_FAR *aAttribValues,
    /* [out] */ unsigned short __RPC_FAR *aNumAttribs)
{
__try{
  *aNumAttribs = 0;

  if (!mContent || IsDocumentNode())
    return E_FAIL;

  PRUint32 numAttribs = mContent->GetAttrCount();
  if (numAttribs > aMaxAttribs)
    numAttribs = aMaxAttribs;
  *aNumAttribs = static_cast<unsigned short>(numAttribs);

  for (PRUint32 index = 0; index < numAttribs; index++) {
    aNameSpaceIDs[index] = 0; aAttribValues[index] = aAttribNames[index] = nsnull;
    nsAutoString attributeValue;

    const nsAttrName* name = mContent->GetAttrNameAt(index);
    aNameSpaceIDs[index] = static_cast<short>(name->NamespaceID());
    aAttribNames[index] = ::SysAllocString(name->LocalName()->GetUTF16String());
    mContent->GetAttr(name->NamespaceID(), name->LocalName(), attributeValue);
    aAttribValues[index] = ::SysAllocString(attributeValue.get());
  }
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK; 
}
        

STDMETHODIMP nsAccessNodeWrap::get_attributesForNames( 
    /* [in] */ unsigned short aNumAttribs,
    /* [length_is][size_is][in] */ BSTR __RPC_FAR *aAttribNames,
    /* [length_is][size_is][in] */ short __RPC_FAR *aNameSpaceID,
    /* [length_is][size_is][retval] */ BSTR __RPC_FAR *aAttribValues)
{
__try {
  if (!mContent || !IsElement())
    return E_FAIL;

  nsCOMPtr<nsIDOMElement> domElement(do_QueryInterface(mContent));
  nsCOMPtr<nsINameSpaceManager> nameSpaceManager =
    do_GetService(NS_NAMESPACEMANAGER_CONTRACTID);

  PRInt32 index;

  for (index = 0; index < aNumAttribs; index++) {
    aAttribValues[index] = nsnull;
    if (aAttribNames[index]) {
      nsAutoString attributeValue, nameSpaceURI;
      nsAutoString attributeName(nsDependentString(static_cast<PRUnichar*>(aAttribNames[index])));
      nsresult rv;

      if (aNameSpaceID[index]>0 && 
        NS_SUCCEEDED(nameSpaceManager->GetNameSpaceURI(aNameSpaceID[index], nameSpaceURI)))
          rv = domElement->GetAttributeNS(nameSpaceURI, attributeName, attributeValue);
      else 
        rv = domElement->GetAttribute(attributeName, attributeValue);

      if (NS_SUCCEEDED(rv))
        aAttribValues[index] = ::SysAllocString(attributeValue.get());
    }
  }
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK; 
}

/* To do: use media type if not null */
STDMETHODIMP nsAccessNodeWrap::get_computedStyle( 
    /* [in] */ unsigned short aMaxStyleProperties,
    /* [in] */ boolean aUseAlternateView,
    /* [length_is][size_is][out] */ BSTR __RPC_FAR *aStyleProperties,
    /* [length_is][size_is][out] */ BSTR __RPC_FAR *aStyleValues,
    /* [out] */ unsigned short __RPC_FAR *aNumStyleProperties)
{
__try{
  *aNumStyleProperties = 0;

  if (!mContent || IsDocumentNode())
    return E_FAIL;

  nsCOMPtr<nsIDOMCSSStyleDeclaration> cssDecl =
    nsWinUtils::GetComputedStyleDeclaration(mContent);
  NS_ENSURE_TRUE(cssDecl, E_FAIL);

  PRUint32 length;
  cssDecl->GetLength(&length);

  PRUint32 index, realIndex;
  for (index = realIndex = 0; index < length && realIndex < aMaxStyleProperties; index ++) {
    nsAutoString property, value;
    if (NS_SUCCEEDED(cssDecl->Item(index, property)) && property.CharAt(0) != '-')  // Ignore -moz-* properties
      cssDecl->GetPropertyValue(property, value);  // Get property value
    if (!value.IsEmpty()) {
      aStyleProperties[realIndex] =   ::SysAllocString(property.get());
      aStyleValues[realIndex]     =   ::SysAllocString(value.get());
      ++realIndex;
    }
  }
  *aNumStyleProperties = static_cast<unsigned short>(realIndex);
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}


STDMETHODIMP nsAccessNodeWrap::get_computedStyleForProperties( 
    /* [in] */ unsigned short aNumStyleProperties,
    /* [in] */ boolean aUseAlternateView,
    /* [length_is][size_is][in] */ BSTR __RPC_FAR *aStyleProperties,
    /* [length_is][size_is][out] */ BSTR __RPC_FAR *aStyleValues)
{
__try {
  if (!mContent || IsDocumentNode())
    return E_FAIL;
 
  nsCOMPtr<nsIDOMCSSStyleDeclaration> cssDecl =
    nsWinUtils::GetComputedStyleDeclaration(mContent);
  NS_ENSURE_TRUE(cssDecl, E_FAIL);

  PRUint32 index;
  for (index = 0; index < aNumStyleProperties; index ++) {
    nsAutoString value;
    if (aStyleProperties[index])
      cssDecl->GetPropertyValue(nsDependentString(static_cast<PRUnichar*>(aStyleProperties[index])), value);  // Get property value
    aStyleValues[index] = ::SysAllocString(value.get());
  }
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP nsAccessNodeWrap::scrollTo(/* [in] */ boolean aScrollTopLeft)
{
__try {
  PRUint32 scrollType =
    aScrollTopLeft ? nsIAccessibleScrollType::SCROLL_TYPE_TOP_LEFT :
                     nsIAccessibleScrollType::SCROLL_TYPE_BOTTOM_RIGHT;

  nsCoreUtils::ScrollTo(mDoc->PresShell(), mContent, scrollType);
  return S_OK;
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return E_FAIL;
}

ISimpleDOMNode*
nsAccessNodeWrap::MakeAccessNode(nsINode *aNode)
{
  if (!aNode)
    return NULL;

  nsAccessNodeWrap *newNode = NULL;

  ISimpleDOMNode *iNode = NULL;
  Accessible* acc = mDoc->GetAccessible(aNode);
  if (acc) {
    IAccessible *msaaAccessible = nsnull;
    acc->GetNativeInterface((void**)&msaaAccessible); // addrefs
    msaaAccessible->QueryInterface(IID_ISimpleDOMNode, (void**)&iNode); // addrefs
    msaaAccessible->Release(); // Release IAccessible
  }
  else {
    nsCOMPtr<nsIContent> content(do_QueryInterface(aNode));
    if (!content) {
      NS_NOTREACHED("The node is a document which is not accessible!");
      return NULL;
    }

    newNode = new nsAccessNodeWrap(content, mDoc);
    if (!newNode)
      return NULL;

    iNode = static_cast<ISimpleDOMNode*>(newNode);
    iNode->AddRef();
  }

  return iNode;
}


STDMETHODIMP nsAccessNodeWrap::get_parentNode(ISimpleDOMNode __RPC_FAR *__RPC_FAR *aNode)
{
__try {
  nsINode* node = GetNode();
  if (!node)
    return E_FAIL;

  *aNode = MakeAccessNode(node->GetNodeParent());

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP nsAccessNodeWrap::get_firstChild(ISimpleDOMNode __RPC_FAR *__RPC_FAR *aNode)
{
__try {
  nsINode* node = GetNode();
  if (!node)
    return E_FAIL;

  *aNode = MakeAccessNode(node->GetFirstChild());

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP nsAccessNodeWrap::get_lastChild(ISimpleDOMNode __RPC_FAR *__RPC_FAR *aNode)
{
__try {
  nsINode* node = GetNode();
  if (!node)
    return E_FAIL;

  *aNode = MakeAccessNode(node->GetLastChild());

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP nsAccessNodeWrap::get_previousSibling(ISimpleDOMNode __RPC_FAR *__RPC_FAR *aNode)
{
__try {
  nsINode* node = GetNode();
  if (!node)
    return E_FAIL;

  *aNode = MakeAccessNode(node->GetPreviousSibling());

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP nsAccessNodeWrap::get_nextSibling(ISimpleDOMNode __RPC_FAR *__RPC_FAR *aNode)
{
__try {
  nsINode* node = GetNode();
  if (!node)
    return E_FAIL;

  *aNode = MakeAccessNode(node->GetNextSibling());

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP 
nsAccessNodeWrap::get_childAt(unsigned aChildIndex,
                              ISimpleDOMNode __RPC_FAR *__RPC_FAR *aNode)
{
__try {
  *aNode = nsnull;

  nsINode* node = GetNode();
  if (!node)
    return E_FAIL;

  *aNode = MakeAccessNode(node->GetChildAt(aChildIndex));

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP 
nsAccessNodeWrap::get_innerHTML(BSTR __RPC_FAR *aInnerHTML)
{
__try {
  *aInnerHTML = nsnull;

  nsCOMPtr<nsIDOMHTMLElement> htmlElement = do_QueryInterface(GetNode());
  if (!htmlElement)
    return E_FAIL; // Node already shut down

  nsAutoString innerHTML;
  htmlElement->GetInnerHTML(innerHTML);
  if (innerHTML.IsEmpty())
    return S_FALSE;

  *aInnerHTML = ::SysAllocStringLen(innerHTML.get(), innerHTML.Length());
  if (!*aInnerHTML)
    return E_OUTOFMEMORY;

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP 
nsAccessNodeWrap::get_language(BSTR __RPC_FAR *aLanguage)
{
__try {
  *aLanguage = NULL;

  nsAutoString language;
  Language(language);
  if (language.IsEmpty())
    return S_FALSE;

  *aLanguage = ::SysAllocStringLen(language.get(), language.Length());
  if (!*aLanguage)
    return E_OUTOFMEMORY;

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP 
nsAccessNodeWrap::get_localInterface( 
    /* [out] */ void __RPC_FAR *__RPC_FAR *localInterface)
{
__try {
  *localInterface = static_cast<nsAccessNode*>(this);
  NS_ADDREF_THIS();
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return S_OK;
}
 
void nsAccessNodeWrap::InitAccessibility()
{
  Compatibility::Init();

  nsWinUtils::MaybeStartWindowEmulation();
}

void nsAccessNodeWrap::ShutdownAccessibility()
{
  NS_IF_RELEASE(gTextEvent);
  ::DestroyCaret();

  nsWinUtils::ShutdownWindowEmulation();

  nsAccessNode::ShutdownXPAccessibility();
}

int nsAccessNodeWrap::FilterA11yExceptions(unsigned int aCode, EXCEPTION_POINTERS *aExceptionInfo)
{
  if (aCode == EXCEPTION_ACCESS_VIOLATION) {
#ifdef MOZ_CRASHREPORTER
    // MSAA swallows crashes (because it is COM-based)
    // but we still need to learn about those crashes so we can fix them
    // Make sure to pass them to the crash reporter
    nsCOMPtr<nsICrashReporter> crashReporter =
      do_GetService("@mozilla.org/toolkit/crash-reporter;1");
    if (crashReporter) {
      crashReporter->WriteMinidumpForException(aExceptionInfo);
    }
#endif
  }
  else {
    NS_NOTREACHED("We should only be catching crash exceptions");
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

HRESULT
GetHRESULT(nsresult aResult)
{
  switch (aResult) {
    case NS_OK:
      return S_OK;

    case NS_ERROR_INVALID_ARG: case NS_ERROR_INVALID_POINTER:
      return E_INVALIDARG;

    case NS_ERROR_OUT_OF_MEMORY:
      return E_OUTOFMEMORY;

    case NS_ERROR_NOT_IMPLEMENTED:
      return E_NOTIMPL;

    default:
      return E_FAIL;
  }
}

nsRefPtrHashtable<nsPtrHashKey<void>, DocAccessible> nsAccessNodeWrap::sHWNDCache;

LRESULT CALLBACK
nsAccessNodeWrap::WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  // Note, this window's message handling should not invoke any call that
  // may result in a cross-process ipc call. Doing so may violate RPC
  // message semantics.

  switch (msg) {
    case WM_GETOBJECT:
    {
      if (lParam == OBJID_CLIENT) {
        DocAccessible* document = sHWNDCache.GetWeak(static_cast<void*>(hWnd));
        if (document) {
          IAccessible* msaaAccessible = NULL;
          document->GetNativeInterface((void**)&msaaAccessible); // does an addref
          if (msaaAccessible) {
            LRESULT result = ::LresultFromObject(IID_IAccessible, wParam,
                                                 msaaAccessible); // does an addref
            msaaAccessible->Release(); // release extra addref
            return result;
          }
        }
      }
      return 0;
    }
    case WM_NCHITTEST:
    {
      LRESULT lRet = ::DefWindowProc(hWnd, msg, wParam, lParam);
      if (HTCLIENT == lRet)
        lRet = HTTRANSPARENT;
      return lRet;
    }
  }

  return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
