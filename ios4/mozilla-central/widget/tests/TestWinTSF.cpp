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
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ningjie Chen <chenn@email.uc.edu>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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


/* This tests Mozilla's Text Services Framework implementation (bug #88831)
 *
 * The Mozilla implementation interacts with the TSF system through a
 * system-provided COM interface, ITfThreadMgr. This tests works by swapping
 * the system version of the interface with a custom version implemented in
 * here. This way the Mozilla implementation thinks it's interacting with the
 * system but in fact is interacting with this test program. This allows the
 * test program to access and test every aspect of the Mozilla implementation.
 */

#include <ole2.h>
#include <msctf.h>
#include <textstor.h>
#include <richedit.h>

#include "TestHarness.h"

#define WM_USER_TSF_TEXTCHANGE  (WM_USER + 0x100)

#ifndef MOZILLA_INTERNAL_API
// some of the includes make use of internal string types
#define nsAString_h___
#define nsString_h___
#define nsStringFwd_h___
#define nsReadableUtils_h___
class nsACString;
class nsAString;
class nsAFlatString;
class nsAFlatCString;
class nsAdoptingString;
class nsAdoptingCString;
class nsXPIDLString;
template<class T> class nsReadingIterator;
#endif

#include "nscore.h"
#include "nsWeakReference.h"
#include "nsIAppShell.h"
#include "nsWidgetsCID.h"
#include "nsIAppShellService.h"
#include "nsAppShellCID.h"
#include "nsNetUtil.h"
#include "nsIWebBrowserChrome.h"
#include "nsIXULWindow.h"
#include "nsIBaseWindow.h"
#include "nsIDOMWindowInternal.h"
#include "nsIDocShell.h"
#include "nsIWidget.h"
#include "nsIPresShell.h"
#include "nsPresContext.h"
#include "nsIFrame.h"
#include "nsIWebProgress.h"
#include "nsIWebProgressListener.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIDOMHTMLDocument.h"
#include "nsIDOMHTMLBodyElement.h"
#include "nsIDOMHTMLElement.h"
#include "nsIDOMHTMLInputElement.h"
#include "nsIDOMHTMLTextAreaElement.h"
#include "nsIDOMNSElement.h"
#include "nsISelectionController.h"
#include "nsIViewManager.h"
#include "nsTArray.h"
#include "nsGUIEvent.h"

#ifndef MOZILLA_INTERNAL_API
#undef nsString_h___
#undef nsAString_h___
#undef nsReadableUtils_h___
#endif

static NS_DEFINE_CID(kAppShellCID, NS_APPSHELL_CID);

class TSFMgrImpl;
class TSFDocumentMgrImpl;
class TSFContextImpl;
class TSFRangeImpl;
class TSFEnumRangeImpl;
class TSFAttrPropImpl;

class TestApp : public nsIWebProgressListener, public nsSupportsWeakReference
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIWEBPROGRESSLISTENER

  TestApp() : mFailed(PR_FALSE) {}
  ~TestApp() {}

  nsresult Run(void);
  PRBool CheckFailed(void);

  typedef PRBool (TestApp::*test_type)(void);

protected:
  nsresult Init(void);
  nsresult Term(void);
  PRBool RunTest(test_type aTest, PRBool aLock = PR_TRUE);

  PRBool TestFocus(void);
  PRBool TestClustering(void);
  PRBool TestSelection(void);
  PRBool TestText(void);
  PRBool TestExtents(void);
  PRBool TestComposition(void);
  PRBool TestNotification(void);
  PRBool TestEditMessages(void);
  PRBool TestScrollMessages(void);

  PRBool TestSelectionInternal(char* aTestName,
                                        LONG aStart,
                                        LONG aEnd,
                                        TsActiveSelEnd aSelEnd);
  PRBool TestCompositionSelectionAndText(char* aTestName,
                                         LONG aExpectedSelStart,
                                         LONG aExpectedSelEnd,
                                         nsString& aReferenceString);
  PRBool TestNotificationTextChange(nsIWidget* aWidget,
                                    PRUint32 aCode,
                                    const nsAString& aCharacter,
                                    LONG aStart,
                                    LONG aOldEnd,
                                    LONG aNewEnd);
  nsresult GetSelCon(nsISelectionController** aSelCon);

  PRBool GetWidget(nsIWidget** aWidget);

  PRBool mFailed;
  nsString mTestString;
  nsRefPtr<TSFMgrImpl> mMgr;
  nsCOMPtr<nsIAppShell> mAppShell;
  nsCOMPtr<nsIXULWindow> mWindow;
  nsCOMPtr<nsIDOMNode> mCurrentNode;
  nsCOMPtr<nsIDOMHTMLInputElement> mInput;
  nsCOMPtr<nsIDOMHTMLTextAreaElement> mTextArea;
  nsCOMPtr<nsIDOMHTMLInputElement> mButton;
};

NS_IMETHODIMP
TestApp::OnProgressChange(nsIWebProgress *aWebProgress,
                           nsIRequest *aRequest,
                           PRInt32 aCurSelfProgress,
                           PRInt32 aMaxSelfProgress,
                           PRInt32 aCurTotalProgress,
                           PRInt32 aMaxTotalProgress)
{
  return NS_OK;
}

NS_IMETHODIMP
TestApp::OnLocationChange(nsIWebProgress *aWebProgress,
                           nsIRequest *aRequest,
                           nsIURI *aLocation)
{
  return NS_OK;
}

NS_IMETHODIMP
TestApp::OnStatusChange(nsIWebProgress *aWebProgress,
                         nsIRequest *aRequest,
                         nsresult aStatus,
                         const PRUnichar *aMessage)
{
  return NS_OK;
}

NS_IMETHODIMP
TestApp::OnSecurityChange(nsIWebProgress *aWebProgress,
                           nsIRequest *aRequest,
                           PRUint32 aState)
{
  return NS_OK;
}

static HRESULT
GetRegularExtent(ITfRange *aRange, LONG &aStart, LONG &aEnd)
{
  NS_ENSURE_TRUE(aRange, E_INVALIDARG);
  nsRefPtr<ITfRangeACP> rangeACP;
  HRESULT hr = aRange->QueryInterface(IID_ITfRangeACP,
                                      getter_AddRefs(rangeACP));
  NS_ENSURE_TRUE(SUCCEEDED(hr) && rangeACP, E_FAIL);

  LONG start, length;
  hr = rangeACP->GetExtent(&start, &length);
  NS_ENSURE_TRUE(SUCCEEDED(hr), hr);
  if (length >= 0) {
    aStart = start;
    aEnd = start + length;
  } else {
    aEnd = start;
    aStart = start + length;
  }
  return S_OK;
}

// {3B2DFDF5-2485-4858-8185-5C6B4EFD38F5}
static const GUID GUID_COMPOSING_SELECTION_ATTR = 
  { 0x3b2dfdf5, 0x2485, 0x4858,
    { 0x81, 0x85, 0x5c, 0x6b, 0x4e, 0xfd, 0x38, 0xf5 } };
#define GUID_ATOM_COMPOSING_SELECTION_ATTR \
  (static_cast<TfGuidAtom>(0x3b2dfdf5))

/******************************************************************************
 * TSFRangeImpl
 ******************************************************************************/

class TSFRangeImpl : public ITfRangeACP
{
private:
  ULONG mRefCnt;

public:
  LONG mStart;
  LONG mLength;

  TSFRangeImpl(LONG aStart = 0, LONG aLength = 0) :
      mRefCnt(0), mStart(aStart), mLength(aLength)
  {
  }

  ~TSFRangeImpl()
  {
  }

public: // IUnknown

