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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2003
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Original Author: Aaron Leventhal (aaronl@netscape.com)
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

#include "nsAccessNodeWrap.h"

#include "AccessibleApplication.h"
#include "ISimpleDOMNode_i.c"

#include "nsAccessibilityAtoms.h"
#include "nsAccessibilityService.h"
#include "nsApplicationAccessibleWrap.h"
#include "nsCoreUtils.h"
#include "nsRootAccessible.h"

#include "nsAttrName.h"
#include "nsIDocument.h"
#include "nsIDOMNodeList.h"
#include "nsIDOMNSHTMLElement.h"
#include "nsIDOMViewCSS.h"
#include "nsIFrame.h"
#include "nsINameSpaceManager.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsPIDOMWindow.h"
#include "nsIServiceManager.h"

/// the accessible library and cached methods
HINSTANCE nsAccessNodeWrap::gmAccLib = nsnull;
HINSTANCE nsAccessNodeWrap::gmUserLib = nsnull;
LPFNACCESSIBLEOBJECTFROMWINDOW nsAccessNodeWrap::gmAccessibleObjectFromWindow = nsnull;
LPFNNOTIFYWINEVENT nsAccessNodeWrap::gmNotifyWinEvent = nsnull;
LPFNGETGUITHREADINFO nsAccessNodeWrap::gmGetGUIThreadInfo = nsnull;

PRBool nsAccessNodeWrap::gIsEnumVariantSupportDisabled = 0;
// Used to determine whether an IAccessible2 compatible screen reader is loaded.
PRBool nsAccessNodeWrap::gIsIA2Disabled = PR_FALSE;

AccTextChangeEvent* nsAccessNodeWrap::gTextEvent = nsnull;

// Pref to disallow CtrlTab preview functionality if JAWS or Window-Eyes are
// running.
#define CTRLTAB_DISALLOW_FOR_SCREEN_READERS_PREF "browser.ctrlTab.disallowForScreenReaders"


/* For documentation of the accessibility architecture, 
 * see http://lxr.mozilla.org/seamonkey/source/accessible/accessible-docs.html
 */

////////////////////////////////////////////////////////////////////////////////
// nsAccessNodeWrap
////////////////////////////////////////////////////////////////////////////////

nsAccessNodeWrap::
  nsAccessNodeWrap(nsIContent *aContent, nsIWeakReference *aShell) :
  nsAccessNode(aContent, aShell)
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

  if (IID_IUnknown == iid || IID_ISimpleDOMNode == iid)
    *ppv = static_cast<ISimpleDOMNode*>(this);

  if (nsnull == *ppv)
    return E_NOINTERFACE;      //iid not supported.
   
  (reinterpret_cast<IUnknown*>(*ppv))->AddRef(); 
  return S_OK;
}

STDMETHODIMP
nsAccessNodeWrap::QueryService(REFGUID guidService, REFIID iid, void** ppv)
{
  static const GUID IID_SimpleDOMDeprecated = {0x0c539790,0x12e4,0x11cf,0xb6,0x61,0x00,0xaa,0x00,0x4c,0xd6,0xd8};
  if (guidService != IID_ISimpleDOMNode &&
      guidService != IID_SimpleDOMDeprecated &&
      guidService != IID_IAccessible &&  guidService != IID_IAccessible2 &&
      guidService != IID_IAccessibleApplication)
    return E_INVALIDARG;

  // Can get to IAccessibleApplication from any node via QS
  if (iid == IID_IAccessibleApplication) {
    nsApplicationAccessible *applicationAcc = GetApplicationAccessible();
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

  return QueryInterface(iid, ppv);
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

  if (IsDefunct())
    return E_FAIL;

  nsCOMPtr<nsIDOMNode> DOMNode(do_QueryInterface(GetNode()));

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
  void *uniqueID;
  GetUniqueID(&uniqueID);
  *aUniqueID = - NS_PTR_TO_INT32(uniqueID);

  *aNumChildren = GetNode()->GetChildCount();

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

  if (IsDefunct() || IsDocument())
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
  if (IsDefunct() || IsDocument())
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

  if (IsDefunct() || IsDocument())
    return E_FAIL;

  nsCOMPtr<nsIDOMCSSStyleDeclaration> cssDecl =
    nsCoreUtils::GetComputedStyleDeclaration(EmptyString(), mContent);
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
  if (IsDefunct() || IsDocument())
    return E_FAIL;
 
  nsCOMPtr<nsIDOMCSSStyleDeclaration> cssDecl =
    nsCoreUtils::GetComputedStyleDeclaration(EmptyString(), mContent);
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

  nsresult rv = ScrollTo(scrollType);
  if (NS_SUCCEEDED(rv))
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
  nsAccessible *acc =
    GetAccService()->GetAccessibleInWeakShell(aNode, mWeakShell);
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

    newNode = new nsAccessNodeWrap(content, mWeakShell);
    if (!newNode)
      return NULL;

    newNode->Init();
    iNode = static_cast<ISimpleDOMNode*>(newNode);
    iNode->AddRef();
  }

  return iNode;
}


