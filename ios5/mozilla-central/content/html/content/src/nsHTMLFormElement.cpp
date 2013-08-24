/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "nsHTMLFormElement.h"
#include "nsIHTMLDocument.h"
#include "nsIDOMEventTarget.h"
#include "nsEventStateManager.h"
#include "nsEventStates.h"
#include "nsGkAtoms.h"
#include "nsStyleConsts.h"
#include "nsPresContext.h"
#include "nsIDocument.h"
#include "nsIFrame.h"
#include "nsIFormControlFrame.h"
#include "nsDOMError.h"
#include "nsContentUtils.h"
#include "nsInterfaceHashtable.h"
#include "nsContentList.h"
#include "nsGUIEvent.h"
#include "nsCOMArray.h"
#include "nsAutoPtr.h"
#include "nsTArray.h"
#include "nsIMutableArray.h"

// form submission
#include "nsIFormSubmitObserver.h"
#include "nsIObserverService.h"
#include "nsICategoryManager.h"
#include "nsCategoryManagerUtils.h"
#include "nsISimpleEnumerator.h"
#include "nsRange.h"
#include "nsIScriptSecurityManager.h"
#include "nsNetUtil.h"
#include "nsIWebProgress.h"
#include "nsIDocShell.h"
#include "nsFormData.h"
#include "nsFormSubmissionConstants.h"

// radio buttons
#include "nsIDOMHTMLInputElement.h"
#include "nsHTMLInputElement.h"
#include "nsIRadioVisitor.h"

#include "nsLayoutUtils.h"

#include "nsEventDispatcher.h"

#include "mozAutoDocUpdate.h"
#include "nsIHTMLCollection.h"

#include "nsIConstraintValidation.h"

#include "nsIDOMHTMLButtonElement.h"
#include "dombindings.h"

using namespace mozilla::dom;

static const int NS_FORM_CONTROL_LIST_HASHTABLE_SIZE = 16;

static const PRUint8 NS_FORM_AUTOCOMPLETE_ON  = 1;
static const PRUint8 NS_FORM_AUTOCOMPLETE_OFF = 0;

static const nsAttrValue::EnumTable kFormAutocompleteTable[] = {
  { "on",  NS_FORM_AUTOCOMPLETE_ON },
  { "off", NS_FORM_AUTOCOMPLETE_OFF },
  { 0 }
};
// Default autocomplete value is 'on'.
static const nsAttrValue::EnumTable* kFormDefaultAutocomplete = &kFormAutocompleteTable[0];

// nsHTMLFormElement

bool nsHTMLFormElement::gFirstFormSubmitted = false;
bool nsHTMLFormElement::gPasswordManagerInitialized = false;


// nsFormControlList
class nsFormControlList : public nsIHTMLCollection,
                          public nsWrapperCache
{
public:
  nsFormControlList(nsHTMLFormElement* aForm);
  virtual ~nsFormControlList();

  nsresult Init();

  void DropFormReference();

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  // nsIDOMHTMLCollection interface
  NS_DECL_NSIDOMHTMLCOLLECTION

  virtual nsINode* GetParentObject()
  {
    return mForm;
  }

  nsresult AddElementToTable(nsGenericHTMLFormElement* aChild,
                             const nsAString& aName);
  nsresult RemoveElementFromTable(nsGenericHTMLFormElement* aChild,
                                  const nsAString& aName);
  nsresult IndexOfControl(nsIFormControl* aControl,
                          PRInt32* aIndex);

  nsISupports* NamedItemInternal(const nsAString& aName, bool aFlushContent);
  
  /**
   * Create a sorted list of form control elements. This list is sorted
   * in document order and contains the controls in the mElements and
   * mNotInElements list. This function does not add references to the
   * elements.
   *
   * @param aControls The list of sorted controls[out].
   * @return NS_OK or NS_ERROR_OUT_OF_MEMORY.
   */
  nsresult GetSortedControls(nsTArray<nsGenericHTMLFormElement*>& aControls) const;

  // nsWrapperCache
  virtual JSObject* WrapObject(JSContext *cx, JSObject *scope,
                               bool *triedToWrap)
  {
    return mozilla::dom::binding::HTMLCollection::create(cx, scope, this,
                                                         triedToWrap);
  }

  nsHTMLFormElement* mForm;  // WEAK - the form owns me

  nsTArray<nsGenericHTMLFormElement*> mElements;  // Holds WEAK references - bug 36639

  // This array holds on to all form controls that are not contained
  // in mElements (form.elements in JS, see ShouldBeInFormControl()).
  // This is needed to properly clean up the bi-directional references
  // (both weak and strong) between the form and its form controls.

  nsTArray<nsGenericHTMLFormElement*> mNotInElements; // Holds WEAK references

  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(nsFormControlList)

protected:
  // Drop all our references to the form elements
  void Clear();

  // Flush out the content model so it's up to date.
  void FlushPendingNotifications();
  
  // A map from an ID or NAME attribute to the form control(s), this
  // hash holds strong references either to the named form control, or
  // to a list of named form controls, in the case where this hash
  // holds on to a list of named form controls the list has weak
  // references to the form control.

  nsInterfaceHashtable<nsStringHashKey,nsISupports> mNameLookupTable;
};

static bool
ShouldBeInElements(nsIFormControl* aFormControl)
{
  // For backwards compatibility (with 4.x and IE) we must not add
  // <input type=image> elements to the list of form controls in a
  // form.

  switch (aFormControl->GetType()) {
  case NS_FORM_BUTTON_BUTTON :
  case NS_FORM_BUTTON_RESET :
  case NS_FORM_BUTTON_SUBMIT :
  case NS_FORM_INPUT_BUTTON :
  case NS_FORM_INPUT_CHECKBOX :
  case NS_FORM_INPUT_EMAIL :
  case NS_FORM_INPUT_FILE :
  case NS_FORM_INPUT_HIDDEN :
  case NS_FORM_INPUT_RESET :
  case NS_FORM_INPUT_PASSWORD :
  case NS_FORM_INPUT_RADIO :
  case NS_FORM_INPUT_SEARCH :
  case NS_FORM_INPUT_SUBMIT :
  case NS_FORM_INPUT_TEXT :
  case NS_FORM_INPUT_TEL :
  case NS_FORM_INPUT_URL :
  case NS_FORM_INPUT_NUMBER :
  case NS_FORM_SELECT :
  case NS_FORM_TEXTAREA :
  case NS_FORM_FIELDSET :
  case NS_FORM_OBJECT :
  case NS_FORM_OUTPUT :
    return true;
  }

  // These form control types are not supposed to end up in the
  // form.elements array
  //
  // NS_FORM_INPUT_IMAGE
  // NS_FORM_LABEL

  return false;
}

// nsHTMLFormElement implementation

// construction, destruction
nsGenericHTMLElement*
NS_NewHTMLFormElement(already_AddRefed<nsINodeInfo> aNodeInfo,
                      FromParser aFromParser)
{
  nsHTMLFormElement* it = new nsHTMLFormElement(aNodeInfo);

  nsresult rv = it->Init();

  if (NS_FAILED(rv)) {
    delete it;
    return nsnull;
  }

  return it;
}

nsHTMLFormElement::nsHTMLFormElement(already_AddRefed<nsINodeInfo> aNodeInfo)
  : nsGenericHTMLElement(aNodeInfo),
    mGeneratingSubmit(false),
    mGeneratingReset(false),
    mIsSubmitting(false),
    mDeferSubmission(false),
    mNotifiedObservers(false),
    mNotifiedObserversResult(false),
    mSubmitPopupState(openAbused),
    mSubmitInitiatedFromUserInput(false),
    mPendingSubmission(nsnull),
    mSubmittingRequest(nsnull),
    mDefaultSubmitElement(nsnull),
    mFirstSubmitInElements(nsnull),
    mFirstSubmitNotInElements(nsnull),
    mInvalidElementsCount(0),
    mEverTriedInvalidSubmit(false)
{
}

nsHTMLFormElement::~nsHTMLFormElement()
{
  if (mControls) {
    mControls->DropFormReference();
  }
}

nsresult
nsHTMLFormElement::Init()
{
  mControls = new nsFormControlList(this);
  if (!mControls) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  nsresult rv = mControls->Init();
  
  if (NS_FAILED(rv))
  {
    mControls = nsnull;
    return rv;
  }
  
  mSelectedRadioButtons.Init(4);
  mRequiredRadioButtonCounts.Init(4);
  mValueMissingRadioGroups.Init(4);

  return NS_OK;
}


// nsISupports

static PLDHashOperator
ElementTraverser(const nsAString& key, nsIDOMHTMLInputElement* element,
                 void* userArg)
{
  nsCycleCollectionTraversalCallback *cb = 
    static_cast<nsCycleCollectionTraversalCallback*>(userArg);
 
  cb->NoteXPCOMChild(element);
  return PL_DHASH_NEXT;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsHTMLFormElement)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsHTMLFormElement,
                                                  nsGenericHTMLElement)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR_AMBIGUOUS(mControls,
                                                       nsIDOMHTMLCollection)
  tmp->mSelectedRadioButtons.EnumerateRead(ElementTraverser, &cb);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_ADDREF_INHERITED(nsHTMLFormElement, nsGenericElement) 
NS_IMPL_RELEASE_INHERITED(nsHTMLFormElement, nsGenericElement) 


DOMCI_NODE_DATA(HTMLFormElement, nsHTMLFormElement)

// QueryInterface implementation for nsHTMLFormElement
NS_INTERFACE_TABLE_HEAD_CYCLE_COLLECTION_INHERITED(nsHTMLFormElement)
  NS_HTML_CONTENT_INTERFACE_TABLE4(nsHTMLFormElement,
                                   nsIDOMHTMLFormElement,
                                   nsIForm,
                                   nsIWebProgressListener,
                                   nsIRadioGroupContainer)
  NS_HTML_CONTENT_INTERFACE_TABLE_TO_MAP_SEGUE(nsHTMLFormElement,
                                               nsGenericHTMLElement)
NS_HTML_CONTENT_INTERFACE_TABLE_TAIL_CLASSINFO(HTMLFormElement)


// nsIDOMHTMLFormElement

NS_IMPL_ELEMENT_CLONE_WITH_INIT(nsHTMLFormElement)

