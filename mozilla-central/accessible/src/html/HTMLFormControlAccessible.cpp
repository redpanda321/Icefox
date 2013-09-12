/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLFormControlAccessible.h"

#include "Accessible-inl.h"
#include "nsAccUtils.h"
#include "nsARIAMap.h"
#include "nsEventShell.h"
#include "nsTextEquivUtils.h"
#include "Relation.h"
#include "Role.h"
#include "States.h"

#include "nsContentList.h"
#include "nsHTMLInputElement.h"
#include "nsIAccessibleRelation.h"
#include "nsIDOMNSEditableElement.h"
#include "nsIDOMHTMLTextAreaElement.h"
#include "nsIEditor.h"
#include "nsIFormControl.h"
#include "nsIFrame.h"
#include "nsINameSpaceManager.h"
#include "nsISelectionController.h"
#include "jsapi.h"
#include "nsIJSContextStack.h"
#include "nsIServiceManager.h"
#include "nsITextControlFrame.h"

#include "mozilla/Preferences.h"

using namespace mozilla;
using namespace mozilla::a11y;

////////////////////////////////////////////////////////////////////////////////
// HTMLCheckboxAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLCheckboxAccessible::
  HTMLCheckboxAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  LeafAccessible(aContent, aDoc)
{
}

role
HTMLCheckboxAccessible::NativeRole()
{
  return roles::CHECKBUTTON;
}

uint8_t
HTMLCheckboxAccessible::ActionCount()
{
  return 1;
}

NS_IMETHODIMP
HTMLCheckboxAccessible::GetActionName(uint8_t aIndex, nsAString& aName)
{
  if (aIndex == eAction_Click) {    // 0 is the magic value for default action
    // cycle, check or uncheck
    uint64_t state = NativeState();

    if (state & states::CHECKED)
      aName.AssignLiteral("uncheck"); 
    else if (state & states::MIXED)
      aName.AssignLiteral("cycle"); 
    else
      aName.AssignLiteral("check"); 

    return NS_OK;
  }
  return NS_ERROR_INVALID_ARG;
}

NS_IMETHODIMP
HTMLCheckboxAccessible::DoAction(uint8_t aIndex)
{
  if (aIndex != 0)
    return NS_ERROR_INVALID_ARG;

  DoCommand();
  return NS_OK;
}

uint64_t
HTMLCheckboxAccessible::NativeState()
{
  uint64_t state = LeafAccessible::NativeState();

  state |= states::CHECKABLE;
  nsHTMLInputElement* input = nsHTMLInputElement::FromContent(mContent);
  if (!input)
    return state;

  if (input->Indeterminate())
    return state | states::MIXED;

  if (input->Checked())
    return state | states::CHECKED;
           
  return state;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLCheckboxAccessible: Widgets

bool
HTMLCheckboxAccessible::IsWidget() const
{
  return true;
}


////////////////////////////////////////////////////////////////////////////////
// HTMLRadioButtonAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLRadioButtonAccessible::
  HTMLRadioButtonAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  RadioButtonAccessible(aContent, aDoc)
{
}

uint64_t
HTMLRadioButtonAccessible::NativeState()
{
  uint64_t state = AccessibleWrap::NativeState();

  state |= states::CHECKABLE;

  nsHTMLInputElement* input = nsHTMLInputElement::FromContent(mContent);
  if (input && input->Checked())
    state |= states::CHECKED;

  return state;
}

void
HTMLRadioButtonAccessible::GetPositionAndSizeInternal(int32_t* aPosInSet,
                                                      int32_t* aSetSize)
{
  int32_t namespaceId = mContent->NodeInfo()->NamespaceID();
  nsAutoString tagName;
  mContent->NodeInfo()->GetName(tagName);

  nsAutoString type;
  mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::type, type);
  nsAutoString name;
  mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::name, name);

  nsRefPtr<nsContentList> inputElms;

  nsCOMPtr<nsIFormControl> formControlNode(do_QueryInterface(mContent));
  dom::Element* formElm = formControlNode->GetFormElement();
  if (formElm)
    inputElms = NS_GetContentList(formElm, namespaceId, tagName);
  else
    inputElms = NS_GetContentList(mContent->OwnerDoc(), namespaceId, tagName);
  NS_ENSURE_TRUE_VOID(inputElms);

  uint32_t inputCount = inputElms->Length(false);

  // Compute posinset and setsize.
  int32_t indexOf = 0;
  int32_t count = 0;

  for (uint32_t index = 0; index < inputCount; index++) {
    nsIContent* inputElm = inputElms->Item(index, false);
    if (inputElm->AttrValueIs(kNameSpaceID_None, nsGkAtoms::type,
                              type, eCaseMatters) &&
        inputElm->AttrValueIs(kNameSpaceID_None, nsGkAtoms::name,
                              name, eCaseMatters)) {
      count++;
      if (inputElm == mContent)
        indexOf = count;
    }
  }

  *aPosInSet = indexOf;
  *aSetSize = count;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLButtonAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLButtonAccessible::
  HTMLButtonAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  HyperTextAccessibleWrap(aContent, aDoc)
{
}

