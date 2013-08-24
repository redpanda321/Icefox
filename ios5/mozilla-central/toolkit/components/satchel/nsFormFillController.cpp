/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsFormFillController.h"

#include "nsIFormAutoComplete.h"
#include "nsIInputListAutoComplete.h"
#include "nsIAutoCompleteSimpleResult.h"
#include "nsString.h"
#include "nsReadableUtils.h"
#include "nsIServiceManager.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIDocShellTreeItem.h"
#include "nsPIDOMWindow.h"
#include "nsIWebNavigation.h"
#include "nsIContentViewer.h"
#include "nsIDOMEventTarget.h"
#include "nsIDOMKeyEvent.h"
#include "nsIDOMDocument.h"
#include "nsIDOMElement.h"
#include "nsIFormControl.h"
#include "nsIDocument.h"
#include "nsIContent.h"
#include "nsIPresShell.h"
#include "nsIFrame.h"
#include "nsRect.h"
#include "nsIDOMHTMLFormElement.h"
#include "nsILoginManager.h"
#include "nsIDOMMouseEvent.h"
#include "mozilla/ModuleUtils.h"
#include "nsToolkitCompsCID.h"
#include "nsEmbedCID.h"
#include "nsIDOMNSEditableElement.h"
#include "nsIDOMNSEvent.h"
#include "mozilla/dom/Element.h"
#include "nsContentUtils.h"

NS_IMPL_ISUPPORTS5(nsFormFillController,
                   nsIFormFillController,
                   nsIAutoCompleteInput,
                   nsIAutoCompleteSearch,
                   nsIDOMEventListener,
                   nsIMutationObserver)

nsFormFillController::nsFormFillController() :
  mFocusedInput(nsnull),
  mFocusedInputNode(nsnull),
  mListNode(nsnull),
  mTimeout(50),
  mMinResultsForPopup(1),
  mMaxRows(0),
  mDisableAutoComplete(false),
  mCompleteDefaultIndex(false),
  mCompleteSelectedIndex(false),
  mForceComplete(false),
  mSuppressOnInput(false)
{
  mController = do_GetService("@mozilla.org/autocomplete/controller;1");
  mDocShells = do_CreateInstance("@mozilla.org/supports-array;1");
  mPopups = do_CreateInstance("@mozilla.org/supports-array;1");
  mPwmgrInputs.Init();
}

struct PwmgrInputsEnumData
{
  PwmgrInputsEnumData(nsFormFillController* aFFC, nsIDocument* aDoc)
  : mFFC(aFFC), mDoc(aDoc) {}

  nsFormFillController* mFFC;
  nsCOMPtr<nsIDocument> mDoc;
};

nsFormFillController::~nsFormFillController()
{
  if (mListNode) {
    mListNode->RemoveMutationObserver(this);
    mListNode = nsnull;
  }
  if (mFocusedInputNode) {
    MaybeRemoveMutationObserver(mFocusedInputNode);
    mFocusedInputNode = nsnull;
    mFocusedInput = nsnull;
  }
  PwmgrInputsEnumData ed(this, nsnull);
  mPwmgrInputs.Enumerate(RemoveForDocumentEnumerator, &ed);

  // Remove ourselves as a focus listener from all cached docShells
  PRUint32 count;
  mDocShells->Count(&count);
  for (PRUint32 i = 0; i < count; ++i) {
    nsCOMPtr<nsIDocShell> docShell;
    mDocShells->GetElementAt(i, getter_AddRefs(docShell));
    nsCOMPtr<nsIDOMWindow> domWindow = GetWindowForDocShell(docShell);
    RemoveWindowListeners(domWindow);
  }
}

////////////////////////////////////////////////////////////////////////
//// nsIMutationObserver
//

void
nsFormFillController::AttributeChanged(nsIDocument* aDocument,
                                       mozilla::dom::Element* aElement,
                                       PRInt32 aNameSpaceID,
                                       nsIAtom* aAttribute, PRInt32 aModType)
{
  if (mListNode && mListNode->Contains(aElement)) {
    RevalidateDataList();
  }
}

void
nsFormFillController::ContentAppended(nsIDocument* aDocument,
                                      nsIContent* aContainer,
                                      nsIContent* aChild,
                                      PRInt32 aIndexInContainer)
{
  if (mListNode && mListNode->Contains(aContainer)) {
    RevalidateDataList();
  }
}

void
nsFormFillController::ContentInserted(nsIDocument* aDocument,
                                      nsIContent* aContainer,
                                      nsIContent* aChild,
                                      PRInt32 aIndexInContainer)
{
  if (mListNode && mListNode->Contains(aContainer)) {
    RevalidateDataList();
  }
}

