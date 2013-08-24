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
 * The Original Code is the Mozilla SVG project.
 *
 * The Initial Developer of the Original Code is IBM Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#ifndef __NS_SVGLENGTH2_H__
#define __NS_SVGLENGTH2_H__

#include "nsIDOMSVGLength.h"
#include "nsIDOMSVGAnimatedLength.h"
#include "nsSVGUtils.h"
#include "nsSVGElement.h"
#include "nsDOMError.h"

#ifdef MOZ_SMIL
#include "nsISMILAttr.h"
class nsSMILValue;
class nsISMILType;
#endif // MOZ_SMIL

class nsIFrame;

class nsSVGLength2
{

public:
  void Init(PRUint8 aCtxType = nsSVGUtils::XY,
            PRUint8 aAttrEnum = 0xff,
            float aValue = 0,
            PRUint8 aUnitType = nsIDOMSVGLength::SVG_LENGTHTYPE_NUMBER) {
    mAnimVal = mBaseVal = aValue;
    mSpecifiedUnitType = aUnitType;
    mAttrEnum = aAttrEnum;
    mCtxType = aCtxType;
    mIsAnimated = PR_FALSE;
  }

  nsresult SetBaseValueString(const nsAString& aValue,
                              nsSVGElement *aSVGElement,
                              PRBool aDoSetAttr);
  void GetBaseValueString(nsAString& aValue);
  void GetAnimValueString(nsAString& aValue);

  float GetBaseValue(nsSVGElement* aSVGElement) const
    { return mBaseVal / GetUnitScaleFactor(aSVGElement, mSpecifiedUnitType); }
  float GetAnimValue(nsSVGElement* aSVGElement) const
    { return mAnimVal / GetUnitScaleFactor(aSVGElement, mSpecifiedUnitType); }
  float GetAnimValue(nsIFrame* aFrame) const
    { return mAnimVal / GetUnitScaleFactor(aFrame, mSpecifiedUnitType); }

  PRUint8 GetCtxType() const { return mCtxType; }
  PRUint8 GetSpecifiedUnitType() const { return mSpecifiedUnitType; }
  PRBool IsPercentage() const
    { return mSpecifiedUnitType == nsIDOMSVGLength::SVG_LENGTHTYPE_PERCENTAGE; }
  float GetAnimValInSpecifiedUnits() const { return mAnimVal; }
  float GetBaseValInSpecifiedUnits() const { return mBaseVal; }

  float GetBaseValue(nsSVGSVGElement* aCtx) const
    { return mBaseVal / GetUnitScaleFactor(aCtx, mSpecifiedUnitType); }
  float GetAnimValue(nsSVGSVGElement* aCtx) const
    { return mAnimVal / GetUnitScaleFactor(aCtx, mSpecifiedUnitType); }
  
  nsresult ToDOMAnimatedLength(nsIDOMSVGAnimatedLength **aResult,
                               nsSVGElement* aSVGElement);
#ifdef MOZ_SMIL
  // Returns a new nsISMILAttr object that the caller must delete
  nsISMILAttr* ToSMILAttr(nsSVGElement* aSVGElement);
#endif // MOZ_SMIL

private:
  
  float mAnimVal;
  float mBaseVal;
  PRUint8 mSpecifiedUnitType;
  PRUint8 mAttrEnum; // element specified tracking for attribute
  PRUint8 mCtxType; // X, Y or Unspecified
  PRPackedBool mIsAnimated;
  
  static float GetMMPerPixel() { return MM_PER_INCH_FLOAT / 96; }
  float GetAxisLength(nsIFrame *aNonSVGFrame) const;
  static float GetEmLength(nsIFrame *aFrame)
    { return nsSVGUtils::GetFontSize(aFrame); }
  static float GetExLength(nsIFrame *aFrame)
    { return nsSVGUtils::GetFontXHeight(aFrame); }
  float GetUnitScaleFactor(nsIFrame *aFrame, PRUint8 aUnitType) const;