uint8_t
HTMLButtonAccessible::ActionCount()
{
  return 1;
}

NS_IMETHODIMP
HTMLButtonAccessible::GetActionName(uint8_t aIndex, nsAString& aName)
{
  if (aIndex == eAction_Click) {
    aName.AssignLiteral("press"); 
    return NS_OK;
  }
  return NS_ERROR_INVALID_ARG;
}

NS_IMETHODIMP
HTMLButtonAccessible::DoAction(uint8_t aIndex)
{
  if (aIndex != eAction_Click)
    return NS_ERROR_INVALID_ARG;

  DoCommand();
  return NS_OK;
}

uint64_t
HTMLButtonAccessible::State()
{
  uint64_t state = HyperTextAccessibleWrap::State();
  if (state == states::DEFUNCT)
    return state;

  // Inherit states from input@type="file" suitable for the button. Note,
  // no special processing for unavailable state since inheritance is supplied
  // other code paths.
  if (mParent && mParent->IsHTMLFileInput()) {
    uint64_t parentState = mParent->State();
    state |= parentState & (states::BUSY | states::REQUIRED |
                            states::HASPOPUP | states::INVALID);
  }

  return state;
}

uint64_t
HTMLButtonAccessible::NativeState()
{
  uint64_t state = HyperTextAccessibleWrap::NativeState();

  nsEventStates elmState = mContent->AsElement()->State();
  if (elmState.HasState(NS_EVENT_STATE_DEFAULT))
    state |= states::DEFAULT;

  return state;
}

role
HTMLButtonAccessible::NativeRole()
{
  return roles::PUSHBUTTON;
}

ENameValueFlag
HTMLButtonAccessible::NativeName(nsString& aName)
{
  ENameValueFlag nameFlag = Accessible::NativeName(aName);
  if (!aName.IsEmpty() || mContent->Tag() != nsGkAtoms::input)
    return nameFlag;

  // Note: No need to check @value attribute since it results in anonymous text
  // node. The name is calculated from subtree in this case.
  if (!mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::alt, aName)) {
    // Use the button's (default) label if nothing else works
    nsIFrame* frame = GetFrame();
    if (frame) {
      nsIFormControlFrame* fcFrame = do_QueryFrame(frame);
      if (fcFrame)
        fcFrame->GetFormProperty(nsGkAtoms::defaultLabel, aName);
    }
  }

  if (aName.IsEmpty() &&
      !mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::src, aName)) {
    mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::data, aName);
  }

  aName.CompressWhitespace();
  return eNameOK;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLButtonAccessible: Widgets

bool
HTMLButtonAccessible::IsWidget() const
{
  return true;
}


////////////////////////////////////////////////////////////////////////////////
// HTMLTextFieldAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLTextFieldAccessible::
  HTMLTextFieldAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  HyperTextAccessibleWrap(aContent, aDoc)
{
}

NS_IMPL_ISUPPORTS_INHERITED2(HTMLTextFieldAccessible,
                             Accessible,                             
                             nsIAccessibleText,
                             nsIAccessibleEditableText)

role
HTMLTextFieldAccessible::NativeRole()
{
  if (mContent->AttrValueIs(kNameSpaceID_None, nsGkAtoms::type,
                            nsGkAtoms::password, eIgnoreCase)) {
    return roles::PASSWORD_TEXT;
  }
  
  return roles::ENTRY;
}

