/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
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
 * Mozilla Corporation
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Olli Pettay <Olli.Pettay@helsinki.fi>
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

#include "nsDOMEventTargetHelper.h"
#include "nsContentUtils.h"
#include "nsEventDispatcher.h"
#include "nsGUIEvent.h"
#include "nsIDocument.h"

NS_IMPL_CYCLE_COLLECTION_CLASS(nsDOMEventListenerWrapper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsDOMEventListenerWrapper)
  NS_INTERFACE_MAP_ENTRY(nsIDOMEventListener)
NS_INTERFACE_MAP_END_AGGREGATED(mListener)

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsDOMEventListenerWrapper)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsDOMEventListenerWrapper)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsDOMEventListenerWrapper)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mListener)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsDOMEventListenerWrapper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mListener)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMETHODIMP
nsDOMEventListenerWrapper::HandleEvent(nsIDOMEvent* aEvent)
{
  return mListener->HandleEvent(aEvent);
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsDOMEventTargetHelper)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mListenerManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mScriptContext)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mOwner)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mListenerManager)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mScriptContext)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mOwner)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsDOMEventTargetHelper)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsPIDOMEventTarget)
  NS_INTERFACE_MAP_ENTRY(nsPIDOMEventTarget)
  NS_INTERFACE_MAP_ENTRY(nsIDOMEventTarget)
  NS_INTERFACE_MAP_ENTRY(nsIDOMNSEventTarget)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF_AMBIGUOUS(nsDOMEventTargetHelper,
                                          nsPIDOMEventTarget)
NS_IMPL_CYCLE_COLLECTING_RELEASE_AMBIGUOUS(nsDOMEventTargetHelper,
                                           nsPIDOMEventTarget)


NS_IMETHODIMP
nsDOMEventTargetHelper::AddEventListener(const nsAString& aType,
                                         nsIDOMEventListener* aListener,
                                         PRBool aUseCapture)
{
  return AddEventListener(aType, aListener, aUseCapture, PR_FALSE, 0);
}

