/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsSVGViewBox.h"
#include "prdtoa.h"
#include "nsTextFormatter.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsMathUtils.h"
#include "nsSMILValue.h"
#include "nsSVGAttrTearoffTable.h"
#include "SVGContentUtils.h"
#include "SVGViewBoxSMILType.h"
#include "nsAttrValueInlines.h"

#define NUM_VIEWBOX_COMPONENTS 4
using namespace mozilla;

/* Implementation of nsSVGViewBoxRect methods */

bool
nsSVGViewBoxRect::operator==(const nsSVGViewBoxRect& aOther) const
{
  if (&aOther == this)
    return true;

  return x == aOther.x &&
    y == aOther.y &&
    width == aOther.width &&
    height == aOther.height;
}

/* Cycle collection macros for nsSVGViewBox */

NS_SVG_VAL_IMPL_CYCLE_COLLECTION(nsSVGViewBox::DOMBaseVal, mSVGElement)
NS_SVG_VAL_IMPL_CYCLE_COLLECTION(nsSVGViewBox::DOMAnimVal, mSVGElement)
NS_SVG_VAL_IMPL_CYCLE_COLLECTION(nsSVGViewBox::DOMAnimatedRect, mSVGElement)

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsSVGViewBox::DOMBaseVal)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsSVGViewBox::DOMBaseVal)

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsSVGViewBox::DOMAnimVal)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsSVGViewBox::DOMAnimVal)

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsSVGViewBox::DOMAnimatedRect)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsSVGViewBox::DOMAnimatedRect)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsSVGViewBox::DOMBaseVal)
  NS_INTERFACE_MAP_ENTRY(nsIDOMSVGRect)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(SVGRect)
NS_INTERFACE_MAP_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsSVGViewBox::DOMAnimVal)
  NS_INTERFACE_MAP_ENTRY(nsIDOMSVGRect)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(SVGRect)
NS_INTERFACE_MAP_END

DOMCI_DATA(SVGAnimatedRect, nsSVGViewBox::DOMAnimatedRect)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsSVGViewBox::DOMAnimatedRect)
  NS_INTERFACE_MAP_ENTRY(nsIDOMSVGAnimatedRect)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(SVGAnimatedRect)
NS_INTERFACE_MAP_END

static nsSVGAttrTearoffTable<nsSVGViewBox, nsSVGViewBox::DOMAnimatedRect>
  sSVGAnimatedRectTearoffTable;
static nsSVGAttrTearoffTable<nsSVGViewBox, nsSVGViewBox::DOMBaseVal>
  sBaseSVGViewBoxTearoffTable;
static nsSVGAttrTearoffTable<nsSVGViewBox, nsSVGViewBox::DOMAnimVal>
  sAnimSVGViewBoxTearoffTable;


/* Implementation of nsSVGViewBox methods */

void
nsSVGViewBox::Init()
{
  mBaseVal = nsSVGViewBoxRect();
  mAnimVal = nullptr;
  mHasBaseVal = false;
}

void
nsSVGViewBox::SetAnimValue(float aX, float aY, float aWidth, float aHeight,
                           nsSVGElement *aSVGElement)
{
  if (!mAnimVal) {
    // it's okay if allocation fails - and no point in reporting that
    mAnimVal = new nsSVGViewBoxRect(aX, aY, aWidth, aHeight);
  } else {
    nsSVGViewBoxRect rect(aX, aY, aWidth, aHeight);
    if (rect == *mAnimVal) {
      return;
    }
    *mAnimVal = rect;
  }
  aSVGElement->DidAnimateViewBox();
}

void
nsSVGViewBox::SetBaseValue(const nsSVGViewBoxRect& aRect,
                           nsSVGElement *aSVGElement)
{
  if (mHasBaseVal && mBaseVal == aRect) {
    return;
  }

  nsAttrValue emptyOrOldValue = aSVGElement->WillChangeViewBox();

  mBaseVal = aRect;
  mHasBaseVal = true;

  aSVGElement->DidChangeViewBox(emptyOrOldValue);
  if (mAnimVal) {
    aSVGElement->AnimationNeedsResample();
  }
}