ENameValueFlag
HTMLTextFieldAccessible::NativeName(nsString& aName)
{
  ENameValueFlag nameFlag = Accessible::NativeName(aName);
  if (!aName.IsEmpty())
    return nameFlag;

  if (mContent->GetBindingParent()) {
    // XXX: bug 459640
    // There's a binding parent.
    // This means we're part of another control, so use parent accessible for name.
    // This ensures that a textbox inside of a XUL widget gets
    // an accessible name.
    Accessible* parent = Parent();
    if (parent)
      parent->GetName(aName);
  }

  if (!aName.IsEmpty())
    return eNameOK;

  // text inputs and textareas might have useful placeholder text
  mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::placeholder, aName);
  return eNameOK;
}

void
HTMLTextFieldAccessible::Value(nsString& aValue)
{
  aValue.Truncate();
  if (NativeState() & states::PROTECTED)    // Don't return password text!
    return;

  nsCOMPtr<nsIDOMHTMLTextAreaElement> textArea(do_QueryInterface(mContent));
  if (textArea) {
    textArea->GetValue(aValue);
    return;
  }

  nsHTMLInputElement* input = nsHTMLInputElement::FromContent(mContent);
  if (input)
    input->GetValue(aValue);
}

void
HTMLTextFieldAccessible::ApplyARIAState(uint64_t* aState) const
{
  HyperTextAccessibleWrap::ApplyARIAState(aState);

  aria::MapToState(aria::eARIAAutoComplete, mContent->AsElement(), aState);
}

uint64_t
HTMLTextFieldAccessible::State()
{
  uint64_t state = HyperTextAccessibleWrap::State();
  if (state & states::DEFUNCT)
    return state;

  // Inherit states from input@type="file" suitable for the button. Note,
  // no special processing for unavailable state since inheritance is supplied
  // by other code paths.
  if (mParent && mParent->IsHTMLFileInput()) {
    uint64_t parentState = mParent->State();
    state |= parentState & (states::BUSY | states::REQUIRED |
      states::HASPOPUP | states::INVALID);
  }

  return state;
}

uint64_t
HTMLTextFieldAccessible::NativeState()
{
  uint64_t state = HyperTextAccessibleWrap::NativeState();

  // can be focusable, focused, protected. readonly, unavailable, selected
  if (mContent->AttrValueIs(kNameSpaceID_None, nsGkAtoms::type,
                            nsGkAtoms::password, eIgnoreCase)) {
    state |= states::PROTECTED;
  }

  if (mContent->HasAttr(kNameSpaceID_None, nsGkAtoms::readonly)) {
    state |= states::READONLY;
  }

  // Is it an <input> or a <textarea> ?
  nsHTMLInputElement* input = nsHTMLInputElement::FromContent(mContent);
  state |= input && input->IsSingleLineTextControl() ?
    states::SINGLE_LINE : states::MULTI_LINE;

  if (!(state & states::EDITABLE) ||
      (state & (states::PROTECTED | states::MULTI_LINE)))
    return state;

  // Expose autocomplete states if this input is part of autocomplete widget.
  Accessible* widget = ContainerWidget();
  if (widget && widget-IsAutoComplete()) {
    state |= states::HASPOPUP | states::SUPPORTS_AUTOCOMPLETION;
    return state;
  }

  // Expose autocomplete state if it has associated autocomplete list.
  if (mContent->HasAttr(kNameSpaceID_None, nsGkAtoms::list))
    return state | states::SUPPORTS_AUTOCOMPLETION;

  // No parent can mean a fake widget created for XUL textbox. If accessible
  // is unattached from tree then we don't care.
  if (mParent && Preferences::GetBool("browser.formfill.enable")) {
    // Check to see if autocompletion is allowed on this input. We don't expose
    // it for password fields even though the entire password can be remembered
    // for a page if the user asks it to be. However, the kind of autocomplete
    // we're talking here is based on what the user types, where a popup of
    // possible choices comes up.
    nsAutoString autocomplete;
    mContent->GetAttr(kNameSpaceID_None, nsGkAtoms::autocomplete,
                      autocomplete);

    if (!autocomplete.LowerCaseEqualsLiteral("off")) {
      nsIContent* formContent = input->GetFormElement();
      if (formContent) {
        formContent->GetAttr(kNameSpaceID_None,
                             nsGkAtoms::autocomplete, autocomplete);
      }

      if (!formContent || !autocomplete.LowerCaseEqualsLiteral("off"))
        state |= states::SUPPORTS_AUTOCOMPLETION;
    }
  }

  return state;
}

uint8_t
HTMLTextFieldAccessible::ActionCount()
{
  return 1;
}

