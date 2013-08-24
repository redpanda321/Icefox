/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "nsIDOMHTMLButtonElement.h"
#include "nsIDOMHTMLFormElement.h"
#include "nsIDOMEventTarget.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsIPresShell.h"
#include "nsStyleConsts.h"
#include "nsPresContext.h"
#include "nsIFormControl.h"
#include "nsIForm.h"
#include "nsFormSubmission.h"
#include "nsFormSubmissionConstants.h"
#include "nsIURL.h"
#include "nsEventStateManager.h"
#include "nsIFrame.h"
#include "nsIFormControlFrame.h"
#include "nsIDOMEvent.h"
#include "nsIDOMNSEvent.h"
#include "nsIDocument.h"
#include "nsGUIEvent.h"
#include "nsUnicharUtils.h"
#include "nsLayoutUtils.h"
#include "nsEventDispatcher.h"
#include "nsPresState.h"
#include "nsLayoutErrors.h"
#include "nsFocusManager.h"
#include "nsHTMLFormElement.h"
#include "nsIConstraintValidation.h"
#include "mozAutoDocUpdate.h"

using namespace mozilla::dom;

#define NS_IN_SUBMIT_CLICK      (1 << 0)
#define NS_OUTER_ACTIVATE_EVENT (1 << 1)

static const nsAttrValue::EnumTable kButtonTypeTable[] = {
  { "button", NS_FORM_BUTTON_BUTTON },
  { "reset", NS_FORM_BUTTON_RESET },
  { "submit", NS_FORM_BUTTON_SUBMIT },
  { 0 }
};

// Default type is 'submit'.
static const nsAttrValue::EnumTable* kButtonDefaultType = &kButtonTypeTable[2];

class nsHTMLButtonElement : public nsGenericHTMLFormElement,
                            public nsIDOMHTMLButtonElement,
                            public nsIConstraintValidation
{
public:
  using nsIConstraintValidation::GetValidationMessage;

  nsHTMLButtonElement(already_AddRefed<nsINodeInfo> aNodeInfo,
                      FromParser aFromParser = NOT_FROM_PARSER);
  virtual ~nsHTMLButtonElement();

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMNode
  NS_FORWARD_NSIDOMNODE(nsGenericHTMLFormElement::)

  // nsIDOMElement
  NS_FORWARD_NSIDOMELEMENT(nsGenericHTMLFormElement::)

  // nsIDOMHTMLElement
  NS_FORWARD_NSIDOMHTMLELEMENT_BASIC(nsGenericHTMLFormElement::)
  NS_SCRIPTABLE NS_IMETHOD Click() {
    return nsGenericHTMLFormElement::Click();
  }
  NS_SCRIPTABLE NS_IMETHOD GetTabIndex(PRInt32* aTabIndex);
  NS_SCRIPTABLE NS_IMETHOD SetTabIndex(PRInt32 aTabIndex);
  NS_SCRIPTABLE NS_IMETHOD Focus() {
    return nsGenericHTMLFormElement::Focus();
  }
  NS_SCRIPTABLE NS_IMETHOD GetDraggable(bool* aDraggable) {
    return nsGenericHTMLFormElement::GetDraggable(aDraggable);
  }
  NS_SCRIPTABLE NS_IMETHOD GetInnerHTML(nsAString& aInnerHTML) {
    return nsGenericHTMLFormElement::GetInnerHTML(aInnerHTML);
  }
  NS_SCRIPTABLE NS_IMETHOD SetInnerHTML(const nsAString& aInnerHTML) {
    return nsGenericHTMLFormElement::SetInnerHTML(aInnerHTML);
  }

  // nsIDOMHTMLButtonElement
  NS_DECL_NSIDOMHTMLBUTTONELEMENT

  // overriden nsIFormControl methods
  NS_IMETHOD_(PRUint32) GetType() const { return mType; }
  NS_IMETHOD Reset();
  NS_IMETHOD SubmitNamesValues(nsFormSubmission* aFormSubmission);
  NS_IMETHOD SaveState();
  bool RestoreState(nsPresState* aState);

  nsEventStates IntrinsicState() const;

  /**
   * Called when an attribute is about to be changed
   */
  virtual nsresult BeforeSetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                                 const nsAttrValueOrString* aValue,
                                 bool aNotify);
  /**
   * Called when an attribute has just been changed
   */
  nsresult AfterSetAttr(PRInt32 aNamespaceID, nsIAtom* aName,
                        const nsAttrValue* aValue, bool aNotify);

  // nsIContent overrides...
  virtual bool IsHTMLFocusable(bool aWithMouse, bool *aIsFocusable, PRInt32 *aTabIndex);
  virtual bool ParseAttribute(PRInt32 aNamespaceID,
                                nsIAtom* aAttribute,
                                const nsAString& aValue,
                                nsAttrValue& aResult);
  virtual nsresult PreHandleEvent(nsEventChainPreVisitor& aVisitor);
  virtual nsresult PostHandleEvent(nsEventChainPostVisitor& aVisitor);

  virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              bool aCompileEventHandlers);
  virtual void UnbindFromTree(bool aDeep = true,
                              bool aNullParent = true);

  virtual nsresult Clone(nsINodeInfo *aNodeInfo, nsINode **aResult) const;
  virtual void DoneCreatingElement();
  virtual nsXPCClassInfo* GetClassInfo();
  virtual nsIDOMNode* AsDOMNode() { return this; }