STDMETHODIMP nsAccessNodeWrap::get_parentNode(ISimpleDOMNode __RPC_FAR *__RPC_FAR *aNode)
{
__try {
  if (IsDefunct())
    return E_FAIL;

  *aNode = MakeAccessNode(GetNode()->GetNodeParent());

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP nsAccessNodeWrap::get_firstChild(ISimpleDOMNode __RPC_FAR *__RPC_FAR *aNode)
{
__try {
  if (IsDefunct())
    return E_FAIL;

  *aNode = MakeAccessNode(GetNode()->GetFirstChild());

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP nsAccessNodeWrap::get_lastChild(ISimpleDOMNode __RPC_FAR *__RPC_FAR *aNode)
{
__try {
  if (IsDefunct())
    return E_FAIL;

  *aNode = MakeAccessNode(GetNode()->GetLastChild());

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP nsAccessNodeWrap::get_previousSibling(ISimpleDOMNode __RPC_FAR *__RPC_FAR *aNode)
{
__try {
  if (IsDefunct())
    return E_FAIL;

  *aNode = MakeAccessNode(GetNode()->GetPreviousSibling());

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP nsAccessNodeWrap::get_nextSibling(ISimpleDOMNode __RPC_FAR *__RPC_FAR *aNode)
{
__try {
  if (IsDefunct())
    return E_FAIL;

  *aNode = MakeAccessNode(GetNode()->GetNextSibling());

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP 
nsAccessNodeWrap::get_childAt(unsigned aChildIndex,
                              ISimpleDOMNode __RPC_FAR *__RPC_FAR *aNode)
{
__try {
  *aNode = nsnull;

  if (IsDefunct())
    return E_FAIL;

  *aNode = MakeAccessNode(GetNode()->GetChildAt(aChildIndex));

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP 
nsAccessNodeWrap::get_innerHTML(BSTR __RPC_FAR *aInnerHTML)
{
__try {
  *aInnerHTML = nsnull;

  nsCOMPtr<nsIDOMNSHTMLElement> domNSElement(do_QueryInterface(GetNode()));
  if (!domNSElement)
    return E_FAIL; // Node already shut down

  nsAutoString innerHTML;
  domNSElement->GetInnerHTML(innerHTML);
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
  if (NS_FAILED(GetLanguage(language))) {
    return E_FAIL;
  }

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
  *localInterface = static_cast<nsIAccessNode*>(this);
  NS_ADDREF_THIS();
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return S_OK;
}
 
void nsAccessNodeWrap::InitAccessibility()
{
  nsCOMPtr<nsIPrefBranch> prefBranch(do_GetService(NS_PREFSERVICE_CONTRACTID));
  if (prefBranch) {
    prefBranch->GetBoolPref("accessibility.disableenumvariant", &gIsEnumVariantSupportDisabled);
  }

  if (!gmUserLib) {
    gmUserLib =::LoadLibraryW(L"USER32.DLL");
  }

  if (gmUserLib) {
    if (!gmNotifyWinEvent)
      gmNotifyWinEvent = (LPFNNOTIFYWINEVENT)GetProcAddress(gmUserLib,"NotifyWinEvent");
    if (!gmGetGUIThreadInfo)
      gmGetGUIThreadInfo = (LPFNGETGUITHREADINFO)GetProcAddress(gmUserLib,"GetGUIThreadInfo");
  }

  DoATSpecificProcessing();
  
  nsAccessNode::InitXPAccessibility();
}

void nsAccessNodeWrap::ShutdownAccessibility()
{
  NS_IF_RELEASE(gTextEvent);
  ::DestroyCaret();

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

PRBool nsAccessNodeWrap::IsOnlyMsaaCompatibleJawsPresent()
{
  HMODULE jhookhandle = ::GetModuleHandleW(L"jhook");
  if (!jhookhandle)
    return PR_FALSE;  // No JAWS, or some other screen reader, use IA2

  PRUnichar fileName[MAX_PATH];
  ::GetModuleFileNameW(jhookhandle, fileName, MAX_PATH);

  DWORD dummy;
  DWORD length = ::GetFileVersionInfoSizeW(fileName, &dummy);

  LPBYTE versionInfo = new BYTE[length];
  ::GetFileVersionInfoW(fileName, 0, length, versionInfo);

  UINT uLen;
  VS_FIXEDFILEINFO *fixedFileInfo;
  ::VerQueryValueW(versionInfo, L"\\", (LPVOID*)&fixedFileInfo, &uLen);
  DWORD dwFileVersionMS = fixedFileInfo->dwFileVersionMS;
  DWORD dwFileVersionLS = fixedFileInfo->dwFileVersionLS;
  delete [] versionInfo;

  DWORD dwLeftMost = HIWORD(dwFileVersionMS);
//  DWORD dwSecondLeft = LOWORD(dwFileVersionMS);
  DWORD dwSecondRight = HIWORD(dwFileVersionLS);
//  DWORD dwRightMost = LOWORD(dwFileVersionLS);

  return (dwLeftMost < 8
          || (dwLeftMost == 8 && dwSecondRight < 2173));
}

void nsAccessNodeWrap::TurnOffNewTabSwitchingForJawsAndWE()
{
  HMODULE srHandle = ::GetModuleHandleW(L"jhook");
  if (!srHandle) {
    // No JAWS, try Window-Eyes
    srHandle = ::GetModuleHandleW(L"gwm32inc");
    if (!srHandle) {
      // no screen reader we're interested in. Bail out.
      return;
    }
  }

  // Check to see if the pref for disallowing CtrlTab is already set.
  // If so, bail out.
  // If not, set it.
  nsCOMPtr<nsIPrefBranch> prefs (do_GetService(NS_PREFSERVICE_CONTRACTID));
  if (prefs) {
    PRBool hasDisallowNewCtrlTabPref = PR_FALSE;
    nsresult rv = prefs->PrefHasUserValue(CTRLTAB_DISALLOW_FOR_SCREEN_READERS_PREF,
             &hasDisallowNewCtrlTabPref);
    if (NS_SUCCEEDED(rv) && hasDisallowNewCtrlTabPref) {
      // This pref has been set before. There is no default for it.
      // Do nothing further, respect the setting that's there.
      // That way, if noone touches it, it'll stay on after toggled once.
      // If someone decided to turn it off, we respect that, too.
      return;
    }
    
    // Value has never been set, set it.
    prefs->SetBoolPref(CTRLTAB_DISALLOW_FOR_SCREEN_READERS_PREF, PR_TRUE);
  }
}

void nsAccessNodeWrap::DoATSpecificProcessing()
{
  if (IsOnlyMsaaCompatibleJawsPresent())
    // All versions below 8.0.2173 are not compatible
    gIsIA2Disabled  = PR_TRUE;

  TurnOffNewTabSwitchingForJawsAndWE();
}