NS_IMETHODIMP
HTMLTextFieldAccessible::GetActionName(uint8_t aIndex, nsAString& aName)
{
  if (aIndex == eAction_Click) {
    aName.AssignLiteral("activate");
    return NS_OK;
  }
  return NS_ERROR_INVALID_ARG;
}

NS_IMETHODIMP
HTMLTextFieldAccessible::DoAction(uint8_t aIndex)
{
  if (aIndex == 0) {
    nsHTMLInputElement* element = nsHTMLInputElement::FromContent(mContent);
    if (element)
      return element->Focus();

    return NS_ERROR_FAILURE;
  }
  return NS_ERROR_INVALID_ARG;
}

already_AddRefed<nsIEditor>
HTMLTextFieldAccessible::GetEditor() const
{
  nsCOMPtr<nsIDOMNSEditableElement> editableElt(do_QueryInterface(mContent));
  if (!editableElt)
    return nullptr;

  // nsGenericHTMLElement::GetEditor has a security check.
  // Make sure we're not restricted by the permissions of
  // whatever script is currently running.
  nsCOMPtr<nsIJSContextStack> stack =
    do_GetService("@mozilla.org/js/xpc/ContextStack;1");
  bool pushed = stack && NS_SUCCEEDED(stack->Push(nullptr));

  nsCOMPtr<nsIEditor> editor;
  editableElt->GetEditor(getter_AddRefs(editor));

  if (pushed) {
    JSContext* cx;
    stack->Pop(&cx);
    NS_ASSERTION(!cx, "context should be null");
  }

  return editor.forget();
}

////////////////////////////////////////////////////////////////////////////////
// HTMLTextFieldAccessible: Widgets

bool
HTMLTextFieldAccessible::IsWidget() const
{
  return true;
}

Accessible*
HTMLTextFieldAccessible::ContainerWidget() const
{
  return mParent && mParent->Role() == roles::AUTOCOMPLETE ? mParent : nullptr;
}


////////////////////////////////////////////////////////////////////////////////
// HTMLFileInputAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLFileInputAccessible::
HTMLFileInputAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  HyperTextAccessibleWrap(aContent, aDoc)
{
  mType = eHTMLFileInputType;
}

role
HTMLFileInputAccessible::NativeRole()
{
  // JAWS wants a text container, others don't mind. No specific role in
  // AT APIs.
  return roles::TEXT_CONTAINER;
}