protected:
  PRUint8 mType;
  bool mDisabledChanged;
  bool mInInternalActivate;
  bool mInhibitStateRestoration;
};


// Construction, destruction


NS_IMPL_NS_NEW_HTML_ELEMENT_CHECK_PARSER(Button)


nsHTMLButtonElement::nsHTMLButtonElement(already_AddRefed<nsINodeInfo> aNodeInfo,
                                         FromParser aFromParser)
  : nsGenericHTMLFormElement(aNodeInfo),
    mType(kButtonDefaultType->value),
    mDisabledChanged(false),
    mInInternalActivate(false),
    mInhibitStateRestoration(!!(aFromParser & FROM_PARSER_FRAGMENT))
{
  // <button> is always barred from constraint validation.
  SetBarredFromConstraintValidation(true);

  // Set up our default state: enabled
  AddStatesSilently(NS_EVENT_STATE_ENABLED);
}

nsHTMLButtonElement::~nsHTMLButtonElement()
{
}

// nsISupports

NS_IMPL_ADDREF_INHERITED(nsHTMLButtonElement, nsGenericElement)
NS_IMPL_RELEASE_INHERITED(nsHTMLButtonElement, nsGenericElement)


DOMCI_NODE_DATA(HTMLButtonElement, nsHTMLButtonElement)

// QueryInterface implementation for nsHTMLButtonElement
NS_INTERFACE_TABLE_HEAD(nsHTMLButtonElement)
  NS_HTML_CONTENT_INTERFACE_TABLE2(nsHTMLButtonElement,
                                   nsIDOMHTMLButtonElement,
                                   nsIConstraintValidation)
  NS_HTML_CONTENT_INTERFACE_TABLE_TO_MAP_SEGUE(nsHTMLButtonElement,
                                               nsGenericHTMLFormElement)
NS_HTML_CONTENT_INTERFACE_TABLE_TAIL_CLASSINFO(HTMLButtonElement)

// nsIConstraintValidation
NS_IMPL_NSICONSTRAINTVALIDATION(nsHTMLButtonElement)

// nsIDOMHTMLButtonElement


NS_IMPL_ELEMENT_CLONE(nsHTMLButtonElement)


// nsIDOMHTMLButtonElement

NS_IMETHODIMP
nsHTMLButtonElement::GetForm(nsIDOMHTMLFormElement** aForm)
{
  return nsGenericHTMLFormElement::GetForm(aForm);
}

