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
 * The Original Code is nsStyleAnimation.
 *
 * The Initial Developer of the Original Code is
 * The Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Daniel Holbert <dholbert@mozilla.com>, Mozilla Corporation
 *   L. David Baron <dbaron@dbaron.org>, Mozilla Corporation
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

/* Utilities for animation of computed style values */

#include "nsStyleAnimation.h"
#include "nsCOMArray.h"
#include "nsIStyleRule.h"
#include "nsICSSStyleRule.h"
#include "nsString.h"
#include "nsStyleContext.h"
#include "nsStyleSet.h"
#include "nsComputedDOMStyle.h"
#include "nsCSSParser.h"
#include "mozilla/css/Declaration.h"
#include "nsCSSStruct.h"
#include "prlog.h"
#include <math.h>

namespace css = mozilla::css;

// HELPER METHODS
// --------------
/*
 * Given two units, this method returns a common unit that they can both be
 * converted into, if possible.  This is intended to facilitate
 * interpolation, distance-computation, and addition between "similar" units.
 *
 * The ordering of the arguments should not affect the output of this method.
 *
 * If there's no sensible common unit, this method returns eUnit_Null.
 *
 * @param   aFirstUnit One unit to resolve.
 * @param   aFirstUnit The other unit to resolve.
 * @return  A "common" unit that both source units can be converted into, or
 *          eUnit_Null if that's not possible.
 */
static
nsStyleAnimation::Unit
GetCommonUnit(nsStyleAnimation::Unit aFirstUnit,
              nsStyleAnimation::Unit aSecondUnit)
{
  // XXXdholbert Naive implementation for now: simply require that the input
  // units match.
  if (aFirstUnit != aSecondUnit) {
    // NOTE: Some unit-pairings will need special handling,
    // e.g. percent vs coord (bug 520234)
    return nsStyleAnimation::eUnit_Null;
  }
  return aFirstUnit;
}

// Greatest Common Divisor
static PRUint32
gcd(PRUint32 a, PRUint32 b)
{
  // Euclid's algorithm; O(N) in the worst case.  (There are better
  // ways, but we don't need them for stroke-dasharray animation.)
  NS_ABORT_IF_FALSE(a > 0 && b > 0, "positive numbers expected");

  while (a != b) {
    if (a > b) {
      a = a - b;
    } else {
      b = b - a;
    }
  }

  return a;
}

// Least Common Multiple
static PRUint32
lcm(PRUint32 a, PRUint32 b)
{
  // Divide first to reduce overflow risk.
  return (a / gcd(a, b)) * b;
}

// CLASS METHODS
// -------------

PRBool
nsStyleAnimation::ComputeDistance(nsCSSProperty aProperty,
                                  const Value& aStartValue,
                                  const Value& aEndValue,
                                  double& aDistance)
{
  Unit commonUnit = GetCommonUnit(aStartValue.GetUnit(), aEndValue.GetUnit());

  switch (commonUnit) {
    case eUnit_Null:
    case eUnit_Auto:
    case eUnit_None:
    case eUnit_Normal:
    case eUnit_UnparsedString:
      return PR_FALSE;

    case eUnit_Enumerated:
      switch (aProperty) {
        case eCSSProperty_font_stretch: {
          // just like eUnit_Integer.
          PRInt32 startInt = aStartValue.GetIntValue();
          PRInt32 endInt = aEndValue.GetIntValue();
          aDistance = PR_ABS(endInt - startInt);
          return PR_TRUE;
        }
        default:
          return PR_FALSE;
      }
   case eUnit_Visibility: {
      PRInt32 startVal =
        aStartValue.GetIntValue() == NS_STYLE_VISIBILITY_VISIBLE;
      PRInt32 endVal =
        aEndValue.GetIntValue() == NS_STYLE_VISIBILITY_VISIBLE;
      aDistance = PR_ABS(startVal - endVal);
      return PR_TRUE;
    }
    case eUnit_Integer: {
      PRInt32 startInt = aStartValue.GetIntValue();
      PRInt32 endInt = aEndValue.GetIntValue();
      aDistance = PR_ABS(endInt - startInt);
      return PR_TRUE;
    }
    case eUnit_Coord: {
      nscoord startCoord = aStartValue.GetCoordValue();
      nscoord endCoord = aEndValue.GetCoordValue();
      aDistance = fabs(double(endCoord - startCoord));
      return PR_TRUE;
    }
    case eUnit_Percent: {
      float startPct = aStartValue.GetPercentValue();
      float endPct = aEndValue.GetPercentValue();
      aDistance = fabs(double(endPct - startPct));
      return PR_TRUE;
    }
    case eUnit_Float: {
      float startFloat = aStartValue.GetFloatValue();
      float endFloat = aEndValue.GetFloatValue();
      aDistance = fabs(double(endFloat - startFloat));
      return PR_TRUE;
    }
    case eUnit_Color: {
      // http://www.w3.org/TR/smil-animation/#animateColorElement says
      // that we should use Euclidean RGB cube distance.  However, we
      // have to extend that to RGBA.  For now, we'll just use the
      // Euclidean distance in the (part of the) 4-cube of premultiplied
      // colors.
      // FIXME (spec): The CSS transitions spec doesn't say whether
      // colors are premultiplied, but things work better when they are,
      // so use premultiplication.  Spec issue is still open per
      // http://lists.w3.org/Archives/Public/www-style/2009Jul/0050.html
      nscolor startColor = aStartValue.GetColorValue();
      nscolor endColor = aEndValue.GetColorValue();

      // Get a color component on a 0-1 scale, which is much easier to
      // deal with when working with alpha.
      #define GET_COMPONENT(component_, color_) \
        (NS_GET_##component_(color_) * (1.0 / 255.0))

      double startA = GET_COMPONENT(A, startColor);
      double startR = GET_COMPONENT(R, startColor) * startA;
      double startG = GET_COMPONENT(G, startColor) * startA;
      double startB = GET_COMPONENT(B, startColor) * startA;
      double endA = GET_COMPONENT(A, endColor);
      double endR = GET_COMPONENT(R, endColor) * endA;
      double endG = GET_COMPONENT(G, endColor) * endA;
      double endB = GET_COMPONENT(B, endColor) * endA;

      #undef GET_COMPONENT

      double diffA = startA - endA;
      double diffR = startR - endR;
      double diffG = startG - endG;
      double diffB = startB - endB;
      aDistance = sqrt(diffA * diffA + diffR * diffR +
                       diffG * diffG + diffB * diffB);
      return PR_TRUE;
    }
    case eUnit_CSSValuePair: {
      const nsCSSValuePair *pair1 = aStartValue.GetCSSValuePairValue();
      const nsCSSValuePair *pair2 = aEndValue.GetCSSValuePairValue();
      if (pair1->mXValue.GetUnit() != pair2->mXValue.GetUnit() ||
          pair1->mYValue.GetUnit() != pair2->mYValue.GetUnit()) {
        // At least until we have calc()
        return PR_FALSE;
      }

      double squareDistance = 0.0;
      static nsCSSValue nsCSSValuePair::* const pairValues[] = {
        &nsCSSValuePair::mXValue, &nsCSSValuePair::mYValue
      };
      for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(pairValues); ++i) {
        nsCSSValue nsCSSValuePair::*member = pairValues[i];
        NS_ABORT_IF_FALSE((pair1->*member).GetUnit() ==
                            (pair2->*member).GetUnit(),
                          "should have returned above");
        double diff;
        switch ((pair1->*member).GetUnit()) {
          case eCSSUnit_Pixel:
            diff = (pair1->*member).GetFloatValue() -
                   (pair2->*member).GetFloatValue();
            break;
          case eCSSUnit_Percent:
            diff = (pair1->*member).GetPercentValue() -
                   (pair2->*member).GetPercentValue();
            break;
          default:
            NS_ABORT_IF_FALSE(PR_FALSE, "unexpected unit");
            return PR_FALSE;
        }
        squareDistance += diff * diff;
      }

      aDistance = sqrt(squareDistance);
      return PR_TRUE;
    }
    case eUnit_CSSRect: {
      const nsCSSRect *rect1 = aStartValue.GetCSSRectValue();
      const nsCSSRect *rect2 = aEndValue.GetCSSRectValue();
      if (rect1->mTop.GetUnit() != rect2->mTop.GetUnit() ||
          rect1->mRight.GetUnit() != rect2->mRight.GetUnit() ||
          rect1->mBottom.GetUnit() != rect2->mBottom.GetUnit() ||
          rect1->mLeft.GetUnit() != rect2->mLeft.GetUnit()) {
        // At least until we have calc()
        return PR_FALSE;
      }

      double squareDistance = 0.0;
      for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(nsCSSRect::sides); ++i) {
        nsCSSValue nsCSSRect::*member = nsCSSRect::sides[i];
        NS_ABORT_IF_FALSE((rect1->*member).GetUnit() ==
                            (rect2->*member).GetUnit(),
                          "should have returned above");
        double diff;
        switch ((rect1->*member).GetUnit()) {
          case eCSSUnit_Pixel:
            diff = (rect1->*member).GetFloatValue() -
                   (rect2->*member).GetFloatValue();
            break;
          case eCSSUnit_Auto:
            diff = 0;
            break;
          default:
            NS_ABORT_IF_FALSE(PR_FALSE, "unexpected unit");
            return PR_FALSE;
        }
        squareDistance += diff * diff;
      }

      aDistance = sqrt(squareDistance);
      return PR_TRUE;
    }
    case eUnit_Dasharray: {
      // NOTE: This produces results on substantially different scales
      // for length values and percentage values, which might even be
      // mixed in the same property value.  This means the result isn't
      // particularly useful for paced animation.

      // Call AddWeighted to make us lists of the same length.
      Value normValue1, normValue2;
      if (!AddWeighted(aProperty, 1.0, aStartValue, 0.0, aEndValue,
                       normValue1) ||
          !AddWeighted(aProperty, 0.0, aStartValue, 1.0, aEndValue,
                       normValue2)) {
        return PR_FALSE;
      }

      double squareDistance = 0.0;
      const nsCSSValueList *list1 = normValue1.GetCSSValueListValue();
      const nsCSSValueList *list2 = normValue2.GetCSSValueListValue();

      NS_ABORT_IF_FALSE(!list1 == !list2, "lists should be same length");
      while (list1) {
        const nsCSSValue &val1 = list1->mValue;
        const nsCSSValue &val2 = list2->mValue;

        NS_ABORT_IF_FALSE(val1.GetUnit() == val2.GetUnit(),
                          "unit match should be assured by AddWeighted");
        double diff;
        switch (val1.GetUnit()) {
          case eCSSUnit_Percent:
            diff = val1.GetPercentValue() - val2.GetPercentValue();
            break;
          case eCSSUnit_Number:
            diff = val1.GetFloatValue() - val2.GetFloatValue();
            break;
          default:
            NS_ABORT_IF_FALSE(PR_FALSE, "unexpected unit");
            return PR_FALSE;
        }
        squareDistance += diff * diff;

        list1 = list1->mNext;
        list2 = list2->mNext;
        NS_ABORT_IF_FALSE(!list1 == !list2, "lists should be same length");
      }

      aDistance = sqrt(squareDistance);
      return PR_TRUE;
    }
    case eUnit_Shadow: {
      // Call AddWeighted to make us lists of the same length.
      Value normValue1, normValue2;
      if (!AddWeighted(aProperty, 1.0, aStartValue, 0.0, aEndValue,
                       normValue1) ||
          !AddWeighted(aProperty, 0.0, aStartValue, 1.0, aEndValue,
                       normValue2)) {
        return PR_FALSE;
      }

      const nsCSSValueList *shadow1 = normValue1.GetCSSValueListValue();
      const nsCSSValueList *shadow2 = normValue2.GetCSSValueListValue();

      double squareDistance = 0.0;
      NS_ABORT_IF_FALSE(!shadow1 == !shadow2, "lists should be same length");
      while (shadow1) {
        nsCSSValue::Array *array1 = shadow1->mValue.GetArrayValue();
        nsCSSValue::Array *array2 = shadow2->mValue.GetArrayValue();
        for (size_t i = 0; i < 4; ++i) {
          NS_ABORT_IF_FALSE(array1->Item(i).GetUnit() == eCSSUnit_Pixel,
                            "unexpected unit");
          NS_ABORT_IF_FALSE(array2->Item(i).GetUnit() == eCSSUnit_Pixel,
                            "unexpected unit");
          double diff = array1->Item(i).GetFloatValue() -
                        array2->Item(i).GetFloatValue();
          squareDistance += diff * diff;
        }

        const nsCSSValue &color1 = array1->Item(4);
        const nsCSSValue &color2 = array2->Item(4);
#ifdef DEBUG
        {
          const nsCSSValue &inset1 = array1->Item(5);
          const nsCSSValue &inset2 = array2->Item(5);
          // There are only two possible states of the inset value:
          //  (1) GetUnit() == eCSSUnit_Null
          //  (2) GetUnit() == eCSSUnit_Enumerated &&
          //      GetIntValue() == NS_STYLE_BOX_SHADOW_INSET
          NS_ABORT_IF_FALSE(color1.GetUnit() == color2.GetUnit() &&
                            inset1 == inset2,
                            "AddWeighted should have failed");
        }
#endif

        if (color1.GetUnit() != eCSSUnit_Null) {
          nsStyleAnimation::Value color1Value
            (color1.GetColorValue(), nsStyleAnimation::Value::ColorConstructor);
          nsStyleAnimation::Value color2Value
            (color2.GetColorValue(), nsStyleAnimation::Value::ColorConstructor);
          double colorDistance;

        #ifdef DEBUG
          PRBool ok =
        #endif
            nsStyleAnimation::ComputeDistance(eCSSProperty_color,
                                              color1Value, color2Value,
                                              colorDistance);
          NS_ABORT_IF_FALSE(ok, "should not fail");
          squareDistance += colorDistance * colorDistance;
        }

        shadow1 = shadow1->mNext;
        shadow2 = shadow2->mNext;
        NS_ABORT_IF_FALSE(!shadow1 == !shadow2, "lists should be same length");
      }
      aDistance = sqrt(squareDistance);
      return PR_TRUE;
    }
    case eUnit_Transform: {
      const nsCSSValueList *list1 = aStartValue.GetCSSValueListValue();
      const nsCSSValueList *list2 = aEndValue.GetCSSValueListValue();

      nsStyleTransformMatrix matrix1, matrix2; // initialized to identity

      PRBool dummy;
      if (list1->mValue.GetUnit() != eCSSUnit_None) {
        matrix1 = nsStyleTransformMatrix::ReadTransforms(list1, nsnull,
                                                         nsnull, dummy);
      }
      if (list2->mValue.GetUnit() != eCSSUnit_None) {
        matrix2 = nsStyleTransformMatrix::ReadTransforms(list2, nsnull,
                                                         nsnull, dummy);
      }

      double diff;
      double squareDistance = 0.0;
      for (PRUint32 i = 0; i < 4; ++i) {
        diff = matrix1.GetMainMatrixEntry(i) - matrix2.GetMainMatrixEntry(i);
        squareDistance += diff * diff;
      }
      diff = nsPresContext::AppUnitsToFloatCSSPixels(
        matrix1.GetCoordXTranslation() - matrix2.GetCoordXTranslation());
      squareDistance += diff * diff;
      diff = nsPresContext::AppUnitsToFloatCSSPixels(
        matrix1.GetCoordYTranslation() - matrix2.GetCoordYTranslation());
      squareDistance += diff * diff;
      diff = matrix1.GetWidthRelativeXTranslation() -
             matrix2.GetWidthRelativeXTranslation();
      squareDistance += diff * diff;
      diff = matrix1.GetWidthRelativeYTranslation() -
             matrix2.GetWidthRelativeYTranslation();
      squareDistance += diff * diff;
      diff = matrix1.GetHeightRelativeXTranslation() -
             matrix2.GetHeightRelativeXTranslation();
      squareDistance += diff * diff;
      diff = matrix1.GetHeightRelativeYTranslation() -
             matrix2.GetHeightRelativeYTranslation();
      squareDistance += diff * diff;

      aDistance = sqrt(squareDistance);
      return PR_TRUE;
    }
    case eUnit_CSSValuePairList: {
      const nsCSSValuePairList *list1 = aStartValue.GetCSSValuePairListValue();
      const nsCSSValuePairList *list2 = aEndValue.GetCSSValuePairListValue();
      double squareDistance = 0.0;
      do {
        static nsCSSValue nsCSSValuePairList::* const pairListValues[] = {
          &nsCSSValuePairList::mXValue,
          &nsCSSValuePairList::mYValue,
        };
        for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(pairListValues); ++i) {
          const nsCSSValue &v1 = list1->*(pairListValues[i]);
          const nsCSSValue &v2 = list2->*(pairListValues[i]);
          if (v1.GetUnit() != v2.GetUnit()) {
            return PR_FALSE;
          }
          double diff = 0.0;
          switch (v1.GetUnit()) {
            case eCSSUnit_Pixel:
              diff = v1.GetFloatValue() - v2.GetFloatValue();
              break;
            case eCSSUnit_Percent:
              diff = v1.GetPercentValue() - v2.GetPercentValue();
              break;
            default:
              if (v1 != v2) {
                return PR_FALSE;
              }
              break;
          }
          squareDistance += diff * diff;
        }
        list1 = list1->mNext;
        list2 = list2->mNext;
      } while (list1 && list2);
      if (list1 || list2) {
        // We can't interpolate lists of different lengths.
        return PR_FALSE;
      }
      aDistance = sqrt(squareDistance);
      return PR_TRUE;
    }
  }

  NS_ABORT_IF_FALSE(false, "Can't compute distance using the given common unit");
  return PR_FALSE;
}