NS_IMETHODIMP
nsHTMLFormElement::GetElements(nsIDOMHTMLCollection** aElements)
{
  *aElements = mControls;
  NS_ADDREF(*aElements);
  return NS_OK;
}

nsresult
nsHTMLFormElement::SetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                           nsIAtom* aPrefix, const nsAString& aValue,
                           bool aNotify)
{
  if ((aName == nsGkAtoms::action || aName == nsGkAtoms::target) &&
      aNameSpaceID == kNameSpaceID_None) {
    if (mPendingSubmission) {
      // aha, there is a pending submission that means we're in
      // the script and we need to flush it. let's tell it
      // that the event was ignored to force the flush.
      // the second argument is not playing a role at all.
      FlushPendingSubmission();
    }
    // Don't forget we've notified the password manager already if the
    // page sets the action/target in the during submit. (bug 343182)
    bool notifiedObservers = mNotifiedObservers;
    ForgetCurrentSubmission();
    mNotifiedObservers = notifiedObservers;
  }
  return nsGenericHTMLElement::SetAttr(aNameSpaceID, aName, aPrefix, aValue,
                                       aNotify);
}

nsresult
nsHTMLFormElement::AfterSetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                                const nsAttrValue* aValue, bool aNotify)
{
  if (aName == nsGkAtoms::novalidate && aNameSpaceID == kNameSpaceID_None) {
    // Update all form elements states because they might be [no longer]
    // affected by :-moz-ui-valid or :-moz-ui-invalid.
    for (PRUint32 i = 0, length = mControls->mElements.Length();
         i < length; ++i) {
      mControls->mElements[i]->UpdateState(true);
    }

    for (PRUint32 i = 0, length = mControls->mNotInElements.Length();
         i < length; ++i) {
      mControls->mNotInElements[i]->UpdateState(true);
    }
  }

  return nsGenericHTMLElement::AfterSetAttr(aNameSpaceID, aName, aValue, aNotify);
}

NS_IMPL_STRING_ATTR(nsHTMLFormElement, AcceptCharset, acceptcharset)
NS_IMPL_ACTION_ATTR(nsHTMLFormElement, Action, action)
NS_IMPL_ENUM_ATTR_DEFAULT_VALUE(nsHTMLFormElement, Autocomplete, autocomplete,
                                kFormDefaultAutocomplete->tag)
NS_IMPL_ENUM_ATTR_DEFAULT_VALUE(nsHTMLFormElement, Enctype, enctype,
                                kFormDefaultEnctype->tag)
NS_IMPL_ENUM_ATTR_DEFAULT_VALUE(nsHTMLFormElement, Method, method,
                                kFormDefaultMethod->tag)
NS_IMPL_BOOL_ATTR(nsHTMLFormElement, NoValidate, novalidate)
NS_IMPL_STRING_ATTR(nsHTMLFormElement, Name, name)
NS_IMPL_STRING_ATTR(nsHTMLFormElement, Target, target)

NS_IMETHODIMP
nsHTMLFormElement::Submit()
{
  // Send the submit event
  nsresult rv = NS_OK;
  nsRefPtr<nsPresContext> presContext = GetPresContext();
  if (mPendingSubmission) {
    // aha, we have a pending submission that was not flushed
    // (this happens when form.submit() is called twice)
    // we have to delete it and build a new one since values
    // might have changed inbetween (we emulate IE here, that's all)
    mPendingSubmission = nsnull;
  }

  rv = DoSubmitOrReset(nsnull, NS_FORM_SUBMIT);
  return rv;
}

NS_IMETHODIMP
nsHTMLFormElement::Reset()
{
  nsFormEvent event(true, NS_FORM_RESET);
  nsEventDispatcher::Dispatch(static_cast<nsIContent*>(this), nsnull,
                              &event);
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLFormElement::CheckValidity(bool* retVal)
{
  *retVal = CheckFormValidity(nsnull);
  return NS_OK;
}

bool
nsHTMLFormElement::ParseAttribute(PRInt32 aNamespaceID,
                                  nsIAtom* aAttribute,
                                  const nsAString& aValue,
                                  nsAttrValue& aResult)
{
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::method) {
      return aResult.ParseEnumValue(aValue, kFormMethodTable, false);
    }
    if (aAttribute == nsGkAtoms::enctype) {
      return aResult.ParseEnumValue(aValue, kFormEnctypeTable, false);
    }
    if (aAttribute == nsGkAtoms::autocomplete) {
      return aResult.ParseEnumValue(aValue, kFormAutocompleteTable, false);
    }
  }

  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aResult);
}

nsresult
nsHTMLFormElement::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              bool aCompileEventHandlers)
{
  nsresult rv = nsGenericHTMLElement::BindToTree(aDocument, aParent,
                                                 aBindingParent,
                                                 aCompileEventHandlers);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIHTMLDocument> htmlDoc(do_QueryInterface(aDocument));
  if (htmlDoc) {
    htmlDoc->AddedForm();
  }

  return rv;
}

static void
MarkOrphans(const nsTArray<nsGenericHTMLFormElement*> aArray)
{
  PRUint32 length = aArray.Length();
  for (PRUint32 i = 0; i < length; ++i) {
    aArray[i]->SetFlags(MAYBE_ORPHAN_FORM_ELEMENT);
  }
}

static void
CollectOrphans(nsINode* aRemovalRoot, nsTArray<nsGenericHTMLFormElement*> aArray
#ifdef DEBUG
               , nsIDOMHTMLFormElement* aThisForm
#endif
               )
{
  // Put a script blocker around all the notifications we're about to do.
  nsAutoScriptBlocker scriptBlocker;

  // Walk backwards so that if we remove elements we can just keep iterating
  PRUint32 length = aArray.Length();
  for (PRUint32 i = length; i > 0; --i) {
    nsGenericHTMLFormElement* node = aArray[i-1];

    // Now if MAYBE_ORPHAN_FORM_ELEMENT is not set, that would mean that the
    // node is in fact a descendant of the form and hence should stay in the
    // form.  If it _is_ set, then we need to check whether the node is a
    // descendant of aRemovalRoot.  If it is, we leave it in the form.  See
    // also the code in nsGenericHTMLFormElement::FindForm.
#ifdef DEBUG
    bool removed = false;
#endif
    if (node->HasFlag(MAYBE_ORPHAN_FORM_ELEMENT)) {
      node->UnsetFlags(MAYBE_ORPHAN_FORM_ELEMENT);
      if (!nsContentUtils::ContentIsDescendantOf(node, aRemovalRoot)) {
        node->ClearForm(true);

        // When a form control loses its form owner, its state can change.
        node->UpdateState(true);
#ifdef DEBUG
        removed = true;
#endif
      }
    }

#ifdef DEBUG
    if (!removed) {
      nsCOMPtr<nsIDOMHTMLFormElement> form;
      node->GetForm(getter_AddRefs(form));
      NS_ASSERTION(form == aThisForm, "How did that happen?");
    }
#endif /* DEBUG */
  }
}

void
nsHTMLFormElement::UnbindFromTree(bool aDeep, bool aNullParent)
{
  nsCOMPtr<nsIHTMLDocument> oldDocument = do_QueryInterface(GetCurrentDoc());

  // Mark all of our controls as maybe being orphans
  MarkOrphans(mControls->mElements);
  MarkOrphans(mControls->mNotInElements);

  nsGenericHTMLElement::UnbindFromTree(aDeep, aNullParent);

  nsINode* ancestor = this;
  nsINode* cur;
  do {
    cur = ancestor->GetNodeParent();
    if (!cur) {
      break;
    }
    ancestor = cur;
  } while (1);
  
  CollectOrphans(ancestor, mControls->mElements
#ifdef DEBUG
                 , this
#endif                 
                 );
  CollectOrphans(ancestor, mControls->mNotInElements
#ifdef DEBUG
                 , this
#endif                 
                 );

  if (oldDocument) {
    oldDocument->RemovedForm();
  }     
  ForgetCurrentSubmission();
}

nsresult
nsHTMLFormElement::PreHandleEvent(nsEventChainPreVisitor& aVisitor)
{
  aVisitor.mWantsWillHandleEvent = true;
  if (aVisitor.mEvent->originalTarget == static_cast<nsIContent*>(this)) {
    PRUint32 msg = aVisitor.mEvent->message;
    if (msg == NS_FORM_SUBMIT) {
      if (mGeneratingSubmit) {
        aVisitor.mCanHandle = false;
        return NS_OK;
      }
      mGeneratingSubmit = true;

      // let the form know that it needs to defer the submission,
      // that means that if there are scripted submissions, the
      // latest one will be deferred until after the exit point of the handler.
      mDeferSubmission = true;
    }
    else if (msg == NS_FORM_RESET) {
      if (mGeneratingReset) {
        aVisitor.mCanHandle = false;
        return NS_OK;
      }
      mGeneratingReset = true;
    }
  }
  return nsGenericHTMLElement::PreHandleEvent(aVisitor);
}

nsresult
nsHTMLFormElement::WillHandleEvent(nsEventChainPostVisitor& aVisitor)
{
  // If this is the bubble stage and there is a nested form below us which
  // received a submit event we do *not* want to handle the submit event
  // for this form too.
  if ((aVisitor.mEvent->message == NS_FORM_SUBMIT ||
       aVisitor.mEvent->message == NS_FORM_RESET) &&
      aVisitor.mEvent->flags & NS_EVENT_FLAG_BUBBLE &&
      aVisitor.mEvent->originalTarget != static_cast<nsIContent*>(this)) {
    aVisitor.mEvent->flags |= NS_EVENT_FLAG_STOP_DISPATCH;
  }
  return NS_OK;
}

nsresult
nsHTMLFormElement::PostHandleEvent(nsEventChainPostVisitor& aVisitor)
{
  if (aVisitor.mEvent->originalTarget == static_cast<nsIContent*>(this)) {
    PRUint32 msg = aVisitor.mEvent->message;
    if (msg == NS_FORM_SUBMIT) {
      // let the form know not to defer subsequent submissions
      mDeferSubmission = false;
    }

    if (aVisitor.mEventStatus == nsEventStatus_eIgnore) {
      switch (msg) {
        case NS_FORM_RESET:
        case NS_FORM_SUBMIT:
        {
          if (mPendingSubmission && msg == NS_FORM_SUBMIT) {
            // tell the form to forget a possible pending submission.
            // the reason is that the script returned true (the event was
            // ignored) so if there is a stored submission, it will miss
            // the name/value of the submitting element, thus we need
            // to forget it and the form element will build a new one
            mPendingSubmission = nsnull;
          }
          DoSubmitOrReset(aVisitor.mEvent, msg);
        }
        break;
      }
    } else {
      if (msg == NS_FORM_SUBMIT) {
        // tell the form to flush a possible pending submission.
        // the reason is that the script returned false (the event was
        // not ignored) so if there is a stored submission, it needs to
        // be submitted immediatelly.
        FlushPendingSubmission();
      }
    }

    if (msg == NS_FORM_SUBMIT) {
      mGeneratingSubmit = false;
    }
    else if (msg == NS_FORM_RESET) {
      mGeneratingReset = false;
    }
  }
  return NS_OK;
}