NS_IMPL_BOOL_ATTR(nsHTMLButtonElement, Autofocus, autofocus)
NS_IMPL_BOOL_ATTR(nsHTMLButtonElement, Disabled, disabled)
NS_IMPL_ACTION_ATTR(nsHTMLButtonElement, FormAction, formaction)
NS_IMPL_ENUM_ATTR_DEFAULT_VALUE(nsHTMLButtonElement, FormEnctype, formenctype,
                                kFormDefaultEnctype->tag)
NS_IMPL_ENUM_ATTR_DEFAULT_VALUE(nsHTMLButtonElement, FormMethod, formmethod,
                                kFormDefaultMethod->tag)
NS_IMPL_BOOL_ATTR(nsHTMLButtonElement, FormNoValidate, formnovalidate)
NS_IMPL_STRING_ATTR(nsHTMLButtonElement, FormTarget, formtarget)
NS_IMPL_STRING_ATTR(nsHTMLButtonElement, Name, name)
NS_IMPL_INT_ATTR(nsHTMLButtonElement, TabIndex, tabindex)
NS_IMPL_STRING_ATTR(nsHTMLButtonElement, Value, value)
NS_IMPL_ENUM_ATTR_DEFAULT_VALUE(nsHTMLButtonElement, Type, type,
                                kButtonDefaultType->tag)

bool
nsHTMLButtonElement::IsHTMLFocusable(bool aWithMouse, bool *aIsFocusable, PRInt32 *aTabIndex)
{
  if (nsGenericHTMLFormElement::IsHTMLFocusable(aWithMouse, aIsFocusable, aTabIndex)) {
    return true;
  }

  *aIsFocusable = 
#ifdef XP_MACOSX
    (!aWithMouse || nsFocusManager::sMouseFocusesFormControl) &&
#endif
    !IsDisabled();

  return false;
}

bool
nsHTMLButtonElement::ParseAttribute(PRInt32 aNamespaceID,
                                    nsIAtom* aAttribute,
                                    const nsAString& aValue,
                                    nsAttrValue& aResult)
{
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::type) {
      // XXX ARG!! This is major evilness. ParseAttribute
      // shouldn't set members. Override SetAttr instead
      bool success = aResult.ParseEnumValue(aValue, kButtonTypeTable, false);
      if (success) {
        mType = aResult.GetEnumValue();
      } else {
        mType = kButtonDefaultType->value;
      }

      return success;
    }

    if (aAttribute == nsGkAtoms::formmethod) {
      return aResult.ParseEnumValue(aValue, kFormMethodTable, false);
    }
    if (aAttribute == nsGkAtoms::formenctype) {
      return aResult.ParseEnumValue(aValue, kFormEnctypeTable, false);
    }
  }

  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aResult);
}

nsresult
nsHTMLButtonElement::PreHandleEvent(nsEventChainPreVisitor& aVisitor)
{
  nsIFormControlFrame* formControlFrame = GetFormControlFrame(false);
  nsIFrame* formFrame = NULL;
  if (formControlFrame) {
    formFrame = do_QueryFrame(formControlFrame);
  }

  aVisitor.mCanHandle = false;
  if (IsElementDisabledForEvents(aVisitor.mEvent->message, formFrame)) {
    return NS_OK;
  }

  // Track whether we're in the outermost Dispatch invocation that will
  // cause activation of the input.  That is, if we're a click event, or a
  // DOMActivate that was dispatched directly, this will be set, but if we're
  // a DOMActivate dispatched from click handling, it will not be set.
  bool outerActivateEvent =
    (NS_IS_MOUSE_LEFT_CLICK(aVisitor.mEvent) ||
     (aVisitor.mEvent->message == NS_UI_ACTIVATE &&
      !mInInternalActivate));

  if (outerActivateEvent) {
    aVisitor.mItemFlags |= NS_OUTER_ACTIVATE_EVENT;
    if (mType == NS_FORM_BUTTON_SUBMIT && mForm) {
      aVisitor.mItemFlags |= NS_IN_SUBMIT_CLICK;
      // tell the form that we are about to enter a click handler.
      // that means that if there are scripted submissions, the
      // latest one will be deferred until after the exit point of the handler.
      mForm->OnSubmitClickBegin(this);
    }
  }

  return nsGenericHTMLElement::PreHandleEvent(aVisitor);
}