inline void
nscoordToCSSValue(nscoord aCoord, nsCSSValue& aCSSValue)
{
  aCSSValue.SetFloatValue(nsPresContext::AppUnitsToFloatCSSPixels(aCoord),
                          eCSSUnit_Pixel);
}

#define MAX_PACKED_COLOR_COMPONENT 255

inline PRUint8 ClampColor(double aColor)
{
  if (aColor >= MAX_PACKED_COLOR_COMPONENT)
    return MAX_PACKED_COLOR_COMPONENT;
  if (aColor <= 0.0)
    return 0;
  return NSToIntRound(aColor);
}

static inline void
AddCSSValuePixel(double aCoeff1, const nsCSSValue &aValue1,
                 double aCoeff2, const nsCSSValue &aValue2,
                 nsCSSValue &aResult)
{
  NS_ABORT_IF_FALSE(aValue1.GetUnit() == eCSSUnit_Pixel, "unexpected unit");
  NS_ABORT_IF_FALSE(aValue2.GetUnit() == eCSSUnit_Pixel, "unexpected unit");
  aResult.SetFloatValue(aCoeff1 * aValue1.GetFloatValue() +
                        aCoeff2 * aValue2.GetFloatValue(),
                        eCSSUnit_Pixel);
}

static inline void
AddCSSValueNumber(double aCoeff1, const nsCSSValue &aValue1,
                  double aCoeff2, const nsCSSValue &aValue2,
                  nsCSSValue &aResult)
{
  NS_ABORT_IF_FALSE(aValue1.GetUnit() == eCSSUnit_Number, "unexpected unit");
  NS_ABORT_IF_FALSE(aValue2.GetUnit() == eCSSUnit_Number, "unexpected unit");
  aResult.SetFloatValue(aCoeff1 * aValue1.GetFloatValue() +
                        aCoeff2 * aValue2.GetFloatValue(),
                        eCSSUnit_Number);
}

static inline void
AddCSSValuePercent(double aCoeff1, const nsCSSValue &aValue1,
                   double aCoeff2, const nsCSSValue &aValue2,
                   nsCSSValue &aResult)
{
  NS_ABORT_IF_FALSE(aValue1.GetUnit() == eCSSUnit_Percent, "unexpected unit");
  NS_ABORT_IF_FALSE(aValue2.GetUnit() == eCSSUnit_Percent, "unexpected unit");
  aResult.SetPercentValue(aCoeff1 * aValue1.GetPercentValue() +
                          aCoeff2 * aValue2.GetPercentValue());
}

static void
AddCSSValueCalc(double aCoeff1, const nsCSSValue &aValue1,
                double aCoeff2, const nsCSSValue &aValue2,
                nsCSSValue &aResult)
{
  NS_ABORT_IF_FALSE(aValue1.GetUnit() == eCSSUnit_Percent ||
                    aValue1.GetUnit() == eCSSUnit_Pixel ||
                    aValue1.IsCalcUnit(), "unexpected unit");
  NS_ABORT_IF_FALSE(aValue2.GetUnit() == eCSSUnit_Percent ||
                    aValue2.GetUnit() == eCSSUnit_Pixel ||
                    aValue2.IsCalcUnit(), "unexpected unit");
  nsRefPtr<nsCSSValue::Array> a1 = nsCSSValue::Array::Create(2),
                              a2 = nsCSSValue::Array::Create(2),
                              atop = nsCSSValue::Array::Create(2),
                              acalc = nsCSSValue::Array::Create(1);
  // Don't nest the eCSSUnit_Calc in our input inside any expressions.
  a1->Item(0).SetFloatValue(aCoeff1, eCSSUnit_Number);
  a1->Item(1) = aValue1.GetUnit() == eCSSUnit_Calc
                  ? aValue1.GetArrayValue()->Item(0) : aValue1;
  a2->Item(0).SetFloatValue(aCoeff2, eCSSUnit_Number);
  a2->Item(1) = aValue2.GetUnit() == eCSSUnit_Calc
                  ? aValue2.GetArrayValue()->Item(0) : aValue2;
  atop->Item(0).SetArrayValue(a1, eCSSUnit_Calc_Times_L);
  atop->Item(1).SetArrayValue(a2, eCSSUnit_Calc_Times_L);
  acalc->Item(0).SetArrayValue(atop, eCSSUnit_Calc_Plus);
  aResult.SetArrayValue(acalc, eCSSUnit_Calc);
}

static void
AddCSSValueAngle(const nsCSSValue &aValue1, double aCoeff1,
                 const nsCSSValue &aValue2, double aCoeff2,
                 nsCSSValue &aResult)
{
  aResult.SetFloatValue(aCoeff1 * aValue1.GetAngleValueInRadians() +
                        aCoeff2 * aValue2.GetAngleValueInRadians(),
                        eCSSUnit_Radian);
}

static PRBool
AddShadowItems(double aCoeff1, const nsCSSValue &aValue1,
               double aCoeff2, const nsCSSValue &aValue2,
               nsCSSValueList **&aResultTail)
{
  // X, Y, Radius, Spread, Color, Inset
  NS_ABORT_IF_FALSE(aValue1.GetUnit() == eCSSUnit_Array,
                    "wrong unit");
  NS_ABORT_IF_FALSE(aValue2.GetUnit() == eCSSUnit_Array,
                    "wrong unit");
  nsCSSValue::Array *array1 = aValue1.GetArrayValue();
  nsCSSValue::Array *array2 = aValue2.GetArrayValue();
  nsRefPtr<nsCSSValue::Array> resultArray = nsCSSValue::Array::Create(6);
  if (!resultArray) {
    return PR_FALSE;
  }

  for (size_t i = 0; i < 4; ++i) {
    AddCSSValuePixel(aCoeff1, array1->Item(i), aCoeff2, array2->Item(i),
                     resultArray->Item(i));
  }

  const nsCSSValue& color1 = array1->Item(4);
  const nsCSSValue& color2 = array2->Item(4);
  const nsCSSValue& inset1 = array1->Item(5);
  const nsCSSValue& inset2 = array2->Item(5);
  if (color1.GetUnit() != color2.GetUnit() ||
      inset1.GetUnit() != inset2.GetUnit()) {
    // We don't know how to animate between color and no-color, or
    // between inset and not-inset.
    return PR_FALSE;
  }

  if (color1.GetUnit() != eCSSUnit_Null) {
    nsStyleAnimation::Value color1Value
      (color1.GetColorValue(), nsStyleAnimation::Value::ColorConstructor);
    nsStyleAnimation::Value color2Value
      (color2.GetColorValue(), nsStyleAnimation::Value::ColorConstructor);
    nsStyleAnimation::Value resultColorValue;
  #ifdef DEBUG
    PRBool ok =
  #endif
      nsStyleAnimation::AddWeighted(eCSSProperty_color, aCoeff1, color1Value,
                                    aCoeff2, color2Value, resultColorValue);
    NS_ABORT_IF_FALSE(ok, "should not fail");
    resultArray->Item(4).SetColorValue(resultColorValue.GetColorValue());
  }

  NS_ABORT_IF_FALSE(inset1 == inset2, "should match");
  resultArray->Item(5) = inset1;

  nsCSSValueList *resultItem = new nsCSSValueList;
  if (!resultItem) {
    return PR_FALSE;
  }
  resultItem->mValue.SetArrayValue(resultArray, eCSSUnit_Array);
  *aResultTail = resultItem;
  aResultTail = &resultItem->mNext;
  return PR_TRUE;
}

static void
AddTransformTranslate(const nsCSSValue &aValue1, double aCoeff1,
                      const nsCSSValue &aValue2, double aCoeff2,
                      nsCSSValue &aResult)
{
  NS_ABORT_IF_FALSE(aValue1.GetUnit() == eCSSUnit_Percent ||
                    aValue1.GetUnit() == eCSSUnit_Pixel ||
                    aValue1.IsCalcUnit(),
                    "unexpected unit");
  NS_ABORT_IF_FALSE(aValue2.GetUnit() == eCSSUnit_Percent ||
                    aValue2.GetUnit() == eCSSUnit_Pixel ||
                    aValue2.IsCalcUnit(),
                    "unexpected unit");

  if (aValue1.GetUnit() != aValue2.GetUnit() || aValue1.IsCalcUnit()) {
    // different units; create a calc() expression
    AddCSSValueCalc(aCoeff1, aValue1, aCoeff2, aValue2, aResult);
  } else if (aValue1.GetUnit() == eCSSUnit_Percent) {
    // both percent
    AddCSSValuePercent(aCoeff1, aValue1, aCoeff2, aValue2, aResult);
  } else {
    // both pixels
    AddCSSValuePixel(aCoeff1, aValue1, aCoeff2, aValue2, aResult);
  }
}

static void
AddTransformScale(const nsCSSValue &aValue1, double aCoeff1,
                  const nsCSSValue &aValue2, double aCoeff2,
                  nsCSSValue &aResult)
{
  // Handle scale, and the two matrix components where identity is 1, by
  // subtracting 1, multiplying by the coefficients, and then adding 1
  // back.  This gets the right AddWeighted behavior and gets us the
  // interpolation-against-identity behavior for free.
  NS_ABORT_IF_FALSE(aValue1.GetUnit() == eCSSUnit_Number, "unexpected unit");
  NS_ABORT_IF_FALSE(aValue2.GetUnit() == eCSSUnit_Number, "unexpected unit");

  float v1 = aValue1.GetFloatValue() - 1.0f,
        v2 = aValue2.GetFloatValue() - 1.0f;
  float result = v1 * aCoeff1 + v2 * aCoeff2;
  aResult.SetFloatValue(result + 1.0f, eCSSUnit_Number);
}