void
nsFormFillController::ContentRemoved(nsIDocument* aDocument,
                                     nsIContent* aContainer,
                                     nsIContent* aChild,
                                     PRInt32 aIndexInContainer,
                                     nsIContent* aPreviousSibling)
{
  if (mListNode && mListNode->Contains(aContainer)) {
    RevalidateDataList();
  }
}

void
nsFormFillController::CharacterDataWillChange(nsIDocument* aDocument,
                                              nsIContent* aContent,
                                              CharacterDataChangeInfo* aInfo)
{
}

void
nsFormFillController::CharacterDataChanged(nsIDocument* aDocument,
                                           nsIContent* aContent,
                                           CharacterDataChangeInfo* aInfo)
{
}

void
nsFormFillController::AttributeWillChange(nsIDocument* aDocument,
                                          mozilla::dom::Element* aElement,
                                          PRInt32 aNameSpaceID,
                                          nsIAtom* aAttribute, PRInt32 aModType)
{
}

void
nsFormFillController::ParentChainChanged(nsIContent* aContent)
{
}

void
nsFormFillController::NodeWillBeDestroyed(const nsINode* aNode)
{
  mPwmgrInputs.Remove(aNode);
  if (aNode == mListNode) {
    mListNode = nsnull;
    RevalidateDataList();
  } else if (aNode == mFocusedInputNode) {
    mFocusedInputNode = nsnull;
    mFocusedInput = nsnull;
  }
}

void
nsFormFillController::MaybeRemoveMutationObserver(nsINode* aNode)
{
  // Nodes being tracked in mPwmgrInputs will have their observers removed when
  // they stop being tracked. 
  bool dummy;
  if (!mPwmgrInputs.Get(aNode, &dummy)) {
    aNode->RemoveMutationObserver(this);
  }
}

////////////////////////////////////////////////////////////////////////
//// nsIFormFillController