nsresult
nsHTMLButtonElement::PostHandleEvent(nsEventChainPostVisitor& aVisitor)
{
  nsresult rv = NS_OK;
  if (!aVisitor.mPresContext) {
    return rv;
  }

  if (aVisitor.mEventStatus != nsEventStatus_eConsumeNoDefault &&
      NS_IS_MOUSE_LEFT_CLICK(aVisitor.mEvent)) {
    nsUIEvent actEvent(NS_IS_TRUSTED_EVENT(aVisitor.mEvent), NS_UI_ACTIVATE, 1);

    nsCOMPtr<nsIPresShell> shell = aVisitor.mPresContext->GetPresShell();
    if (shell) {
      nsEventStatus status = nsEventStatus_eIgnore;
      mInInternalActivate = true;
      shell->HandleDOMEventWithTarget(this, &actEvent, &status);
      mInInternalActivate = false;

      // If activate is cancelled, we must do the same as when click is
      // cancelled (revert the checkbox to its original value).
      if (status == nsEventStatus_eConsumeNoDefault)
        aVisitor.mEventStatus = status;
    }
  }

  // mForm is null if the event handler removed us from the document (bug 194582).
  if ((aVisitor.mItemFlags & NS_IN_SUBMIT_CLICK) && mForm) {
    // tell the form that we are about to exit a click handler
    // so the form knows not to defer subsequent submissions
    // the pending ones that were created during the handler
    // will be flushed or forgoten.
    mForm->OnSubmitClickEnd();
  }

  if (nsEventStatus_eIgnore == aVisitor.mEventStatus) {
    switch (aVisitor.mEvent->message) {
      case NS_KEY_PRESS:
      case NS_KEY_UP:
        {
          // For backwards compat, trigger buttons with space or enter
          // (bug 25300)
          nsKeyEvent * keyEvent = (nsKeyEvent *)aVisitor.mEvent;
          if ((keyEvent->keyCode == NS_VK_RETURN &&
               NS_KEY_PRESS == aVisitor.mEvent->message) ||
              (keyEvent->keyCode == NS_VK_SPACE &&
               NS_KEY_UP == aVisitor.mEvent->message)) {
            nsEventStatus status = nsEventStatus_eIgnore;

            nsMouseEvent event(NS_IS_TRUSTED_EVENT(aVisitor.mEvent),
                               NS_MOUSE_CLICK, nsnull,
                               nsMouseEvent::eReal);
            event.inputSource = nsIDOMMouseEvent::MOZ_SOURCE_KEYBOARD;
            nsEventDispatcher::Dispatch(static_cast<nsIContent*>(this),
                                        aVisitor.mPresContext, &event, nsnull,
                                        &status);
            aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
          }
        }
        break;// NS_KEY_PRESS

      case NS_MOUSE_BUTTON_DOWN:
        {
          if (aVisitor.mEvent->eventStructType == NS_MOUSE_EVENT) {
            if (static_cast<nsMouseEvent*>(aVisitor.mEvent)->button ==
                  nsMouseEvent::eLeftButton) {
              if (NS_IS_TRUSTED_EVENT(aVisitor.mEvent)) {
                nsEventStateManager* esm =
                  aVisitor.mPresContext->EventStateManager();
                nsEventStateManager::SetActiveManager(
                  static_cast<nsEventStateManager*>(esm), this);
              }
              nsIFocusManager* fm = nsFocusManager::GetFocusManager();
              if (fm)
                fm->SetFocus(this, nsIFocusManager::FLAG_BYMOUSE |
                                   nsIFocusManager::FLAG_NOSCROLL);
              aVisitor.mEvent->flags |= NS_EVENT_FLAG_PREVENT_MULTIPLE_ACTIONS;
            } else if (static_cast<nsMouseEvent*>(aVisitor.mEvent)->button ==
                         nsMouseEvent::eMiddleButton ||
                       static_cast<nsMouseEvent*>(aVisitor.mEvent)->button ==
                         nsMouseEvent::eRightButton) {
              // cancel all of these events for buttons
              //XXXsmaug What to do with these events? Why these should be cancelled?
              if (aVisitor.mDOMEvent) {
                aVisitor.mDOMEvent->StopPropagation();
              }
            }
          }
        }
        break;

      // cancel all of these events for buttons
      //XXXsmaug What to do with these events? Why these should be cancelled?
      case NS_MOUSE_BUTTON_UP:
      case NS_MOUSE_DOUBLECLICK:
        {
          if (aVisitor.mEvent->eventStructType == NS_MOUSE_EVENT &&
              aVisitor.mDOMEvent &&
              (static_cast<nsMouseEvent*>(aVisitor.mEvent)->button ==
                 nsMouseEvent::eMiddleButton ||
               static_cast<nsMouseEvent*>(aVisitor.mEvent)->button ==
                 nsMouseEvent::eRightButton)) {
            aVisitor.mDOMEvent->StopPropagation();
          }
        }
        break;

      case NS_MOUSE_ENTER_SYNTH:
        {
          aVisitor.mPresContext->EventStateManager()->
            SetContentState(this, NS_EVENT_STATE_HOVER);
          aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
        }
        break;

        // XXX this doesn't seem to do anything yet
      case NS_MOUSE_EXIT_SYNTH:
        {
          aVisitor.mPresContext->EventStateManager()->
            SetContentState(nsnull, NS_EVENT_STATE_HOVER);
          aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
        }
        break;

      default:
        break;
    }
    if (aVisitor.mItemFlags & NS_OUTER_ACTIVATE_EVENT) {
      if (mForm && (mType == NS_FORM_BUTTON_SUBMIT ||
                    mType == NS_FORM_BUTTON_RESET)) {
        nsFormEvent event(true,
                          (mType == NS_FORM_BUTTON_RESET)
                          ? NS_FORM_RESET : NS_FORM_SUBMIT);
        event.originator     = this;
        nsEventStatus status = nsEventStatus_eIgnore;

        nsCOMPtr<nsIPresShell> presShell =
          aVisitor.mPresContext->GetPresShell();
        // If |nsIPresShell::Destroy| has been called due to
        // handling the event, the pres context will return
        // a null pres shell.  See bug 125624.
        //
        // Using presShell to dispatch the event. It makes sure that
        // event is not handled if the window is being destroyed.
        if (presShell && (event.message != NS_FORM_SUBMIT ||
                          mForm->HasAttr(kNameSpaceID_None, nsGkAtoms::novalidate) ||
                          // We know the element is a submit control, if this check is moved,
                          // make sure formnovalidate is used only if it's a submit control.
                          HasAttr(kNameSpaceID_None, nsGkAtoms::formnovalidate) ||
                          mForm->CheckValidFormSubmission())) {
          // TODO: removing this code and have the submit event sent by the form
          // see bug 592124.
          // Hold a strong ref while dispatching
          nsRefPtr<nsHTMLFormElement> form(mForm);
          presShell->HandleDOMEventWithTarget(mForm, &event, &status);
          aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
        }
      }
    }
  } else if ((aVisitor.mItemFlags & NS_IN_SUBMIT_CLICK) && mForm) {
    // Tell the form to flush a possible pending submission.
    // the reason is that the script returned false (the event was
    // not ignored) so if there is a stored submission, it needs to
    // be submitted immediatelly.
    // Note, NS_IN_SUBMIT_CLICK is set only when we're in outer activate event.
    mForm->FlushPendingSubmission();
  } //if

  return rv;
}