// FIXME: The spec still says skew should animate in angle space,
// although I think we at least sort of agreed that it should animate
// in tangent space.  So here I animate in in tangent space.
// Animating in angle space would mean just using AddCSSValueAngle.
static void
AddTransformSkew(const nsCSSValue &aValue1, double aCoeff1,
                 const nsCSSValue &aValue2, double aCoeff2,
                 nsCSSValue &aResult)
{
  aResult.SetFloatValue(atan(aCoeff1 * tan(aValue1.GetAngleValueInRadians()) +
                             aCoeff2 * tan(aValue2.GetAngleValueInRadians())),
                        eCSSUnit_Radian);
}


static already_AddRefed<nsCSSValue::Array>
AppendTransformFunction(nsCSSKeyword aTransformFunction,
                        nsCSSValueList**& aListTail)
{
  PRUint32 nargs;
  if (aTransformFunction == eCSSKeyword_matrix) {
    nargs = 6;
  } else if (aTransformFunction == eCSSKeyword_translate ||
             aTransformFunction == eCSSKeyword_skew ||
             aTransformFunction == eCSSKeyword_scale) {
    nargs = 2;
  } else {
    NS_ABORT_IF_FALSE(aTransformFunction == eCSSKeyword_translatex ||
                      aTransformFunction == eCSSKeyword_translatey ||
                      aTransformFunction == eCSSKeyword_scalex ||
                      aTransformFunction == eCSSKeyword_scaley ||
                      aTransformFunction == eCSSKeyword_skewx ||
                      aTransformFunction == eCSSKeyword_skewy ||
                      aTransformFunction == eCSSKeyword_rotate,
                      "must be a transform function");
    nargs = 1;
  }

  nsRefPtr<nsCSSValue::Array> arr = nsCSSValue::Array::Create(nargs + 1);
  arr->Item(0).SetStringValue(
    NS_ConvertUTF8toUTF16(nsCSSKeywords::GetStringValue(aTransformFunction)),
    eCSSUnit_Ident);

  nsCSSValueList *item = new nsCSSValueList;
  item->mValue.SetArrayValue(arr, eCSSUnit_Function);

  *aListTail = item;
  aListTail = &item->mNext;

  return arr.forget();
}

/*
 * The relevant section of the transitions specification:
 * http://dev.w3.org/csswg/css3-transitions/#animation-of-property-types-
 * defers all of the details to the 2-D and 3-D transforms specifications.
 * For the 2-D transforms specification (all that's relevant for us, right
 * now), the relevant section is:
 * http://dev.w3.org/csswg/css3-2d-transforms/#animation
 * This, in turn, refers to the unmatrix program in Graphics Gems,
 * available from http://tog.acm.org/resources/GraphicsGems/ , and in
 * particular as the file GraphicsGems/gemsii/unmatrix.c
 * in http://tog.acm.org/resources/GraphicsGems/AllGems.tar.gz
 *
 * The unmatrix reference is for general 3-D transform matrices (any of the
 * 16 components can have any value).
 *
 * For CSS 2-D transforms, we have a 2-D matrix with the bottom row constant:
 *
 * [ A C E ]
 * [ B D F ]
 * [ 0 0 1 ]
 *
 * For that case, I believe the algorithm in unmatrix reduces to:
 *
 *  (1) If A * D - B * C == 0, the matrix is singular.  Fail.
 *
 *  (2) Set translation components (Tx and Ty) to the translation parts of
 *      the matrix (E and F) and then ignore them for the rest of the time.
 *      (For us, E and F each actually consist of three constants:  a
 *      length, a multiplier for the width, and a multiplier for the
 *      height.  This actually requires its own decomposition, but I'll
 *      keep that separate.)
 *
 *  (3) Let the X scale (Sx) be sqrt(A^2 + B^2).  Then divide both A and B
 *      by it.
 *
 *  (4) Let the XY shear (K) be A * C + B * D.  From C, subtract A times
 *      the XY shear.  From D, subtract B times the XY shear.
 *
 *  (5) Let the Y scale (Sy) be sqrt(C^2 + D^2).  Divide C, D, and the XY
 *      shear (K) by it.
 *
 *  (6) At this point, A * D - B * C is either 1 or -1.  If it is -1,
 *      negate the XY shear (K), the X scale (Sx), and A, B, C, and D.
 *      (Alternatively, we could negate the XY shear (K) and the Y scale
 *      (Sy).)
 *
 *  (7) Let the rotation be R = atan2(B, A).
 *
 * Then the resulting decomposed transformation is:
 *
 *   translate(Tx, Ty) rotate(R) skewX(atan(K)) scale(Sx, Sy)
 *
 * An interesting result of this is that all of the simple transform
 * functions (i.e., all functions other than matrix()), in isolation,
 * decompose back to themselves except for:
 *   'skewY(φ)', which is 'matrix(1, tan(φ), 0, 1, 0, 0)', which decomposes
 *   to 'rotate(φ) skewX(φ) scale(sec(φ), cos(φ))' since (ignoring the
 *   alternate sign possibilities that would get fixed in step 6):
 *     In step 3, the X scale factor is sqrt(1+tan²(φ)) = sqrt(sec²(φ)) = sec(φ).
 *     Thus, after step 3, A = 1/sec(φ) = cos(φ) and B = tan(φ) / sec(φ) = sin(φ).
 *     In step 4, the XY shear is sin(φ).
 *     Thus, after step 4, C = -cos(φ)sin(φ) and D = 1 - sin²(φ) = cos²(φ).
 *     Thus, in step 5, the Y scale is sqrt(cos²(φ)(sin²(φ) + cos²(φ)) = cos(φ).
 *     Thus, after step 5, C = -sin(φ), D = cos(φ), and the XY shear is tan(φ).
 *     Thus, in step 6, A * D - B * C = cos²(φ) + sin²(φ) = 1.
 *     In step 7, the rotation is thus φ.
 *
 *   skew(θ, φ), which is matrix(1, tan(φ), tan(θ), 1, 0, 0), which decomposes
 *   to 'rotate(φ) skewX(θ + φ) scale(sec(φ), cos(φ))' since (ignoring
 *   the alternate sign possibilities that would get fixed in step 6):
 *     In step 3, the X scale factor is sqrt(1+tan²(φ)) = sqrt(sec²(φ)) = sec(φ).
 *     Thus, after step 3, A = 1/sec(φ) = cos(φ) and B = tan(φ) / sec(φ) = sin(φ).
 *     In step 4, the XY shear is cos(φ)tan(θ) + sin(φ).
 *     Thus, after step 4,
 *     C = tan(θ) - cos(φ)(cos(φ)tan(θ) + sin(φ)) = tan(θ)sin²(φ) - cos(φ)sin(φ)
 *     D = 1 - sin(φ)(cos(φ)tan(θ) + sin(φ)) = cos²(φ) - sin(φ)cos(φ)tan(θ)
 *     Thus, in step 5, the Y scale is sqrt(C² + D²) =
 *     sqrt(tan²(θ)(sin⁴(φ) + sin²(φ)cos²(φ)) -
 *          2 tan(θ)(sin³(φ)cos(φ) + sin(φ)cos³(φ)) +
 *          (sin²(φ)cos²(φ) + cos⁴(φ))) =
 *     sqrt(tan²(θ)sin²(φ) - 2 tan(θ)sin(φ)cos(φ) + cos²(φ)) =
 *     cos(φ) - tan(θ)sin(φ) (taking the negative of the obvious solution so
 *     we avoid flipping in step 6).
 *     After step 5, C = -sin(φ) and D = cos(φ), and the XY shear is
 *     (cos(φ)tan(θ) + sin(φ)) / (cos(φ) - tan(θ)sin(φ)) =
 *     (dividing both numerator and denominator by cos(φ))
 *     (tan(θ) + tan(φ)) / (1 - tan(θ)tan(φ)) = tan(θ + φ).
 *     (See http://en.wikipedia.org/wiki/List_of_trigonometric_identities .)
 *     Thus, in step 6, A * D - B * C = cos²(φ) + sin²(φ) = 1.
 *     In step 7, the rotation is thus φ.
 *
 *     To check this result, we can multiply things back together:
 *
 *     [ cos(φ) -sin(φ) ] [ 1 tan(θ + φ) ] [ sec(φ)    0   ]
 *     [ sin(φ)  cos(φ) ] [ 0      1     ] [   0    cos(φ) ]
 *
 *     [ cos(φ)      cos(φ)tan(θ + φ) - sin(φ) ] [ sec(φ)    0   ]
 *     [ sin(φ)      sin(φ)tan(θ + φ) + cos(φ) ] [   0    cos(φ) ]
 *
 *     but since tan(θ + φ) = (tan(θ) + tan(φ)) / (1 - tan(θ)tan(φ)),
 *     cos(φ)tan(θ + φ) - sin(φ)
 *      = cos(φ)(tan(θ) + tan(φ)) - sin(φ) + sin(φ)tan(θ)tan(φ)
 *      = cos(φ)tan(θ) + sin(φ) - sin(φ) + sin(φ)tan(θ)tan(φ)
 *      = cos(φ)tan(θ) + sin(φ)tan(θ)tan(φ)
 *      = tan(θ) (cos(φ) + sin(φ)tan(φ))
 *      = tan(θ) sec(φ) (cos²(φ) + sin²(φ))
 *      = tan(θ) sec(φ)
 *     and
 *     sin(φ)tan(θ + φ) + cos(φ)
 *      = sin(φ)(tan(θ) + tan(φ)) + cos(φ) - cos(φ)tan(θ)tan(φ)
 *      = tan(θ) (sin(φ) - sin(φ)) + sin(φ)tan(φ) + cos(φ)
 *      = sec(φ) (sin²(φ) + cos²(φ))
 *      = sec(φ)
 *     so the above is:
 *     [ cos(φ)  tan(θ) sec(φ) ] [ sec(φ)    0   ]
 *     [ sin(φ)     sec(φ)     ] [   0    cos(φ) ]
 *
 *     [    1   tan(θ) ]
 *     [ tan(φ)    1   ]
 */

/*
 * DecomposeMatrix implements the non-translation parts of the above
 * decomposition algorithm.
 */
static PRBool
DecomposeMatrix(const nsStyleTransformMatrix &aMatrix,
                float &aRotate, float &aXYShear, float &aScaleX, float &aScaleY)
{
  float A = aMatrix.GetMainMatrixEntry(0),
        B = aMatrix.GetMainMatrixEntry(1),
        C = aMatrix.GetMainMatrixEntry(2),
        D = aMatrix.GetMainMatrixEntry(3);
  if (A * D == B * C) {
    // singular matrix
    return PR_FALSE;
  }

  float scaleX = sqrt(A * A + B * B);
  A /= scaleX;
  B /= scaleX;

  float XYshear = A * C + B * D;
  C -= A * XYshear;
  D -= B * XYshear;

  float scaleY = sqrt(C * C + D * D);
  C /= scaleY;
  D /= scaleY;
  XYshear /= scaleY;

 // A*D - B*C should now be 1 or -1
  NS_ASSERTION(0.99 < PR_ABS(A*D - B*C) && PR_ABS(A*D - B*C) < 1.01,
               "determinant should now be 1 or -1");
  if (A * D < B * C) {
    A = -A;
    B = -B;
    C = -C;
    D = -D;
    XYshear = -XYshear;
    scaleX = -scaleX;
  }

  float rotation = atan2f(B, A);

  aRotate = rotation;
  aXYShear = XYshear;
  aScaleX = scaleX;
  aScaleY = scaleY;

  return PR_TRUE;
}

static nsCSSValueList*
AddTransformMatrix(const nsStyleTransformMatrix &aMatrix1, double aCoeff1,
                   const nsStyleTransformMatrix &aMatrix2, double aCoeff2)
{

  nsAutoPtr<nsCSSValueList> result;
  nsCSSValueList **resultTail = getter_Transfers(result);
  nsRefPtr<nsCSSValue::Array> arr;

  // The translation part of the matrix comes first in our result list,
  // but it's complicated by the mix of %s, possibly in between rotates.

  // append a rotate(90deg)
  arr = AppendTransformFunction(eCSSKeyword_rotate, resultTail);
  arr->Item(1).SetFloatValue(90.0f, eCSSUnit_Degree);

  // append the translation for parts of the % translation components
  // that were from inside a rotation
  float rtranslateXPercent =
    aMatrix1.GetWidthRelativeYTranslation() * aCoeff1 +
    aMatrix2.GetWidthRelativeYTranslation() * aCoeff2;
  float rtranslateYPercent =
    - (aMatrix1.GetHeightRelativeXTranslation() * aCoeff1 +
       aMatrix2.GetHeightRelativeXTranslation() * aCoeff2);
  arr = AppendTransformFunction(eCSSKeyword_translate, resultTail);
  arr->Item(1).SetPercentValue(rtranslateXPercent);
  arr->Item(2).SetPercentValue(rtranslateYPercent);

  // append a rotate(-90deg)
  arr = AppendTransformFunction(eCSSKeyword_rotate, resultTail);
  arr->Item(1).SetFloatValue(-90.0f, eCSSUnit_Degree);

  nscoord translateXCoord = NSToCoordRound(
                              aMatrix1.GetCoordXTranslation() * aCoeff1 +
                              aMatrix2.GetCoordXTranslation() * aCoeff2);
  nscoord translateYCoord = NSToCoordRound(
                              aMatrix1.GetCoordYTranslation() * aCoeff1 +
                              aMatrix2.GetCoordYTranslation() * aCoeff2);
  float translateXPercent = aMatrix1.GetWidthRelativeXTranslation() * aCoeff1 +
                            aMatrix2.GetWidthRelativeXTranslation() * aCoeff2;
  float translateYPercent = aMatrix1.GetHeightRelativeYTranslation() * aCoeff1 +
                            aMatrix2.GetHeightRelativeYTranslation() * aCoeff2;

  float rotate1, XYshear1, scaleX1, scaleY1;
  DecomposeMatrix(aMatrix1, rotate1, XYshear1, scaleX1, scaleY1);
  float rotate2, XYshear2, scaleX2, scaleY2;
  DecomposeMatrix(aMatrix2, rotate2, XYshear2, scaleX2, scaleY2);

  float rotate = rotate1 * aCoeff1 + rotate2 * aCoeff2;

  // FIXME: The spec still says skew should animate in angle space,
  // although I think we at least sort of agreed that it should animate
  // in tangent space.  So here I animate in in tangent space.
  float skewX = atanf(XYshear1 * aCoeff1 + XYshear2 * aCoeff2);

  // Handle scale, and the two matrix components where identity is 1, by
  // subtracting 1, multiplying by the coefficients, and then adding 1
  // back.  This gets the right AddWeighted behavior and gets us the
  // interpolation-against-identity behavior for free.
  float scaleX =
    ((scaleX1 - 1.0f) * aCoeff1 + (scaleX2 - 1.0f) * aCoeff2) + 1.0f;
  float scaleY =
    ((scaleY1 - 1.0f) * aCoeff1 + (scaleY2 - 1.0f) * aCoeff2) + 1.0f;

  // It's simpler to append an additional function for the percentage
  // translate parts than to build a calc() expression.
  arr = AppendTransformFunction(eCSSKeyword_translate, resultTail);
  arr->Item(1).SetPercentValue(translateXPercent);
  arr->Item(2).SetPercentValue(translateYPercent);

  arr = AppendTransformFunction(eCSSKeyword_translate, resultTail);
  arr->Item(1).SetFloatValue(
    nsPresContext::AppUnitsToFloatCSSPixels(translateXCoord), eCSSUnit_Pixel);
  arr->Item(2).SetFloatValue(
    nsPresContext::AppUnitsToFloatCSSPixels(translateYCoord), eCSSUnit_Pixel);

  arr = AppendTransformFunction(eCSSKeyword_rotate, resultTail);
  arr->Item(1).SetFloatValue(rotate, eCSSUnit_Radian);

  arr = AppendTransformFunction(eCSSKeyword_skewx, resultTail);
  arr->Item(1).SetFloatValue(skewX, eCSSUnit_Radian);

  arr = AppendTransformFunction(eCSSKeyword_scale, resultTail);
  arr->Item(1).SetFloatValue(scaleX, eCSSUnit_Number);
  arr->Item(2).SetFloatValue(scaleY, eCSSUnit_Number);

  return result.forget();
}

