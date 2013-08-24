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

#include "nsAccessibleWrap.h"

#include "nsAccessibilityAtoms.h"
#include "nsAccUtils.h"
#include "nsCoreUtils.h"
#include "nsRelUtils.h"

#include "nsIAccessibleDocument.h"
#include "nsIAccessibleSelectable.h"
#include "nsIAccessibleEvent.h"
#include "nsIAccessibleWin32Object.h"

#include "Accessible2_i.c"
#include "AccessibleStates.h"

#include "nsIMutableArray.h"
#include "nsIDOMDocument.h"
#include "nsIFrame.h"
#include "nsIScrollableFrame.h"
#include "nsINameSpaceManager.h"
#include "nsINodeInfo.h"
#include "nsIPrefService.h"
#include "nsRootAccessible.h"
#include "nsIServiceManager.h"
#include "nsTextFormatter.h"
#include "nsIView.h"
#include "nsIViewManager.h"
#include "nsRoleMap.h"
#include "nsEventMap.h"
#include "nsArrayUtils.h"

/* For documentation of the accessibility architecture,
 * see http://lxr.mozilla.org/seamonkey/source/accessible/accessible-docs.html
 */

//#define DEBUG_LEAKS

#ifdef DEBUG_LEAKS
static gAccessibles = 0;
#endif

EXTERN_C GUID CDECL CLSID_Accessible =
{ 0x61044601, 0xa811, 0x4e2b, { 0xbb, 0xba, 0x17, 0xbf, 0xab, 0xd3, 0x29, 0xd7 } };

static const PRInt32 kIEnumVariantDisconnected = -1;

////////////////////////////////////////////////////////////////////////////////
// nsAccessibleWrap
////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------
// construction
//-----------------------------------------------------
nsAccessibleWrap::
  nsAccessibleWrap(nsIContent *aContent, nsIWeakReference *aShell) :
  nsAccessible(aContent, aShell), mEnumVARIANTPosition(0), mTypeInfo(NULL)
{
}

//-----------------------------------------------------
// destruction
//-----------------------------------------------------
nsAccessibleWrap::~nsAccessibleWrap()
{
  if (mTypeInfo)
    mTypeInfo->Release();
}

NS_IMPL_ISUPPORTS_INHERITED0(nsAccessibleWrap, nsAccessible);

//-----------------------------------------------------
// IUnknown interface methods - see iunknown.h for documentation
//-----------------------------------------------------