nsresult
HTMLFileInputAccessible::HandleAccEvent(AccEvent* aEvent)
{
  nsresult rv = HyperTextAccessibleWrap::HandleAccEvent(aEvent);
  NS_ENSURE_SUCCESS(rv, rv);

  // Redirect state change events for inherited states to child controls. Note,
  // unavailable state is not redirected. That's a standard for unavailable
  // state handling.
  AccStateChangeEvent* event = downcast_accEvent(aEvent);
  if (event &&
      (event->GetState() == states::BUSY ||
       event->GetState() == states::REQUIRED ||
       event->GetState() == states::HASPOPUP ||
       event->GetState() == states::INVALID)) {
    Accessible* input = GetChildAt(0);
    if (input && input->Role() == roles::ENTRY) {
      nsRefPtr<AccStateChangeEvent> childEvent =
        new AccStateChangeEvent(input, event->GetState(),
                                event->IsStateEnabled(),
                                (event->IsFromUserInput() ? eFromUserInput : eNoUserInput));
      nsEventShell::FireEvent(childEvent);
    }

    Accessible* button = GetChildAt(1);
    if (button && button->Role() == roles::PUSHBUTTON) {
      nsRefPtr<AccStateChangeEvent> childEvent =
        new AccStateChangeEvent(button, event->GetState(),
                                event->IsStateEnabled(),
                                (event->IsFromUserInput() ? eFromUserInput : eNoUserInput));
      nsEventShell::FireEvent(childEvent);
    }
  }
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLGroupboxAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLGroupboxAccessible::
  HTMLGroupboxAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  HyperTextAccessibleWrap(aContent, aDoc)
{
}

role
HTMLGroupboxAccessible::NativeRole()
{
  return roles::GROUPING;
}

nsIContent*
HTMLGroupboxAccessible::GetLegend()
{
  for (nsIContent* legendContent = mContent->GetFirstChild(); legendContent;
       legendContent = legendContent->GetNextSibling()) {
    if (legendContent->NodeInfo()->Equals(nsGkAtoms::legend,
                                          mContent->GetNameSpaceID())) {
      // Either XHTML namespace or no namespace
      return legendContent;
    }
  }

  return nullptr;
}

ENameValueFlag
HTMLGroupboxAccessible::NativeName(nsString& aName)
{
  ENameValueFlag nameFlag = Accessible::NativeName(aName);
  if (!aName.IsEmpty())
    return nameFlag;

  nsIContent* legendContent = GetLegend();
  if (legendContent)
    nsTextEquivUtils::AppendTextEquivFromContent(this, legendContent, &aName);

  return eNameOK;
}

Relation
HTMLGroupboxAccessible::RelationByType(uint32_t aType)
{
  Relation rel = HyperTextAccessibleWrap::RelationByType(aType);
    // No override for label, so use <legend> for this <fieldset>
  if (aType == nsIAccessibleRelation::RELATION_LABELLED_BY)
    rel.AppendTarget(mDoc, GetLegend());

  return rel;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLLegendAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLLegendAccessible::
  HTMLLegendAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  HyperTextAccessibleWrap(aContent, aDoc)
{
}

Relation
HTMLLegendAccessible::RelationByType(uint32_t aType)
{
  Relation rel = HyperTextAccessibleWrap::RelationByType(aType);
  if (aType != nsIAccessibleRelation::RELATION_LABEL_FOR)
    return rel;

  Accessible* groupbox = Parent();
  if (groupbox && groupbox->Role() == roles::GROUPING)
    rel.AppendTarget(groupbox);

  return rel;
}

role
HTMLLegendAccessible::NativeRole()
{
  return roles::LABEL;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLFigureAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLFigureAccessible::
  HTMLFigureAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  HyperTextAccessibleWrap(aContent, aDoc)
{
}

already_AddRefed<nsIPersistentProperties>
HTMLFigureAccessible::NativeAttributes()
{
  nsCOMPtr<nsIPersistentProperties> attributes =
    HyperTextAccessibleWrap::NativeAttributes();

  // Expose figure xml-role.
  nsAccUtils::SetAccAttr(attributes, nsGkAtoms::xmlroles,
                         NS_LITERAL_STRING("figure"));
  return attributes.forget();
}

role
HTMLFigureAccessible::NativeRole()
{
  return roles::FIGURE;
}

ENameValueFlag
HTMLFigureAccessible::NativeName(nsString& aName)
{
  ENameValueFlag nameFlag = HyperTextAccessibleWrap::NativeName(aName);
  if (!aName.IsEmpty())
    return nameFlag;

  nsIContent* captionContent = Caption();
  if (captionContent)
    nsTextEquivUtils::AppendTextEquivFromContent(this, captionContent, &aName);

  return eNameOK;
}

Relation
HTMLFigureAccessible::RelationByType(uint32_t aType)
{
  Relation rel = HyperTextAccessibleWrap::RelationByType(aType);
  if (aType == nsIAccessibleRelation::RELATION_LABELLED_BY)
    rel.AppendTarget(mDoc, Caption());

  return rel;
}

nsIContent*
HTMLFigureAccessible::Caption() const
{
  for (nsIContent* childContent = mContent->GetFirstChild(); childContent;
       childContent = childContent->GetNextSibling()) {
    if (childContent->NodeInfo()->Equals(nsGkAtoms::figcaption,
                                         mContent->GetNameSpaceID())) {
      return childContent;
    }
  }

  return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
// HTMLFigcaptionAccessible
////////////////////////////////////////////////////////////////////////////////

HTMLFigcaptionAccessible::
  HTMLFigcaptionAccessible(nsIContent* aContent, DocAccessible* aDoc) :
  HyperTextAccessibleWrap(aContent, aDoc)
{
}

role
HTMLFigcaptionAccessible::NativeRole()
{
  return roles::CAPTION;
}

Relation
HTMLFigcaptionAccessible::RelationByType(uint32_t aType)
{
  Relation rel = HyperTextAccessibleWrap::RelationByType(aType);
  if (aType != nsIAccessibleRelation::RELATION_LABEL_FOR)
    return rel;

  Accessible* figure = Parent();
  if (figure &&
      figure->GetContent()->NodeInfo()->Equals(nsGkAtoms::figure,
                                               mContent->GetNameSpaceID())) {
    rel.AppendTarget(figure);
  }

  return rel;
}