static nsCSSValueList*
AddTransformLists(const nsCSSValueList* aList1, double aCoeff1,
                  const nsCSSValueList* aList2, double aCoeff2)
{
  nsAutoPtr<nsCSSValueList> result;
  nsCSSValueList **resultTail = getter_Transfers(result);

  do {
    const nsCSSValue::Array *a1 = aList1->mValue.GetArrayValue(),
                            *a2 = aList2->mValue.GetArrayValue();
    NS_ABORT_IF_FALSE(nsStyleTransformMatrix::TransformFunctionOf(a1) ==
                      nsStyleTransformMatrix::TransformFunctionOf(a2),
                      "transform function mismatch");

    nsCSSKeyword tfunc = nsStyleTransformMatrix::TransformFunctionOf(a1);
    nsRefPtr<nsCSSValue::Array> arr;
    if (tfunc != eCSSKeyword_matrix) {
      arr = AppendTransformFunction(tfunc, resultTail);
    }

    switch (tfunc) {
      case eCSSKeyword_translate: {
        NS_ABORT_IF_FALSE(a1->Count() == 2 || a1->Count() == 3,
                          "unexpected count");
        NS_ABORT_IF_FALSE(a2->Count() == 2 || a2->Count() == 3,
                          "unexpected count");

        // FIXME: This would produce fewer calc() expressions if the
        // zero were of compatible type (length vs. percent) when
        // needed.
        nsCSSValue zero(0.0f, eCSSUnit_Pixel);
        // Add Y component of translation.
        AddTransformTranslate(a1->Count() == 3 ? a1->Item(2) : zero,
                              aCoeff1,
                              a2->Count() == 3 ? a2->Item(2) : zero,
                              aCoeff2,
                              arr->Item(2));

        // Add X component of translation (which can be merged with case
        // below in non-DEBUG).
        AddTransformTranslate(a1->Item(1), aCoeff1, a2->Item(1), aCoeff2,
                              arr->Item(1));
        break;
      }
      case eCSSKeyword_translatex:
      case eCSSKeyword_translatey: {
        NS_ABORT_IF_FALSE(a1->Count() == 2, "unexpected count");
        NS_ABORT_IF_FALSE(a2->Count() == 2, "unexpected count");
        AddTransformTranslate(a1->Item(1), aCoeff1, a2->Item(1), aCoeff2,
                              arr->Item(1));
        break;
      }
      case eCSSKeyword_scale: {
        NS_ABORT_IF_FALSE(a1->Count() == 2 || a1->Count() == 3,
                          "unexpected count");
        NS_ABORT_IF_FALSE(a2->Count() == 2 || a2->Count() == 3,
                          "unexpected count");

        // This is different from skew() and translate(), since an
        // omitted second parameter repeats the first rather than being
        // zero.
        // Add Y component of scale.
        AddTransformScale(a1->Count() == 3 ? a1->Item(2) : a1->Item(1),
                          aCoeff1,
                          a2->Count() == 3 ? a2->Item(2) : a2->Item(1),
                          aCoeff2,
                          arr->Item(2));

        // Add X component of scale (which can be merged with case below
        // in non-DEBUG).
        AddTransformScale(a1->Item(1), aCoeff1, a2->Item(1), aCoeff2,
                          arr->Item(1));

        break;
      }
      case eCSSKeyword_scalex:
      case eCSSKeyword_scaley: {
        NS_ABORT_IF_FALSE(a1->Count() == 2, "unexpected count");
        NS_ABORT_IF_FALSE(a2->Count() == 2, "unexpected count");

        AddTransformScale(a1->Item(1), aCoeff1, a2->Item(1), aCoeff2,
                          arr->Item(1));

        break;
      }
      case eCSSKeyword_skew: {
        NS_ABORT_IF_FALSE(a1->Count() == 2 || a1->Count() == 3,
                          "unexpected count");
        NS_ABORT_IF_FALSE(a2->Count() == 2 || a2->Count() == 3,
                          "unexpected count");

        nsCSSValue zero(0.0f, eCSSUnit_Radian);
        // Add Y component of skew.
        AddTransformSkew(a1->Count() == 3 ? a1->Item(2) : zero,
                         aCoeff1,
                         a2->Count() == 3 ? a2->Item(2) : zero,
                         aCoeff2,
                         arr->Item(2));

        // Add X component of skew (which can be merged with case below
        // in non-DEBUG).
        AddTransformSkew(a1->Item(1), aCoeff1, a2->Item(1), aCoeff2,
                         arr->Item(1));

        break;
      }
      case eCSSKeyword_skewx:
      case eCSSKeyword_skewy: {
        NS_ABORT_IF_FALSE(a1->Count() == 2, "unexpected count");
        NS_ABORT_IF_FALSE(a2->Count() == 2, "unexpected count");

        AddTransformSkew(a1->Item(1), aCoeff1, a2->Item(1), aCoeff2,
                         arr->Item(1));

        break;
      }
      case eCSSKeyword_rotate: {
        NS_ABORT_IF_FALSE(a1->Count() == 2, "unexpected count");
        NS_ABORT_IF_FALSE(a2->Count() == 2, "unexpected count");

        AddCSSValueAngle(a1->Item(1), aCoeff1, a2->Item(1), aCoeff2,
                         arr->Item(1));

        break;
      }
      case eCSSKeyword_matrix: {
        NS_ABORT_IF_FALSE(a1->Count() == 7, "unexpected count");
        NS_ABORT_IF_FALSE(a2->Count() == 7, "unexpected count");

        PRBool dummy;
        nsStyleTransformMatrix matrix1, matrix2;
        matrix1.SetToTransformFunction(a1, nsnull, nsnull, dummy);
        matrix2.SetToTransformFunction(a2, nsnull, nsnull, dummy);

        *resultTail =
          AddTransformMatrix(matrix1, aCoeff1, matrix2, aCoeff2);

        while ((*resultTail)->mNext) {
          resultTail = &(*resultTail)->mNext;
        }

        break;
      }
      default:
        NS_ABORT_IF_FALSE(PR_FALSE, "unknown transform function");
    }

    aList1 = aList1->mNext;
    aList2 = aList2->mNext;
  } while (aList1);
  NS_ABORT_IF_FALSE(!aList2, "list length mismatch");

  return result.forget();
}