// Microsoft COM QueryInterface
STDMETHODIMP nsAccessibleWrap::QueryInterface(REFIID iid, void** ppv)
{
__try {
  *ppv = NULL;

  if (IID_IUnknown == iid || IID_IDispatch == iid || IID_IAccessible == iid)
    *ppv = static_cast<IAccessible*>(this);
  else if (IID_IEnumVARIANT == iid && !gIsEnumVariantSupportDisabled) {
    long numChildren;
    get_accChildCount(&numChildren);
    if (numChildren > 0)  // Don't support this interface for leaf elements
      *ppv = static_cast<IEnumVARIANT*>(this);
  } else if (IID_IServiceProvider == iid)
    *ppv = static_cast<IServiceProvider*>(this);
  else if (IID_IAccessible2 == iid && !gIsIA2Disabled)
    *ppv = static_cast<IAccessible2*>(this);

  if (NULL == *ppv) {
    HRESULT hr = CAccessibleComponent::QueryInterface(iid, ppv);
    if (SUCCEEDED(hr))
      return hr;
  }

  if (NULL == *ppv) {
    HRESULT hr = CAccessibleHyperlink::QueryInterface(iid, ppv);
    if (SUCCEEDED(hr))
      return hr;
  }

  if (NULL == *ppv) {
    HRESULT hr = CAccessibleValue::QueryInterface(iid, ppv);
    if (SUCCEEDED(hr))
      return hr;
  }

  if (NULL == *ppv)
    return nsAccessNodeWrap::QueryInterface(iid, ppv);

  (reinterpret_cast<IUnknown*>(*ppv))->AddRef();
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return S_OK;
}

//-----------------------------------------------------
// IAccessible methods
//-----------------------------------------------------


STDMETHODIMP nsAccessibleWrap::AccessibleObjectFromWindow(HWND hwnd,
                                                          DWORD dwObjectID,
                                                          REFIID riid,
                                                          void **ppvObject)
{
  // open the dll dynamically
  if (!gmAccLib)
    gmAccLib =::LoadLibraryW(L"OLEACC.DLL");

  if (gmAccLib) {
    if (!gmAccessibleObjectFromWindow)
      gmAccessibleObjectFromWindow = (LPFNACCESSIBLEOBJECTFROMWINDOW)GetProcAddress(gmAccLib,"AccessibleObjectFromWindow");

    if (gmAccessibleObjectFromWindow)
      return gmAccessibleObjectFromWindow(hwnd, dwObjectID, riid, ppvObject);
  }

  return E_FAIL;
}

STDMETHODIMP nsAccessibleWrap::NotifyWinEvent(DWORD event,
                                              HWND hwnd,
                                              LONG idObjectType,
                                              LONG idObject)
{
  if (gmNotifyWinEvent)
    return gmNotifyWinEvent(event, hwnd, idObjectType, idObject);

  return E_FAIL;
}

STDMETHODIMP nsAccessibleWrap::get_accParent( IDispatch __RPC_FAR *__RPC_FAR *ppdispParent)
{
__try {
  *ppdispParent = NULL;
  if (!mWeakShell)
    return E_FAIL;  // We've been shut down

  nsIFrame *frame = GetFrame();
  HWND hwnd = 0;
  if (frame) {
    nsIView *view = frame->GetViewExternal();
    if (view) {
      // This code is essentially our implementation of WindowFromAccessibleObject,
      // because MSAA iterates get_accParent() until it sees an object of ROLE_WINDOW
      // to know where the window for a given accessible is. We must expose the native
      // window accessible that MSAA creates for us. This must be done for the document
      // object as well as any layout that creates its own window (e.g. via overflow: scroll)
      nsIWidget *widget = view->GetWidget();
      if (widget) {
        hwnd = (HWND)widget->GetNativeData(NS_NATIVE_WINDOW);
        NS_ASSERTION(hwnd, "No window handle for window");

        nsIViewManager* viewManager = view->GetViewManager();
        if (!viewManager)
          return E_UNEXPECTED;

        nsIView *rootView;
        viewManager->GetRootView(rootView);
        if (rootView == view) {
          // If the client accessible (OBJID_CLIENT) has a window but its window
          // was created by an outer window then we want the native accessible
          // for that outer window. If the accessible was created for outer
          // window (if the outer window has inner windows then they share the
          // same client accessible with it) then return native accessible for
          // the outer window.
          HWND parenthwnd = ::GetParent(hwnd);
          if (parenthwnd)
            hwnd = parenthwnd;

          NS_ASSERTION(hwnd, "No window handle for window");
        }
      }
      else {
        // If a frame is a scrollable frame, then it has one window for the client area,
        // not an extra parent window for just the scrollbars
        nsIScrollableFrame *scrollFrame = do_QueryFrame(frame);
        if (scrollFrame) {
          hwnd = (HWND)scrollFrame->GetScrolledFrame()->GetNearestWidget()->GetNativeData(NS_NATIVE_WINDOW);
          NS_ASSERTION(hwnd, "No window handle for window");
        }
      }
    }

    if (hwnd && SUCCEEDED(AccessibleObjectFromWindow(hwnd, OBJID_WINDOW, IID_IAccessible,
                                              (void**)ppdispParent))) {
      return S_OK;
    }
  }

  nsAccessible* xpParentAcc = GetParent();
  NS_ASSERTION(xpParentAcc,
               "No parent accessible where we're not direct child of window");

  if (!xpParentAcc)
    return E_UNEXPECTED;

  *ppdispParent = NativeAccessible(xpParentAcc);

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return S_OK;
}

STDMETHODIMP nsAccessibleWrap::get_accChildCount( long __RPC_FAR *pcountChildren)
{
__try {
  *pcountChildren = 0;
  if (nsAccUtils::MustPrune(this))
    return NS_OK;

  PRInt32 numChildren;
  GetChildCount(&numChildren);
  *pcountChildren = numChildren;
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP nsAccessibleWrap::get_accChild(
      /* [in] */ VARIANT varChild,
      /* [retval][out] */ IDispatch __RPC_FAR *__RPC_FAR *ppdispChild)
{
__try {
  *ppdispChild = NULL;
  if (!mWeakShell || varChild.vt != VT_I4)
    return E_FAIL;

  if (varChild.lVal == CHILDID_SELF) {
    *ppdispChild = static_cast<IDispatch*>(this);
    AddRef();
    return S_OK;
  }

  if (!nsAccUtils::MustPrune(this)) {
    nsAccessible* child = GetChildAt(varChild.lVal - 1);
    if (child) {
      *ppdispChild = NativeAccessible(child);
    }
  }
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return (*ppdispChild)? S_OK: E_FAIL;
}

STDMETHODIMP nsAccessibleWrap::get_accName(
      /* [optional][in] */ VARIANT varChild,
      /* [retval][out] */ BSTR __RPC_FAR *pszName)
{
__try {
  *pszName = NULL;
  nsAccessible *xpAccessible = GetXPAccessibleFor(varChild);
  if (!xpAccessible)
    return E_FAIL;
  nsAutoString name;
  nsresult rv = xpAccessible->GetName(name);
  if (NS_FAILED(rv))
    return GetHRESULT(rv);
    
  if (name.IsVoid()) {
    // Valid return value for the name:
    // The name was not provided, e.g. no alt attribute for an image.
    // A screen reader may choose to invent its own accessible name, e.g. from
    // an image src attribute.
    // See nsHTMLImageAccessible::GetName()
    return S_OK;
  }

  *pszName = ::SysAllocStringLen(name.get(), name.Length());
  if (!*pszName)
    return E_OUTOFMEMORY;

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}


STDMETHODIMP nsAccessibleWrap::get_accValue(
      /* [optional][in] */ VARIANT varChild,
      /* [retval][out] */ BSTR __RPC_FAR *pszValue)
{
__try {
  *pszValue = NULL;
  nsAccessible *xpAccessible = GetXPAccessibleFor(varChild);
  if (xpAccessible) {
    nsAutoString value;
    if (NS_FAILED(xpAccessible->GetValue(value)))
      return E_FAIL;

    // see bug 438784: Need to expose URL on doc's value attribute.
    // For this, reverting part of fix for bug 425693 to make this MSAA method 
    // behave IAccessible2-style.
    if (value.IsEmpty())
      return S_FALSE;

    *pszValue = ::SysAllocStringLen(value.get(), value.Length());
    if (!*pszValue)
      return E_OUTOFMEMORY;
  }
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return S_OK;
}

STDMETHODIMP
nsAccessibleWrap::get_accDescription(VARIANT varChild,
                                     BSTR __RPC_FAR *pszDescription)
{
__try {
  *pszDescription = NULL;

  nsAccessible *xpAccessible = GetXPAccessibleFor(varChild);
  if (!xpAccessible)
    return E_FAIL;

  nsAutoString description;
  xpAccessible->GetDescription(description);

  *pszDescription = ::SysAllocStringLen(description.get(),
                                        description.Length());
  return *pszDescription ? S_OK : E_OUTOFMEMORY;

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP nsAccessibleWrap::get_accRole(
      /* [optional][in] */ VARIANT varChild,
      /* [retval][out] */ VARIANT __RPC_FAR *pvarRole)
{
__try {
  VariantInit(pvarRole);

  nsAccessible *xpAccessible = GetXPAccessibleFor(varChild);
  if (!xpAccessible)
    return E_FAIL;

#ifdef DEBUG_A11Y
  NS_ASSERTION(nsAccUtils::IsTextInterfaceSupportCorrect(xpAccessible),
               "Does not support nsIAccessibleText when it should");
#endif

  PRUint32 xpRole = 0, msaaRole = 0;
  if (NS_FAILED(xpAccessible->GetRole(&xpRole)))
    return E_FAIL;

  msaaRole = gWindowsRoleMap[xpRole].msaaRole;
  NS_ASSERTION(gWindowsRoleMap[nsIAccessibleRole::ROLE_LAST_ENTRY].msaaRole == ROLE_WINDOWS_LAST_ENTRY,
               "MSAA role map skewed");

  // Special case, if there is a ROLE_ROW inside of a ROLE_TREE_TABLE, then call the MSAA role
  // a ROLE_OUTLINEITEM for consistency and compatibility.
  // We need this because ARIA has a role of "row" for both grid and treegrid
  if (xpRole == nsIAccessibleRole::ROLE_ROW) {
    if (nsAccUtils::Role(GetParent()) == nsIAccessibleRole::ROLE_TREE_TABLE)
      msaaRole = ROLE_SYSTEM_OUTLINEITEM;
  }
  
  // -- Try enumerated role
  if (msaaRole != USE_ROLE_STRING) {
    pvarRole->vt = VT_I4;
    pvarRole->lVal = msaaRole;  // Normal enumerated role
    return S_OK;
  }

  // -- Try BSTR role
  // Could not map to known enumerated MSAA role like ROLE_BUTTON
  // Use BSTR role to expose role attribute or tag name + namespace
  nsIContent *content = xpAccessible->GetContent();
  if (!content)
    return E_FAIL;

  if (content->IsElement()) {
    nsAutoString roleString;
    if (msaaRole != ROLE_SYSTEM_CLIENT &&
        !content->GetAttr(kNameSpaceID_None, nsAccessibilityAtoms::role, roleString)) {
      nsIDocument * document = content->GetCurrentDoc();
      if (!document)
        return E_FAIL;

      nsINodeInfo *nodeInfo = content->NodeInfo();
      nodeInfo->GetName(roleString);

      // Only append name space if different from that of current document.
      if (!nodeInfo->NamespaceEquals(document->GetDefaultNamespaceID())) {
        nsAutoString nameSpaceURI;
        nodeInfo->GetNamespaceURI(nameSpaceURI);
        roleString += NS_LITERAL_STRING(", ") + nameSpaceURI;
      }
    }

    if (!roleString.IsEmpty()) {
      pvarRole->vt = VT_BSTR;
      pvarRole->bstrVal = ::SysAllocString(roleString.get());
      return S_OK;
    }
  }
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP nsAccessibleWrap::get_accState(
      /* [optional][in] */ VARIANT varChild,
      /* [retval][out] */ VARIANT __RPC_FAR *pvarState)
{
__try {
  VariantInit(pvarState);
  pvarState->vt = VT_I4;
  pvarState->lVal = 0;

  nsAccessible *xpAccessible = GetXPAccessibleFor(varChild);
  if (!xpAccessible)
    return E_FAIL;

  PRUint32 state = 0;
  if (NS_FAILED(xpAccessible->GetState(&state, nsnull)))
    return E_FAIL;

  pvarState->lVal = state;
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return S_OK;
}


STDMETHODIMP nsAccessibleWrap::get_accHelp(
      /* [optional][in] */ VARIANT varChild,
      /* [retval][out] */ BSTR __RPC_FAR *pszHelp)
{
__try {
  *pszHelp = NULL;
  return S_FALSE;

} __except(FilterA11yExceptions(::GetExceptionCode(),
                                GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP nsAccessibleWrap::get_accHelpTopic(
      /* [out] */ BSTR __RPC_FAR *pszHelpFile,
      /* [optional][in] */ VARIANT varChild,
      /* [retval][out] */ long __RPC_FAR *pidTopic)
{
__try {
  *pszHelpFile = NULL;
  *pidTopic = 0;
  return S_FALSE;

} __except(FilterA11yExceptions(::GetExceptionCode(),
                                GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP nsAccessibleWrap::get_accKeyboardShortcut(
      /* [optional][in] */ VARIANT varChild,
      /* [retval][out] */ BSTR __RPC_FAR *pszKeyboardShortcut)
{
__try {
  *pszKeyboardShortcut = NULL;
  nsAccessible *xpAccessible = GetXPAccessibleFor(varChild);
  if (xpAccessible) {
    nsAutoString shortcut;
    nsresult rv = xpAccessible->GetKeyboardShortcut(shortcut);
    if (NS_FAILED(rv))
      return E_FAIL;

    *pszKeyboardShortcut = ::SysAllocStringLen(shortcut.get(),
                                               shortcut.Length());
    return *pszKeyboardShortcut ? S_OK : E_OUTOFMEMORY;
  }
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP nsAccessibleWrap::get_accFocus(
      /* [retval][out] */ VARIANT __RPC_FAR *pvarChild)
{
  // VT_EMPTY:    None. This object does not have the keyboard focus itself
  //              and does not contain a child that has the keyboard focus.
  // VT_I4:       lVal is CHILDID_SELF. The object itself has the keyboard focus.
  // VT_I4:       lVal contains the child ID of the child element with the keyboard focus.
  // VT_DISPATCH: pdispVal member is the address of the IDispatch interface
  //              for the child object with the keyboard focus.
__try {
  if (IsDefunct())
    return E_FAIL;

  VariantInit(pvarChild);

  // Return the current IAccessible child that has focus
  nsCOMPtr<nsIAccessible> focusedAccessible;
  GetFocusedChild(getter_AddRefs(focusedAccessible));
  if (focusedAccessible == this) {
    pvarChild->vt = VT_I4;
    pvarChild->lVal = CHILDID_SELF;
  }
  else if (focusedAccessible) {
    pvarChild->vt = VT_DISPATCH;
    pvarChild->pdispVal = NativeAccessible(focusedAccessible);
  }
  else {
    pvarChild->vt = VT_EMPTY;   // No focus or focus is not a child
  }

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return S_OK;
}

// This helper class implements IEnumVARIANT for a nsIArray containing nsIAccessible objects.

class AccessibleEnumerator : public IEnumVARIANT
{
public:
  AccessibleEnumerator(nsIArray* aArray) : mArray(aArray), mCurIndex(0) { }
  AccessibleEnumerator(const AccessibleEnumerator& toCopy) :
    mArray(toCopy.mArray), mCurIndex(toCopy.mCurIndex) { }
  ~AccessibleEnumerator() { }

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID iid, void ** ppvObject);
  STDMETHODIMP_(ULONG) AddRef(void);
  STDMETHODIMP_(ULONG) Release(void);

  // IEnumVARIANT
  STDMETHODIMP Next(unsigned long celt, VARIANT FAR* rgvar, unsigned long FAR* pceltFetched);
  STDMETHODIMP Skip(unsigned long celt);
  STDMETHODIMP Reset()
  {
    mCurIndex = 0;
    return S_OK;
  }
  STDMETHODIMP Clone(IEnumVARIANT FAR* FAR* ppenum);

private:
  nsCOMPtr<nsIArray> mArray;
  PRUint32 mCurIndex;
  nsAutoRefCnt mRefCnt;
};

HRESULT
AccessibleEnumerator::QueryInterface(REFIID iid, void ** ppvObject)
{
__try {
  if (iid == IID_IEnumVARIANT) {
    *ppvObject = static_cast<IEnumVARIANT*>(this);
    AddRef();
    return S_OK;
  }
  if (iid == IID_IUnknown) {
    *ppvObject = static_cast<IUnknown*>(this);
    AddRef();
    return S_OK;
  }

  *ppvObject = NULL;
} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
AccessibleEnumerator::AddRef(void)
{
  return ++mRefCnt;
}

STDMETHODIMP_(ULONG)
AccessibleEnumerator::Release(void)
{
  ULONG r = --mRefCnt;
  if (r == 0)
    delete this;
  return r;
}

STDMETHODIMP
AccessibleEnumerator::Next(unsigned long celt, VARIANT FAR* rgvar, unsigned long FAR* pceltFetched)
{
__try {
  PRUint32 length = 0;
  mArray->GetLength(&length);

  HRESULT hr = S_OK;

  // Can't get more elements than there are...
  if (celt > length - mCurIndex) {
    hr = S_FALSE;
    celt = length - mCurIndex;
  }

  for (PRUint32 i = 0; i < celt; ++i, ++mCurIndex) {
    // Copy the elements of the array into rgvar
    nsCOMPtr<nsIAccessible> accel(do_QueryElementAt(mArray, mCurIndex));
    NS_ASSERTION(accel, "Invalid pointer in mArray");

    if (accel) {
      rgvar[i].vt = VT_DISPATCH;
      rgvar[i].pdispVal = nsAccessibleWrap::NativeAccessible(accel);
    }
  }

  if (pceltFetched)
    *pceltFetched = celt;

  return hr;
} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP
AccessibleEnumerator::Clone(IEnumVARIANT FAR* FAR* ppenum)
{
__try {
  *ppenum = new AccessibleEnumerator(*this);
  if (!*ppenum)
    return E_OUTOFMEMORY;
  NS_ADDREF(*ppenum);
} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return S_OK;
}

STDMETHODIMP
AccessibleEnumerator::Skip(unsigned long celt)
{
__try {
  PRUint32 length = 0;
  mArray->GetLength(&length);
  // Check if we can skip the requested number of elements
  if (celt > length - mCurIndex) {
    mCurIndex = length;
    return S_FALSE;
  }
  mCurIndex += celt;
} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return S_OK;
}

/**
  * This method is called when a client wants to know which children of a node
  *  are selected. Note that this method can only find selected children for
  *  nsIAccessible object which implement nsIAccessibleSelectable.
  *
  * The VARIANT return value arguement is expected to either contain a single IAccessible
  *  or an IEnumVARIANT of IAccessibles. We return the IEnumVARIANT regardless of the number
  *  of children selected, unless there are none selected in which case we return an empty
  *  VARIANT.
  *
  * We get the selected options from the select's accessible object and wrap
  *  those in an AccessibleEnumerator which we then put in the return VARIANT.
  *
  * returns a VT_EMPTY VARIANT if:
  *  - there are no selected children for this object
  *  - the object is not the type that can have children selected
  */
STDMETHODIMP nsAccessibleWrap::get_accSelection(VARIANT __RPC_FAR *pvarChildren)
{
__try {
  VariantInit(pvarChildren);
  pvarChildren->vt = VT_EMPTY;

  nsCOMPtr<nsIAccessibleSelectable> 
    select(do_QueryInterface(static_cast<nsIAccessible*>(this)));

  if (select) {  // do we have an nsIAccessibleSelectable?
    // we have an accessible that can have children selected
    nsCOMPtr<nsIArray> selectedOptions;
    // gets the selected options as nsIAccessibles.
    select->GetSelectedChildren(getter_AddRefs(selectedOptions));
    if (selectedOptions) { // false if the select has no children or none are selected
      // 1) Create and initialize the enumeration
      nsRefPtr<AccessibleEnumerator> pEnum = new AccessibleEnumerator(selectedOptions);

      // 2) Put the enumerator in the VARIANT
      if (!pEnum)
        return E_OUTOFMEMORY;
      pvarChildren->vt = VT_UNKNOWN;    // this must be VT_UNKNOWN for an IEnumVARIANT
      NS_ADDREF(pvarChildren->punkVal = pEnum);
    }
  }
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return S_OK;
}

STDMETHODIMP nsAccessibleWrap::get_accDefaultAction(
      /* [optional][in] */ VARIANT varChild,
      /* [retval][out] */ BSTR __RPC_FAR *pszDefaultAction)
{
__try {
  *pszDefaultAction = NULL;
  nsAccessible *xpAccessible = GetXPAccessibleFor(varChild);
  if (xpAccessible) {
    nsAutoString defaultAction;
    if (NS_FAILED(xpAccessible->GetActionName(0, defaultAction)))
      return E_FAIL;

    *pszDefaultAction = ::SysAllocStringLen(defaultAction.get(),
                                            defaultAction.Length());
    return *pszDefaultAction ? S_OK : E_OUTOFMEMORY;
  }

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP nsAccessibleWrap::accSelect(
      /* [in] */ long flagsSelect,
      /* [optional][in] */ VARIANT varChild)
{
__try {
  // currently only handle focus and selection
  nsAccessible *xpAccessible = GetXPAccessibleFor(varChild);
  NS_ENSURE_TRUE(xpAccessible, E_FAIL);

  if (flagsSelect & (SELFLAG_TAKEFOCUS|SELFLAG_TAKESELECTION|SELFLAG_REMOVESELECTION))
  {
    if (flagsSelect & SELFLAG_TAKEFOCUS)
      xpAccessible->TakeFocus();

    if (flagsSelect & SELFLAG_TAKESELECTION)
      xpAccessible->TakeSelection();

    if (flagsSelect & SELFLAG_ADDSELECTION)
      xpAccessible->SetSelected(PR_TRUE);

    if (flagsSelect & SELFLAG_REMOVESELECTION)
      xpAccessible->SetSelected(PR_FALSE);

    if (flagsSelect & SELFLAG_EXTENDSELECTION)
      xpAccessible->ExtendSelection();

    return S_OK;
  }

} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP nsAccessibleWrap::accLocation(
      /* [out] */ long __RPC_FAR *pxLeft,
      /* [out] */ long __RPC_FAR *pyTop,
      /* [out] */ long __RPC_FAR *pcxWidth,
      /* [out] */ long __RPC_FAR *pcyHeight,
      /* [optional][in] */ VARIANT varChild)
{
__try {
  nsAccessible *xpAccessible = GetXPAccessibleFor(varChild);

  if (xpAccessible) {
    PRInt32 x, y, width, height;
    if (NS_FAILED(xpAccessible->GetBounds(&x, &y, &width, &height)))
      return E_FAIL;

    *pxLeft = x;
    *pyTop = y;
    *pcxWidth = width;
    *pcyHeight = height;
    return S_OK;
  }
} __except(FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return E_FAIL;
}

STDMETHODIMP nsAccessibleWrap::accNavigate(
      /* [in] */ long navDir,
      /* [optional][in] */ VARIANT varStart,
      /* [retval][out] */ VARIANT __RPC_FAR *pvarEndUpAt)
{
__try {
  nsAccessible *xpAccessibleStart = GetXPAccessibleFor(varStart);
  if (!xpAccessibleStart)
    return E_FAIL;

  VariantInit(pvarEndUpAt);

  nsCOMPtr<nsIAccessible> xpAccessibleResult;
  PRUint32 xpRelation = 0;

  switch(navDir) {
    case NAVDIR_DOWN:
      xpAccessibleStart->GetAccessibleBelow(getter_AddRefs(xpAccessibleResult));
      break;
    case NAVDIR_FIRSTCHILD:
      if (!nsAccUtils::MustPrune(xpAccessibleStart))
        xpAccessibleStart->GetFirstChild(getter_AddRefs(xpAccessibleResult));
      break;
    case NAVDIR_LASTCHILD:
      if (!nsAccUtils::MustPrune(xpAccessibleStart))
        xpAccessibleStart->GetLastChild(getter_AddRefs(xpAccessibleResult));
      break;
    case NAVDIR_LEFT:
      xpAccessibleStart->GetAccessibleToLeft(getter_AddRefs(xpAccessibleResult));
      break;
    case NAVDIR_NEXT:
      xpAccessibleStart->GetNextSibling(getter_AddRefs(xpAccessibleResult));
      break;
    case NAVDIR_PREVIOUS:
      xpAccessibleStart->GetPreviousSibling(getter_AddRefs(xpAccessibleResult));
      break;
    case NAVDIR_RIGHT:
      xpAccessibleStart->GetAccessibleToRight(getter_AddRefs(xpAccessibleResult));
      break;
    case NAVDIR_UP:
      xpAccessibleStart->GetAccessibleAbove(getter_AddRefs(xpAccessibleResult));
      break;

    // MSAA relationship extensions to accNavigate
    case NAVRELATION_CONTROLLED_BY:
      xpRelation = nsIAccessibleRelation::RELATION_CONTROLLED_BY;
      break;
    case NAVRELATION_CONTROLLER_FOR:
      xpRelation = nsIAccessibleRelation::RELATION_CONTROLLER_FOR;
      break;
    case NAVRELATION_LABEL_FOR:
      xpRelation = nsIAccessibleRelation::RELATION_LABEL_FOR;
      break;
    case NAVRELATION_LABELLED_BY:
      xpRelation = nsIAccessibleRelation::RELATION_LABELLED_BY;
      break;
    case NAVRELATION_MEMBER_OF:
      xpRelation = nsIAccessibleRelation::RELATION_MEMBER_OF;
      break;
    case NAVRELATION_NODE_CHILD_OF:
      xpRelation = nsIAccessibleRelation::RELATION_NODE_CHILD_OF;
      break;
    case NAVRELATION_FLOWS_TO:
      xpRelation = nsIAccessibleRelation::RELATION_FLOWS_TO;
      break;
    case NAVRELATION_FLOWS_FROM:
      xpRelation = nsIAccessibleRelation::RELATION_FLOWS_FROM;
      break;
    case NAVRELATION_SUBWINDOW_OF:
      xpRelation = nsIAccessibleRelation::RELATION_SUBWINDOW_OF;
      break;
    case NAVRELATION_EMBEDS:
      xpRelation = nsIAccessibleRelation::RELATION_EMBEDS;
      break;
    case NAVRELATION_EMBEDDED_BY:
      xpRelation = nsIAccessibleRelation::RELATION_EMBEDDED_BY;
      break;
    case NAVRELATION_POPUP_FOR:
      xpRelation = nsIAccessibleRelation::RELATION_POPUP_FOR;
      break;
    case NAVRELATION_PARENT_WINDOW_OF:
      xpRelation = nsIAccessibleRelation::RELATION_PARENT_WINDOW_OF;
      break;
    case NAVRELATION_DEFAULT_BUTTON:
      xpRelation = nsIAccessibleRelation::RELATION_DEFAULT_BUTTON;
      break;
    case NAVRELATION_DESCRIBED_BY:
      xpRelation = nsIAccessibleRelation::RELATION_DESCRIBED_BY;
      break;
    case NAVRELATION_DESCRIPTION_FOR:
      xpRelation = nsIAccessibleRelation::RELATION_DESCRIPTION_FOR;
      break;
  }

  pvarEndUpAt->vt = VT_EMPTY;

  if (xpRelation)
    xpAccessibleResult = nsRelUtils::GetRelatedAccessible(this, xpRelation);

  if (xpAccessibleResult) {
    pvarEndUpAt->pdispVal = NativeAccessible(xpAccessibleResult);
    pvarEndUpAt->vt = VT_DISPATCH;
    return NS_OK;
  }
} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP nsAccessibleWrap::accHitTest(
      /* [in] */ long xLeft,
      /* [in] */ long yTop,
      /* [retval][out] */ VARIANT __RPC_FAR *pvarChild)
{
__try {
  VariantInit(pvarChild);

  // convert to window coords
  nsCOMPtr<nsIAccessible> xpAccessible;

  xLeft = xLeft;
  yTop = yTop;

  if (nsAccUtils::MustPrune(this)) {
    xpAccessible = this;
  }
  else {
    GetChildAtPoint(xLeft, yTop, getter_AddRefs(xpAccessible));
  }

  // if we got a child
  if (xpAccessible) {
    // if the child is us
    if (xpAccessible == static_cast<nsIAccessible*>(this)) {
      pvarChild->vt = VT_I4;
      pvarChild->lVal = CHILDID_SELF;
    } else { // its not create an Accessible for it.
      pvarChild->vt = VT_DISPATCH;
      pvarChild->pdispVal = NativeAccessible(xpAccessible);
      nsCOMPtr<nsIAccessNode> accessNode(do_QueryInterface(xpAccessible));
      NS_ASSERTION(accessNode, "Unable to QI to nsIAccessNode");
      nsCOMPtr<nsIDOMNode> domNode;
      accessNode->GetDOMNode(getter_AddRefs(domNode));
      if (!domNode) {
        // Has already been shut down
        pvarChild->vt = VT_EMPTY;
        return E_FAIL;
      }
    }
  } else {
    // no child at that point
    pvarChild->vt = VT_EMPTY;
    return S_FALSE;
  }
} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return S_OK;
}

STDMETHODIMP nsAccessibleWrap::accDoDefaultAction(
      /* [optional][in] */ VARIANT varChild)
{
__try {
  nsAccessible *xpAccessible = GetXPAccessibleFor(varChild);
  if (!xpAccessible || FAILED(xpAccessible->DoAction(0))) {
    return E_FAIL;
  }
} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return S_OK;
}

STDMETHODIMP nsAccessibleWrap::put_accName(
      /* [optional][in] */ VARIANT varChild,
      /* [in] */ BSTR szName)
{
  return E_NOTIMPL;
}

STDMETHODIMP nsAccessibleWrap::put_accValue(
      /* [optional][in] */ VARIANT varChild,
      /* [in] */ BSTR szValue)
{
  return E_NOTIMPL;
}

#include "mshtml.h"

////////////////////////////////////////////////////////////////////////////////
// nsAccessibleWrap. IEnumVariant

STDMETHODIMP
nsAccessibleWrap::Next(ULONG aNumElementsRequested, VARIANT FAR* aPVar,
                       ULONG FAR* aNumElementsFetched)
{
  // Children already cached via QI to IEnumVARIANT
__try {
  *aNumElementsFetched = 0;

  if (aNumElementsRequested <= 0 || !aPVar)
    return E_INVALIDARG;

  if (mEnumVARIANTPosition == kIEnumVariantDisconnected)
    return CO_E_OBJNOTCONNECTED;

  PRUint32 numElementsFetched = 0;
  for (; numElementsFetched < aNumElementsRequested;
       numElementsFetched++, mEnumVARIANTPosition++) {

    nsAccessible* accessible = GetChildAt(mEnumVARIANTPosition);
    if (!accessible)
      break;

    VariantInit(&aPVar[numElementsFetched]);

    aPVar[numElementsFetched].pdispVal = NativeAccessible(accessible);
    aPVar[numElementsFetched].vt = VT_DISPATCH;
  }

  (*aNumElementsFetched) = numElementsFetched;

  return numElementsFetched < aNumElementsRequested ? S_FALSE : S_OK;

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
nsAccessibleWrap::Skip(ULONG aNumElements)
{
__try {
  if (mEnumVARIANTPosition == kIEnumVariantDisconnected)
    return CO_E_OBJNOTCONNECTED;

  mEnumVARIANTPosition += aNumElements;

  PRInt32 numChildren;
  GetChildCount(&numChildren);

  if (mEnumVARIANTPosition > numChildren)
  {
    mEnumVARIANTPosition = numChildren;
    return S_FALSE;
  }
} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return NOERROR;
}

STDMETHODIMP
nsAccessibleWrap::Reset(void)
{
  mEnumVARIANTPosition = 0;
  return NOERROR;
}

STDMETHODIMP
nsAccessibleWrap::Clone(IEnumVARIANT FAR* FAR* ppenum)
{
__try {
  *ppenum = nsnull;
  
  nsCOMPtr<nsIArray> childArray;
  nsresult rv = GetChildren(getter_AddRefs(childArray));

  *ppenum = new AccessibleEnumerator(childArray);
  if (!*ppenum)
    return E_OUTOFMEMORY;
  NS_ADDREF(*ppenum);

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return NOERROR;
}

////////////////////////////////////////////////////////////////////////////////
// nsAccessibleWrap. IAccessible2

STDMETHODIMP
nsAccessibleWrap::get_nRelations(long *aNRelations)
{
__try {
  PRUint32 count = 0;
  nsresult rv = GetRelationsCount(&count);
  *aNRelations = count;

  return GetHRESULT(rv);

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
nsAccessibleWrap::get_relation(long aRelationIndex,
                               IAccessibleRelation **aRelation)
{
__try {
  *aRelation = NULL;

  nsCOMPtr<nsIAccessibleRelation> relation;
  nsresult rv = GetRelation(aRelationIndex, getter_AddRefs(relation));
  if (NS_FAILED(rv))
    return GetHRESULT(rv);

  nsCOMPtr<nsIWinAccessNode> winAccessNode(do_QueryInterface(relation));
  if (!winAccessNode)
    return E_FAIL;

  void *instancePtr = NULL;
  rv =  winAccessNode->QueryNativeInterface(IID_IAccessibleRelation,
                                            &instancePtr);
  if (NS_FAILED(rv))
    return GetHRESULT(rv);

  *aRelation = static_cast<IAccessibleRelation*>(instancePtr);
  return S_OK;

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
nsAccessibleWrap::get_relations(long aMaxRelations,
                                IAccessibleRelation **aRelation,
                                long *aNRelations)
{
__try {
  *aRelation = NULL;
  *aNRelations = 0;

  nsCOMPtr<nsIArray> relations;
  nsresult rv = GetRelations(getter_AddRefs(relations));
  if (NS_FAILED(rv))
    return GetHRESULT(rv);

  PRUint32 length = 0;
  rv = relations->GetLength(&length);
  if (NS_FAILED(rv))
    return GetHRESULT(rv);

  if (length == 0)
    return S_FALSE;

  PRUint32 count = length < (PRUint32)aMaxRelations ? length : aMaxRelations;

  PRUint32 index = 0;
  for (; index < count; index++) {
    nsCOMPtr<nsIWinAccessNode> winAccessNode =
      do_QueryElementAt(relations, index, &rv);
    if (NS_FAILED(rv))
      break;

    void *instancePtr = NULL;
    nsresult rv =  winAccessNode->QueryNativeInterface(IID_IAccessibleRelation,
                                                       &instancePtr);
    if (NS_FAILED(rv))
      break;

    aRelation[index] = static_cast<IAccessibleRelation*>(instancePtr);
  }

  if (NS_FAILED(rv)) {
    for (PRUint32 index2 = 0; index2 < index; index2++) {
      aRelation[index2]->Release();
      aRelation[index2] = NULL;
    }
    return GetHRESULT(rv);
  }

  *aNRelations = count;
  return S_OK;

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
nsAccessibleWrap::role(long *aRole)
{
__try {
  *aRole = 0;

  PRUint32 xpRole = 0;
  nsresult rv = GetRole(&xpRole);
  if (NS_FAILED(rv))
    return GetHRESULT(rv);

  NS_ASSERTION(gWindowsRoleMap[nsIAccessibleRole::ROLE_LAST_ENTRY].ia2Role == ROLE_WINDOWS_LAST_ENTRY,
               "MSAA role map skewed");

  *aRole = gWindowsRoleMap[xpRole].ia2Role;

  // Special case, if there is a ROLE_ROW inside of a ROLE_TREE_TABLE, then call
  // the IA2 role a ROLE_OUTLINEITEM.
  if (xpRole == nsIAccessibleRole::ROLE_ROW) {
    if (nsAccUtils::Role(GetParent()) == nsIAccessibleRole::ROLE_TREE_TABLE)
      *aRole = ROLE_SYSTEM_OUTLINEITEM;
  }

  return S_OK;

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
nsAccessibleWrap::scrollTo(enum IA2ScrollType aScrollType)
{
__try {
  nsresult rv = ScrollTo(aScrollType);
  return GetHRESULT(rv);

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
nsAccessibleWrap::scrollToPoint(enum IA2CoordinateType aCoordType,
                                long aX, long aY)
{
__try {
  PRUint32 geckoCoordType = (aCoordType == IA2_COORDTYPE_SCREEN_RELATIVE) ?
    nsIAccessibleCoordinateType::COORDTYPE_SCREEN_RELATIVE :
    nsIAccessibleCoordinateType::COORDTYPE_PARENT_RELATIVE;

  nsresult rv = ScrollToPoint(geckoCoordType, aX, aY);
  return GetHRESULT(rv);

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
nsAccessibleWrap::get_groupPosition(long *aGroupLevel,
                                    long *aSimilarItemsInGroup,
                                    long *aPositionInGroup)
{
__try {
  PRInt32 groupLevel = 0;
  PRInt32 similarItemsInGroup = 0;
  PRInt32 positionInGroup = 0;

  nsresult rv = GroupPosition(&groupLevel, &similarItemsInGroup,
                              &positionInGroup);
  if (NS_FAILED(rv))
    return GetHRESULT(rv);

  // Group information for accessibles having level only (like html headings
  // elements) isn't exposed by this method. AT should look for 'level' object
  // attribute.
  if (!similarItemsInGroup && !positionInGroup)
    return S_FALSE;

  *aGroupLevel = groupLevel;
  *aSimilarItemsInGroup = similarItemsInGroup;
  *aPositionInGroup = positionInGroup;

  return S_OK;

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
nsAccessibleWrap::get_states(AccessibleStates *aStates)
{
__try {
  *aStates = 0;

  // XXX: bug 344674 should come with better approach that we have here.

  PRUint32 states = 0, extraStates = 0;
  nsresult rv = GetState(&states, &extraStates);
  if (NS_FAILED(rv))
    return GetHRESULT(rv);

  if (states & nsIAccessibleStates::STATE_INVALID)
    *aStates |= IA2_STATE_INVALID_ENTRY;
  if (states & nsIAccessibleStates::STATE_REQUIRED)
    *aStates |= IA2_STATE_REQUIRED;

  // The following IA2 states are not supported by Gecko
  // IA2_STATE_ARMED
  // IA2_STATE_MANAGES_DESCENDANTS
  // IA2_STATE_ICONIFIED
  // IA2_STATE_INVALID // This is not a state, it is the absence of a state

  if (extraStates & nsIAccessibleStates::EXT_STATE_ACTIVE)
    *aStates |= IA2_STATE_ACTIVE;
  if (extraStates & nsIAccessibleStates::EXT_STATE_DEFUNCT)
    *aStates |= IA2_STATE_DEFUNCT;
  if (extraStates & nsIAccessibleStates::EXT_STATE_EDITABLE)
    *aStates |= IA2_STATE_EDITABLE;
  if (extraStates & nsIAccessibleStates::EXT_STATE_HORIZONTAL)
    *aStates |= IA2_STATE_HORIZONTAL;
  if (extraStates & nsIAccessibleStates::EXT_STATE_MODAL)
    *aStates |= IA2_STATE_MODAL;
  if (extraStates & nsIAccessibleStates::EXT_STATE_MULTI_LINE)
    *aStates |= IA2_STATE_MULTI_LINE;
  if (extraStates & nsIAccessibleStates::EXT_STATE_OPAQUE)
    *aStates |= IA2_STATE_OPAQUE;
  if (extraStates & nsIAccessibleStates::EXT_STATE_SELECTABLE_TEXT)
    *aStates |= IA2_STATE_SELECTABLE_TEXT;
  if (extraStates & nsIAccessibleStates::EXT_STATE_SINGLE_LINE)
    *aStates |= IA2_STATE_SINGLE_LINE;
  if (extraStates & nsIAccessibleStates::EXT_STATE_STALE)
    *aStates |= IA2_STATE_STALE;
  if (extraStates & nsIAccessibleStates::EXT_STATE_SUPPORTS_AUTOCOMPLETION)
    *aStates |= IA2_STATE_SUPPORTS_AUTOCOMPLETION;
  if (extraStates & nsIAccessibleStates::EXT_STATE_TRANSIENT)
    *aStates |= IA2_STATE_TRANSIENT;
  if (extraStates & nsIAccessibleStates::EXT_STATE_VERTICAL)
    *aStates |= IA2_STATE_VERTICAL;

  return S_OK;

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
nsAccessibleWrap::get_extendedRole(BSTR *aExtendedRole)
{
__try {
  *aExtendedRole = NULL;
} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return E_NOTIMPL;
}

STDMETHODIMP
nsAccessibleWrap::get_localizedExtendedRole(BSTR *aLocalizedExtendedRole)
{
__try {
  *aLocalizedExtendedRole = NULL;
} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return E_NOTIMPL;
}

STDMETHODIMP
nsAccessibleWrap::get_nExtendedStates(long *aNExtendedStates)
{
__try {
  *aNExtendedStates = 0;
} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return E_NOTIMPL;
}

STDMETHODIMP
nsAccessibleWrap::get_extendedStates(long aMaxExtendedStates,
                                     BSTR **aExtendedStates,
                                     long *aNExtendedStates)
{
__try {
  *aExtendedStates = NULL;
  *aNExtendedStates = 0;
} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return E_NOTIMPL;
}

STDMETHODIMP
nsAccessibleWrap::get_localizedExtendedStates(long aMaxLocalizedExtendedStates,
                                              BSTR **aLocalizedExtendedStates,
                                              long *aNLocalizedExtendedStates)
{
__try {
  *aLocalizedExtendedStates = NULL;
  *aNLocalizedExtendedStates = 0;
} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }

  return E_NOTIMPL;
}

STDMETHODIMP
nsAccessibleWrap::get_uniqueID(long *uniqueID)
{
__try {
  void *id = nsnull;
  nsresult rv = GetUniqueID(&id);
  if (NS_FAILED(rv))
    return GetHRESULT(rv);

  *uniqueID = - reinterpret_cast<long>(id);
  return S_OK;

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
nsAccessibleWrap::get_windowHandle(HWND *aWindowHandle)
{
__try {
  *aWindowHandle = 0;

  if (IsDefunct())
    return E_FAIL;

  void *handle = nsnull;
  nsresult rv = GetOwnerWindow(&handle);
  if (NS_FAILED(rv))
    return GetHRESULT(rv);

  *aWindowHandle = reinterpret_cast<HWND>(handle);
  return S_OK;

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
nsAccessibleWrap::get_indexInParent(long *aIndexInParent)
{
__try {
  *aIndexInParent = -1;

  PRInt32 index = -1;
  nsresult rv = GetIndexInParent(&index);
  if (NS_FAILED(rv))
    return GetHRESULT(rv);

  if (index == -1)
    return S_FALSE;

  *aIndexInParent = index;
  return S_OK;

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
nsAccessibleWrap::get_locale(IA2Locale *aLocale)
{
__try {
  // Language codes consist of a primary code and a possibly empty series of
  // subcodes: language-code = primary-code ( "-" subcode )*
  // Two-letter primary codes are reserved for [ISO639] language abbreviations.
  // Any two-letter subcode is understood to be a [ISO3166] country code.

  nsAutoString lang;
  nsresult rv = GetLanguage(lang);
  if (NS_FAILED(rv))
    return GetHRESULT(rv);

  // If primary code consists from two letters then expose it as language.
  PRInt32 offset = lang.FindChar('-', 0);
  if (offset == -1) {
    if (lang.Length() == 2) {
      aLocale->language = ::SysAllocString(lang.get());
      return S_OK;
    }
  } else if (offset == 2) {
    aLocale->language = ::SysAllocStringLen(lang.get(), 2);

    // If the first subcode consists from two letters then expose it as
    // country.
    offset = lang.FindChar('-', 3);
    if (offset == -1) {
      if (lang.Length() == 5) {
        aLocale->country = ::SysAllocString(lang.get() + 3);
        return S_OK;
      }
    } else if (offset == 5) {
      aLocale->country = ::SysAllocStringLen(lang.get() + 3, 2);
    }
  }

  // Expose as a string if primary code or subcode cannot point to language or
  // country abbreviations or if there are more than one subcode.
  aLocale->variant = ::SysAllocString(lang.get());
  return S_OK;

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

STDMETHODIMP
nsAccessibleWrap::get_attributes(BSTR *aAttributes)
{
  // The format is name:value;name:value; with \ for escaping these
  // characters ":;=,\".
__try {
  *aAttributes = NULL;

  nsCOMPtr<nsIPersistentProperties> attributes;
  nsresult rv = GetAttributes(getter_AddRefs(attributes));
  if (NS_FAILED(rv))
    return GetHRESULT(rv);

  return ConvertToIA2Attributes(attributes, aAttributes);

} __except(nsAccessNodeWrap::FilterA11yExceptions(::GetExceptionCode(), GetExceptionInformation())) { }
  return E_FAIL;
}

////////////////////////////////////////////////////////////////////////////////
// IDispatch

STDMETHODIMP
nsAccessibleWrap::GetTypeInfoCount(UINT *pctinfo)
{
  *pctinfo = 1;
  return S_OK;
}

STDMETHODIMP
nsAccessibleWrap::GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
  *ppTInfo = NULL;

  if (iTInfo != 0)
    return DISP_E_BADINDEX;

  ITypeInfo * typeInfo = GetTI(lcid);
  if (!typeInfo)
    return E_FAIL;

  typeInfo->AddRef();
  *ppTInfo = typeInfo;

  return S_OK;
}

STDMETHODIMP
nsAccessibleWrap::GetIDsOfNames(REFIID riid, LPOLESTR *rgszNames,
                                UINT cNames, LCID lcid, DISPID *rgDispId)
{
  ITypeInfo *typeInfo = GetTI(lcid);
  if (!typeInfo)
    return E_FAIL;

  HRESULT hr = DispGetIDsOfNames(typeInfo, rgszNames, cNames, rgDispId);
  return hr;
}

STDMETHODIMP
nsAccessibleWrap::Invoke(DISPID dispIdMember, REFIID riid,
                         LCID lcid, WORD wFlags, DISPPARAMS *pDispParams,
                         VARIANT *pVarResult, EXCEPINFO *pExcepInfo,
                         UINT *puArgErr)
{
  ITypeInfo *typeInfo = GetTI(lcid);
  if (!typeInfo)
    return E_FAIL;

  return typeInfo->Invoke(static_cast<IAccessible*>(this), dispIdMember,
                          wFlags, pDispParams, pVarResult, pExcepInfo,
                          puArgErr);
}


// nsIAccessible method
NS_IMETHODIMP nsAccessibleWrap::GetNativeInterface(void **aOutAccessible)
{
  *aOutAccessible = static_cast<IAccessible*>(this);
  NS_ADDREF_THIS();
  return NS_OK;
}

// nsAccessible

nsresult
nsAccessibleWrap::HandleAccEvent(AccEvent* aEvent)
{
  nsresult rv = nsAccessible::HandleAccEvent(aEvent);
  NS_ENSURE_SUCCESS(rv, rv);

  return FirePlatformEvent(aEvent);
}

nsresult
nsAccessibleWrap::FirePlatformEvent(AccEvent* aEvent)
{
  PRUint32 eventType = aEvent->GetEventType();

  NS_ENSURE_TRUE(eventType > 0 &&
                 eventType < nsIAccessibleEvent::EVENT_LAST_ENTRY,
                 NS_ERROR_FAILURE);

  PRUint32 winLastEntry = gWinEventMap[nsIAccessibleEvent::EVENT_LAST_ENTRY];
  NS_ASSERTION(winLastEntry == kEVENT_LAST_ENTRY,
               "MSAA event map skewed");

  PRUint32 winEvent = gWinEventMap[eventType];
  if (!winEvent)
    return NS_OK;

  // Means we're not active.
  NS_ENSURE_TRUE(mWeakShell, NS_ERROR_FAILURE);

  nsAccessible *accessible = aEvent->GetAccessible();
  if (!accessible)
    return NS_OK;

  if (eventType == nsIAccessibleEvent::EVENT_TEXT_CARET_MOVED ||
      eventType == nsIAccessibleEvent::EVENT_FOCUS) {
    UpdateSystemCaret();
  }
 
  PRInt32 childID = GetChildIDFor(accessible); // get the id for the accessible
  if (!childID)
    return NS_OK; // Can't fire an event without a child ID

  // See if we're in a scrollable area with its own window
  nsAccessible *newAccessible = nsnull;
  if (eventType == nsIAccessibleEvent::EVENT_HIDE) {
    // Don't use frame from current accessible when we're hiding that
    // accessible.
    newAccessible = accessible->GetCachedParent();
  } else {
    newAccessible = accessible;
  }

  HWND hWnd = GetHWNDFor(newAccessible);
  NS_ENSURE_TRUE(hWnd, NS_ERROR_FAILURE);

  // Gecko uses two windows for every scrollable area. One window contains
  // scrollbars and the child window contains only the client area.
  // Details of the 2 window system:
  // * Scrollbar window: caret drawing window & return value for WindowFromAccessibleObject()
  // * Client area window: text drawing window & MSAA event window

  // Fire MSAA event for client area window.
  NotifyWinEvent(winEvent, hWnd, OBJID_CLIENT, childID);

  // If the accessible children are changed then drop the IEnumVariant current
  // position of the accessible.
  if (eventType == nsIAccessibleEvent::EVENT_REORDER)
    UnattachIEnumVariant();

  return NS_OK;
}

//------- Helper methods ---------

PRInt32 nsAccessibleWrap::GetChildIDFor(nsIAccessible* aAccessible)
{
  // A child ID of the window is required, when we use NotifyWinEvent,
  // so that the 3rd party application can call back and get the IAccessible
  // the event occurred on.

  void *uniqueID = nsnull;
  nsCOMPtr<nsIAccessNode> accessNode(do_QueryInterface(aAccessible));
  if (!accessNode) {
    return 0;
  }
  accessNode->GetUniqueID(&uniqueID);

  // Yes, this means we're only compatibible with 32 bit
  // MSAA is only available for 32 bit windows, so it's okay
  return - NS_PTR_TO_INT32(uniqueID);
}

HWND
nsAccessibleWrap::GetHWNDFor(nsAccessible *aAccessible)
{
  HWND hWnd = 0;
  if (!aAccessible)
    return hWnd;

  nsIFrame *frame = aAccessible->GetFrame();
  if (frame) {
    nsIWidget *window = frame->GetNearestWidget();
    PRBool isVisible;
    window->IsVisible(isVisible);
    if (isVisible) {
      // Short explanation:
      // If HWND for frame is inside a hidden window, fire the event on the
      // containing document's visible window.
      //
      // Long explanation:
      // This is really just to fix combo boxes with JAWS. Window-Eyes already
      // worked with combo boxes because they use the value change event in
      // the closed combo box case. JAWS will only pay attention to the focus
      // events on the list items. The JAWS developers haven't fixed that, so
      // we'll use the focus events to make JAWS work. However, JAWS is
      // ignoring events on a hidden window. So, in order to fix the bug where
      // JAWS doesn't echo the current option as it changes in a closed
      // combo box, we need to use an ensure that we never fire an event with
      // an HWND for a hidden window.
      hWnd = (HWND)frame->GetNearestWidget()->GetNativeData(NS_NATIVE_WINDOW);
    }
  }

  if (!hWnd) {
    void* handle = nsnull;
    nsDocAccessible *accessibleDoc = aAccessible->GetDocAccessible();
    if (!accessibleDoc)
      return 0;

    accessibleDoc->GetWindowHandle(&handle);
    hWnd = (HWND)handle;
  }

  return hWnd;
}

HRESULT
nsAccessibleWrap::ConvertToIA2Attributes(nsIPersistentProperties *aAttributes,
                                         BSTR *aIA2Attributes)
{
  *aIA2Attributes = NULL;

  // The format is name:value;name:value; with \ for escaping these
  // characters ":;=,\".

  if (!aAttributes)
    return S_FALSE;

  nsCOMPtr<nsISimpleEnumerator> propEnum;
  aAttributes->Enumerate(getter_AddRefs(propEnum));
  if (!propEnum)
    return E_FAIL;

  nsAutoString strAttrs;

  const char kCharsToEscape[] = ":;=,\\";

  PRBool hasMore = PR_FALSE;
  while (NS_SUCCEEDED(propEnum->HasMoreElements(&hasMore)) && hasMore) {
    nsCOMPtr<nsISupports> propSupports;
    propEnum->GetNext(getter_AddRefs(propSupports));

    nsCOMPtr<nsIPropertyElement> propElem(do_QueryInterface(propSupports));
    if (!propElem)
      return E_FAIL;

    nsCAutoString name;
    if (NS_FAILED(propElem->GetKey(name)))
      return E_FAIL;

    PRUint32 offset = 0;
    while ((offset = name.FindCharInSet(kCharsToEscape, offset)) != kNotFound) {
      name.Insert('\\', offset);
      offset += 2;
    }

    nsAutoString value;
    if (NS_FAILED(propElem->GetValue(value)))
      return E_FAIL;

    offset = 0;
    while ((offset = value.FindCharInSet(kCharsToEscape, offset)) != kNotFound) {
      value.Insert('\\', offset);
      offset += 2;
    }

    AppendUTF8toUTF16(name, strAttrs);
    strAttrs.Append(':');
    strAttrs.Append(value);
    strAttrs.Append(';');
  }

  if (strAttrs.IsEmpty())
    return S_FALSE;

  *aIA2Attributes = ::SysAllocStringLen(strAttrs.get(), strAttrs.Length());
  return *aIA2Attributes ? S_OK : E_OUTOFMEMORY;
}

IDispatch *nsAccessibleWrap::NativeAccessible(nsIAccessible *aXPAccessible)
{
  if (!aXPAccessible) {
   NS_WARNING("Not passing in an aXPAccessible");
   return NULL;
  }

  nsCOMPtr<nsIAccessibleWin32Object> accObject(do_QueryInterface(aXPAccessible));
  if (accObject) {
    void* hwnd = nsnull;
    accObject->GetHwnd(&hwnd);
    if (hwnd) {
      IDispatch *retval = nsnull;
      AccessibleObjectFromWindow(reinterpret_cast<HWND>(hwnd),
        OBJID_WINDOW, IID_IAccessible, (void **) &retval);
      return retval;
    }
  }

  IAccessible *msaaAccessible;
  aXPAccessible->GetNativeInterface((void**)&msaaAccessible);

  return static_cast<IDispatch*>(msaaAccessible);
}

void
nsAccessibleWrap::UnattachIEnumVariant()
{
  if (mEnumVARIANTPosition > 0)
    mEnumVARIANTPosition = kIEnumVariantDisconnected;
}

nsAccessible*
nsAccessibleWrap::GetXPAccessibleFor(const VARIANT& aVarChild)
{
  if (IsDefunct())
    return nsnull;

  // if its us real easy - this seems to always be the case
  if (aVarChild.lVal == CHILDID_SELF)
    return this;

  if (nsAccUtils::MustPrune(this))
    return nsnull;

  return GetChildAt(aVarChild.lVal);
}

void nsAccessibleWrap::UpdateSystemCaret()
{
  // Move the system caret so that Windows Tablet Edition and tradional ATs with 
  // off-screen model can follow the caret
  ::DestroyCaret();

  nsRefPtr<nsRootAccessible> rootAccessible = GetRootAccessible();
  if (!rootAccessible) {
    return;
  }

  nsRefPtr<nsCaretAccessible> caretAccessible = rootAccessible->GetCaretAccessible();
  if (!caretAccessible) {
    return;
  }

  nsIWidget *widget;
  nsIntRect caretRect = caretAccessible->GetCaretRect(&widget);
  HWND caretWnd; 
  if (caretRect.IsEmpty() || !(caretWnd = (HWND)widget->GetNativeData(NS_NATIVE_WINDOW))) {
    return;
  }

  // Create invisible bitmap for caret, otherwise its appearance interferes
  // with Gecko caret
  HBITMAP caretBitMap = CreateBitmap(1, caretRect.height, 1, 1, NULL);
  if (::CreateCaret(caretWnd, caretBitMap, 1, caretRect.height)) {  // Also destroys the last caret
    ::ShowCaret(caretWnd);
    RECT windowRect;
    ::GetWindowRect(caretWnd, &windowRect);
    ::SetCaretPos(caretRect.x - windowRect.left, caretRect.y - windowRect.top);
    ::DeleteObject(caretBitMap);
  }
}

ITypeInfo*
nsAccessibleWrap::GetTI(LCID lcid)
{
  if (mTypeInfo)
    return mTypeInfo;

  ITypeLib *typeLib = NULL;
  HRESULT hr = LoadRegTypeLib(LIBID_Accessibility, 1, 0, lcid, &typeLib);
  if (FAILED(hr))
    return NULL;

  hr = typeLib->GetTypeInfoOfGuid(IID_IAccessible, &mTypeInfo);
  typeLib->Release();

  if (FAILED(hr))
    return NULL;

  return mTypeInfo;
}