nsresult
nsHTMLFormElement::DoSubmitOrReset(nsEvent* aEvent,
                                   PRInt32 aMessage)
{
  // Make sure the presentation is up-to-date
  nsIDocument* doc = GetCurrentDoc();
  if (doc) {
    doc->FlushPendingNotifications(Flush_ContentAndNotify);
  }

  // JBK Don't get form frames anymore - bug 34297

  // Submit or Reset the form
  if (NS_FORM_RESET == aMessage) {
    return DoReset();
  }

  if (NS_FORM_SUBMIT == aMessage) {
    // Don't submit if we're not in a document.
    if (!doc) {
      return NS_OK;
    }
    return DoSubmit(aEvent);
  }

  MOZ_ASSERT(false);
  return NS_OK;
}

nsresult
nsHTMLFormElement::DoReset()
{
  // JBK walk the elements[] array instead of form frame controls - bug 34297
  PRUint32 numElements = GetElementCount();
  for (PRUint32 elementX = 0; elementX < numElements; ++elementX) {
    // Hold strong ref in case the reset does something weird
    nsCOMPtr<nsIFormControl> controlNode = GetElementAt(elementX);
    if (controlNode) {
      controlNode->Reset();
    }
  }

  return NS_OK;
}

#define NS_ENSURE_SUBMIT_SUCCESS(rv)                                          \
  if (NS_FAILED(rv)) {                                                        \
    ForgetCurrentSubmission();                                                \
    return rv;                                                                \
  }

nsresult
nsHTMLFormElement::DoSubmit(nsEvent* aEvent)
{
  NS_ASSERTION(GetCurrentDoc(), "Should never get here without a current doc");

  if (mIsSubmitting) {
    NS_WARNING("Preventing double form submission");
    // XXX Should this return an error?
    return NS_OK;
  }

  // Mark us as submitting so that we don't try to submit again
  mIsSubmitting = true;
  NS_ASSERTION(!mWebProgress && !mSubmittingRequest, "Web progress / submitting request should not exist here!");

  nsAutoPtr<nsFormSubmission> submission;

  //
  // prepare the submission object
  //
  nsresult rv = BuildSubmission(getter_Transfers(submission), aEvent);
  if (NS_FAILED(rv)) {
    mIsSubmitting = false;
    return rv;
  }

  // XXXbz if the script global is that for an sXBL/XBL2 doc, it won't
  // be a window...
  nsPIDOMWindow *window = OwnerDoc()->GetWindow();

  if (window) {
    mSubmitPopupState = window->GetPopupControlState();
  } else {
    mSubmitPopupState = openAbused;
  }

  mSubmitInitiatedFromUserInput = nsEventStateManager::IsHandlingUserInput();

  if(mDeferSubmission) { 
    // we are in an event handler, JS submitted so we have to
    // defer this submission. let's remember it and return
    // without submitting
    mPendingSubmission = submission;
    // ensure reentrancy
    mIsSubmitting = false;
    return NS_OK; 
  } 
  
  // 
  // perform the submission
  //
  return SubmitSubmission(submission); 
}

nsresult
nsHTMLFormElement::BuildSubmission(nsFormSubmission** aFormSubmission, 
                                   nsEvent* aEvent)
{
  NS_ASSERTION(!mPendingSubmission, "tried to build two submissions!");

  // Get the originating frame (failure is non-fatal)
  nsGenericHTMLElement* originatingElement = nsnull;
  if (aEvent) {
    if (NS_FORM_EVENT == aEvent->eventStructType) {
      nsIContent* originator = ((nsFormEvent *)aEvent)->originator;
      if (originator) {
        if (!originator->IsHTML()) {
          return NS_ERROR_UNEXPECTED;
        }
        originatingElement =
          static_cast<nsGenericHTMLElement*>(((nsFormEvent *)aEvent)->originator);
      }
    }
  }

  nsresult rv;

  //
  // Get the submission object
  //
  rv = GetSubmissionFromForm(this, originatingElement, aFormSubmission);
  NS_ENSURE_SUBMIT_SUCCESS(rv);

  //
  // Dump the data into the submission object
  //
  rv = WalkFormElements(*aFormSubmission);
  NS_ENSURE_SUBMIT_SUCCESS(rv);

  return NS_OK;
}

nsresult
nsHTMLFormElement::SubmitSubmission(nsFormSubmission* aFormSubmission)
{
  nsresult rv;
  nsIContent* originatingElement = aFormSubmission->GetOriginatingElement();

  //
  // Get the action and target
  //
  nsCOMPtr<nsIURI> actionURI;
  rv = GetActionURL(getter_AddRefs(actionURI), originatingElement);
  NS_ENSURE_SUBMIT_SUCCESS(rv);

  if (!actionURI) {
    mIsSubmitting = false;
    return NS_OK;
  }

  // If there is no link handler, then we won't actually be able to submit.
  nsIDocument* doc = GetCurrentDoc();
  nsCOMPtr<nsISupports> container = doc ? doc->GetContainer() : nsnull;
  nsCOMPtr<nsILinkHandler> linkHandler(do_QueryInterface(container));
  if (!linkHandler || IsEditable()) {
    mIsSubmitting = false;
    return NS_OK;
  }

  // javascript URIs are not really submissions; they just call a function.
  // Also, they may synchronously call submit(), and we want them to be able to
  // do so while still disallowing other double submissions. (Bug 139798)
  // Note that any other URI types that are of equivalent type should also be
  // added here.
  // XXXbz this is a mess.  The real issue here is that nsJSChannel sets the
  // LOAD_BACKGROUND flag, so doesn't notify us, compounded by the fact that
  // the JS executes before we forget the submission in OnStateChange on
  // STATE_STOP.  As a result, we have to make sure that we simply pretend
  // we're not submitting when submitting to a JS URL.  That's kinda bogus, but
  // there we are.
  bool schemeIsJavaScript = false;
  if (NS_SUCCEEDED(actionURI->SchemeIs("javascript", &schemeIsJavaScript)) &&
      schemeIsJavaScript) {
    mIsSubmitting = false;
  }

  // The target is the originating element formtarget attribute if the element
  // is a submit control and has such an attribute.
  // Otherwise, the target is the form owner's target attribute,
  // if it has such an attribute.
  // Finally, if one of the child nodes of the head element is a base element
  // with a target attribute, then the value of the target attribute of the
  // first such base element; or, if there is no such element, the empty string.
  nsAutoString target;
  if (!(originatingElement && originatingElement->GetAttr(kNameSpaceID_None,
                                                          nsGkAtoms::formtarget,
                                                          target)) &&
      !GetAttr(kNameSpaceID_None, nsGkAtoms::target, target)) {
    GetBaseTarget(target);
  }

  //
  // Notify observers of submit
  //
  bool cancelSubmit = false;
  if (mNotifiedObservers) {
    cancelSubmit = mNotifiedObserversResult;
  } else {
    rv = NotifySubmitObservers(actionURI, &cancelSubmit, true);
    NS_ENSURE_SUBMIT_SUCCESS(rv);
  }

  if (cancelSubmit) {
    mIsSubmitting = false;
    return NS_OK;
  }

  cancelSubmit = false;
  rv = NotifySubmitObservers(actionURI, &cancelSubmit, false);
  NS_ENSURE_SUBMIT_SUCCESS(rv);

  if (cancelSubmit) {
    mIsSubmitting = false;
    return NS_OK;
  }

  //
  // Submit
  //
  nsCOMPtr<nsIDocShell> docShell;

  {
    nsAutoPopupStatePusher popupStatePusher(mSubmitPopupState);

    nsAutoHandlingUserInputStatePusher userInpStatePusher(
                                         mSubmitInitiatedFromUserInput,
                                         nsnull, doc);

    nsCOMPtr<nsIInputStream> postDataStream;
    rv = aFormSubmission->GetEncodedSubmission(actionURI,
                                               getter_AddRefs(postDataStream));
    NS_ENSURE_SUBMIT_SUCCESS(rv);

    rv = linkHandler->OnLinkClickSync(this, actionURI,
                                      target.get(),
                                      postDataStream, nsnull,
                                      getter_AddRefs(docShell),
                                      getter_AddRefs(mSubmittingRequest));
    NS_ENSURE_SUBMIT_SUCCESS(rv);
  }

  // Even if the submit succeeds, it's possible for there to be no docshell
  // or request; for example, if it's to a named anchor within the same page
  // the submit will not really do anything.
  if (docShell) {
    // If the channel is pending, we have to listen for web progress.
    bool pending = false;
    mSubmittingRequest->IsPending(&pending);
    if (pending && !schemeIsJavaScript) {
      nsCOMPtr<nsIWebProgress> webProgress = do_GetInterface(docShell);
      NS_ASSERTION(webProgress, "nsIDocShell not converted to nsIWebProgress!");
      rv = webProgress->AddProgressListener(this, nsIWebProgress::NOTIFY_STATE_ALL);
      NS_ENSURE_SUBMIT_SUCCESS(rv);
      mWebProgress = do_GetWeakReference(webProgress);
      NS_ASSERTION(mWebProgress, "can't hold weak ref to webprogress!");
    } else {
      ForgetCurrentSubmission();
    }
  } else {
    ForgetCurrentSubmission();
  }

  return rv;
}