PRBool
nsStyleAnimation::AddWeighted(nsCSSProperty aProperty,
                              double aCoeff1, const Value& aValue1,
                              double aCoeff2, const Value& aValue2,
                              Value& aResultValue)
{
  Unit commonUnit = GetCommonUnit(aValue1.GetUnit(), aValue2.GetUnit());
  // Maybe need a followup method to convert the inputs into the common
  // unit-type, if they don't already match it. (Or would it make sense to do
  // that in GetCommonUnit? in which case maybe ConvertToCommonUnit would be
  // better.)

  switch (commonUnit) {
    case eUnit_Null:
    case eUnit_Auto:
    case eUnit_None:
    case eUnit_Normal:
    case eUnit_UnparsedString:
      return PR_FALSE;

    case eUnit_Enumerated:
      switch (aProperty) {
        case eCSSProperty_font_stretch: {
          // Animate just like eUnit_Integer.
          PRInt32 result = NS_floor(aCoeff1 * double(aValue1.GetIntValue()) +
                                    aCoeff2 * double(aValue2.GetIntValue()));
          aResultValue.SetIntValue(result, eUnit_Enumerated);
          return PR_TRUE;
        }
        default:
          return PR_FALSE;
      }
    case eUnit_Visibility: {
      PRInt32 val1 = aValue1.GetIntValue() == NS_STYLE_VISIBILITY_VISIBLE;
      PRInt32 val2 = aValue2.GetIntValue() == NS_STYLE_VISIBILITY_VISIBLE;
      double interp = aCoeff1 * val1 + aCoeff2 * val2;
      PRInt32 result = interp > 0.0 ? NS_STYLE_VISIBILITY_VISIBLE
                                    : NS_STYLE_VISIBILITY_HIDDEN;
      aResultValue.SetIntValue(result, eUnit_Visibility);
      return PR_TRUE;
    }
    case eUnit_Integer: {
      // http://dev.w3.org/csswg/css3-transitions/#animation-of-property-types-
      // says we should use floor
      PRInt32 result = NS_floor(aCoeff1 * double(aValue1.GetIntValue()) +
                                aCoeff2 * double(aValue2.GetIntValue()));
      if (aProperty == eCSSProperty_font_weight) {
        NS_ASSERTION(result > 0, "unexpected value");
        result -= result % 100;
      }
      aResultValue.SetIntValue(result, eUnit_Integer);
      return PR_TRUE;
    }
    case eUnit_Coord: {
      aResultValue.SetCoordValue(NSToCoordRound(
        aCoeff1 * aValue1.GetCoordValue() +
        aCoeff2 * aValue2.GetCoordValue()));
      return PR_TRUE;
    }
    case eUnit_Percent: {
      aResultValue.SetPercentValue(
        aCoeff1 * aValue1.GetPercentValue() +
        aCoeff2 * aValue2.GetPercentValue());
      return PR_TRUE;
    }
    case eUnit_Float: {
      aResultValue.SetFloatValue(
        aCoeff1 * aValue1.GetFloatValue() +
        aCoeff2 * aValue2.GetFloatValue());
      return PR_TRUE;
    }
    case eUnit_Color: {
      nscolor color1 = aValue1.GetColorValue();
      nscolor color2 = aValue2.GetColorValue();
      // FIXME (spec): The CSS transitions spec doesn't say whether
      // colors are premultiplied, but things work better when they are,
      // so use premultiplication.  Spec issue is still open per
      // http://lists.w3.org/Archives/Public/www-style/2009Jul/0050.html

      // To save some math, scale the alpha down to a 0-1 scale, but
      // leave the color components on a 0-255 scale.
      double A1 = NS_GET_A(color1) * (1.0 / 255.0);
      double R1 = NS_GET_R(color1) * A1;
      double G1 = NS_GET_G(color1) * A1;
      double B1 = NS_GET_B(color1) * A1;
      double A2 = NS_GET_A(color2) * (1.0 / 255.0);
      double R2 = NS_GET_R(color2) * A2;
      double G2 = NS_GET_G(color2) * A2;
      double B2 = NS_GET_B(color2) * A2;
      double Aresf = (A1 * aCoeff1 + A2 * aCoeff2);
      nscolor resultColor;
      if (Aresf <= 0.0) {
        resultColor = NS_RGBA(0, 0, 0, 0);
      } else {
        if (Aresf > 1.0) {
          Aresf = 1.0;
        }
        double factor = 1.0 / Aresf;
        PRUint8 Ares = NSToIntRound(Aresf * 255.0);
        PRUint8 Rres = ClampColor((R1 * aCoeff1 + R2 * aCoeff2) * factor);
        PRUint8 Gres = ClampColor((G1 * aCoeff1 + G2 * aCoeff2) * factor);
        PRUint8 Bres = ClampColor((B1 * aCoeff1 + B2 * aCoeff2) * factor);
        resultColor = NS_RGBA(Rres, Gres, Bres, Ares);
      }
      aResultValue.SetColorValue(resultColor);
      return PR_TRUE;
    }
    case eUnit_CSSValuePair: {
      const nsCSSValuePair *pair1 = aValue1.GetCSSValuePairValue();
      const nsCSSValuePair *pair2 = aValue2.GetCSSValuePairValue();
      if (pair1->mXValue.GetUnit() != pair2->mXValue.GetUnit() ||
          pair1->mYValue.GetUnit() != pair2->mYValue.GetUnit()) {
        // At least until we have calc()
        return PR_FALSE;
      }

      nsAutoPtr<nsCSSValuePair> result(new nsCSSValuePair);
      static nsCSSValue nsCSSValuePair::* const pairValues[] = {
        &nsCSSValuePair::mXValue, &nsCSSValuePair::mYValue
      };
      for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(pairValues); ++i) {
        nsCSSValue nsCSSValuePair::*member = pairValues[i];
        NS_ABORT_IF_FALSE((pair1->*member).GetUnit() ==
                            (pair2->*member).GetUnit(),
                          "should have returned above");
        switch ((pair1->*member).GetUnit()) {
          case eCSSUnit_Pixel:
            AddCSSValuePixel(aCoeff1, pair1->*member, aCoeff2, pair2->*member,
                             result->*member);
            break;
          case eCSSUnit_Percent:
            AddCSSValuePercent(aCoeff1, pair1->*member,
                               aCoeff2, pair2->*member,
                               result->*member);
            break;
          default:
            NS_ABORT_IF_FALSE(PR_FALSE, "unexpected unit");
            return PR_FALSE;
        }
      }

      aResultValue.SetAndAdoptCSSValuePairValue(result.forget(),
                                                eUnit_CSSValuePair);
      return PR_TRUE;
    }
    case eUnit_CSSRect: {
      const nsCSSRect *rect1 = aValue1.GetCSSRectValue();
      const nsCSSRect *rect2 = aValue2.GetCSSRectValue();
      if (rect1->mTop.GetUnit() != rect2->mTop.GetUnit() ||
          rect1->mRight.GetUnit() != rect2->mRight.GetUnit() ||
          rect1->mBottom.GetUnit() != rect2->mBottom.GetUnit() ||
          rect1->mLeft.GetUnit() != rect2->mLeft.GetUnit()) {
        // At least until we have calc()
        return PR_FALSE;
      }

      nsAutoPtr<nsCSSRect> result(new nsCSSRect);
      for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(nsCSSRect::sides); ++i) {
        nsCSSValue nsCSSRect::*member = nsCSSRect::sides[i];
        NS_ABORT_IF_FALSE((rect1->*member).GetUnit() ==
                            (rect2->*member).GetUnit(),
                          "should have returned above");
        switch ((rect1->*member).GetUnit()) {
          case eCSSUnit_Pixel:
            AddCSSValuePixel(aCoeff1, rect1->*member, aCoeff2, rect2->*member,
                             result->*member);
            break;
          case eCSSUnit_Auto:
            if (float(aCoeff1 + aCoeff2) != 1.0f) {
              // Interpolating between two auto values makes sense;
              // adding in other ratios does not.
              return PR_FALSE;
            }
            (result->*member).SetAutoValue();
            break;
          default:
            NS_ABORT_IF_FALSE(PR_FALSE, "unexpected unit");
            return PR_FALSE;
        }
      }

      aResultValue.SetAndAdoptCSSRectValue(result.forget(), eUnit_CSSRect);
      return PR_TRUE;
    }
    case eUnit_Dasharray: {
      const nsCSSValueList *list1 = aValue1.GetCSSValueListValue();
      const nsCSSValueList *list2 = aValue2.GetCSSValueListValue();

      PRUint32 len1 = 0, len2 = 0;
      for (const nsCSSValueList *v = list1; v; v = v->mNext) {
        ++len1;
      }
      for (const nsCSSValueList *v = list2; v; v = v->mNext) {
        ++len2;
      }
      NS_ABORT_IF_FALSE(len1 > 0 && len2 > 0, "unexpected length");
      if (list1->mValue.GetUnit() == eCSSUnit_None ||
          list2->mValue.GetUnit() == eCSSUnit_None) {
        // One of our values is "none".  Can't do addition with that.
        NS_ABORT_IF_FALSE(
          (list1->mValue.GetUnit() != eCSSUnit_None || len1 == 1) &&
          (list2->mValue.GetUnit() != eCSSUnit_None || len2 == 1),
          "multi-value valuelist with 'none' as first element");
        return PR_FALSE;
      }

      nsAutoPtr<nsCSSValueList> result;
      nsCSSValueList **resultTail = getter_Transfers(result);
      for (PRUint32 i = 0, i_end = lcm(len1, len2); i != i_end; ++i) {
        const nsCSSValue &v1 = list1->mValue;
        const nsCSSValue &v2 = list2->mValue;
        NS_ABORT_IF_FALSE(v1.GetUnit() == eCSSUnit_Number ||
                          v1.GetUnit() == eCSSUnit_Percent, "unexpected");
        NS_ABORT_IF_FALSE(v2.GetUnit() == eCSSUnit_Number ||
                          v2.GetUnit() == eCSSUnit_Percent, "unexpected");
        if (v1.GetUnit() != v2.GetUnit()) {
          // Can't animate between lengths and percentages (until calc()).
          return PR_FALSE;
        }

        nsCSSValueList *item = new nsCSSValueList;
        if (!item) {
          return PR_FALSE;
        }
        *resultTail = item;
        resultTail = &item->mNext;

        if (v1.GetUnit() == eCSSUnit_Number) {
          AddCSSValueNumber(aCoeff1, v1, aCoeff2, v2, item->mValue);
        } else {
          AddCSSValuePercent(aCoeff1, v1, aCoeff2, v2, item->mValue);
        }

        list1 = list1->mNext;
        if (!list1) {
          list1 = aValue1.GetCSSValueListValue();
        }
        list2 = list2->mNext;
        if (!list2) {
          list2 = aValue2.GetCSSValueListValue();
        }
      }

      aResultValue.SetAndAdoptCSSValueListValue(result.forget(),
                                                eUnit_Dasharray);
      return PR_TRUE;
    }
    case eUnit_Shadow: {
      // This is implemented according to:
      // http://dev.w3.org/csswg/css3-transitions/#animation-of-property-types-
      // and the third item in the summary of:
      // http://lists.w3.org/Archives/Public/www-style/2009Jul/0050.html
      const nsCSSValueList *shadow1 = aValue1.GetCSSValueListValue();
      const nsCSSValueList *shadow2 = aValue2.GetCSSValueListValue();
      nsAutoPtr<nsCSSValueList> result;
      nsCSSValueList **resultTail = getter_Transfers(result);
      while (shadow1 && shadow2) {
        if (!AddShadowItems(aCoeff1, shadow1->mValue,
                            aCoeff2, shadow2->mValue,
                            resultTail)) {
          return PR_FALSE;
        }
        shadow1 = shadow1->mNext;
        shadow2 = shadow2->mNext;
      }
      if (shadow1 || shadow2) {
        const nsCSSValueList *longShadow;
        double longCoeff;
        if (shadow1) {
          longShadow = shadow1;
          longCoeff = aCoeff1;
        } else {
          longShadow = shadow2;
          longCoeff = aCoeff2;
        }

        while (longShadow) {
          // Passing coefficients that add to less than 1 produces the
          // desired result of interpolating "0 0 0 transparent" with
          // the current shadow.
          if (!AddShadowItems(longCoeff, longShadow->mValue,
                              0.0, longShadow->mValue,
                              resultTail)) {
            return PR_FALSE;
          }

          longShadow = longShadow->mNext;
        }
      }
      aResultValue.SetAndAdoptCSSValueListValue(result.forget(), eUnit_Shadow);
      return PR_TRUE;
    }
    case eUnit_Transform: {
      const nsCSSValueList *list1 = aValue1.GetCSSValueListValue();
      const nsCSSValueList *list2 = aValue2.GetCSSValueListValue();

      // We want to avoid the matrix decomposition when we can, since
      // avoiding it can produce better results both for compound
      // transforms and for skew and skewY (see below).  We can do this
      // in two cases:
      //   (1) if one of the transforms is 'none'
      //   (2) if the lists have the same length and the transform
      //       functions match
      nsAutoPtr<nsCSSValueList> result;
      if (list1->mValue.GetUnit() == eCSSUnit_None) {
        if (list2->mValue.GetUnit() == eCSSUnit_None) {
          result = new nsCSSValueList;
          if (result) {
            result->mValue.SetNoneValue();
          }
        } else {
          result = AddTransformLists(list2, aCoeff2, list2, 0);
        }
      } else {
        if (list2->mValue.GetUnit() == eCSSUnit_None) {
          result = AddTransformLists(list1, aCoeff1, list1, 0);
        } else {
          PRBool match = PR_TRUE;

          {
            const nsCSSValueList *item1 = list1, *item2 = list2;
            do {
              nsCSSKeyword func1 = nsStyleTransformMatrix::TransformFunctionOf(
                                     item1->mValue.GetArrayValue());
              nsCSSKeyword func2 = nsStyleTransformMatrix::TransformFunctionOf(
                                     item2->mValue.GetArrayValue());
              if (func1 != func2) {
                break;
              }

              item1 = item1->mNext;
              item2 = item2->mNext;
            } while (item1 && item2);
            if (item1 || item2) {
              // Either |break| above or length mismatch.
              match = PR_FALSE;
            }
          }

          if (match) {
            result = AddTransformLists(list1, aCoeff1, list2, aCoeff2);
          } else {
            PRBool dummy;
            nsStyleTransformMatrix matrix1 =
              nsStyleTransformMatrix::ReadTransforms(list1, nsnull, nsnull,
                                                     dummy),
                                   matrix2 =
              nsStyleTransformMatrix::ReadTransforms(list2, nsnull, nsnull,
                                                     dummy);
            result = AddTransformMatrix(matrix1, aCoeff1, matrix2, aCoeff2);
          }
        }
      }

      aResultValue.SetAndAdoptCSSValueListValue(result.forget(),
                                                eUnit_Transform);
      return PR_TRUE;
    }
    case eUnit_CSSValuePairList: {
      const nsCSSValuePairList *list1 = aValue1.GetCSSValuePairListValue();
      const nsCSSValuePairList *list2 = aValue2.GetCSSValuePairListValue();
      nsAutoPtr<nsCSSValuePairList> result;
      nsCSSValuePairList **resultTail = getter_Transfers(result);
      do {
        nsCSSValuePairList *item = new nsCSSValuePairList;
        if (!item) {
          return PR_FALSE;
        }
        *resultTail = item;
        resultTail = &item->mNext;

        static nsCSSValue nsCSSValuePairList::* const pairListValues[] = {
          &nsCSSValuePairList::mXValue,
          &nsCSSValuePairList::mYValue,
        };
        for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(pairListValues); ++i) {
          const nsCSSValue &v1 = list1->*(pairListValues[i]);
          const nsCSSValue &v2 = list2->*(pairListValues[i]);
          nsCSSValue &vr = item->*(pairListValues[i]);
          if (v1.GetUnit() != v2.GetUnit()) {
            return PR_FALSE;
          }
          switch (v1.GetUnit()) {
            case eCSSUnit_Pixel:
              AddCSSValuePixel(aCoeff1, v1, aCoeff2, v2, vr);
              break;
            case eCSSUnit_Percent:
              AddCSSValuePercent(aCoeff1, v1, aCoeff2, v2, vr);
              break;
            default:
              if (v1 != v2) {
                return PR_FALSE;
              }
              vr = v1;
              break;
          }
        }
        list1 = list1->mNext;
        list2 = list2->mNext;
      } while (list1 && list2);
      if (list1 || list2) {
        // We can't interpolate lists of different lengths.
        return PR_FALSE;
      }

      aResultValue.SetAndAdoptCSSValuePairListValue(result.forget());
      return PR_TRUE;
    }
  }

  NS_ABORT_IF_FALSE(false, "Can't interpolate using the given common unit");
  return PR_FALSE;
}

already_AddRefed<nsICSSStyleRule>
BuildStyleRule(nsCSSProperty aProperty,
               nsIContent* aTargetElement,
               const nsAString& aSpecifiedValue,
               PRBool aUseSVGMode)
{
  // Set up an empty CSS Declaration
  nsAutoPtr<css::Declaration> declaration(new css::Declaration());
  declaration->InitializeEmpty();

  PRBool changed; // ignored, but needed as outparam for ParseProperty
  nsIDocument* doc = aTargetElement->GetOwnerDoc();
  nsCOMPtr<nsIURI> baseURI = aTargetElement->GetBaseURI();
  nsCSSParser parser(doc->CSSLoader());

  if (aUseSVGMode) {
#ifdef MOZ_SVG
    parser.SetSVGMode(PR_TRUE);
#else
    NS_NOTREACHED("aUseSVGMode should not be set");
#endif
  }

  nsCSSProperty propertyToCheck = nsCSSProps::IsShorthand(aProperty) ?
    nsCSSProps::SubpropertyEntryFor(aProperty)[0] : aProperty;

  // Get a parser, parse the property, and check for CSS parsing errors.
  // If any of these steps fails, we bail out and delete the declaration.
  if (!parser ||
      NS_FAILED(parser.ParseProperty(aProperty, aSpecifiedValue,
                                     doc->GetDocumentURI(), baseURI,
                                     aTargetElement->NodePrincipal(),
                                     declaration, &changed, PR_FALSE)) ||
      // check whether property parsed without CSS parsing errors
      !declaration->HasNonImportantValueFor(propertyToCheck)) {
    NS_WARNING("failure in BuildStyleRule");
    return nsnull;
  }

  return NS_NewCSSStyleRule(nsnull, declaration.forget());
}

inline
already_AddRefed<nsStyleContext>
LookupStyleContext(nsIContent* aElement)
{
  nsIDocument* doc = aElement->GetCurrentDoc();
  nsIPresShell* shell = doc->GetShell();
  if (!shell) {
    return nsnull;
  }
  return nsComputedDOMStyle::GetStyleContextForElement(aElement->AsElement(),
                                                       nsnull, shell);
}