NS_IMETHODIMP
nsFormFillController::AttachToBrowser(nsIDocShell *aDocShell, nsIAutoCompletePopup *aPopup)
{
  NS_ENSURE_TRUE(aDocShell && aPopup, NS_ERROR_ILLEGAL_VALUE);

  mDocShells->AppendElement(aDocShell);
  mPopups->AppendElement(aPopup);

  // Listen for focus events on the domWindow of the docShell
  nsCOMPtr<nsIDOMWindow> domWindow = GetWindowForDocShell(aDocShell);
  AddWindowListeners(domWindow);

  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::DetachFromBrowser(nsIDocShell *aDocShell)
{
  PRInt32 index = GetIndexOfDocShell(aDocShell);
  NS_ENSURE_TRUE(index >= 0, NS_ERROR_FAILURE);

  // Stop listening for focus events on the domWindow of the docShell
  nsCOMPtr<nsIDocShell> docShell;
  mDocShells->GetElementAt(index, getter_AddRefs(docShell));
  nsCOMPtr<nsIDOMWindow> domWindow = GetWindowForDocShell(docShell);
  RemoveWindowListeners(domWindow);

  mDocShells->RemoveElementAt(index);
  mPopups->RemoveElementAt(index);

  return NS_OK;
}


NS_IMETHODIMP
nsFormFillController::MarkAsLoginManagerField(nsIDOMHTMLInputElement *aInput)
{
  /*
   * The Login Manager can supply autocomplete results for username fields,
   * when a user has multiple logins stored for a site. It uses this
   * interface to indicate that the form manager shouldn't handle the
   * autocomplete. The form manager also checks for this tag when saving
   * form history (so it doesn't save usernames).
   */
  nsCOMPtr<nsINode> node = do_QueryInterface(aInput);
  NS_ENSURE_STATE(node);
  mPwmgrInputs.Put(node, true);
  node->AddMutationObserverUnlessExists(this);

  if (!mLoginManager)
    mLoginManager = do_GetService("@mozilla.org/login-manager;1");

  return NS_OK;
}


////////////////////////////////////////////////////////////////////////
//// nsIAutoCompleteInput

NS_IMETHODIMP
nsFormFillController::GetPopup(nsIAutoCompletePopup **aPopup)
{
  *aPopup = mFocusedPopup;
  NS_IF_ADDREF(*aPopup);
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetController(nsIAutoCompleteController **aController)
{
  *aController = mController;
  NS_IF_ADDREF(*aController);
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetPopupOpen(bool *aPopupOpen)
{
  if (mFocusedPopup)
    mFocusedPopup->GetPopupOpen(aPopupOpen);
  else
    *aPopupOpen = false;
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::SetPopupOpen(bool aPopupOpen)
{
  if (mFocusedPopup) {
    if (aPopupOpen) {
      // make sure input field is visible before showing popup (bug 320938)
      nsCOMPtr<nsIContent> content = do_QueryInterface(mFocusedInput);
      NS_ENSURE_STATE(content);
      nsCOMPtr<nsIDocShell> docShell = GetDocShellForInput(mFocusedInput);
      NS_ENSURE_STATE(docShell);
      nsCOMPtr<nsIPresShell> presShell;
      docShell->GetPresShell(getter_AddRefs(presShell));
      NS_ENSURE_STATE(presShell);
      presShell->ScrollContentIntoView(content,
                                       nsIPresShell::ScrollAxis(),
                                       nsIPresShell::ScrollAxis(),
                                       nsIPresShell::SCROLL_OVERFLOW_HIDDEN);
      // mFocusedPopup can be destroyed after ScrollContentIntoView, see bug 420089
      if (mFocusedPopup)
        mFocusedPopup->OpenAutocompletePopup(this, mFocusedInput);
    } else
      mFocusedPopup->ClosePopup();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetDisableAutoComplete(bool *aDisableAutoComplete)
{
  *aDisableAutoComplete = mDisableAutoComplete;
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::SetDisableAutoComplete(bool aDisableAutoComplete)
{
  mDisableAutoComplete = aDisableAutoComplete;
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetCompleteDefaultIndex(bool *aCompleteDefaultIndex)
{
  *aCompleteDefaultIndex = mCompleteDefaultIndex;
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::SetCompleteDefaultIndex(bool aCompleteDefaultIndex)
{
  mCompleteDefaultIndex = aCompleteDefaultIndex;
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetCompleteSelectedIndex(bool *aCompleteSelectedIndex)
{
  *aCompleteSelectedIndex = mCompleteSelectedIndex;
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::SetCompleteSelectedIndex(bool aCompleteSelectedIndex)
{
  mCompleteSelectedIndex = aCompleteSelectedIndex;
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetForceComplete(bool *aForceComplete)
{
  *aForceComplete = mForceComplete;
  return NS_OK;
}

NS_IMETHODIMP nsFormFillController::SetForceComplete(bool aForceComplete)
{
  mForceComplete = aForceComplete;
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetMinResultsForPopup(PRUint32 *aMinResultsForPopup)
{
  *aMinResultsForPopup = mMinResultsForPopup;
  return NS_OK;
}

NS_IMETHODIMP nsFormFillController::SetMinResultsForPopup(PRUint32 aMinResultsForPopup)
{
  mMinResultsForPopup = aMinResultsForPopup;
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetMaxRows(PRUint32 *aMaxRows)
{
  *aMaxRows = mMaxRows;
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::SetMaxRows(PRUint32 aMaxRows)
{
  mMaxRows = aMaxRows;
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetShowImageColumn(bool *aShowImageColumn)
{
  *aShowImageColumn = false;
  return NS_OK;
}

NS_IMETHODIMP nsFormFillController::SetShowImageColumn(bool aShowImageColumn)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}


NS_IMETHODIMP
nsFormFillController::GetShowCommentColumn(bool *aShowCommentColumn)
{
  *aShowCommentColumn = false;
  return NS_OK;
}

NS_IMETHODIMP nsFormFillController::SetShowCommentColumn(bool aShowCommentColumn)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsFormFillController::GetTimeout(PRUint32 *aTimeout)
{
  *aTimeout = mTimeout;
  return NS_OK;
}

NS_IMETHODIMP nsFormFillController::SetTimeout(PRUint32 aTimeout)
{
  mTimeout = aTimeout;
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::SetSearchParam(const nsAString &aSearchParam)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsFormFillController::GetSearchParam(nsAString &aSearchParam)
{
  if (!mFocusedInput) {
    NS_WARNING("mFocusedInput is null for some reason! avoiding a crash. should find out why... - ben");
    return NS_ERROR_FAILURE; // XXX why? fix me.
  }

  mFocusedInput->GetName(aSearchParam);
  if (aSearchParam.IsEmpty())
    mFocusedInput->GetId(aSearchParam);

  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetSearchCount(PRUint32 *aSearchCount)
{
  *aSearchCount = 1;
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetSearchAt(PRUint32 index, nsACString & _retval)
{
  _retval.Assign("form-history");
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetTextValue(nsAString & aTextValue)
{
  if (mFocusedInput) {
    mFocusedInput->GetValue(aTextValue);
  } else {
    aTextValue.Truncate();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::SetTextValue(const nsAString & aTextValue)
{
  nsCOMPtr<nsIDOMNSEditableElement> editable = do_QueryInterface(mFocusedInput);
  if (editable) {
    mSuppressOnInput = true;
    editable->SetUserInput(aTextValue);
    mSuppressOnInput = false;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetSelectionStart(PRInt32 *aSelectionStart)
{
  if (mFocusedInput)
    mFocusedInput->GetSelectionStart(aSelectionStart);
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetSelectionEnd(PRInt32 *aSelectionEnd)
{
  if (mFocusedInput)
    mFocusedInput->GetSelectionEnd(aSelectionEnd);
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::SelectTextRange(PRInt32 aStartIndex, PRInt32 aEndIndex)
{
 if (mFocusedInput)
    mFocusedInput->SetSelectionRange(aStartIndex, aEndIndex, EmptyString());
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::OnSearchBegin()
{
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::OnSearchComplete()
{
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::OnTextEntered(bool* aPrevent)
{
  NS_ENSURE_ARG(aPrevent);
  NS_ENSURE_TRUE(mFocusedInput, NS_OK);
  // Fire off a DOMAutoComplete event
  nsCOMPtr<nsIDOMDocument> domDoc;
  mFocusedInput->GetOwnerDocument(getter_AddRefs(domDoc));
  NS_ENSURE_STATE(domDoc);

  nsCOMPtr<nsIDOMEvent> event;
  domDoc->CreateEvent(NS_LITERAL_STRING("Events"), getter_AddRefs(event));
  NS_ENSURE_STATE(event);

  event->InitEvent(NS_LITERAL_STRING("DOMAutoComplete"), true, true);

  // XXXjst: We mark this event as a trusted event, it's up to the
  // callers of this to ensure that it's only called from trusted
  // code.
  event->SetTrusted(true);

  nsCOMPtr<nsIDOMEventTarget> targ = do_QueryInterface(mFocusedInput);

  bool defaultActionEnabled;
  targ->DispatchEvent(event, &defaultActionEnabled);
  *aPrevent = !defaultActionEnabled;
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::OnTextReverted(bool *_retval)
{
  return NS_OK;
}

NS_IMETHODIMP
nsFormFillController::GetConsumeRollupEvent(bool *aConsumeRollupEvent)
{
  *aConsumeRollupEvent = false;
  return NS_OK;
}


////////////////////////////////////////////////////////////////////////
//// nsIAutoCompleteSearch

NS_IMETHODIMP
nsFormFillController::StartSearch(const nsAString &aSearchString, const nsAString &aSearchParam,
                                  nsIAutoCompleteResult *aPreviousResult, nsIAutoCompleteObserver *aListener)
{
  nsresult rv;
  nsCOMPtr<nsIAutoCompleteResult> result;

  // If the login manager has indicated it's responsible for this field, let it
  // handle the autocomplete. Otherwise, handle with form history.
  bool dummy;
  if (mPwmgrInputs.Get(mFocusedInputNode, &dummy)) {
    // XXX aPreviousResult shouldn't ever be a historyResult type, since we're not letting
    // satchel manage the field?
    rv = mLoginManager->AutoCompleteSearch(aSearchString,
                                         aPreviousResult,
                                         mFocusedInput,
                                         getter_AddRefs(result));
  } else {
    nsCOMPtr<nsIAutoCompleteResult> formHistoryResult;

    // It appears that mFocusedInput is always null when we are focusing a XUL
    // element. Scary :)
    if (!mFocusedInput || nsContentUtils::IsAutocompleteEnabled(mFocusedInput)) {
      nsCOMPtr <nsIFormAutoComplete> formAutoComplete =
        do_GetService("@mozilla.org/satchel/form-autocomplete;1", &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = formAutoComplete->AutoCompleteSearch(aSearchParam,
                                                aSearchString,
                                                mFocusedInput,
                                                aPreviousResult,
                                                getter_AddRefs(formHistoryResult));

      NS_ENSURE_SUCCESS(rv, rv);
    }

    mLastSearchResult = formHistoryResult;
    mLastListener = aListener;
    mLastSearchString = aSearchString;

    nsCOMPtr <nsIInputListAutoComplete> inputListAutoComplete =
      do_GetService("@mozilla.org/satchel/inputlist-autocomplete;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = inputListAutoComplete->AutoCompleteSearch(formHistoryResult,
                                                   aSearchString,
                                                   mFocusedInput,
                                                   getter_AddRefs(result));

    if (mFocusedInput) {
      nsCOMPtr<nsIDOMHTMLElement> list;
      mFocusedInput->GetList(getter_AddRefs(list));

      nsCOMPtr<nsINode> node = do_QueryInterface(list);
      if (mListNode != node) {
        if (mListNode) {
          mListNode->RemoveMutationObserver(this);
          mListNode = nsnull;
        }
        if (node) {
          node->AddMutationObserverUnlessExists(this);
          mListNode = node;
        }
      }
    }
  }
  NS_ENSURE_SUCCESS(rv, rv);

  aListener->OnSearchResult(this, result);

  return NS_OK;
}

class UpdateSearchResultRunnable : public nsRunnable
{
public:
  UpdateSearchResultRunnable(nsIAutoCompleteObserver* aObserver,
                             nsIAutoCompleteSearch* aSearch,
                             nsIAutoCompleteResult* aResult)
    : mObserver(aObserver)
    , mSearch(aSearch)
    , mResult(aResult)
  {}

  NS_IMETHOD Run() {
    NS_ASSERTION(mObserver, "You shouldn't call this runnable with a null observer!");

    mObserver->OnUpdateSearchResult(mSearch, mResult);
    return NS_OK;
  }

private:
  nsCOMPtr<nsIAutoCompleteObserver> mObserver;
  nsCOMPtr<nsIAutoCompleteSearch> mSearch;
  nsCOMPtr<nsIAutoCompleteResult> mResult;
};

void nsFormFillController::RevalidateDataList()
{
  if (!mLastListener) {
    return;
  }
  nsresult rv;
  nsCOMPtr <nsIInputListAutoComplete> inputListAutoComplete =
    do_GetService("@mozilla.org/satchel/inputlist-autocomplete;1", &rv);

  nsCOMPtr<nsIAutoCompleteResult> result;

  rv = inputListAutoComplete->AutoCompleteSearch(mLastSearchResult,
                                                 mLastSearchString,
                                                 mFocusedInput,
                                                 getter_AddRefs(result));

  nsCOMPtr<nsIRunnable> event =
    new UpdateSearchResultRunnable(mLastListener, this, result);
  NS_DispatchToCurrentThread(event);
}

NS_IMETHODIMP
nsFormFillController::StopSearch()
{
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////
//// nsIDOMEventListener

NS_IMETHODIMP
nsFormFillController::HandleEvent(nsIDOMEvent* aEvent)
{
  nsAutoString type;
  aEvent->GetType(type);

  if (type.EqualsLiteral("focus")) {
    return Focus(aEvent);
  }
  if (type.EqualsLiteral("mousedown")) {
    return MouseDown(aEvent);
  }
  if (type.EqualsLiteral("keypress")) {
    return KeyPress(aEvent);
  }
  if (type.EqualsLiteral("input")) {
    return (!mSuppressOnInput && mController && mFocusedInput) ?
           mController->HandleText() : NS_OK;
  }
  if (type.EqualsLiteral("blur")) {
    if (mFocusedInput)
      StopControllingInput();
    return NS_OK;
  }
  if (type.EqualsLiteral("compositionstart")) {
    NS_ASSERTION(mController, "should have a controller!");
    if (mController && mFocusedInput)
      mController->HandleStartComposition();
    return NS_OK;
  }
  if (type.EqualsLiteral("compositionend")) {
    NS_ASSERTION(mController, "should have a controller!");
    if (mController && mFocusedInput)
      mController->HandleEndComposition();
    return NS_OK;
  }
  if (type.EqualsLiteral("contextmenu")) {
    if (mFocusedPopup)
      mFocusedPopup->ClosePopup();
    return NS_OK;
  }
  if (type.EqualsLiteral("pagehide")) {
    nsCOMPtr<nsIDOMEventTarget> target;
    aEvent->GetTarget(getter_AddRefs(target));

    nsCOMPtr<nsIDOMDocument> domDoc = do_QueryInterface(target);
    if (!domDoc)
      return NS_OK;

    if (mFocusedInput) {
      nsCOMPtr<nsIDOMDocument> inputDoc;
      mFocusedInput->GetOwnerDocument(getter_AddRefs(inputDoc));
      if (domDoc == inputDoc)
        StopControllingInput();
    }

    nsCOMPtr<nsIDocument> doc = do_QueryInterface(domDoc);
    PwmgrInputsEnumData ed(this, doc);
    mPwmgrInputs.Enumerate(RemoveForDocumentEnumerator, &ed);
  }

  return NS_OK;
}


/* static */ PLDHashOperator
nsFormFillController::RemoveForDocumentEnumerator(const nsINode* aKey,
                                                  bool& aEntry,
                                                  void* aUserData)
{
  PwmgrInputsEnumData* ed = static_cast<PwmgrInputsEnumData*>(aUserData);
  if (aKey && (!ed->mDoc || aKey->OwnerDoc() == ed->mDoc)) {
    // mFocusedInputNode's observer is tracked separately, don't remove it here.
    if (aKey != ed->mFFC->mFocusedInputNode) {
      const_cast<nsINode*>(aKey)->RemoveMutationObserver(ed->mFFC);
    }
    return PL_DHASH_REMOVE;
  }
  return PL_DHASH_NEXT;
}

nsresult
nsFormFillController::Focus(nsIDOMEvent* aEvent)
{
  nsCOMPtr<nsIDOMEventTarget> target;
  aEvent->GetTarget(getter_AddRefs(target));

  nsCOMPtr<nsIDOMHTMLInputElement> input = do_QueryInterface(target);
  nsCOMPtr<nsINode> inputNode = do_QueryInterface(input); 
  if (!inputNode)
    return NS_OK;

  bool isReadOnly = false;
  input->GetReadOnly(&isReadOnly);

  bool autocomplete = nsContentUtils::IsAutocompleteEnabled(input);

  nsCOMPtr<nsIDOMHTMLElement> datalist;
  input->GetList(getter_AddRefs(datalist));
  bool hasList = datalist != nsnull;

  bool dummy;
  bool isPwmgrInput = false;
  if (mPwmgrInputs.Get(inputNode, &dummy))
      isPwmgrInput = true;

  nsCOMPtr<nsIFormControl> formControl = do_QueryInterface(input);
  if (isPwmgrInput || (formControl &&
                       formControl->IsSingleLineTextControl(true) &&
                       (hasList || autocomplete) && !isReadOnly)) {
    StartControllingInput(input);
  }

  return NS_OK;
}

nsresult
nsFormFillController::KeyPress(nsIDOMEvent* aEvent)
{
  NS_ASSERTION(mController, "should have a controller!");
  if (!mFocusedInput || !mController)
    return NS_OK;

  nsCOMPtr<nsIDOMKeyEvent> keyEvent = do_QueryInterface(aEvent);
  if (!keyEvent)
    return NS_ERROR_FAILURE;

  bool cancel = false;

  PRUint32 k;
  keyEvent->GetKeyCode(&k);
  switch (k) {
  case nsIDOMKeyEvent::DOM_VK_DELETE:
#ifndef XP_MACOSX
    mController->HandleDelete(&cancel);
    break;
  case nsIDOMKeyEvent::DOM_VK_BACK_SPACE:
    mController->HandleText();
    break;
#else
  case nsIDOMKeyEvent::DOM_VK_BACK_SPACE:
    {
      bool isShift = false;
      keyEvent->GetShiftKey(&isShift);

      if (isShift)
        mController->HandleDelete(&cancel);
      else
        mController->HandleText();

      break;
    }
#endif
  case nsIDOMKeyEvent::DOM_VK_PAGE_UP:
  case nsIDOMKeyEvent::DOM_VK_PAGE_DOWN:
    {
      bool isCtrl, isAlt, isMeta;
      keyEvent->GetCtrlKey(&isCtrl);
      keyEvent->GetAltKey(&isAlt);
      keyEvent->GetMetaKey(&isMeta);
      if (isCtrl || isAlt || isMeta)
        break;
    }
    /* fall through */
  case nsIDOMKeyEvent::DOM_VK_UP:
  case nsIDOMKeyEvent::DOM_VK_DOWN:
  case nsIDOMKeyEvent::DOM_VK_LEFT:
  case nsIDOMKeyEvent::DOM_VK_RIGHT:
    mController->HandleKeyNavigation(k, &cancel);
    break;
  case nsIDOMKeyEvent::DOM_VK_ESCAPE:
    mController->HandleEscape(&cancel);
    break;
  case nsIDOMKeyEvent::DOM_VK_TAB:
    mController->HandleTab();
    cancel = false;
    break;
  case nsIDOMKeyEvent::DOM_VK_RETURN:
    mController->HandleEnter(false, &cancel);
    break;
  }

  if (cancel) {
    aEvent->PreventDefault();
  }

  return NS_OK;
}

nsresult
nsFormFillController::MouseDown(nsIDOMEvent* aEvent)
{
  nsCOMPtr<nsIDOMMouseEvent> mouseEvent(do_QueryInterface(aEvent));
  if (!mouseEvent)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsIDOMEventTarget> target;
  aEvent->GetTarget(getter_AddRefs(target));
  nsCOMPtr<nsIDOMHTMLInputElement> targetInput = do_QueryInterface(target);
  if (!targetInput)
    return NS_OK;

  PRUint16 button;
  mouseEvent->GetButton(&button);
  if (button != 0)
    return NS_OK;

  bool isOpen = false;
  GetPopupOpen(&isOpen);
  if (isOpen)
    return NS_OK;

  nsCOMPtr<nsIAutoCompleteInput> input;
  mController->GetInput(getter_AddRefs(input));
  if (!input)
    return NS_OK;

  nsAutoString value;
  input->GetTextValue(value);
  if (value.Length() > 0) {
    // Show the popup with a filtered result set
    mController->SetSearchString(EmptyString());
    mController->HandleText();
  } else {
    // Show the popup with the complete result set.  Can't use HandleText()
    // because it doesn't display the popup if the input is blank.
    bool cancel = false;
    mController->HandleKeyNavigation(nsIDOMKeyEvent::DOM_VK_DOWN, &cancel);
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////
//// nsFormFillController

void
nsFormFillController::AddWindowListeners(nsIDOMWindow *aWindow)
{
  if (!aWindow)
    return;

  nsCOMPtr<nsPIDOMWindow> privateDOMWindow(do_QueryInterface(aWindow));
  nsIDOMEventTarget* target = nsnull;
  if (privateDOMWindow)
    target = privateDOMWindow->GetChromeEventHandler();

  if (!target)
    return;

  target->AddEventListener(NS_LITERAL_STRING("focus"), this,
                           true, false);
  target->AddEventListener(NS_LITERAL_STRING("blur"), this,
                           true, false);
  target->AddEventListener(NS_LITERAL_STRING("pagehide"), this,
                           true, false);
  target->AddEventListener(NS_LITERAL_STRING("mousedown"), this,
                           true, false);
  target->AddEventListener(NS_LITERAL_STRING("input"), this,
                           true, false);
  target->AddEventListener(NS_LITERAL_STRING("compositionstart"), this,
                           true, false);
  target->AddEventListener(NS_LITERAL_STRING("compositionend"), this,
                           true, false);
  target->AddEventListener(NS_LITERAL_STRING("contextmenu"), this,
                           true, false);

  // Note that any additional listeners added should ensure that they ignore
  // untrusted events, which might be sent by content that's up to no good.
}

void
nsFormFillController::RemoveWindowListeners(nsIDOMWindow *aWindow)
{
  if (!aWindow)
    return;

  StopControllingInput();

  nsCOMPtr<nsIDOMDocument> domDoc;
  aWindow->GetDocument(getter_AddRefs(domDoc));
  nsCOMPtr<nsIDocument> doc = do_QueryInterface(domDoc);
  PwmgrInputsEnumData ed(this, doc);
  mPwmgrInputs.Enumerate(RemoveForDocumentEnumerator, &ed);

  nsCOMPtr<nsPIDOMWindow> privateDOMWindow(do_QueryInterface(aWindow));
  nsIDOMEventTarget* target = nsnull;
  if (privateDOMWindow)
    target = privateDOMWindow->GetChromeEventHandler();

  if (!target)
    return;

  target->RemoveEventListener(NS_LITERAL_STRING("focus"), this, true);
  target->RemoveEventListener(NS_LITERAL_STRING("blur"), this, true);
  target->RemoveEventListener(NS_LITERAL_STRING("pagehide"), this, true);
  target->RemoveEventListener(NS_LITERAL_STRING("mousedown"), this, true);
  target->RemoveEventListener(NS_LITERAL_STRING("input"), this, true);
  target->RemoveEventListener(NS_LITERAL_STRING("compositionstart"), this,
                              true);
  target->RemoveEventListener(NS_LITERAL_STRING("compositionend"), this,
                              true);
  target->RemoveEventListener(NS_LITERAL_STRING("contextmenu"), this, true);
}

void
nsFormFillController::AddKeyListener(nsIDOMHTMLInputElement *aInput)
{
  if (!aInput)
    return;

  nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(aInput);

  target->AddEventListener(NS_LITERAL_STRING("keypress"), this,
                           true, false);
}

void
nsFormFillController::RemoveKeyListener()
{
  if (!mFocusedInput)
    return;

  nsCOMPtr<nsIDOMEventTarget> target = do_QueryInterface(mFocusedInput);
  target->RemoveEventListener(NS_LITERAL_STRING("keypress"), this, true);
}

void
nsFormFillController::StartControllingInput(nsIDOMHTMLInputElement *aInput)
{
  // Make sure we're not still attached to an input
  StopControllingInput();

  // Find the currently focused docShell
  nsCOMPtr<nsIDocShell> docShell = GetDocShellForInput(aInput);
  PRInt32 index = GetIndexOfDocShell(docShell);
  if (index < 0)
    return;

  // Cache the popup for the focused docShell
  mPopups->GetElementAt(index, getter_AddRefs(mFocusedPopup));

  nsCOMPtr<nsINode> node = do_QueryInterface(aInput);
  if (!node) {
    return;
  }

  AddKeyListener(aInput);
  
  node->AddMutationObserverUnlessExists(this);
  mFocusedInputNode = node;
  mFocusedInput = aInput;

  nsCOMPtr<nsIDOMHTMLElement> list;
  mFocusedInput->GetList(getter_AddRefs(list));
  nsCOMPtr<nsINode> listNode = do_QueryInterface(list);
  if (listNode) {
    listNode->AddMutationObserverUnlessExists(this);
    mListNode = listNode;
  }

  // Now we are the autocomplete controller's bitch
  mController->SetInput(this);
}

void
nsFormFillController::StopControllingInput()
{
  RemoveKeyListener();

  if (mListNode) {
    mListNode->RemoveMutationObserver(this);
    mListNode = nsnull;
  }

  // Reset the controller's input, but not if it has been switched
  // to another input already, which might happen if the user switches
  // focus by clicking another autocomplete textbox
  nsCOMPtr<nsIAutoCompleteInput> input;
  mController->GetInput(getter_AddRefs(input));
  if (input == this)
    mController->SetInput(nsnull);

  if (mFocusedInputNode) {
    MaybeRemoveMutationObserver(mFocusedInputNode);
    mFocusedInputNode = nsnull;
    mFocusedInput = nsnull;
  }
  mFocusedPopup = nsnull;
}

nsIDocShell *
nsFormFillController::GetDocShellForInput(nsIDOMHTMLInputElement *aInput)
{
  nsCOMPtr<nsIDOMDocument> domDoc;
  aInput->GetOwnerDocument(getter_AddRefs(domDoc));
  nsCOMPtr<nsIDocument> doc = do_QueryInterface(domDoc);
  NS_ENSURE_TRUE(doc, nsnull);
  nsCOMPtr<nsIWebNavigation> webNav = do_GetInterface(doc->GetWindow());
  nsCOMPtr<nsIDocShell> docShell = do_QueryInterface(webNav);
  return docShell;
}

nsIDOMWindow *
nsFormFillController::GetWindowForDocShell(nsIDocShell *aDocShell)
{
  nsCOMPtr<nsIContentViewer> contentViewer;
  aDocShell->GetContentViewer(getter_AddRefs(contentViewer));
  NS_ENSURE_TRUE(contentViewer, nsnull);

  nsCOMPtr<nsIDOMDocument> domDoc;
  contentViewer->GetDOMDocument(getter_AddRefs(domDoc));
  nsCOMPtr<nsIDocument> doc = do_QueryInterface(domDoc);
  NS_ENSURE_TRUE(doc, nsnull);

  return doc->GetWindow();
}

PRInt32
nsFormFillController::GetIndexOfDocShell(nsIDocShell *aDocShell)
{
  if (!aDocShell)
    return -1;

  // Loop through our cached docShells looking for the given docShell
  PRUint32 count;
  mDocShells->Count(&count);
  for (PRUint32 i = 0; i < count; ++i) {
    nsCOMPtr<nsIDocShell> docShell;
    mDocShells->GetElementAt(i, getter_AddRefs(docShell));
    if (docShell == aDocShell)
      return i;
  }

  // Recursively check the parent docShell of this one
  nsCOMPtr<nsIDocShellTreeItem> treeItem = do_QueryInterface(aDocShell);
  nsCOMPtr<nsIDocShellTreeItem> parentItem;
  treeItem->GetParent(getter_AddRefs(parentItem));
  if (parentItem) {
    nsCOMPtr<nsIDocShell> parentShell = do_QueryInterface(parentItem);
    return GetIndexOfDocShell(parentShell);
  }

  return -1;
}

NS_GENERIC_FACTORY_CONSTRUCTOR(nsFormFillController)

NS_DEFINE_NAMED_CID(NS_FORMFILLCONTROLLER_CID);

static const mozilla::Module::CIDEntry kSatchelCIDs[] = {
  { &kNS_FORMFILLCONTROLLER_CID, false, NULL, nsFormFillControllerConstructor },
  { NULL }
};

static const mozilla::Module::ContractIDEntry kSatchelContracts[] = {
  { "@mozilla.org/satchel/form-fill-controller;1", &kNS_FORMFILLCONTROLLER_CID },
  { NS_FORMHISTORYAUTOCOMPLETE_CONTRACTID, &kNS_FORMFILLCONTROLLER_CID },
  { NULL }
};

static const mozilla::Module kSatchelModule = {
  mozilla::Module::kVersion,
  kSatchelCIDs,
  kSatchelContracts
};

NSMODULE_DEFN(satchel) = &kSatchelModule;