nsresult
nsHTMLFormElement::NotifySubmitObservers(nsIURI* aActionURL,
                                         bool* aCancelSubmit,
                                         bool    aEarlyNotify)
{
  // If this is the first form, bring alive the first form submit
  // category observers
  if (!gFirstFormSubmitted) {
    gFirstFormSubmitted = true;
    NS_CreateServicesFromCategory(NS_FIRST_FORMSUBMIT_CATEGORY,
                                  nsnull,
                                  NS_FIRST_FORMSUBMIT_CATEGORY);
  }

  // Notify observers that the form is being submitted.
  nsCOMPtr<nsIObserverService> service =
    mozilla::services::GetObserverService();
  if (!service)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsISimpleEnumerator> theEnum;
  nsresult rv = service->EnumerateObservers(aEarlyNotify ?
                                            NS_EARLYFORMSUBMIT_SUBJECT :
                                            NS_FORMSUBMIT_SUBJECT,
                                            getter_AddRefs(theEnum));
  NS_ENSURE_SUCCESS(rv, rv);

  if (theEnum) {
    nsCOMPtr<nsISupports> inst;
    *aCancelSubmit = false;

    // XXXbz what do the submit observers actually want?  The window
    // of the document this is shown in?  Or something else?
    // sXBL/XBL2 issue
    nsCOMPtr<nsPIDOMWindow> window = OwnerDoc()->GetWindow();

    bool loop = true;
    while (NS_SUCCEEDED(theEnum->HasMoreElements(&loop)) && loop) {
      theEnum->GetNext(getter_AddRefs(inst));

      nsCOMPtr<nsIFormSubmitObserver> formSubmitObserver(
                      do_QueryInterface(inst));
      if (formSubmitObserver) {
        rv = formSubmitObserver->Notify(this,
                                        window,
                                        aActionURL,
                                        aCancelSubmit);
        NS_ENSURE_SUCCESS(rv, rv);
      }
      if (*aCancelSubmit) {
        return NS_OK;
      }
    }
  }

  return rv;
}


nsresult
nsHTMLFormElement::WalkFormElements(nsFormSubmission* aFormSubmission)
{
  nsTArray<nsGenericHTMLFormElement*> sortedControls;
  nsresult rv = mControls->GetSortedControls(sortedControls);
  NS_ENSURE_SUCCESS(rv, rv);

  //
  // Walk the list of nodes and call SubmitNamesValues() on the controls
  //
  PRUint32 len = sortedControls.Length();
  for (PRUint32 i = 0; i < len; ++i) {
    // Tell the control to submit its name/value pairs to the submission
    sortedControls[i]->SubmitNamesValues(aFormSubmission);
  }

  return NS_OK;
}

// nsIForm

NS_IMETHODIMP_(PRUint32)
nsHTMLFormElement::GetElementCount() const 
{
  PRUint32 count = nsnull;
  mControls->GetLength(&count); 
  return count;
}

NS_IMETHODIMP_(nsIFormControl*)
nsHTMLFormElement::GetElementAt(PRInt32 aIndex) const
{
  return mControls->mElements.SafeElementAt(aIndex, nsnull);
}

/**
 * Compares the position of aControl1 and aControl2 in the document
 * @param aControl1 First control to compare.
 * @param aControl2 Second control to compare.
 * @param aForm Parent form of the controls.
 * @return < 0 if aControl1 is before aControl2,
 *         > 0 if aControl1 is after aControl2,
 *         0 otherwise
 */
static inline PRInt32
CompareFormControlPosition(nsGenericHTMLFormElement *aControl1,
                           nsGenericHTMLFormElement *aControl2,
                           const nsIContent* aForm)
{
  NS_ASSERTION(aControl1 != aControl2, "Comparing a form control to itself");

  // If an element has a @form, we can assume it *might* be able to not have
  // a parent and still be in the form.
  NS_ASSERTION((aControl1->HasAttr(kNameSpaceID_None, nsGkAtoms::form) ||
                aControl1->GetParent()) &&
               (aControl2->HasAttr(kNameSpaceID_None, nsGkAtoms::form) ||
                aControl2->GetParent()),
               "Form controls should always have parents");

  // If we pass aForm, we are assuming both controls are form descendants which
  // is not always the case. This function should work but maybe slower.
  // However, checking if both elements are form descendants may be slow too...
  // TODO: remove the prevent asserts fix, see bug 598468.
#ifdef DEBUG
  nsLayoutUtils::gPreventAssertInCompareTreePosition = true;
  PRInt32 rVal = nsLayoutUtils::CompareTreePosition(aControl1, aControl2, aForm);
  nsLayoutUtils::gPreventAssertInCompareTreePosition = false;

  return rVal;
#else // DEBUG
  return nsLayoutUtils::CompareTreePosition(aControl1, aControl2, aForm);
#endif // DEBUG
}
 
#ifdef DEBUG
/**
 * Checks that all form elements are in document order. Asserts if any pair of
 * consecutive elements are not in increasing document order.
 *
 * @param aControls List of form controls to check.
 * @param aForm Parent form of the controls.
 */
static void
AssertDocumentOrder(const nsTArray<nsGenericHTMLFormElement*>& aControls,
                    nsIContent* aForm)
{
  // TODO: remove the return statement with bug 598468.
  // This is done to prevent asserts in some edge cases.
  return;

  // Only iterate if aControls is not empty, since otherwise
  // |aControls.Length() - 1| will be a very large unsigned number... not what
  // we want here.
  if (!aControls.IsEmpty()) {
    for (PRUint32 i = 0; i < aControls.Length() - 1; ++i) {
      NS_ASSERTION(CompareFormControlPosition(aControls[i], aControls[i + 1],
                                              aForm) < 0,
                   "Form controls not ordered correctly");
    }
  }
}
#endif

nsresult
nsHTMLFormElement::AddElement(nsGenericHTMLFormElement* aChild,
                              bool aUpdateValidity, bool aNotify)
{
  // If an element has a @form, we can assume it *might* be able to not have
  // a parent and still be in the form.
  NS_ASSERTION(aChild->HasAttr(kNameSpaceID_None, nsGkAtoms::form) ||
               aChild->GetParent(),
               "Form control should have a parent");

  // Determine whether to add the new element to the elements or
  // the not-in-elements list.
  bool childInElements = ShouldBeInElements(aChild);
  nsTArray<nsGenericHTMLFormElement*>& controlList = childInElements ?
      mControls->mElements : mControls->mNotInElements;
  
  NS_ASSERTION(controlList.IndexOf(aChild) == controlList.NoIndex,
               "Form control already in form");

  PRUint32 count = controlList.Length();
  nsGenericHTMLFormElement* element;
  
  // Optimize most common case where we insert at the end.
  bool lastElement = false;
  PRInt32 position = -1;
  if (count > 0) {
    element = controlList[count - 1];
    position = CompareFormControlPosition(aChild, element, this);
  }

  // If this item comes after the last element, or the elements array is
  // empty, we append to the end. Otherwise, we do a binary search to
  // determine where the element should go.
  if (position >= 0 || count == 0) {
    // WEAK - don't addref
    controlList.AppendElement(aChild);
    lastElement = true;
  }
  else {
    PRInt32 low = 0, mid, high;
    high = count - 1;
      
    while (low <= high) {
      mid = (low + high) / 2;
        
      element = controlList[mid];
      position = CompareFormControlPosition(aChild, element, this);
      if (position >= 0)
        low = mid + 1;
      else
        high = mid - 1;
    }
      
    // WEAK - don't addref
    controlList.InsertElementAt(low, aChild);
  }

#ifdef DEBUG
  AssertDocumentOrder(controlList, this);
#endif

  PRInt32 type = aChild->GetType();

  //
  // If it is a password control, and the password manager has not yet been
  // initialized, initialize the password manager
  //
  if (!gPasswordManagerInitialized && type == NS_FORM_INPUT_PASSWORD) {
    // Initialize the password manager category
    gPasswordManagerInitialized = true;
    NS_CreateServicesFromCategory(NS_PASSWORDMANAGER_CATEGORY,
                                  nsnull,
                                  NS_PASSWORDMANAGER_CATEGORY);
  }
 
  // Default submit element handling
  if (aChild->IsSubmitControl()) {
    // Update mDefaultSubmitElement, mFirstSubmitInElements,
    // mFirstSubmitNotInElements.

    nsGenericHTMLFormElement** firstSubmitSlot =
      childInElements ? &mFirstSubmitInElements : &mFirstSubmitNotInElements;
    
    // The new child is the new first submit in its list if the firstSubmitSlot
    // is currently empty or if the child is before what's currently in the
    // slot.  Note that if we already have a control in firstSubmitSlot and
    // we're appending this element can't possibly replace what's currently in
    // the slot.  Also note that aChild can't become the mDefaultSubmitElement
    // unless it replaces what's in the slot.  If it _does_ replace what's in
    // the slot, it becomes the default submit if either the default submit is
    // what's in the slot or the child is earlier than the default submit.
    nsGenericHTMLFormElement* oldDefaultSubmit = mDefaultSubmitElement;
    if (!*firstSubmitSlot ||
        (!lastElement &&
         CompareFormControlPosition(aChild, *firstSubmitSlot, this) < 0)) {
      // Update mDefaultSubmitElement if it's currently in a valid state.
      // Valid state means either non-null or null because there are in fact
      // no submit elements around.
      if ((mDefaultSubmitElement ||
           (!mFirstSubmitInElements && !mFirstSubmitNotInElements)) &&
          (*firstSubmitSlot == mDefaultSubmitElement ||
           CompareFormControlPosition(aChild,
                                      mDefaultSubmitElement, this) < 0)) {
        mDefaultSubmitElement = aChild;
      }
      *firstSubmitSlot = aChild;
    }
    NS_POSTCONDITION(mDefaultSubmitElement == mFirstSubmitInElements ||
                     mDefaultSubmitElement == mFirstSubmitNotInElements ||
                     !mDefaultSubmitElement,
                     "What happened here?");

    // Notify that the state of the previous default submit element has changed
    // if the element which is the default submit element has changed.  The new
    // default submit element is responsible for its own state update.
    if (oldDefaultSubmit && oldDefaultSubmit != mDefaultSubmitElement) {
      oldDefaultSubmit->UpdateState(aNotify);
    }
  }

  // If the element is subject to constraint validaton and is invalid, we need
  // to update our internal counter.
  if (aUpdateValidity) {
    nsCOMPtr<nsIConstraintValidation> cvElmt = do_QueryObject(aChild);
    if (cvElmt &&
        cvElmt->IsCandidateForConstraintValidation() && !cvElmt->IsValid()) {
      UpdateValidity(false);
    }
  }

  // Notify the radio button it's been added to a group
  // This has to be done _after_ UpdateValidity() call to prevent the element
  // being count twice.
  if (type == NS_FORM_INPUT_RADIO) {
    nsRefPtr<nsHTMLInputElement> radio =
      static_cast<nsHTMLInputElement*>(aChild);
    radio->AddedToRadioGroup();
  }

  return NS_OK;
}