  STDMETHODIMP QueryInterface(REFIID riid, void** ppUnk)
  {
    *ppUnk = NULL;
    if (IID_IUnknown == riid || IID_ITfRange == riid || IID_ITfRangeACP == riid)
      *ppUnk = static_cast<ITfRangeACP*>(this);
    if (*ppUnk)
      AddRef();
    return *ppUnk ? S_OK : E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef(void)
  {
    return ++mRefCnt;
  }

  STDMETHODIMP_(ULONG) Release(void)
  {
    if (--mRefCnt) return mRefCnt;
    delete this;
    return 0;
  }

public: // ITfRange

  STDMETHODIMP GetText(TfEditCookie ec, DWORD dwFlags, WCHAR *pchText,
                       ULONG cchMax, ULONG *pcch)
  {
    NS_NOTREACHED("ITfRange::GetText");
    return E_NOTIMPL;
  }

  STDMETHODIMP SetText(TfEditCookie ec, DWORD dwFlags, const WCHAR *pchText,
                       LONG cch)
  {
    NS_NOTREACHED("ITfRange::SetText");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetFormattedText(TfEditCookie ec, IDataObject **ppDataObject)
  {
    NS_NOTREACHED("ITfRange::GetFormattedText");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetEmbedded(TfEditCookie ec, REFGUID rguidService, REFIID riid,
                           IUnknown **ppunk)
  {
    NS_NOTREACHED("ITfRange::GetEmbedded");
    return E_NOTIMPL;
  }

  STDMETHODIMP InsertEmbedded(TfEditCookie ec, DWORD dwFlags,
                              IDataObject *pDataObject)
  {
    NS_NOTREACHED("ITfRange::InsertEmbedded");
    return E_NOTIMPL;
  }

  STDMETHODIMP ShiftStart(TfEditCookie ec, LONG cchReq, LONG *pcch,
                          const TF_HALTCOND *pHalt)
  {
    NS_NOTREACHED("ITfRange::ShiftStart");
    return E_NOTIMPL;
  }

  STDMETHODIMP ShiftEnd(TfEditCookie ec, LONG cchReq, LONG *pcch,
                        const TF_HALTCOND *pHalt)
  {
    NS_NOTREACHED("ITfRange::ShiftEnd");
    return E_NOTIMPL;
  }

  STDMETHODIMP ShiftStartToRange(TfEditCookie ec, ITfRange *pRange,
                                 TfAnchor aPos)
  {
    NS_NOTREACHED("ITfRange::ShiftStartToRange");
    return E_NOTIMPL;
  }

  STDMETHODIMP ShiftEndToRange(TfEditCookie ec, ITfRange *pRange, TfAnchor aPos)
  {
    NS_NOTREACHED("ITfRange::ShiftEndToRange");
    return E_NOTIMPL;
  }

  STDMETHODIMP ShiftStartRegion(TfEditCookie ec, TfShiftDir dir,
                                BOOL *pfNoRegion)
  {
    NS_NOTREACHED("ITfRange::ShiftStartRegion");
    return E_NOTIMPL;
  }

  STDMETHODIMP ShiftEndRegion(TfEditCookie ec, TfShiftDir dir, BOOL *pfNoRegion)
  {
    NS_NOTREACHED("ITfRange::ShiftEndRegion");
    return E_NOTIMPL;
  }

  STDMETHODIMP IsEmpty(TfEditCookie ec, BOOL *pfEmpty)
  {
    NS_NOTREACHED("ITfRange::IsEmpty");
    return E_NOTIMPL;
  }

  STDMETHODIMP Collapse(TfEditCookie ec, TfAnchor aPos)
  {
    NS_NOTREACHED("ITfRange::Collapse");
    return E_NOTIMPL;
  }

  STDMETHODIMP IsEqualStart(TfEditCookie ec, ITfRange *pWith, TfAnchor aPos,
                            BOOL *pfEqual)
  {
    NS_NOTREACHED("ITfRange::IsEqualStart");
    return E_NOTIMPL;
  }

  STDMETHODIMP IsEqualEnd(TfEditCookie ec, ITfRange *pWith, TfAnchor aPos,
                          BOOL *pfEqual)
  {
    NS_NOTREACHED("ITfRange::IsEqualEnd");
    return E_NOTIMPL;
  }

  STDMETHODIMP CompareStart(TfEditCookie ec, ITfRange *pWith, TfAnchor aPos,
                            LONG *plResult)
  {
    NS_NOTREACHED("ITfRange::CompareStart");
    return E_NOTIMPL;
  }

  STDMETHODIMP CompareEnd(TfEditCookie ec, ITfRange *pWith, TfAnchor aPos,
                          LONG *plResult)
  {
    NS_NOTREACHED("ITfRange::CompareEnd");
    return E_NOTIMPL;
  }

  STDMETHODIMP AdjustForInsert(TfEditCookie ec, ULONG cchInsert,
                               BOOL *pfInsertOk)
  {
    NS_NOTREACHED("ITfRange::AdjustForInsert");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetGravity(TfGravity *pgStart, TfGravity *pgEnd)
  {
    NS_NOTREACHED("ITfRange::GetGravity");
    return E_NOTIMPL;
  }

  STDMETHODIMP SetGravity(TfEditCookie ec, TfGravity gStart, TfGravity gEnd)
  {
    NS_NOTREACHED("ITfRange::SetGravity");
    return E_NOTIMPL;
  }

  STDMETHODIMP Clone(ITfRange **ppClone)
  {
    NS_NOTREACHED("ITfRange::Clone");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetContext(ITfContext **ppContext)
  {
    NS_NOTREACHED("ITfRange::GetContext");
    return E_NOTIMPL;
  }

public: // ITfRangeACP

  STDMETHODIMP GetExtent(LONG *pacpAnchor, LONG *pcch)
  {
    NS_ENSURE_TRUE(pacpAnchor, E_FAIL);
    NS_ENSURE_TRUE(pcch, E_FAIL);
    *pacpAnchor = mStart;
    *pcch = mLength;
    return S_OK;
  }

  STDMETHODIMP SetExtent(LONG acpAnchor, LONG cch)
  {
    mStart = acpAnchor;
    mLength = cch;
    return S_OK;
  }
};

/******************************************************************************
 * TSFEnumRangeImpl
 ******************************************************************************/

class TSFEnumRangeImpl : public IEnumTfRanges
{
private:
  ULONG mRefCnt;
  PRUint32 mCurrentIndex;

public:
  nsTArray<nsRefPtr<TSFRangeImpl> > mRanges;

  TSFEnumRangeImpl() :
      mRefCnt(0), mCurrentIndex(0)
  {
  }

  ~TSFEnumRangeImpl()
  {
  }

public: // IUnknown

  STDMETHODIMP QueryInterface(REFIID riid, void** ppUnk)
  {
    *ppUnk = NULL;
    if (IID_IUnknown == riid || IID_IEnumTfRanges == riid)
      *ppUnk = static_cast<IEnumTfRanges*>(this);
    if (*ppUnk)
      AddRef();
    return *ppUnk ? S_OK : E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef(void)
  {
    return ++mRefCnt;
  }

  STDMETHODIMP_(ULONG) Release(void)
  {
    if (--mRefCnt) return mRefCnt;
    delete this;
    return 0;
  }

public: // IEnumTfRanges

  STDMETHODIMP Clone(IEnumTfRanges **ppEnum)
  {
    NS_NOTREACHED("IEnumTfRanges::Clone");
    return E_NOTIMPL;
  }

  STDMETHODIMP Next(ULONG ulCount, ITfRange **ppRange, ULONG *pcFetched)
  {
    NS_ENSURE_TRUE(ppRange, E_FAIL);
    if (pcFetched)
      *pcFetched = 0;
    if (mCurrentIndex + ulCount - 1 >= mRanges.Length())
      return E_FAIL;
    for (PRUint32 i = 0; i < ulCount; i++) {
      ppRange[i] = mRanges[mCurrentIndex++];
      ppRange[i]->AddRef();
      if (pcFetched)
        (*pcFetched)++;
    }
    return S_OK;
  }

  STDMETHODIMP Reset()
  {
    mCurrentIndex = 0;
    return S_OK;
  }

  STDMETHODIMP Skip(ULONG ulCount)
  {
    mCurrentIndex += ulCount;
    return S_OK;
  }
};

/******************************************************************************
 * TSFDispAttrInfoImpl
 ******************************************************************************/

class TSFDispAttrInfoImpl : public ITfDisplayAttributeInfo
{
private:
  ULONG mRefCnt;
  TF_DISPLAYATTRIBUTE mAttr;

public:

  TSFDispAttrInfoImpl(REFGUID aGUID) :
      mRefCnt(0)
  {
    if (aGUID == GUID_COMPOSING_SELECTION_ATTR) {
      mAttr.crText.type = TF_CT_NONE;
      mAttr.crBk.type = TF_CT_NONE;
      mAttr.lsStyle = TF_LS_SQUIGGLE;
      mAttr.fBoldLine = FALSE;
      mAttr.crLine.type = TF_CT_NONE;
      mAttr.bAttr = TF_ATTR_INPUT;
    } else {
      NS_NOTREACHED("TSFDispAttrInfoImpl::TSFDispAttrInfoImpl");
    }
  }

  ~TSFDispAttrInfoImpl()
  {
  }

public: // IUnknown

  STDMETHODIMP QueryInterface(REFIID riid, void** ppUnk)
  {
    *ppUnk = NULL;
    if (IID_IUnknown == riid || IID_ITfDisplayAttributeInfo == riid)
      *ppUnk = static_cast<ITfDisplayAttributeInfo*>(this);
    if (*ppUnk)
      AddRef();
    return *ppUnk ? S_OK : E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef(void)
  {
    return ++mRefCnt;
  }

  STDMETHODIMP_(ULONG) Release(void)
  {
    if (--mRefCnt) return mRefCnt;
    delete this;
    return 0;
  }

public: // ITfDisplayAttributeInfo

  STDMETHODIMP GetGUID(GUID *pguid)
  {
    NS_NOTREACHED("ITfDisplayAttributeInfo::GetGUID");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetDescription(BSTR *pbstrDesc)
  {
    NS_NOTREACHED("ITfDisplayAttributeInfo::GetDescription");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE *pda)
  {
    NS_ENSURE_TRUE(pda, E_INVALIDARG);
    *pda = mAttr;
    return S_OK;
  }

  STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE *pda)
  {
    NS_NOTREACHED("ITfDisplayAttributeInfo::SetAttributeInfo");
    return E_NOTIMPL;
  }

  STDMETHODIMP Reset()
  {
    NS_NOTREACHED("ITfDisplayAttributeInfo::Reset");
    return E_NOTIMPL;
  }
};

/******************************************************************************
 * TSFAttrPropImpl
 ******************************************************************************/

class TSFAttrPropImpl : public ITfProperty
{
private:
  ULONG mRefCnt;

public:
  nsTArray<nsRefPtr<TSFRangeImpl> > mRanges;

  TSFAttrPropImpl() :
      mRefCnt(0)
  {
  }

  ~TSFAttrPropImpl()
  {
  }

public: // IUnknown

  STDMETHODIMP QueryInterface(REFIID riid, void** ppUnk)
  {
    *ppUnk = NULL;
    if (IID_IUnknown == riid || IID_ITfProperty == riid ||
        IID_ITfReadOnlyProperty == riid)
      *ppUnk = static_cast<ITfProperty*>(this);
    if (*ppUnk)
      AddRef();
    return *ppUnk ? S_OK : E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef(void)
  {
    return ++mRefCnt;
  }

  STDMETHODIMP_(ULONG) Release(void)
  {
    if (--mRefCnt) return mRefCnt;
    delete this;
    return 0;
  }

public: // ITfProperty

  STDMETHODIMP FindRange(TfEditCookie ec, ITfRange *pRange, ITfRange **ppRange,
                         TfAnchor aPos)
  {
    NS_NOTREACHED("ITfProperty::FindRange");
    return E_NOTIMPL;
  }

  STDMETHODIMP SetValueStore(TfEditCookie ec, ITfRange *pRange,
                             ITfPropertyStore *pPropStore)
  {
    NS_NOTREACHED("ITfProperty::SetValueStore");
    return E_NOTIMPL;
  }

  STDMETHODIMP SetValue(TfEditCookie ec, ITfRange *pRange,
                        const VARIANT *pvarValue)
  {
    NS_NOTREACHED("ITfProperty::SetValue");
    return E_NOTIMPL;
  }

  STDMETHODIMP Clear(TfEditCookie ec, ITfRange *pRange)
  {
    NS_NOTREACHED("ITfProperty::Clear");
    return E_NOTIMPL;
  }

public: // ITfReadOnlyProperty

  STDMETHODIMP GetType(GUID *pguid)
  {
    NS_NOTREACHED("ITfReadOnlyProperty::GetType");
    return E_NOTIMPL;
  }

  STDMETHODIMP EnumRanges(TfEditCookie ec, IEnumTfRanges **ppEnum,
                          ITfRange *pTargetRange)
  {
    NS_ENSURE_TRUE(ppEnum, E_INVALIDARG);
    NS_ENSURE_TRUE(pTargetRange, E_INVALIDARG);

    // XXX ec checking is not implemented yet.

    LONG targetStart = 0, targetEnd = 0;
    if (pTargetRange) {
      HRESULT hr = GetRegularExtent(pTargetRange, targetStart, targetEnd);
      NS_ENSURE_TRUE(SUCCEEDED(hr), hr);
    }
    nsRefPtr<TSFEnumRangeImpl> er = new TSFEnumRangeImpl();
    NS_ENSURE_TRUE(er, E_OUTOFMEMORY);
    for (PRUint32 i = 0; i < mRanges.Length(); i++) {
      LONG start, end;
      HRESULT hr = GetRegularExtent(mRanges[i], start, end);
      NS_ENSURE_TRUE(SUCCEEDED(hr), hr);
      if (pTargetRange) {
        // If pTargetRange is not null and the current range is not overlapped
        // with it, we don't need to add range.
        if (targetStart > end || targetEnd < start)
          continue;
        // Otherwise, shrink to the target range.
        start = PR_MAX(targetStart, start);
        end = PR_MIN(targetEnd, end);
      }
      nsRefPtr<TSFRangeImpl> range = new TSFRangeImpl(start, end - start);
      NS_ENSURE_TRUE(range, E_OUTOFMEMORY);
      er->mRanges.AppendElement(range);
    }
    *ppEnum = er;
    (*ppEnum)->AddRef();
    return S_OK;
  }

  STDMETHODIMP GetValue(TfEditCookie ec, ITfRange *pRange, VARIANT *pvarValue)
  {
    NS_ENSURE_TRUE(pvarValue, E_INVALIDARG);

    LONG givenStart, givenEnd;
    HRESULT hr = GetRegularExtent(pRange, givenStart, givenEnd);
    NS_ENSURE_TRUE(SUCCEEDED(hr), hr);
    for (PRUint32 i = 0; i < mRanges.Length(); i++) {
      LONG start, end;
      HRESULT hr = GetRegularExtent(mRanges[i], start, end);
      NS_ENSURE_TRUE(SUCCEEDED(hr), hr);
      // pRange must be same as (or included in) a range of mRanges.
      if (givenStart > start || givenEnd < end)
        continue;
      pvarValue->vt = VT_I4;
      pvarValue->lVal = static_cast<DWORD>(GUID_ATOM_COMPOSING_SELECTION_ATTR);
      return S_OK;
    }
    return E_FAIL;
  }

  STDMETHODIMP GetContext(ITfContext **ppContext)
  {
    NS_NOTREACHED("ITfReadOnlyProperty::GetContext");
    return E_NOTIMPL;
  }
};

/******************************************************************************
 * TSFContextImpl
 ******************************************************************************/

class TSFContextImpl : public ITfContext,
                       public ITfCompositionView, public ITextStoreACPSink
{
private:
  ULONG mRefCnt;

public:
  nsRefPtr<TSFAttrPropImpl> mAttrProp;
  nsRefPtr<TSFDocumentMgrImpl> mDocMgr;
  PRBool mTextChanged;
  PRBool mSelChanged;
  TS_TEXTCHANGE mTextChangeData;

public:
  TSFContextImpl(TSFDocumentMgrImpl* aDocMgr) :
      mDocMgr(aDocMgr), mRefCnt(0), mTextChanged(PR_FALSE),
      mSelChanged(PR_FALSE)
  {
    mAttrProp = new TSFAttrPropImpl();
    if (!mAttrProp) {
      NS_NOTREACHED("TSFContextImpl::TSFContextImpl (OOM)");
      return;
    }
  }

  ~TSFContextImpl()
  {
  }

public: // IUnknown

  STDMETHODIMP QueryInterface(REFIID riid, void** ppUnk)
  {
    *ppUnk = NULL;
    if (IID_IUnknown == riid || IID_ITfContext == riid)
      *ppUnk = static_cast<ITfContext*>(this);
    else if (IID_ITextStoreACPSink == riid)
      *ppUnk = static_cast<ITextStoreACPSink*>(this);
    if (*ppUnk)
      AddRef();
    return *ppUnk ? S_OK : E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef(void)
  {
    return ++mRefCnt;
  }

  STDMETHODIMP_(ULONG) Release(void)
  {
    if (--mRefCnt) return mRefCnt;
    delete this;
    return 0;
  }

public: // ITfContext

  STDMETHODIMP RequestEditSession(TfClientId tid, ITfEditSession *pes,
                                  DWORD dwFlags, HRESULT *phrSession)
  {
    NS_NOTREACHED("ITfContext::RequestEditSession");
    return E_NOTIMPL;
  }

  STDMETHODIMP InWriteSession(TfClientId tid, BOOL *pfWriteSession)
  {
    NS_NOTREACHED("ITfContext::InWriteSession");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetSelection(TfEditCookie ec, ULONG ulIndex, ULONG ulCount,
                            TF_SELECTION *pSelection, ULONG *pcFetched)
  {
    NS_NOTREACHED("ITfContext::GetSelection");
    return E_NOTIMPL;
  }

  STDMETHODIMP SetSelection(TfEditCookie ec, ULONG ulCount,
                         const TF_SELECTION *pSelection)
  {
    NS_NOTREACHED("ITfContext::SetSelection");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetStart(TfEditCookie ec, ITfRange **ppStart)
  {
    NS_NOTREACHED("ITfContext::GetStart");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetEnd(TfEditCookie ec, ITfRange **ppEnd)
  {
    NS_NOTREACHED("ITfContext::GetEnd");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetActiveView(ITfContextView **ppView)
  {
    NS_NOTREACHED("ITfContext::GetActiveView");
    return E_NOTIMPL;
  }

  STDMETHODIMP EnumViews(IEnumTfContextViews **ppEnum)
  {
    NS_NOTREACHED("ITfContext::EnumViews");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetStatus(TF_STATUS *pdcs)
  {
    NS_NOTREACHED("ITfContext::GetStatus");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetProperty(REFGUID guidProp, ITfProperty **ppProp)
  {
    NS_ENSURE_TRUE(ppProp, E_INVALIDARG);
    if (guidProp == GUID_PROP_ATTRIBUTE) {
      (*ppProp) = mAttrProp;
      (*ppProp)->AddRef();
      return S_OK;
    }
    NS_NOTREACHED("ITfContext::GetProperty");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetAppProperty(REFGUID guidProp, ITfReadOnlyProperty **ppProp)
  {
    NS_NOTREACHED("ITfContext::GetAppProperty");
    return E_NOTIMPL;
  }

  STDMETHODIMP TrackProperties(const GUID **prgProp, ULONG cProp,
                            const GUID **prgAppProp, ULONG cAppProp,
                            ITfReadOnlyProperty **ppProperty)
  {
    NS_NOTREACHED("ITfContext::TrackProperties");
    return E_NOTIMPL;
  }

  STDMETHODIMP EnumProperties(IEnumTfProperties **ppEnum)
  {
    NS_NOTREACHED("ITfContext::EnumProperties");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetDocumentMgr(ITfDocumentMgr **ppDm)
  {
    NS_NOTREACHED("ITfContext::GetDocumentMgr");
    return E_NOTIMPL;
  }

  STDMETHODIMP CreateRangeBackup(TfEditCookie ec, ITfRange *pRange,
                              ITfRangeBackup **ppBackup)
  {
    NS_NOTREACHED("ITfContext::CreateRangeBackup");
    return E_NOTIMPL;
  }

public: // ITfCompositionView

  STDMETHODIMP GetOwnerClsid(CLSID* pclsid)
  {
    NS_NOTREACHED("ITfCompositionView::GetOwnerClsid");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetRange(ITfRange** ppRange)
  {
    NS_ENSURE_TRUE(ppRange, E_INVALIDARG);
    NS_ENSURE_TRUE(mAttrProp->mRanges.Length() > 0, E_FAIL);
    LONG start = LONG_MAX, end = 0;
    for (PRUint32 i = 0; i < mAttrProp->mRanges.Length(); i++) {
      LONG tmpStart, tmpEnd;
      HRESULT hr = GetRegularExtent(mAttrProp->mRanges[i], tmpStart, tmpEnd);
      NS_ENSURE_TRUE(SUCCEEDED(hr), hr);
      start = PR_MIN(start, tmpStart);
      end = PR_MAX(end, tmpEnd);
    }
    nsRefPtr<TSFRangeImpl> range = new TSFRangeImpl();
    NS_ENSURE_TRUE(range, E_OUTOFMEMORY);
    HRESULT hr = range->SetExtent(start, end - start);
    NS_ENSURE_TRUE(SUCCEEDED(hr), hr);
    (*ppRange) = range;
    (*ppRange)->AddRef();
    return S_OK;
  }

public: // ITextStoreACPSink

  STDMETHODIMP OnTextChange(DWORD dwFlags, const TS_TEXTCHANGE *pChange)
  {
    mTextChanged = PR_TRUE;
    mTextChangeData = *pChange;
    return S_OK;
  }

  STDMETHODIMP OnSelectionChange(void)
  {
    mSelChanged = PR_TRUE;
    return S_OK;
  }

  STDMETHODIMP OnLayoutChange(TsLayoutCode lcode, TsViewCookie vcView)
  {
    return S_OK;
  }

  STDMETHODIMP OnStatusChange(DWORD dwFlags)
  {
    return S_OK;
  }

  STDMETHODIMP OnAttrsChange(LONG acpStart, LONG acpEnd, ULONG cAttrs,
                          const TS_ATTRID *paAttrs)
  {
    return S_OK;
  }

  STDMETHODIMP OnLockGranted(DWORD dwLockFlags);

  STDMETHODIMP OnStartEditTransaction(void)
  {
    return S_OK;
  }

  STDMETHODIMP OnEndEditTransaction(void)
  {
    return S_OK;
  }
};

/******************************************************************************
 * TSFDocumentMgrImpl
 ******************************************************************************/

class TSFDocumentMgrImpl : public ITfDocumentMgr
{
private:
  ULONG mRefCnt;

public:
  nsRefPtr<TSFMgrImpl> mMgr;
  nsRefPtr<ITextStoreACP> mStore;
  nsRefPtr<TSFContextImpl> mContextBase;
  nsRefPtr<TSFContextImpl> mContextTop; // XXX currently, we don't support this.

public:
  TSFDocumentMgrImpl(TSFMgrImpl* aMgr) :
      mRefCnt(0), mMgr(aMgr)
  {
  }

  ~TSFDocumentMgrImpl()
  {
  }

public: // IUnknown

  STDMETHODIMP QueryInterface(REFIID riid, void** ppUnk)
  {
    *ppUnk = NULL;
    if (IID_IUnknown == riid || IID_ITfDocumentMgr == riid)
      *ppUnk = static_cast<ITfDocumentMgr*>(this);
    if (*ppUnk)
      AddRef();
    return *ppUnk ? S_OK : E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef(void)
  {
    return ++mRefCnt;
  }

  STDMETHODIMP_(ULONG) Release(void);

public: // ITfDocumentMgr

  STDMETHODIMP CreateContext(TfClientId tidOwner, DWORD dwFlags,
                             IUnknown *punk, ITfContext **ppic,
                             TfEditCookie *pecTextStore)
  {
    nsRefPtr<TSFContextImpl> context = new TSFContextImpl(this);
    punk->QueryInterface(IID_ITextStoreACP, getter_AddRefs(mStore));
    NS_ENSURE_TRUE(mStore, E_FAIL);
    HRESULT hr =
      mStore->AdviseSink(IID_ITextStoreACPSink,
                         static_cast<ITextStoreACPSink*>(context.get()),
                         TS_AS_ALL_SINKS);
    if (FAILED(hr)) mStore = NULL;
    NS_ENSURE_TRUE(SUCCEEDED(hr), E_FAIL);
    (*ppic) = context;
    (*ppic)->AddRef();
    *pecTextStore = 1;
    return S_OK;
  }

  STDMETHODIMP Push(ITfContext *pic)
  {
    if (mContextTop) {
      NS_NOTREACHED("TSFDocumentMgrImpl::Push stack is already full");
      return E_FAIL;
    }
    if (mContextBase) {
      NS_WARNING("TSFDocumentMgrImpl::Push additional context is pushed, but we don't support that yet.");
      if (mContextBase == pic) {
        NS_NOTREACHED("TSFDocumentMgrImpl::Push same context is pused again");
        return E_FAIL;
      }
      mContextTop = static_cast<TSFContextImpl*>(pic);
      return S_OK;
    }
    mContextBase = static_cast<TSFContextImpl*>(pic);
    return S_OK;
  }

  STDMETHODIMP Pop(DWORD dwFlags)
  {
    if (!mStore)
      return E_FAIL;
    if (dwFlags == TF_POPF_ALL) {
      NS_ENSURE_TRUE(mContextBase, E_FAIL);
      mStore->UnadviseSink(static_cast<ITextStoreACPSink*>(mContextBase.get()));
      mStore = NULL;
      mContextBase = nsnull;
      mContextTop = nsnull;
      return S_OK;
    }
    if (dwFlags == 0) {
      if (!mContextTop) {
        NS_NOTREACHED("TSFDocumentMgrImpl::Pop there is non-base context");
        return E_FAIL;
      }
      mContextTop = nsnull;
      return S_OK;
    }
    NS_NOTREACHED("TSFDocumentMgrImpl::Pop invalid flag");
    return E_FAIL;
  }

  STDMETHODIMP GetTop(ITfContext **ppic)
  {
    (*ppic) = mContextTop ? mContextTop : mContextBase;
    if (*ppic) (*ppic)->AddRef();
    return S_OK;
  }

  STDMETHODIMP GetBase(ITfContext **ppic)
  {
    (*ppic) = mContextBase;
    if (*ppic) (*ppic)->AddRef();
    return S_OK;
  }

  STDMETHODIMP EnumContexts(IEnumTfContexts **ppEnum)
  {
    NS_NOTREACHED("ITfDocumentMgr::EnumContexts");
    return E_NOTIMPL;
  }

};

/******************************************************************************
 * TSFMgrImpl
 ******************************************************************************/

class TSFMgrImpl : public ITfThreadMgr,
                   public ITfDisplayAttributeMgr, public ITfCategoryMgr
{
private:
  ULONG mRefCnt;

public:
  nsRefPtr<TestApp> mTestApp;
  TestApp::test_type mTest;
  PRBool mDeactivated;
  TSFDocumentMgrImpl* mFocusedDocument; // Must be raw pointer, but strong.
  PRInt32 mFocusCount;

  TSFMgrImpl(TestApp* test) : mTestApp(test), mTest(nsnull), mRefCnt(0),
    mDeactivated(PR_FALSE), mFocusedDocument(nsnull), mFocusCount(0)
  {
  }

  ~TSFMgrImpl()
  {
  }

public: // IUnknown

  STDMETHODIMP QueryInterface(REFIID riid, void** ppUnk)
  {
    *ppUnk = NULL;
    if (IID_IUnknown == riid || IID_ITfThreadMgr == riid)
      *ppUnk = static_cast<ITfThreadMgr*>(this);
    else if (IID_ITfDisplayAttributeMgr == riid)
      *ppUnk = static_cast<ITfDisplayAttributeMgr*>(this);
    else if (IID_ITfCategoryMgr == riid)
      *ppUnk = static_cast<ITfCategoryMgr*>(this);
    if (*ppUnk)
      AddRef();
    return *ppUnk ? S_OK : E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef(void)
  {
    return ++mRefCnt;
  }

  STDMETHODIMP_(ULONG) Release(void)
  {
    if (--mRefCnt) return mRefCnt;
    delete this;
    return 0;
  }

public: // ITfThreadMgr

  STDMETHODIMP Activate(TfClientId *ptid)
  {
    *ptid = 1;
    return S_OK;
  }

  STDMETHODIMP Deactivate(void)
  {
    mDeactivated = PR_TRUE;
    return S_OK;
  }

  STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr **ppdim)
  {
    nsRefPtr<TSFDocumentMgrImpl> docMgr = new TSFDocumentMgrImpl(this);
    if (!docMgr) {
      NS_NOTREACHED("TSFMgrImpl::CreateDocumentMgr (OOM)");
      return E_OUTOFMEMORY;
    }
    (*ppdim) = docMgr;
    (*ppdim)->AddRef();
    return S_OK;
  }

  STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs **ppEnum)
  {
    NS_NOTREACHED("ITfThreadMgr::EnumDocumentMgrs");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetFocus(ITfDocumentMgr **ppdimFocus)
  {
    (*ppdimFocus) = mFocusedDocument;
    if (*ppdimFocus) (*ppdimFocus)->AddRef();
    return S_OK;
  }

  STDMETHODIMP SetFocus(ITfDocumentMgr *pdimFocus)
  {
    if (!pdimFocus) {
      NS_NOTREACHED("ITfThreadMgr::SetFocus must not be called with NULL");
      return E_FAIL;
    }
    mFocusCount++;
    if (mFocusedDocument == pdimFocus) {
      return S_OK;
    }
    mFocusedDocument = static_cast<TSFDocumentMgrImpl*>(pdimFocus);
    mFocusedDocument->AddRef();
    return S_OK;
  }

  STDMETHODIMP AssociateFocus(HWND hwnd, ITfDocumentMgr *pdimNew,
                           ITfDocumentMgr **ppdimPrev)
  {
    NS_NOTREACHED("ITfThreadMgr::AssociateFocus");
    return E_NOTIMPL;
  }

  STDMETHODIMP IsThreadFocus(BOOL *pfThreadFocus)
  {
    *pfThreadFocus = TRUE;
    return S_OK;
  }

  STDMETHODIMP GetFunctionProvider(REFCLSID clsid,
                                ITfFunctionProvider **ppFuncProv)
  {
    NS_NOTREACHED("ITfThreadMgr::GetFunctionProvider");
    return E_NOTIMPL;
  }

  STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders **ppEnum)
  {
    NS_NOTREACHED("ITfThreadMgr::EnumFunctionProviders");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr **ppCompMgr)
  {
    NS_NOTREACHED("ITfThreadMgr::GetGlobalCompartment");
    return E_NOTIMPL;
  }

public: // ITfCategoryMgr

  STDMETHODIMP RegisterCategory(REFCLSID rclsid, REFGUID rcatid, REFGUID rguid)
  {
    NS_NOTREACHED("ITfCategoryMgr::RegisterCategory");
    return E_NOTIMPL;
  }

  STDMETHODIMP UnregisterCategory(REFCLSID rclsid, REFGUID rcatid, REFGUID rguid)
  {
    NS_NOTREACHED("ITfCategoryMgr::UnregisterCategory");
    return E_NOTIMPL;
  }

  STDMETHODIMP EnumCategoriesInItem(REFGUID rguid, IEnumGUID **ppEnum)
  {
    NS_NOTREACHED("ITfCategoryMgr::EnumCategoriesInItem");
    return E_NOTIMPL;
  }

  STDMETHODIMP EnumItemsInCategory(REFGUID rcatid, IEnumGUID **ppEnum)
  {
    NS_NOTREACHED("ITfCategoryMgr::EnumItemsInCategory");
    return E_NOTIMPL;
  }

  STDMETHODIMP FindClosestCategory(REFGUID rguid, GUID *pcatid,
                                   const GUID **ppcatidList, ULONG ulCount)
  {
    NS_NOTREACHED("ITfCategoryMgr::FindClosestCategory");
    return E_NOTIMPL;
  }

  STDMETHODIMP RegisterGUIDDescription(REFCLSID rclsid, REFGUID rguid,
                                       const WCHAR *pchDesc, ULONG cch)
  {
    NS_NOTREACHED("ITfCategoryMgr::RegisterGUIDDescription");
    return E_NOTIMPL;
  }

  STDMETHODIMP UnregisterGUIDDescription(REFCLSID rclsid, REFGUID rguid)
  {
    NS_NOTREACHED("ITfCategoryMgr::UnregisterGUIDDescription");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetGUIDDescription(REFGUID rguid, BSTR *pbstrDesc)
  {
    NS_NOTREACHED("ITfCategoryMgr::GetGUIDDescription");
    return E_NOTIMPL;
  }

  STDMETHODIMP RegisterGUIDDWORD(REFCLSID rclsid, REFGUID rguid, DWORD dw)
  {
    NS_NOTREACHED("ITfCategoryMgr::RegisterGUIDDWORD");
    return E_NOTIMPL;
  }

  STDMETHODIMP UnregisterGUIDDWORD(REFCLSID rclsid, REFGUID rguid)
  {
    NS_NOTREACHED("ITfCategoryMgr::UnregisterGUIDDWORD");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetGUIDDWORD(REFGUID rguid, DWORD *pdw)
  {
    NS_NOTREACHED("ITfCategoryMgr::GetGUIDDWORD");
    return E_NOTIMPL;
  }

  STDMETHODIMP RegisterGUID(REFGUID rguid, TfGuidAtom *pguidatom)
  {
    NS_NOTREACHED("ITfCategoryMgr::RegisterGUID");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetGUID(TfGuidAtom guidatom, GUID *pguid)
  {
    if (guidatom == GUID_ATOM_COMPOSING_SELECTION_ATTR) {
      *pguid = GUID_COMPOSING_SELECTION_ATTR;
      return S_OK;
    }
    NS_NOTREACHED("ITfCategoryMgr::GetGUID");
    return E_FAIL;
  }

  STDMETHODIMP IsEqualTfGuidAtom(TfGuidAtom guidatom, REFGUID rguid,
                                 BOOL *pfEqual)
  {
    NS_NOTREACHED("ITfCategoryMgr::IsEqualTfGuidAtom");
    return E_NOTIMPL;
  }

public: // ITfDisplayAttributeMgr

  STDMETHODIMP OnUpdateInfo()
  {
    NS_NOTREACHED("ITfDisplayAttributeMgr::OnUpdateInfo");
    return E_NOTIMPL;
  }

  STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo **ppEnum)
  {
    NS_NOTREACHED("ITfDisplayAttributeMgr::EnumDisplayAttributeInfo");
    return E_NOTIMPL;
  }

  STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid,
                                       ITfDisplayAttributeInfo **ppInfo,
                                       CLSID *pclsidOwner)
  {
    NS_ENSURE_TRUE(ppInfo, E_INVALIDARG);
    NS_ENSURE_TRUE(!pclsidOwner, E_INVALIDARG);
    if (guid == GUID_COMPOSING_SELECTION_ATTR) {
      (*ppInfo) = new TSFDispAttrInfoImpl(guid);
      (*ppInfo)->AddRef();
      return S_OK;
    }
    NS_NOTREACHED("ITfDisplayAttributeMgr::GetDisplayAttributeInfo");
    return E_FAIL;
  }

public:

  ITextStoreACP* GetFocusedStore()
  {
    return mFocusedDocument ? mFocusedDocument->mStore : nsnull;
  }

  TSFContextImpl* GetFocusedContext()
  {
    return mFocusedDocument ? mFocusedDocument->mContextBase : nsnull;
  }

  TSFAttrPropImpl* GetFocusedAttrProp()
  {
    TSFContextImpl* context = GetFocusedContext();
    return context ? context->mAttrProp : nsnull;
  }

};

STDMETHODIMP
TSFContextImpl::OnLockGranted(DWORD dwLockFlags)
{
  // If we have a test, run it
  if (mDocMgr->mMgr->mTest &&
     !((*mDocMgr->mMgr->mTestApp).*(mDocMgr->mMgr->mTest))())
    return S_FALSE;
  return S_OK;
}


STDMETHODIMP_(ULONG)
TSFDocumentMgrImpl::Release(void)
{
  --mRefCnt;
  if (mRefCnt == 1 && mMgr->mFocusedDocument == this) {
    mMgr->mFocusedDocument = nsnull;
    --mRefCnt;
  }
  if (mRefCnt) return mRefCnt;
  delete this;
  return 0;
}

NS_IMPL_ISUPPORTS2(TestApp, nsIWebProgressListener,
                            nsISupportsWeakReference)

nsresult
TestApp::Run(void)
{
  // Create a test window
  // We need a full-fledged window to test for TSF functionality
  nsresult rv;
  mAppShell = do_GetService(kAppShellCID);
  NS_ENSURE_TRUE(mAppShell, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIAppShellService> appShellService(
      do_GetService(NS_APPSHELLSERVICE_CONTRACTID));
  NS_ENSURE_TRUE(appShellService, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIURI> uri;
  rv = NS_NewURI(getter_AddRefs(uri), "about:blank", nsnull);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = appShellService->CreateTopLevelWindow(nsnull, uri,
                           nsIWebBrowserChrome::CHROME_DEFAULT,
                           800 /*nsIAppShellService::SIZE_TO_CONTENT*/,
                           600 /*nsIAppShellService::SIZE_TO_CONTENT*/,
                           mAppShell, getter_AddRefs(mWindow));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDocShell> docShell;
  rv = mWindow->GetDocShell(getter_AddRefs(docShell));
  NS_ENSURE_TRUE(docShell, NS_ERROR_UNEXPECTED);
  nsCOMPtr<nsIWebProgress> progress(do_GetInterface(docShell));
  NS_ENSURE_TRUE(progress, NS_ERROR_UNEXPECTED);
  rv = progress->AddProgressListener(this,
                                     nsIWebProgress::NOTIFY_STATE_WINDOW |
                                         nsIWebProgress::NOTIFY_STATUS);
  NS_ENSURE_SUCCESS(rv, rv);

  mAppShell->Run();
  return NS_OK;
}

PRBool
TestApp::CheckFailed(void)
{
  // All windows should be closed by now
  if (mMgr && !mMgr->mDeactivated) {
    fail("TSF not terminated properly");
    mFailed = PR_TRUE;
  }
  mMgr = nsnull;
  return mFailed;
}

nsresult
TestApp::Init(void)
{
  // Replace TSF manager pointer, category manager pointer and display
  // attribute manager pointer.
  nsCOMPtr<nsIBaseWindow> baseWindow(do_QueryInterface(mWindow));
  NS_ENSURE_TRUE(baseWindow, NS_ERROR_UNEXPECTED);
  nsCOMPtr<nsIWidget> widget;
  nsresult rv = baseWindow->GetMainWidget(getter_AddRefs(widget));
  NS_ENSURE_TRUE(widget, NS_ERROR_UNEXPECTED);

  ITfThreadMgr **threadMgr = reinterpret_cast<ITfThreadMgr**>(
      widget->GetNativeData(NS_NATIVE_TSF_THREAD_MGR));
  if (!threadMgr) {
    fail("nsIWidget::GetNativeData(NS_NATIVE_TSF_THREAD_MGR) not supported");
    return NS_ERROR_FAILURE;
  }
  if (*threadMgr) {
    (*threadMgr)->Deactivate();
    (*threadMgr)->Release();
    (*threadMgr) = NULL;
  } else {
    // This is only for information. The test does not need TSF to run.
    printf("TSF not initialized properly (TSF is not enabled/installed?)\n");
  }

  ITfCategoryMgr **catMgr = reinterpret_cast<ITfCategoryMgr**>(
      widget->GetNativeData(NS_NATIVE_TSF_CATEGORY_MGR));
  if (*catMgr) {
    (*catMgr)->Release();
    (*catMgr) = NULL;
  }
  ITfDisplayAttributeMgr **daMgr = reinterpret_cast<ITfDisplayAttributeMgr**>(
      widget->GetNativeData(NS_NATIVE_TSF_DISPLAY_ATTR_MGR));
  if (*daMgr) {
    (*daMgr)->Release();
    (*daMgr) = NULL;
  }

  mMgr = new TSFMgrImpl(this);
  if (!mMgr) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  (*threadMgr) = mMgr;
  (*threadMgr)->AddRef();
  (*catMgr) = mMgr;
  (*catMgr)->AddRef();
  (*daMgr) = mMgr;
  (*daMgr)->AddRef();

  // Apply the change
  reinterpret_cast<ITfThreadMgr**>(
      widget->GetNativeData(NS_NATIVE_TSF_THREAD_MGR));

  // Create a couple of text boxes for testing
  nsCOMPtr<nsIDOMWindowInternal> win(do_GetInterface(mWindow));
  NS_ENSURE_TRUE(win, NS_ERROR_UNEXPECTED);
  nsCOMPtr<nsIDOMDocument> document;
  rv = win->GetDocument(getter_AddRefs(document));
  NS_ENSURE_TRUE(document, NS_ERROR_UNEXPECTED);
  nsCOMPtr<nsIDOMHTMLDocument> htmlDoc(do_QueryInterface(document));
  NS_ENSURE_TRUE(htmlDoc, NS_ERROR_UNEXPECTED);
  nsCOMPtr<nsIDOMHTMLElement> htmlBody;
  rv = htmlDoc->GetBody(getter_AddRefs(htmlBody));
  NS_ENSURE_TRUE(htmlBody, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIDOMElement> form;
  rv = htmlDoc->CreateElementNS(
                     NS_LITERAL_STRING("http://www.w3.org/1999/xhtml"),
                     NS_LITERAL_STRING("form"),
                     getter_AddRefs(form));
  nsCOMPtr<nsIDOMElement> elem;
  rv = htmlDoc->CreateElementNS(
                     NS_LITERAL_STRING("http://www.w3.org/1999/xhtml"),
                     NS_LITERAL_STRING("input"),
                     getter_AddRefs(elem));
  NS_ENSURE_SUCCESS(rv, rv);
  elem->SetAttribute(NS_LITERAL_STRING("type"),
                      NS_LITERAL_STRING("text"));
  mInput = do_QueryInterface(elem);
  NS_ENSURE_TRUE(mInput, NS_ERROR_UNEXPECTED);
  rv = htmlDoc->CreateElementNS(
                     NS_LITERAL_STRING("http://www.w3.org/1999/xhtml"),
                     NS_LITERAL_STRING("textarea"),
                     getter_AddRefs(elem));
  NS_ENSURE_SUCCESS(rv, rv);
  mTextArea = do_QueryInterface(elem);
  NS_ENSURE_TRUE(mTextArea, NS_ERROR_UNEXPECTED);
  rv = htmlDoc->CreateElementNS(
                     NS_LITERAL_STRING("http://www.w3.org/1999/xhtml"),
                     NS_LITERAL_STRING("input"),
                     getter_AddRefs(elem));
  NS_ENSURE_SUCCESS(rv, rv);
  elem->SetAttribute(NS_LITERAL_STRING("type"),
                     NS_LITERAL_STRING("button"));
  mButton = do_QueryInterface(elem);
  NS_ENSURE_TRUE(mButton, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIDOMNode> node;
  rv = form->AppendChild(mInput, getter_AddRefs(node));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = form->AppendChild(mTextArea, getter_AddRefs(node));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = form->AppendChild(mButton, getter_AddRefs(node));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = htmlBody->AppendChild(form, getter_AddRefs(node));
  NS_ENSURE_SUCCESS(rv, rv);

  // set a background color manually,
  // otherwise the window might be transparent
  nsCOMPtr<nsIDOMHTMLBodyElement>(do_QueryInterface(htmlBody))->
      SetBgColor(NS_LITERAL_STRING("white"));

  widget->Show(PR_TRUE);
  widget->SetFocus();
  return NS_OK;
}

nsresult
TestApp::Term(void)
{
  mCurrentNode = nsnull;
  mInput = nsnull;
  mTextArea = nsnull;
  mButton = nsnull;

  nsCOMPtr<nsIDOMWindowInternal> win(do_GetInterface(mWindow));
  if (win)
    win->Close();
  win = nsnull;
  mWindow = nsnull;

  if (mAppShell)
    mAppShell->Exit();
  mAppShell = nsnull;
  return NS_OK;
}

PRBool
TestApp::RunTest(test_type aTest, PRBool aLock)
{
  PRBool succeeded;
  if (aLock && mMgr && mMgr->GetFocusedStore()) {
    mMgr->mTest = aTest;
    HRESULT hr = E_FAIL;
    mMgr->GetFocusedStore()->RequestLock(TS_LF_READWRITE | TS_LF_SYNC, &hr);
    succeeded = hr == S_OK;
  } else {
    succeeded = (this->*aTest)();
  }
  mFailed |= !succeeded;
  return succeeded;
}

NS_IMETHODIMP
TestApp::OnStateChange(nsIWebProgress *aWebProgress,
                        nsIRequest *aRequest,
                        PRUint32 aStateFlags,
                        nsresult aStatus)
{
  NS_ASSERTION(aStateFlags & nsIWebProgressListener::STATE_IS_WINDOW &&
              aStateFlags & nsIWebProgressListener::STATE_STOP, "wrong state");
  if (NS_SUCCEEDED(Init())) {
    mCurrentNode = mTextArea;
    mTextArea->Focus();

    if (RunTest(&TestApp::TestEditMessages))
      passed("TestEditMessages");
    if (RunTest(&TestApp::TestScrollMessages))
      passed("TestScrollMessages");

    if (RunTest(&TestApp::TestFocus, PR_FALSE))
      passed("TestFocus");

    mCurrentNode = mInput;
    mInput->Focus();
    if (mMgr->GetFocusedStore()) {
      if (RunTest(&TestApp::TestClustering))
        passed("TestClustering");
    } else {
      fail("no text store (clustering)");
      mFailed = PR_TRUE;
    }

    printf("Testing TSF support in text input element...\n");
    mCurrentNode = mInput;
    mTestString = NS_LITERAL_STRING(
      "This is a test of the Text Services Framework implementation.");
    mInput->SetValue(mTestString);
    mInput->Focus();
    if (mMgr->GetFocusedStore()) {
      if (RunTest(&TestApp::TestSelection))
        passed("TestSelection (input)");
      if (RunTest(&TestApp::TestText))
        passed("TestText (input)");
      if (RunTest(&TestApp::TestExtents))
        passed("TestExtents (input)");
      if (RunTest(&TestApp::TestComposition))
        passed("TestComposition (input)");
      if (RunTest(&TestApp::TestNotification, PR_FALSE))
        passed("TestNotification (input)");
    } else {
      fail("no text store (input)");
      mFailed = PR_TRUE;
    }

    printf("Testing TSF support in textarea element...\n");
    mCurrentNode = mTextArea;
    mTestString = NS_LITERAL_STRING(
      "This is a test of the\r\nText Services Framework\r\nimplementation.");
    mTextArea->SetValue(mTestString);
    mTextArea->Focus();
    if (mMgr->GetFocusedStore()) {
      if (RunTest(&TestApp::TestSelection))
        passed("TestSelection (textarea)");
      if (RunTest(&TestApp::TestText))
        passed("TestText (textarea)");
      if (RunTest(&TestApp::TestExtents))
        passed("TestExtents (textarea)");
      if (RunTest(&TestApp::TestComposition))
        passed("TestComposition (textarea)");
      if (RunTest(&TestApp::TestNotification, PR_FALSE))
        passed("TestNotification (textarea)");
    } else {
      fail("no text store (textarea)");
      mFailed = PR_TRUE;
    }
  } else {
    fail("initialization");
    mFailed = PR_TRUE;
  }
  Term();
  return NS_OK;
}

PRBool
TestApp::TestFocus(void)
{
  PRUint32 focus = mMgr->mFocusCount;
  nsresult rv;

  /* If these fail the cause is probably one or more of:
   * - nsIMEStateManager::OnTextStateFocus not called by nsEventStateManager
   * - nsIMEStateManager::OnTextStateBlur not called by nsEventStateManager
   * - nsWindow::OnIMEFocusChange (nsIWidget) not called by nsIMEStateManager
   * - nsTextStore::Create/Focus/Destroy not called by nsWindow
   * - ITfThreadMgr::CreateDocumentMgr/SetFocus not called by nsTextStore
   * - ITfDocumentMgr::CreateContext/Push not called by nsTextStore
   */

  rv = mInput->Focus();
  if (!(NS_SUCCEEDED(rv) &&
        mMgr->mFocusedDocument &&
        mMgr->mFocusCount - focus == 1 &&
        mMgr->GetFocusedStore())) {
    fail("TestFocus: document focus was not set");
    return PR_FALSE;
  }

  rv = mTextArea->Focus();
  if (!(NS_SUCCEEDED(rv) &&
        mMgr->mFocusedDocument &&
        mMgr->mFocusCount - focus == 2 &&
        mMgr->GetFocusedStore())) {
    fail("TestFocus: document focus was not changed");
    return PR_FALSE;
  }

  rv = mButton->Focus();
  if (!(NS_SUCCEEDED(rv) &&
        !mMgr->mFocusedDocument &&
        mMgr->mFocusCount - focus == 2 &&
        !mMgr->GetFocusedStore())) {
    fail("TestFocus: document focus was changed");
    return PR_FALSE;
  }
  return PR_TRUE;
}

PRBool
TestApp::TestClustering(void)
{
  // Text for testing
  const PRUint32 STRING_LENGTH = 2;
  PRUnichar string[3];
  string[0] = 'e';
  string[1] = 0x0301; // U+0301 'acute accent'
  string[2] = nsnull;

  if (!mMgr->GetFocusedStore()) {
    fail("TestClustering: GetFocusedStore returns null #1");
    return PR_FALSE;
  }

  // Replace entire string with our string
  TS_TEXTCHANGE textChange;
  HRESULT hr =
    mMgr->GetFocusedStore()->SetText(0, 0, -1, string, STRING_LENGTH,
                                     &textChange);
  if (!(SUCCEEDED(hr) &&
        0 == textChange.acpStart &&
        STRING_LENGTH == textChange.acpNewEnd)) {
    fail("TestClustering: SetText");
    return PR_FALSE;
  }

  TsViewCookie view;
  RECT rectLetter, rectAccent, rectWhole, rectCombined;
  BOOL clipped, nonEmpty;

  if (!mMgr->GetFocusedStore()) {
    fail("TestClustering: GetFocusedStore returns null #2");
    return PR_FALSE;
  }

  hr = mMgr->GetFocusedStore()->GetActiveView(&view);
  if (!(SUCCEEDED(hr))) {
    fail("TestClustering: GetActiveView");
    return PR_FALSE;
  }

  if (!mMgr->GetFocusedStore()) {
    fail("TestClustering: GetFocusedStore returns null #3");
    return PR_FALSE;
  }

  // Get rect of first char (the letter)
  hr = mMgr->GetFocusedStore()->GetTextExt(view, 0, STRING_LENGTH / 2,
                                           &rectLetter, &clipped);
  if (!(SUCCEEDED(hr))) {
    fail("TestClustering: GetTextExt (letter)");
    return PR_FALSE;
  }

  if (!mMgr->GetFocusedStore()) {
    fail("TestClustering: GetFocusedStore returns null #4");
    return PR_FALSE;
  }

  // Get rect of second char (the accent)
  hr = mMgr->GetFocusedStore()->GetTextExt(view, STRING_LENGTH / 2,
                                           STRING_LENGTH,
                                           &rectAccent, &clipped);
  if (!(SUCCEEDED(hr))) {
    fail("TestClustering: GetTextExt (accent)");
    return PR_FALSE;
  }

  if (!mMgr->GetFocusedStore()) {
    fail("TestClustering: GetFocusedStore returns null #5");
    return PR_FALSE;
  }

  // Get rect of combined char
  hr = mMgr->GetFocusedStore()->GetTextExt(view, 0, STRING_LENGTH,
                                           &rectWhole, &clipped);
  if (!(SUCCEEDED(hr))) {
    fail("TestClustering: GetTextExt (whole)");
    return PR_FALSE;
  }

  nonEmpty = ::UnionRect(&rectCombined, &rectLetter, &rectAccent);
  if (!(nonEmpty &&
        ::EqualRect(&rectCombined, &rectWhole))) {
    fail("TestClustering: unexpected combined rect");
    return PR_FALSE;
  }
  return PR_TRUE;
}

PRBool
TestApp::TestSelectionInternal(char* aTestName,
                               LONG aStart,
                               LONG aEnd,
                               TsActiveSelEnd aSelEnd)
{
  PRBool succeeded = PR_TRUE, continueTest = PR_TRUE;
  TS_SELECTION_ACP sel, testSel;
  ULONG selFetched;

  if (!mMgr->GetFocusedStore()) {
    fail("TestSelectionInternal: GetFocusedStore returns null #1");
    return PR_FALSE;
  }

  sel.acpStart = aStart;
  sel.acpEnd = aEnd;
  sel.style.ase = aSelEnd;
  sel.style.fInterimChar = FALSE;
  HRESULT hr = mMgr->GetFocusedStore()->SetSelection(1, &sel);
  if (!(SUCCEEDED(hr))) {
    fail("TestSelection: SetSelection (%s)", aTestName);
    continueTest = succeeded = PR_FALSE;
  }

  if (continueTest) {
    if (!mMgr->GetFocusedStore()) {
      fail("TestSelectionInternal: GetFocusedStore returns null #2");
      return PR_FALSE;
    }

    hr = mMgr->GetFocusedStore()->GetSelection(TS_DEFAULT_SELECTION, 1,
                                               &testSel, &selFetched);
    if (!(SUCCEEDED(hr) &&
          selFetched == 1 &&
          !memcmp(&sel, &testSel, sizeof(sel)))) {
      fail("TestSelection: unexpected GetSelection result (%s)", aTestName);
      succeeded = PR_FALSE;
    }
  }
  return succeeded;
}

PRBool
TestApp::TestSelection(void)
{
  PRBool succeeded = PR_TRUE;

  /* If these fail the cause is probably one or more of:
   * nsTextStore::GetSelection not sending NS_QUERY_SELECTED_TEXT
   * NS_QUERY_SELECTED_TEXT not handled by nsContentEventHandler
   * Bug in NS_QUERY_SELECTED_TEXT handler
   * nsTextStore::SetSelection not sending NS_SELECTION_SET
   * NS_SELECTION_SET not handled by nsContentEventHandler
   * Bug in NS_SELECTION_SET handler
   */

  TS_SELECTION_ACP testSel;
  ULONG selFetched;

  if (!mMgr->GetFocusedStore()) {
    fail("TestSelection: GetFocusedStore returns null");
    return PR_FALSE;
  }

  HRESULT hr =
    mMgr->GetFocusedStore()->GetSelection(0, 1, &testSel, &selFetched);
  if (!(SUCCEEDED(hr) &&
        selFetched == 1)) {
    fail("TestSelection: GetSelection");
    succeeded = PR_FALSE;
  }

  const LONG SELECTION1_START            = 0;
  const LONG SELECTION1_END              = mTestString.Length();
  const TsActiveSelEnd SELECTION1_SELEND = TS_AE_END;

  if (!TestSelectionInternal("normal",
                             SELECTION1_START,
                             SELECTION1_END,
                             SELECTION1_SELEND)) {
    succeeded = PR_FALSE;
  }

  const LONG SELECTION2_START            = mTestString.Length() / 2;
  const LONG SELECTION2_END              = SELECTION2_START;
  const TsActiveSelEnd SELECTION2_SELEND = TS_AE_END;

  if (!TestSelectionInternal("collapsed",
                             SELECTION2_START,
                             SELECTION2_END,
                             SELECTION2_SELEND)) {
    succeeded = PR_FALSE;
  }

  const LONG SELECTION3_START            = 12;
  const LONG SELECTION3_END              = mTestString.Length() - 20;
  const TsActiveSelEnd SELECTION3_SELEND = TS_AE_START;

  if (!TestSelectionInternal("reversed",
                             SELECTION3_START,
                             SELECTION3_END,
                             SELECTION3_SELEND)) {
    succeeded = PR_FALSE;
  }
  return succeeded;
}

PRBool
TestApp::TestText(void)
{
  const PRUint32 BUFFER_SIZE  = (0x100);
  const PRUint32 RUNINFO_SIZE = (0x10);

  PRBool succeeded = PR_TRUE, continueTest;
  PRUnichar buffer[BUFFER_SIZE];
  TS_RUNINFO runInfo[RUNINFO_SIZE];
  ULONG bufferRet, runInfoRet;
  LONG acpRet, acpCurrent;
  TS_TEXTCHANGE textChange;
  HRESULT hr;

  /* If these fail the cause is probably one or more of:
   * nsTextStore::GetText not sending NS_QUERY_TEXT_CONTENT
   * NS_QUERY_TEXT_CONTENT not handled by nsContentEventHandler
   * Bug in NS_QUERY_TEXT_CONTENT handler
   * nsTextStore::SetText not calling SetSelection or InsertTextAtSelection
   * Bug in SetSelection or InsertTextAtSelection
   *  NS_SELECTION_SET bug or NS_COMPOSITION_* / NS_TEXT_TEXT bug
   */

  if (!mMgr->GetFocusedStore()) {
    fail("TestText: GetFocusedStore returns null #1");
    return PR_FALSE;
  }

  // Get all text
  hr = mMgr->GetFocusedStore()->GetText(0, -1, buffer, BUFFER_SIZE, &bufferRet,
                                        runInfo, RUNINFO_SIZE, &runInfoRet,
                                        &acpRet);
  if (!(SUCCEEDED(hr) &&
        bufferRet <= mTestString.Length() &&
        !wcsncmp(mTestString.get(), buffer, bufferRet) &&
        acpRet == LONG(bufferRet) &&
        runInfoRet > 0)) {
    fail("TestText: GetText 1");
    succeeded = PR_FALSE;
  }


  if (!mMgr->GetFocusedStore()) {
    fail("TestText: GetFocusedStore returns null #2");
    return PR_FALSE;
  }

  // Get text from GETTEXT2_START to GETTEXT2_END
  const PRUint32 GETTEXT2_START       = (18);
  const PRUint32 GETTEXT2_END         = (mTestString.Length() - 16);
  const PRUint32 GETTEXT2_BUFFER_SIZE = (0x10);

  hr = mMgr->GetFocusedStore()->GetText(GETTEXT2_START, GETTEXT2_END,
                                        buffer, GETTEXT2_BUFFER_SIZE,
                                        &bufferRet, runInfo, RUNINFO_SIZE,
                                        &runInfoRet, &acpRet);
  if (!(SUCCEEDED(hr) &&
        bufferRet <= GETTEXT2_BUFFER_SIZE &&
        !wcsncmp(mTestString.get() + GETTEXT2_START, buffer, bufferRet) &&
        acpRet == LONG(bufferRet) + GETTEXT2_START &&
        runInfoRet > 0)) {
    fail("TestText: GetText 2");
    succeeded = PR_FALSE;
  }


  if (!mMgr->GetFocusedStore()) {
    fail("TestText: GetFocusedStore returns null #3");
    return PR_FALSE;
  }

  // Replace text from SETTEXT1_START to SETTEXT1_END with insertString
  const PRUint32 SETTEXT1_START        = (8);
  const PRUint32 SETTEXT1_TAIL_LENGTH  = (40);
  const PRUint32 SETTEXT1_END          = (mTestString.Length() -
                                          SETTEXT1_TAIL_LENGTH);
  NS_NAMED_LITERAL_STRING(insertString, "(Inserted string)");

  continueTest = PR_TRUE;
  hr = mMgr->GetFocusedStore()->SetText(0, SETTEXT1_START, SETTEXT1_END,
                                        insertString.get(),
                                        insertString.Length(), &textChange);
  if (!(SUCCEEDED(hr) &&
        textChange.acpStart == SETTEXT1_START &&
        textChange.acpOldEnd == LONG(SETTEXT1_END) &&
        textChange.acpNewEnd == LONG(SETTEXT1_START +
                                insertString.Length()))) {
    fail("TestText: SetText 1");
    continueTest = succeeded = PR_FALSE;
  }

  const PRUint32 SETTEXT1_FINAL_LENGTH = (SETTEXT1_START +
                                          SETTEXT1_TAIL_LENGTH +
                                          insertString.Length());

  if (continueTest) {
    acpCurrent = 0;
    while (acpCurrent < LONG(SETTEXT1_FINAL_LENGTH)) {
      if (!mMgr->GetFocusedStore()) {
        fail("TestText: GetFocusedStore returns null #4");
        return PR_FALSE;
      }

      hr = mMgr->GetFocusedStore()->GetText(acpCurrent, -1, &buffer[acpCurrent],
                                            BUFFER_SIZE, &bufferRet, runInfo,
                                            RUNINFO_SIZE, &runInfoRet, &acpRet);
      if (!(SUCCEEDED(hr) &&
            acpRet > acpCurrent &&
            bufferRet <= SETTEXT1_FINAL_LENGTH &&
            runInfoRet > 0)) {
        fail("TestText: GetText failed after SetTest 1");
        continueTest = succeeded = PR_FALSE;
        break;
      }
      acpCurrent = acpRet;
    }
  }

  if (continueTest) {
    if (!(acpCurrent == LONG(SETTEXT1_FINAL_LENGTH) &&
          !wcsncmp(buffer, mTestString.get(), SETTEXT1_START) &&
          !wcsncmp(&buffer[SETTEXT1_START], insertString.get(),
                   insertString.Length()) &&
          !wcsncmp(&buffer[SETTEXT1_START + insertString.Length()],
                   mTestString.get() + SETTEXT1_END, SETTEXT1_TAIL_LENGTH))) {
      fail("TestText: unexpected GetText result after SetText 1");
      succeeded = PR_FALSE;
    }
  }


  if (!mMgr->GetFocusedStore()) {
    fail("TestText: GetFocusedStore returns null #5");
    return PR_FALSE;
  }

  // Restore entire text to original text (mTestString)
  continueTest = PR_TRUE;
  hr = mMgr->GetFocusedStore()->SetText(0, 0, -1, mTestString.get(),
                                        mTestString.Length(), &textChange);
  if (!(SUCCEEDED(hr) &&
        textChange.acpStart == 0 &&
        textChange.acpOldEnd == LONG(SETTEXT1_FINAL_LENGTH) &&
        textChange.acpNewEnd == LONG(mTestString.Length()))) {
    fail("TestText: SetText 2");
    continueTest = succeeded = PR_FALSE;
  }

  if (continueTest) {
    acpCurrent = 0;
    while (acpCurrent < LONG(mTestString.Length())) {
      if (!mMgr->GetFocusedStore()) {
        fail("TestText: GetFocusedStore returns null #6");
        return PR_FALSE;
      }

      hr = mMgr->GetFocusedStore()->GetText(acpCurrent, -1, &buffer[acpCurrent],
                                            BUFFER_SIZE, &bufferRet, runInfo,
                                            RUNINFO_SIZE, &runInfoRet, &acpRet);
      if (!(SUCCEEDED(hr) &&
            acpRet > acpCurrent &&
            bufferRet <= mTestString.Length() &&
            runInfoRet > 0)) {
        fail("TestText: GetText failed after SetText 2");
        continueTest = succeeded = PR_FALSE;
        break;
      }
      acpCurrent = acpRet;
    }
  }

  if (continueTest) {
    if (!(acpCurrent == LONG(mTestString.Length()) &&
          !wcsncmp(buffer, mTestString.get(), mTestString.Length()))) {
      fail("TestText: unexpected GetText result after SetText 2");
      succeeded = PR_FALSE;
    }
  }
  return succeeded;
}

PRBool
TestApp::TestExtents(void)
{
  if (!mMgr->GetFocusedStore()) {
    fail("TestExtents: GetFocusedStore returns null #1");
    return PR_FALSE;
  }

  TS_SELECTION_ACP sel;
  sel.acpStart = 0;
  sel.acpEnd = 0;
  sel.style.ase = TS_AE_END;
  sel.style.fInterimChar = FALSE;
  mMgr->GetFocusedStore()->SetSelection(1, &sel);

  nsCOMPtr<nsISelectionController> selCon;
  if (!(NS_SUCCEEDED(GetSelCon(getter_AddRefs(selCon))) && selCon)) {
    fail("TestExtents: get nsISelectionController");
    return PR_FALSE;
  }
  selCon->ScrollSelectionIntoView(nsISelectionController::SELECTION_NORMAL,
              nsISelectionController::SELECTION_FOCUS_REGION, PR_TRUE);

  nsCOMPtr<nsIDOMWindowInternal> window(do_GetInterface(mWindow));
  if (!window) {
    fail("TestExtents: get nsIDOMWindowInternal");
    return PR_FALSE;
  }
  RECT windowRect, screenRect, textRect1, textRect2;
  BOOL clipped;
  PRInt32 val;
  TsViewCookie view;
  HRESULT hr;

  nsresult nsr = window->GetScreenX(&val);
  windowRect.left = val;
  nsr |= window->GetScreenY(&val);
  windowRect.top = val;
  nsr |= window->GetOuterWidth(&val);
  windowRect.right = windowRect.left + val;
  nsr |= window->GetOuterHeight(&val);
  windowRect.bottom = windowRect.top + val;
  if (!(NS_SUCCEEDED(nsr))) {
    fail("TestExtents: get window rect failed");
    return PR_FALSE;
  }

  if (!mMgr->GetFocusedStore()) {
    fail("TestExtents: GetFocusedStore returns null #2");
    return PR_FALSE;
  }

  hr = mMgr->GetFocusedStore()->GetActiveView(&view);
  if (!(SUCCEEDED(hr))) {
    fail("TestExtents: GetActiveView");
    return PR_FALSE;
  }

  if (!mMgr->GetFocusedStore()) {
    fail("TestExtents: GetFocusedStore returns null #3");
    return PR_FALSE;
  }

  PRBool succeeded = PR_TRUE;
  HWND hwnd;
  hr = mMgr->GetFocusedStore()->GetWnd(view, &hwnd);
  if (!(SUCCEEDED(hr) &&
        ::IsWindow(hwnd))) {
    fail("TestExtents: GetWnd");
    succeeded = PR_FALSE;
  }

  ::SetRectEmpty(&screenRect);

  if (!mMgr->GetFocusedStore()) {
    fail("TestExtents: GetFocusedStore returns null #4");
    return PR_FALSE;
  }

  hr = mMgr->GetFocusedStore()->GetScreenExt(view, &screenRect);
  if (!(SUCCEEDED(hr) &&
        screenRect.left > windowRect.left &&
        screenRect.top > windowRect.top &&
        screenRect.right > screenRect.left &&
        screenRect.bottom > screenRect.top &&
        screenRect.right < windowRect.right &&
        screenRect.bottom < windowRect.bottom)) {
    fail("TestExtents: GetScreenExt");
    succeeded = PR_FALSE;
  }

  const LONG GETTEXTEXT1_START = 0;
  const LONG GETTEXTEXT1_END   = 0;

  ::SetRectEmpty(&textRect1);

  if (!mMgr->GetFocusedStore()) {
    fail("TestExtents: GetFocusedStore returns null #5");
    return PR_FALSE;
  }

  hr = mMgr->GetFocusedStore()->GetTextExt(view, GETTEXTEXT1_START,
                                           GETTEXTEXT1_END, &textRect1,
                                           &clipped);
  if (!(SUCCEEDED(hr) &&
        textRect1.left >= screenRect.left &&
        textRect1.top >= screenRect.top &&
        textRect1.right < screenRect.right &&
        textRect1.bottom <= screenRect.bottom &&
        textRect1.right >= textRect1.left &&
        textRect1.bottom > textRect1.top)) {
    fail("TestExtents: GetTextExt (offset %ld to %ld)",
         GETTEXTEXT1_START, GETTEXTEXT1_END);
    succeeded = PR_FALSE;
  }

  const LONG GETTEXTEXT2_START = 10;
  const LONG GETTEXTEXT2_END   = 25;

  ::SetRectEmpty(&textRect2);

  if (!mMgr->GetFocusedStore()) {
    fail("TestExtents: GetFocusedStore returns null #6");
    return PR_FALSE;
  }

  hr = mMgr->GetFocusedStore()->GetTextExt(view, GETTEXTEXT2_START,
                                           GETTEXTEXT2_END, &textRect2,
                                           &clipped);
  if (!(SUCCEEDED(hr) &&
        textRect2.left >= screenRect.left &&
        textRect2.top >= screenRect.top &&
        textRect2.right <= screenRect.right &&
        textRect2.bottom <= screenRect.bottom &&
        textRect2.right > textRect2.left &&
        textRect2.bottom > textRect2.top)) {
    fail("TestExtents: GetTextExt (offset %ld to %ld)",
         GETTEXTEXT2_START, GETTEXTEXT2_END);
    succeeded = PR_FALSE;
  }

  // Offsets must be between GETTEXTEXT2_START and GETTEXTEXT2_END
  const LONG GETTEXTEXT3_START = 23;
  const LONG GETTEXTEXT3_END   = 23;

  ::SetRectEmpty(&textRect1);

  if (!mMgr->GetFocusedStore()) {
    fail("TestExtents: GetFocusedStore returns null #7");
    return PR_FALSE;
  }

  hr = mMgr->GetFocusedStore()->GetTextExt(view, GETTEXTEXT3_START,
                                           GETTEXTEXT3_END, &textRect1,
                                           &clipped);
  // Rectangle must be entirely inside the previous rectangle,
  // since GETTEXTEXT3_START and GETTEXTEXT3_END are between
  // GETTEXTEXT2_START and GETTEXTEXT2_START
  if (!(SUCCEEDED(hr) && ::IsRectEmpty(&textRect1) ||
        (textRect1.left >= textRect2.left &&
        textRect1.top >= textRect2.top &&
        textRect1.right <= textRect2.right &&
        textRect1.bottom <= textRect2.bottom &&
        textRect1.right >= textRect1.left &&
        textRect1.bottom > textRect1.top))) {
    fail("TestExtents: GetTextExt (offset %ld to %ld)",
         GETTEXTEXT3_START, GETTEXTEXT3_END);
    succeeded = PR_FALSE;
  }
  return succeeded;
}

PRBool
TestApp::TestCompositionSelectionAndText(char* aTestName,
                                         LONG aExpectedSelStart,
                                         LONG aExpectedSelEnd,
                                         nsString& aReferenceString)
{
  if (!mMgr->GetFocusedStore()) {
    fail("TestCompositionSelectionAndText: GetFocusedStore returns null #1");
    return PR_FALSE;
  }

  TS_SELECTION_ACP currentSel;
  ULONG selFetched = 0;
  HRESULT hr = mMgr->GetFocusedStore()->GetSelection(TF_DEFAULT_SELECTION, 1,
                                                     &currentSel, &selFetched);
  if (!(SUCCEEDED(hr) &&
        1 == selFetched &&
        currentSel.acpStart == aExpectedSelStart &&
        currentSel.acpEnd == aExpectedSelEnd)) {
    fail("TestComposition: GetSelection (%s)", aTestName);
    return PR_FALSE;
  }

  const PRUint32 bufferSize = 0x100, runInfoSize = 0x10;
  PRUnichar buffer[bufferSize];
  TS_RUNINFO runInfo[runInfoSize];
  ULONG bufferRet, runInfoRet;
  LONG acpRet, acpCurrent = 0;
  while (acpCurrent < LONG(aReferenceString.Length())) {
    if (!mMgr->GetFocusedStore()) {
      fail("TestCompositionSelectionAndText: GetFocusedStore returns null #2");
      return PR_FALSE;
    }

    hr = mMgr->GetFocusedStore()->GetText(acpCurrent, aReferenceString.Length(),
                                          &buffer[acpCurrent], bufferSize,
                                          &bufferRet, runInfo, runInfoSize,
                                          &runInfoRet, &acpRet);
    if (!(SUCCEEDED(hr) &&
          acpRet > acpCurrent &&
          bufferRet <= aReferenceString.Length() &&
          runInfoRet > 0)) {
      fail("TestComposition: GetText (%s)", aTestName);
      return PR_FALSE;
    }
    acpCurrent = acpRet;
  }
  if (!(acpCurrent == aReferenceString.Length() &&
        !wcsncmp(buffer, aReferenceString.get(), aReferenceString.Length()))) {
    fail("TestComposition: unexpected GetText result (%s)", aTestName);
    return PR_FALSE;
  }
  return PR_TRUE;
}

PRBool
TestApp::TestComposition(void)
{
  if (!mMgr->GetFocusedStore()) {
    fail("TestComposition: GetFocusedStore returns null #1");
    return PR_FALSE;
  }

  nsRefPtr<ITfContextOwnerCompositionSink> sink;
  HRESULT hr =
    mMgr->GetFocusedStore()->QueryInterface(IID_ITfContextOwnerCompositionSink,
                                            getter_AddRefs(sink));
  if (!(SUCCEEDED(hr))) {
    fail("TestComposition: QueryInterface");
    return PR_FALSE;
  }

  const LONG PRECOMPOSITION_SEL_START            = 2;
  const LONG PRECOMPOSITION_SEL_END              = PRECOMPOSITION_SEL_START;
  const TsActiveSelEnd PRECOMPOSITION_SEL_SELEND = TS_AE_END;

  TS_SELECTION_ACP sel;
  sel.acpStart = PRECOMPOSITION_SEL_START;
  sel.acpEnd = PRECOMPOSITION_SEL_END;
  sel.style.ase = PRECOMPOSITION_SEL_SELEND;
  sel.style.fInterimChar = FALSE;
  hr = mMgr->GetFocusedStore()->SetSelection(1, &sel);
  if (!(SUCCEEDED(hr))) {
    fail("TestComposition: SetSelection (pre-composition)");
    return PR_FALSE;
  }

  if (!mMgr->GetFocusedStore()) {
    fail("TestComposition: GetFocusedStore returns null #2");
    return PR_FALSE;
  }

  TS_TEXTCHANGE textChange;
  NS_NAMED_LITERAL_STRING(insertString1, "Compo1");
  hr = mMgr->GetFocusedStore()->InsertTextAtSelection(TF_IAS_NOQUERY,
                                                      insertString1.get(),
                                                      insertString1.Length(),
                                                      NULL, NULL, &textChange);
  if (!(SUCCEEDED(hr) &&
        sel.acpEnd == textChange.acpStart &&
        sel.acpEnd == textChange.acpOldEnd &&
        sel.acpEnd + insertString1.Length() == textChange.acpNewEnd)) {
    fail("TestComposition: InsertTextAtSelection");
    return PR_FALSE;
  }
  sel.acpEnd = textChange.acpNewEnd;

  if (!mMgr->GetFocusedAttrProp()) {
    fail("TestComposition: GetFocusedAttrProp returns null #1");
    return PR_FALSE;
  }
  mMgr->GetFocusedAttrProp()->mRanges.Clear();
  nsRefPtr<TSFRangeImpl> range =
    new TSFRangeImpl(textChange.acpStart,
                     textChange.acpNewEnd - textChange.acpOldEnd);
  if (!mMgr->GetFocusedAttrProp()) {
    fail("TestComposition: GetFocusedAttrProp returns null #2");
    return PR_FALSE;
  }
  mMgr->GetFocusedAttrProp()->mRanges.AppendElement(range);

  BOOL okay = FALSE;
  hr = sink->OnStartComposition(mMgr->GetFocusedContext(), &okay);
  if (!(SUCCEEDED(hr) &&
        okay)) {
    fail("TestComposition: OnStartComposition");
    return PR_FALSE;
  }


  if (!mMgr->GetFocusedStore()) {
    fail("TestComposition: GetFocusedStore returns null #3");
    return PR_FALSE;
  }

  NS_NAMED_LITERAL_STRING(insertString2, "Composition2");
  hr = mMgr->GetFocusedStore()->SetText(0, range->mStart + range->mLength,
                                        range->mStart + range->mLength,
                                        insertString2.get(),
                                        insertString2.Length(),
                                        &textChange);
  if (!(SUCCEEDED(hr) &&
        sel.acpEnd == textChange.acpStart &&
        sel.acpEnd == textChange.acpOldEnd &&
        sel.acpEnd + insertString2.Length() == textChange.acpNewEnd)) {
    fail("TestComposition: SetText 1");
    return PR_FALSE;
  }
  sel.acpEnd = textChange.acpNewEnd;
  range->mLength += textChange.acpNewEnd - textChange.acpOldEnd;


  if (!mMgr->GetFocusedStore()) {
    fail("TestComposition: GetFocusedStore returns null #4");
    return PR_FALSE;
  }

  const LONG COMPOSITION3_TEXT_START_OFFSET = -8; // offset 8 from the end
  const LONG COMPOSITION3_TEXT_END_OFFSET   = 4;

  const LONG COMPOSITION3_TEXT_START = range->mStart + range->mLength +
                                       COMPOSITION3_TEXT_START_OFFSET;
  const LONG COMPOSITION3_TEXT_END   = COMPOSITION3_TEXT_START +
                                       COMPOSITION3_TEXT_END_OFFSET;

  NS_NAMED_LITERAL_STRING(insertString3, "Compo3");
  hr = mMgr->GetFocusedStore()->SetText(0, COMPOSITION3_TEXT_START,
                                        COMPOSITION3_TEXT_END,
                                        insertString3.get(),
                                        insertString3.Length(),
                                        &textChange);
  if (!(SUCCEEDED(hr) &&
        sel.acpEnd + COMPOSITION3_TEXT_START_OFFSET == textChange.acpStart &&
        sel.acpEnd + COMPOSITION3_TEXT_START_OFFSET +
            COMPOSITION3_TEXT_END_OFFSET == textChange.acpOldEnd &&
        sel.acpEnd + insertString3.Length() + COMPOSITION3_TEXT_START_OFFSET ==
            textChange.acpNewEnd)) {
    fail("TestComposition: SetText 2");
    return PR_FALSE;
  }
  sel.acpEnd = textChange.acpNewEnd;
  range->mLength += textChange.acpNewEnd - textChange.acpOldEnd;


  nsString referenceString;
  referenceString.Append(mTestString.get(), sel.acpStart);
  referenceString.Append(insertString1);
  referenceString.Append(insertString2.get(),
      insertString2.Length() + COMPOSITION3_TEXT_START_OFFSET);
  referenceString.Append(insertString3);
  referenceString.Append(insertString2.get() + insertString2.Length() -
      COMPOSITION3_TEXT_END_OFFSET, COMPOSITION3_TEXT_END_OFFSET);
  referenceString.Append(mTestString.get() + sel.acpStart,
      COMPOSITION3_TEXT_END_OFFSET);

  if (!TestCompositionSelectionAndText("composition",
           sel.acpEnd, sel.acpEnd,
           referenceString))
    return PR_FALSE;


  if (!mMgr->GetFocusedStore()) {
    fail("TestComposition: GetFocusedStore returns null #5");
    return PR_FALSE;
  }

  const LONG POSTCOMPOSITION_SEL_START = sel.acpEnd - 8;
  const LONG POSTCOMPOSITION_SEL_END   = POSTCOMPOSITION_SEL_START + 2;

  sel.acpStart = POSTCOMPOSITION_SEL_START;
  sel.acpEnd = POSTCOMPOSITION_SEL_END;
  hr = mMgr->GetFocusedStore()->SetSelection(1, &sel);
  if (!(SUCCEEDED(hr))) {
    fail("TestComposition: SetSelection (composition)");
    return PR_FALSE;
  }

  if (!mMgr->GetFocusedAttrProp()) {
    fail("TestComposition: GetFocusedAttrProp returns null #3");
    return PR_FALSE;
  }
  mMgr->GetFocusedAttrProp()->mRanges.Clear();

  hr = sink->OnEndComposition(mMgr->GetFocusedContext());
  if (!(SUCCEEDED(hr))) {
    fail("TestComposition: OnEndComposition");
    return PR_FALSE;
  }

  if (!TestCompositionSelectionAndText("post-composition",
           sel.acpStart, sel.acpEnd,
           referenceString))
    return PR_FALSE;

  const LONG EMPTYCOMPOSITION_START  = range->mStart + 2;
  const LONG EMPTYCOMPOSITION_LENGTH = range->mLength - 4;

  range->mStart = EMPTYCOMPOSITION_START;
  range->mLength = EMPTYCOMPOSITION_LENGTH;
  if (!mMgr->GetFocusedAttrProp()) {
    fail("TestComposition: GetFocusedAttrProp returns null #4");
    return PR_FALSE;
  }
  mMgr->GetFocusedAttrProp()->mRanges.AppendElement(range);

  okay = FALSE;
  hr = sink->OnStartComposition(mMgr->GetFocusedContext(), &okay);
  if (!(SUCCEEDED(hr) &&
        okay)) {
    fail("TestComposition: OnStartComposition (empty composition)");
    return PR_FALSE;
  }

  if (!mMgr->GetFocusedAttrProp()) {
    fail("TestComposition: GetFocusedAttrProp returns null #5");
    return PR_FALSE;
  }
  mMgr->GetFocusedAttrProp()->mRanges.Clear();

  hr = sink->OnEndComposition(mMgr->GetFocusedContext());
  if (!(SUCCEEDED(hr))) {
    fail("TestComposition: OnEndComposition (empty composition)");
    return PR_FALSE;
  }

  if (!TestCompositionSelectionAndText("empty composition",
           range->mStart, range->mStart + range->mLength,
           referenceString))
    return PR_FALSE;

  return PR_TRUE;
}

PRBool
TestApp::TestNotificationTextChange(nsIWidget* aWidget,
                                    PRUint32 aCode,
                                    const nsAString& aCharacter,
                                    LONG aStart,
                                    LONG aOldEnd,
                                    LONG aNewEnd)
{
  MSG msg;
  if (::PeekMessageW(&msg, NULL, WM_USER_TSF_TEXTCHANGE,
                     WM_USER_TSF_TEXTCHANGE, PM_REMOVE))
    ::DispatchMessageW(&msg);
  if (!mMgr->GetFocusedContext()) {
    fail("TestNotificationTextChange: GetFocusedContext returns null");
    return PR_FALSE;
  }
  mMgr->GetFocusedContext()->mTextChanged = PR_FALSE;
  nsresult nsr = aWidget->SynthesizeNativeKeyEvent(0, aCode, 0,
                              aCharacter, aCharacter);
  if (::PeekMessageW(&msg, NULL, WM_USER_TSF_TEXTCHANGE,
                     WM_USER_TSF_TEXTCHANGE, PM_REMOVE))
    ::DispatchMessageW(&msg);
  return NS_SUCCEEDED(nsr) &&
         mMgr->GetFocusedContext()->mTextChanged &&
         aStart == mMgr->GetFocusedContext()->mTextChangeData.acpStart &&
         aOldEnd == mMgr->GetFocusedContext()->mTextChangeData.acpOldEnd &&
         aNewEnd == mMgr->GetFocusedContext()->mTextChangeData.acpNewEnd;
}

PRBool
TestApp::TestNotification(void)
{
  nsresult nsr;
  // get selection to test notification support
  nsCOMPtr<nsISelectionController> selCon;
  if (!(NS_SUCCEEDED(GetSelCon(getter_AddRefs(selCon))) && selCon)) {
    fail("TestNotification: get nsISelectionController");
    return PR_FALSE;
  }

  nsr = selCon->CompleteMove(PR_FALSE, PR_FALSE);
  if (!(NS_SUCCEEDED(nsr))) {
    fail("TestNotification: CompleteMove");
    return PR_FALSE;
  }

  if (!mMgr->GetFocusedContext()) {
    fail("TestNotification: GetFocusedContext returns null #1");
    return PR_FALSE;
  }

  mMgr->GetFocusedContext()->mSelChanged = PR_FALSE;
  nsr = selCon->CharacterMove(PR_TRUE, PR_FALSE);
  if (!(NS_SUCCEEDED(nsr) &&
        mMgr->GetFocusedContext()->mSelChanged)) {
    fail("TestNotification: CharacterMove");
    return PR_FALSE;
  }

  if (!mMgr->GetFocusedContext()) {
    fail("TestNotification: GetFocusedContext returns null #2");
    return PR_FALSE;
  }

  mMgr->GetFocusedContext()->mSelChanged = PR_FALSE;
  nsr = selCon->CharacterMove(PR_TRUE, PR_TRUE);
  if (!(NS_SUCCEEDED(nsr) &&
        mMgr->GetFocusedContext()->mSelChanged)) {
    fail("TestNotification: CharacterMove (extend)");
    return PR_FALSE;
  }

  nsCOMPtr<nsIWidget> widget;
  if (!GetWidget(getter_AddRefs(widget))) {
    fail("TestNotification: get nsIWidget");
    return PR_FALSE;
  }

  NS_NAMED_LITERAL_STRING(character, "");
  NS_NAMED_LITERAL_STRING(characterA, "A");

  // The selection test code above placed the selection at offset 1 to 2
  const LONG TEXTCHANGE1_START  = 1;
  const LONG TEXTCHANGE1_OLDEND = 2;
  const LONG TEXTCHANGE1_NEWEND = 2;

  // replace single selected character with 'A'
  if (!TestNotificationTextChange(widget, 'A', characterA,
        TEXTCHANGE1_START, TEXTCHANGE1_OLDEND, TEXTCHANGE1_NEWEND)) {
    fail("TestNotification: text change 1");
    return PR_FALSE;
  }

  const LONG TEXTCHANGE2_START  = TEXTCHANGE1_NEWEND;
  const LONG TEXTCHANGE2_OLDEND = TEXTCHANGE1_NEWEND;
  const LONG TEXTCHANGE2_NEWEND = TEXTCHANGE1_NEWEND + 1;

  // insert 'A'
  if (!TestNotificationTextChange(widget, 'A', characterA,
        TEXTCHANGE2_START, TEXTCHANGE2_OLDEND, TEXTCHANGE2_NEWEND)) {
    fail("TestNotification: text change 2");
    return PR_FALSE;
  }

  const LONG TEXTCHANGE3_START  = TEXTCHANGE2_NEWEND - 1;
  const LONG TEXTCHANGE3_OLDEND = TEXTCHANGE2_NEWEND;
  const LONG TEXTCHANGE3_NEWEND = TEXTCHANGE2_NEWEND - 1;

  // backspace
  if (!TestNotificationTextChange(widget, '\b', character,
        TEXTCHANGE3_START, TEXTCHANGE3_OLDEND, TEXTCHANGE3_NEWEND)) {
    fail("TestNotification: text change 3");
    return PR_FALSE;
  }
  return PR_TRUE;
}

PRBool
TestApp::TestEditMessages(void)
{
  mTestString = NS_LITERAL_STRING(
    "This is a test of\nthe native editing command messages");
  // 0123456789012345678901 2345678901234567890123456789012
  // 0         1         2          3         4         5

  // The native text string is increased by converting \n to \r\n.
  PRUint32 testStringLength = mTestString.Length() + 1;

  mTextArea->SetValue(mTestString);
  mTextArea->Focus();

  nsCOMPtr<nsIWidget> widget;
  if (!GetWidget(getter_AddRefs(widget))) {
    fail("TestEditMessages: get nsIWidget");
    return PR_FALSE;
  }

  HWND wnd = (HWND)widget->GetNativeData(NS_NATIVE_WINDOW);
  PRBool result = PR_TRUE;

  if (!::SendMessage(wnd, EM_CANUNDO, 0, 0)) {
    fail("TestEditMessages: EM_CANUNDO");
    return PR_FALSE;
  }

  if (::SendMessage(wnd, EM_CANREDO, 0, 0)) {
    fail("TestEditMessages: EM_CANREDO #1");
    return PR_FALSE;
  }


  if (!::SendMessage(wnd, EM_UNDO, 0, 0)) {
    fail("TestEditMessages: EM_UNDO #1");
    return PR_FALSE;
  }

  nsAutoString str;
  mTextArea->GetValue(str);
  if (str == mTestString) {
    fail("TestEditMessage: EM_UNDO #1, failed to execute");
    printf("Current Str: \"%s\"\n", NS_ConvertUTF16toUTF8(str).get());
    return PR_FALSE;
  }

  if (!::SendMessage(wnd, EM_CANREDO, 0, 0)) {
    fail("TestEditMessages: EM_CANREDO #2");
    return PR_FALSE;
  }

  if (!::SendMessage(wnd, EM_REDO, 0, 0)) {
    fail("TestEditMessages: EM_REDO #1");
    return PR_FALSE;
  }

  mTextArea->GetValue(str);
  if (str != mTestString) {
    fail("TestEditMessage: EM_REDO #1, failed to execute");
    printf("Current Str: \"%s\"\n", NS_ConvertUTF16toUTF8(str).get());
    return PR_FALSE;
  }

  TS_SELECTION_ACP sel;
  HRESULT hr;

  sel.acpStart = 0;
  sel.acpEnd = testStringLength;
  sel.style.ase = TS_AE_END;
  sel.style.fInterimChar = FALSE;
  hr = mMgr->GetFocusedStore()->SetSelection(1, &sel);
  if (!(SUCCEEDED(hr))) {
    fail("TestEditMessages: SetSelection #1");
    return PR_FALSE;
  }

  ::SendMessage(wnd, WM_CUT, 0, 0);
  mTextArea->GetValue(str);
  if (!str.IsEmpty()) {
    fail("TestEditMessages: WM_CUT");
    printf("Current Str: \"%s\"\n", NS_ConvertUTF16toUTF8(str).get());
    return PR_FALSE;
  }

  ::SendMessage(wnd, WM_PASTE, 0, 0);
  mTextArea->GetValue(str);
  if (str != mTestString) {
    fail("TestEditMessages: WM_PASTE #1");
    printf("Current Str: \"%s\"\n", NS_ConvertUTF16toUTF8(str).get());
    return PR_FALSE;
  }

  ::SendMessage(wnd, WM_PASTE, 0, 0);
  mTextArea->GetValue(str);
  nsAutoString expectedStr(mTestString);
  expectedStr += mTestString;
  if (str != expectedStr) {
    fail("TestEditMessages: WM_PASTE #2");
    printf("Current Str: \"%s\"\n", NS_ConvertUTF16toUTF8(str).get());
    return PR_FALSE;
  }

  sel.acpStart = 0;
  sel.acpEnd = testStringLength;
  hr = mMgr->GetFocusedStore()->SetSelection(1, &sel);
  if (!(SUCCEEDED(hr))) {
    fail("TestEditMessages: SetSelection #2");
    return PR_FALSE;
  }

  ::SendMessage(wnd, WM_CLEAR, 0, 0);
  mTextArea->GetValue(str);
  if (str != mTestString) {
    fail("TestEditMessages: WM_CLEAR #1");
    printf("Current Str: \"%s\"\n", NS_ConvertUTF16toUTF8(str).get());
    return PR_FALSE;
  }

  sel.acpStart = 4;
  sel.acpEnd = testStringLength;
  hr = mMgr->GetFocusedStore()->SetSelection(1, &sel);
  if (!(SUCCEEDED(hr))) {
    fail("TestEditMessages: SetSelection #3");
    return PR_FALSE;
  }

  ::SendMessage(wnd, WM_COPY, 0, 0);
  mTextArea->GetValue(str);
  if (str != mTestString) {
    fail("TestEditMessages: WM_COPY");
    printf("Current Str: \"%s\"\n", NS_ConvertUTF16toUTF8(str).get());
    return PR_FALSE;
  }

  if (!::SendMessage(wnd, EM_CANPASTE, 0, 0)) {
    fail("TestEditMessages: EM_CANPASTE #1");
    return PR_FALSE;
  }

  if (!::SendMessage(wnd, EM_CANPASTE, CF_TEXT, 0)) {
    fail("TestEditMessages: EM_CANPASTE #2");
    return PR_FALSE;
  }

  if (!::SendMessage(wnd, EM_CANPASTE, CF_UNICODETEXT, 0)) {
    fail("TestEditMessages: EM_CANPASTE #3");
    return PR_FALSE;
  }

  ::SendMessage(wnd, WM_PASTE, 0, 0);
  mTextArea->GetValue(str);
  if (str != mTestString) {
    fail("TestEditMessages: WM_PASTE #3");
    printf("Current Str: \"%s\"\n", NS_ConvertUTF16toUTF8(str).get());
    return PR_FALSE;
  }

  sel.acpStart = 4;
  sel.acpEnd = testStringLength;
  hr = mMgr->GetFocusedStore()->SetSelection(1, &sel);
  if (!(SUCCEEDED(hr))) {
    fail("TestEditMessages: SetSelection #3");
    return PR_FALSE;
  }

  ::SendMessage(wnd, WM_CLEAR, 0, 0);
  mTextArea->GetValue(str);
  if (str != NS_LITERAL_STRING("This")) {
    fail("TestEditMessages: WM_CLEAR #2");
    printf("Current Str: \"%s\"\n", NS_ConvertUTF16toUTF8(str).get());
    return PR_FALSE;
  }

  ::SendMessage(wnd, WM_PASTE, 0, 0);
  mTextArea->GetValue(str);
  if (str != mTestString) {
    fail("TestEditMessages: WM_PASTE #4");
    printf("Current Str: \"%s\"\n", NS_ConvertUTF16toUTF8(str).get());
    return PR_FALSE;
  }

  return PR_TRUE;
}

PRBool
TestApp::TestScrollMessages(void)
{
  NS_NAMED_LITERAL_STRING(kLine, "ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss\n");
  mTestString.Truncate();
  for (PRUint32 i = 0; i < 30; i++) {
    mTestString.Append(kLine);
  }

  mTextArea->SetAttribute(NS_LITERAL_STRING("style"),
    NS_LITERAL_STRING("width:3em;height:3em;word-wrap:normal;"));
  mTextArea->SetValue(mTestString);
  mTextArea->Focus();

  nsCOMPtr<nsIWidget> widget;
  if (!GetWidget(getter_AddRefs(widget))) {
    fail("TestScrollMessages: get nsIWidget");
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
    return PR_FALSE;
  }

  nsCOMPtr<nsIDOMNSElement> textAreaNS(do_QueryInterface(mTextArea));
  if (!textAreaNS) {
    fail("TestScrollMessages: get nsIDOMNSElement");
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
    return PR_FALSE;
  }

#define DO_CHECK(aFailureCondition, aDescription) \
  if (aFailureCondition) { \
    nsCAutoString str(aDescription); \
    str.Append(": "); \
    str.Append(#aFailureCondition); \
    fail(str.get()); \
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString()); \
    return PR_FALSE; \
  }

  HWND wnd = (HWND)widget->GetNativeData(NS_NATIVE_WINDOW);

  textAreaNS->SetScrollTop(0);
  textAreaNS->SetScrollLeft(0);

  if (::SendMessage(wnd, WM_VSCROLL, SB_LINEDOWN, 0) != 0) {
    fail("TestScrollMessages: SendMessage WM_VSCROLL #1");
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
    return PR_FALSE;
  }

  PRInt32 x, y, prevX, prevY;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  DO_CHECK(x != 0, "TestScrollMessages: SendMessage WM_VSCROLL #1");
  DO_CHECK(y == 0, "TestScrollMessages: SendMessage WM_VSCROLL #1");

  if (::SendMessage(wnd, WM_HSCROLL, SB_LINERIGHT, 0) != 0) {
    fail("TestScrollMessages: SendMessage WM_HSCROLL #1");
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
    return PR_FALSE;
  }

  prevX = x;
  prevY = y;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  const PRInt32 kLineWidth  = x;
  const PRInt32 kLineHeight = y;

  DO_CHECK(x == 0,     "TestScrollMessages: SendMessage WM_HSCROLL #1");
  DO_CHECK(y != prevY, "TestScrollMessages: SendMessage WM_HSCROLL #1");

  if (::SendMessage(wnd, WM_VSCROLL, SB_LINEUP, 0) != 0) {
    fail("TestScrollMessages: SendMessage WM_VSCROLL #2");
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
    return PR_FALSE;
  }

  prevX = x;
  prevY = y;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  DO_CHECK(x != prevX, "TestScrollMessages: SendMessage WM_VSCROLL #2");
  DO_CHECK(y != 0,     "TestScrollMessages: SendMessage WM_VSCROLL #2");

  if (::SendMessage(wnd, WM_HSCROLL, SB_LINELEFT, 0) != 0) {
    fail("TestScrollMessages: SendMessage WM_HSCROLL #2");
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
    return PR_FALSE;
  }

  prevX = x;
  prevY = y;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  DO_CHECK(x != 0, "TestScrollMessages: SendMessage WM_HSCROLL #2");
  DO_CHECK(y != 0, "TestScrollMessages: SendMessage WM_HSCROLL #2");

  if (::SendMessage(wnd, WM_VSCROLL, SB_PAGEDOWN, 0) != 0) {
    fail("TestScrollMessages: SendMessage WM_VSCROLL #3");
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
    return PR_FALSE;
  }

  prevX = x;
  prevY = y;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  DO_CHECK(x != 0,           "TestScrollMessages: SendMessage WM_VSCROLL #3");
  DO_CHECK(y <= kLineHeight, "TestScrollMessages: SendMessage WM_VSCROLL #3");

  if (::SendMessage(wnd, WM_HSCROLL, SB_PAGERIGHT, 0) != 0) {
    fail("TestScrollMessages: SendMessage WM_HSCROLL #3");
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
    return PR_FALSE;
  }

  prevX = x;
  prevY = y;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  DO_CHECK(x <= kLineWidth, "TestScrollMessages: SendMessage WM_HSCROLL #3");
  DO_CHECK(y != prevY,      "TestScrollMessages: SendMessage WM_HSCROLL #3");

  const PRInt32 kPageWidth  = x;
  const PRInt32 kPageHeight = y;

  ::SendMessage(wnd, WM_VSCROLL, SB_LINEDOWN, 0);
  ::SendMessage(wnd, WM_VSCROLL, SB_LINEUP, 0);
  ::SendMessage(wnd, WM_HSCROLL, SB_LINERIGHT, 0);
  ::SendMessage(wnd, WM_HSCROLL, SB_LINELEFT, 0);

  prevX = x;
  prevY = y;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  DO_CHECK(x != prevX, "TestScrollMessages: SB_LINELEFT scrolled wrong amount");
  DO_CHECK(y != prevY, "TestScrollMessages: SB_LINEUP scrolled wrong amount");

  if (::SendMessage(wnd, WM_VSCROLL, SB_PAGEUP, 0) != 0) {
    fail("TestScrollMessages: SendMessage WM_VSCROLL #4");
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
    return PR_FALSE;
  }

  prevX = x;
  prevY = y;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  DO_CHECK(x != prevX, "TestScrollMessages: SendMessage WM_VSCROLL #4");
  DO_CHECK(y != 0,     "TestScrollMessages: SendMessage WM_VSCROLL #4");

  if (::SendMessage(wnd, WM_HSCROLL, SB_PAGELEFT, 0) != 0) {
    fail("TestScrollMessages: SendMessage WM_HSCROLL #4");
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
    return PR_FALSE;
  }

  prevX = x;
  prevY = y;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  DO_CHECK(x != 0, "TestScrollMessages: SendMessage WM_HSCROLL #4");
  DO_CHECK(y != 0, "TestScrollMessages: SendMessage WM_HSCROLL #4");

  if (::SendMessage(wnd, WM_VSCROLL, SB_BOTTOM, 0) != 0) {
    fail("TestScrollMessages: SendMessage WM_VSCROLL #5");
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
    return PR_FALSE;
  }

  prevX = x;
  prevY = y;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  DO_CHECK(x != 0,           "TestScrollMessages: SendMessage WM_VSCROLL #5");
  DO_CHECK(y <= kPageHeight, "TestScrollMessages: SendMessage WM_VSCROLL #5");

  if (::SendMessage(wnd, WM_HSCROLL, SB_RIGHT, 0) != 0) {
    fail("TestScrollMessages: SendMessage WM_HSCROLL #6");
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
    return PR_FALSE;
  }

  prevX = x;
  prevY = y;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  DO_CHECK(x <= kPageWidth, "TestScrollMessages: SendMessage WM_HSCROLL #5");
  DO_CHECK(y != prevY,      "TestScrollMessages: SendMessage WM_HSCROLL #5");

  ::SendMessage(wnd, WM_VSCROLL, SB_LINEDOWN, 0);
  ::SendMessage(wnd, WM_HSCROLL, SB_LINERIGHT, 0);

  prevX = x;
  prevY = y;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  DO_CHECK(x != prevX, "SB_RIGHT didn't scroll to right most");
  DO_CHECK(y != prevY, "SB_BOTTOM didn't scroll to bottom most");

  ::SendMessage(wnd, WM_VSCROLL, SB_PAGEUP, 0);
  ::SendMessage(wnd, WM_VSCROLL, SB_PAGEDOWN, 0);
  ::SendMessage(wnd, WM_HSCROLL, SB_PAGELEFT, 0);
  ::SendMessage(wnd, WM_HSCROLL, SB_PAGERIGHT, 0);

  prevX = x;
  prevY = y;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  DO_CHECK(x != prevX, "TestScrollMessages: SB_PAGELEFT scrolled wrong amount");
  DO_CHECK(y != prevY, "TestScrollMessages: SB_PAGEUP scrolled wrong amount");

  if (::SendMessage(wnd, WM_VSCROLL, SB_TOP, 0) != 0) {
    fail("TestScrollMessages: SendMessage WM_VSCROLL #6");
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
    return PR_FALSE;
  }

  prevX = x;
  prevY = y;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  DO_CHECK(x != prevX, "TestScrollMessages: SendMessage WM_VSCROLL #6");
  DO_CHECK(y != 0,     "TestScrollMessages: SendMessage WM_VSCROLL #6");

  if (::SendMessage(wnd, WM_HSCROLL, SB_LEFT, 0) != 0) {
    fail("TestScrollMessages: SendMessage WM_HSCROLL #4");
    mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
    return PR_FALSE;
  }

  prevX = x;
  prevY = y;
  textAreaNS->GetScrollTop(&y);
  textAreaNS->GetScrollLeft(&x);

  DO_CHECK(x != 0, "TestScrollMessages: SendMessage WM_HSCROLL #6");
  DO_CHECK(y != 0, "TestScrollMessages: SendMessage WM_HSCROLL #6");
#undef DO_CHECK

  mTextArea->SetAttribute(NS_LITERAL_STRING("style"), EmptyString());
  return PR_TRUE;
}

PRBool
TestApp::GetWidget(nsIWidget** aWidget)
{
  nsCOMPtr<nsIDocShell> docShell;
  nsresult rv = mWindow->GetDocShell(getter_AddRefs(docShell));
  if (NS_FAILED(rv) || !docShell) {
    return PR_FALSE;
  }

  nsCOMPtr<nsIPresShell> presShell;
  rv = docShell->GetPresShell(getter_AddRefs(presShell));
  if (NS_FAILED(rv) || !presShell) {
    return PR_FALSE;
  }

  nsCOMPtr<nsIViewManager> viewManager = presShell->GetViewManager();
  if (!viewManager) {
    return PR_FALSE;
  }

  rv = viewManager->GetRootWidget(aWidget);
  return (NS_SUCCEEDED(rv) && aWidget);
}

nsresult
TestApp::GetSelCon(nsISelectionController** aSelCon)
{
  nsCOMPtr<nsIDocShell> docShell;
  nsresult nsr = mWindow->GetDocShell(getter_AddRefs(docShell));
  if (NS_SUCCEEDED(nsr) && docShell) {
    nsCOMPtr<nsIPresShell> presShell;
    nsr = docShell->GetPresShell(getter_AddRefs(presShell));
    if (NS_SUCCEEDED(nsr) && presShell) {
      nsIFrame* frame = 
        nsCOMPtr<nsIContent>(do_QueryInterface(mCurrentNode))->GetPrimaryFrame();
      if (frame) {
        nsPresContext* presContext = presShell->GetPresContext();
        if (presContext) {
          nsr = frame->GetSelectionController(presContext, aSelCon);
        }
      }
    }
  }
  return nsr;
}

int main(int argc, char** argv)
{
  ScopedXPCOM xpcom("TestWinTSF (bug #88831)");
  if (xpcom.failed())
    return 1;

  nsRefPtr<TestApp> tests = new TestApp();
  if (!tests)
    return 1;

  if (NS_FAILED(tests->Run())) {
    fail("run failed");
    return 1;
  }
  return int(tests->CheckFailed());
}