/**
 * Helper function: StyleWithDeclarationAdded
 * Creates a nsStyleRule with the specified property set to the specified
 * value, and returns a nsStyleContext for this rule, as a sibling of the
 * given element's nsStyleContext.
 *
 * If we fail to parse |aSpecifiedValue| for |aProperty|, this method will
 * return nsnull.
 *
 * @param aProperty       The property whose value we're customizing in the
 *                        custom style context.
 * @param aTargetElement  The element whose style context we'll use as a
 *                        sibling for our custom style context.
 * @param aSpecifiedValue The value for |aProperty| in our custom style
 *                        context.
 * @param aUseSVGMode     A flag to indicate whether we should parse
 *                        |aSpecifiedValue| in SVG mode.
 * @return The generated custom nsStyleContext, or nsnull on failure.
 */
already_AddRefed<nsStyleContext>
StyleWithDeclarationAdded(nsCSSProperty aProperty,
                          nsIContent* aTargetElement,
                          const nsAString& aSpecifiedValue,
                          PRBool aUseSVGMode)
{
  NS_ABORT_IF_FALSE(aTargetElement, "null target element");
  NS_ABORT_IF_FALSE(aTargetElement->GetCurrentDoc(),
                    "element needs to be in a document "
                    "if we're going to look up its style context");

  // Look up style context for our target element
  nsRefPtr<nsStyleContext> styleContext = LookupStyleContext(aTargetElement);
  if (!styleContext) {
    return nsnull;
  }

  // Parse specified value into a temporary nsICSSStyleRule
  nsCOMPtr<nsICSSStyleRule> styleRule =
    BuildStyleRule(aProperty, aTargetElement, aSpecifiedValue, aUseSVGMode);
  if (!styleRule) {
    return nsnull;
  }

  styleRule->RuleMatched();

  // Create a temporary nsStyleContext for the style rule
  nsCOMArray<nsIStyleRule> ruleArray;
  ruleArray.AppendObject(styleRule);
  nsStyleSet* styleSet = styleContext->PresContext()->StyleSet();
  return styleSet->ResolveStyleByAddingRules(styleContext, ruleArray);
}

PRBool
nsStyleAnimation::ComputeValue(nsCSSProperty aProperty,
                               nsIContent* aTargetElement,
                               const nsAString& aSpecifiedValue,
                               PRBool aUseSVGMode,
                               Value& aComputedValue)
{
  // XXXbz aTargetElement should be an Element
  NS_ABORT_IF_FALSE(aTargetElement, "null target element");
  NS_ABORT_IF_FALSE(aTargetElement->GetCurrentDoc(),
                    "we should only be able to actively animate nodes that "
                    "are in a document");

  nsRefPtr<nsStyleContext> tmpStyleContext =
    StyleWithDeclarationAdded(aProperty, aTargetElement,
                              aSpecifiedValue, aUseSVGMode);
  if (!tmpStyleContext) {
    return PR_FALSE;
  }

 if (nsCSSProps::IsShorthand(aProperty) ||
     nsCSSProps::kAnimTypeTable[aProperty] == eStyleAnimType_None) {
    // Just capture the specified value
    aComputedValue.SetUnparsedStringValue(nsString(aSpecifiedValue));
    return PR_TRUE;
  }
  // Extract computed value of our property from the temporary style rule
  return ExtractComputedValue(aProperty, tmpStyleContext, aComputedValue);
}

PRBool
nsStyleAnimation::UncomputeValue(nsCSSProperty aProperty,
                                 nsPresContext* aPresContext,
                                 const Value& aComputedValue,
                                 nsCSSValue& aSpecifiedValue)
{
  NS_ABORT_IF_FALSE(aPresContext, "null pres context");

  switch (aComputedValue.GetUnit()) {
    case eUnit_Normal:
      aSpecifiedValue.SetNormalValue();
      break;
    case eUnit_Auto:
      aSpecifiedValue.SetAutoValue();
      break;
    case eUnit_None:
      aSpecifiedValue.SetNoneValue();
      break;
    case eUnit_Enumerated:
    case eUnit_Visibility:
      aSpecifiedValue.
        SetIntValue(aComputedValue.GetIntValue(), eCSSUnit_Enumerated);
      break;
    case eUnit_Integer:
      aSpecifiedValue.
        SetIntValue(aComputedValue.GetIntValue(), eCSSUnit_Integer);
      break;
    case eUnit_Coord:
      nscoordToCSSValue(aComputedValue.GetCoordValue(), aSpecifiedValue);
      break;
    case eUnit_Percent:
      aSpecifiedValue.SetPercentValue(aComputedValue.GetPercentValue());
      break;
    case eUnit_Float:
      aSpecifiedValue.
        SetFloatValue(aComputedValue.GetFloatValue(), eCSSUnit_Number);
      break;
    case eUnit_Color:
      // colors can be alone, or part of a paint server
      aSpecifiedValue.SetColorValue(aComputedValue.GetColorValue());
      break;
    case eUnit_CSSValuePair: {
      // Rule node processing expects pair values to be collapsed to a
      // single value if both halves would be equal, for most but not
      // all properties.  At present, all animatable properties that
      // use pairs do expect collapsing.
      const nsCSSValuePair* pair = aComputedValue.GetCSSValuePairValue();
      if (pair->mXValue == pair->mYValue) {
        aSpecifiedValue = pair->mXValue;
      } else {
        aSpecifiedValue.SetPairValue(pair);
      }
    } break;
    case eUnit_CSSRect: {
      nsCSSRect& rect = aSpecifiedValue.SetRectValue();
      rect = *aComputedValue.GetCSSRectValue();
    } break;
    case eUnit_Dasharray:
    case eUnit_Shadow:
    case eUnit_Transform:
      aSpecifiedValue.
        SetDependentListValue(aComputedValue.GetCSSValueListValue());
      break;
    case eUnit_CSSValuePairList:
      aSpecifiedValue.
        SetDependentPairListValue(aComputedValue.GetCSSValuePairListValue());
      break;
    default:
      return PR_FALSE;
  }
  return PR_TRUE;
}

PRBool
nsStyleAnimation::UncomputeValue(nsCSSProperty aProperty,
                                 nsPresContext* aPresContext,
                                 const Value& aComputedValue,
                                 nsAString& aSpecifiedValue)
{
  NS_ABORT_IF_FALSE(aPresContext, "null pres context");
  aSpecifiedValue.Truncate(); // Clear outparam, if it's not already empty

  if (aComputedValue.GetUnit() == eUnit_UnparsedString) {
    aComputedValue.GetStringValue(aSpecifiedValue);
    return PR_TRUE;
  }
  nsCSSValue val;
  if (!nsStyleAnimation::UncomputeValue(aProperty, aPresContext,
                                        aComputedValue, val)) {
    return PR_FALSE;
  }

  val.AppendToString(aProperty, aSpecifiedValue);
  return PR_TRUE;
}

inline const void*
StyleDataAtOffset(const void* aStyleStruct, ptrdiff_t aOffset)
{
  return reinterpret_cast<const char*>(aStyleStruct) + aOffset;
}

inline void*
StyleDataAtOffset(void* aStyleStruct, ptrdiff_t aOffset)
{
  return reinterpret_cast<char*>(aStyleStruct) + aOffset;
}

static void
ExtractBorderColor(nsStyleContext* aStyleContext, const void* aStyleBorder,
                   mozilla::css::Side aSide, nsStyleAnimation::Value& aComputedValue)
{
  nscolor color;
  PRBool foreground;
  static_cast<const nsStyleBorder*>(aStyleBorder)->
    GetBorderColor(aSide, color, foreground);
  if (foreground) {
    // FIXME: should add test for this
    color = aStyleContext->GetStyleColor()->mColor;
  }
  aComputedValue.SetColorValue(color);
}

static PRBool
StyleCoordToValue(const nsStyleCoord& aCoord, nsStyleAnimation::Value& aValue)
{
  switch (aCoord.GetUnit()) {
    case eStyleUnit_Normal:
      aValue.SetNormalValue();
      break;
    case eStyleUnit_Auto:
      aValue.SetAutoValue();
      break;
    case eStyleUnit_None:
      aValue.SetNoneValue();
      break;
    case eStyleUnit_Percent:
      aValue.SetPercentValue(aCoord.GetPercentValue());
      break;
    case eStyleUnit_Factor:
      aValue.SetFloatValue(aCoord.GetFactorValue());
      break;
    case eStyleUnit_Coord:
      aValue.SetCoordValue(aCoord.GetCoordValue());
      break;
    case eStyleUnit_Enumerated:
      aValue.SetIntValue(aCoord.GetIntValue(),
                         nsStyleAnimation::eUnit_Enumerated);
      break;
    case eStyleUnit_Integer:
      aValue.SetIntValue(aCoord.GetIntValue(),
                         nsStyleAnimation::eUnit_Integer);
      break;
    default:
      return PR_FALSE;
  }
  return PR_TRUE;
}

static void
StyleCoordToCSSValue(const nsStyleCoord& aCoord, nsCSSValue& aCSSValue)
{
  switch (aCoord.GetUnit()) {
    case eStyleUnit_Coord:
      nscoordToCSSValue(aCoord.GetCoordValue(), aCSSValue);
      break;
    case eStyleUnit_Percent:
      aCSSValue.SetPercentValue(aCoord.GetPercentValue());
      break;
    default:
      NS_ABORT_IF_FALSE(PR_FALSE, "unexpected unit");
  }
}

/*
 * Assign |aOutput = aInput|, except with any non-pixel lengths
 * replaced with the equivalent in pixels.
 */
static void
SubstitutePixelValues(nsStyleContext* aStyleContext,
                      const nsCSSValue& aInput, nsCSSValue& aOutput)
{
  if (aInput.UnitHasArrayValue()) {
    const nsCSSValue::Array *inputArray = aInput.GetArrayValue();
    nsRefPtr<nsCSSValue::Array> outputArray =
      nsCSSValue::Array::Create(inputArray->Count());
    for (size_t i = 0, i_end = inputArray->Count(); i < i_end; ++i) {
      SubstitutePixelValues(aStyleContext,
                            inputArray->Item(i), outputArray->Item(i));
    }
    aOutput.SetArrayValue(outputArray, aInput.GetUnit());
  } else if (aInput.IsLengthUnit() &&
             aInput.GetUnit() != eCSSUnit_Pixel) {
    PRBool canStoreInRuleTree = PR_TRUE;
    nscoord len = nsRuleNode::CalcLength(aInput, aStyleContext,
                                         aStyleContext->PresContext(),
                                         canStoreInRuleTree);
    aOutput.SetFloatValue(nsPresContext::AppUnitsToFloatCSSPixels(len),
                          eCSSUnit_Pixel);
  } else {
    aOutput = aInput;
  }
}