nsresult
nsHTMLFormElement::AddElementToTable(nsGenericHTMLFormElement* aChild,
                                     const nsAString& aName)
{
  return mControls->AddElementToTable(aChild, aName);  
}


nsresult
nsHTMLFormElement::RemoveElement(nsGenericHTMLFormElement* aChild,
                                 bool aUpdateValidity)
{
  //
  // Remove it from the radio group if it's a radio button
  //
  nsresult rv = NS_OK;
  if (aChild->GetType() == NS_FORM_INPUT_RADIO) {
    nsRefPtr<nsHTMLInputElement> radio =
      static_cast<nsHTMLInputElement*>(aChild);
    radio->WillRemoveFromRadioGroup();
  }

  // Determine whether to remove the child from the elements list
  // or the not in elements list.
  bool childInElements = ShouldBeInElements(aChild);
  nsTArray<nsGenericHTMLFormElement*>& controls = childInElements ?
      mControls->mElements :  mControls->mNotInElements;
  
  // Find the index of the child. This will be used later if necessary
  // to find the default submit.
  PRUint32 index = controls.IndexOf(aChild);
  NS_ENSURE_STATE(index != controls.NoIndex);

  controls.RemoveElementAt(index);

  // Update our mFirstSubmit* values.
  nsGenericHTMLFormElement** firstSubmitSlot =
    childInElements ? &mFirstSubmitInElements : &mFirstSubmitNotInElements;
  if (aChild == *firstSubmitSlot) {
    *firstSubmitSlot = nsnull;

    // We are removing the first submit in this list, find the new first submit
    PRUint32 length = controls.Length();
    for (PRUint32 i = index; i < length; ++i) {
      nsGenericHTMLFormElement* currentControl = controls[i];
      if (currentControl->IsSubmitControl()) {
        *firstSubmitSlot = currentControl;
        break;
      }
    }
  }

  if (aChild == mDefaultSubmitElement) {
    // Need to reset mDefaultSubmitElement.  Do this asynchronously so
    // that we're not doing it while the DOM is in flux.
    mDefaultSubmitElement = nsnull;
    nsContentUtils::AddScriptRunner(new RemoveElementRunnable(this));

    // Note that we don't need to notify on the old default submit (which is
    // being removed) because it's either being removed from the DOM or
    // changing attributes in a way that makes it responsible for sending its
    // own notifications.
  }

  // If the element was subject to constraint validaton and is invalid, we need
  // to update our internal counter.
  if (aUpdateValidity) {
    nsCOMPtr<nsIConstraintValidation> cvElmt = do_QueryObject(aChild);
    if (cvElmt &&
        cvElmt->IsCandidateForConstraintValidation() && !cvElmt->IsValid()) {
      UpdateValidity(true);
    }
  }

  return rv;
}

void
nsHTMLFormElement::HandleDefaultSubmitRemoval()
{
  if (mDefaultSubmitElement) {
    // Already got reset somehow; nothing else to do here
    return;
  }

  if (!mFirstSubmitNotInElements) {
    mDefaultSubmitElement = mFirstSubmitInElements;
  } else if (!mFirstSubmitInElements) {
    mDefaultSubmitElement = mFirstSubmitNotInElements;
  } else {
    NS_ASSERTION(mFirstSubmitInElements != mFirstSubmitNotInElements,
                 "How did that happen?");
    // Have both; use the earlier one
    mDefaultSubmitElement =
      CompareFormControlPosition(mFirstSubmitInElements,
                                 mFirstSubmitNotInElements, this) < 0 ?
      mFirstSubmitInElements : mFirstSubmitNotInElements;
  }

  NS_POSTCONDITION(mDefaultSubmitElement == mFirstSubmitInElements ||
                   mDefaultSubmitElement == mFirstSubmitNotInElements,
                   "What happened here?");

  // Notify about change if needed.
  if (mDefaultSubmitElement) {
    mDefaultSubmitElement->UpdateState(true);
  }
}

nsresult
nsHTMLFormElement::RemoveElementFromTable(nsGenericHTMLFormElement* aElement,
                                          const nsAString& aName)
{
  return mControls->RemoveElementFromTable(aElement, aName);
}

NS_IMETHODIMP_(already_AddRefed<nsISupports>)
nsHTMLFormElement::ResolveName(const nsAString& aName)
{
  return DoResolveName(aName, true);
}

already_AddRefed<nsISupports>
nsHTMLFormElement::DoResolveName(const nsAString& aName,
                                 bool aFlushContent)
{
  nsISupports *result;
  NS_IF_ADDREF(result = mControls->NamedItemInternal(aName, aFlushContent));
  return result;
}

void
nsHTMLFormElement::OnSubmitClickBegin(nsIContent* aOriginatingElement)
{
  mDeferSubmission = true;

  // Prepare to run NotifySubmitObservers early before the
  // scripts on the page get to modify the form data, possibly
  // throwing off any password manager. (bug 257781)
  nsCOMPtr<nsIURI> actionURI;
  nsresult rv;

  rv = GetActionURL(getter_AddRefs(actionURI), aOriginatingElement);
  if (NS_FAILED(rv) || !actionURI)
    return;

  // Notify observers of submit if the form is valid.
  // TODO: checking for mInvalidElementsCount is a temporary fix that should be
  // removed with bug 610402.
  if (mInvalidElementsCount == 0) {
    bool cancelSubmit = false;
    rv = NotifySubmitObservers(actionURI, &cancelSubmit, true);
    if (NS_SUCCEEDED(rv)) {
      mNotifiedObservers = true;
      mNotifiedObserversResult = cancelSubmit;
    }
  }
}

void
nsHTMLFormElement::OnSubmitClickEnd()
{
  mDeferSubmission = false;
}

void
nsHTMLFormElement::FlushPendingSubmission()
{
  if (mPendingSubmission) {
    // Transfer owning reference so that the submissioin doesn't get deleted
    // if we reenter
    nsAutoPtr<nsFormSubmission> submission = mPendingSubmission;

    SubmitSubmission(submission);
  }
}