static nsresult
ToSVGViewBoxRect(const nsAString& aStr, nsSVGViewBoxRect *aViewBox)
{
  nsCharSeparatedTokenizerTemplate<IsSVGWhitespace>
    tokenizer(aStr, ',',
              nsCharSeparatedTokenizer::SEPARATOR_OPTIONAL);
  float vals[NUM_VIEWBOX_COMPONENTS];
  uint32_t i;
  for (i = 0; i < NUM_VIEWBOX_COMPONENTS && tokenizer.hasMoreTokens(); ++i) {
    NS_ConvertUTF16toUTF8 utf8Token(tokenizer.nextToken());
    const char *token = utf8Token.get();
    if (*token == '\0') {
      return NS_ERROR_DOM_SYNTAX_ERR; // empty string (e.g. two commas in a row)
    }

    char *end;
    vals[i] = float(PR_strtod(token, &end));
    if (*end != '\0' || !NS_finite(vals[i])) {
      return NS_ERROR_DOM_SYNTAX_ERR; // parse error
    }
  }

  if (i != NUM_VIEWBOX_COMPONENTS ||              // Too few values.
      tokenizer.hasMoreTokens() ||                // Too many values.
      tokenizer.lastTokenEndedWithSeparator()) {  // Trailing comma.
    return NS_ERROR_DOM_SYNTAX_ERR;
  }

  aViewBox->x = vals[0];
  aViewBox->y = vals[1];
  aViewBox->width = vals[2];
  aViewBox->height = vals[3];

  return NS_OK;
}

nsresult
nsSVGViewBox::SetBaseValueString(const nsAString& aValue,
                                 nsSVGElement *aSVGElement,
                                 bool aDoSetAttr)
{
  nsSVGViewBoxRect viewBox;
  nsresult res = ToSVGViewBoxRect(aValue, &viewBox);
  if (NS_SUCCEEDED(res)) {
    nsAttrValue emptyOrOldValue;
    if (aDoSetAttr) {
      emptyOrOldValue = aSVGElement->WillChangeViewBox();
    }
    mBaseVal = nsSVGViewBoxRect(viewBox.x, viewBox.y, viewBox.width, viewBox.height);
    mHasBaseVal = true;

    if (aDoSetAttr) {
      aSVGElement->DidChangeViewBox(emptyOrOldValue);
    }
    if (mAnimVal) {
      aSVGElement->AnimationNeedsResample();
    }
  }
  return res;
}

void
nsSVGViewBox::GetBaseValueString(nsAString& aValue) const
{
  PRUnichar buf[200];
  nsTextFormatter::snprintf(buf, sizeof(buf)/sizeof(PRUnichar),
                            NS_LITERAL_STRING("%g %g %g %g").get(),
                            (double)mBaseVal.x, (double)mBaseVal.y,
                            (double)mBaseVal.width, (double)mBaseVal.height);
  aValue.Assign(buf);
}

nsresult
nsSVGViewBox::ToDOMAnimatedRect(nsIDOMSVGAnimatedRect **aResult,
                                nsSVGElement* aSVGElement)
{
  nsRefPtr<DOMAnimatedRect> domAnimatedRect =
    sSVGAnimatedRectTearoffTable.GetTearoff(this);
  if (!domAnimatedRect) {
    domAnimatedRect = new DOMAnimatedRect(this, aSVGElement);
    sSVGAnimatedRectTearoffTable.AddTearoff(this, domAnimatedRect);
  }

  domAnimatedRect.forget(aResult);
  return NS_OK;
}

nsSVGViewBox::DOMAnimatedRect::~DOMAnimatedRect()
{
  sSVGAnimatedRectTearoffTable.RemoveTearoff(mVal);
}

nsresult
nsSVGViewBox::ToDOMBaseVal(nsIDOMSVGRect **aResult, nsSVGElement *aSVGElement)
{
  nsRefPtr<DOMBaseVal> domBaseVal =
    sBaseSVGViewBoxTearoffTable.GetTearoff(this);
  if (!domBaseVal) {
    domBaseVal = new DOMBaseVal(this, aSVGElement);
    sBaseSVGViewBoxTearoffTable.AddTearoff(this, domBaseVal);
  }

  domBaseVal.forget(aResult);
  return NS_OK;
}

nsSVGViewBox::DOMBaseVal::~DOMBaseVal()
{
  sBaseSVGViewBoxTearoffTable.RemoveTearoff(mVal);
}