  float GetMMPerPixel(nsSVGSVGElement *aCtx) const;
  float GetAxisLength(nsSVGSVGElement *aCtx) const;
  static float GetEmLength(nsSVGElement *aSVGElement)
    { return nsSVGUtils::GetFontSize(aSVGElement); }
  static float GetExLength(nsSVGElement *aSVGElement)
    { return nsSVGUtils::GetFontXHeight(aSVGElement); }
  float GetUnitScaleFactor(nsSVGElement *aSVGElement, PRUint8 aUnitType) const;
  float GetUnitScaleFactor(nsSVGSVGElement *aCtx, PRUint8 aUnitType) const;

  void SetBaseValue(float aValue, nsSVGElement *aSVGElement);
  void SetBaseValueInSpecifiedUnits(float aValue, nsSVGElement *aSVGElement);
  void SetAnimValue(float aValue, nsSVGElement *aSVGElement);
  nsresult NewValueSpecifiedUnits(PRUint16 aUnitType, float aValue,
                                  nsSVGElement *aSVGElement);
  nsresult ConvertToSpecifiedUnits(PRUint16 aUnitType, nsSVGElement *aSVGElement);
  nsresult ToDOMBaseVal(nsIDOMSVGLength **aResult, nsSVGElement* aSVGElement);
  nsresult ToDOMAnimVal(nsIDOMSVGLength **aResult, nsSVGElement* aSVGElement);

  struct DOMBaseVal : public nsIDOMSVGLength
  {
    NS_DECL_CYCLE_COLLECTING_ISUPPORTS
    NS_DECL_CYCLE_COLLECTION_CLASS(DOMBaseVal)

    DOMBaseVal(nsSVGLength2* aVal, nsSVGElement *aSVGElement)
      : mVal(aVal), mSVGElement(aSVGElement) {}
    virtual ~DOMBaseVal();
    
    nsSVGLength2* mVal; // kept alive because it belongs to mSVGElement
    nsRefPtr<nsSVGElement> mSVGElement;
    
    NS_IMETHOD GetUnitType(PRUint16* aResult)
      { *aResult = mVal->mSpecifiedUnitType; return NS_OK; }

    NS_IMETHOD GetValue(float* aResult)
      { *aResult = mVal->GetBaseValue(mSVGElement); return NS_OK; }
    NS_IMETHOD SetValue(float aValue)
      {
        NS_ENSURE_FINITE(aValue, NS_ERROR_ILLEGAL_VALUE);
        mVal->SetBaseValue(aValue, mSVGElement);
        return NS_OK;
      }

    NS_IMETHOD GetValueInSpecifiedUnits(float* aResult)
      { *aResult = mVal->mBaseVal; return NS_OK; }
    NS_IMETHOD SetValueInSpecifiedUnits(float aValue)
      {
        NS_ENSURE_FINITE(aValue, NS_ERROR_ILLEGAL_VALUE);
        mVal->SetBaseValueInSpecifiedUnits(aValue, mSVGElement);
        return NS_OK;
      }

    NS_IMETHOD SetValueAsString(const nsAString& aValue)
      { return mVal->SetBaseValueString(aValue, mSVGElement, PR_TRUE); }
    NS_IMETHOD GetValueAsString(nsAString& aValue)
      { mVal->GetBaseValueString(aValue); return NS_OK; }

    NS_IMETHOD NewValueSpecifiedUnits(PRUint16 unitType,
                                      float valueInSpecifiedUnits)
      {
        return mVal->NewValueSpecifiedUnits(unitType, valueInSpecifiedUnits,
                                            mSVGElement); }

    NS_IMETHOD ConvertToSpecifiedUnits(PRUint16 unitType)
      { return mVal->ConvertToSpecifiedUnits(unitType, mSVGElement); }
  };

  struct DOMAnimVal : public nsIDOMSVGLength
  {
    NS_DECL_CYCLE_COLLECTING_ISUPPORTS
    NS_DECL_CYCLE_COLLECTION_CLASS(DOMAnimVal)