nsresult
nsHTMLFormElement::GetActionURL(nsIURI** aActionURL,
                                nsIContent* aOriginatingElement)
{
  nsresult rv = NS_OK;

  *aActionURL = nsnull;

  //
  // Grab the URL string
  //
  // If the originating element is a submit control and has the formaction
  // attribute specified, it should be used. Otherwise, the action attribute
  // from the form element should be used.
  //
  nsAutoString action;

  if (aOriginatingElement &&
      aOriginatingElement->HasAttr(kNameSpaceID_None, nsGkAtoms::formaction)) {
#ifdef DEBUG
    nsCOMPtr<nsIFormControl> formControl = do_QueryInterface(aOriginatingElement);
    NS_ASSERTION(formControl && formControl->IsSubmitControl(),
                 "The originating element must be a submit form control!");
#endif // DEBUG

    nsCOMPtr<nsIDOMHTMLInputElement> inputElement = do_QueryInterface(aOriginatingElement);
    if (inputElement) {
      inputElement->GetFormAction(action);
    } else {
      nsCOMPtr<nsIDOMHTMLButtonElement> buttonElement = do_QueryInterface(aOriginatingElement);
      if (buttonElement) {
        buttonElement->GetFormAction(action);
      } else {
        NS_ERROR("Originating element must be an input or button element!");
        return NS_ERROR_UNEXPECTED;
      }
    }
  } else {
    GetAction(action);
  }

  //
  // Form the full action URL
  //

  // Get the document to form the URL.
  // We'll also need it later to get the DOM window when notifying form submit
  // observers (bug 33203)
  if (!IsInDoc()) {
    return NS_OK; // No doc means don't submit, see Bug 28988
  }

  // Get base URL
  nsIDocument *document = OwnerDoc();
  nsIURI *docURI = document->GetDocumentURI();
  NS_ENSURE_TRUE(docURI, NS_ERROR_UNEXPECTED);

  // If an action is not specified and we are inside
  // a HTML document then reload the URL. This makes us
  // compatible with 4.x browsers.
  // If we are in some other type of document such as XML or
  // XUL, do nothing. This prevents undesirable reloading of
  // a document inside XUL.

  nsCOMPtr<nsIURI> actionURL;
  if (action.IsEmpty()) {
    nsCOMPtr<nsIHTMLDocument> htmlDoc(do_QueryInterface(document));
    if (!htmlDoc) {
      // Must be a XML, XUL or other non-HTML document type
      // so do nothing.
      return NS_OK;
    }

    rv = docURI->Clone(getter_AddRefs(actionURL));
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    nsCOMPtr<nsIURI> baseURL = GetBaseURI();
    NS_ASSERTION(baseURL, "No Base URL found in Form Submit!\n");
    if (!baseURL) {
      return NS_OK; // No base URL -> exit early, see Bug 30721
    }
    rv = NS_NewURI(getter_AddRefs(actionURL), action, nsnull, baseURL);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  //
  // Verify the URL should be reached
  //
  // Get security manager, check to see if access to action URI is allowed.
  //
  nsIScriptSecurityManager *securityManager =
      nsContentUtils::GetSecurityManager();
  rv = securityManager->
    CheckLoadURIWithPrincipal(NodePrincipal(), actionURL,
                              nsIScriptSecurityManager::STANDARD);
  NS_ENSURE_SUCCESS(rv, rv);

  //
  // Assign to the output
  //
  *aActionURL = actionURL;
  NS_ADDREF(*aActionURL);

  return rv;
}

NS_IMETHODIMP_(nsIFormControl*)
nsHTMLFormElement::GetDefaultSubmitElement() const
{
  NS_PRECONDITION(mDefaultSubmitElement == mFirstSubmitInElements ||
                  mDefaultSubmitElement == mFirstSubmitNotInElements,
                  "What happened here?");
  
  return mDefaultSubmitElement;
}

bool
nsHTMLFormElement::IsDefaultSubmitElement(const nsIFormControl* aControl) const
{
  NS_PRECONDITION(aControl, "Unexpected call");

  if (aControl == mDefaultSubmitElement) {
    // Yes, it is
    return true;
  }

  if (mDefaultSubmitElement ||
      (aControl != mFirstSubmitInElements &&
       aControl != mFirstSubmitNotInElements)) {
    // It isn't
    return false;
  }

  // mDefaultSubmitElement is null, but we have a non-null submit around
  // (aControl, in fact).  figure out whether it's in fact the default submit
  // and just hasn't been set that way yet.  Note that we can't just call
  // HandleDefaultSubmitRemoval because we might need to notify to handle that
  // correctly and we don't know whether that's safe right here.
  if (!mFirstSubmitInElements || !mFirstSubmitNotInElements) {
    // We only have one first submit; aControl has to be it
    return true;
  }

  // We have both kinds of submits.  Check which comes first.
  nsIFormControl* defaultSubmit =
    CompareFormControlPosition(mFirstSubmitInElements,
                               mFirstSubmitNotInElements, this) < 0 ?
      mFirstSubmitInElements : mFirstSubmitNotInElements;
  return aControl == defaultSubmit;
}

bool
nsHTMLFormElement::HasSingleTextControl() const
{
  // Input text controls are always in the elements list.
  PRUint32 numTextControlsFound = 0;
  PRUint32 length = mControls->mElements.Length();
  for (PRUint32 i = 0; i < length && numTextControlsFound < 2; ++i) {
    if (mControls->mElements[i]->IsSingleLineTextControl(false)) {
      numTextControlsFound++;
    }
  }
  return numTextControlsFound == 1;
}

NS_IMETHODIMP
nsHTMLFormElement::GetEncoding(nsAString& aEncoding)
{
  return GetEnctype(aEncoding);
}
 
NS_IMETHODIMP
nsHTMLFormElement::SetEncoding(const nsAString& aEncoding)
{
  return SetEnctype(aEncoding);
}

NS_IMETHODIMP    
nsHTMLFormElement::GetLength(PRInt32* aLength)
{
  PRUint32 length;
  nsresult rv = mControls->GetLength(&length);
  *aLength = length;
  return rv;
}

void
nsHTMLFormElement::ForgetCurrentSubmission()
{
  mNotifiedObservers = false;
  mIsSubmitting = false;
  mSubmittingRequest = nsnull;
  nsCOMPtr<nsIWebProgress> webProgress = do_QueryReferent(mWebProgress);
  if (webProgress) {
    webProgress->RemoveProgressListener(this);
  }
  mWebProgress = nsnull;
}

bool
nsHTMLFormElement::CheckFormValidity(nsIMutableArray* aInvalidElements) const
{
  bool ret = true;

  nsTArray<nsGenericHTMLFormElement*> sortedControls;
  if (NS_FAILED(mControls->GetSortedControls(sortedControls))) {
    return false;
  }

  PRUint32 len = sortedControls.Length();

  // Hold a reference to the elements so they can't be deleted while calling
  // the invalid events.
  for (PRUint32 i = 0; i < len; ++i) {
    static_cast<nsGenericHTMLElement*>(sortedControls[i])->AddRef();
  }

  for (PRUint32 i = 0; i < len; ++i) {
    nsCOMPtr<nsIConstraintValidation> cvElmt =
      do_QueryInterface((nsGenericHTMLElement*)sortedControls[i]);
    if (cvElmt && cvElmt->IsCandidateForConstraintValidation() &&
        !cvElmt->IsValid()) {
      ret = false;
      bool defaultAction = true;
      nsContentUtils::DispatchTrustedEvent(sortedControls[i]->OwnerDoc(),
                                           static_cast<nsIContent*>(sortedControls[i]),
                                           NS_LITERAL_STRING("invalid"),
                                           false, true, &defaultAction);

      // Add all unhandled invalid controls to aInvalidElements if the caller
      // requested them.
      if (defaultAction && aInvalidElements) {
        aInvalidElements->AppendElement((nsGenericHTMLElement*)sortedControls[i],
                                        false);
      }
    }
  }

  // Release the references.
  for (PRUint32 i = 0; i < len; ++i) {
    static_cast<nsGenericHTMLElement*>(sortedControls[i])->Release();
  }

  return ret;
}

bool
nsHTMLFormElement::CheckValidFormSubmission()
{
  /**
   * Check for form validity: do not submit a form if there are unhandled
   * invalid controls in the form.
   * This should not be done if the form has been submitted with .submit().
   *
   * NOTE: for the moment, we are also checking that there is an observer for
   * NS_INVALIDFORMSUBMIT_SUBJECT so it will prevent blocking form submission
   * if the browser does not have implemented a UI yet.
   *
   * TODO: the check for observer should be removed later when HTML5 Forms will
   * be spread enough and authors will assume forms can't be submitted when
   * invalid. See bug 587671.
   */

  NS_ASSERTION(!HasAttr(kNameSpaceID_None, nsGkAtoms::novalidate),
               "We shouldn't be there if novalidate is set!");

  // When .submit() is called aEvent = nsnull so we can rely on that to know if
  // we have to check the validity of the form.
  nsCOMPtr<nsIObserverService> service =
    mozilla::services::GetObserverService();
  if (!service) {
    NS_WARNING("No observer service available!");
    return true;
  }

  nsCOMPtr<nsISimpleEnumerator> theEnum;
  nsresult rv = service->EnumerateObservers(NS_INVALIDFORMSUBMIT_SUBJECT,
                                            getter_AddRefs(theEnum));
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasObserver = false;
  rv = theEnum->HasMoreElements(&hasObserver);

  // Do not check form validity if there is no observer for
  // NS_INVALIDFORMSUBMIT_SUBJECT.
  if (NS_SUCCEEDED(rv) && hasObserver) {
    nsCOMPtr<nsIMutableArray> invalidElements =
      do_CreateInstance(NS_ARRAY_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    if (!CheckFormValidity(invalidElements.get())) {
      // For the first invalid submission, we should update element states.
      // We have to do that _before_ calling the observers so we are sure they
      // will not interfere (like focusing the element).
      if (!mEverTriedInvalidSubmit) {
        mEverTriedInvalidSubmit = true;

        /*
         * We are going to call update states assuming elements want to
         * be notified because we can't know.
         * Submissions shouldn't happen during parsing so it _should_ be safe.
         */

        nsAutoScriptBlocker scriptBlocker;

        for (PRUint32 i = 0, length = mControls->mElements.Length();
             i < length; ++i) {
          // Input elements can trigger a form submission and we want to
          // update the style in that case.
          if (mControls->mElements[i]->IsHTML(nsGkAtoms::input) &&
              nsContentUtils::IsFocusedContent(mControls->mElements[i])) {
            static_cast<nsHTMLInputElement*>(mControls->mElements[i])
              ->UpdateValidityUIBits(true);
          }

          mControls->mElements[i]->UpdateState(true);
        }

        // Because of backward compatibility, <input type='image'> is not in
        // elements but can be invalid.
        // TODO: should probably be removed when bug 606491 will be fixed.
        for (PRUint32 i = 0, length = mControls->mNotInElements.Length();
             i < length; ++i) {
          mControls->mNotInElements[i]->UpdateState(true);
        }
      }

      nsCOMPtr<nsISupports> inst;
      nsCOMPtr<nsIFormSubmitObserver> observer;
      bool more = true;
      while (NS_SUCCEEDED(theEnum->HasMoreElements(&more)) && more) {
        theEnum->GetNext(getter_AddRefs(inst));
        observer = do_QueryInterface(inst);

        if (observer) {
          observer->NotifyInvalidSubmit(this,
                                        static_cast<nsIArray*>(invalidElements));
        }
      }

      // The form is invalid. Observers have been alerted. Do not submit.
      return false;
    }
  } else {
    NS_WARNING("There is no observer for \"invalidformsubmit\". \
One should be implemented!");
  }

  return true;
}

void
nsHTMLFormElement::UpdateValidity(bool aElementValidity)
{
  if (aElementValidity) {
    --mInvalidElementsCount;
  } else {
    ++mInvalidElementsCount;
  }

  NS_ASSERTION(mInvalidElementsCount >= 0, "Something went seriously wrong!");

  // The form validity has just changed if:
  // - there are no more invalid elements ;
  // - or there is one invalid elmement and an element just became invalid.
  // If we have invalid elements and we used to before as well, do nothing.
  if (mInvalidElementsCount &&
      (mInvalidElementsCount != 1 || aElementValidity)) {
    return;
  }

  /*
   * We are going to update states assuming submit controls want to
   * be notified because we can't know.
   * UpdateValidity shouldn't be called so much during parsing so it _should_
   * be safe.
   */

  nsAutoScriptBlocker scriptBlocker;

  // Inform submit controls that the form validity has changed.
  for (PRUint32 i = 0, length = mControls->mElements.Length();
       i < length; ++i) {
    if (mControls->mElements[i]->IsSubmitControl()) {
      mControls->mElements[i]->UpdateState(true);
    }
  }

  // Because of backward compatibility, <input type='image'> is not in elements
  // so we have to check for controls not in elements too.
  PRUint32 length = mControls->mNotInElements.Length();
  for (PRUint32 i = 0; i < length; ++i) {
    if (mControls->mNotInElements[i]->IsSubmitControl()) {
      mControls->mNotInElements[i]->UpdateState(true);
    }
  }

  UpdateState(true);
}

// nsIWebProgressListener
NS_IMETHODIMP
nsHTMLFormElement::OnStateChange(nsIWebProgress* aWebProgress,
                                 nsIRequest* aRequest,
                                 PRUint32 aStateFlags,
                                 PRUint32 aStatus)
{
  // If STATE_STOP is never fired for any reason (redirect?  Failed state
  // change?) the form element will leak.  It will be kept around by the
  // nsIWebProgressListener (assuming it keeps a strong pointer).  We will
  // consequently leak the request.
  if (aRequest == mSubmittingRequest &&
      aStateFlags & nsIWebProgressListener::STATE_STOP) {
    ForgetCurrentSubmission();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLFormElement::OnProgressChange(nsIWebProgress* aWebProgress,
                                    nsIRequest* aRequest,
                                    PRInt32 aCurSelfProgress,
                                    PRInt32 aMaxSelfProgress,
                                    PRInt32 aCurTotalProgress,
                                    PRInt32 aMaxTotalProgress)
{
  NS_NOTREACHED("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLFormElement::OnLocationChange(nsIWebProgress* aWebProgress,
                                    nsIRequest* aRequest,
                                    nsIURI* location,
                                    PRUint32 aFlags)
{
  NS_NOTREACHED("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLFormElement::OnStatusChange(nsIWebProgress* aWebProgress,
                                  nsIRequest* aRequest,
                                  nsresult aStatus,
                                  const PRUnichar* aMessage)
{
  NS_NOTREACHED("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLFormElement::OnSecurityChange(nsIWebProgress* aWebProgress,
                                    nsIRequest* aRequest,
                                    PRUint32 state)
{
  NS_NOTREACHED("notification excluded in AddProgressListener(...)");
  return NS_OK;
}
 
NS_IMETHODIMP_(PRInt32)
nsHTMLFormElement::IndexOfControl(nsIFormControl* aControl)
{
  PRInt32 index = nsnull;
  return mControls->IndexOfControl(aControl, &index) == NS_OK ? index : nsnull;
}

NS_IMETHODIMP
nsHTMLFormElement::SetCurrentRadioButton(const nsAString& aName,
                                         nsIDOMHTMLInputElement* aRadio)
{
  mSelectedRadioButtons.Put(aName, aRadio);

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLFormElement::GetCurrentRadioButton(const nsAString& aName,
                                         nsIDOMHTMLInputElement** aRadio)
{
  mSelectedRadioButtons.Get(aName, aRadio);

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLFormElement::GetNextRadioButton(const nsAString& aName,
                                      const bool aPrevious,
                                      nsIDOMHTMLInputElement*  aFocusedRadio,
                                      nsIDOMHTMLInputElement** aRadioOut)
{
  // Return the radio button relative to the focused radio button.
  // If no radio is focused, get the radio relative to the selected one.
  *aRadioOut = nsnull;

  nsCOMPtr<nsIDOMHTMLInputElement> currentRadio;
  if (aFocusedRadio) {
    currentRadio = aFocusedRadio;
  }
  else {
    mSelectedRadioButtons.Get(aName, getter_AddRefs(currentRadio));
  }

  nsCOMPtr<nsISupports> itemWithName = ResolveName(aName);
  nsCOMPtr<nsINodeList> radioGroup(do_QueryInterface(itemWithName));

  if (!radioGroup) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIContent> currentRadioNode(do_QueryInterface(currentRadio));
  NS_ASSERTION(currentRadioNode, "No nsIContent for current radio button");
  PRInt32 index = radioGroup->IndexOf(currentRadioNode);
  if (index < 0) {
    return NS_ERROR_FAILURE;
  }

  PRUint32 numRadios;
  radioGroup->GetLength(&numRadios);
  bool disabled = true;
  nsCOMPtr<nsIDOMHTMLInputElement> radio;
  nsCOMPtr<nsIFormControl> formControl;

  do {
    if (aPrevious) {
      if (--index < 0) {
        index = numRadios -1;
      }
    }
    else if (++index >= (PRInt32)numRadios) {
      index = 0;
    }
    radio = do_QueryInterface(radioGroup->GetNodeAt(index));
    if (!radio)
      continue;

    // XXXbz why is this formControl check needed, exactly?
    formControl = do_QueryInterface(radio);
    if (!formControl || formControl->GetType() != NS_FORM_INPUT_RADIO)
      continue;

    radio->GetDisabled(&disabled);
  } while (disabled && radio != currentRadio);

  NS_IF_ADDREF(*aRadioOut = radio);
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLFormElement::WalkRadioGroup(const nsAString& aName,
                                  nsIRadioVisitor* aVisitor,
                                  bool aFlushContent)
{
  if (aName.IsEmpty()) {
    //
    // XXX If the name is empty, it's not stored in the control list.  There
    // *must* be a more efficient way to do this.
    //
    nsCOMPtr<nsIFormControl> control;
    PRUint32 len = GetElementCount();
    for (PRUint32 i = 0; i < len; i++) {
      control = GetElementAt(i);
      if (control->GetType() == NS_FORM_INPUT_RADIO) {
        nsCOMPtr<nsIContent> controlContent = do_QueryInterface(control);
        if (controlContent &&
            controlContent->AttrValueIs(kNameSpaceID_None, nsGkAtoms::name,
                                        EmptyString(), eCaseMatters) &&
            !aVisitor->Visit(control)) {
          break;
        }
      }
    }
    return NS_OK;
  }

  // Get the control / list of controls from the form using form["name"]
  nsCOMPtr<nsISupports> item = DoResolveName(aName, aFlushContent);
  if (!item) {
    return NS_ERROR_FAILURE;
  }

  // If it's just a lone radio button, then select it.
  nsCOMPtr<nsIFormControl> formControl = do_QueryInterface(item);
  if (formControl) {
    if (formControl->GetType() == NS_FORM_INPUT_RADIO) {
      aVisitor->Visit(formControl);
    }
    return NS_OK;
  }

  nsCOMPtr<nsIDOMNodeList> nodeList = do_QueryInterface(item);
  if (!nodeList) {
    return NS_OK;
  }
  PRUint32 length = 0;
  nodeList->GetLength(&length);
  for (PRUint32 i = 0; i < length; i++) {
    nsCOMPtr<nsIDOMNode> node;
    nodeList->Item(i, getter_AddRefs(node));
    nsCOMPtr<nsIFormControl> formControl = do_QueryInterface(node);
    if (formControl && formControl->GetType() == NS_FORM_INPUT_RADIO &&
        !aVisitor->Visit(formControl)) {
      break;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLFormElement::AddToRadioGroup(const nsAString& aName,
                                   nsIFormControl* aRadio)
{
  nsCOMPtr<nsIContent> element = do_QueryInterface(aRadio);
  NS_ASSERTION(element, "radio controls have to be content elements!");

  if (element->HasAttr(kNameSpaceID_None, nsGkAtoms::required)) {
    mRequiredRadioButtonCounts.Put(aName,
                                   mRequiredRadioButtonCounts.Get(aName)+1);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHTMLFormElement::RemoveFromRadioGroup(const nsAString& aName,
                                        nsIFormControl* aRadio)
{
  nsCOMPtr<nsIContent> element = do_QueryInterface(aRadio);
  NS_ASSERTION(element, "radio controls have to be content elements!");

  if (element->HasAttr(kNameSpaceID_None, nsGkAtoms::required)) {
    PRUint32 requiredNb = mRequiredRadioButtonCounts.Get(aName);
    NS_ASSERTION(requiredNb >= 1,
                 "At least one radio button has to be required!");

    if (requiredNb == 1) {
      mRequiredRadioButtonCounts.Remove(aName);
    } else {
      mRequiredRadioButtonCounts.Put(aName, requiredNb-1);
    }
  }

  return NS_OK;
}

PRUint32
nsHTMLFormElement::GetRequiredRadioCount(const nsAString& aName) const
{
  return mRequiredRadioButtonCounts.Get(aName);
}

void
nsHTMLFormElement::RadioRequiredChanged(const nsAString& aName,
                                        nsIFormControl* aRadio)
{
  nsCOMPtr<nsIContent> element = do_QueryInterface(aRadio);
  NS_ASSERTION(element, "radio controls have to be content elements!");

  if (element->HasAttr(kNameSpaceID_None, nsGkAtoms::required)) {
    mRequiredRadioButtonCounts.Put(aName,
                                   mRequiredRadioButtonCounts.Get(aName)+1);
  } else {
    PRUint32 requiredNb = mRequiredRadioButtonCounts.Get(aName);
    NS_ASSERTION(requiredNb >= 1,
                 "At least one radio button has to be required!");
    if (requiredNb == 1) {
      mRequiredRadioButtonCounts.Remove(aName);
    } else {
      mRequiredRadioButtonCounts.Put(aName, requiredNb-1);
    }
  }
}

bool
nsHTMLFormElement::GetValueMissingState(const nsAString& aName) const
{
  return mValueMissingRadioGroups.Get(aName);
}

void
nsHTMLFormElement::SetValueMissingState(const nsAString& aName, bool aValue)
{
  mValueMissingRadioGroups.Put(aName, aValue);
}

nsEventStates
nsHTMLFormElement::IntrinsicState() const
{
  nsEventStates state = nsGenericHTMLElement::IntrinsicState();

  if (mInvalidElementsCount) {
    state |= NS_EVENT_STATE_INVALID;
  } else {
      state |= NS_EVENT_STATE_VALID;
  }

  return state;
}

//----------------------------------------------------------------------
// nsFormControlList implementation, this could go away if there were
// a lightweight collection implementation somewhere

nsFormControlList::nsFormControlList(nsHTMLFormElement* aForm) :
  mForm(aForm),
  // Initialize the elements list to have an initial capacity
  // of 8 to reduce allocations on small forms.
  mElements(8)
{
  SetIsDOMBinding();
}

nsFormControlList::~nsFormControlList()
{
  mForm = nsnull;
  Clear();
}

nsresult nsFormControlList::Init()
{
  mNameLookupTable.Init(NS_FORM_CONTROL_LIST_HASHTABLE_SIZE);
  return NS_OK;
}

void
nsFormControlList::DropFormReference()
{
  mForm = nsnull;
  Clear();
}

void
nsFormControlList::Clear()
{
  // Null out childrens' pointer to me.  No refcounting here
  for (PRInt32 i = mElements.Length() - 1; i >= 0; i--) {
    mElements[i]->ClearForm(false);
  }
  mElements.Clear();

  for (PRInt32 i = mNotInElements.Length() - 1; i >= 0; i--) {
    mNotInElements[i]->ClearForm(false);
  }
  mNotInElements.Clear();

  mNameLookupTable.Clear();
}

void
nsFormControlList::FlushPendingNotifications()
{
  if (mForm) {
    nsIDocument* doc = mForm->GetCurrentDoc();
    if (doc) {
      doc->FlushPendingNotifications(Flush_Content);
    }
  }
}

static PLDHashOperator
ControlTraverser(const nsAString& key, nsISupports* control, void* userArg)
{
  nsCycleCollectionTraversalCallback *cb = 
    static_cast<nsCycleCollectionTraversalCallback*>(userArg);
 
  cb->NoteXPCOMChild(control);
  return PL_DHASH_NEXT;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsFormControlList)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsFormControlList)
  tmp->Clear();
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsFormControlList)
  tmp->mNameLookupTable.EnumerateRead(ControlTraverser, &cb);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END
NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(nsFormControlList)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_TRACE_END

DOMCI_DATA(HTMLCollection, nsFormControlList)

// XPConnect interface list for nsFormControlList
NS_INTERFACE_TABLE_HEAD(nsFormControlList)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_TABLE2(nsFormControlList,
                      nsIHTMLCollection,
                      nsIDOMHTMLCollection)
  NS_INTERFACE_TABLE_TO_MAP_SEGUE_CYCLE_COLLECTION(nsFormControlList)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(HTMLCollection)
NS_INTERFACE_MAP_END


NS_IMPL_CYCLE_COLLECTING_ADDREF(nsFormControlList)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsFormControlList)


// nsIDOMHTMLCollection interface

NS_IMETHODIMP    
nsFormControlList::GetLength(PRUint32* aLength)
{
  FlushPendingNotifications();
  *aLength = mElements.Length();
  return NS_OK;
}

NS_IMETHODIMP
nsFormControlList::Item(PRUint32 aIndex, nsIDOMNode** aReturn)
{
  nsISupports* item = GetNodeAt(aIndex);
  if (!item) {
    *aReturn = nsnull;

    return NS_OK;
  }

  return CallQueryInterface(item, aReturn);
}

NS_IMETHODIMP 
nsFormControlList::NamedItem(const nsAString& aName,
                             nsIDOMNode** aReturn)
{
  FlushPendingNotifications();

  *aReturn = nsnull;

  nsCOMPtr<nsISupports> supports;
  
  if (!mNameLookupTable.Get(aName, getter_AddRefs(supports))) {
    // key not found
    return NS_OK;
  }

  if (!supports) {
    return NS_OK;
  }

  // We found something, check if it's a node
  CallQueryInterface(supports, aReturn);
  if (*aReturn) {
    return NS_OK;
  }

  // If not, we check if it's a node list.
  nsCOMPtr<nsIDOMNodeList> nodeList = do_QueryInterface(supports);
  NS_ASSERTION(nodeList, "Huh, what's going one here?");
  if (!nodeList) {
    return NS_OK;
  }

  // And since we're only asking for one node here, we return the first
  // one from the list.
  return nodeList->Item(0, aReturn);
}

nsISupports*
nsFormControlList::NamedItemInternal(const nsAString& aName,
                                     bool aFlushContent)
{
  if (aFlushContent) {
    FlushPendingNotifications();
  }

  return mNameLookupTable.GetWeak(aName);
}

nsresult
nsFormControlList::AddElementToTable(nsGenericHTMLFormElement* aChild,
                                     const nsAString& aName)
{
  if (!ShouldBeInElements(aChild)) {
    return NS_OK;
  }

  nsCOMPtr<nsISupports> supports;
  mNameLookupTable.Get(aName, getter_AddRefs(supports));

  if (!supports) {
    // No entry found, add the form control
    mNameLookupTable.Put(aName, NS_ISUPPORTS_CAST(nsIContent*, aChild));
  } else {
    // Found something in the hash, check its type
    nsCOMPtr<nsIContent> content = do_QueryInterface(supports);

    if (content) {
      // Check if the new content is the same as the one we found in the
      // hash, if it is then we leave it in the hash as it is, this will
      // happen if a form control has both a name and an id with the same
      // value
      if (content == aChild) {
        return NS_OK;
      }

      // Found an element, create a list, add the element to the list and put
      // the list in the hash
      nsSimpleContentList *list = new nsSimpleContentList(mForm);

      // If an element has a @form, we can assume it *might* be able to not have
      // a parent and still be in the form.
      NS_ASSERTION(content->HasAttr(kNameSpaceID_None, nsGkAtoms::form) ||
                   content->GetParent(), "Item in list without parent");

      // Determine the ordering between the new and old element.
      bool newFirst = nsContentUtils::PositionIsBefore(aChild, content);

      list->AppendElement(newFirst ? aChild : content);
      list->AppendElement(newFirst ? content : aChild);


      nsCOMPtr<nsISupports> listSupports = do_QueryObject(list);

      // Replace the element with the list.
      mNameLookupTable.Put(aName, listSupports);
    } else {
      // There's already a list in the hash, add the child to the list
      nsCOMPtr<nsIDOMNodeList> nodeList = do_QueryInterface(supports);
      NS_ENSURE_TRUE(nodeList, NS_ERROR_FAILURE);

      // Upcast, uggly, but it works!
      nsSimpleContentList *list =
        static_cast<nsSimpleContentList*>(nodeList.get());

      NS_ASSERTION(list->Length() > 1,
                   "List should have been converted back to a single element");

      // Fast-path appends; this check is ok even if the child is
      // already in the list, since if it tests true the child would
      // have come at the end of the list, and the PositionIsBefore
      // will test false.
      if (nsContentUtils::PositionIsBefore(list->GetNodeAt(list->Length() - 1), aChild)) {
        list->AppendElement(aChild);
        return NS_OK;
      }

      // If a control has a name equal to its id, it could be in the
      // list already.
      if (list->IndexOf(aChild) != -1) {
        return NS_OK;
      }
      
      // first is the first possible insertion index, last is the last possible
      // insertion index
      PRUint32 first = 0;
      PRUint32 last = list->Length() - 1;
      PRUint32 mid;
      
      // Stop when there is only one index in our range
      while (last != first) {
        mid = (first + last) / 2;
          
        if (nsContentUtils::PositionIsBefore(aChild, list->GetNodeAt(mid)))
          last = mid;
        else
          first = mid + 1;
      }

      list->InsertElementAt(aChild, first);
    }
  }

  return NS_OK;
}

nsresult
nsFormControlList::IndexOfControl(nsIFormControl* aControl,
                                  PRInt32* aIndex)
{
  // Note -- not a DOM method; callers should handle flushing themselves
  
  NS_ENSURE_ARG_POINTER(aIndex);

  *aIndex = mElements.IndexOf(aControl);

  return NS_OK;
}

nsresult
nsFormControlList::RemoveElementFromTable(nsGenericHTMLFormElement* aChild,
                                          const nsAString& aName)
{
  if (!ShouldBeInElements(aChild)) {
    return NS_OK;
  }

  nsCOMPtr<nsISupports> supports;

  if (!mNameLookupTable.Get(aName, getter_AddRefs(supports)))
    return NS_OK;

  nsCOMPtr<nsIFormControl> fctrl(do_QueryInterface(supports));

  if (fctrl) {
    // Single element in the hash, just remove it if it's the one
    // we're trying to remove...
    if (fctrl == aChild) {
      mNameLookupTable.Remove(aName);
    }

    return NS_OK;
  }

  nsCOMPtr<nsIDOMNodeList> nodeList(do_QueryInterface(supports));
  NS_ENSURE_TRUE(nodeList, NS_ERROR_FAILURE);

  // Upcast, uggly, but it works!
  nsBaseContentList *list = static_cast<nsBaseContentList*>(nodeList.get());

  list->RemoveElement(aChild);

  PRUint32 length = 0;
  list->GetLength(&length);

  if (!length) {
    // If the list is empty we remove if from our hash, this shouldn't
    // happen tho
    mNameLookupTable.Remove(aName);
  } else if (length == 1) {
    // Only one element left, replace the list in the hash with the
    // single element.
    nsIContent* node = list->GetNodeAt(0);
    if (node) {
      mNameLookupTable.Put(aName, node);
    }
  }

  return NS_OK;
}

nsresult
nsFormControlList::GetSortedControls(nsTArray<nsGenericHTMLFormElement*>& aControls) const
{
#ifdef DEBUG
  AssertDocumentOrder(mElements, mForm);
  AssertDocumentOrder(mNotInElements, mForm);
#endif

  aControls.Clear();

  // Merge the elements list and the not in elements list. Both lists are
  // already sorted.
  PRUint32 elementsLen = mElements.Length();
  PRUint32 notInElementsLen = mNotInElements.Length();
  aControls.SetCapacity(elementsLen + notInElementsLen);

  PRUint32 elementsIdx = 0;
  PRUint32 notInElementsIdx = 0;

  while (elementsIdx < elementsLen || notInElementsIdx < notInElementsLen) {
    // Check whether we're done with mElements
    if (elementsIdx == elementsLen) {
      NS_ASSERTION(notInElementsIdx < notInElementsLen,
                   "Should have remaining not-in-elements");
      // Append the remaining mNotInElements elements
      if (!aControls.AppendElements(mNotInElements.Elements() +
                                      notInElementsIdx,
                                    notInElementsLen -
                                      notInElementsIdx)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      break;
    }
    // Check whether we're done with mNotInElements
    if (notInElementsIdx == notInElementsLen) {
      NS_ASSERTION(elementsIdx < elementsLen,
                   "Should have remaining in-elements");
      // Append the remaining mElements elements
      if (!aControls.AppendElements(mElements.Elements() +
                                      elementsIdx,
                                    elementsLen -
                                      elementsIdx)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
      break;
    }
    // Both lists have elements left.
    NS_ASSERTION(mElements[elementsIdx] &&
                 mNotInElements[notInElementsIdx],
                 "Should have remaining elements");
    // Determine which of the two elements should be ordered
    // first and add it to the end of the list.
    nsGenericHTMLFormElement* elementToAdd;
    if (CompareFormControlPosition(mElements[elementsIdx],
                                   mNotInElements[notInElementsIdx],
                                   mForm) < 0) {
      elementToAdd = mElements[elementsIdx];
      ++elementsIdx;
    } else {
      elementToAdd = mNotInElements[notInElementsIdx];
      ++notInElementsIdx;
    }
    // Add the first element to the list.
    if (!aControls.AppendElement(elementToAdd)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  NS_ASSERTION(aControls.Length() == elementsLen + notInElementsLen,
               "Not all form controls were added to the sorted list");
#ifdef DEBUG
  AssertDocumentOrder(aControls, mForm);
#endif

  return NS_OK;
}

nsIContent*
nsFormControlList::GetNodeAt(PRUint32 aIndex)
{
  FlushPendingNotifications();

  return mElements.SafeElementAt(aIndex, nsnull);
}

nsISupports*
nsFormControlList::GetNamedItem(const nsAString& aName, nsWrapperCache **aCache)
{
  nsISupports *item = NamedItemInternal(aName, true);
  *aCache = nsnull;
  return item;
}