nsresult
nsHTMLButtonElement::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                                nsIContent* aBindingParent,
                                bool aCompileEventHandlers)
{
  nsresult rv = nsGenericHTMLFormElement::BindToTree(aDocument, aParent,
                                                     aBindingParent,
                                                     aCompileEventHandlers);
  NS_ENSURE_SUCCESS(rv, rv);

  // Update our state; we may now be the default submit element
  UpdateState(false);

  return NS_OK;
}

void
nsHTMLButtonElement::UnbindFromTree(bool aDeep, bool aNullParent)
{
  nsGenericHTMLFormElement::UnbindFromTree(aDeep, aNullParent);

  // Update our state; we may no longer be the default submit element
  UpdateState(false);
}

NS_IMETHODIMP
nsHTMLButtonElement::Reset()
{
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLButtonElement::SubmitNamesValues(nsFormSubmission* aFormSubmission)
{
  //
  // We only submit if we were the button pressed
  //
  if (aFormSubmission->GetOriginatingElement() != this) {
    return NS_OK;
  }

  // Disabled elements don't submit
  if (IsDisabled()) {
    return NS_OK;
  }

  //
  // Get the name (if no name, no submit)
  //
  nsAutoString name;
  GetAttr(kNameSpaceID_None, nsGkAtoms::name, name);
  if (name.IsEmpty()) {
    return NS_OK;
  }

  //
  // Get the value
  //
  nsAutoString value;
  nsresult rv = GetValue(value);
  if (NS_FAILED(rv)) {
    return rv;
  }

  //
  // Submit
  //
  return aFormSubmission->AddNameValuePair(name, value);
}

void
nsHTMLButtonElement::DoneCreatingElement()
{
  if (!mInhibitStateRestoration) {
    // Restore state as needed.
    RestoreFormControlState(this, this);
  }
}

nsresult
nsHTMLButtonElement::BeforeSetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                                   const nsAttrValueOrString* aValue,
                                   bool aNotify)
{
  if (aNotify && aName == nsGkAtoms::disabled &&
      aNameSpaceID == kNameSpaceID_None) {
    mDisabledChanged = true;
  }

  return nsGenericHTMLFormElement::BeforeSetAttr(aNameSpaceID, aName,
                                                 aValue, aNotify);
}