nsresult
nsSVGViewBox::ToDOMAnimVal(nsIDOMSVGRect **aResult, nsSVGElement *aSVGElement)
{
  nsRefPtr<DOMAnimVal> domAnimVal =
    sAnimSVGViewBoxTearoffTable.GetTearoff(this);
  if (!domAnimVal) {
    domAnimVal = new DOMAnimVal(this, aSVGElement);
    sAnimSVGViewBoxTearoffTable.AddTearoff(this, domAnimVal);
  }

  domAnimVal.forget(aResult);
  return NS_OK;
}

nsSVGViewBox::DOMAnimVal::~DOMAnimVal()
{
  sAnimSVGViewBoxTearoffTable.RemoveTearoff(mVal);
}

NS_IMETHODIMP
nsSVGViewBox::DOMBaseVal::SetX(float aX)
{
  nsSVGViewBoxRect rect = mVal->GetBaseValue();
  rect.x = aX;
  mVal->SetBaseValue(rect.x, rect.y, rect.width, rect.height,
                     mSVGElement);
  return NS_OK;
}

NS_IMETHODIMP
nsSVGViewBox::DOMBaseVal::SetY(float aY)
{
  nsSVGViewBoxRect rect = mVal->GetBaseValue();
  rect.y = aY;
  mVal->SetBaseValue(rect.x, rect.y, rect.width, rect.height,
                     mSVGElement);
  return NS_OK;
}

NS_IMETHODIMP
nsSVGViewBox::DOMBaseVal::SetWidth(float aWidth)
{
  nsSVGViewBoxRect rect = mVal->GetBaseValue();
  rect.width = aWidth;
  mVal->SetBaseValue(rect.x, rect.y, rect.width, rect.height,
                     mSVGElement);
  return NS_OK;
}

NS_IMETHODIMP
nsSVGViewBox::DOMBaseVal::SetHeight(float aHeight)
{
  nsSVGViewBoxRect rect = mVal->GetBaseValue();
  rect.height = aHeight;
  mVal->SetBaseValue(rect.x, rect.y, rect.width, rect.height,
                     mSVGElement);
  return NS_OK;
}

nsISMILAttr*
nsSVGViewBox::ToSMILAttr(nsSVGElement *aSVGElement)
{
  return new SMILViewBox(this, aSVGElement);
}

nsresult
nsSVGViewBox::SMILViewBox
            ::ValueFromString(const nsAString& aStr,
                              const nsISMILAnimationElement* /*aSrcElement*/,
                              nsSMILValue& aValue,
                              bool& aPreventCachingOfSandwich) const
{
  nsSVGViewBoxRect viewBox;
  nsresult res = ToSVGViewBoxRect(aStr, &viewBox);
  if (NS_FAILED(res)) {
    return res;
  }
  nsSMILValue val(&SVGViewBoxSMILType::sSingleton);
  *static_cast<nsSVGViewBoxRect*>(val.mU.mPtr) = viewBox;
  aValue.Swap(val);
  aPreventCachingOfSandwich = false;
  
  return NS_OK;
}

nsSMILValue
nsSVGViewBox::SMILViewBox::GetBaseValue() const
{
  nsSMILValue val(&SVGViewBoxSMILType::sSingleton);
  *static_cast<nsSVGViewBoxRect*>(val.mU.mPtr) = mVal->mBaseVal;
  return val;
}

void
nsSVGViewBox::SMILViewBox::ClearAnimValue()
{
  if (mVal->mAnimVal) {
    mVal->mAnimVal = nullptr;
    mSVGElement->DidAnimateViewBox();
  }
}

nsresult
nsSVGViewBox::SMILViewBox::SetAnimValue(const nsSMILValue& aValue)
{
  NS_ASSERTION(aValue.mType == &SVGViewBoxSMILType::sSingleton,
               "Unexpected type to assign animated value");
  if (aValue.mType == &SVGViewBoxSMILType::sSingleton) {
    nsSVGViewBoxRect &vb = *static_cast<nsSVGViewBoxRect*>(aValue.mU.mPtr);
    mVal->SetAnimValue(vb.x, vb.y, vb.width, vb.height, mSVGElement);
  }
  return NS_OK;
}