PRBool
nsStyleAnimation::ExtractComputedValue(nsCSSProperty aProperty,
                                       nsStyleContext* aStyleContext,
                                       Value& aComputedValue)
{
  NS_ABORT_IF_FALSE(0 <= aProperty &&
                    aProperty < eCSSProperty_COUNT_no_shorthands,
                    "bad property");
  const void* styleStruct =
    aStyleContext->GetStyleData(nsCSSProps::kSIDTable[aProperty]);
  ptrdiff_t ssOffset = nsCSSProps::kStyleStructOffsetTable[aProperty];
  nsStyleAnimType animType = nsCSSProps::kAnimTypeTable[aProperty];
  NS_ABORT_IF_FALSE(0 <= ssOffset || animType == eStyleAnimType_Custom,
                    "must be dealing with animatable property");
  switch (animType) {
    case eStyleAnimType_Custom:
      switch (aProperty) {
        // For border-width, ignore the border-image business (which
        // only exists until we update our implementation to the current
        // spec) and use GetComputedBorder

        #define BORDER_WIDTH_CASE(prop_, side_)                               \
        case prop_:                                                           \
          aComputedValue.SetCoordValue(                                       \
            static_cast<const nsStyleBorder*>(styleStruct)->                  \
              GetComputedBorder().side_);                                     \
          break;
        BORDER_WIDTH_CASE(eCSSProperty_border_bottom_width, bottom)
        BORDER_WIDTH_CASE(eCSSProperty_border_left_width_value, left)
        BORDER_WIDTH_CASE(eCSSProperty_border_right_width_value, right)
        BORDER_WIDTH_CASE(eCSSProperty_border_top_width, top)
        #undef BORDER_WIDTH_CASE

        case eCSSProperty__moz_column_rule_width:
          aComputedValue.SetCoordValue(
            static_cast<const nsStyleColumn*>(styleStruct)->
              GetComputedColumnRuleWidth());
          break;

        case eCSSProperty_border_bottom_color:
          ExtractBorderColor(aStyleContext, styleStruct, NS_SIDE_BOTTOM,
                             aComputedValue);
          break;
        case eCSSProperty_border_left_color_value:
          ExtractBorderColor(aStyleContext, styleStruct, NS_SIDE_LEFT,
                             aComputedValue);
          break;
        case eCSSProperty_border_right_color_value:
          ExtractBorderColor(aStyleContext, styleStruct, NS_SIDE_RIGHT,
                             aComputedValue);
          break;
        case eCSSProperty_border_top_color:
          ExtractBorderColor(aStyleContext, styleStruct, NS_SIDE_TOP,
                             aComputedValue);
          break;

        case eCSSProperty_outline_color: {
          const nsStyleOutline *styleOutline =
            static_cast<const nsStyleOutline*>(styleStruct);
          nscolor color;
        #ifdef GFX_HAS_INVERT
          // This isn't right.  And note that outline drawing itself
          // goes through this codepath via GetVisitedDependentColor.
          styleOutline->GetOutlineColor(color);
        #else
          if (!styleOutline->GetOutlineColor(color))
            color = aStyleContext->GetStyleColor()->mColor;
        #endif
          aComputedValue.SetColorValue(color);
          break;
        }

        case eCSSProperty__moz_column_rule_color: {
          const nsStyleColumn *styleColumn =
            static_cast<const nsStyleColumn*>(styleStruct);
          nscolor color;
          if (styleColumn->mColumnRuleColorIsForeground) {
            color = aStyleContext->GetStyleColor()->mColor;
          } else {
            color = styleColumn->mColumnRuleColor;
          }
          aComputedValue.SetColorValue(color);
          break;
        }

        case eCSSProperty__moz_column_count: {
          const nsStyleColumn *styleColumn =
            static_cast<const nsStyleColumn*>(styleStruct);
          if (styleColumn->mColumnCount == NS_STYLE_COLUMN_COUNT_AUTO) {
            aComputedValue.SetAutoValue();
          } else {
            aComputedValue.SetIntValue(styleColumn->mColumnCount,
                                       eUnit_Integer);
          }
          break;
        }

        case eCSSProperty_border_spacing: {
          const nsStyleTableBorder *styleTableBorder =
            static_cast<const nsStyleTableBorder*>(styleStruct);
          nsCSSValuePair *pair = new nsCSSValuePair;
          if (!pair) {
            return PR_FALSE;
          }
          nscoordToCSSValue(styleTableBorder->mBorderSpacingX, pair->mXValue);
          nscoordToCSSValue(styleTableBorder->mBorderSpacingY, pair->mYValue);
          aComputedValue.SetAndAdoptCSSValuePairValue(pair,
                                                      eUnit_CSSValuePair);
          break;
        }

        case eCSSProperty__moz_transform_origin: {
          const nsStyleDisplay *styleDisplay =
            static_cast<const nsStyleDisplay*>(styleStruct);
          nsCSSValuePair *pair = new nsCSSValuePair;
          if (!pair) {
            return PR_FALSE;
          }
          StyleCoordToCSSValue(styleDisplay->mTransformOrigin[0],
                               pair->mXValue);
          StyleCoordToCSSValue(styleDisplay->mTransformOrigin[1],
                               pair->mYValue);
          aComputedValue.SetAndAdoptCSSValuePairValue(pair,
                                                      eUnit_CSSValuePair);
          break;
        }

        case eCSSProperty_stroke_dasharray: {
          const nsStyleSVG *svg = static_cast<const nsStyleSVG*>(styleStruct);
          NS_ABORT_IF_FALSE((svg->mStrokeDasharray != nsnull) ==
                            (svg->mStrokeDasharrayLength != 0),
                            "pointer/length mismatch");
          nsAutoPtr<nsCSSValueList> result;
          if (svg->mStrokeDasharray) {
            NS_ABORT_IF_FALSE(svg->mStrokeDasharrayLength > 0,
                              "non-null list should have positive length");
            nsCSSValueList **resultTail = getter_Transfers(result);
            for (PRUint32 i = 0, i_end = svg->mStrokeDasharrayLength;
                 i != i_end; ++i) {
              nsCSSValueList *item = new nsCSSValueList;
              if (!item) {
                return PR_FALSE;
              }
              *resultTail = item;
              resultTail = &item->mNext;

              const nsStyleCoord &coord = svg->mStrokeDasharray[i];
              nsCSSValue &value = item->mValue;
              switch (coord.GetUnit()) {
                case eStyleUnit_Coord:
                  // Number means the same thing as length; we want to
                  // animate them the same way.  Normalize both to number
                  // since it has more accuracy (float vs nscoord).
                  value.SetFloatValue(nsPresContext::
                    AppUnitsToFloatCSSPixels(coord.GetCoordValue()),
                    eCSSUnit_Number);
                  break;
                case eStyleUnit_Factor:
                  value.SetFloatValue(coord.GetFactorValue(),
                                      eCSSUnit_Number);
                  break;
                case eStyleUnit_Percent:
                  value.SetPercentValue(coord.GetPercentValue());
                  break;
                default:
                  NS_ABORT_IF_FALSE(PR_FALSE, "unexpected unit");
                  return PR_FALSE;
              }
            }
          } else {
            result = new nsCSSValueList;
            if (!result) {
              return PR_FALSE;
            }
            result->mValue.SetNoneValue();
          }
          aComputedValue.SetAndAdoptCSSValueListValue(result.forget(),
                                                      eUnit_Dasharray);
          break;
        }

        case eCSSProperty_font_stretch: {
          PRInt16 stretch =
            static_cast<const nsStyleFont*>(styleStruct)->mFont.stretch;
          PR_STATIC_ASSERT(NS_STYLE_FONT_STRETCH_ULTRA_CONDENSED == -4);
          PR_STATIC_ASSERT(NS_STYLE_FONT_STRETCH_ULTRA_EXPANDED == 4);
          if (stretch < NS_STYLE_FONT_STRETCH_ULTRA_CONDENSED ||
              stretch > NS_STYLE_FONT_STRETCH_ULTRA_EXPANDED) {
            return PR_FALSE;
          }
          aComputedValue.SetIntValue(stretch, eUnit_Enumerated);
          return PR_TRUE;
        }

        case eCSSProperty_font_weight: {
          PRUint16 weight =
            static_cast<const nsStyleFont*>(styleStruct)->mFont.weight;
          if (weight % 100 != 0) {
            return PR_FALSE;
          }
          aComputedValue.SetIntValue(weight, eUnit_Integer);
          return PR_TRUE;
        }

        case eCSSProperty_image_region: {
          const nsStyleList *list =
            static_cast<const nsStyleList*>(styleStruct);
          const nsRect &srect = list->mImageRegion;
          if (srect.IsEmpty()) {
            aComputedValue.SetAutoValue();
            break;
          }

          nsCSSRect *vrect = new nsCSSRect;
          nscoordToCSSValue(srect.x, vrect->mLeft);
          nscoordToCSSValue(srect.y, vrect->mTop);
          nscoordToCSSValue(srect.XMost(), vrect->mRight);
          nscoordToCSSValue(srect.YMost(), vrect->mBottom);
          aComputedValue.SetAndAdoptCSSRectValue(vrect, eUnit_CSSRect);
          break;
        }

        case eCSSProperty_clip: {
          const nsStyleDisplay *display =
            static_cast<const nsStyleDisplay*>(styleStruct);
          if (!(display->mClipFlags & NS_STYLE_CLIP_RECT)) {
            aComputedValue.SetAutoValue();
          } else {
            nsCSSRect *vrect = new nsCSSRect;
            const nsRect &srect = display->mClip;
            if (display->mClipFlags & NS_STYLE_CLIP_TOP_AUTO) {
              vrect->mTop.SetAutoValue();
            } else {
              nscoordToCSSValue(srect.y, vrect->mTop);
            }
            if (display->mClipFlags & NS_STYLE_CLIP_RIGHT_AUTO) {
              vrect->mRight.SetAutoValue();
            } else {
              nscoordToCSSValue(srect.XMost(), vrect->mRight);
            }
            if (display->mClipFlags & NS_STYLE_CLIP_BOTTOM_AUTO) {
              vrect->mBottom.SetAutoValue();
            } else {
              nscoordToCSSValue(srect.YMost(), vrect->mBottom);
            }
            if (display->mClipFlags & NS_STYLE_CLIP_LEFT_AUTO) {
              vrect->mLeft.SetAutoValue();
            } else {
              nscoordToCSSValue(srect.x, vrect->mLeft);
            }
            aComputedValue.SetAndAdoptCSSRectValue(vrect, eUnit_CSSRect);
          }
          break;
        }

        case eCSSProperty_background_position: {
          const nsStyleBackground *bg =
            static_cast<const nsStyleBackground*>(styleStruct);
          nsCSSValuePairList *result = nsnull;
          nsCSSValuePairList **resultTail = &result;
          NS_ABORT_IF_FALSE(bg->mPositionCount > 0, "unexpected count");
          for (PRUint32 i = 0, i_end = bg->mPositionCount; i != i_end; ++i) {
            nsCSSValuePairList *item = new nsCSSValuePairList;
            *resultTail = item;
            resultTail = &item->mNext;

            const nsStyleBackground::Position &pos = bg->mLayers[i].mPosition;
            if (pos.mXIsPercent) {
              item->mXValue.SetPercentValue(pos.mXPosition.mFloat);
            } else {
              nscoordToCSSValue(pos.mXPosition.mCoord, item->mXValue);
            }
            if (pos.mYIsPercent) {
              item->mYValue.SetPercentValue(pos.mYPosition.mFloat);
            } else {
              nscoordToCSSValue(pos.mYPosition.mCoord, item->mYValue);
            }
          }

          aComputedValue.SetAndAdoptCSSValuePairListValue(result);
          break;
        }

        case eCSSProperty_background_size: {
          const nsStyleBackground *bg =
            static_cast<const nsStyleBackground*>(styleStruct);
          nsCSSValuePairList *result = nsnull;
          nsCSSValuePairList **resultTail = &result;
          NS_ABORT_IF_FALSE(bg->mSizeCount > 0, "unexpected count");
          for (PRUint32 i = 0, i_end = bg->mSizeCount; i != i_end; ++i) {
            nsCSSValuePairList *item = new nsCSSValuePairList;
            *resultTail = item;
            resultTail = &item->mNext;

            const nsStyleBackground::Size &size = bg->mLayers[i].mSize;
            switch (size.mWidthType) {
              case nsStyleBackground::Size::eContain:
              case nsStyleBackground::Size::eCover:
                item->mXValue.SetIntValue(size.mWidthType,
                                          eCSSUnit_Enumerated);
                break;
              case nsStyleBackground::Size::ePercentage:
                item->mXValue.SetPercentValue(size.mWidth.mFloat);
                break;
              case nsStyleBackground::Size::eAuto:
                item->mXValue.SetAutoValue();
                break;
              case nsStyleBackground::Size::eLength:
                nscoordToCSSValue(size.mWidth.mCoord, item->mXValue);
                break;
            }

            switch (size.mHeightType) {
              case nsStyleBackground::Size::eContain:
              case nsStyleBackground::Size::eCover:
                // leave it null
                break;
              case nsStyleBackground::Size::ePercentage:
                item->mYValue.SetPercentValue(size.mHeight.mFloat);
                break;
              case nsStyleBackground::Size::eAuto:
                item->mYValue.SetAutoValue();
                break;
              case nsStyleBackground::Size::eLength:
                nscoordToCSSValue(size.mHeight.mCoord, item->mYValue);
                break;
            }
          }

          aComputedValue.SetAndAdoptCSSValuePairListValue(result);
          break;
        }

        case eCSSProperty__moz_transform: {
          const nsStyleDisplay *display =
            static_cast<const nsStyleDisplay*>(styleStruct);
          nsAutoPtr<nsCSSValueList> result;
          if (display->mSpecifiedTransform) {
            // Clone, and convert all lengths (not percents) to pixels.
            nsCSSValueList **resultTail = getter_Transfers(result);
            for (const nsCSSValueList *l = display->mSpecifiedTransform;
                 l; l = l->mNext) {
              nsCSSValueList *clone = new nsCSSValueList;
              *resultTail = clone;
              resultTail = &clone->mNext;

              SubstitutePixelValues(aStyleContext, l->mValue, clone->mValue);
            }
          } else {
            result = new nsCSSValueList();
            result->mValue.SetNoneValue();
          }

          aComputedValue.SetAndAdoptCSSValueListValue(result.forget(),
                                                      eUnit_Transform);
          break;
        }

        default:
          NS_ABORT_IF_FALSE(PR_FALSE, "missing property implementation");
          return PR_FALSE;
      };
      return PR_TRUE;
    case eStyleAnimType_Coord:
      return StyleCoordToValue(*static_cast<const nsStyleCoord*>(
        StyleDataAtOffset(styleStruct, ssOffset)), aComputedValue);
    case eStyleAnimType_Sides_Top:
    case eStyleAnimType_Sides_Right:
    case eStyleAnimType_Sides_Bottom:
    case eStyleAnimType_Sides_Left: {
      PR_STATIC_ASSERT(0 == NS_SIDE_TOP);
      PR_STATIC_ASSERT(eStyleAnimType_Sides_Right - eStyleAnimType_Sides_Top
                         == NS_SIDE_RIGHT);
      PR_STATIC_ASSERT(eStyleAnimType_Sides_Bottom - eStyleAnimType_Sides_Top
                         == NS_SIDE_BOTTOM);
      PR_STATIC_ASSERT(eStyleAnimType_Sides_Left - eStyleAnimType_Sides_Top
                         == NS_SIDE_LEFT);
      const nsStyleCoord &coord = static_cast<const nsStyleSides*>(
        StyleDataAtOffset(styleStruct, ssOffset))->
          Get(mozilla::css::Side(animType - eStyleAnimType_Sides_Top));
      return StyleCoordToValue(coord, aComputedValue);
    }
    case eStyleAnimType_Corner_TopLeft:
    case eStyleAnimType_Corner_TopRight:
    case eStyleAnimType_Corner_BottomRight:
    case eStyleAnimType_Corner_BottomLeft: {
      PR_STATIC_ASSERT(0 == NS_CORNER_TOP_LEFT);
      PR_STATIC_ASSERT(eStyleAnimType_Corner_TopRight -
                         eStyleAnimType_Corner_TopLeft
                       == NS_CORNER_TOP_RIGHT);
      PR_STATIC_ASSERT(eStyleAnimType_Corner_BottomRight -
                         eStyleAnimType_Corner_TopLeft
                       == NS_CORNER_BOTTOM_RIGHT);
      PR_STATIC_ASSERT(eStyleAnimType_Corner_BottomLeft -
                         eStyleAnimType_Corner_TopLeft
                       == NS_CORNER_BOTTOM_LEFT);
      const nsStyleCorners *corners = static_cast<const nsStyleCorners*>(
        StyleDataAtOffset(styleStruct, ssOffset));
      PRUint8 fullCorner = animType - eStyleAnimType_Corner_TopLeft;
      const nsStyleCoord &horiz =
        corners->Get(NS_FULL_TO_HALF_CORNER(fullCorner, PR_FALSE));
      const nsStyleCoord &vert =
        corners->Get(NS_FULL_TO_HALF_CORNER(fullCorner, PR_TRUE));
      nsCSSValuePair *pair = new nsCSSValuePair;
      if (!pair) {
        return PR_FALSE;
      }
      StyleCoordToCSSValue(horiz, pair->mXValue);
      StyleCoordToCSSValue(vert, pair->mYValue);
      aComputedValue.SetAndAdoptCSSValuePairValue(pair, eUnit_CSSValuePair);
      return PR_TRUE;
    }
    case eStyleAnimType_nscoord:
      aComputedValue.SetCoordValue(*static_cast<const nscoord*>(
        StyleDataAtOffset(styleStruct, ssOffset)));
      return PR_TRUE;
    case eStyleAnimType_EnumU8:
      aComputedValue.SetIntValue(*static_cast<const PRUint8*>(
        StyleDataAtOffset(styleStruct, ssOffset)), eUnit_Enumerated);
      return PR_TRUE;
    case eStyleAnimType_float:
      aComputedValue.SetFloatValue(*static_cast<const float*>(
        StyleDataAtOffset(styleStruct, ssOffset)));
      if (aProperty == eCSSProperty_font_size_adjust &&
          aComputedValue.GetFloatValue() == 0.0f) {
        // In nsStyleFont, we set mFont.sizeAdjust to 0 to represent
        // font-size-adjust: none.  Here, we have to treat this as a keyword
        // instead of a float value, to make sure we don't end up doing
        // interpolation with it.
        aComputedValue.SetNoneValue();
      }
      return PR_TRUE;
    case eStyleAnimType_Color:
      aComputedValue.SetColorValue(*static_cast<const nscolor*>(
        StyleDataAtOffset(styleStruct, ssOffset)));
      return PR_TRUE;
    case eStyleAnimType_PaintServer: {
      const nsStyleSVGPaint &paint = *static_cast<const nsStyleSVGPaint*>(
        StyleDataAtOffset(styleStruct, ssOffset));
      // FIXME: At some point in the future, we should animate gradients.
      if (paint.mType == eStyleSVGPaintType_Color) {
        aComputedValue.SetColorValue(paint.mPaint.mColor);
        return PR_TRUE;
      }
      if (paint.mType == eStyleSVGPaintType_None) {
        aComputedValue.SetNoneValue();
        return PR_TRUE;
      }
      return PR_FALSE;
    }
    case eStyleAnimType_Shadow: {
      const nsCSSShadowArray *shadowArray =
        *static_cast<const nsRefPtr<nsCSSShadowArray>*>(
          StyleDataAtOffset(styleStruct, ssOffset));
      if (!shadowArray) {
        aComputedValue.SetAndAdoptCSSValueListValue(nsnull, eUnit_Shadow);
        return PR_TRUE;
      }
      nsAutoPtr<nsCSSValueList> result;
      nsCSSValueList **resultTail = getter_Transfers(result);
      for (PRUint32 i = 0, i_end = shadowArray->Length(); i < i_end; ++i) {
        const nsCSSShadowItem *shadow = shadowArray->ShadowAt(i);
        // X, Y, Radius, Spread, Color, Inset
        nsRefPtr<nsCSSValue::Array> arr = nsCSSValue::Array::Create(6);
        nscoordToCSSValue(shadow->mXOffset, arr->Item(0));
        nscoordToCSSValue(shadow->mYOffset, arr->Item(1));
        nscoordToCSSValue(shadow->mRadius, arr->Item(2));
        // NOTE: This code sometimes stores mSpread: 0 even when
        // the parser would be required to leave it null.
        nscoordToCSSValue(shadow->mSpread, arr->Item(3));
        if (shadow->mHasColor) {
          arr->Item(4).SetColorValue(shadow->mColor);
        }
        if (shadow->mInset) {
          arr->Item(5).SetIntValue(NS_STYLE_BOX_SHADOW_INSET,
                                   eCSSUnit_Enumerated);
        }

        nsCSSValueList *resultItem = new nsCSSValueList;
        if (!resultItem) {
          return PR_FALSE;
        }
        resultItem->mValue.SetArrayValue(arr, eCSSUnit_Array);
        *resultTail = resultItem;
        resultTail = &resultItem->mNext;
      }
      aComputedValue.SetAndAdoptCSSValueListValue(result.forget(),
                                                  eUnit_Shadow);
      return PR_TRUE;
    }
    case eStyleAnimType_None:
      NS_NOTREACHED("shouldn't use on non-animatable properties");
  }
  return PR_FALSE;
}

