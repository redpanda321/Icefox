/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __NS_SVGSTRING_H__
#define __NS_SVGSTRING_H__

#include "nsDOMError.h"
#include "nsIDOMSVGAnimatedString.h"
#include "nsSVGElement.h"
#include "mozilla/Attributes.h"

class nsSVGString
{

public:
  void Init(PRUint8 aAttrEnum) {
    mAnimVal = nsnull;
    mAttrEnum = aAttrEnum;
    mIsBaseSet = false;
  }

  void SetBaseValue(const nsAString& aValue,
                    nsSVGElement *aSVGElement,
                    bool aDoSetAttr);
  void GetBaseValue(nsAString& aValue, const nsSVGElement *aSVGElement) const
    { aSVGElement->GetStringBaseValue(mAttrEnum, aValue); }

  void SetAnimValue(const nsAString& aValue, nsSVGElement *aSVGElement);
  void GetAnimValue(nsAString& aValue, const nsSVGElement *aSVGElement) const;

  // Returns true if the animated value of this string has been explicitly
  // set (either by animation, or by taking on the base value which has been
  // explicitly set by markup or a DOM call), false otherwise.
  // If this returns false, the animated value is still valid, that is,
  // useable, and represents the default base value of the attribute.
  bool IsExplicitlySet() const
    { return !!mAnimVal || mIsBaseSet; }

  nsresult ToDOMAnimatedString(nsIDOMSVGAnimatedString **aResult,
                               nsSVGElement *aSVGElement);
  // Returns a new nsISMILAttr object that the caller must delete
  nsISMILAttr* ToSMILAttr(nsSVGElement *aSVGElement);

private:

  nsAutoPtr<nsString> mAnimVal;
  PRUint8 mAttrEnum; // element specified tracking for attribute
  bool mIsBaseSet;

public:
  struct DOMAnimatedString MOZ_FINAL : public nsIDOMSVGAnimatedString
  {
    NS_DECL_CYCLE_COLLECTING_ISUPPORTS
    NS_DECL_CYCLE_COLLECTION_CLASS(DOMAnimatedString)

    DOMAnimatedString(nsSVGString *aVal, nsSVGElement *aSVGElement)
      : mVal(aVal), mSVGElement(aSVGElement) {}

    nsSVGString* mVal; // kept alive because it belongs to content
    nsRefPtr<nsSVGElement> mSVGElement;

    NS_IMETHOD GetBaseVal(nsAString & aResult)
      { mVal->GetBaseValue(aResult, mSVGElement); return NS_OK; }
    NS_IMETHOD SetBaseVal(const nsAString & aValue)
      { mVal->SetBaseValue(aValue, mSVGElement, true); return NS_OK; }

    NS_IMETHOD GetAnimVal(nsAString & aResult)
    { 
      mSVGElement->FlushAnimations();
      mVal->GetAnimValue(aResult, mSVGElement); return NS_OK;
    }

  };
  struct SMILString : public nsISMILAttr
  {
  public:
    SMILString(nsSVGString *aVal, nsSVGElement *aSVGElement)
      : mVal(aVal), mSVGElement(aSVGElement) {}

    // These will stay alive because a nsISMILAttr only lives as long
    // as the Compositing step, and DOM elements don't get a chance to
    // die during that.
    nsSVGString* mVal;
    nsSVGElement* mSVGElement;

    // nsISMILAttr methods
    virtual nsresult ValueFromString(const nsAString& aStr,
                                     const nsISMILAnimationElement *aSrcElement,
                                     nsSMILValue& aValue,
                                     bool& aPreventCachingOfSandwich) const;
    virtual nsSMILValue GetBaseValue() const;
    virtual void ClearAnimValue();
    virtual nsresult SetAnimValue(const nsSMILValue& aValue);
  };
};
#endif //__NS_SVGSTRING_H__