nsresult
nsHTMLButtonElement::AfterSetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                                  const nsAttrValue* aValue, bool aNotify)
{
  if (aNameSpaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::type) {
      if (!aValue) {
        mType = kButtonDefaultType->value;
      }

      UpdateState(aNotify);
    }
  }

  return nsGenericHTMLFormElement::AfterSetAttr(aNameSpaceID, aName,
                                                aValue, aNotify);
}

NS_IMETHODIMP
nsHTMLButtonElement::SaveState()
{
  if (!mDisabledChanged) {
    return NS_OK;
  }
  
  nsPresState *state = nsnull;
  nsresult rv = GetPrimaryPresState(this, &state);
  if (state) {
    // We do not want to save the real disabled state but the disabled
    // attribute.
    state->SetDisabled(HasAttr(kNameSpaceID_None, nsGkAtoms::disabled));
  }

  return rv;
}

bool
nsHTMLButtonElement::RestoreState(nsPresState* aState)
{
  if (aState && aState->IsDisabledSet()) {
    SetDisabled(aState->GetDisabled());
  }

  return false;
}

nsEventStates
nsHTMLButtonElement::IntrinsicState() const
{
  nsEventStates state = nsGenericHTMLFormElement::IntrinsicState();

  if (mForm && !mForm->GetValidity() && IsSubmitControl()) {
    state |= NS_EVENT_STATE_MOZ_SUBMITINVALID;
  }

  return state;
}