nsStyleAnimation::Value::Value(PRInt32 aInt, Unit aUnit,
                               IntegerConstructorType)
{
  NS_ASSERTION(IsIntUnit(aUnit), "unit must be of integer type");
  mUnit = aUnit;
  mValue.mInt = aInt;
}

nsStyleAnimation::Value::Value(nscoord aLength, CoordConstructorType)
{
  mUnit = eUnit_Coord;
  mValue.mCoord = aLength;
}

nsStyleAnimation::Value::Value(float aPercent, PercentConstructorType)
{
  mUnit = eUnit_Percent;
  mValue.mFloat = aPercent;
}

nsStyleAnimation::Value::Value(float aFloat, FloatConstructorType)
{
  mUnit = eUnit_Float;
  mValue.mFloat = aFloat;
}

nsStyleAnimation::Value::Value(nscolor aColor, ColorConstructorType)
{
  mUnit = eUnit_Color;
  mValue.mColor = aColor;
}

nsStyleAnimation::Value&
nsStyleAnimation::Value::operator=(const Value& aOther)
{
  FreeValue();

  mUnit = aOther.mUnit;
  switch (mUnit) {
    case eUnit_Null:
    case eUnit_Normal:
    case eUnit_Auto:
    case eUnit_None:
      break;
    case eUnit_Enumerated:
    case eUnit_Visibility:
    case eUnit_Integer:
      mValue.mInt = aOther.mValue.mInt;
      break;
    case eUnit_Coord:
      mValue.mCoord = aOther.mValue.mCoord;
      break;
    case eUnit_Percent:
    case eUnit_Float:
      mValue.mFloat = aOther.mValue.mFloat;
      break;
    case eUnit_Color:
      mValue.mColor = aOther.mValue.mColor;
      break;
    case eUnit_CSSValuePair:
      NS_ABORT_IF_FALSE(aOther.mValue.mCSSValuePair,
                        "value pairs may not be null");
      mValue.mCSSValuePair = new nsCSSValuePair(*aOther.mValue.mCSSValuePair);
      if (!mValue.mCSSValuePair) {
        mUnit = eUnit_Null;
      }
      break;
    case eUnit_CSSRect:
      NS_ABORT_IF_FALSE(aOther.mValue.mCSSRect, "rects may not be null");
      mValue.mCSSRect = new nsCSSRect(*aOther.mValue.mCSSRect);
      if (!mValue.mCSSRect) {
        mUnit = eUnit_Null;
      }
      break;
    case eUnit_Dasharray:
    case eUnit_Shadow:
    case eUnit_Transform:
      NS_ABORT_IF_FALSE(mUnit == eUnit_Shadow || aOther.mValue.mCSSValueList,
                        "value lists other than shadows may not be null");
      if (aOther.mValue.mCSSValueList) {
        mValue.mCSSValueList = aOther.mValue.mCSSValueList->Clone();
        if (!mValue.mCSSValueList) {
          mUnit = eUnit_Null;
        }
      } else {
        mValue.mCSSValueList = nsnull;
      }
      break;
    case eUnit_CSSValuePairList:
      NS_ABORT_IF_FALSE(aOther.mValue.mCSSValuePairList,
                        "value pair lists may not be null");
      mValue.mCSSValuePairList = aOther.mValue.mCSSValuePairList->Clone();
      if (!mValue.mCSSValuePairList) {
        mUnit = eUnit_Null;
      }
      break;
    case eUnit_UnparsedString:
      NS_ABORT_IF_FALSE(aOther.mValue.mString, "expecting non-null string");
      mValue.mString = aOther.mValue.mString;
      mValue.mString->AddRef();
      break;
  }

  return *this;
}

void
nsStyleAnimation::Value::SetNormalValue()
{
  FreeValue();
  mUnit = eUnit_Normal;
}

void
nsStyleAnimation::Value::SetAutoValue()
{
  FreeValue();
  mUnit = eUnit_Auto;
}

void
nsStyleAnimation::Value::SetNoneValue()
{
  FreeValue();
  mUnit = eUnit_None;
}

void
nsStyleAnimation::Value::SetIntValue(PRInt32 aInt, Unit aUnit)
{
  NS_ASSERTION(IsIntUnit(aUnit), "unit must be of integer type");
  FreeValue();
  mUnit = aUnit;
  mValue.mInt = aInt;
}

void
nsStyleAnimation::Value::SetCoordValue(nscoord aLength)
{
  FreeValue();
  mUnit = eUnit_Coord;
  mValue.mCoord = aLength;
}

void
nsStyleAnimation::Value::SetPercentValue(float aPercent)
{
  FreeValue();
  mUnit = eUnit_Percent;
  mValue.mFloat = aPercent;
}

void
nsStyleAnimation::Value::SetFloatValue(float aFloat)
{
  FreeValue();
  mUnit = eUnit_Float;
  mValue.mFloat = aFloat;
}

void
nsStyleAnimation::Value::SetColorValue(nscolor aColor)
{
  FreeValue();
  mUnit = eUnit_Color;
  mValue.mColor = aColor;
}

void
nsStyleAnimation::Value::SetUnparsedStringValue(const nsString& aString)
{
  FreeValue();
  mUnit = eUnit_UnparsedString;
  mValue.mString = nsCSSValue::BufferFromString(aString);
  if (NS_UNLIKELY(!mValue.mString)) {
    // not much we can do here; just make sure that our promise of a
    // non-null mValue.mString holds for string units.
    mUnit = eUnit_Null;
  }
}

void
nsStyleAnimation::Value::SetAndAdoptCSSValuePairValue(
                           nsCSSValuePair *aValuePair, Unit aUnit)
{
  FreeValue();
  NS_ABORT_IF_FALSE(IsCSSValuePairUnit(aUnit), "bad unit");
  NS_ABORT_IF_FALSE(aValuePair != nsnull, "value pairs may not be null");
  mUnit = aUnit;
  mValue.mCSSValuePair = aValuePair; // take ownership
}

void
nsStyleAnimation::Value::SetAndAdoptCSSRectValue(nsCSSRect *aRect, Unit aUnit)
{
  FreeValue();
  NS_ABORT_IF_FALSE(IsCSSRectUnit(aUnit), "bad unit");
  NS_ABORT_IF_FALSE(aRect != nsnull, "value pairs may not be null");
  mUnit = aUnit;
  mValue.mCSSRect = aRect; // take ownership
}

void
nsStyleAnimation::Value::SetAndAdoptCSSValueListValue(
                           nsCSSValueList *aValueList, Unit aUnit)
{
  FreeValue();
  NS_ABORT_IF_FALSE(IsCSSValueListUnit(aUnit), "bad unit");
  NS_ABORT_IF_FALSE(aUnit != eUnit_Dasharray || aValueList != nsnull,
                    "dasharrays may not be null");
  mUnit = aUnit;
  mValue.mCSSValueList = aValueList; // take ownership
}

void
nsStyleAnimation::Value::SetAndAdoptCSSValuePairListValue(
                           nsCSSValuePairList *aValuePairList)
{
  FreeValue();
  NS_ABORT_IF_FALSE(aValuePairList, "may not be null");
  mUnit = eUnit_CSSValuePairList;
  mValue.mCSSValuePairList = aValuePairList; // take ownership
}

void
nsStyleAnimation::Value::FreeValue()
{
  if (IsCSSValueListUnit(mUnit)) {
    delete mValue.mCSSValueList;
  } else if (IsCSSValuePairUnit(mUnit)) {
    delete mValue.mCSSValuePair;
  } else if (IsCSSRectUnit(mUnit)) {
    delete mValue.mCSSRect;
  } else if (IsCSSValuePairListUnit(mUnit)) {
    delete mValue.mCSSValuePairList;
  } else if (IsStringUnit(mUnit)) {
    NS_ABORT_IF_FALSE(mValue.mString, "expecting non-null string");
    mValue.mString->Release();
  }
}

PRBool
nsStyleAnimation::Value::operator==(const Value& aOther) const
{
  if (mUnit != aOther.mUnit) {
    return PR_FALSE;
  }

  switch (mUnit) {
    case eUnit_Null:
    case eUnit_Normal:
    case eUnit_Auto:
    case eUnit_None:
      return PR_TRUE;
    case eUnit_Enumerated:
    case eUnit_Visibility:
    case eUnit_Integer:
      return mValue.mInt == aOther.mValue.mInt;
    case eUnit_Coord:
      return mValue.mCoord == aOther.mValue.mCoord;
    case eUnit_Percent:
    case eUnit_Float:
      return mValue.mFloat == aOther.mValue.mFloat;
    case eUnit_Color:
      return mValue.mColor == aOther.mValue.mColor;
    case eUnit_CSSValuePair:
      return *mValue.mCSSValuePair == *aOther.mValue.mCSSValuePair;
    case eUnit_CSSRect:
      return *mValue.mCSSRect == *aOther.mValue.mCSSRect;
    case eUnit_Dasharray:
    case eUnit_Shadow:
    case eUnit_Transform:
      return *mValue.mCSSValueList == *aOther.mValue.mCSSValueList;
    case eUnit_CSSValuePairList:
      return *mValue.mCSSValuePairList == *aOther.mValue.mCSSValuePairList;
    case eUnit_UnparsedString:
      return (NS_strcmp(GetStringBufferValue(),
                        aOther.GetStringBufferValue()) == 0);
  }

  NS_NOTREACHED("incomplete case");
  return PR_FALSE;
}