    DOMAnimVal(nsSVGLength2* aVal, nsSVGElement *aSVGElement)
      : mVal(aVal), mSVGElement(aSVGElement) {}
    virtual ~DOMAnimVal();
    
    nsSVGLength2* mVal; // kept alive because it belongs to mSVGElement
    nsRefPtr<nsSVGElement> mSVGElement;
    
    // Script may have modified animation parameters or timeline -- DOM getters
    // need to flush any resample requests to reflect these modifications.
    NS_IMETHOD GetUnitType(PRUint16* aResult)
    {
#ifdef MOZ_SMIL
      mSVGElement->FlushAnimations();
#endif
      *aResult = mVal->mSpecifiedUnitType;
      return NS_OK;
    }

    NS_IMETHOD GetValue(float* aResult)
    {
#ifdef MOZ_SMIL
      mSVGElement->FlushAnimations();
#endif
      *aResult = mVal->GetAnimValue(mSVGElement);
      return NS_OK;
    }
    NS_IMETHOD SetValue(float aValue)
      { return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR; }

    NS_IMETHOD GetValueInSpecifiedUnits(float* aResult)
    {
#ifdef MOZ_SMIL
      mSVGElement->FlushAnimations();
#endif
      *aResult = mVal->mAnimVal;
      return NS_OK;
    }
    NS_IMETHOD SetValueInSpecifiedUnits(float aValue)
      { return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR; }

    NS_IMETHOD SetValueAsString(const nsAString& aValue)
      { return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR; }
    NS_IMETHOD GetValueAsString(nsAString& aValue)
    {
#ifdef MOZ_SMIL
      mSVGElement->FlushAnimations();
#endif
      mVal->GetAnimValueString(aValue);
      return NS_OK;
    }

    NS_IMETHOD NewValueSpecifiedUnits(PRUint16 unitType,
                                      float valueInSpecifiedUnits)
      { return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR; }

    NS_IMETHOD ConvertToSpecifiedUnits(PRUint16 unitType)
      { return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR; }
  };

public:
  struct DOMAnimatedLength : public nsIDOMSVGAnimatedLength
  {
    NS_DECL_CYCLE_COLLECTING_ISUPPORTS
    NS_DECL_CYCLE_COLLECTION_CLASS(DOMAnimatedLength)

    DOMAnimatedLength(nsSVGLength2* aVal, nsSVGElement *aSVGElement)
      : mVal(aVal), mSVGElement(aSVGElement) {}
    virtual ~DOMAnimatedLength();
    
    nsSVGLength2* mVal; // kept alive because it belongs to content
    nsRefPtr<nsSVGElement> mSVGElement;

    NS_IMETHOD GetBaseVal(nsIDOMSVGLength **aBaseVal)
      { return mVal->ToDOMBaseVal(aBaseVal, mSVGElement); }

    NS_IMETHOD GetAnimVal(nsIDOMSVGLength **aAnimVal)
      { return mVal->ToDOMAnimVal(aAnimVal, mSVGElement); }
  };

#ifdef MOZ_SMIL
  struct SMILLength : public nsISMILAttr
  {
  public:
    SMILLength(nsSVGLength2* aVal, nsSVGElement *aSVGElement)
      : mVal(aVal), mSVGElement(aSVGElement) {}

    // These will stay alive because a nsISMILAttr only lives as long
    // as the Compositing step, and DOM elements don't get a chance to
    // die during that.
    nsSVGLength2* mVal;
    nsSVGElement* mSVGElement;

    // nsISMILAttr methods
    virtual nsresult ValueFromString(const nsAString& aStr,
                                     const nsISMILAnimationElement* aSrcElement,
                                     nsSMILValue &aValue,
                                     PRBool& aPreventCachingOfSandwich) const;
    virtual nsSMILValue GetBaseValue() const;
    virtual void ClearAnimValue();
    virtual nsresult SetAnimValue(const nsSMILValue& aValue);
  };
#endif // MOZ_SMIL
};

#endif //  __NS_SVGLENGTH2_H__