NS_IMETHODIMP
nsDOMEventTargetHelper::RemoveEventListener(const nsAString& aType,
                                            nsIDOMEventListener* aListener,
                                            PRBool aUseCapture)
{
  nsIEventListenerManager* elm = GetListenerManager(PR_FALSE);
  if (elm) {
    PRInt32 flags = aUseCapture ? NS_EVENT_FLAG_CAPTURE : NS_EVENT_FLAG_BUBBLE;
    elm->RemoveEventListenerByType(aListener, aType, flags, nsnull);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDOMEventTargetHelper::AddEventListener(const nsAString& aType,
                                         nsIDOMEventListener *aListener,
                                         PRBool aUseCapture,
                                         PRBool aWantsUntrusted,
                                         PRUint8 optional_argc)
{
  NS_ASSERTION(!aWantsUntrusted || optional_argc > 0,
               "Won't check if this is chrome, you want to set "
               "aWantsUntrusted to PR_FALSE or make the aWantsUntrusted "
               "explicit by making optional_argc non-zero.");

  nsIEventListenerManager* elm = GetListenerManager(PR_TRUE);
  NS_ENSURE_STATE(elm);
  PRInt32 flags = aUseCapture ? NS_EVENT_FLAG_CAPTURE : NS_EVENT_FLAG_BUBBLE;

  if (optional_argc == 0) {
    nsresult rv;
    nsIScriptContext* context = GetContextForEventHandlers(&rv);
    NS_ENSURE_SUCCESS(rv, rv);
    nsCOMPtr<nsIDocument> doc =
      nsContentUtils::GetDocumentFromScriptContext(context);
    aWantsUntrusted = doc && !nsContentUtils::IsChromeDoc(doc);
  }

  if (aWantsUntrusted) {
    flags |= NS_PRIV_EVENT_UNTRUSTED_PERMITTED;
  }

  return elm->AddEventListenerByType(aListener, aType, flags, nsnull);
}

NS_IMETHODIMP
nsDOMEventTargetHelper::GetScriptTypeID(PRUint32 *aLang)
{
  *aLang = mLang;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMEventTargetHelper::SetScriptTypeID(PRUint32 aLang)
{
  mLang = aLang;
  return NS_OK;
}

NS_IMETHODIMP
nsDOMEventTargetHelper::DispatchEvent(nsIDOMEvent* aEvent, PRBool* aRetVal)
{
  nsEventStatus status = nsEventStatus_eIgnore;
  nsresult rv =
    nsEventDispatcher::DispatchDOMEvent(static_cast<nsPIDOMEventTarget*>(this),
                                        nsnull, aEvent, nsnull, &status);

  *aRetVal = (status != nsEventStatus_eConsumeNoDefault);
  return rv;
}

nsresult
nsDOMEventTargetHelper::RemoveAddEventListener(const nsAString& aType,
                                               nsRefPtr<nsDOMEventListenerWrapper>& aCurrent,
                                               nsIDOMEventListener* aNew)
{
  if (aCurrent) {
    RemoveEventListener(aType, aCurrent, PR_FALSE);
    aCurrent = nsnull;
  }
  if (aNew) {
    aCurrent = new nsDOMEventListenerWrapper(aNew);
    NS_ENSURE_TRUE(aCurrent, NS_ERROR_OUT_OF_MEMORY);
    AddEventListener(aType, aCurrent, PR_FALSE);
  }
  return NS_OK;
}

nsresult
nsDOMEventTargetHelper::GetInnerEventListener(nsRefPtr<nsDOMEventListenerWrapper>& aWrapper,
                                              nsIDOMEventListener** aListener)
{
  NS_ENSURE_ARG_POINTER(aListener);
  if (aWrapper) {
    NS_ADDREF(*aListener = aWrapper->GetInner());
  } else {
    *aListener = nsnull;
  }
  return NS_OK;
}


nsresult
nsDOMEventTargetHelper::PreHandleEvent(nsEventChainPreVisitor& aVisitor)
{
  aVisitor.mCanHandle = PR_TRUE;
  aVisitor.mParentTarget = nsnull;
  return NS_OK;
}

nsresult
nsDOMEventTargetHelper::PostHandleEvent(nsEventChainPostVisitor& aVisitor)
{
  return NS_OK;
}

nsresult
nsDOMEventTargetHelper::DispatchDOMEvent(nsEvent* aEvent,
                                         nsIDOMEvent* aDOMEvent,
                                         nsPresContext* aPresContext,
                                         nsEventStatus* aEventStatus)
{
  return
    nsEventDispatcher::DispatchDOMEvent(static_cast<nsPIDOMEventTarget*>(this),
                                        aEvent, aDOMEvent, aPresContext,
                                        aEventStatus);
}

nsIEventListenerManager*
nsDOMEventTargetHelper::GetListenerManager(PRBool aCreateIfNotFound)
{
  if (!mListenerManager) {
    if (!aCreateIfNotFound) {
      return nsnull;
    }
    nsresult rv = NS_NewEventListenerManager(getter_AddRefs(mListenerManager));
    NS_ENSURE_SUCCESS(rv, nsnull);
    mListenerManager->SetListenerTarget(static_cast<nsPIDOMEventTarget*>(this));
  }

  return mListenerManager;
}

nsresult
nsDOMEventTargetHelper::AddEventListenerByIID(nsIDOMEventListener *aListener,
                                              const nsIID& aIID)
{
  nsIEventListenerManager* elm = GetListenerManager(PR_TRUE);
  NS_ENSURE_STATE(elm);
  return elm->AddEventListenerByIID(aListener, aIID, NS_EVENT_FLAG_BUBBLE);
}

nsresult
nsDOMEventTargetHelper::RemoveEventListenerByIID(nsIDOMEventListener *aListener,
                                                 const nsIID& aIID)
{
  nsIEventListenerManager* elm = GetListenerManager(PR_FALSE);
  return elm ?
    elm->RemoveEventListenerByIID(aListener, aIID, NS_EVENT_FLAG_BUBBLE) :
    NS_OK;
}

nsresult
nsDOMEventTargetHelper::GetSystemEventGroup(nsIDOMEventGroup** aGroup)
{
  nsIEventListenerManager* elm = GetListenerManager(PR_TRUE);
  NS_ENSURE_STATE(elm);
  return elm->GetSystemEventGroupLM(aGroup);
}

nsIScriptContext*
nsDOMEventTargetHelper::GetContextForEventHandlers(nsresult* aRv)
{
  *aRv = CheckInnerWindowCorrectness();
  if (NS_FAILED(*aRv)) {
    return nsnull;
  }
  return mScriptContext;
}

