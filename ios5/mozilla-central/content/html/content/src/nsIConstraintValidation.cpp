/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIConstraintValidation.h"

#include "nsAString.h"
#include "nsGenericHTMLElement.h"
#include "nsHTMLFormElement.h"
#include "nsDOMValidityState.h"
#include "nsIFormControl.h"
#include "nsContentUtils.h"

const PRUint16 nsIConstraintValidation::sContentSpecifiedMaxLengthMessage = 256;

nsIConstraintValidation::nsIConstraintValidation()
  : mValidityBitField(0)
  , mValidity(nsnull)
  // By default, all elements are subjects to constraint validation.
  , mBarredFromConstraintValidation(false)
{
}

nsIConstraintValidation::~nsIConstraintValidation()
{
  if (mValidity) {
    mValidity->Disconnect();
  }
}

nsresult
nsIConstraintValidation::GetValidity(nsIDOMValidityState** aValidity)
{
  if (!mValidity) {
    mValidity = new nsDOMValidityState(this);
  }

  NS_ADDREF(*aValidity = mValidity);

  return NS_OK;
}

NS_IMETHODIMP
nsIConstraintValidation::GetValidationMessage(nsAString& aValidationMessage)
{
  aValidationMessage.Truncate();

  if (IsCandidateForConstraintValidation() && !IsValid()) {
    nsCOMPtr<nsIContent> content = do_QueryInterface(this);
    NS_ASSERTION(content, "This class should be inherited by HTML elements only!");

    nsAutoString authorMessage;
    content->GetAttr(kNameSpaceID_None, nsGkAtoms::x_moz_errormessage,
                     authorMessage);

    if (!authorMessage.IsEmpty()) {
      aValidationMessage.Assign(authorMessage);
      if (aValidationMessage.Length() > sContentSpecifiedMaxLengthMessage) {
        aValidationMessage.Truncate(sContentSpecifiedMaxLengthMessage);
      }
    } else if (GetValidityState(VALIDITY_STATE_CUSTOM_ERROR)) {
      aValidationMessage.Assign(mCustomValidity);
      if (aValidationMessage.Length() > sContentSpecifiedMaxLengthMessage) {
        aValidationMessage.Truncate(sContentSpecifiedMaxLengthMessage);
      }
    } else if (GetValidityState(VALIDITY_STATE_TOO_LONG)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_TOO_LONG);
    } else if (GetValidityState(VALIDITY_STATE_VALUE_MISSING)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_VALUE_MISSING);
    } else if (GetValidityState(VALIDITY_STATE_TYPE_MISMATCH)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_TYPE_MISMATCH);
    } else if (GetValidityState(VALIDITY_STATE_PATTERN_MISMATCH)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_PATTERN_MISMATCH);
    } else if (GetValidityState(VALIDITY_STATE_RANGE_OVERFLOW)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_RANGE_OVERFLOW);
    } else if (GetValidityState(VALIDITY_STATE_RANGE_UNDERFLOW)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_RANGE_UNDERFLOW);
    } else if (GetValidityState(VALIDITY_STATE_STEP_MISMATCH)) {
      GetValidationMessage(aValidationMessage, VALIDITY_STATE_STEP_MISMATCH);
    } else {
      // There should not be other validity states.
      return NS_ERROR_UNEXPECTED;
    }
  } else {
    aValidationMessage.Truncate();
  }

  return NS_OK;
}

nsresult
nsIConstraintValidation::CheckValidity(bool* aValidity)
{
  if (!IsCandidateForConstraintValidation() || IsValid()) {
    *aValidity = true;
    return NS_OK;
  }

  *aValidity = false;

  nsCOMPtr<nsIContent> content = do_QueryInterface(this);
  NS_ASSERTION(content, "This class should be inherited by HTML elements only!");

  return nsContentUtils::DispatchTrustedEvent(content->OwnerDoc(), content,
                                              NS_LITERAL_STRING("invalid"),
                                              false, true);
}

void
nsIConstraintValidation::SetValidityState(ValidityStateType aState,
                                          bool aValue)
{
  bool previousValidity = IsValid();

  if (aValue) {
    mValidityBitField |= aState;
  } else {
    mValidityBitField &= ~aState;
  }

  // Inform the form element if our validity has changed.
  if (previousValidity != IsValid() && IsCandidateForConstraintValidation()) {
    nsCOMPtr<nsIFormControl> formCtrl = do_QueryInterface(this);
    NS_ASSERTION(formCtrl, "This interface should be used by form elements!");

    nsHTMLFormElement* form =
      static_cast<nsHTMLFormElement*>(formCtrl->GetFormElement());
    if (form) {
      form->UpdateValidity(IsValid());
    }
  }
}

void
nsIConstraintValidation::SetCustomValidity(const nsAString& aError)
{
  mCustomValidity.Assign(aError);
  SetValidityState(VALIDITY_STATE_CUSTOM_ERROR, !mCustomValidity.IsEmpty());
}

void
nsIConstraintValidation::SetBarredFromConstraintValidation(bool aBarred)
{
  bool previousBarred = mBarredFromConstraintValidation;

  mBarredFromConstraintValidation = aBarred;

  // Inform the form element if our status regarding constraint validation
  // is going to change.
  if (!IsValid() && previousBarred != mBarredFromConstraintValidation) {
    nsCOMPtr<nsIFormControl> formCtrl = do_QueryInterface(this);
    NS_ASSERTION(formCtrl, "This interface should be used by form elements!");

    nsHTMLFormElement* form =
      static_cast<nsHTMLFormElement*>(formCtrl->GetFormElement());
    if (form) {
      // If the element is going to be barred from constraint validation,
      // we can inform the form that we are now valid.
      // Otherwise, we are now invalid.
      form->UpdateValidity(aBarred);
    }
  }
}

