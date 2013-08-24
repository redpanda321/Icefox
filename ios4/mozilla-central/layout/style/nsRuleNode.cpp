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
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Original Author: David W. Hyatt (hyatt@netscape.com)
 *   Daniel Glazman <glazman@netscape.com>
 *   Roger B. Sidje <rbs@maths.uq.edu.au>
 *   Mats Palmgren <mats.palmgren@bredband.net>
 *   L. David Baron <dbaron@dbaron.org>
 *   Christian Biesinger <cbiesinger@web.de>
 *   Michael Ventnor <m.ventnor@gmail.com>
 *   Keith Rarick <kr@xph.us>
 *   Jonathon Jongsma <jonathon.jongsma@collabora.co.uk>, Collabora Ltd.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

/*
 * a node in the lexicographic tree of rules that match an element,
 * responsible for converting the rules' information into computed style
 */

#include "nsRuleNode.h"
#include "nscore.h"
#include "nsIServiceManager.h"
#include "nsIDeviceContext.h"
#include "nsIWidget.h"
#include "nsILookAndFeel.h"
#include "nsIPresShell.h"
#include "nsIThebesFontMetrics.h"
#include "gfxFont.h"
#include "nsStyleUtil.h"
#include "nsCSSPseudoElements.h"
#include "nsThemeConstants.h"
#include "nsITheme.h"
#include "pldhash.h"
#include "nsStyleContext.h"
#include "nsStyleSet.h"
#include "nsSize.h"
#include "imgIRequest.h"
#include "nsRuleData.h"
#include "nsILanguageAtomService.h"
#include "nsIStyleRule.h"
#include "nsBidiUtils.h"
#include "nsUnicharUtils.h"
#include "nsStyleStructInlines.h"
#include "nsStyleTransformMatrix.h"
#include "nsCSSKeywords.h"
#include "nsCSSProps.h"
#include "nsTArray.h"
#include "nsContentUtils.h"
#include "mozilla/dom/Element.h"
#include "CSSCalc.h"
#include "nsPrintfCString.h"

using namespace mozilla::dom;
namespace css = mozilla::css;

#define NS_SET_IMAGE_REQUEST(method_, context_, request_)                   \
  if ((context_)->PresContext()->IsDynamic()) {                               \
    method_(request_);                                                      \
  } else {                                                                  \
    nsCOMPtr<imgIRequest> req = nsContentUtils::GetStaticRequest(request_); \
    method_(req);                                                           \
  }

/*
 * For storage of an |nsRuleNode|'s children in a PLDHashTable.
 */

struct ChildrenHashEntry : public PLDHashEntryHdr {
  // key is |mRuleNode->GetKey()|
  nsRuleNode *mRuleNode;
};

/* static */ PLDHashNumber
nsRuleNode::ChildrenHashHashKey(PLDHashTable *aTable, const void *aKey)
{
  const nsRuleNode::Key *key =
    static_cast<const nsRuleNode::Key*>(aKey);
  // Disagreement on importance and level for the same rule is extremely
  // rare, so hash just on the rule.
  return PL_DHashVoidPtrKeyStub(aTable, key->mRule);
}

/* static */ PRBool
nsRuleNode::ChildrenHashMatchEntry(PLDHashTable *aTable,
                                   const PLDHashEntryHdr *aHdr,
                                   const void *aKey)
{
  const ChildrenHashEntry *entry =
    static_cast<const ChildrenHashEntry*>(aHdr);
  const nsRuleNode::Key *key =
    static_cast<const nsRuleNode::Key*>(aKey);
  return entry->mRuleNode->GetKey() == *key;
}

/* static */ PLDHashTableOps
nsRuleNode::ChildrenHashOps = {
  // It's probably better to allocate the table itself using malloc and
  // free rather than the pres shell's arena because the table doesn't
  // grow very often and the pres shell's arena doesn't recycle very
  // large size allocations.
  PL_DHashAllocTable,
  PL_DHashFreeTable,
  ChildrenHashHashKey,
  ChildrenHashMatchEntry,
  PL_DHashMoveEntryStub,
  PL_DHashClearEntryStub,
  PL_DHashFinalizeStub,
  NULL
};


// EnsureBlockDisplay:
//  - if the display value (argument) is not a block-type
//    then we set it to a valid block display value
//  - For enforcing the floated/positioned element CSS2 rules
static void EnsureBlockDisplay(PRUint8& display)
{
  // see if the display value is already a block
  switch (display) {
  case NS_STYLE_DISPLAY_NONE :
    // never change display:none *ever*
  case NS_STYLE_DISPLAY_TABLE :
  case NS_STYLE_DISPLAY_BLOCK :
  case NS_STYLE_DISPLAY_LIST_ITEM :
    // do not muck with these at all - already blocks
    // This is equivalent to nsStyleDisplay::IsBlockOutside.  (XXX Maybe we
    // should just call that?)
    // This needs to match the check done in
    // nsCSSFrameConstructor::FindMathMLData for <math>.
    break;

  case NS_STYLE_DISPLAY_INLINE_TABLE :
    // make inline tables into tables
    display = NS_STYLE_DISPLAY_TABLE;
    break;

  default :
    // make it a block
    display = NS_STYLE_DISPLAY_BLOCK;
  }
}

static nscoord CalcLengthWith(const nsCSSValue& aValue,
                              nscoord aFontSize,
                              const nsStyleFont* aStyleFont,
                              nsStyleContext* aStyleContext,
                              nsPresContext* aPresContext,
                              PRBool aUseProvidedRootEmSize,
                              PRBool aUseUserFontSet,
                              PRBool& aCanStoreInRuleTree);

struct CalcLengthCalcOps : public css::BasicCoordCalcOps,
                           public css::NumbersAlreadyNormalizedOps
{
  // All of the parameters to CalcLengthWith except aValue.
  const nscoord mFontSize;
  const nsStyleFont* const mStyleFont;
  nsStyleContext* const mStyleContext;
  nsPresContext* const mPresContext;
  const PRBool mUseProvidedRootEmSize;
  const PRBool mUseUserFontSet;
  PRBool& mCanStoreInRuleTree;

  CalcLengthCalcOps(nscoord aFontSize, const nsStyleFont* aStyleFont,
                    nsStyleContext* aStyleContext, nsPresContext* aPresContext,
                    PRBool aUseProvidedRootEmSize, PRBool aUseUserFontSet,
                    PRBool& aCanStoreInRuleTree)
    : mFontSize(aFontSize),
      mStyleFont(aStyleFont),
      mStyleContext(aStyleContext),
      mPresContext(aPresContext),
      mUseProvidedRootEmSize(aUseProvidedRootEmSize),
      mUseUserFontSet(aUseUserFontSet),
      mCanStoreInRuleTree(aCanStoreInRuleTree)
  {
  }

  result_type ComputeLeafValue(const nsCSSValue& aValue)
  {
    return CalcLengthWith(aValue, mFontSize, mStyleFont, mStyleContext,
                          mPresContext, mUseProvidedRootEmSize,
                          mUseUserFontSet, mCanStoreInRuleTree);
  }
};

static inline nscoord ScaleCoord(const nsCSSValue &aValue, float factor)
{
  return NSToCoordRoundWithClamp(aValue.GetFloatValue() * factor);
}

static nscoord CalcLengthWith(const nsCSSValue& aValue,
                              nscoord aFontSize,
                              const nsStyleFont* aStyleFont,
                              nsStyleContext* aStyleContext,
                              nsPresContext* aPresContext,
                              PRBool aUseProvidedRootEmSize,
                              // aUseUserFontSet should always be PR_TRUE
                              // except when called from
                              // CalcLengthWithInitialFont.
                              PRBool aUseUserFontSet,
                              PRBool& aCanStoreInRuleTree)
{
  NS_ASSERTION(aValue.IsLengthUnit() || aValue.IsCalcUnit(),
               "not a length or calc unit");
  NS_ASSERTION(aStyleFont || aStyleContext, "Must have style data");
  NS_ASSERTION(!aStyleFont || !aStyleContext, "Duplicate sources of data");
  NS_ASSERTION(aPresContext, "Must have prescontext");

  if (aValue.IsFixedLengthUnit()) {
    return aValue.GetFixedLength(aPresContext);
  }
  if (aValue.IsPixelLengthUnit()) {
    return aValue.GetPixelLength();
  }
  // Common code for all units other than pixel-based units and fixed-length
  // units:
  aCanStoreInRuleTree = PR_FALSE;
  const nsStyleFont *styleFont =
    aStyleFont ? aStyleFont : aStyleContext->GetStyleFont();
  if (aFontSize == -1) {
    // XXX Should this be styleFont->mSize instead to avoid taking minfontsize
    // prefs into account?
    aFontSize = styleFont->mFont.size;
  }
  switch (aValue.GetUnit()) {
    case eCSSUnit_RootEM: {
      nscoord rootFontSize;

      if (aUseProvidedRootEmSize) {
        // We should use the provided aFontSize as the reference length to
        // scale. This only happens when we are calculating font-size or
        // an equivalent (scriptminsize or CalcLengthWithInitialFont) on
        // the root element, in which case aFontSize is already the
        // value we want.
        rootFontSize = aFontSize;
      } else if (aStyleContext && !aStyleContext->GetParent()) {
        // This is the root element (XXX we don't really know this, but
        // nsRuleNode::SetFont makes the same assumption!), so we should
        // use GetStyleFont on this context to get the root element's
        // font size.
        rootFontSize = styleFont->mFont.size;
      } else {
        // This is not the root element or we are calculating something other
        // than font size, so rem is relative to the root element's font size.
        nsRefPtr<nsStyleContext> rootStyle;
        const nsStyleFont *rootStyleFont = styleFont;
        Element* docElement = aPresContext->Document()->GetRootElement();

        if (docElement) {
          rootStyle = aPresContext->StyleSet()->ResolveStyleFor(docElement,
                                                                nsnull);
          if (rootStyle) {
            rootStyleFont = rootStyle->GetStyleFont();
          }
        }

        rootFontSize = rootStyleFont->mFont.size;
      }

      return ScaleCoord(aValue, float(rootFontSize));
    }
    case eCSSUnit_EM: {
      return ScaleCoord(aValue, float(aFontSize));
      // XXX scale against font metrics height instead?
    }
    case eCSSUnit_XHeight: {
      nsFont font = styleFont->mFont;
      font.size = aFontSize;
      nsCOMPtr<nsIFontMetrics> fm =
        aPresContext->GetMetricsFor(font, aUseUserFontSet);
      nscoord xHeight;
      fm->GetXHeight(xHeight);
      return ScaleCoord(aValue, float(xHeight));
    }
    case eCSSUnit_Char: {
      nsFont font = styleFont->mFont;
      font.size = aFontSize;
      nsCOMPtr<nsIFontMetrics> fm =
        aPresContext->GetMetricsFor(font, aUseUserFontSet);
      nsCOMPtr<nsIThebesFontMetrics> tfm(do_QueryInterface(fm));
      gfxFloat zeroWidth = (tfm->GetThebesFontGroup()->GetFontAt(0)
                            ->GetMetrics().zeroOrAveCharWidth);

      return ScaleCoord(aValue, NS_ceil(aPresContext->AppUnitsPerDevPixel() *
                                        zeroWidth));
    }
    // For properties for which lengths are the *only* units accepted in
    // calc(), we can handle calc() here and just compute a final
    // result.  We ensure that we don't get to this code for other
    // properties by not calling CalcLength in those cases:  SetCoord
    // only calls CalcLength for a calc when it is appropriate to do so.
    case eCSSUnit_Calc:
    case eCSSUnit_Calc_Plus:
    case eCSSUnit_Calc_Minus:
    case eCSSUnit_Calc_Times_L:
    case eCSSUnit_Calc_Times_R:
    case eCSSUnit_Calc_Divided:
    case eCSSUnit_Calc_Minimum:
    case eCSSUnit_Calc_Maximum: {
      CalcLengthCalcOps ops(aFontSize, aStyleFont, aStyleContext, aPresContext,
                            aUseProvidedRootEmSize, aUseUserFontSet,
                            aCanStoreInRuleTree);
      return css::ComputeCalc(aValue, ops);
    }
    default:
      NS_NOTREACHED("unexpected unit");
      break;
  }
  return 0;
}

/* static */ nscoord
nsRuleNode::CalcLength(const nsCSSValue& aValue,
                       nsStyleContext* aStyleContext,
                       nsPresContext* aPresContext,
                       PRBool& aCanStoreInRuleTree)
{
  NS_ASSERTION(aStyleContext, "Must have style data");

  return CalcLengthWith(aValue, -1, nsnull, aStyleContext, aPresContext,
                        PR_FALSE, PR_TRUE, aCanStoreInRuleTree);
}

/* Inline helper function to redirect requests to CalcLength. */
static inline nscoord CalcLength(const nsCSSValue& aValue,
                                 nsStyleContext* aStyleContext,
                                 nsPresContext* aPresContext,
                                 PRBool& aCanStoreInRuleTree)
{
  return nsRuleNode::CalcLength(aValue, aStyleContext,
                                aPresContext, aCanStoreInRuleTree);
}

/* static */ nscoord
nsRuleNode::CalcLengthWithInitialFont(nsPresContext* aPresContext,
                                      const nsCSSValue& aValue)
{
  nsStyleFont defaultFont(aPresContext);
  PRBool canStoreInRuleTree;
  return CalcLengthWith(aValue, -1, &defaultFont, nsnull, aPresContext,
                        PR_TRUE, PR_FALSE, canStoreInRuleTree);
}

struct SpecifiedToComputedCalcOps : public css::NumbersAlreadyNormalizedOps
{
  // FIXME (perf): Is there too much copying as a result of returning
  // nsStyleCoord objects?
  typedef nsStyleCoord result_type;

  nsStyleContext* const mStyleContext;
  nsPresContext* const mPresContext;
  PRBool& mCanStoreInRuleTree;

  SpecifiedToComputedCalcOps(nsStyleContext* aStyleContext,
                             nsPresContext* aPresContext,
                             PRBool& aCanStoreInRuleTree)
    : mStyleContext(aStyleContext),
      mPresContext(aPresContext),
      mCanStoreInRuleTree(aCanStoreInRuleTree)
  {
  }

  result_type
  MergeAdditive(nsCSSUnit aCalcFunction,
                result_type aValue1, result_type aValue2)
  {
    nsStyleUnit unit1 = aValue1.GetUnit();
    nsStyleUnit unit2 = aValue2.GetUnit();
    NS_ABORT_IF_FALSE(unit1 == eStyleUnit_Coord ||
                      unit1 == eStyleUnit_Percent ||
                      aValue1.IsCalcUnit(),
                      "unexpected unit");
    NS_ABORT_IF_FALSE(unit2 == eStyleUnit_Coord ||
                      unit2 == eStyleUnit_Percent ||
                      aValue2.IsCalcUnit(),
                      "unexpected unit");
    nsStyleCoord result;
    if (unit1 == unit2 && !aValue1.IsCalcUnit()) {
      // Merge nodes that we don't need to keep separate.
      if (unit1 == eStyleUnit_Percent) {
        css::BasicFloatCalcOps ops;
        result.SetPercentValue(ops.MergeAdditive(aCalcFunction,
                                                 aValue1.GetPercentValue(),
                                                 aValue2.GetPercentValue()));
      } else {
        css::BasicCoordCalcOps ops;
        result.SetCoordValue(ops.MergeAdditive(aCalcFunction,
                                               aValue1.GetCoordValue(),
                                               aValue2.GetCoordValue()));
      }
    } else {
      nsStyleCoord::Array *array =
        nsStyleCoord::Array::Create(mStyleContext, mCanStoreInRuleTree, 2);
      array->Item(0) = aValue1;
      array->Item(1) = aValue2;
      result.SetArrayValue(array, css::ConvertCalcUnit(aCalcFunction));
    }
    return result;
  }

  result_type
  MergeMultiplicativeL(nsCSSUnit aCalcFunction,
                       float aValue1, result_type aValue2)
  {
    nsStyleCoord result;
    switch (aValue2.GetUnit()) {
      case eStyleUnit_Percent: {
        css::BasicFloatCalcOps ops;
        result.SetPercentValue(ops.MergeMultiplicativeL(
          aCalcFunction, aValue1, aValue2.GetPercentValue()));
        break;
      }
      case eStyleUnit_Coord: {
        css::BasicCoordCalcOps ops;
        result.SetCoordValue(ops.MergeMultiplicativeL(
          aCalcFunction, aValue1, aValue2.GetCoordValue()));
        break;
      }
      default:
        NS_ABORT_IF_FALSE(aValue2.IsCalcUnit(), "unexpected unit");
        nsStyleCoord::Array *array =
          nsStyleCoord::Array::Create(mStyleContext, mCanStoreInRuleTree, 2);
        array->Item(0).SetFactorValue(aValue1);
        array->Item(1) = aValue2;
        result.SetArrayValue(array, css::ConvertCalcUnit(aCalcFunction));
        break;
    }
    return result;
  }

  result_type
  MergeMultiplicativeR(nsCSSUnit aCalcFunction,
                       result_type aValue1, float aValue2)
  {
    nsStyleCoord result;
    switch (aValue1.GetUnit()) {
      case eStyleUnit_Percent: {
        css::BasicFloatCalcOps ops;
        result.SetPercentValue(ops.MergeMultiplicativeR(
          aCalcFunction, aValue1.GetPercentValue(), aValue2));
        break;
      }
      case eStyleUnit_Coord: {
        css::BasicCoordCalcOps ops;
        result.SetCoordValue(ops.MergeMultiplicativeR(
          aCalcFunction, aValue1.GetCoordValue(), aValue2));
        break;
      }
      default:
        NS_ABORT_IF_FALSE(aValue1.IsCalcUnit(), "unexpected unit");
        nsStyleCoord::Array *array =
          nsStyleCoord::Array::Create(mStyleContext, mCanStoreInRuleTree, 2);
        array->Item(0) = aValue1;
        array->Item(1).SetFactorValue(aValue2);
        result.SetArrayValue(array, css::ConvertCalcUnit(aCalcFunction));
        break;
    }
    return result;
  }

  result_type ComputeLeafValue(const nsCSSValue& aValue)
  {
    nsStyleCoord result;
    if (aValue.GetUnit() == eCSSUnit_Percent) {
      result.SetPercentValue(aValue.GetPercentValue());
    } else {
      result.SetCoordValue(CalcLength(aValue, mStyleContext, mPresContext,
                                      mCanStoreInRuleTree));
    }
    return result;
  }
};

static void
SpecifiedCalcToComputedCalc(const nsCSSValue& aValue, nsStyleCoord& aCoord, 
                            nsStyleContext* aStyleContext,
                            PRBool& aCanStoreInRuleTree)
{
  SpecifiedToComputedCalcOps ops(aStyleContext, aStyleContext->PresContext(),
                                 aCanStoreInRuleTree);
  aCoord = ComputeCalc(aValue, ops);
  if (!aCoord.IsCalcUnit()) {
    // Some callers distinguish between calc(50%) and 50%, or calc(50px)
    // and 50px.
    nsStyleCoord::Array *array =
      nsStyleCoord::Array::Create(aStyleContext, aCanStoreInRuleTree, 1);
    array->Item(0) = aCoord;
    aCoord.SetArrayValue(array, eStyleUnit_Calc);
  }
}

struct ComputeComputedCalcCalcOps : public css::StyleCoordInputCalcOps,
                                    public css::BasicCoordCalcOps
{
  const nscoord mPercentageBasis;

  ComputeComputedCalcCalcOps(nscoord aPercentageBasis)
    : mPercentageBasis(aPercentageBasis)
  {
  }

  result_type ComputeLeafValue(const nsStyleCoord& aValue)
  {
    nscoord result;
    if (aValue.GetUnit() == eStyleUnit_Percent) {
      result =
        NSToCoordFloorClamped(mPercentageBasis * aValue.GetPercentValue());
    } else {
      result = aValue.GetCoordValue();
    }
    return result;
  }
};

// This is our public API for handling calc() expressions that involve
// percentages.
/* static */ nscoord
nsRuleNode::ComputeComputedCalc(const nsStyleCoord& aValue,
                                nscoord aPercentageBasis)
{
  ComputeComputedCalcCalcOps ops(aPercentageBasis);
  return css::ComputeCalc(aValue, ops);
}

/* Given an enumerated value that represents a box position, converts it to
 * a float representing the percentage of the box it corresponds to.  For
 * example, "center" becomes 0.5f.
 *
 * @param aEnumValue The enumerated value.
 * @return The float percent it corresponds to.
 */
static float
GetFloatFromBoxPosition(PRInt32 aEnumValue)
{
  switch (aEnumValue) {
  case NS_STYLE_BG_POSITION_LEFT:
  case NS_STYLE_BG_POSITION_TOP:
    return 0.0f;
  case NS_STYLE_BG_POSITION_RIGHT:
  case NS_STYLE_BG_POSITION_BOTTOM:
    return 1.0f;
  default:
    NS_NOTREACHED("unexpected value");
    // fall through
  case NS_STYLE_BG_POSITION_CENTER:
    return 0.5f;
  }
}

#define SETCOORD_NORMAL                 0x01   // N
#define SETCOORD_AUTO                   0x02   // A
#define SETCOORD_INHERIT                0x04   // H
#define SETCOORD_PERCENT                0x08   // P
#define SETCOORD_FACTOR                 0x10   // F
#define SETCOORD_LENGTH                 0x20   // L
#define SETCOORD_INTEGER                0x40   // I
#define SETCOORD_ENUMERATED             0x80   // E
#define SETCOORD_NONE                   0x100  // O
#define SETCOORD_INITIAL_ZERO           0x200
#define SETCOORD_INITIAL_AUTO           0x400
#define SETCOORD_INITIAL_NONE           0x800
#define SETCOORD_INITIAL_NORMAL         0x1000
#define SETCOORD_INITIAL_HALF           0x2000
#define SETCOORD_CALC_LENGTH_ONLY       0x4000
#define SETCOORD_CALC_CLAMP_NONNEGATIVE 0x8000 // modifier for CALC_LENGTH_ONLY
#define SETCOORD_STORE_CALC             0x00010000
#define SETCOORD_BOX_POSITION           0x00020000 // exclusive with _ENUMERATED

#define SETCOORD_LP     (SETCOORD_LENGTH | SETCOORD_PERCENT)
#define SETCOORD_LH     (SETCOORD_LENGTH | SETCOORD_INHERIT)
#define SETCOORD_AH     (SETCOORD_AUTO | SETCOORD_INHERIT)
#define SETCOORD_LAH    (SETCOORD_AUTO | SETCOORD_LENGTH | SETCOORD_INHERIT)
#define SETCOORD_LPH    (SETCOORD_LP | SETCOORD_INHERIT)
#define SETCOORD_LPAH   (SETCOORD_LP | SETCOORD_AH)
#define SETCOORD_LPEH   (SETCOORD_LP | SETCOORD_ENUMERATED | SETCOORD_INHERIT)
#define SETCOORD_LPAEH  (SETCOORD_LPAH | SETCOORD_ENUMERATED)
#define SETCOORD_LPO    (SETCOORD_LP | SETCOORD_NONE)
#define SETCOORD_LPOH   (SETCOORD_LPH | SETCOORD_NONE)
#define SETCOORD_LPOEH  (SETCOORD_LPOH | SETCOORD_ENUMERATED)
#define SETCOORD_LE     (SETCOORD_LENGTH | SETCOORD_ENUMERATED)
#define SETCOORD_LEH    (SETCOORD_LE | SETCOORD_INHERIT)
#define SETCOORD_IA     (SETCOORD_INTEGER | SETCOORD_AUTO)
#define SETCOORD_LAE    (SETCOORD_LENGTH | SETCOORD_AUTO | SETCOORD_ENUMERATED)

// changes aCoord iff it returns PR_TRUE
static PRBool SetCoord(const nsCSSValue& aValue, nsStyleCoord& aCoord,
                       const nsStyleCoord& aParentCoord,
                       PRInt32 aMask, nsStyleContext* aStyleContext,
                       nsPresContext* aPresContext,
                       PRBool& aCanStoreInRuleTree)
{
  PRBool  result = PR_TRUE;
  if (aValue.GetUnit() == eCSSUnit_Null) {
    result = PR_FALSE;
  }
  else if ((((aMask & SETCOORD_LENGTH) != 0) &&
            aValue.IsLengthUnit()) ||
           (((aMask & SETCOORD_CALC_LENGTH_ONLY) != 0) &&
            aValue.IsCalcUnit())) {
    nscoord len = CalcLength(aValue, aStyleContext, aPresContext,
                             aCanStoreInRuleTree);
    if ((aMask & SETCOORD_CALC_CLAMP_NONNEGATIVE) && len < 0) {
      NS_ASSERTION(aValue.IsCalcUnit(),
                   "parser should have ensured no nonnegative lengths");
      len = 0;
    }
    aCoord.SetCoordValue(len);
  }
  else if (((aMask & SETCOORD_PERCENT) != 0) &&
           (aValue.GetUnit() == eCSSUnit_Percent)) {
    aCoord.SetPercentValue(aValue.GetPercentValue());
  }
  else if (((aMask & SETCOORD_INTEGER) != 0) &&
           (aValue.GetUnit() == eCSSUnit_Integer)) {
    aCoord.SetIntValue(aValue.GetIntValue(), eStyleUnit_Integer);
  }
  else if (((aMask & SETCOORD_ENUMERATED) != 0) &&
           (aValue.GetUnit() == eCSSUnit_Enumerated)) {
    aCoord.SetIntValue(aValue.GetIntValue(), eStyleUnit_Enumerated);
  }
  else if (((aMask & SETCOORD_BOX_POSITION) != 0) &&
           (aValue.GetUnit() == eCSSUnit_Enumerated)) {
    aCoord.SetPercentValue(GetFloatFromBoxPosition(aValue.GetIntValue()));
  }
  else if (((aMask & SETCOORD_AUTO) != 0) &&
           (aValue.GetUnit() == eCSSUnit_Auto)) {
    aCoord.SetAutoValue();
  }
  else if (((aMask & SETCOORD_INHERIT) != 0) &&
           (aValue.GetUnit() == eCSSUnit_Inherit)) {
    aCoord = aParentCoord;  // just inherit value from parent
    aCanStoreInRuleTree = PR_FALSE;
  }
  else if (((aMask & SETCOORD_NORMAL) != 0) &&
           (aValue.GetUnit() == eCSSUnit_Normal)) {
    aCoord.SetNormalValue();
  }
  else if (((aMask & SETCOORD_NONE) != 0) &&
           (aValue.GetUnit() == eCSSUnit_None)) {
    aCoord.SetNoneValue();
  }
  else if (((aMask & SETCOORD_FACTOR) != 0) &&
           (aValue.GetUnit() == eCSSUnit_Number)) {
    aCoord.SetFactorValue(aValue.GetFloatValue());
  }
  else if (((aMask & SETCOORD_INITIAL_AUTO) != 0) &&
           (aValue.GetUnit() == eCSSUnit_Initial)) {
    aCoord.SetAutoValue();
  }
  else if (((aMask & SETCOORD_INITIAL_ZERO) != 0) &&
           (aValue.GetUnit() == eCSSUnit_Initial)) {
    aCoord.SetCoordValue(0);
  }
  else if (((aMask & SETCOORD_INITIAL_NONE) != 0) &&
           (aValue.GetUnit() == eCSSUnit_Initial)) {
    aCoord.SetNoneValue();
  }
  else if (((aMask & SETCOORD_INITIAL_NORMAL) != 0) &&
           (aValue.GetUnit() == eCSSUnit_Initial)) {
    aCoord.SetNormalValue();
  }
  else if (((aMask & SETCOORD_INITIAL_HALF) != 0) &&
           (aValue.GetUnit() == eCSSUnit_Initial)) {
    aCoord.SetPercentValue(0.5f);
  }
  else if (((aMask & SETCOORD_STORE_CALC) != 0) &&
           (aValue.IsCalcUnit())) {
    if (aValue.GetUnit() == eCSSUnit_Calc) {
      // Don't copy the extra Calc node at top-level.
      nsCSSValue::Array *array = aValue.GetArrayValue();
      NS_ABORT_IF_FALSE(array->Count() == 1, "unexpected count");
      SpecifiedCalcToComputedCalc(array->Item(0), aCoord, aStyleContext,
                                  aCanStoreInRuleTree);
    } else {
      SpecifiedCalcToComputedCalc(aValue, aCoord, aStyleContext,
                                  aCanStoreInRuleTree);
    }
  }
  else {
    result = PR_FALSE;  // didn't set anything
  }
  return result;
}

// This inline function offers a shortcut for SetCoord() by refusing to accept
// SETCOORD_LENGTH and SETCOORD_INHERIT masks.
static inline PRBool SetAbsCoord(const nsCSSValue& aValue,
                                 nsStyleCoord& aCoord,
                                 PRInt32 aMask)
{
  NS_ABORT_IF_FALSE((aMask & SETCOORD_LH) == 0,
                    "does not handle SETCOORD_LENGTH and SETCOORD_INHERIT");

  // The values of the following variables will never be used; so it does not
  // matter what to set.
  const nsStyleCoord dummyParentCoord;
  nsStyleContext* dummyStyleContext = nsnull;
  nsPresContext* dummyPresContext = nsnull;
  PRBool dummyCanStoreInRuleTree = PR_TRUE;

  PRBool rv = SetCoord(aValue, aCoord, dummyParentCoord, aMask,
                       dummyStyleContext, dummyPresContext,
                       dummyCanStoreInRuleTree);
  NS_ABORT_IF_FALSE(dummyCanStoreInRuleTree,
                    "SetCoord() should not modify dummyCanStoreInRuleTree.");

  return rv;
}

/* Given a specified value that might be a pair value, call SetCoord twice,
 * either using each member of the pair, or using the unpaired value twice.
 */
static PRBool
SetPairCoords(const nsCSSValue& aValue,
              nsStyleCoord& aCoordX, nsStyleCoord& aCoordY,
              const nsStyleCoord& aParentX, const nsStyleCoord& aParentY,
              PRInt32 aMask, nsStyleContext* aStyleContext,
              nsPresContext* aPresContext, PRBool& aCanStoreInRuleTree)
{
  const nsCSSValue& valX =
    aValue.GetUnit() == eCSSUnit_Pair ? aValue.GetPairValue().mXValue : aValue;
  const nsCSSValue& valY =
    aValue.GetUnit() == eCSSUnit_Pair ? aValue.GetPairValue().mYValue : aValue;

  PRBool cX = SetCoord(valX, aCoordX, aParentX, aMask, aStyleContext,
                       aPresContext, aCanStoreInRuleTree);
  PRBool cY = SetCoord(valY, aCoordY, aParentY, aMask, aStyleContext,
                       aPresContext, aCanStoreInRuleTree);
  NS_ABORT_IF_FALSE(cX == cY, "changed one but not the other");
  return cX;
}

static PRBool SetColor(const nsCSSValue& aValue, const nscolor aParentColor,
                       nsPresContext* aPresContext, nsStyleContext *aContext,
                       nscolor& aResult, PRBool& aCanStoreInRuleTree)
{
  PRBool  result = PR_FALSE;
  nsCSSUnit unit = aValue.GetUnit();

  if (eCSSUnit_Color == unit) {
    aResult = aValue.GetColorValue();
    result = PR_TRUE;
  }
  else if (eCSSUnit_Ident == unit) {
    nsAutoString  value;
    aValue.GetStringValue(value);
    nscolor rgba;
    if (NS_ColorNameToRGB(value, &rgba)) {
      aResult = rgba;
      result = PR_TRUE;
    }
  }
  else if (eCSSUnit_EnumColor == unit) {
    PRInt32 intValue = aValue.GetIntValue();
    if (0 <= intValue) {
      nsILookAndFeel* look = aPresContext->LookAndFeel();
      nsILookAndFeel::nsColorID colorID = (nsILookAndFeel::nsColorID) intValue;
      if (NS_SUCCEEDED(look->GetColor(colorID, aResult))) {
        result = PR_TRUE;
      }
    }
    else {
      switch (intValue) {
        case NS_COLOR_MOZ_HYPERLINKTEXT:
          aResult = aPresContext->DefaultLinkColor();
          break;
        case NS_COLOR_MOZ_VISITEDHYPERLINKTEXT:
          aResult = aPresContext->DefaultVisitedLinkColor();
          break;
        case NS_COLOR_MOZ_ACTIVEHYPERLINKTEXT:
          aResult = aPresContext->DefaultActiveLinkColor();
          break;
        case NS_COLOR_CURRENTCOLOR:
          // The data computed from this can't be shared in the rule tree
          // because they could be used on a node with a different color
          aCanStoreInRuleTree = PR_FALSE;
          aResult = aContext->GetStyleColor()->mColor;
          break;
        default:
          NS_NOTREACHED("Should never have an unknown negative colorID.");
          break;
      }
      result = PR_TRUE;
    }
  }
  else if (eCSSUnit_Inherit == unit) {
    aResult = aParentColor;
    result = PR_TRUE;
    aCanStoreInRuleTree = PR_FALSE;
  }
  return result;
}

static void SetGradientCoord(const nsCSSValue& aValue, nsPresContext* aPresContext,
                             nsStyleContext* aContext, nsStyleCoord& aResult,
                             PRBool& aCanStoreInRuleTree)
{
  // OK to pass bad aParentCoord since we're not passing SETCOORD_INHERIT
  if (!SetCoord(aValue, aResult, nsStyleCoord(),
                SETCOORD_LPO | SETCOORD_BOX_POSITION,
                aContext, aPresContext, aCanStoreInRuleTree)) {
    NS_NOTREACHED("unexpected unit for gradient anchor point");
    aResult.SetNoneValue();
  }
}

static void SetGradient(const nsCSSValue& aValue, nsPresContext* aPresContext,
                        nsStyleContext* aContext, nsStyleGradient& aResult,
                        PRBool& aCanStoreInRuleTree)
{
  NS_ABORT_IF_FALSE(aValue.GetUnit() == eCSSUnit_Gradient,
                    "The given data is not a gradient");

  nsCSSValueGradient* gradient = aValue.GetGradientValue();

  if (gradient->mIsRadial) {
    if (gradient->mRadialShape.GetUnit() == eCSSUnit_Enumerated) {
      aResult.mShape = gradient->mRadialShape.GetIntValue();
    } else {
      NS_ASSERTION(gradient->mRadialShape.GetUnit() == eCSSUnit_None,
                   "bad unit for radial shape");
      aResult.mShape = NS_STYLE_GRADIENT_SHAPE_ELLIPTICAL;
    }
    if (gradient->mRadialSize.GetUnit() == eCSSUnit_Enumerated) {
      aResult.mSize = gradient->mRadialSize.GetIntValue();
    } else {
      NS_ASSERTION(gradient->mRadialSize.GetUnit() == eCSSUnit_None,
                   "bad unit for radial shape");
      aResult.mSize = NS_STYLE_GRADIENT_SIZE_FARTHEST_CORNER;
    }
  } else {
    NS_ASSERTION(gradient->mRadialShape.GetUnit() == eCSSUnit_None,
                 "bad unit for linear shape");
    NS_ASSERTION(gradient->mRadialSize.GetUnit() == eCSSUnit_None,
                 "bad unit for linear size");
    aResult.mShape = NS_STYLE_GRADIENT_SHAPE_LINEAR;
    aResult.mSize = NS_STYLE_GRADIENT_SIZE_FARTHEST_CORNER;
  }

  // bg-position
  SetGradientCoord(gradient->mBgPos.mXValue, aPresContext, aContext,
                   aResult.mBgPosX, aCanStoreInRuleTree);

  SetGradientCoord(gradient->mBgPos.mYValue, aPresContext, aContext,
                   aResult.mBgPosY, aCanStoreInRuleTree);

  aResult.mRepeating = gradient->mIsRepeating;

  // angle
  if (gradient->mAngle.IsAngularUnit()) {
    nsStyleUnit unit;
    switch (gradient->mAngle.GetUnit()) {
    case eCSSUnit_Degree: unit = eStyleUnit_Degree; break;
    case eCSSUnit_Grad:   unit = eStyleUnit_Grad; break;
    case eCSSUnit_Radian: unit = eStyleUnit_Radian; break;
    default: NS_NOTREACHED("unrecognized angular unit");
      unit = eStyleUnit_Degree;
    }
    aResult.mAngle.SetAngleValue(gradient->mAngle.GetAngleValue(), unit);
  } else {
    NS_ASSERTION(gradient->mAngle.GetUnit() == eCSSUnit_None,
                 "bad unit for gradient angle");
    aResult.mAngle.SetNoneValue();
  }

  // stops
  for (PRUint32 i = 0; i < gradient->mStops.Length(); i++) {
    nsStyleGradientStop stop;
    nsCSSValueGradientStop &valueStop = gradient->mStops[i];

    if (!SetCoord(valueStop.mLocation, stop.mLocation,
                  nsStyleCoord(), SETCOORD_LPO,
                  aContext, aPresContext, aCanStoreInRuleTree)) {
      NS_NOTREACHED("unexpected unit for gradient stop location");
    }

    // inherit is not a valid color for stops, so we pass in a dummy
    // parent color
    NS_ASSERTION(valueStop.mColor.GetUnit() != eCSSUnit_Inherit,
                 "inherit is not a valid color for gradient stops");
    SetColor(valueStop.mColor, NS_RGB(0, 0, 0), aPresContext,
             aContext, stop.mColor, aCanStoreInRuleTree);

    aResult.mStops.AppendElement(stop);
  }
}

// -moz-image-rect(<uri>, <top>, <right>, <bottom>, <left>)
static void SetStyleImageToImageRect(nsStyleContext* aStyleContext,
                                     const nsCSSValue& aValue,
                                     nsStyleImage& aResult)
{
  NS_ABORT_IF_FALSE(aValue.GetUnit() == eCSSUnit_Function &&
                    aValue.EqualsFunction(eCSSKeyword__moz_image_rect),
                    "the value is not valid -moz-image-rect()");

  nsCSSValue::Array* arr = aValue.GetArrayValue();
  NS_ABORT_IF_FALSE(arr && arr->Count() == 6, "invalid number of arguments");

  // <uri>
  if (arr->Item(1).GetUnit() == eCSSUnit_Image) {
    NS_SET_IMAGE_REQUEST(aResult.SetImageData,
                         aStyleContext,
                         arr->Item(1).GetImageValue())
  } else {
    NS_WARNING("nsCSSValue::Image::Image() failed?");
  }

  // <top>, <right>, <bottom>, <left>
  nsStyleSides cropRect;
  NS_FOR_CSS_SIDES(side) {
    nsStyleCoord coord;
    const nsCSSValue& val = arr->Item(2 + side);
    PRBool unitOk = SetAbsCoord(val, coord, SETCOORD_FACTOR | SETCOORD_PERCENT);
    NS_ABORT_IF_FALSE(unitOk, "Incorrect data structure created by CSS parser");
    cropRect.Set(side, coord);
  }
  aResult.SetCropRect(&cropRect);
}

static void SetStyleImage(nsStyleContext* aStyleContext,
                          const nsCSSValue& aValue,
                          nsStyleImage& aResult,
                          PRBool& aCanStoreInRuleTree)
{
  aResult.SetNull();

  switch (aValue.GetUnit()) {
    case eCSSUnit_Image:
      NS_SET_IMAGE_REQUEST(aResult.SetImageData,
                           aStyleContext,
                           aValue.GetImageValue())
      break;
    case eCSSUnit_Function:
      if (aValue.EqualsFunction(eCSSKeyword__moz_image_rect)) {
        SetStyleImageToImageRect(aStyleContext, aValue, aResult);
      } else {
        NS_NOTREACHED("-moz-image-rect() is the only expected function");
      }
      break;
    case eCSSUnit_Gradient:
    {
      nsStyleGradient* gradient = new nsStyleGradient();
      if (gradient) {
        SetGradient(aValue, aStyleContext->PresContext(), aStyleContext,
                    *gradient, aCanStoreInRuleTree);
        aResult.SetGradientData(gradient);
      }
      break;
    }
    case eCSSUnit_Element:
      aResult.SetElementId(aValue.GetStringBufferValue());
      break;
    case eCSSUnit_None:
      break;
    default:
      // We might have eCSSUnit_URL values for if-visited style
      // contexts, which we can safely treat like 'none'.  Otherwise
      // this is an unexpected unit.
      NS_ASSERTION(aStyleContext->IsStyleIfVisited() &&
                   aValue.GetUnit() == eCSSUnit_URL,
                   "unexpected unit; maybe nsCSSValue::Image::Image() failed?");
      break;
  }
}

// flags for SetDiscrete - align values with SETCOORD_* constants
// where possible

#define SETDSC_NORMAL                 0x01   // N
#define SETDSC_AUTO                   0x02   // A
#define SETDSC_INTEGER                0x40   // I
#define SETDSC_ENUMERATED             0x80   // E
#define SETDSC_NONE                   0x100  // O
#define SETDSC_SYSTEM_FONT            0x2000

// no caller cares whether aField was changed or not
template <typename FieldT,
          typename T1, typename T2, typename T3, typename T4, typename T5>
static void
SetDiscrete(const nsCSSValue& aValue, FieldT & aField,
            PRBool& aCanStoreInRuleTree, PRUint32 aMask,
            FieldT aParentValue,
            T1 aInitialValue,
            T2 aAutoValue,
            T3 aNoneValue,
            T4 aNormalValue,
            T5 aSystemFontValue)
{
  switch (aValue.GetUnit()) {
  case eCSSUnit_Null:
    return;

    // every caller of SetDiscrete provides inherit and initial
    // alternatives, so we don't require them to say so in the mask
  case eCSSUnit_Inherit:
    aCanStoreInRuleTree = PR_FALSE;
    aField = aParentValue;
    return;

  case eCSSUnit_Initial:
    aField = aInitialValue;
    return;

    // every caller provides one or other of these alternatives,
    // but they have to say which
  case eCSSUnit_Enumerated:
    if (aMask & SETDSC_ENUMERATED) {
      aField = aValue.GetIntValue();
      return;
    }
    break;

  case eCSSUnit_Integer:
    if (aMask & SETDSC_INTEGER) {
      aField = aValue.GetIntValue();
      return;
    }
    break;

    // remaining possibilities in descending order of frequency of use
  case eCSSUnit_Auto:
    if (aMask & SETDSC_AUTO) {
      aField = aAutoValue;
      return;
    }
    break;

  case eCSSUnit_None:
    if (aMask & SETDSC_NONE) {
      aField = aNoneValue;
      return;
    }
    break;

  case eCSSUnit_Normal:
    if (aMask & SETDSC_NORMAL) {
      aField = aNormalValue;
      return;
    }
    break;

  case eCSSUnit_System_Font:
    if (aMask & SETDSC_SYSTEM_FONT) {
      aField = aSystemFontValue;
      return;
    }
    break;

  default:
    break;
  }

  NS_NOTREACHED("SetDiscrete: inappropriate unit");
}

// flags for SetFactor
#define SETFCT_POSITIVE 0x01        // assert value is >= 0.0f
#define SETFCT_OPACITY  0x02        // clamp value to [0.0f .. 1.0f]
#define SETFCT_NONE     0x04        // allow _None (uses aInitialValue).

static void
SetFactor(const nsCSSValue& aValue, float& aField, PRBool& aCanStoreInRuleTree,
          float aParentValue, float aInitialValue, PRUint32 aFlags = 0)
{
  switch (aValue.GetUnit()) {
  case eCSSUnit_Null:
    return;

  case eCSSUnit_Number:
    aField = aValue.GetFloatValue();
    if (aFlags & SETFCT_POSITIVE) {
      NS_ASSERTION(aField >= 0.0f, "negative value for positive-only property");
      if (aField < 0.0f)
        aField = 0.0f;
    }
    if (aFlags & SETFCT_OPACITY) {
      if (aField < 0.0f)
        aField = 0.0f;
      if (aField > 1.0f)
        aField = 1.0f;
    }
    return;

  case eCSSUnit_Inherit:
    aCanStoreInRuleTree = PR_FALSE;
    aField = aParentValue;
    return;

  case eCSSUnit_Initial:
    aField = aInitialValue;
    return;

  case eCSSUnit_None:
    if (aFlags & SETFCT_NONE) {
      aField = aInitialValue;
      return;
    }
    break;

  default:
    break;
  }

  NS_NOTREACHED("SetFactor: inappropriate unit");
}

// Overloaded new operator. Initializes the memory to 0 and relies on an arena
// (which comes from the presShell) to perform the allocation.
void*
nsRuleNode::operator new(size_t sz, nsPresContext* aPresContext) CPP_THROW_NEW
{
  // Check the recycle list first.
  return aPresContext->AllocateFromShell(sz);
}

/* static */ PLDHashOperator
nsRuleNode::EnqueueRuleNodeChildren(PLDHashTable *table, PLDHashEntryHdr *hdr,
                                    PRUint32 number, void *arg)
{
  ChildrenHashEntry *entry = static_cast<ChildrenHashEntry*>(hdr);
  nsRuleNode ***destroyQueueTail = static_cast<nsRuleNode***>(arg);
  **destroyQueueTail = entry->mRuleNode;
  *destroyQueueTail = &entry->mRuleNode->mNextSibling;
  return PL_DHASH_NEXT;
}

// Overridden to prevent the global delete from being called, since the memory
// came out of an nsIArena instead of the global delete operator's heap.
void
nsRuleNode::DestroyInternal(nsRuleNode ***aDestroyQueueTail)
{
  nsRuleNode *destroyQueue, **destroyQueueTail;
  if (aDestroyQueueTail) {
    destroyQueueTail = *aDestroyQueueTail;
  } else {
    destroyQueue = nsnull;
    destroyQueueTail = &destroyQueue;
  }

  if (ChildrenAreHashed()) {
    PLDHashTable *children = ChildrenHash();
    PL_DHashTableEnumerate(children, EnqueueRuleNodeChildren,
                           &destroyQueueTail);
    *destroyQueueTail = nsnull; // ensure null-termination
    PL_DHashTableDestroy(children);
  } else if (HaveChildren()) {
    *destroyQueueTail = ChildrenList();
    do {
      destroyQueueTail = &(*destroyQueueTail)->mNextSibling;
    } while (*destroyQueueTail);
  }
  mChildren.asVoid = nsnull;

  if (aDestroyQueueTail) {
    // Our caller destroys the queue.
    *aDestroyQueueTail = destroyQueueTail;
  } else {
    // We have to do destroy the queue.  When we destroy each node, it
    // will add its children to the queue.
    while (destroyQueue) {
      nsRuleNode *cur = destroyQueue;
      destroyQueue = destroyQueue->mNextSibling;
      if (!destroyQueue) {
        NS_ASSERTION(destroyQueueTail == &cur->mNextSibling, "mangled list");
        destroyQueueTail = &destroyQueue;
      }
      cur->DestroyInternal(&destroyQueueTail);
    }
  }

  // Destroy ourselves.
  this->~nsRuleNode();

  // Don't let the memory be freed, since it will be recycled
  // instead. Don't call the global operator delete.
  mPresContext->FreeToShell(sizeof(nsRuleNode), this);
}

nsRuleNode* nsRuleNode::CreateRootNode(nsPresContext* aPresContext)
{
  return new (aPresContext)
    nsRuleNode(aPresContext, nsnull, nsnull, 0xff, PR_FALSE);
}

nsILanguageAtomService* nsRuleNode::gLangService = nsnull;

nsRuleNode::nsRuleNode(nsPresContext* aContext, nsRuleNode* aParent,
                       nsIStyleRule* aRule, PRUint8 aLevel,
                       PRBool aIsImportant)
  : mPresContext(aContext),
    mParent(aParent),
    mRule(aRule),
    mDependentBits((PRUint32(aLevel) << NS_RULE_NODE_LEVEL_SHIFT) |
                   (aIsImportant ? NS_RULE_NODE_IS_IMPORTANT : 0)),
    mNoneBits(0),
    mRefCnt(0)
{
  mChildren.asVoid = nsnull;
  MOZ_COUNT_CTOR(nsRuleNode);
  NS_IF_ADDREF(mRule);

  NS_ASSERTION(IsRoot() || GetLevel() == aLevel, "not enough bits");
  NS_ASSERTION(IsRoot() || IsImportantRule() == aIsImportant, "yikes");
  /* If IsRoot(), then aContext->StyleSet() is typically null at this
     point.  In any case, we don't want to treat the root rulenode as
     unused.  */
  if (!IsRoot()) {
    mParent->AddRef();
    aContext->StyleSet()->RuleNodeUnused();
  }
}

nsRuleNode::~nsRuleNode()
{
  MOZ_COUNT_DTOR(nsRuleNode);
  if (mStyleData.mResetData || mStyleData.mInheritedData)
    mStyleData.Destroy(0, mPresContext);
  NS_IF_RELEASE(mRule);
}

nsRuleNode*
nsRuleNode::Transition(nsIStyleRule* aRule, PRUint8 aLevel,
                       PRPackedBool aIsImportantRule)
{
  nsRuleNode* next = nsnull;
  nsRuleNode::Key key(aRule, aLevel, aIsImportantRule);

  if (HaveChildren() && !ChildrenAreHashed()) {
    PRInt32 numKids = 0;
    nsRuleNode* curr = ChildrenList();
    while (curr && curr->GetKey() != key) {
      curr = curr->mNextSibling;
      ++numKids;
    }
    if (curr)
      next = curr;
    else if (numKids >= kMaxChildrenInList)
      ConvertChildrenToHash();
  }

  if (ChildrenAreHashed()) {
    ChildrenHashEntry *entry = static_cast<ChildrenHashEntry*>
                                          (PL_DHashTableOperate(ChildrenHash(), &key, PL_DHASH_ADD));
    if (!entry) {
      NS_WARNING("out of memory");
      return this;
    }
    if (entry->mRuleNode)
      next = entry->mRuleNode;
    else {
      next = entry->mRuleNode = new (mPresContext)
        nsRuleNode(mPresContext, this, aRule, aLevel, aIsImportantRule);
      if (!next) {
        PL_DHashTableRawRemove(ChildrenHash(), entry);
        NS_WARNING("out of memory");
        return this;
      }
    }
  } else if (!next) {
    // Create the new entry in our list.
    next = new (mPresContext)
      nsRuleNode(mPresContext, this, aRule, aLevel, aIsImportantRule);
    if (!next) {
      NS_WARNING("out of memory");
      return this;
    }
    next->mNextSibling = ChildrenList();
    SetChildrenList(next);
  }

  return next;
}

void
nsRuleNode::ConvertChildrenToHash()
{
  NS_ASSERTION(!ChildrenAreHashed() && HaveChildren(),
               "must have a non-empty list of children");
  PLDHashTable *hash = PL_NewDHashTable(&ChildrenHashOps, nsnull,
                                        sizeof(ChildrenHashEntry),
                                        kMaxChildrenInList * 4);
  if (!hash)
    return;
  for (nsRuleNode* curr = ChildrenList(); curr; curr = curr->mNextSibling) {
    // This will never fail because of the initial size we gave the table.
    ChildrenHashEntry *entry = static_cast<ChildrenHashEntry*>(
      PL_DHashTableOperate(hash, curr->mRule, PL_DHASH_ADD));
    NS_ASSERTION(!entry->mRuleNode, "duplicate entries in list");
    entry->mRuleNode = curr;
  }
  SetChildrenHash(hash);
}

inline void
nsRuleNode::PropagateNoneBit(PRUint32 aBit, nsRuleNode* aHighestNode)
{
  nsRuleNode* curr = this;
  for (;;) {
    NS_ASSERTION(!(curr->mNoneBits & aBit), "propagating too far");
    curr->mNoneBits |= aBit;
    if (curr == aHighestNode)
      break;
    curr = curr->mParent;
  }
}

inline void
nsRuleNode::PropagateDependentBit(PRUint32 aBit, nsRuleNode* aHighestNode)
{
  for (nsRuleNode* curr = this; curr != aHighestNode; curr = curr->mParent) {
    if (curr->mDependentBits & aBit) {
#ifdef DEBUG
      while (curr != aHighestNode) {
        NS_ASSERTION(curr->mDependentBits & aBit, "bit not set");
        curr = curr->mParent;
      }
#endif
      break;
    }

    curr->mDependentBits |= aBit;
  }
}

/*
 * The following "Check" functions are used for determining what type of
 * sharing can be used for the data on this rule node.  MORE HERE...
 */

/* the information for a property (or in some cases, a rect group of
   properties) */

struct PropertyCheckData {
  size_t offset;
  // These duplicate the data in nsCSSProps::kFlagsTable, except that
  // we have some extra entries for CSS_PROP_INCLUDE_NOT_CSS.
  PRUint32 flags;
};

/*
 * a callback function that that can revise the result of
 * CheckSpecifiedProperties before finishing; aResult is the current
 * result, and it returns the revised one.
 */
typedef nsRuleNode::RuleDetail
  (* CheckCallbackFn)(const nsRuleDataStruct& aData,
                      nsRuleNode::RuleDetail aResult);

/* the information for all the properties in a style struct */
struct StructCheckData {
  const PropertyCheckData* props;
  PRInt32 nprops;
  CheckCallbackFn callback;
};


/**
 * @param aValue the value being examined
 * @param aSpecifiedCount to be incremented by one if the value is specified
 * @param aInherited to be incremented by one if the value is set to inherit
 */
inline void
ExamineCSSValue(const nsCSSValue& aValue,
                PRUint32& aSpecifiedCount, PRUint32& aInheritedCount)
{
  if (aValue.GetUnit() != eCSSUnit_Null) {
    ++aSpecifiedCount;
    if (aValue.GetUnit() == eCSSUnit_Inherit) {
      ++aInheritedCount;
    }
  }
}

static nsRuleNode::RuleDetail
CheckFontCallback(const nsRuleDataStruct& aData,
                  nsRuleNode::RuleDetail aResult)
{
  const nsRuleDataFont& fontData =
      static_cast<const nsRuleDataFont&>(aData);

  // em, ex, percent, 'larger', and 'smaller' values on font-size depend
  // on the parent context's font-size
  // Likewise, 'lighter' and 'bolder' values of 'font-weight', and 'wider'
  // and 'narrower' values of 'font-stretch' depend on the parent.
  const nsCSSValue& size = fontData.mSize;
  const nsCSSValue& weight = fontData.mWeight;
  const nsCSSValue& stretch = fontData.mStretch;
  if (size.IsRelativeLengthUnit() ||
      size.GetUnit() == eCSSUnit_Percent ||
      (size.GetUnit() == eCSSUnit_Enumerated &&
       (size.GetIntValue() == NS_STYLE_FONT_SIZE_SMALLER ||
        size.GetIntValue() == NS_STYLE_FONT_SIZE_LARGER)) ||
#ifdef MOZ_MATHML
      fontData.mScriptLevel.GetUnit() == eCSSUnit_Integer ||
#endif
      (stretch.GetUnit() == eCSSUnit_Enumerated &&
       (stretch.GetIntValue() == NS_FONT_STRETCH_NARROWER ||
        stretch.GetIntValue() == NS_FONT_STRETCH_WIDER)) ||
      (weight.GetUnit() == eCSSUnit_Enumerated &&
       (weight.GetIntValue() == NS_STYLE_FONT_WEIGHT_BOLDER ||
        weight.GetIntValue() == NS_STYLE_FONT_WEIGHT_LIGHTER))) {
    NS_ASSERTION(aResult == nsRuleNode::eRulePartialReset ||
                 aResult == nsRuleNode::eRuleFullReset ||
                 aResult == nsRuleNode::eRulePartialMixed ||
                 aResult == nsRuleNode::eRuleFullMixed,
                 "we know we already have a reset-counted property");
    // Promote reset to mixed since we have something that depends on
    // the parent.  But never promote to inherited since that could
    // cause inheritance of the exact value.
    if (aResult == nsRuleNode::eRulePartialReset)
      aResult = nsRuleNode::eRulePartialMixed;
    else if (aResult == nsRuleNode::eRuleFullReset)
      aResult = nsRuleNode::eRuleFullMixed;
  }

  return aResult;
}

static nsRuleNode::RuleDetail
CheckColorCallback(const nsRuleDataStruct& aData,
                   nsRuleNode::RuleDetail aResult)
{
  const nsRuleDataColor& colorData =
      static_cast<const nsRuleDataColor&>(aData);

  // currentColor values for color require inheritance
  if (colorData.mColor.GetUnit() == eCSSUnit_EnumColor &&
      colorData.mColor.GetIntValue() == NS_COLOR_CURRENTCOLOR) {
    NS_ASSERTION(aResult == nsRuleNode::eRuleFullReset,
                 "we should already be counted as full-reset");
    aResult = nsRuleNode::eRuleFullInherited;
  }

  return aResult;
}

static nsRuleNode::RuleDetail
CheckTextCallback(const nsRuleDataStruct& aData,
                  nsRuleNode::RuleDetail aResult)
{
  const nsRuleDataText& textData =
    static_cast<const nsRuleDataText&>(aData);

  if (textData.mTextAlign.GetUnit() == eCSSUnit_Enumerated &&
      textData.mTextAlign.GetIntValue() ==
        NS_STYLE_TEXT_ALIGN_MOZ_CENTER_OR_INHERIT) {
    // Promote reset to mixed since we have something that depends on
    // the parent.
    if (aResult == nsRuleNode::eRulePartialReset)
      aResult = nsRuleNode::eRulePartialMixed;
    else if (aResult == nsRuleNode::eRuleFullReset)
      aResult = nsRuleNode::eRuleFullMixed;
  }

  return aResult;
}

// for nsCSSPropList.h, so we get information on things in the style
// structs but not nsCSS*
#define CSS_PROP_INCLUDE_NOT_CSS

#define CHECK_DATA_FOR_PROPERTY(name_, id_, method_, flags_, datastruct_, member_, kwtable_, stylestructoffset_, animtype_) \
  { offsetof(nsRuleData##datastruct_, member_), flags_ },

static const PropertyCheckData FontCheckProperties[] = {
#define CSS_PROP_FONT CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_FONT
};

static const PropertyCheckData DisplayCheckProperties[] = {
#define CSS_PROP_DISPLAY CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_DISPLAY
};

static const PropertyCheckData VisibilityCheckProperties[] = {
#define CSS_PROP_VISIBILITY CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_VISIBILITY
};

static const PropertyCheckData MarginCheckProperties[] = {
#define CSS_PROP_MARGIN CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_MARGIN
};

static const PropertyCheckData BorderCheckProperties[] = {
#define CSS_PROP_BORDER CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_BORDER
};

static const PropertyCheckData PaddingCheckProperties[] = {
#define CSS_PROP_PADDING CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_PADDING
};

static const PropertyCheckData OutlineCheckProperties[] = {
#define CSS_PROP_OUTLINE CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_OUTLINE
};

static const PropertyCheckData ListCheckProperties[] = {
#define CSS_PROP_LIST CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_LIST
};

static const PropertyCheckData ColorCheckProperties[] = {
#define CSS_PROP_COLOR CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_COLOR
};

static const PropertyCheckData BackgroundCheckProperties[] = {
#define CSS_PROP_BACKGROUND CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_BACKGROUND
};

static const PropertyCheckData PositionCheckProperties[] = {
#define CSS_PROP_POSITION CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_POSITION
};

static const PropertyCheckData TableCheckProperties[] = {
#define CSS_PROP_TABLE CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_TABLE
};

static const PropertyCheckData TableBorderCheckProperties[] = {
#define CSS_PROP_TABLEBORDER CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_TABLEBORDER
};

static const PropertyCheckData ContentCheckProperties[] = {
#define CSS_PROP_CONTENT CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_CONTENT
};

static const PropertyCheckData QuotesCheckProperties[] = {
#define CSS_PROP_QUOTES CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_QUOTES
};

static const PropertyCheckData TextCheckProperties[] = {
#define CSS_PROP_TEXT CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_TEXT
};

static const PropertyCheckData TextResetCheckProperties[] = {
#define CSS_PROP_TEXTRESET CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_TEXTRESET
};

static const PropertyCheckData UserInterfaceCheckProperties[] = {
#define CSS_PROP_USERINTERFACE CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_USERINTERFACE
};

static const PropertyCheckData UIResetCheckProperties[] = {
#define CSS_PROP_UIRESET CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_UIRESET
};

static const PropertyCheckData XULCheckProperties[] = {
#define CSS_PROP_XUL CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_XUL
};

static const PropertyCheckData SVGCheckProperties[] = {
#define CSS_PROP_SVG CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_SVG
};

static const PropertyCheckData SVGResetCheckProperties[] = {
#define CSS_PROP_SVGRESET CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_SVGRESET
};

static const PropertyCheckData ColumnCheckProperties[] = {
#define CSS_PROP_COLUMN CHECK_DATA_FOR_PROPERTY
#include "nsCSSPropList.h"
#undef CSS_PROP_COLUMN
};

#undef CSS_PROP_INCLUDE_NOT_CSS
#undef CHECK_DATA_FOR_PROPERTY

static const StructCheckData gCheckProperties[] = {

#define STYLE_STRUCT(name, checkdata_cb, ctor_args) \
  {name##CheckProperties, \
   sizeof(name##CheckProperties)/sizeof(PropertyCheckData), \
   checkdata_cb},
#include "nsStyleStructList.h"
#undef STYLE_STRUCT
  {nsnull, 0, nsnull}

};



// XXXldb Taking the address of a reference is evil.

inline nsCSSValue&
ValueAtOffset(nsRuleDataStruct& aRuleDataStruct, size_t aOffset)
{
  return * reinterpret_cast<nsCSSValue*>
                           (reinterpret_cast<char*>(&aRuleDataStruct) + aOffset);
}

inline const nsCSSValue&
ValueAtOffset(const nsRuleDataStruct& aRuleDataStruct, size_t aOffset)
{
  return * reinterpret_cast<const nsCSSValue*>
                           (reinterpret_cast<const char*>(&aRuleDataStruct) + aOffset);
}

inline nsCSSRect*
RectAtOffset(nsRuleDataStruct& aRuleDataStruct, size_t aOffset)
{
  return reinterpret_cast<nsCSSRect*>
                         (reinterpret_cast<char*>(&aRuleDataStruct) + aOffset);
}

inline const nsCSSRect*
RectAtOffset(const nsRuleDataStruct& aRuleDataStruct, size_t aOffset)
{
  return reinterpret_cast<const nsCSSRect*>
                         (reinterpret_cast<const char*>(&aRuleDataStruct) + aOffset);
}

inline nsCSSValueList*&
ValueListAtOffset(nsRuleDataStruct& aRuleDataStruct, size_t aOffset)
{
  return * reinterpret_cast<nsCSSValueList**>
                           (reinterpret_cast<char*>(&aRuleDataStruct) + aOffset);
}

inline const nsCSSValueList*
ValueListAtOffset(const nsRuleDataStruct& aRuleDataStruct, size_t aOffset)
{
  return * reinterpret_cast<const nsCSSValueList*const*>
                           (reinterpret_cast<const char*>(&aRuleDataStruct) + aOffset);
}

inline nsCSSValuePairList*&
ValuePairListAtOffset(nsRuleDataStruct& aRuleDataStruct, size_t aOffset)
{
  return * reinterpret_cast<nsCSSValuePairList**>
                           (reinterpret_cast<char*>(&aRuleDataStruct) + aOffset);
}

inline const nsCSSValuePairList*
ValuePairListAtOffset(const nsRuleDataStruct& aRuleDataStruct, size_t aOffset)
{
  return * reinterpret_cast<const nsCSSValuePairList*const*>
                           (reinterpret_cast<const char*>(&aRuleDataStruct) + aOffset);
}

#if defined(MOZ_MATHML) && defined(DEBUG)
static PRBool
AreAllMathMLPropertiesUndefined(const nsCSSFont& aRuleData)
{
  return aRuleData.mScriptLevel.GetUnit() == eCSSUnit_Null &&
         aRuleData.mScriptSizeMultiplier.GetUnit() == eCSSUnit_Null &&
         aRuleData.mScriptMinSize.GetUnit() == eCSSUnit_Null;
}
#endif

inline nsRuleNode::RuleDetail
nsRuleNode::CheckSpecifiedProperties(const nsStyleStructID aSID,
                                     const nsRuleDataStruct& aRuleDataStruct)
{
  const StructCheckData *structData = gCheckProperties + aSID;

  // Build a count of the:
  PRUint32 total = 0,      // total number of props in the struct
           specified = 0,  // number that were specified for this node
           inherited = 0;  // number that were 'inherit' (and not
                           //   eCSSUnit_Inherit) for this node

  for (const PropertyCheckData *prop = structData->props,
                           *prop_end = prop + structData->nprops;
       prop != prop_end;
       ++prop) {

    ++total;
    ExamineCSSValue(ValueAtOffset(aRuleDataStruct, prop->offset),
                    specified, inherited);
  }

#if 0
  printf("CheckSpecifiedProperties: SID=%d total=%d spec=%d inh=%d.\n",
         aSID, total, specified, inherited);
#endif

#ifdef MOZ_MATHML
  NS_ASSERTION(aSID != eStyleStruct_Font ||
               mPresContext->Document()->GetMathMLEnabled() ||
               AreAllMathMLPropertiesUndefined(static_cast<const nsCSSFont&>(aRuleDataStruct)),
               "MathML style property was defined even though MathML is disabled");
#endif

  /*
   * Return the most specific information we can: prefer None or Full
   * over Partial, and Reset or Inherited over Mixed, since we can
   * optimize based on the edge cases and not the in-between cases.
   */
  nsRuleNode::RuleDetail result;
  if (inherited == total)
    result = eRuleFullInherited;
  else if (specified == total
#ifdef MOZ_MATHML
           // MathML defines 3 properties in Font that will never be set when
           // MathML is not in use. Therefore if all but three
           // properties have been set, and MathML is not enabled, we can treat
           // this as fully specified. Code in nsMathMLElementFactory will
           // rebuild the rule tree and style data when MathML is first enabled
           // (see nsMathMLElement::BindToTree).
           || (aSID == eStyleStruct_Font && specified + 3 == total &&
               !mPresContext->Document()->GetMathMLEnabled())
#endif
          ) {
    if (inherited == 0)
      result = eRuleFullReset;
    else
      result = eRuleFullMixed;
  } else if (specified == 0)
    result = eRuleNone;
  else if (specified == inherited)
    result = eRulePartialInherited;
  else if (inherited == 0)
    result = eRulePartialReset;
  else
    result = eRulePartialMixed;

  if (structData->callback) {
    result = (*structData->callback)(aRuleDataStruct, result);
  }

  return result;
}

const void*
nsRuleNode::GetDisplayData(nsStyleContext* aContext)
{
  nsRuleDataDisplay displayData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Display), mPresContext, aContext);
  ruleData.mDisplayData = &displayData;

  return WalkRuleTree(eStyleStruct_Display, aContext, &ruleData, &displayData);
}

const void*
nsRuleNode::GetVisibilityData(nsStyleContext* aContext)
{
  nsRuleDataDisplay displayData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Visibility), mPresContext, aContext);
  ruleData.mDisplayData = &displayData;

  return WalkRuleTree(eStyleStruct_Visibility, aContext, &ruleData, &displayData);
}

const void*
nsRuleNode::GetTextData(nsStyleContext* aContext)
{
  nsRuleDataText textData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Text), mPresContext, aContext);
  ruleData.mTextData = &textData;

  return WalkRuleTree(eStyleStruct_Text, aContext, &ruleData, &textData);
}

const void*
nsRuleNode::GetTextResetData(nsStyleContext* aContext)
{
  nsRuleDataText textData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(TextReset), mPresContext, aContext);
  ruleData.mTextData = &textData;

  return WalkRuleTree(eStyleStruct_TextReset, aContext, &ruleData, &textData);
}

const void*
nsRuleNode::GetUserInterfaceData(nsStyleContext* aContext)
{
  nsRuleDataUserInterface uiData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(UserInterface), mPresContext, aContext);
  ruleData.mUserInterfaceData = &uiData;

  return WalkRuleTree(eStyleStruct_UserInterface, aContext, &ruleData, &uiData);
}

const void*
nsRuleNode::GetUIResetData(nsStyleContext* aContext)
{
  nsRuleDataUserInterface uiData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(UIReset), mPresContext, aContext);
  ruleData.mUserInterfaceData = &uiData;

  return WalkRuleTree(eStyleStruct_UIReset, aContext, &ruleData, &uiData);
}

const void*
nsRuleNode::GetFontData(nsStyleContext* aContext)
{
  nsRuleDataFont fontData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Font), mPresContext, aContext);
  ruleData.mFontData = &fontData;

  return WalkRuleTree(eStyleStruct_Font, aContext, &ruleData, &fontData);
}

const void*
nsRuleNode::GetColorData(nsStyleContext* aContext)
{
  nsRuleDataColor colorData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Color), mPresContext, aContext);
  ruleData.mColorData = &colorData;

  return WalkRuleTree(eStyleStruct_Color, aContext, &ruleData, &colorData);
}

const void*
nsRuleNode::GetBackgroundData(nsStyleContext* aContext)
{
  nsRuleDataColor colorData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Background), mPresContext, aContext);
  ruleData.mColorData = &colorData;

  return WalkRuleTree(eStyleStruct_Background, aContext, &ruleData, &colorData);
}

const void*
nsRuleNode::GetMarginData(nsStyleContext* aContext)
{
  nsRuleDataMargin marginData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Margin), mPresContext, aContext);
  ruleData.mMarginData = &marginData;

  return WalkRuleTree(eStyleStruct_Margin, aContext, &ruleData, &marginData);
}

const void*
nsRuleNode::GetBorderData(nsStyleContext* aContext)
{
  nsRuleDataMargin marginData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Border), mPresContext, aContext);
  ruleData.mMarginData = &marginData;

  return WalkRuleTree(eStyleStruct_Border, aContext, &ruleData, &marginData);
}

const void*
nsRuleNode::GetPaddingData(nsStyleContext* aContext)
{
  nsRuleDataMargin marginData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Padding), mPresContext, aContext);
  ruleData.mMarginData = &marginData;

  return WalkRuleTree(eStyleStruct_Padding, aContext, &ruleData, &marginData);
}

const void*
nsRuleNode::GetOutlineData(nsStyleContext* aContext)
{
  nsRuleDataMargin marginData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Outline), mPresContext, aContext);
  ruleData.mMarginData = &marginData;

  return WalkRuleTree(eStyleStruct_Outline, aContext, &ruleData, &marginData);
}

const void*
nsRuleNode::GetListData(nsStyleContext* aContext)
{
  nsRuleDataList listData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(List), mPresContext, aContext);
  ruleData.mListData = &listData;

  return WalkRuleTree(eStyleStruct_List, aContext, &ruleData, &listData);
}

const void*
nsRuleNode::GetPositionData(nsStyleContext* aContext)
{
  nsRuleDataPosition posData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Position), mPresContext, aContext);
  ruleData.mPositionData = &posData;

  return WalkRuleTree(eStyleStruct_Position, aContext, &ruleData, &posData);
}

const void*
nsRuleNode::GetTableData(nsStyleContext* aContext)
{
  nsRuleDataTable tableData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Table), mPresContext, aContext);
  ruleData.mTableData = &tableData;

  return WalkRuleTree(eStyleStruct_Table, aContext, &ruleData, &tableData);
}

const void*
nsRuleNode::GetTableBorderData(nsStyleContext* aContext)
{
  nsRuleDataTable tableData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(TableBorder), mPresContext, aContext);
  ruleData.mTableData = &tableData;

  return WalkRuleTree(eStyleStruct_TableBorder, aContext, &ruleData, &tableData);
}

const void*
nsRuleNode::GetContentData(nsStyleContext* aContext)
{
  nsRuleDataContent contentData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Content), mPresContext, aContext);
  ruleData.mContentData = &contentData;

  return WalkRuleTree(eStyleStruct_Content, aContext, &ruleData, &contentData);
}

const void*
nsRuleNode::GetQuotesData(nsStyleContext* aContext)
{
  nsRuleDataContent contentData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Quotes), mPresContext, aContext);
  ruleData.mContentData = &contentData;

  return WalkRuleTree(eStyleStruct_Quotes, aContext, &ruleData, &contentData);
}

const void*
nsRuleNode::GetXULData(nsStyleContext* aContext)
{
  nsRuleDataXUL xulData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(XUL), mPresContext, aContext);
  ruleData.mXULData = &xulData;

  return WalkRuleTree(eStyleStruct_XUL, aContext, &ruleData, &xulData);
}

const void*
nsRuleNode::GetColumnData(nsStyleContext* aContext)
{
  nsRuleDataColumn columnData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Column), mPresContext, aContext);
  ruleData.mColumnData = &columnData;

  return WalkRuleTree(eStyleStruct_Column, aContext, &ruleData, &columnData);
}

const void*
nsRuleNode::GetSVGData(nsStyleContext* aContext)
{
  nsRuleDataSVG svgData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(SVG), mPresContext, aContext);
  ruleData.mSVGData = &svgData;

  return WalkRuleTree(eStyleStruct_SVG, aContext, &ruleData, &svgData);
}

const void*
nsRuleNode::GetSVGResetData(nsStyleContext* aContext)
{
  nsRuleDataSVG svgData; // Declare a struct with null CSS values.
  nsRuleData ruleData(NS_STYLE_INHERIT_BIT(SVGReset), mPresContext, aContext);
  ruleData.mSVGData = &svgData;

  return WalkRuleTree(eStyleStruct_SVGReset, aContext, &ruleData, &svgData);
}

// If we need to restrict which properties apply to the style context,
// return the bit to check in nsCSSProp's flags table.  Otherwise,
// return 0.
inline PRUint32
GetPseudoRestriction(nsStyleContext *aContext)
{
  // This needs to match nsStyleSet::WalkRestrictionRule.
  PRUint32 pseudoRestriction = 0;
  nsIAtom *pseudoType = aContext->GetPseudo();
  if (pseudoType) {
    if (pseudoType == nsCSSPseudoElements::firstLetter) {
      pseudoRestriction = CSS_PROPERTY_APPLIES_TO_FIRST_LETTER;
    } else if (pseudoType == nsCSSPseudoElements::firstLine) {
      pseudoRestriction = CSS_PROPERTY_APPLIES_TO_FIRST_LINE;
    }
  }
  return pseudoRestriction;
}

static void
UnsetPropertiesWithoutFlags(const nsStyleStructID aSID,
                            nsRuleDataStruct& aRuleDataStruct,
                            PRUint32 aFlags)
{
  NS_ASSERTION(aFlags != 0, "aFlags must be nonzero");
  const StructCheckData *structData = gCheckProperties + aSID;

  for (const PropertyCheckData *prop = structData->props,
                           *prop_end = prop + structData->nprops;
       prop != prop_end;
       ++prop) {
    if ((prop->flags & aFlags) != aFlags)
      ValueAtOffset(aRuleDataStruct, prop->offset).Reset();
  }
}

const void*
nsRuleNode::WalkRuleTree(const nsStyleStructID aSID,
                         nsStyleContext* aContext,
                         nsRuleData* aRuleData,
                         nsRuleDataStruct* aSpecificData)
{
  // We start at the most specific rule in the tree.
  void* startStruct = nsnull;

  nsRuleNode* ruleNode = this;
  nsRuleNode* highestNode = nsnull; // The highest node in the rule tree
                                    // that has the same properties
                                    // specified for struct |aSID| as
                                    // |this| does.
  nsRuleNode* rootNode = this; // After the loop below, this will be the
                               // highest node that we've walked without
                               // finding cached data on the rule tree.
                               // If we don't find any cached data, it
                               // will be the root.  (XXX misnamed)
  RuleDetail detail = eRuleNone;
  PRUint32 bit = nsCachedStyleData::GetBitForSID(aSID);

  while (ruleNode) {
    // See if this rule node has cached the fact that the remaining
    // nodes along this path specify no data whatsoever.
    if (ruleNode->mNoneBits & bit)
      break;

    // If the dependent bit is set on a rule node for this struct, that
    // means its rule won't have any information to add, so skip it.
    while (ruleNode->mDependentBits & bit) {
      NS_ASSERTION(ruleNode->mStyleData.GetStyleData(aSID) == nsnull,
                   "dependent bit with cached data makes no sense");
      // Climb up to the next rule in the tree (a less specific rule).
      rootNode = ruleNode;
      ruleNode = ruleNode->mParent;
      NS_ASSERTION(!(ruleNode->mNoneBits & bit), "can't have both bits set");
    }

    // Check for cached data after the inner loop above -- otherwise
    // we'll miss it.
    startStruct = ruleNode->mStyleData.GetStyleData(aSID);
    if (startStruct)
      break; // We found a rule with fully specified data.  We don't
             // need to go up the tree any further, since the remainder
             // of this branch has already been computed.

    // Ask the rule to fill in the properties that it specifies.
    nsIStyleRule *rule = ruleNode->mRule;
    if (rule) {
      aRuleData->mLevel = ruleNode->GetLevel();
      aRuleData->mIsImportantRule = ruleNode->IsImportantRule();
      rule->MapRuleInfoInto(aRuleData);
    }

    // Now we check to see how many properties have been specified by
    // the rules we've examined so far.
    RuleDetail oldDetail = detail;
    detail = CheckSpecifiedProperties(aSID, *aSpecificData);

    if (oldDetail == eRuleNone && detail != eRuleNone)
      highestNode = ruleNode;

    if (detail == eRuleFullReset ||
        detail == eRuleFullMixed ||
        detail == eRuleFullInherited)
      break; // We don't need to examine any more rules.  All properties
             // have been fully specified.

    // Climb up to the next rule in the tree (a less specific rule).
    rootNode = ruleNode;
    ruleNode = ruleNode->mParent;
  }

  // If needed, unset the properties that don't have a flag that allows
  // them to be set for this style context.  (For example, only some
  // properties apply to :first-line and :first-letter.)
  PRUint32 pseudoRestriction = GetPseudoRestriction(aContext);
  if (pseudoRestriction) {
    UnsetPropertiesWithoutFlags(aSID, *aSpecificData, pseudoRestriction);

    // Recompute |detail| based on the restrictions we just applied.
    // We can adjust |detail| arbitrarily because of the restriction
    // rule added in nsStyleSet::WalkRestrictionRule.
    detail = CheckSpecifiedProperties(aSID, *aSpecificData);
  }

  NS_ASSERTION(!startStruct || (detail != eRuleFullReset &&
                                detail != eRuleFullMixed &&
                                detail != eRuleFullInherited),
               "can't have start struct and be fully specified");

  PRBool isReset = nsCachedStyleData::IsReset(aSID);
  if (!highestNode)
    highestNode = rootNode;

  if (!aRuleData->mCanStoreInRuleTree)
    detail = eRulePartialMixed; // Treat as though some data is specified to avoid
                                // the optimizations and force data computation.

  if (detail == eRuleNone && startStruct && !aRuleData->mPostResolveCallback) {
    // We specified absolutely no rule information, but a parent rule in the tree
    // specified all the rule information.  We set a bit along the branch from our
    // node in the tree to the node that specified the data that tells nodes on that
    // branch that they never need to examine their rules for this particular struct type
    // ever again.
    PropagateDependentBit(bit, ruleNode);
    return startStruct;
  }
  // FIXME Do we need to check for mPostResolveCallback?
  if ((!startStruct && !isReset &&
       (detail == eRuleNone || detail == eRulePartialInherited)) ||
      detail == eRuleFullInherited) {
    // We specified no non-inherited information and neither did any of
    // our parent rules.

    // We set a bit along the branch from the highest node (ruleNode)
    // down to our node (this) indicating that no non-inherited data was
    // specified.  This bit is guaranteed to be set already on the path
    // from the highest node to the root node in the case where
    // (detail == eRuleNone), which is the most common case here.
    // We must check |!isReset| because the Compute*Data functions for
    // reset structs wouldn't handle none bits correctly.
    if (highestNode != this && !isReset)
      PropagateNoneBit(bit, highestNode);

    // All information must necessarily be inherited from our parent style context.
    // In the absence of any computed data in the rule tree and with
    // no rules specified that didn't have values of 'inherit', we should check our parent.
    nsStyleContext* parentContext = aContext->GetParent();
    if (isReset) {
      /* Reset structs don't inherit from first-line. */
      /* See similar code in COMPUTE_START_RESET */
      while (parentContext &&
             parentContext->GetPseudo() == nsCSSPseudoElements::firstLine) {
        parentContext = parentContext->GetParent();
      }
    }
    if (parentContext) {
      // We have a parent, and so we should just inherit from the parent.
      // Set the inherit bits on our context.  These bits tell the style context that
      // it never has to go back to the rule tree for data.  Instead the style context tree
      // should be walked to find the data.
      const void* parentStruct = parentContext->GetStyleData(aSID);
      aContext->AddStyleBit(bit); // makes const_cast OK.
      aContext->SetStyle(aSID, const_cast<void*>(parentStruct));
      return parentStruct;
    }
    else
      // We are the root.  In the case of fonts, the default values just
      // come from the pres context.
      return SetDefaultOnRoot(aSID, aContext);
  }

  // We need to compute the data from the information that the rules specified.
  const void* res;
#define STYLE_STRUCT_TEST aSID
#define STYLE_STRUCT(name, checkdata_cb, ctor_args)                           \
  res = Compute##name##Data(startStruct, *aSpecificData, aContext,            \
                      highestNode, detail, aRuleData->mCanStoreInRuleTree);
#include "nsStyleStructList.h"
#undef STYLE_STRUCT
#undef STYLE_STRUCT_TEST

  // If we have a post-resolve callback, handle that now.
  if (aRuleData->mPostResolveCallback && (NS_LIKELY(res != nsnull)))
    (*aRuleData->mPostResolveCallback)(const_cast<void*>(res), aRuleData);

  // Now return the result.
  return res;
}

const void*
nsRuleNode::SetDefaultOnRoot(const nsStyleStructID aSID, nsStyleContext* aContext)
{
  switch (aSID) {
    case eStyleStruct_Font:
    {
      nsStyleFont* fontData = new (mPresContext) nsStyleFont(mPresContext);
      if (NS_LIKELY(fontData != nsnull)) {
        nscoord minimumFontSize =
          mPresContext->GetCachedIntPref(kPresContext_MinimumFontSize);

        if (minimumFontSize > 0 && !mPresContext->IsChrome()) {
          fontData->mFont.size = NS_MAX(fontData->mSize, minimumFontSize);
        }
        else {
          fontData->mFont.size = fontData->mSize;
        }
        aContext->SetStyle(eStyleStruct_Font, fontData);
      }
      return fontData;
    }
    case eStyleStruct_Display:
    {
      nsStyleDisplay* disp = new (mPresContext) nsStyleDisplay();
      if (NS_LIKELY(disp != nsnull)) {
        aContext->SetStyle(eStyleStruct_Display, disp);
      }
      return disp;
    }
    case eStyleStruct_Visibility:
    {
      nsStyleVisibility* vis = new (mPresContext) nsStyleVisibility(mPresContext);
      if (NS_LIKELY(vis != nsnull)) {
        aContext->SetStyle(eStyleStruct_Visibility, vis);
      }
      return vis;
    }
    case eStyleStruct_Text:
    {
      nsStyleText* text = new (mPresContext) nsStyleText();
      if (NS_LIKELY(text != nsnull)) {
        aContext->SetStyle(eStyleStruct_Text, text);
      }
      return text;
    }
    case eStyleStruct_TextReset:
    {
      nsStyleTextReset* text = new (mPresContext) nsStyleTextReset();
      if (NS_LIKELY(text != nsnull)) {
        aContext->SetStyle(eStyleStruct_TextReset, text);
      }
      return text;
    }
    case eStyleStruct_Color:
    {
      nsStyleColor* color = new (mPresContext) nsStyleColor(mPresContext);
      if (NS_LIKELY(color != nsnull)) {
        aContext->SetStyle(eStyleStruct_Color, color);
      }
      return color;
    }
    case eStyleStruct_Background:
    {
      nsStyleBackground* bg = new (mPresContext) nsStyleBackground();
      if (NS_LIKELY(bg != nsnull)) {
        aContext->SetStyle(eStyleStruct_Background, bg);
      }
      return bg;
    }
    case eStyleStruct_Margin:
    {
      nsStyleMargin* margin = new (mPresContext) nsStyleMargin();
      if (NS_LIKELY(margin != nsnull)) {
        aContext->SetStyle(eStyleStruct_Margin, margin);
      }
      return margin;
    }
    case eStyleStruct_Border:
    {
      nsStyleBorder* border = new (mPresContext) nsStyleBorder(mPresContext);
      if (NS_LIKELY(border != nsnull)) {
        aContext->SetStyle(eStyleStruct_Border, border);
      }
      return border;
    }
    case eStyleStruct_Padding:
    {
      nsStylePadding* padding = new (mPresContext) nsStylePadding();
      if (NS_LIKELY(padding != nsnull)) {
        aContext->SetStyle(eStyleStruct_Padding, padding);
      }
      return padding;
    }
    case eStyleStruct_Outline:
    {
      nsStyleOutline* outline = new (mPresContext) nsStyleOutline(mPresContext);
      if (NS_LIKELY(outline != nsnull)) {
        aContext->SetStyle(eStyleStruct_Outline, outline);
      }
      return outline;
    }
    case eStyleStruct_List:
    {
      nsStyleList* list = new (mPresContext) nsStyleList();
      if (NS_LIKELY(list != nsnull)) {
        aContext->SetStyle(eStyleStruct_List, list);
      }
      return list;
    }
    case eStyleStruct_Position:
    {
      nsStylePosition* pos = new (mPresContext) nsStylePosition();
      if (NS_LIKELY(pos != nsnull)) {
        aContext->SetStyle(eStyleStruct_Position, pos);
      }
      return pos;
    }
    case eStyleStruct_Table:
    {
      nsStyleTable* table = new (mPresContext) nsStyleTable();
      if (NS_LIKELY(table != nsnull)) {
        aContext->SetStyle(eStyleStruct_Table, table);
      }
      return table;
    }
    case eStyleStruct_TableBorder:
    {
      nsStyleTableBorder* table = new (mPresContext) nsStyleTableBorder(mPresContext);
      if (NS_LIKELY(table != nsnull)) {
        aContext->SetStyle(eStyleStruct_TableBorder, table);
      }
      return table;
    }
    case eStyleStruct_Content:
    {
      nsStyleContent* content = new (mPresContext) nsStyleContent();
      if (NS_LIKELY(content != nsnull)) {
        aContext->SetStyle(eStyleStruct_Content, content);
      }
      return content;
    }
    case eStyleStruct_Quotes:
    {
      nsStyleQuotes* quotes = new (mPresContext) nsStyleQuotes();
      if (NS_LIKELY(quotes != nsnull)) {
        aContext->SetStyle(eStyleStruct_Quotes, quotes);
      }
      return quotes;
    }
    case eStyleStruct_UserInterface:
    {
      nsStyleUserInterface* ui = new (mPresContext) nsStyleUserInterface();
      if (NS_LIKELY(ui != nsnull)) {
        aContext->SetStyle(eStyleStruct_UserInterface, ui);
      }
      return ui;
    }
    case eStyleStruct_UIReset:
    {
      nsStyleUIReset* ui = new (mPresContext) nsStyleUIReset();
      if (NS_LIKELY(ui != nsnull)) {
        aContext->SetStyle(eStyleStruct_UIReset, ui);
      }
      return ui;
    }

    case eStyleStruct_XUL:
    {
      nsStyleXUL* xul = new (mPresContext) nsStyleXUL();
      if (NS_LIKELY(xul != nsnull)) {
        aContext->SetStyle(eStyleStruct_XUL, xul);
      }
      return xul;
    }

    case eStyleStruct_Column:
    {
      nsStyleColumn* column = new (mPresContext) nsStyleColumn(mPresContext);
      if (NS_LIKELY(column != nsnull)) {
        aContext->SetStyle(eStyleStruct_Column, column);
      }
      return column;
    }

    case eStyleStruct_SVG:
    {
      nsStyleSVG* svg = new (mPresContext) nsStyleSVG();
      if (NS_LIKELY(svg != nsnull)) {
        aContext->SetStyle(eStyleStruct_SVG, svg);
      }
      return svg;
    }

    case eStyleStruct_SVGReset:
    {
      nsStyleSVGReset* svgReset = new (mPresContext) nsStyleSVGReset();
      if (NS_LIKELY(svgReset != nsnull)) {
        aContext->SetStyle(eStyleStruct_SVGReset, svgReset);
      }
      return svgReset;
    }
    default:
      /*
       * unhandled case: nsStyleStructID_Length.
       * last item of nsStyleStructID, to know its length.
       */
      return nsnull;
  }
  return nsnull;
}

/*
 * This function handles cascading of *-left or *-right box properties
 * against *-start (which is L for LTR and R for RTL) or *-end (which is
 * R for LTR and L for RTL).
 *
 * Cascading these properties correctly is hard because we need to
 * cascade two properties as one, but which two properties depends on a
 * third property ('direction').  We solve this by treating each of
 * these properties (say, 'margin-start') as a shorthand that sets a
 * property containing the value of the property specified
 * ('margin-start-value') and sets a pair of properties
 * ('margin-left-ltr-source' and 'margin-right-rtl-source') saying which
 * of the properties we use.  Thus, when we want to compute the value of
 * 'margin-left' when 'direction' is 'ltr', we look at the value of
 * 'margin-left-ltr-source', which tells us whether to use the highest
 * 'margin-left' in the cascade or the highest 'margin-start'.
 *
 * Finally, since we can compute the normal (*-left and *-right)
 * properties in a loop, this function works by modifying the data we
 * will use in that loop (which the caller must copy from the const
 * input).
 */
void
nsRuleNode::AdjustLogicalBoxProp(nsStyleContext* aContext,
                                 const nsCSSValue& aLTRSource,
                                 const nsCSSValue& aRTLSource,
                                 const nsCSSValue& aLTRLogicalValue,
                                 const nsCSSValue& aRTLLogicalValue,
                                 mozilla::css::Side aSide,
                                 nsCSSRect& aValueRect,
                                 PRBool& aCanStoreInRuleTree)
{
  PRBool LTRlogical = aLTRSource.GetUnit() == eCSSUnit_Enumerated &&
                      aLTRSource.GetIntValue() == NS_BOXPROP_SOURCE_LOGICAL;
  PRBool RTLlogical = aRTLSource.GetUnit() == eCSSUnit_Enumerated &&
                      aRTLSource.GetIntValue() == NS_BOXPROP_SOURCE_LOGICAL;
  if (LTRlogical || RTLlogical) {
    // We can't cache anything on the rule tree if we use any data from
    // the style context, since data cached in the rule tree could be
    // used with a style context with a different value.
    aCanStoreInRuleTree = PR_FALSE;
    PRUint8 dir = aContext->GetStyleVisibility()->mDirection;

    if (dir == NS_STYLE_DIRECTION_LTR) {
      if (LTRlogical)
        aValueRect.*(nsCSSRect::sides[aSide]) = aLTRLogicalValue;
    } else {
      if (RTLlogical)
        aValueRect.*(nsCSSRect::sides[aSide]) = aRTLLogicalValue;
    }
  } else if (aLTRLogicalValue.GetUnit() == eCSSUnit_Inherit ||
             aRTLLogicalValue.GetUnit() == eCSSUnit_Inherit) {
    // It actually is valid to store this in the ruletree, since
    // LTRlogical and RTLlogical are both false, but doing that will
    // trigger asserts.  Silence those.
    aCanStoreInRuleTree = PR_FALSE;
  }
}

/**
 * Begin an nsRuleNode::Compute*Data function for an inherited struct.
 *
 * @param type_ The nsStyle* type this function computes.
 * @param ctorargs_ The arguments used for the default nsStyle* constructor.
 * @param data_ Variable (declared here) holding the result of this
 *              function.
 * @param parentdata_ Variable (declared here) holding the parent style
 *                    context's data for this struct.
 * @param rdtype_ The nsCSS* struct type used to compute this struct's data.
 * @param rdata_ Variable (declared here) holding the nsCSS* used here.
 */
#define COMPUTE_START_INHERITED(type_, ctorargs_, data_, parentdata_, rdtype_, rdata_) \
  NS_ASSERTION(aRuleDetail != eRuleFullInherited,                             \
               "should not have bothered calling Compute*Data");              \
                                                                              \
  nsStyleContext* parentContext = aContext->GetParent();                      \
                                                                              \
  const nsRuleData##rdtype_& rdata_ =                                         \
    static_cast<const nsRuleData##rdtype_&>(aData);                           \
  nsStyle##type_* data_ = nsnull;                                             \
  const nsStyle##type_* parentdata_ = nsnull;                                 \
  PRBool canStoreInRuleTree = aCanStoreInRuleTree;                            \
                                                                              \
  /* If |canStoreInRuleTree| might be true by the time we're done, we */      \
  /* can't call parentContext->GetStyle##type_() since it could recur into */ \
  /* setting the same struct on the same rule node, causing a leak. */        \
  if (parentContext && aRuleDetail != eRuleFullReset &&                       \
      (!aStartStruct || (aRuleDetail != eRulePartialReset &&                  \
                         aRuleDetail != eRuleNone)))                          \
    parentdata_ = parentContext->GetStyle##type_();                           \
  if (aStartStruct)                                                           \
    /* We only need to compute the delta between this computed data and */    \
    /* our computed data. */                                                  \
    data_ = new (mPresContext)                                                \
            nsStyle##type_(*static_cast<nsStyle##type_*>(aStartStruct));      \
  else {                                                                      \
    if (aRuleDetail != eRuleFullMixed && aRuleDetail != eRuleFullReset) {     \
      /* No question. We will have to inherit. Go ahead and init */           \
      /* with inherited vals from parent. */                                  \
      canStoreInRuleTree = PR_FALSE;                                          \
      if (parentdata_)                                                        \
        data_ = new (mPresContext) nsStyle##type_(*parentdata_);              \
      else                                                                    \
        data_ = new (mPresContext) nsStyle##type_ ctorargs_;                  \
    }                                                                         \
    else                                                                      \
      data_ = new (mPresContext) nsStyle##type_ ctorargs_;                    \
  }                                                                           \
                                                                              \
  if (NS_UNLIKELY(!data_))                                                    \
    return nsnull;  /* Out Of Memory */                                       \
  if (!parentdata_)                                                           \
    parentdata_ = data_;

/**
 * Begin an nsRuleNode::Compute*Data function for a reset struct.
 *
 * @param type_ The nsStyle* type this function computes.
 * @param ctorargs_ The arguments used for the default nsStyle* constructor.
 * @param data_ Variable (declared here) holding the result of this
 *              function.
 * @param parentdata_ Variable (declared here) holding the parent style
 *                    context's data for this struct.
 * @param rdtype_ The nsCSS* struct type used to compute this struct's data.
 * @param rdata_ Variable (declared here) holding the nsCSS* used here.
 */
#define COMPUTE_START_RESET(type_, ctorargs_, data_, parentdata_, rdtype_, rdata_) \
  NS_ASSERTION(aRuleDetail != eRuleFullInherited,                             \
               "should not have bothered calling Compute*Data");              \
                                                                              \
  nsStyleContext* parentContext = aContext->GetParent();                      \
  /* Reset structs don't inherit from first-line */                           \
  /* See similar code in WalkRuleTree */                                      \
  while (parentContext &&                                                     \
         parentContext->GetPseudo() == nsCSSPseudoElements::firstLine) {      \
    parentContext = parentContext->GetParent();                               \
  }                                                                           \
                                                                              \
  const nsRuleData##rdtype_& rdata_ =                                         \
    static_cast<const nsRuleData##rdtype_&>(aData);                           \
  nsStyle##type_* data_;                                                      \
  if (aStartStruct)                                                           \
    /* We only need to compute the delta between this computed data and */    \
    /* our computed data. */                                                  \
    data_ = new (mPresContext)                                                \
            nsStyle##type_(*static_cast<nsStyle##type_*>(aStartStruct));      \
  else                                                                        \
    data_ = new (mPresContext) nsStyle##type_ ctorargs_;                      \
                                                                              \
  if (NS_UNLIKELY(!data_))                                                    \
    return nsnull;  /* Out Of Memory */                                       \
                                                                              \
  /* If |canStoreInRuleTree| might be true by the time we're done, we */      \
  /* can't call parentContext->GetStyle##type_() since it could recur into */ \
  /* setting the same struct on the same rule node, causing a leak. */        \
  const nsStyle##type_* parentdata_ = data_;                                  \
  if (parentContext &&                                                        \
      aRuleDetail != eRuleFullReset &&                                        \
      aRuleDetail != eRulePartialReset &&                                     \
      aRuleDetail != eRuleNone)                                               \
    parentdata_ = parentContext->GetStyle##type_();                           \
  PRBool canStoreInRuleTree = aCanStoreInRuleTree;

/**
 * Begin an nsRuleNode::Compute*Data function for an inherited struct.
 *
 * @param type_ The nsStyle* type this function computes.
 * @param data_ Variable holding the result of this function.
 */
#define COMPUTE_END_INHERITED(type_, data_)                                   \
  NS_POSTCONDITION(!canStoreInRuleTree || aRuleDetail == eRuleFullReset ||    \
                   (aStartStruct && aRuleDetail == eRulePartialReset),        \
                   "canStoreInRuleTree must be false for inherited structs "  \
                   "unless all properties have been specified with values "   \
                   "other than inherit");                                     \
  if (canStoreInRuleTree) {                                                   \
    /* We were fully specified and can therefore be cached right on the */    \
    /* rule node. */                                                          \
    if (!aHighestNode->mStyleData.mInheritedData) {                           \
      aHighestNode->mStyleData.mInheritedData =                               \
        new (mPresContext) nsInheritedStyleData;                              \
      if (NS_UNLIKELY(!aHighestNode->mStyleData.mInheritedData)) {            \
        data_->Destroy(mPresContext);                                         \
        return nsnull;                                                        \
      }                                                                       \
    }                                                                         \
    NS_ASSERTION(!aHighestNode->mStyleData.mInheritedData->m##type_##Data,    \
                 "Going to leak style data");                                 \
    aHighestNode->mStyleData.mInheritedData->m##type_##Data = data_;          \
    /* Propagate the bit down. */                                             \
    PropagateDependentBit(NS_STYLE_INHERIT_BIT(type_), aHighestNode);         \
    /* Tell the style context that it doesn't own the data */                 \
    aContext->                                                                \
      AddStyleBit(nsCachedStyleData::GetBitForSID(eStyleStruct_##type_));     \
  }                                                                           \
  /* Always cache inherited data on the style context */                      \
  aContext->SetStyle##type_(data_);                                           \
                                                                              \
  return data_;

/**
 * Begin an nsRuleNode::Compute*Data function for a reset struct.
 *
 * @param type_ The nsStyle* type this function computes.
 * @param data_ Variable holding the result of this function.
 */
#define COMPUTE_END_RESET(type_, data_)                                       \
  NS_POSTCONDITION(!canStoreInRuleTree ||                                     \
                   aRuleDetail == eRuleNone ||                                \
                   aRuleDetail == eRulePartialReset ||                        \
                   aRuleDetail == eRuleFullReset,                             \
                   "canStoreInRuleTree must be false for reset structs "      \
                   "if any properties were specified as inherit");            \
  if (!canStoreInRuleTree)                                                    \
    /* We can't be cached in the rule node.  We have to be put right */       \
    /* on the style context. */                                               \
    aContext->SetStyle(eStyleStruct_##type_, data_);                          \
  else {                                                                      \
    /* We were fully specified and can therefore be cached right on the */    \
    /* rule node. */                                                          \
    if (!aHighestNode->mStyleData.mResetData) {                               \
      aHighestNode->mStyleData.mResetData =                                   \
        new (mPresContext) nsResetStyleData;                                  \
      if (NS_UNLIKELY(!aHighestNode->mStyleData.mResetData)) {                \
        data_->Destroy(mPresContext);                                         \
        return nsnull;                                                        \
      }                                                                       \
    }                                                                         \
    NS_ASSERTION(!aHighestNode->mStyleData.mResetData->m##type_##Data,        \
                 "Going to leak style data");                                 \
    aHighestNode->mStyleData.mResetData->m##type_##Data = data_;              \
    /* Propagate the bit down. */                                             \
    PropagateDependentBit(NS_STYLE_INHERIT_BIT(type_), aHighestNode);         \
  }                                                                           \
                                                                              \
  return data_;

#ifdef MOZ_MATHML
// This function figures out how much scaling should be suppressed to
// satisfy scriptminsize. This is our attempt to implement
// http://www.w3.org/TR/MathML2/chapter3.html#id.3.3.4.2.2
// This is called after mScriptLevel, mScriptMinSize and mScriptSizeMultiplier
// have been set in aFont.
//
// Here are the invariants we enforce:
// 1) A decrease in size must not reduce the size below minscriptsize.
// 2) An increase in size must not increase the size above the size we would
// have if minscriptsize had not been applied anywhere.
// 3) The scriptlevel-induced size change must between 1.0 and the parent's
// scriptsizemultiplier^(new script level - old script level), as close to the
// latter as possible subject to constraints 1 and 2.
static nscoord
ComputeScriptLevelSize(const nsStyleFont* aFont, const nsStyleFont* aParentFont,
                       nsPresContext* aPresContext, nscoord* aUnconstrainedSize)
{
  PRInt32 scriptLevelChange =
    aFont->mScriptLevel - aParentFont->mScriptLevel;
  if (scriptLevelChange == 0) {
    *aUnconstrainedSize = aParentFont->mScriptUnconstrainedSize;
    // Constraint #3 says that we cannot change size, and #1 and #2 are always
    // satisfied with no change. It's important this be fast because it covers
    // all non-MathML content.
    return aParentFont->mSize;
  }

  // Compute actual value of minScriptSize
  nscoord minScriptSize =
    nsStyleFont::ZoomText(aPresContext, aParentFont->mScriptMinSize);

  double scriptLevelScale =
    pow(aParentFont->mScriptSizeMultiplier, scriptLevelChange);
  // Compute the size we would have had if minscriptsize had never been
  // applied, also prevent overflow (bug 413274)
  *aUnconstrainedSize =
    NSToCoordRound(NS_MIN(aParentFont->mScriptUnconstrainedSize*scriptLevelScale,
                          double(nscoord_MAX)));
  // Compute the size we could get via scriptlevel change
  nscoord scriptLevelSize =
    NSToCoordRound(NS_MIN(aParentFont->mSize*scriptLevelScale,
                          double(nscoord_MAX)));
  if (scriptLevelScale <= 1.0) {
    if (aParentFont->mSize <= minScriptSize) {
      // We can't decrease the font size at all, so just stick to no change
      // (authors are allowed to explicitly set the font size smaller than
      // minscriptsize)
      return aParentFont->mSize;
    }
    // We can decrease, so apply constraint #1
    return NS_MAX(minScriptSize, scriptLevelSize);
  } else {
    // scriptminsize can only make sizes larger than the unconstrained size
    NS_ASSERTION(*aUnconstrainedSize <= scriptLevelSize, "How can this ever happen?");
    // Apply constraint #2
    return NS_MIN(scriptLevelSize, NS_MAX(*aUnconstrainedSize, minScriptSize));
  }
}
#endif

struct SetFontSizeCalcOps : public css::BasicCoordCalcOps,
                            public css::NumbersAlreadyNormalizedOps
{
  // The parameters beyond aValue that we need for CalcLengthWith.
  const nscoord mParentSize;
  const nsStyleFont* const mParentFont;
  nsPresContext* const mPresContext;
  const PRBool mAtRoot;
  PRBool& mCanStoreInRuleTree;

  SetFontSizeCalcOps(nscoord aParentSize, const nsStyleFont* aParentFont,
                     nsPresContext* aPresContext, PRBool aAtRoot,
                     PRBool& aCanStoreInRuleTree)
    : mParentSize(aParentSize),
      mParentFont(aParentFont),
      mPresContext(aPresContext),
      mAtRoot(aAtRoot),
      mCanStoreInRuleTree(aCanStoreInRuleTree)
  {
  }

  result_type ComputeLeafValue(const nsCSSValue& aValue)
  {
    nscoord size;
    if (aValue.IsLengthUnit()) {
      // Note that font-based length units use the parent's size
      // unadjusted for scriptlevel changes. A scriptlevel change
      // between us and the parent is simply ignored.
      size = CalcLengthWith(aValue, mParentSize, mParentFont,
                            nsnull, mPresContext, mAtRoot,
                            PR_TRUE, mCanStoreInRuleTree);
      if (!aValue.IsRelativeLengthUnit()) {
        size = nsStyleFont::ZoomText(mPresContext, size);
      }
    }
    else if (eCSSUnit_Percent == aValue.GetUnit()) {
      mCanStoreInRuleTree = PR_FALSE;
      // Note that % units use the parent's size unadjusted for scriptlevel
      // changes. A scriptlevel change between us and the parent is simply
      // ignored.
      // aValue.GetPercentValue() may be negative for, e.g., calc(-50%)
      size = NSCoordSaturatingMultiply(mParentSize, aValue.GetPercentValue());
    } else {
      NS_ABORT_IF_FALSE(PR_FALSE, "unexpected value");
      size = mParentSize;
    }

    return size;
  }
};

/* static */ void
nsRuleNode::SetFontSize(nsPresContext* aPresContext,
                        const nsRuleDataFont& aFontData,
                        const nsStyleFont* aFont,
                        const nsStyleFont* aParentFont,
                        nscoord* aSize,
                        const nsFont& aSystemFont,
                        nscoord aParentSize,
                        nscoord aScriptLevelAdjustedParentSize,
                        PRBool aUsedStartStruct,
                        PRBool aAtRoot,
                        PRBool& aCanStoreInRuleTree)
{
  PRBool zoom = PR_FALSE;
  PRInt32 baseSize = (PRInt32) aPresContext->
    GetDefaultFont(aFont->mGenericID)->size;
  if (eCSSUnit_Enumerated == aFontData.mSize.GetUnit()) {
    PRInt32 value = aFontData.mSize.GetIntValue();
    PRInt32 scaler = aPresContext->FontScaler();
    float scaleFactor = nsStyleUtil::GetScalingFactor(scaler);

    zoom = PR_TRUE;
    if ((NS_STYLE_FONT_SIZE_XXSMALL <= value) &&
        (value <= NS_STYLE_FONT_SIZE_XXLARGE)) {
      *aSize = nsStyleUtil::CalcFontPointSize(value, baseSize,
                       scaleFactor, aPresContext, eFontSize_CSS);
    }
    else if (NS_STYLE_FONT_SIZE_XXXLARGE == value) {
      // <font size="7"> is not specified in CSS, so we don't use eFontSize_CSS.
      *aSize = nsStyleUtil::CalcFontPointSize(value, baseSize,
                       scaleFactor, aPresContext);
    }
    else if (NS_STYLE_FONT_SIZE_LARGER  == value ||
             NS_STYLE_FONT_SIZE_SMALLER == value) {
      aCanStoreInRuleTree = PR_FALSE;

      // Un-zoom so we use the tables correctly.  We'll then rezoom due
      // to the |zoom = PR_TRUE| above.
      // Note that relative units here use the parent's size unadjusted
      // for scriptlevel changes. A scriptlevel change between us and the parent
      // is simply ignored.
      nscoord parentSize =
        nsStyleFont::UnZoomText(aPresContext, aParentSize);

      if (NS_STYLE_FONT_SIZE_LARGER == value) {
        *aSize = nsStyleUtil::FindNextLargerFontSize(parentSize,
                         baseSize, scaleFactor, aPresContext, eFontSize_CSS);
        NS_ASSERTION(*aSize > parentSize,
                     "FindNextLargerFontSize failed");
      }
      else {
        *aSize = nsStyleUtil::FindNextSmallerFontSize(parentSize,
                         baseSize, scaleFactor, aPresContext, eFontSize_CSS);
        NS_ASSERTION(*aSize < parentSize ||
                     parentSize <= nsPresContext::CSSPixelsToAppUnits(1),
                     "FindNextSmallerFontSize failed");
      }
    } else {
      NS_NOTREACHED("unexpected value");
    }
  }
  else if (aFontData.mSize.IsLengthUnit() ||
           aFontData.mSize.GetUnit() == eCSSUnit_Percent ||
           aFontData.mSize.IsCalcUnit()) {
    SetFontSizeCalcOps ops(aParentSize, aParentFont, aPresContext, aAtRoot,
                           aCanStoreInRuleTree);
    *aSize = css::ComputeCalc(aFontData.mSize, ops);
    if (*aSize < 0) {
      NS_ABORT_IF_FALSE(aFontData.mSize.IsCalcUnit(),
                        "negative lengths and percents should be rejected "
                        "by parser");
      *aSize = 0;
    }
    // Zoom is handled inside the calc ops when needed.
    zoom = PR_FALSE;
  }
  else if (eCSSUnit_System_Font == aFontData.mSize.GetUnit()) {
    // this becomes our cascading size
    *aSize = aSystemFont.size;
    zoom = PR_TRUE;
  }
  else if (eCSSUnit_Inherit == aFontData.mSize.GetUnit()) {
    aCanStoreInRuleTree = PR_FALSE;
    // We apply scriptlevel change for this case, because the default is
    // to inherit and we don't want explicit "inherit" to differ from the
    // default.
    *aSize = aScriptLevelAdjustedParentSize;
    zoom = PR_FALSE;
  }
  else if (eCSSUnit_Initial == aFontData.mSize.GetUnit()) {
    // The initial value is 'medium', which has magical sizing based on
    // the generic font family, so do that here too.
    *aSize = baseSize;
    zoom = PR_TRUE;
  } else {
    NS_ASSERTION(eCSSUnit_Null == aFontData.mSize.GetUnit(),
                 "What kind of font-size value is this?");
#ifdef MOZ_MATHML
    // if aUsedStartStruct is true, then every single property in the
    // font struct is being set all at once. This means scriptlevel is not
    // going to have any influence on the font size; there is no need to
    // do anything here.
    if (!aUsedStartStruct && aParentSize != aScriptLevelAdjustedParentSize) {
      // There was no rule affecting the size but the size has been
      // affected by the parent's size via scriptlevel change. So we cannot
      // store the data in the rule tree.
      aCanStoreInRuleTree = PR_FALSE;
      *aSize = aScriptLevelAdjustedParentSize;
    }
#endif
  }

  // We want to zoom the cascaded size so that em-based measurements,
  // line-heights, etc., work.
  if (zoom) {
    *aSize = nsStyleFont::ZoomText(aPresContext, *aSize);
  }
}

static PRInt8 ClampTo8Bit(PRInt32 aValue) {
  if (aValue < -128)
    return -128;
  if (aValue > 127)
    return 127;
  return PRInt8(aValue);
}

/* static */ void
nsRuleNode::SetFont(nsPresContext* aPresContext, nsStyleContext* aContext,
                    nscoord aMinFontSize,
                    PRUint8 aGenericFontID, const nsRuleDataFont& aFontData,
                    const nsStyleFont* aParentFont,
                    nsStyleFont* aFont, PRBool aUsedStartStruct,
                    PRBool& aCanStoreInRuleTree)
{
  const nsFont* defaultVariableFont =
    aPresContext->GetDefaultFont(kPresContext_DefaultVariableFont_ID);
  PRBool atRoot = !aContext->GetParent();

  // -moz-system-font: enum (never inherit!)
  nsFont systemFont;
  if (eCSSUnit_Enumerated == aFontData.mSystemFont.GetUnit()) {
    nsSystemFontID sysID;
    switch (aFontData.mSystemFont.GetIntValue()) {
      case NS_STYLE_FONT_CAPTION:       sysID = eSystemFont_Caption;      break;    // css2
      case NS_STYLE_FONT_ICON:          sysID = eSystemFont_Icon;         break;
      case NS_STYLE_FONT_MENU:          sysID = eSystemFont_Menu;         break;
      case NS_STYLE_FONT_MESSAGE_BOX:   sysID = eSystemFont_MessageBox;   break;
      case NS_STYLE_FONT_SMALL_CAPTION: sysID = eSystemFont_SmallCaption; break;
      case NS_STYLE_FONT_STATUS_BAR:    sysID = eSystemFont_StatusBar;    break;
      case NS_STYLE_FONT_WINDOW:        sysID = eSystemFont_Window;       break;    // css3
      case NS_STYLE_FONT_DOCUMENT:      sysID = eSystemFont_Document;     break;
      case NS_STYLE_FONT_WORKSPACE:     sysID = eSystemFont_Workspace;    break;
      case NS_STYLE_FONT_DESKTOP:       sysID = eSystemFont_Desktop;      break;
      case NS_STYLE_FONT_INFO:          sysID = eSystemFont_Info;         break;
      case NS_STYLE_FONT_DIALOG:        sysID = eSystemFont_Dialog;       break;
      case NS_STYLE_FONT_BUTTON:        sysID = eSystemFont_Button;       break;
      case NS_STYLE_FONT_PULL_DOWN_MENU:sysID = eSystemFont_PullDownMenu; break;
      case NS_STYLE_FONT_LIST:          sysID = eSystemFont_List;         break;
      case NS_STYLE_FONT_FIELD:         sysID = eSystemFont_Field;        break;
    }

    // GetSystemFont sets the font face but not necessarily the size
    // XXX Or at least it used to -- no longer true for thebes.  Maybe
    // it should be again, though.
    systemFont.size = defaultVariableFont->size;

    if (NS_FAILED(aPresContext->DeviceContext()->GetSystemFont(sysID,
                                                               &systemFont))) {
        systemFont.name = defaultVariableFont->name;
    }

    // XXXldb All of this platform-specific stuff should be in the
    // nsIDeviceContext implementations, not here.

#ifdef XP_WIN
    //
    // As far as I can tell the system default fonts and sizes for
    // on MS-Windows for Buttons, Listboxes/Comboxes and Text Fields are
    // all pre-determined and cannot be changed by either the control panel
    // or programmtically.
    //
    switch (sysID) {
      // Fields (text fields)
      // Button and Selects (listboxes/comboboxes)
      //    We use whatever font is defined by the system. Which it appears
      //    (and the assumption is) it is always a proportional font. Then we
      //    always use 2 points smaller than what the browser has defined as
      //    the default proportional font.
      case eSystemFont_Field:
      case eSystemFont_Button:
      case eSystemFont_List:
        // Assumption: system defined font is proportional
        systemFont.size =
          NS_MAX(defaultVariableFont->size - nsPresContext::CSSPointsToAppUnits(2), 0);
        break;
    }
#endif
  } else {
    // In case somebody explicitly used -moz-use-system-font.
    systemFont = *defaultVariableFont;
  }


  // font-family: string list, enum, inherit
  NS_ASSERTION(eCSSUnit_Enumerated != aFontData.mFamily.GetUnit(),
               "system fonts should not be in mFamily anymore");
  if (eCSSUnit_Families == aFontData.mFamily.GetUnit()) {
    // set the correct font if we are using DocumentFonts OR we are overriding for XUL
    // MJA: bug 31816
    if (aGenericFontID == kGenericFont_NONE) {
      // only bother appending fallback fonts if this isn't a fallback generic font itself
      if (!aFont->mFont.name.IsEmpty())
        aFont->mFont.name.Append((PRUnichar)',');
      // defaultVariableFont.name should always be "serif" or "sans-serif".
      aFont->mFont.name.Append(defaultVariableFont->name);
    }
    aFont->mFont.familyNameQuirks =
        (aPresContext->CompatibilityMode() == eCompatibility_NavQuirks &&
         aFontData.mFamilyFromHTML);
    aFont->mFont.systemFont = PR_FALSE;
    // Technically this is redundant with the code below, but it's good
    // to have since we'll still want it once we get rid of
    // SetGenericFont (bug 380915).
    aFont->mGenericID = aGenericFontID;
  }
  else if (eCSSUnit_System_Font == aFontData.mFamily.GetUnit()) {
    aFont->mFont.name = systemFont.name;
    aFont->mFont.familyNameQuirks = PR_FALSE;
    aFont->mFont.systemFont = PR_TRUE;
    aFont->mGenericID = kGenericFont_NONE;
  }
  else if (eCSSUnit_Inherit == aFontData.mFamily.GetUnit()) {
    aCanStoreInRuleTree = PR_FALSE;
    aFont->mFont.name = aParentFont->mFont.name;
    aFont->mFont.familyNameQuirks = aParentFont->mFont.familyNameQuirks;
    aFont->mFont.systemFont = aParentFont->mFont.systemFont;
    aFont->mGenericID = aParentFont->mGenericID;
  }
  else if (eCSSUnit_Initial == aFontData.mFamily.GetUnit()) {
    aFont->mFont.name = defaultVariableFont->name;
    aFont->mFont.familyNameQuirks = PR_FALSE;
    aFont->mFont.systemFont = defaultVariableFont->systemFont;
    aFont->mGenericID = kGenericFont_NONE;
  }

  // When we're in the loop in SetGenericFont, we must ensure that we
  // always keep aFont->mFlags set to the correct generic.  But we have
  // to be careful not to touch it when we're called directly from
  // ComputeFontData, because we could have a start struct.
  if (aGenericFontID != kGenericFont_NONE) {
    aFont->mGenericID = aGenericFontID;
  }

  // font-style: enum, inherit, initial, -moz-system-font
  SetDiscrete(aFontData.mStyle, aFont->mFont.style, aCanStoreInRuleTree,
              SETDSC_ENUMERATED | SETDSC_SYSTEM_FONT,
              aParentFont->mFont.style,
              defaultVariableFont->style,
              0, 0, 0, systemFont.style);

  // font-variant: enum, inherit, initial, -moz-system-font
  SetDiscrete(aFontData.mVariant, aFont->mFont.variant, aCanStoreInRuleTree,
              SETDSC_ENUMERATED | SETDSC_SYSTEM_FONT,
              aParentFont->mFont.variant,
              defaultVariableFont->variant,
              0, 0, 0, systemFont.variant);

  // font-weight: int, enum, inherit, initial, -moz-system-font
  // special handling for enum
  if (eCSSUnit_Enumerated == aFontData.mWeight.GetUnit()) {
    PRInt32 value = aFontData.mWeight.GetIntValue();
    switch (value) {
      case NS_STYLE_FONT_WEIGHT_NORMAL:
      case NS_STYLE_FONT_WEIGHT_BOLD:
        aFont->mFont.weight = value;
        break;
      case NS_STYLE_FONT_WEIGHT_BOLDER:
      case NS_STYLE_FONT_WEIGHT_LIGHTER:
        aCanStoreInRuleTree = PR_FALSE;
        aFont->mFont.weight = nsStyleUtil::ConstrainFontWeight(aParentFont->mFont.weight + value);
        break;
    }
  } else
    SetDiscrete(aFontData.mWeight, aFont->mFont.weight, aCanStoreInRuleTree,
                SETDSC_INTEGER | SETDSC_SYSTEM_FONT,
                aParentFont->mFont.weight,
                defaultVariableFont->weight,
                0, 0, 0, systemFont.weight);

  // font-stretch: enum, inherit
  if (eCSSUnit_Enumerated == aFontData.mStretch.GetUnit()) {
    PRInt32 value = aFontData.mStretch.GetIntValue();
    switch (value) {
      case NS_FONT_STRETCH_WIDER:
      case NS_FONT_STRETCH_NARROWER:
        aCanStoreInRuleTree = PR_FALSE;
        aFont->mFont.stretch = aParentFont->mFont.stretch + value;
        break;
      default:
        aFont->mFont.stretch = value;
        break;
    }
  } else
    SetDiscrete(aFontData.mStretch, aFont->mFont.stretch, aCanStoreInRuleTree,
                SETDSC_SYSTEM_FONT,
                aParentFont->mFont.stretch,
                defaultVariableFont->stretch,
                0, 0, 0, systemFont.stretch);

#ifdef MOZ_MATHML
  // Compute scriptlevel, scriptminsize and scriptsizemultiplier now so
  // they're available for font-size computation.

  // -moz-script-min-size: length
  if (aFontData.mScriptMinSize.IsLengthUnit()) {
    // scriptminsize in font units (em, ex) has to be interpreted relative
    // to the parent font, or the size definitions are circular and we
    //
    aFont->mScriptMinSize =
      CalcLengthWith(aFontData.mScriptMinSize, aParentFont->mSize, aParentFont,
                     nsnull, aPresContext, atRoot, PR_TRUE,
                     aCanStoreInRuleTree);
  }

  // -moz-script-size-multiplier: factor, inherit, initial
  SetFactor(aFontData.mScriptSizeMultiplier, aFont->mScriptSizeMultiplier,
            aCanStoreInRuleTree, aParentFont->mScriptSizeMultiplier,
            NS_MATHML_DEFAULT_SCRIPT_SIZE_MULTIPLIER,
            SETFCT_POSITIVE);

  // -moz-script-level: integer, number, inherit
  if (eCSSUnit_Integer == aFontData.mScriptLevel.GetUnit()) {
    // "relative"
    aFont->mScriptLevel = ClampTo8Bit(aParentFont->mScriptLevel + aFontData.mScriptLevel.GetIntValue());
  }
  else if (eCSSUnit_Number == aFontData.mScriptLevel.GetUnit()) {
    // "absolute"
    aFont->mScriptLevel = ClampTo8Bit(PRInt32(aFontData.mScriptLevel.GetFloatValue()));
  }
  else if (eCSSUnit_Inherit == aFontData.mScriptSizeMultiplier.GetUnit()) {
    aCanStoreInRuleTree = PR_FALSE;
    aFont->mScriptLevel = aParentFont->mScriptLevel;
  }
  else if (eCSSUnit_Initial == aFontData.mScriptSizeMultiplier.GetUnit()) {
    aFont->mScriptLevel = 0;
  }
#endif

  // font-feature-settings
  if (eCSSUnit_Inherit == aFontData.mFontFeatureSettings.GetUnit()) {
    aCanStoreInRuleTree = PR_FALSE;
    aFont->mFont.featureSettings = aParentFont->mFont.featureSettings;
  } else if (eCSSUnit_Normal == aFontData.mFontFeatureSettings.GetUnit() ||
             eCSSUnit_Initial == aFontData.mFontFeatureSettings.GetUnit()) {
    aFont->mFont.featureSettings.Truncate();
  } else if (eCSSUnit_System_Font == aFontData.mFontFeatureSettings.GetUnit()) {
    aFont->mFont.featureSettings = systemFont.featureSettings;
  } else if (eCSSUnit_String == aFontData.mFontFeatureSettings.GetUnit()) {
    aFontData.mFontFeatureSettings.GetStringValue(aFont->mFont.featureSettings);
  }

  // font-language-override
  if (eCSSUnit_Inherit == aFontData.mFontLanguageOverride.GetUnit()) {
    aCanStoreInRuleTree = PR_FALSE;
    aFont->mFont.languageOverride = aParentFont->mFont.languageOverride;
  } else if (eCSSUnit_Normal == aFontData.mFontLanguageOverride.GetUnit() ||
             eCSSUnit_Initial == aFontData.mFontLanguageOverride.GetUnit()) {
    aFont->mFont.languageOverride.Truncate();
  } else if (eCSSUnit_System_Font == aFontData.mFontLanguageOverride.GetUnit()) {
    aFont->mFont.languageOverride = systemFont.languageOverride;
  } else if (eCSSUnit_String == aFontData.mFontLanguageOverride.GetUnit()) {
    aFontData.mFontLanguageOverride.GetStringValue(aFont->mFont.languageOverride);
  }

  // font-size: enum, length, percent, inherit
  nscoord scriptLevelAdjustedParentSize = aParentFont->mSize;
#ifdef MOZ_MATHML
  nscoord scriptLevelAdjustedUnconstrainedParentSize;
  scriptLevelAdjustedParentSize =
    ComputeScriptLevelSize(aFont, aParentFont, aPresContext,
                           &scriptLevelAdjustedUnconstrainedParentSize);
  NS_ASSERTION(!aUsedStartStruct || aFont->mScriptUnconstrainedSize == aFont->mSize,
               "If we have a start struct, we should have reset everything coming in here");
#endif
  SetFontSize(aPresContext, aFontData, aFont, aParentFont, &aFont->mSize,
              systemFont, aParentFont->mSize, scriptLevelAdjustedParentSize,
              aUsedStartStruct, atRoot, aCanStoreInRuleTree);
#ifdef MOZ_MATHML
  if (aParentFont->mSize == aParentFont->mScriptUnconstrainedSize &&
      scriptLevelAdjustedParentSize == scriptLevelAdjustedUnconstrainedParentSize) {
    // Fast path: we have not been affected by scriptminsize so we don't
    // need to call SetFontSize again to compute the
    // scriptminsize-unconstrained size. This is OK even if we have a
    // start struct, because if we have a start struct then 'font-size'
    // was specified and so scriptminsize has no effect.
    aFont->mScriptUnconstrainedSize = aFont->mSize;
  } else {
    SetFontSize(aPresContext, aFontData, aFont, aParentFont,
                &aFont->mScriptUnconstrainedSize, systemFont,
                aParentFont->mScriptUnconstrainedSize,
                scriptLevelAdjustedUnconstrainedParentSize,
                aUsedStartStruct, atRoot, aCanStoreInRuleTree);
  }
  NS_ASSERTION(aFont->mScriptUnconstrainedSize <= aFont->mSize,
               "scriptminsize should never be making things bigger");
#endif

  // enforce the user' specified minimum font-size on the value that we expose
  // (but don't change font-size:0)
  if (0 < aFont->mSize && aFont->mSize < aMinFontSize)
    aFont->mFont.size = aMinFontSize;
  else
    aFont->mFont.size = aFont->mSize;

  // font-size-adjust: number, none, inherit, initial, -moz-system-font
  if (eCSSUnit_System_Font == aFontData.mSizeAdjust.GetUnit()) {
    aFont->mFont.sizeAdjust = systemFont.sizeAdjust;
  } else
    SetFactor(aFontData.mSizeAdjust, aFont->mFont.sizeAdjust,
              aCanStoreInRuleTree, aParentFont->mFont.sizeAdjust, 0.0f,
              SETFCT_NONE);
}

// SetGenericFont:
//  - backtrack to an ancestor with the same generic font name (possibly
//    up to the root where default values come from the presentation context)
//  - re-apply cascading rules from there without caching intermediate values
/* static */ void
nsRuleNode::SetGenericFont(nsPresContext* aPresContext,
                           nsStyleContext* aContext,
                           PRUint8 aGenericFontID, nscoord aMinFontSize,
                           nsStyleFont* aFont)
{
  // walk up the contexts until a context with the desired generic font
  nsAutoTArray<nsStyleContext*, 8> contextPath;
  contextPath.AppendElement(aContext);
  nsStyleContext* higherContext = aContext->GetParent();
  while (higherContext) {
    if (higherContext->GetStyleFont()->mGenericID == aGenericFontID) {
      // done walking up the higher contexts
      break;
    }
    contextPath.AppendElement(higherContext);
    higherContext = higherContext->GetParent();
  }

  // re-apply the cascading rules, starting from the higher context

  // If we stopped earlier because we reached the root of the style tree,
  // we will start with the default generic font from the presentation
  // context. Otherwise we start with the higher context.
  const nsFont* defaultFont = aPresContext->GetDefaultFont(aGenericFontID);
  nsStyleFont parentFont(*defaultFont, aPresContext);
  if (higherContext) {
    const nsStyleFont* tmpFont = higherContext->GetStyleFont();
    parentFont = *tmpFont;
  }
  *aFont = parentFont;

  PRBool dummy;
  PRUint32 fontBit = nsCachedStyleData::GetBitForSID(eStyleStruct_Font);

  for (PRInt32 i = contextPath.Length() - 1; i >= 0; --i) {
    nsStyleContext* context = contextPath[i];
    nsRuleDataFont fontData; // Declare a struct with null CSS values.
    nsRuleData ruleData(NS_STYLE_INHERIT_BIT(Font), aPresContext, context);
    ruleData.mFontData = &fontData;

    // Trimmed down version of ::WalkRuleTree() to re-apply the style rules
    // Note that we *do* need to do this for our own data, since what is
    // in |fontData| in ComputeFontData is only for the rules below
    // aStartStruct.
    for (nsRuleNode* ruleNode = context->GetRuleNode(); ruleNode;
         ruleNode = ruleNode->GetParent()) {
      if (ruleNode->mNoneBits & fontBit)
        // no more font rules on this branch, get out
        break;

      nsIStyleRule *rule = ruleNode->GetRule();
      if (rule) {
        ruleData.mLevel = ruleNode->GetLevel();
        ruleData.mIsImportantRule = ruleNode->IsImportantRule();
        rule->MapRuleInfoInto(&ruleData);
      }
    }

    // Compute the delta from the information that the rules specified

    // Avoid unnecessary operations in SetFont().  But we care if it's
    // the final value that we're computing.
    if (i != 0)
      fontData.mFamily.Reset();

    nsRuleNode::SetFont(aPresContext, context, aMinFontSize,
                        aGenericFontID, fontData, &parentFont, aFont,
                        PR_FALSE, dummy);

    // XXX Not sure if we need to do this here
    // If we have a post-resolve callback, handle that now.
    if (ruleData.mPostResolveCallback)
      (ruleData.mPostResolveCallback)(aFont, &ruleData);

    parentFont = *aFont;
  }
}

static PRBool ExtractGeneric(const nsString& aFamily, PRBool aGeneric,
                             void *aData)
{
  nsAutoString *data = static_cast<nsAutoString*>(aData);

  if (aGeneric) {
    *data = aFamily;
    return PR_FALSE; // stop enumeration
  }
  return PR_TRUE;
}

const void*
nsRuleNode::ComputeFontData(void* aStartStruct,
                            const nsRuleDataStruct& aData,
                            nsStyleContext* aContext,
                            nsRuleNode* aHighestNode,
                            const RuleDetail aRuleDetail,
                            const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_INHERITED(Font, (mPresContext), font, parentFont,
                          Font, fontData)

  // NOTE:  The |aRuleDetail| passed in is a little bit conservative due
  // to the -moz-system-font property.  We really don't need to consider
  // it here in determining whether to cache in the rule tree.  However,
  // we do need to consider it in WalkRuleTree when deciding whether to
  // walk further up the tree.  So this means that when the font struct
  // is fully specified using *longhand* properties (excluding
  // -moz-system-font), we won't cache in the rule tree even though we
  // could.  However, it's pretty unlikely authors will do that
  // (although there is a pretty good chance they'll fully specify it
  // using the 'font' shorthand).

  // See if there is a minimum font-size constraint to honor
  nscoord minimumFontSize =
    mPresContext->GetCachedIntPref(kPresContext_MinimumFontSize);

  if (minimumFontSize < 0)
    minimumFontSize = 0;

  PRBool useDocumentFonts =
    mPresContext->GetCachedBoolPref(kPresContext_UseDocumentFonts);

  // See if we are in the chrome
  // We only need to know this to determine if we have to use the
  // document fonts (overriding the useDocumentFonts flag), or to
  // determine if we have to override the minimum font-size constraint.
  if ((!useDocumentFonts || minimumFontSize > 0) && mPresContext->IsChrome()) {
    // if we are not using document fonts, but this is a XUL document,
    // then we use the document fonts anyway
    useDocumentFonts = PR_TRUE;
    minimumFontSize = 0;
  }

  // Figure out if we are a generic font
  PRUint8 generic = kGenericFont_NONE;
  // XXXldb What if we would have had a string if we hadn't been doing
  // the optimization with a non-null aStartStruct?
  if (eCSSUnit_Families == fontData.mFamily.GetUnit()) {
    fontData.mFamily.GetStringValue(font->mFont.name);
    // XXXldb Do we want to extract the generic for this if it's not only a
    // generic?
    nsFont::GetGenericID(font->mFont.name, &generic);

    // If we aren't allowed to use document fonts, then we are only entitled
    // to use the user's default variable-width font and fixed-width font
    if (!useDocumentFonts) {
      // Extract the generic from the specified font family...
      nsAutoString genericName;
      if (!font->mFont.EnumerateFamilies(ExtractGeneric, &genericName)) {
        // The specified font had a generic family.
        font->mFont.name = genericName;
        nsFont::GetGenericID(genericName, &generic);

        // ... and only use it if it's -moz-fixed or monospace
        if (generic != kGenericFont_moz_fixed &&
            generic != kGenericFont_monospace) {
          font->mFont.name.Truncate();
          generic = kGenericFont_NONE;
        }
      } else {
        // The specified font did not have a generic family.
        font->mFont.name.Truncate();
        generic = kGenericFont_NONE;
      }
    }
  }

  // Now compute our font struct
  if (generic == kGenericFont_NONE) {
    // continue the normal processing
    nsRuleNode::SetFont(mPresContext, aContext, minimumFontSize, generic,
                        fontData, parentFont, font,
                        aStartStruct != nsnull, canStoreInRuleTree);
  }
  else {
    // re-calculate the font as a generic font
    canStoreInRuleTree = PR_FALSE;
    nsRuleNode::SetGenericFont(mPresContext, aContext, generic,
                               minimumFontSize, font);
  }

  COMPUTE_END_INHERITED(Font, font)
}

template <typename T>
inline PRUint32 ListLength(const T* aList)
{
  PRUint32 len = 0;
  while (aList) {
    len++;
    aList = aList->mNext;
  }
  return len;
}



already_AddRefed<nsCSSShadowArray>
nsRuleNode::GetShadowData(const nsCSSValueList* aList,
                          nsStyleContext* aContext,
                          PRBool aIsBoxShadow,
                          PRBool& canStoreInRuleTree)
{
  PRUint32 arrayLength = ListLength(aList);

  NS_ABORT_IF_FALSE(arrayLength > 0,
                    "Non-null text-shadow list, yet we counted 0 items.");
  nsCSSShadowArray* shadowList = new(arrayLength) nsCSSShadowArray(arrayLength);

  if (!shadowList)
    return nsnull;

  nsStyleCoord tempCoord;
  PRBool unitOK;
  for (nsCSSShadowItem* item = shadowList->ShadowAt(0);
       aList;
       aList = aList->mNext, ++item) {
    NS_ABORT_IF_FALSE(aList->mValue.GetUnit() == eCSSUnit_Array,
                      "expecting a plain array value");
    nsCSSValue::Array *arr = aList->mValue.GetArrayValue();
    // OK to pass bad aParentCoord since we're not passing SETCOORD_INHERIT
    unitOK = SetCoord(arr->Item(0), tempCoord, nsStyleCoord(),
                      SETCOORD_LENGTH | SETCOORD_CALC_LENGTH_ONLY,
                      aContext, mPresContext, canStoreInRuleTree);
    NS_ASSERTION(unitOK, "unexpected unit");
    item->mXOffset = tempCoord.GetCoordValue();

    unitOK = SetCoord(arr->Item(1), tempCoord, nsStyleCoord(),
                      SETCOORD_LENGTH | SETCOORD_CALC_LENGTH_ONLY,
                      aContext, mPresContext, canStoreInRuleTree);
    NS_ASSERTION(unitOK, "unexpected unit");
    item->mYOffset = tempCoord.GetCoordValue();

    // Blur radius is optional in the current box-shadow spec
    if (arr->Item(2).GetUnit() != eCSSUnit_Null) {
      unitOK = SetCoord(arr->Item(2), tempCoord, nsStyleCoord(),
                        SETCOORD_LENGTH | SETCOORD_CALC_LENGTH_ONLY |
                          SETCOORD_CALC_CLAMP_NONNEGATIVE,
                        aContext, mPresContext, canStoreInRuleTree);
      NS_ASSERTION(unitOK, "unexpected unit");
      item->mRadius = tempCoord.GetCoordValue();
    } else {
      item->mRadius = 0;
    }

    // Find the spread radius
    if (aIsBoxShadow && arr->Item(3).GetUnit() != eCSSUnit_Null) {
      unitOK = SetCoord(arr->Item(3), tempCoord, nsStyleCoord(),
                        SETCOORD_LENGTH | SETCOORD_CALC_LENGTH_ONLY,
                        aContext, mPresContext, canStoreInRuleTree);
      NS_ASSERTION(unitOK, "unexpected unit");
      item->mSpread = tempCoord.GetCoordValue();
    } else {
      item->mSpread = 0;
    }

    if (arr->Item(4).GetUnit() != eCSSUnit_Null) {
      item->mHasColor = PR_TRUE;
      // 2nd argument can be bogus since inherit is not a valid color
      unitOK = SetColor(arr->Item(4), 0, mPresContext, aContext, item->mColor,
                        canStoreInRuleTree);
      NS_ASSERTION(unitOK, "unexpected unit");
    }

    if (aIsBoxShadow && arr->Item(5).GetUnit() == eCSSUnit_Enumerated) {
      NS_ASSERTION(arr->Item(5).GetIntValue() == NS_STYLE_BOX_SHADOW_INSET,
                   "invalid keyword type for box shadow");
      item->mInset = PR_TRUE;
    } else {
      item->mInset = PR_FALSE;
    }
  }

  NS_ADDREF(shadowList);
  return shadowList;
}

const void*
nsRuleNode::ComputeTextData(void* aStartStruct,
                            const nsRuleDataStruct& aData,
                            nsStyleContext* aContext,
                            nsRuleNode* aHighestNode,
                            const RuleDetail aRuleDetail,
                            const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_INHERITED(Text, (), text, parentText, Text, textData)

  // tab-size: integer, inherit
  SetDiscrete(textData.mTabSize, text->mTabSize, canStoreInRuleTree,
              SETDSC_INTEGER, parentText->mTabSize,
              NS_STYLE_TABSIZE_INITIAL, 0, 0, 0, 0);

  // letter-spacing: normal, length, inherit
  SetCoord(textData.mLetterSpacing, text->mLetterSpacing, parentText->mLetterSpacing,
           SETCOORD_LH | SETCOORD_NORMAL | SETCOORD_INITIAL_NORMAL |
             SETCOORD_CALC_LENGTH_ONLY,
           aContext, mPresContext, canStoreInRuleTree);

  // text-shadow: none, list, inherit, initial
  if (textData.mTextShadow.GetUnit() != eCSSUnit_Null) {
    text->mTextShadow = nsnull;

    // Don't need to handle none/initial explicitly: The above assignment
    // takes care of that
    if (textData.mTextShadow.GetUnit() == eCSSUnit_Inherit) {
      canStoreInRuleTree = PR_FALSE;
      text->mTextShadow = parentText->mTextShadow;
    } else if (textData.mTextShadow.GetUnit() == eCSSUnit_List ||
               textData.mTextShadow.GetUnit() == eCSSUnit_ListDep) {
      // List of arrays
      text->mTextShadow = GetShadowData(textData.mTextShadow.GetListValue(),
                                        aContext, PR_FALSE, canStoreInRuleTree);
    }
  }

  // line-height: normal, number, length, percent, inherit
  if (eCSSUnit_Percent == textData.mLineHeight.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    // Use |mFont.size| to pick up minimum font size.
    text->mLineHeight.SetCoordValue(
        nscoord(float(aContext->GetStyleFont()->mFont.size) *
                textData.mLineHeight.GetPercentValue()));
  }
  else if (eCSSUnit_Initial == textData.mLineHeight.GetUnit() ||
           eCSSUnit_System_Font == textData.mLineHeight.GetUnit()) {
    text->mLineHeight.SetNormalValue();
  }
  else {
    SetCoord(textData.mLineHeight, text->mLineHeight, parentText->mLineHeight,
             SETCOORD_LEH | SETCOORD_FACTOR | SETCOORD_NORMAL,
             aContext, mPresContext, canStoreInRuleTree);
    if (textData.mLineHeight.IsLengthUnit() &&
        !textData.mLineHeight.IsRelativeLengthUnit()) {
      nscoord lh = nsStyleFont::ZoomText(mPresContext,
                                         text->mLineHeight.GetCoordValue());
      nscoord minimumFontSize =
        mPresContext->GetCachedIntPref(kPresContext_MinimumFontSize);

      if (minimumFontSize > 0 && !mPresContext->IsChrome()) {
        // If we applied a minimum font size, scale the line height by
        // the same ratio.  (If we *might* have applied a minimum font
        // size, we can't cache in the rule tree.)
        canStoreInRuleTree = PR_FALSE;
        const nsStyleFont *font = aContext->GetStyleFont();
        if (font->mSize != 0) {
          lh = nscoord(float(lh) * float(font->mFont.size) / float(font->mSize));
        } else {
          lh = minimumFontSize;
        }
      }
      text->mLineHeight.SetCoordValue(lh);
    }
  }


  // text-align: enum, string, inherit, initial
  if (eCSSUnit_String == textData.mTextAlign.GetUnit()) {
    NS_NOTYETIMPLEMENTED("align string");
  } else if (eCSSUnit_Enumerated == textData.mTextAlign.GetUnit() &&
             NS_STYLE_TEXT_ALIGN_MOZ_CENTER_OR_INHERIT ==
               textData.mTextAlign.GetIntValue()) {
    canStoreInRuleTree = PR_FALSE;
    PRUint8 parentAlign = parentText->mTextAlign;
    text->mTextAlign = (NS_STYLE_TEXT_ALIGN_DEFAULT == parentAlign) ?
      NS_STYLE_TEXT_ALIGN_CENTER : parentAlign;
  } else
    SetDiscrete(textData.mTextAlign, text->mTextAlign, canStoreInRuleTree,
                SETDSC_ENUMERATED, parentText->mTextAlign,
                NS_STYLE_TEXT_ALIGN_DEFAULT,
                0, 0, 0, 0);

  // text-indent: length, percent, inherit, initial
  SetCoord(textData.mTextIndent, text->mTextIndent, parentText->mTextIndent,
           SETCOORD_LPH | SETCOORD_INITIAL_ZERO, aContext,
           mPresContext, canStoreInRuleTree);

  // text-transform: enum, inherit, initial
  SetDiscrete(textData.mTextTransform, text->mTextTransform, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentText->mTextTransform,
              NS_STYLE_TEXT_TRANSFORM_NONE, 0, 0, 0, 0);

  // white-space: enum, inherit, initial
  SetDiscrete(textData.mWhiteSpace, text->mWhiteSpace, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentText->mWhiteSpace,
              NS_STYLE_WHITESPACE_NORMAL, 0, 0, 0, 0);

  // word-spacing: normal, length, inherit
  nsStyleCoord tempCoord;
  if (SetCoord(textData.mWordSpacing, tempCoord,
               nsStyleCoord(parentText->mWordSpacing,
                            nsStyleCoord::CoordConstructor),
               SETCOORD_LH | SETCOORD_NORMAL | SETCOORD_INITIAL_NORMAL |
                 SETCOORD_CALC_LENGTH_ONLY,
               aContext, mPresContext, canStoreInRuleTree)) {
    if (tempCoord.GetUnit() == eStyleUnit_Coord) {
      text->mWordSpacing = tempCoord.GetCoordValue();
    } else if (tempCoord.GetUnit() == eStyleUnit_Normal) {
      text->mWordSpacing = 0;
    } else {
      NS_NOTREACHED("unexpected unit");
    }
  } else {
    NS_ASSERTION(textData.mWordSpacing.GetUnit() == eCSSUnit_Null,
                 "unexpected unit");
  }

  // word-wrap: enum, inherit, initial
  SetDiscrete(textData.mWordWrap, text->mWordWrap, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentText->mWordWrap,
              NS_STYLE_WORDWRAP_NORMAL, 0, 0, 0, 0);

  COMPUTE_END_INHERITED(Text, text)
}

const void*
nsRuleNode::ComputeTextResetData(void* aStartStruct,
                                 const nsRuleDataStruct& aData,
                                 nsStyleContext* aContext,
                                 nsRuleNode* aHighestNode,
                                 const RuleDetail aRuleDetail,
                                 const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_RESET(TextReset, (), text, parentText, Text, textData)

  // vertical-align: enum, length, percent, inherit
  if (!SetCoord(textData.mVerticalAlign, text->mVerticalAlign,
                parentText->mVerticalAlign, SETCOORD_LPH | SETCOORD_ENUMERATED,
                aContext, mPresContext, canStoreInRuleTree)) {
    if (eCSSUnit_Initial == textData.mVerticalAlign.GetUnit()) {
      text->mVerticalAlign.SetIntValue(NS_STYLE_VERTICAL_ALIGN_BASELINE,
                                       eStyleUnit_Enumerated);
    }
  }

  // text-decoration: enum (bit field), inherit, initial
  if (eCSSUnit_Enumerated == textData.mDecoration.GetUnit()) {
    PRInt32 td = textData.mDecoration.GetIntValue();
    text->mTextDecoration = td;
    if (td & NS_STYLE_TEXT_DECORATION_PREF_ANCHORS) {
      PRBool underlineLinks =
        mPresContext->GetCachedBoolPref(kPresContext_UnderlineLinks);
      if (underlineLinks) {
        text->mTextDecoration |= NS_STYLE_TEXT_DECORATION_UNDERLINE;
      }
      else {
        text->mTextDecoration &= ~NS_STYLE_TEXT_DECORATION_UNDERLINE;
      }
    }
  } else if (eCSSUnit_Inherit == textData.mDecoration.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    text->mTextDecoration = parentText->mTextDecoration;
  } else if (eCSSUnit_Initial == textData.mDecoration.GetUnit()) {
    text->mTextDecoration = NS_STYLE_TEXT_DECORATION_NONE;
  }

  // unicode-bidi: enum, inherit, initial
  SetDiscrete(textData.mUnicodeBidi, text->mUnicodeBidi, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentText->mUnicodeBidi,
              NS_STYLE_UNICODE_BIDI_NORMAL, 0, 0, 0, 0);

  COMPUTE_END_RESET(TextReset, text)
}

const void*
nsRuleNode::ComputeUserInterfaceData(void* aStartStruct,
                                     const nsRuleDataStruct& aData,
                                     nsStyleContext* aContext,
                                     nsRuleNode* aHighestNode,
                                     const RuleDetail aRuleDetail,
                                     const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_INHERITED(UserInterface, (), ui, parentUI,
                          UserInterface, uiData)

  // cursor: enum, url, inherit
  nsCSSUnit cursorUnit = uiData.mCursor.GetUnit();
  if (cursorUnit != eCSSUnit_Null) {
    delete [] ui->mCursorArray;
    ui->mCursorArray = nsnull;
    ui->mCursorArrayLength = 0;

    if (cursorUnit == eCSSUnit_Inherit) {
      canStoreInRuleTree = PR_FALSE;
      ui->mCursor = parentUI->mCursor;
      ui->CopyCursorArrayFrom(*parentUI);
    }
    else if (cursorUnit == eCSSUnit_Initial) {
      ui->mCursor = NS_STYLE_CURSOR_AUTO;
    }
    else {
      // The parser will never create a list that is *all* URL values --
      // that's invalid.
      NS_ABORT_IF_FALSE(cursorUnit == eCSSUnit_List ||
                        cursorUnit == eCSSUnit_ListDep,
                        nsPrintfCString(64, "unrecognized cursor unit %d",
                                        cursorUnit).get());
      const nsCSSValueList* list = uiData.mCursor.GetListValue();
      const nsCSSValueList* list2 = list;
      PRUint32 arrayLength = 0;
      for ( ; list->mValue.GetUnit() == eCSSUnit_Array; list = list->mNext)
        if (list->mValue.GetArrayValue()->Item(0).GetImageValue())
          ++arrayLength;

      if (arrayLength != 0) {
        ui->mCursorArray = new nsCursorImage[arrayLength];
        if (ui->mCursorArray) {
          ui->mCursorArrayLength = arrayLength;

          for (nsCursorImage *item = ui->mCursorArray;
               list2->mValue.GetUnit() == eCSSUnit_Array;
               list2 = list2->mNext) {
            nsCSSValue::Array *arr = list2->mValue.GetArrayValue();
            imgIRequest *req = arr->Item(0).GetImageValue();
            if (req) {
              item->SetImage(req);
              if (arr->Item(1).GetUnit() != eCSSUnit_Null) {
                item->mHaveHotspot = PR_TRUE;
                item->mHotspotX = arr->Item(1).GetFloatValue(),
                item->mHotspotY = arr->Item(2).GetFloatValue();
              }
              ++item;
            }
          }
        }
      }

      NS_ASSERTION(list, "Must have non-array value at the end");
      NS_ASSERTION(list->mValue.GetUnit() == eCSSUnit_Enumerated,
                   "Unexpected fallback value at end of cursor list");
      ui->mCursor = list->mValue.GetIntValue();
    }
  }

  // user-input: enum, inherit, initial
  SetDiscrete(uiData.mUserInput, ui->mUserInput, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentUI->mUserInput,
              NS_STYLE_USER_INPUT_AUTO, 0, 0, 0, 0);

  // user-modify: enum, inherit, initial
  SetDiscrete(uiData.mUserModify, ui->mUserModify, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentUI->mUserModify,
              NS_STYLE_USER_MODIFY_READ_ONLY,
              0, 0, 0, 0);

  // user-focus: enum, inherit, initial
  SetDiscrete(uiData.mUserFocus, ui->mUserFocus, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentUI->mUserFocus,
              NS_STYLE_USER_FOCUS_NONE, 0, 0, 0, 0);

  COMPUTE_END_INHERITED(UserInterface, ui)
}

const void*
nsRuleNode::ComputeUIResetData(void* aStartStruct,
                               const nsRuleDataStruct& aData,
                               nsStyleContext* aContext,
                               nsRuleNode* aHighestNode,
                               const RuleDetail aRuleDetail,
                               const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_RESET(UIReset, (), ui, parentUI, UserInterface, uiData)

  // user-select: enum, inherit, initial
  SetDiscrete(uiData.mUserSelect, ui->mUserSelect, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentUI->mUserSelect,
              NS_STYLE_USER_SELECT_AUTO, 0, 0, 0, 0);

  // ime-mode: enum, inherit, initial
  SetDiscrete(uiData.mIMEMode, ui->mIMEMode, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentUI->mIMEMode,
              NS_STYLE_IME_MODE_AUTO, 0, 0, 0, 0);

  // force-broken-image-icons: integer, inherit, initial
  SetDiscrete(uiData.mForceBrokenImageIcon, ui->mForceBrokenImageIcon,
              canStoreInRuleTree,
              SETDSC_INTEGER,
              parentUI->mForceBrokenImageIcon,
              0, 0, 0, 0, 0);

  // -moz-window-shadow: enum, inherit, initial
  SetDiscrete(uiData.mWindowShadow, ui->mWindowShadow, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentUI->mWindowShadow,
              NS_STYLE_WINDOW_SHADOW_DEFAULT, 0, 0, 0, 0);

  COMPUTE_END_RESET(UIReset, ui)
}

// Information about each transition property that is constant.
struct TransitionPropInfo {
  // Location of the property's specified value.
  nsCSSValue nsRuleDataDisplay::* rdList;
  // Location of the count of the property's computed value.
  PRUint32 nsStyleDisplay::* sdCount;
};

// Each property's index in this array must match its index in the
// mutable array |transitionPropData| below.
static const TransitionPropInfo transitionPropInfo[4] = {
  { &nsRuleDataDisplay::mTransitionDelay,
    &nsStyleDisplay::mTransitionDelayCount },
  { &nsRuleDataDisplay::mTransitionDuration,
    &nsStyleDisplay::mTransitionDurationCount },
  { &nsRuleDataDisplay::mTransitionProperty,
    &nsStyleDisplay::mTransitionPropertyCount },
  { &nsRuleDataDisplay::mTransitionTimingFunction,
    &nsStyleDisplay::mTransitionTimingFunctionCount },
};

// Information about each transition property that changes during
// ComputeDisplayData.
struct TransitionPropData {
  const nsCSSValueList *list;
  nsCSSUnit unit;
  PRUint32 num;
};

const void*
nsRuleNode::ComputeDisplayData(void* aStartStruct,
                               const nsRuleDataStruct& aData,
                               nsStyleContext* aContext,
                               nsRuleNode* aHighestNode,
                               const RuleDetail aRuleDetail,
                               const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_RESET(Display, (), display, parentDisplay,
                      Display, displayData)

  // Each property's index in this array must match its index in the
  // const array |transitionPropInfo| above.
  TransitionPropData transitionPropData[4];
  TransitionPropData& delay = transitionPropData[0];
  TransitionPropData& duration = transitionPropData[1];
  TransitionPropData& property = transitionPropData[2];
  TransitionPropData& timingFunction = transitionPropData[3];

#define FOR_ALL_TRANSITION_PROPS(var_) \
                                      for (PRUint32 var_ = 0; var_ < 4; ++var_)

  // CSS Transitions

  // The four transition properties are stored in nsCSSDisplay in a
  // single array for all properties.  The number of transitions is
  // equal to the number of items in the longest property's value.
  // Properties that have fewer values than the longest are filled in by
  // repeating the list.  However, this repetition does not extend the
  // computed value of that particular property (for purposes of
  // inheritance, or, in our code, for when other properties are
  // overridden by a more specific rule).

  // But actually, since the spec isn't clear yet, we'll fully compute
  // all of them (so we can switch easily later), but only care about
  // the ones up to the number of items for 'transition-property', per
  // http://lists.w3.org/Archives/Public/www-style/2009Aug/0109.html .

  // Transitions are difficult to handle correctly because of this.  For
  // example, we need to handle scenarios such as:
  //  * a more general rule specifies transition-property: a, b, c;
  //  * a more specific rule overrides as transition-property: d;
  //
  // If only the general rule applied, we would fill in the extra
  // properties (duration, delay, etc) with initial values to create 3
  // fully-specified transitions.  But when the more specific rule
  // applies, we should only create a single transition.  In order to do
  // this we need to remember which properties were explicitly specified
  // and which ones were just filled in with initial values to get a
  // fully-specified transition, which we do by remembering the number
  // of values for each property.

  PRUint32 numTransitions = 0;
  FOR_ALL_TRANSITION_PROPS(p) {
    const TransitionPropInfo& i = transitionPropInfo[p];
    TransitionPropData& d = transitionPropData[p];

    // cache whether any of the properties are specified as 'inherit' so
    // we can use it below

    const nsCSSValue& value = displayData.*(i.rdList);
    d.unit = value.GetUnit();
    d.list = (value.GetUnit() == eCSSUnit_List ||
              value.GetUnit() == eCSSUnit_ListDep)
      ? value.GetListValue() : nsnull;
    d.num = 0;

    // General algorithm to determine how many total transitions we need
    // to build.  For each property:
    //  - if there is no value specified in for the property in
    //    displayData, use the values from the start struct, but only if
    //    they were explicitly specified
    //  - if there is a value specified for the property in displayData:
    //    - if the value is 'inherit', count the number of values for
    //      that property are specified by the parent, but only those
    //      that were explicitly specified
    //    - otherwise, count the number of values specified in displayData


    // calculate number of elements
    if (d.unit == eCSSUnit_Inherit) {
      d.num = parentDisplay->*(i.sdCount);
      canStoreInRuleTree = PR_FALSE;
    } else if (d.list) {
      d.num = ListLength(d.list);
    } else {
      d.num = display->*(i.sdCount);
    }
    if (d.num > numTransitions)
      numTransitions = d.num;
  }

  if (!display->mTransitions.SetLength(numTransitions)) {
    NS_WARNING("failed to allocate transitions array");
    display->mTransitions.SetLength(1);
    NS_ABORT_IF_FALSE(display->mTransitions.Length() == 1,
                      "could not allocate using auto array buffer");
    numTransitions = 1;
    FOR_ALL_TRANSITION_PROPS(p) {
      TransitionPropData& d = transitionPropData[p];

      d.num = 1;
    }
  }

  FOR_ALL_TRANSITION_PROPS(p) {
    const TransitionPropInfo& i = transitionPropInfo[p];
    TransitionPropData& d = transitionPropData[p];

    display->*(i.sdCount) = d.num;
  }

  // Fill in the transitions we just allocated with the appropriate values.
  for (PRUint32 i = 0; i < numTransitions; ++i) {
    nsTransition *transition = &display->mTransitions[i];

    if (i >= delay.num) {
      transition->SetDelay(display->mTransitions[i % delay.num].GetDelay());
    } else if (delay.unit == eCSSUnit_Inherit) {
      // FIXME (Bug 522599) (for all transition properties): write a test that
      // detects when this was wrong for i >= delay.num if parent had
      // count for this property not equal to length
      NS_ABORT_IF_FALSE(i < parentDisplay->mTransitionDelayCount,
                        "delay.num computed incorrectly");
      NS_ABORT_IF_FALSE(!canStoreInRuleTree,
                        "should have made canStoreInRuleTree false above");
      transition->SetDelay(parentDisplay->mTransitions[i].GetDelay());
    } else if (delay.unit == eCSSUnit_Initial) {
      transition->SetDelay(0.0);
    } else if (delay.list) {
      switch (delay.list->mValue.GetUnit()) {
        case eCSSUnit_Seconds:
          transition->SetDelay(PR_MSEC_PER_SEC *
                               delay.list->mValue.GetFloatValue());
          break;
        case eCSSUnit_Milliseconds:
          transition->SetDelay(delay.list->mValue.GetFloatValue());
          break;
        default:
          NS_NOTREACHED("Invalid delay unit");
      }
    }

    if (i >= duration.num) {
      transition->SetDuration(
        display->mTransitions[i % duration.num].GetDuration());
    } else if (duration.unit == eCSSUnit_Inherit) {
      NS_ABORT_IF_FALSE(i < parentDisplay->mTransitionDurationCount,
                        "duration.num computed incorrectly");
      NS_ABORT_IF_FALSE(!canStoreInRuleTree,
                        "should have made canStoreInRuleTree false above");
      transition->SetDuration(parentDisplay->mTransitions[i].GetDuration());
    } else if (duration.unit == eCSSUnit_Initial) {
      transition->SetDuration(0.0);
    } else if (duration.list) {
      switch (duration.list->mValue.GetUnit()) {
        case eCSSUnit_Seconds:
          transition->SetDuration(PR_MSEC_PER_SEC *
                                  duration.list->mValue.GetFloatValue());
          break;
        case eCSSUnit_Milliseconds:
          transition->SetDuration(duration.list->mValue.GetFloatValue());
          break;
        default:
          NS_NOTREACHED("Invalid duration unit");
      }
    }

    if (i >= property.num) {
      transition->CopyPropertyFrom(display->mTransitions[i % property.num]);
    } else if (property.unit == eCSSUnit_Inherit) {
      NS_ABORT_IF_FALSE(i < parentDisplay->mTransitionPropertyCount,
                        "property.num computed incorrectly");
      NS_ABORT_IF_FALSE(!canStoreInRuleTree,
                        "should have made canStoreInRuleTree false above");
      transition->CopyPropertyFrom(parentDisplay->mTransitions[i]);
    } else if (property.unit == eCSSUnit_Initial ||
               property.unit == eCSSUnit_All) {
      transition->SetProperty(eCSSPropertyExtra_all_properties);
    } else if (property.unit == eCSSUnit_None) {
      transition->SetProperty(eCSSPropertyExtra_no_properties);
    } else if (property.list) {
      NS_ABORT_IF_FALSE(property.list->mValue.GetUnit() == eCSSUnit_Ident,
                        nsPrintfCString(64,
                                        "Invalid transition property unit %d",
                                        property.list->mValue.GetUnit()).get());

      nsDependentString
        propertyStr(property.list->mValue.GetStringBufferValue());
      nsCSSProperty prop = nsCSSProps::LookupProperty(propertyStr);
      if (prop == eCSSProperty_UNKNOWN) {
        transition->SetUnknownProperty(propertyStr);
      } else {
        transition->SetProperty(prop);
      }
    }

    if (i >= timingFunction.num) {
      transition->SetTimingFunction(
        display->mTransitions[i % timingFunction.num].GetTimingFunction());
    } else if (timingFunction.unit == eCSSUnit_Inherit) {
      NS_ABORT_IF_FALSE(i < parentDisplay->mTransitionTimingFunctionCount,
                        "timingFunction.num computed incorrectly");
      NS_ABORT_IF_FALSE(!canStoreInRuleTree,
                        "should have made canStoreInRuleTree false above");
      transition->SetTimingFunction(
        parentDisplay->mTransitions[i].GetTimingFunction());
    } else if (timingFunction.unit == eCSSUnit_Initial) {
      transition->SetTimingFunction(
        nsTimingFunction(NS_STYLE_TRANSITION_TIMING_FUNCTION_EASE));
    } else if (timingFunction.list) {
      switch (timingFunction.list->mValue.GetUnit()) {
        case eCSSUnit_Enumerated:
          transition->SetTimingFunction(
            nsTimingFunction(timingFunction.list->mValue.GetIntValue()));
          break;
        case eCSSUnit_Cubic_Bezier:
          {
            nsCSSValue::Array* array =
              timingFunction.list->mValue.GetArrayValue();
            NS_ASSERTION(array && array->Count() == 4,
                         "Need 4 control points");
            transition->SetTimingFunction(
              nsTimingFunction(array->Item(0).GetFloatValue(),
                               array->Item(1).GetFloatValue(),
                               array->Item(2).GetFloatValue(),
                               array->Item(3).GetFloatValue()));
          }
          break;
        default:
          NS_NOTREACHED("Invalid transition property unit");
      }
    }

    FOR_ALL_TRANSITION_PROPS(p) {
      const TransitionPropInfo& info = transitionPropInfo[p];
      TransitionPropData& d = transitionPropData[p];

      // if we're at the end of the list, start at the beginning and repeat
      // until we're out of transitions to populate
      if (d.list) {
        d.list = d.list->mNext ? d.list->mNext :
          (displayData.*(info.rdList)).GetListValue();
      }
    }
  }

  // opacity: factor, inherit, initial
  SetFactor(displayData.mOpacity, display->mOpacity, canStoreInRuleTree,
            parentDisplay->mOpacity, 1.0f, SETFCT_OPACITY);

  // display: enum, inherit, initial
  SetDiscrete(displayData.mDisplay, display->mDisplay, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentDisplay->mDisplay,
              NS_STYLE_DISPLAY_INLINE, 0, 0, 0, 0);

  // appearance: enum, inherit, initial
  SetDiscrete(displayData.mAppearance, display->mAppearance, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentDisplay->mAppearance,
              NS_THEME_NONE, 0, 0, 0, 0);

  // binding: url, none, inherit
  if (eCSSUnit_URL == displayData.mBinding.GetUnit()) {
    nsCSSValue::URL* url = displayData.mBinding.GetURLStructValue();
    NS_ASSERTION(url, "What's going on here?");

    if (NS_LIKELY(url->mURI)) {
      display->mBinding = url;
    } else {
      display->mBinding = nsnull;
    }
  }
  else if (eCSSUnit_None == displayData.mBinding.GetUnit() ||
           eCSSUnit_Initial == displayData.mBinding.GetUnit()) {
    display->mBinding = nsnull;
  }
  else if (eCSSUnit_Inherit == displayData.mBinding.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    display->mBinding = parentDisplay->mBinding;
  }

  // position: enum, inherit, initial
  SetDiscrete(displayData.mPosition, display->mPosition, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentDisplay->mPosition,
              NS_STYLE_POSITION_STATIC, 0, 0, 0, 0);

  // clear: enum, inherit, initial
  SetDiscrete(displayData.mClear, display->mBreakType, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentDisplay->mBreakType,
              NS_STYLE_CLEAR_NONE, 0, 0, 0, 0);

  // temp fix for bug 24000
  // Map 'auto' and 'avoid' to PR_FALSE, and 'always', 'left', and
  // 'right' to PR_TRUE.
  // "A conforming user agent may interpret the values 'left' and
  // 'right' as 'always'." - CSS2.1, section 13.3.1
  if (eCSSUnit_Enumerated == displayData.mBreakBefore.GetUnit()) {
    display->mBreakBefore =
      (NS_STYLE_PAGE_BREAK_AVOID != displayData.mBreakBefore.GetIntValue() &&
       NS_STYLE_PAGE_BREAK_AUTO  != displayData.mBreakBefore.GetIntValue());
  }
  else if (eCSSUnit_Initial == displayData.mBreakBefore.GetUnit()) {
    display->mBreakBefore = PR_FALSE;
  }
  else if (eCSSUnit_Inherit == displayData.mBreakBefore.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    display->mBreakBefore = parentDisplay->mBreakBefore;
  }

  if (eCSSUnit_Enumerated == displayData.mBreakAfter.GetUnit()) {
    display->mBreakAfter =
      (NS_STYLE_PAGE_BREAK_AVOID != displayData.mBreakAfter.GetIntValue() &&
       NS_STYLE_PAGE_BREAK_AUTO  != displayData.mBreakAfter.GetIntValue());
  }
  else if (eCSSUnit_Initial == displayData.mBreakAfter.GetUnit()) {
    display->mBreakAfter = PR_FALSE;
  }
  else if (eCSSUnit_Inherit == displayData.mBreakAfter.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    display->mBreakAfter = parentDisplay->mBreakAfter;
  }
  // end temp fix

  // float: enum, inherit, initial
  SetDiscrete(displayData.mFloat, display->mFloats, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentDisplay->mFloats,
              NS_STYLE_FLOAT_NONE, 0, 0, 0, 0);

  // overflow-x: enum, inherit, initial
  SetDiscrete(displayData.mOverflowX, display->mOverflowX, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentDisplay->mOverflowX,
              NS_STYLE_OVERFLOW_VISIBLE, 0, 0, 0, 0);

  // overflow-y: enum, inherit, initial
  SetDiscrete(displayData.mOverflowY, display->mOverflowY, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentDisplay->mOverflowY,
              NS_STYLE_OVERFLOW_VISIBLE, 0, 0, 0, 0);

  // CSS3 overflow-x and overflow-y require some fixup as well in some
  // cases.  NS_STYLE_OVERFLOW_VISIBLE and NS_STYLE_OVERFLOW_CLIP are
  // meaningful only when used in both dimensions.
  if (display->mOverflowX != display->mOverflowY &&
      (display->mOverflowX == NS_STYLE_OVERFLOW_VISIBLE ||
       display->mOverflowX == NS_STYLE_OVERFLOW_CLIP ||
       display->mOverflowY == NS_STYLE_OVERFLOW_VISIBLE ||
       display->mOverflowY == NS_STYLE_OVERFLOW_CLIP)) {
    // We can't store in the rule tree since a more specific rule might
    // change these conditions.
    canStoreInRuleTree = PR_FALSE;

    // NS_STYLE_OVERFLOW_CLIP is a deprecated value, so if it's specified
    // in only one dimension, convert it to NS_STYLE_OVERFLOW_HIDDEN.
    if (display->mOverflowX == NS_STYLE_OVERFLOW_CLIP)
      display->mOverflowX = NS_STYLE_OVERFLOW_HIDDEN;
    if (display->mOverflowY == NS_STYLE_OVERFLOW_CLIP)
      display->mOverflowY = NS_STYLE_OVERFLOW_HIDDEN;

    // If 'visible' is specified but doesn't match the other dimension, it
    // turns into 'auto'.
    if (display->mOverflowX == NS_STYLE_OVERFLOW_VISIBLE)
      display->mOverflowX = NS_STYLE_OVERFLOW_AUTO;
    if (display->mOverflowY == NS_STYLE_OVERFLOW_VISIBLE)
      display->mOverflowY = NS_STYLE_OVERFLOW_AUTO;
  }

  SetDiscrete(displayData.mResize, display->mResize, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentDisplay->mResize,
              NS_STYLE_RESIZE_NONE, 0, 0, 0, 0);

  // clip property: length, auto, inherit
  switch (displayData.mClip.GetUnit()) {
  case eCSSUnit_Inherit:
    canStoreInRuleTree = PR_FALSE;
    display->mClipFlags = parentDisplay->mClipFlags;
    display->mClip = parentDisplay->mClip;
    break;

  case eCSSUnit_Initial:
  case eCSSUnit_Auto:
    display->mClipFlags = NS_STYLE_CLIP_AUTO;
    display->mClip.SetRect(0,0,0,0);
    break;

  case eCSSUnit_Null:
    break;

  case eCSSUnit_Rect: {
    const nsCSSRect& clipRect = displayData.mClip.GetRectValue();

    display->mClipFlags = NS_STYLE_CLIP_RECT;

    if (clipRect.mTop.GetUnit() == eCSSUnit_Auto) {
      display->mClip.y = 0;
      display->mClipFlags |= NS_STYLE_CLIP_TOP_AUTO;
    }
    else if (clipRect.mTop.IsLengthUnit()) {
      display->mClip.y = CalcLength(clipRect.mTop, aContext,
                                    mPresContext, canStoreInRuleTree);
    }

    if (clipRect.mBottom.GetUnit() == eCSSUnit_Auto) {
      // Setting to NS_MAXSIZE for the 'auto' case ensures that
      // the clip rect is nonempty. It is important that mClip be
      // nonempty if the actual clip rect could be nonempty.
      display->mClip.height = NS_MAXSIZE;
      display->mClipFlags |= NS_STYLE_CLIP_BOTTOM_AUTO;
    }
    else if (clipRect.mBottom.IsLengthUnit()) {
      display->mClip.height = CalcLength(clipRect.mBottom, aContext,
                                         mPresContext, canStoreInRuleTree) -
                              display->mClip.y;
    }

    if (clipRect.mLeft.GetUnit() == eCSSUnit_Auto) {
      display->mClip.x = 0;
      display->mClipFlags |= NS_STYLE_CLIP_LEFT_AUTO;
    }
    else if (clipRect.mLeft.IsLengthUnit()) {
      display->mClip.x = CalcLength(clipRect.mLeft, aContext,
                                    mPresContext, canStoreInRuleTree);
    }

    if (clipRect.mRight.GetUnit() == eCSSUnit_Auto) {
      // Setting to NS_MAXSIZE for the 'auto' case ensures that
      // the clip rect is nonempty. It is important that mClip be
      // nonempty if the actual clip rect could be nonempty.
      display->mClip.width = NS_MAXSIZE;
      display->mClipFlags |= NS_STYLE_CLIP_RIGHT_AUTO;
    }
    else if (clipRect.mRight.IsLengthUnit()) {
      display->mClip.width = CalcLength(clipRect.mRight, aContext,
                                        mPresContext, canStoreInRuleTree) -
                             display->mClip.x;
    }
    break;
  }

  default:
    NS_ABORT_IF_FALSE(false, "unrecognized clip unit");
  }

  if (display->mDisplay != NS_STYLE_DISPLAY_NONE) {
    // CSS2 9.7 specifies display type corrections dealing with 'float'
    // and 'position'.  Since generated content can't be floated or
    // positioned, we can deal with it here.

    if (nsCSSPseudoElements::firstLetter == aContext->GetPseudo()) {
      // a non-floating first-letter must be inline
      // XXX this fix can go away once bug 103189 is fixed correctly
      display->mDisplay = NS_STYLE_DISPLAY_INLINE;

      // We can't cache the data in the rule tree since if a more specific
      // rule has 'float: left' we'll end up with the wrong 'display'
      // property.
      canStoreInRuleTree = PR_FALSE;
    }

    if (display->IsAbsolutelyPositioned()) {
      // 1) if position is 'absolute' or 'fixed' then display must be
      // block-level and float must be 'none'

      // Backup original display value for calculation of a hypothetical
      // box (CSS2 10.6.4/10.6.5).
      // See nsHTMLReflowState::CalculateHypotheticalBox
      display->mOriginalDisplay = display->mDisplay;
      EnsureBlockDisplay(display->mDisplay);
      display->mFloats = NS_STYLE_FLOAT_NONE;

      // We can't cache the data in the rule tree since if a more specific
      // rule has 'position: static' we'll end up with problems with the
      // 'display' and 'float' properties.
      canStoreInRuleTree = PR_FALSE;
    } else if (display->mFloats != NS_STYLE_FLOAT_NONE) {
      // 2) if float is not none, and display is not none, then we must
      // set a block-level 'display' type per CSS2.1 section 9.7.

      EnsureBlockDisplay(display->mDisplay);

      // We can't cache the data in the rule tree since if a more specific
      // rule has 'float: none' we'll end up with the wrong 'display'
      // property.
      canStoreInRuleTree = PR_FALSE;
    }

  }

  /* Convert the nsCSSValueList into an nsTArray<nsTransformFunction *>. */
  switch (displayData.mTransform.GetUnit()) {
  case eCSSUnit_Null:
    break;

  case eCSSUnit_Initial:
  case eCSSUnit_None:
    display->mSpecifiedTransform = nsnull;
    break;

  case eCSSUnit_Inherit:
    display->mSpecifiedTransform = parentDisplay->mSpecifiedTransform;
    if (parentDisplay->mSpecifiedTransform)
      display->mTransform = parentDisplay->mTransform;
    canStoreInRuleTree = PR_FALSE;
    break;

  case eCSSUnit_List:
  case eCSSUnit_ListDep: {
    const nsCSSValueList* head = displayData.mTransform.GetListValue();
    // can get a _None in here from transform animation
    if (head->mValue.GetUnit() == eCSSUnit_None) {
      NS_ABORT_IF_FALSE(head->mNext == nsnull, "none must be alone");
      display->mSpecifiedTransform = nsnull;
    } else {
      display->mSpecifiedTransform = head; // weak pointer, owned by rule
      display->mTransform = nsStyleTransformMatrix::ReadTransforms(head,
                              aContext, mPresContext, canStoreInRuleTree);
    }
    break;
  }

  default:
    NS_ABORT_IF_FALSE(false, "unrecognized transform unit");
  }

  /* Convert -moz-transform-origin. */
  if (displayData.mTransformOrigin.GetUnit() != eCSSUnit_Null) {
#ifdef DEBUG
    PRBool result =
#endif
      SetPairCoords(displayData.mTransformOrigin,
                    display->mTransformOrigin[0],
                    display->mTransformOrigin[1],
                    parentDisplay->mTransformOrigin[0],
                    parentDisplay->mTransformOrigin[1],
                    SETCOORD_LPH | SETCOORD_INITIAL_HALF |
                    SETCOORD_BOX_POSITION,
                    aContext, mPresContext, canStoreInRuleTree);
    NS_ASSERTION(result, "Malformed -moz-transform-origin parse!");
  }

  COMPUTE_END_RESET(Display, display)
}

const void*
nsRuleNode::ComputeVisibilityData(void* aStartStruct,
                                  const nsRuleDataStruct& aData,
                                  nsStyleContext* aContext,
                                  nsRuleNode* aHighestNode,
                                  const RuleDetail aRuleDetail,
                                  const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_INHERITED(Visibility, (mPresContext),
                          visibility, parentVisibility,
                          Display, displayData)

  // direction: enum, inherit, initial
  SetDiscrete(displayData.mDirection, visibility->mDirection,
              canStoreInRuleTree,
              SETDSC_ENUMERATED, parentVisibility->mDirection,
              (GET_BIDI_OPTION_DIRECTION(mPresContext->GetBidi())
               == IBMBIDI_TEXTDIRECTION_RTL)
              ? NS_STYLE_DIRECTION_RTL : NS_STYLE_DIRECTION_LTR,
              0, 0, 0, 0);

  // visibility: enum, inherit, initial
  SetDiscrete(displayData.mVisibility, visibility->mVisible,
              canStoreInRuleTree,
              SETDSC_ENUMERATED, parentVisibility->mVisible,
              NS_STYLE_VISIBILITY_VISIBLE, 0, 0, 0, 0);

  // pointer-events: enum, inherit, initial
  SetDiscrete(displayData.mPointerEvents, visibility->mPointerEvents,
              canStoreInRuleTree,
              SETDSC_ENUMERATED, parentVisibility->mPointerEvents,
              NS_STYLE_POINTER_EVENTS_AUTO, 0, 0, 0, 0);

  // lang: string, inherit
  // this is not a real CSS property, it is a html attribute mapped to CSS struture
  if (eCSSUnit_Ident == displayData.mLang.GetUnit()) {
    if (!gLangService) {
      CallGetService(NS_LANGUAGEATOMSERVICE_CONTRACTID, &gLangService);
    }

    if (gLangService) {
      nsAutoString lang;
      displayData.mLang.GetStringValue(lang);

      nsContentUtils::ASCIIToLower(lang);
      visibility->mLanguage = do_GetAtom(lang);
    }
  }

  COMPUTE_END_INHERITED(Visibility, visibility)
}

const void*
nsRuleNode::ComputeColorData(void* aStartStruct,
                             const nsRuleDataStruct& aData,
                             nsStyleContext* aContext,
                             nsRuleNode* aHighestNode,
                             const RuleDetail aRuleDetail,
                             const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_INHERITED(Color, (mPresContext), color, parentColor,
                          Color, colorData)

  // color: color, string, inherit
  // Special case for currentColor.  According to CSS3, setting color to 'currentColor'
  // should behave as if it is inherited
  if (colorData.mColor.GetUnit() == eCSSUnit_EnumColor &&
      colorData.mColor.GetIntValue() == NS_COLOR_CURRENTCOLOR) {
    color->mColor = parentColor->mColor;
    canStoreInRuleTree = PR_FALSE;
  }
  else if (colorData.mColor.GetUnit() == eCSSUnit_Initial) {
    color->mColor = mPresContext->DefaultColor();
  }
  else {
    SetColor(colorData.mColor, parentColor->mColor, mPresContext, aContext,
             color->mColor, canStoreInRuleTree);
  }

  COMPUTE_END_INHERITED(Color, color)
}

// information about how to compute values for background-* properties
template <class SpecifiedValueItem>
struct InitialInheritLocationFor {
};

NS_SPECIALIZE_TEMPLATE
struct InitialInheritLocationFor<nsCSSValueList> {
  static nsCSSValue nsCSSValueList::* Location() {
    return &nsCSSValueList::mValue;
  }
};

NS_SPECIALIZE_TEMPLATE
struct InitialInheritLocationFor<nsCSSValuePairList> {
  static nsCSSValue nsCSSValuePairList::* Location() {
    return &nsCSSValuePairList::mXValue;
  }
};

template <class SpecifiedValueItem, class ComputedValueItem>
struct BackgroundItemComputer {
};

NS_SPECIALIZE_TEMPLATE
struct BackgroundItemComputer<nsCSSValueList, PRUint8>
{
  static void ComputeValue(nsStyleContext* aStyleContext,
                           const nsCSSValueList* aSpecifiedValue,
                           PRUint8& aComputedValue,
                           PRBool& aCanStoreInRuleTree)
  {
    SetDiscrete(aSpecifiedValue->mValue, aComputedValue, aCanStoreInRuleTree,
                SETDSC_ENUMERATED, PRUint8(0), 0, 0, 0, 0, 0);
  }
};

NS_SPECIALIZE_TEMPLATE
struct BackgroundItemComputer<nsCSSValueList, nsStyleImage>
{
  static void ComputeValue(nsStyleContext* aStyleContext,
                           const nsCSSValueList* aSpecifiedValue,
                           nsStyleImage& aComputedValue,
                           PRBool& aCanStoreInRuleTree)
  {
    SetStyleImage(aStyleContext, aSpecifiedValue->mValue, aComputedValue,
                  aCanStoreInRuleTree);
  }
};

struct BackgroundPositionAxis {
  nsCSSValue nsCSSValuePairList::*specified;
  nsStyleBackground::Position::PositionCoord
    nsStyleBackground::Position::*result;
  PRPackedBool nsStyleBackground::Position::*isPercent;
};

static const BackgroundPositionAxis gBGPosAxes[] = {
  { &nsCSSValuePairList::mXValue,
    &nsStyleBackground::Position::mXPosition,
    &nsStyleBackground::Position::mXIsPercent },
  { &nsCSSValuePairList::mYValue,
    &nsStyleBackground::Position::mYPosition,
    &nsStyleBackground::Position::mYIsPercent }
};

NS_SPECIALIZE_TEMPLATE
struct BackgroundItemComputer<nsCSSValuePairList, nsStyleBackground::Position>
{
  static void ComputeValue(nsStyleContext* aStyleContext,
                           const nsCSSValuePairList* aSpecifiedValue,
                           nsStyleBackground::Position& aComputedValue,
                           PRBool& aCanStoreInRuleTree)
  {
    nsStyleBackground::Position &position = aComputedValue;
    for (const BackgroundPositionAxis *axis = gBGPosAxes,
                        *axis_end = gBGPosAxes + NS_ARRAY_LENGTH(gBGPosAxes);
         axis != axis_end; ++axis) {
      const nsCSSValue &specified = aSpecifiedValue->*(axis->specified);
      if (eCSSUnit_Percent == specified.GetUnit()) {
        (position.*(axis->result)).mFloat = specified.GetPercentValue();
        position.*(axis->isPercent) = PR_TRUE;
      }
      else if (specified.IsLengthUnit()) {
        (position.*(axis->result)).mCoord =
          CalcLength(specified, aStyleContext, aStyleContext->PresContext(),
                     aCanStoreInRuleTree);
        position.*(axis->isPercent) = PR_FALSE;
      }
      else if (eCSSUnit_Enumerated == specified.GetUnit()) {
        (position.*(axis->result)).mFloat =
          GetFloatFromBoxPosition(specified.GetIntValue());
        position.*(axis->isPercent) = PR_TRUE;
      } else {
        NS_NOTREACHED("unexpected unit");
      }
    }
  }
};


struct BackgroundSizeAxis {
  nsCSSValue nsCSSValuePairList::* specified;
  nsStyleBackground::Size::Dimension nsStyleBackground::Size::* result;
  PRUint8 nsStyleBackground::Size::* type;
};

static const BackgroundSizeAxis gBGSizeAxes[] = {
  { &nsCSSValuePairList::mXValue,
    &nsStyleBackground::Size::mWidth,
    &nsStyleBackground::Size::mWidthType },
  { &nsCSSValuePairList::mYValue,
    &nsStyleBackground::Size::mHeight,
    &nsStyleBackground::Size::mHeightType }
};

NS_SPECIALIZE_TEMPLATE
struct BackgroundItemComputer<nsCSSValuePairList, nsStyleBackground::Size>
{
  static void ComputeValue(nsStyleContext* aStyleContext,
                           const nsCSSValuePairList* aSpecifiedValue,
                           nsStyleBackground::Size& aComputedValue,
                           PRBool& aCanStoreInRuleTree)
  {
    nsStyleBackground::Size &size = aComputedValue;
    for (const BackgroundSizeAxis *axis = gBGSizeAxes,
                        *axis_end = gBGSizeAxes + NS_ARRAY_LENGTH(gBGSizeAxes);
         axis != axis_end; ++axis) {
      const nsCSSValue &specified = aSpecifiedValue->*(axis->specified);
      if (eCSSUnit_Auto == specified.GetUnit()) {
        size.*(axis->type) = nsStyleBackground::Size::eAuto;
      }
      else if (eCSSUnit_Enumerated == specified.GetUnit()) {
        PR_STATIC_ASSERT(nsStyleBackground::Size::eContain ==
                         NS_STYLE_BG_SIZE_CONTAIN);
        PR_STATIC_ASSERT(nsStyleBackground::Size::eCover ==
                         NS_STYLE_BG_SIZE_COVER);
        NS_ABORT_IF_FALSE(specified.GetIntValue() == NS_STYLE_BG_SIZE_CONTAIN ||
                          specified.GetIntValue() == NS_STYLE_BG_SIZE_COVER,
                          "invalid enumerated value for size coordinate");
        size.*(axis->type) = specified.GetIntValue();
      }
      else if (eCSSUnit_Null == specified.GetUnit()) {
        NS_ABORT_IF_FALSE(axis == gBGSizeAxes + 1,
                          "null allowed only as height value, and only "
                          "for contain/cover/initial/inherit");
#ifdef DEBUG
        {
          const nsCSSValue &widthValue = aSpecifiedValue->mXValue;
          NS_ABORT_IF_FALSE(widthValue.GetUnit() != eCSSUnit_Inherit &&
                            widthValue.GetUnit() != eCSSUnit_Initial,
                            "initial/inherit should already have been handled");
          NS_ABORT_IF_FALSE(widthValue.GetUnit() == eCSSUnit_Enumerated &&
                            (widthValue.GetIntValue() == NS_STYLE_BG_SIZE_CONTAIN ||
                             widthValue.GetIntValue() == NS_STYLE_BG_SIZE_COVER),
                            "null height value not corresponding to allowable "
                            "non-null width value");
        }
#endif
        size.*(axis->type) = size.mWidthType;
      }
      else if (eCSSUnit_Percent == specified.GetUnit()) {
        (size.*(axis->result)).mFloat = specified.GetPercentValue();
        size.*(axis->type) = nsStyleBackground::Size::ePercentage;
      }
      else {
        NS_ABORT_IF_FALSE(specified.IsLengthUnit(), "unexpected unit");
        (size.*(axis->result)).mCoord =
          CalcLength(specified, aStyleContext, aStyleContext->PresContext(),
                     aCanStoreInRuleTree);
        size.*(axis->type) = nsStyleBackground::Size::eLength;
      }
    }

    NS_ABORT_IF_FALSE(size.mWidthType < nsStyleBackground::Size::eDimensionType_COUNT,
                      "bad width type");
    NS_ABORT_IF_FALSE(size.mHeightType < nsStyleBackground::Size::eDimensionType_COUNT,
                      "bad height type");
    NS_ABORT_IF_FALSE((size.mWidthType != nsStyleBackground::Size::eContain &&
                       size.mWidthType != nsStyleBackground::Size::eCover) ||
                      size.mWidthType == size.mHeightType,
                      "contain/cover apply to both dimensions or to neither");
  }
};

template <class ComputedValueItem>
static void
SetBackgroundList(nsStyleContext* aStyleContext,
                  const nsCSSValue& aValue,
                  nsAutoTArray< nsStyleBackground::Layer, 1> &aLayers,
                  const nsAutoTArray<nsStyleBackground::Layer, 1> &aParentLayers,
                  ComputedValueItem nsStyleBackground::Layer::* aResultLocation,
                  ComputedValueItem aInitialValue,
                  PRUint32 aParentItemCount,
                  PRUint32& aItemCount,
                  PRUint32& aMaxItemCount,
                  PRBool& aRebuild,
                  PRBool& aCanStoreInRuleTree)
{
  switch (aValue.GetUnit()) {
  case eCSSUnit_Null:
    break;

  case eCSSUnit_Inherit:
    aRebuild = PR_TRUE;
    aCanStoreInRuleTree = PR_FALSE;
    if (!aLayers.EnsureLengthAtLeast(aParentItemCount)) {
      NS_WARNING("out of memory");
      aParentItemCount = aLayers.Length();
    }
    aItemCount = aParentItemCount;
    for (PRUint32 i = 0; i < aParentItemCount; ++i) {
      aLayers[i].*aResultLocation = aParentLayers[i].*aResultLocation;
    }
    break;

  case eCSSUnit_Initial:
    aRebuild = PR_TRUE;
    aItemCount = 1;
    aLayers[0].*aResultLocation = aInitialValue;
    break;

  case eCSSUnit_List:
  case eCSSUnit_ListDep: {
    aRebuild = PR_TRUE;
    aItemCount = 0;
    const nsCSSValueList* item = aValue.GetListValue();
    do {
      NS_ASSERTION(item->mValue.GetUnit() != eCSSUnit_Null &&
                   item->mValue.GetUnit() != eCSSUnit_Inherit &&
                   item->mValue.GetUnit() != eCSSUnit_Initial,
                   "unexpected unit");
      ++aItemCount;
      if (!aLayers.EnsureLengthAtLeast(aItemCount)) {
        NS_WARNING("out of memory");
        --aItemCount;
        break;
      }
      BackgroundItemComputer<nsCSSValueList, ComputedValueItem>
        ::ComputeValue(aStyleContext, item,
                       aLayers[aItemCount-1].*aResultLocation,
                       aCanStoreInRuleTree);
      item = item->mNext;
    } while (item);
    break;
  }

  default:
    NS_ABORT_IF_FALSE(false,
                      nsPrintfCString(32, "unexpected unit %d",
                                      aValue.GetUnit()).get());
  }

  if (aItemCount > aMaxItemCount)
    aMaxItemCount = aItemCount;
}

template <class ComputedValueItem>
static void
SetBackgroundPairList(nsStyleContext* aStyleContext,
                      const nsCSSValue& aValue,
                      nsAutoTArray< nsStyleBackground::Layer, 1> &aLayers,
                      const nsAutoTArray<nsStyleBackground::Layer, 1>
                                                                 &aParentLayers,
                      ComputedValueItem nsStyleBackground::Layer::*
                                                                aResultLocation,
                      ComputedValueItem aInitialValue,
                      PRUint32 aParentItemCount,
                      PRUint32& aItemCount,
                      PRUint32& aMaxItemCount,
                      PRBool& aRebuild,
                      PRBool& aCanStoreInRuleTree)
{
  switch (aValue.GetUnit()) {
  case eCSSUnit_Null:
    break;

  case eCSSUnit_Inherit:
    aRebuild = PR_TRUE;
    aCanStoreInRuleTree = PR_FALSE;
    if (!aLayers.EnsureLengthAtLeast(aParentItemCount)) {
      NS_WARNING("out of memory");
      aParentItemCount = aLayers.Length();
    }
    aItemCount = aParentItemCount;
    for (PRUint32 i = 0; i < aParentItemCount; ++i) {
      aLayers[i].*aResultLocation = aParentLayers[i].*aResultLocation;
    }
    break;

  case eCSSUnit_Initial:
    aRebuild = PR_TRUE;
    aItemCount = 1;
    aLayers[0].*aResultLocation = aInitialValue;
    break;

  case eCSSUnit_PairList:
  case eCSSUnit_PairListDep: {
    aRebuild = PR_TRUE;
    aItemCount = 0;
    const nsCSSValuePairList* item = aValue.GetPairListValue();
    do {
      NS_ASSERTION(item->mXValue.GetUnit() != eCSSUnit_Inherit &&
                   item->mXValue.GetUnit() != eCSSUnit_Initial &&
                   item->mYValue.GetUnit() != eCSSUnit_Inherit &&
                   item->mYValue.GetUnit() != eCSSUnit_Initial,
                   "unexpected unit");
      ++aItemCount;
      if (!aLayers.EnsureLengthAtLeast(aItemCount)) {
        NS_WARNING("out of memory");
        --aItemCount;
        break;
      }
      BackgroundItemComputer<nsCSSValuePairList, ComputedValueItem>
        ::ComputeValue(aStyleContext, item,
                       aLayers[aItemCount-1].*aResultLocation,
                       aCanStoreInRuleTree);
      item = item->mNext;
    } while (item);
    break;
  }

  default:
    NS_ABORT_IF_FALSE(false,
                      nsPrintfCString(32, "unexpected unit %d",
                                      aValue.GetUnit()).get());
  }

  if (aItemCount > aMaxItemCount)
    aMaxItemCount = aItemCount;
}

template <class ComputedValueItem>
static void
FillBackgroundList(nsAutoTArray< nsStyleBackground::Layer, 1> &aLayers,
    ComputedValueItem nsStyleBackground::Layer::* aResultLocation,
    PRUint32 aItemCount, PRUint32 aFillCount)
{
  NS_PRECONDITION(aFillCount <= aLayers.Length(), "unexpected array length");
  for (PRUint32 sourceLayer = 0, destLayer = aItemCount;
       destLayer < aFillCount;
       ++sourceLayer, ++destLayer) {
    aLayers[destLayer].*aResultLocation =
      aLayers[sourceLayer].*aResultLocation;
  }
}

const void*
nsRuleNode::ComputeBackgroundData(void* aStartStruct,
                                  const nsRuleDataStruct& aData,
                                  nsStyleContext* aContext,
                                  nsRuleNode* aHighestNode,
                                  const RuleDetail aRuleDetail,
                                  const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_RESET(Background, (), bg, parentBG, Color, colorData)

  // background-color: color, string, inherit
  if (eCSSUnit_Initial == colorData.mBackColor.GetUnit()) {
    bg->mBackgroundColor = NS_RGBA(0, 0, 0, 0);
  } else if (!SetColor(colorData.mBackColor, parentBG->mBackgroundColor,
                       mPresContext, aContext, bg->mBackgroundColor,
                       canStoreInRuleTree)) {
    NS_ASSERTION(eCSSUnit_Null == colorData.mBackColor.GetUnit(),
                 "unexpected color unit");
  }

  PRUint32 maxItemCount = 1;
  PRBool rebuild = PR_FALSE;

  // background-image: url (stored as image), none, inherit [list]
  nsStyleImage initialImage;
  SetBackgroundList(aContext, colorData.mBackImage, bg->mLayers,
                    parentBG->mLayers, &nsStyleBackground::Layer::mImage,
                    initialImage, parentBG->mImageCount, bg->mImageCount,
                    maxItemCount, rebuild, canStoreInRuleTree);

  // background-repeat: enum, inherit, initial [list]
  SetBackgroundList(aContext, colorData.mBackRepeat, bg->mLayers,
                    parentBG->mLayers, &nsStyleBackground::Layer::mRepeat,
                    PRUint8(NS_STYLE_BG_REPEAT_XY), parentBG->mRepeatCount,
                    bg->mRepeatCount, maxItemCount, rebuild, canStoreInRuleTree);

  // background-attachment: enum, inherit, initial [list]
  SetBackgroundList(aContext, colorData.mBackAttachment, bg->mLayers,
                    parentBG->mLayers,
                    &nsStyleBackground::Layer::mAttachment,
                    PRUint8(NS_STYLE_BG_ATTACHMENT_SCROLL),
                    parentBG->mAttachmentCount,
                    bg->mAttachmentCount, maxItemCount, rebuild,
                    canStoreInRuleTree);

  // background-clip: enum, inherit, initial [list]
  SetBackgroundList(aContext, colorData.mBackClip, bg->mLayers,
                    parentBG->mLayers, &nsStyleBackground::Layer::mClip,
                    PRUint8(NS_STYLE_BG_CLIP_BORDER), parentBG->mClipCount,
                    bg->mClipCount, maxItemCount, rebuild, canStoreInRuleTree);

  // background-inline-policy: enum, inherit, initial
  SetDiscrete(colorData.mBackInlinePolicy, bg->mBackgroundInlinePolicy,
              canStoreInRuleTree, SETDSC_ENUMERATED,
              parentBG->mBackgroundInlinePolicy,
              NS_STYLE_BG_INLINE_POLICY_CONTINUOUS, 0, 0, 0, 0);

  // background-origin: enum, inherit, initial [list]
  SetBackgroundList(aContext, colorData.mBackOrigin, bg->mLayers,
                    parentBG->mLayers, &nsStyleBackground::Layer::mOrigin,
                    PRUint8(NS_STYLE_BG_ORIGIN_PADDING), parentBG->mOriginCount,
                    bg->mOriginCount, maxItemCount, rebuild,
                    canStoreInRuleTree);

  // background-position: enum, length, percent (flags), inherit [pair list]
  nsStyleBackground::Position initialPosition;
  initialPosition.SetInitialValues();
  SetBackgroundPairList(aContext, colorData.mBackPosition, bg->mLayers,
                        parentBG->mLayers, &nsStyleBackground::Layer::mPosition,
                        initialPosition, parentBG->mPositionCount,
                        bg->mPositionCount, maxItemCount, rebuild,
                        canStoreInRuleTree);

  // background-size: enum, length, auto, inherit, initial [pair list]
  nsStyleBackground::Size initialSize;
  initialSize.SetInitialValues();
  SetBackgroundPairList(aContext, colorData.mBackSize, bg->mLayers,
                        parentBG->mLayers, &nsStyleBackground::Layer::mSize,
                        initialSize, parentBG->mSizeCount,
                        bg->mSizeCount, maxItemCount, rebuild,
                        canStoreInRuleTree);

  if (rebuild) {
    // Delete any extra items.  We need to keep layers in which any
    // property was specified.
    bg->mLayers.TruncateLength(maxItemCount);

    PRUint32 fillCount = bg->mImageCount;
    FillBackgroundList(bg->mLayers, &nsStyleBackground::Layer::mImage,
                       bg->mImageCount, fillCount);
    FillBackgroundList(bg->mLayers, &nsStyleBackground::Layer::mRepeat,
                       bg->mRepeatCount, fillCount);
    FillBackgroundList(bg->mLayers, &nsStyleBackground::Layer::mAttachment,
                       bg->mAttachmentCount, fillCount);
    FillBackgroundList(bg->mLayers, &nsStyleBackground::Layer::mClip,
                       bg->mClipCount, fillCount);
    FillBackgroundList(bg->mLayers, &nsStyleBackground::Layer::mOrigin,
                       bg->mOriginCount, fillCount);
    FillBackgroundList(bg->mLayers, &nsStyleBackground::Layer::mPosition,
                       bg->mPositionCount, fillCount);
    FillBackgroundList(bg->mLayers, &nsStyleBackground::Layer::mSize,
                       bg->mSizeCount, fillCount);
  }

  // Now that the dust has settled, register the images with the document
  for (PRUint32 i = 0; i < bg->mImageCount; ++i)
    bg->mLayers[i].TrackImages(aContext->PresContext());

  COMPUTE_END_RESET(Background, bg)
}

const void*
nsRuleNode::ComputeMarginData(void* aStartStruct,
                              const nsRuleDataStruct& aData,
                              nsStyleContext* aContext,
                              nsRuleNode* aHighestNode,
                              const RuleDetail aRuleDetail,
                              const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_RESET(Margin, (), margin, parentMargin, Margin, marginData)

  // margin: length, percent, auto, inherit
  nsStyleCoord  coord;
  nsCSSRect ourMargin(marginData.mMargin);
  AdjustLogicalBoxProp(aContext,
                       marginData.mMarginLeftLTRSource,
                       marginData.mMarginLeftRTLSource,
                       marginData.mMarginStart, marginData.mMarginEnd,
                       NS_SIDE_LEFT, ourMargin, canStoreInRuleTree);
  AdjustLogicalBoxProp(aContext,
                       marginData.mMarginRightLTRSource,
                       marginData.mMarginRightRTLSource,
                       marginData.mMarginEnd, marginData.mMarginStart,
                       NS_SIDE_RIGHT, ourMargin, canStoreInRuleTree);
  NS_FOR_CSS_SIDES(side) {
    nsStyleCoord parentCoord = parentMargin->mMargin.Get(side);
    if (SetCoord(ourMargin.*(nsCSSRect::sides[side]),
                 coord, parentCoord, SETCOORD_LPAH | SETCOORD_INITIAL_ZERO,
                 aContext, mPresContext, canStoreInRuleTree)) {
      margin->mMargin.Set(side, coord);
    }
  }

  margin->RecalcData();
  COMPUTE_END_RESET(Margin, margin)
}

const void*
nsRuleNode::ComputeBorderData(void* aStartStruct,
                              const nsRuleDataStruct& aData,
                              nsStyleContext* aContext,
                              nsRuleNode* aHighestNode,
                              const RuleDetail aRuleDetail,
                              const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_RESET(Border, (mPresContext), border, parentBorder,
                      Margin, marginData)

  // -moz-box-shadow: none, list, inherit, initial
  switch (marginData.mBoxShadow.GetUnit()) {
  case eCSSUnit_Null:
    break;

  case eCSSUnit_Initial:
  case eCSSUnit_None:
    border->mBoxShadow = nsnull;
    break;

  case eCSSUnit_Inherit:
    border->mBoxShadow = parentBorder->mBoxShadow;
    canStoreInRuleTree = PR_FALSE;
    break;

  case eCSSUnit_List:
  case eCSSUnit_ListDep:
    border->mBoxShadow = GetShadowData(marginData.mBoxShadow.GetListValue(),
                                       aContext, PR_TRUE, canStoreInRuleTree);
    break;

  default:
    NS_ABORT_IF_FALSE(false,
                      nsPrintfCString(64, "unrecognized shadow unit %d",
                                      marginData.mBoxShadow.GetUnit()).get());
  }

  // border-width, border-*-width: length, enum, inherit
  nsStyleCoord  coord;
  nsCSSRect ourBorderWidth(marginData.mBorderWidth);
  AdjustLogicalBoxProp(aContext,
                       marginData.mBorderLeftWidthLTRSource,
                       marginData.mBorderLeftWidthRTLSource,
                       marginData.mBorderStartWidth,
                       marginData.mBorderEndWidth,
                       NS_SIDE_LEFT, ourBorderWidth, canStoreInRuleTree);
  AdjustLogicalBoxProp(aContext,
                       marginData.mBorderRightWidthLTRSource,
                       marginData.mBorderRightWidthRTLSource,
                       marginData.mBorderEndWidth,
                       marginData.mBorderStartWidth,
                       NS_SIDE_RIGHT, ourBorderWidth, canStoreInRuleTree);
  { // scope for compilers with broken |for| loop scoping
    NS_FOR_CSS_SIDES(side) {
      const nsCSSValue &value = ourBorderWidth.*(nsCSSRect::sides[side]);
      NS_ASSERTION(eCSSUnit_Percent != value.GetUnit(),
                   "Percentage borders not implemented yet "
                   "If implementing, make sure to fix all consumers of "
                   "nsStyleBorder, the IsPercentageAwareChild method, "
                   "the nsAbsoluteContainingBlock::FrameDependsOnContainer "
                   "method, the "
                   "nsLineLayout::IsPercentageAwareReplacedElement method "
                   "and probably some other places");
      if (eCSSUnit_Enumerated == value.GetUnit()) {
        NS_ASSERTION(value.GetIntValue() == NS_STYLE_BORDER_WIDTH_THIN ||
                     value.GetIntValue() == NS_STYLE_BORDER_WIDTH_MEDIUM ||
                     value.GetIntValue() == NS_STYLE_BORDER_WIDTH_THICK,
                     "Unexpected enum value");
        border->SetBorderWidth(side,
                               (mPresContext->GetBorderWidthTable())[value.GetIntValue()]);
      }
      // OK to pass bad aParentCoord since we're not passing SETCOORD_INHERIT
      else if (SetCoord(value, coord, nsStyleCoord(), SETCOORD_LENGTH,
                        aContext, mPresContext, canStoreInRuleTree)) {
        NS_ASSERTION(coord.GetUnit() == eStyleUnit_Coord, "unexpected unit");
        border->SetBorderWidth(side, coord.GetCoordValue());
      }
      else if (eCSSUnit_Inherit == value.GetUnit()) {
        canStoreInRuleTree = PR_FALSE;
        border->SetBorderWidth(side,
                               parentBorder->GetComputedBorder().side(side));
      }
      else if (eCSSUnit_Initial == value.GetUnit()) {
        border->SetBorderWidth(side,
          (mPresContext->GetBorderWidthTable())[NS_STYLE_BORDER_WIDTH_MEDIUM]);
      }
      else {
        NS_ASSERTION(eCSSUnit_Null == value.GetUnit(),
                     "missing case handling border width");
      }
    }
  }

  // border-style, border-*-style: enum, inherit
  nsCSSRect ourStyle(marginData.mBorderStyle);
  AdjustLogicalBoxProp(aContext,
                       marginData.mBorderLeftStyleLTRSource,
                       marginData.mBorderLeftStyleRTLSource,
                       marginData.mBorderStartStyle, marginData.mBorderEndStyle,
                       NS_SIDE_LEFT, ourStyle, canStoreInRuleTree);
  AdjustLogicalBoxProp(aContext,
                       marginData.mBorderRightStyleLTRSource,
                       marginData.mBorderRightStyleRTLSource,
                       marginData.mBorderEndStyle, marginData.mBorderStartStyle,
                       NS_SIDE_RIGHT, ourStyle, canStoreInRuleTree);
  { // scope for compilers with broken |for| loop scoping
    NS_FOR_CSS_SIDES(side) {
      const nsCSSValue &value = ourStyle.*(nsCSSRect::sides[side]);
      nsCSSUnit unit = value.GetUnit();
      NS_ABORT_IF_FALSE(eCSSUnit_None != unit,
                        "'none' should be handled as enumerated value");
      if (eCSSUnit_Enumerated == unit) {
        border->SetBorderStyle(side, value.GetIntValue());
      }
      else if (eCSSUnit_Initial == unit) {
        border->SetBorderStyle(side, NS_STYLE_BORDER_STYLE_NONE);
      }
      else if (eCSSUnit_Inherit == unit) {
        canStoreInRuleTree = PR_FALSE;
        border->SetBorderStyle(side, parentBorder->GetBorderStyle(side));
      }
    }
  }

  // -moz-border-*-colors: color, string, enum, none, inherit/initial
  nscolor borderColor;
  nscolor unused = NS_RGB(0,0,0);

  NS_FOR_CSS_SIDES(side) {
    const nsCSSValue& value =
      marginData.mBorderColors.*(nsCSSRect::sides[side]);
    switch (value.GetUnit()) {
    case eCSSUnit_Null:
      break;

    case eCSSUnit_Initial:
    case eCSSUnit_None:
      border->ClearBorderColors(side);
      break;

    case eCSSUnit_Inherit: {
      canStoreInRuleTree = PR_FALSE;
      nsBorderColors *parentColors;
      parentBorder->GetCompositeColors(side, &parentColors);
      if (parentColors) {
        border->EnsureBorderColors();
        border->ClearBorderColors(side);
        border->mBorderColors[side] = parentColors->Clone();
      } else {
        border->ClearBorderColors(side);
      }
      break;
    }

    case eCSSUnit_List:
    case eCSSUnit_ListDep: {
      // Some composite border color information has been specified for this
      // border side.
      border->EnsureBorderColors();
      border->ClearBorderColors(side);
      const nsCSSValueList* list = value.GetListValue();
      while (list) {
        if (SetColor(list->mValue, unused, mPresContext,
                     aContext, borderColor, canStoreInRuleTree))
          border->AppendBorderColor(side, borderColor);
        else {
          NS_NOTREACHED("unexpected item in -moz-border-*-colors list");
        }
        list = list->mNext;
      }
      break;
    }

    default:
      NS_ABORT_IF_FALSE(false, "unrecognized border color unit");
    }
  }

  // border-color, border-*-color: color, string, enum, inherit
  nsCSSRect ourBorderColor(marginData.mBorderColor);
  PRBool foreground;
  AdjustLogicalBoxProp(aContext,
                       marginData.mBorderLeftColorLTRSource,
                       marginData.mBorderLeftColorRTLSource,
                       marginData.mBorderStartColor, marginData.mBorderEndColor,
                       NS_SIDE_LEFT, ourBorderColor, canStoreInRuleTree);
  AdjustLogicalBoxProp(aContext,
                       marginData.mBorderRightColorLTRSource,
                       marginData.mBorderRightColorRTLSource,
                       marginData.mBorderEndColor, marginData.mBorderStartColor,
                       NS_SIDE_RIGHT, ourBorderColor, canStoreInRuleTree);
  { // scope for compilers with broken |for| loop scoping
    NS_FOR_CSS_SIDES(side) {
      const nsCSSValue &value = ourBorderColor.*(nsCSSRect::sides[side]);
      if (eCSSUnit_Inherit == value.GetUnit()) {
        canStoreInRuleTree = PR_FALSE;
        if (parentContext) {
          parentBorder->GetBorderColor(side, borderColor, foreground);
          if (foreground) {
            // We want to inherit the color from the parent, not use the
            // color on the element where this chunk of style data will be
            // used.  We can ensure that the data for the parent are fully
            // computed (unlike for the element where this will be used, for
            // which the color could be specified on a more specific rule).
            border->SetBorderColor(side, parentContext->GetStyleColor()->mColor);
          } else
            border->SetBorderColor(side, borderColor);
        } else {
          // We're the root
          border->SetBorderToForeground(side);
        }
      }
      else if (SetColor(value, unused, mPresContext, aContext, borderColor,
                        canStoreInRuleTree)) {
        border->SetBorderColor(side, borderColor);
      }
      else if (eCSSUnit_Enumerated == value.GetUnit()) {
        switch (value.GetIntValue()) {
          case NS_STYLE_COLOR_MOZ_USE_TEXT_COLOR:
            border->SetBorderToForeground(side);
            break;
        }
      }
      else if (eCSSUnit_Initial == value.GetUnit()) {
        border->SetBorderToForeground(side);
      }
    }
  }

  // -moz-border-radius: length, percent, inherit
  {
    const nsCSSCornerSizes& borderRadius = marginData.mBorderRadius;
    NS_FOR_CSS_FULL_CORNERS(corner) {
      int cx = NS_FULL_TO_HALF_CORNER(corner, false);
      int cy = NS_FULL_TO_HALF_CORNER(corner, true);
      const nsCSSValue& radius = borderRadius.GetCorner(corner);
      nsStyleCoord parentX = parentBorder->mBorderRadius.Get(cx);
      nsStyleCoord parentY = parentBorder->mBorderRadius.Get(cy);
      nsStyleCoord coordX, coordY;

      if (SetPairCoords(radius, coordX, coordY, parentX, parentY,
                        SETCOORD_LPH | SETCOORD_INITIAL_ZERO,
                        aContext, mPresContext, canStoreInRuleTree)) {
        border->mBorderRadius.Set(cx, coordX);
        border->mBorderRadius.Set(cy, coordY);
      }
    }
  }

  // float-edge: enum, inherit, initial
  SetDiscrete(marginData.mFloatEdge, border->mFloatEdge, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentBorder->mFloatEdge,
              NS_STYLE_FLOAT_EDGE_CONTENT, 0, 0, 0, 0);

  // border-image
  if (eCSSUnit_Array == marginData.mBorderImage.GetUnit()) {
    nsCSSValue::Array *arr = marginData.mBorderImage.GetArrayValue();

    // the image
    if (eCSSUnit_Image == arr->Item(0).GetUnit()) {
      NS_SET_IMAGE_REQUEST(border->SetBorderImage,
                           aContext,
                           arr->Item(0).GetImageValue())
    }

    // the numbers saying where to split the image
    NS_FOR_CSS_SIDES(side) {
      if (SetAbsCoord(arr->Item(1 + side), coord,
                      SETCOORD_FACTOR | SETCOORD_PERCENT)) {
        border->mBorderImageSplit.Set(side, coord);
      }
    }

    // possible replacement for border-width
    // if have one - have all four (see CSSParserImpl::ParseBorderImage())
    if (eCSSUnit_Null != arr->Item(5).GetUnit()) {
      NS_FOR_CSS_SIDES(side) {
        // an uninitialized parentCoord is ok because I'm not passing SETCOORD_INHERIT
        if (!SetCoord(arr->Item(5 + side), coord, nsStyleCoord(),
                      SETCOORD_LENGTH, aContext, mPresContext,
                      canStoreInRuleTree)) {
          NS_NOTREACHED("SetCoord for border-width replacement from border-image failed");
        }
        if (coord.GetUnit() == eStyleUnit_Coord) {
          border->SetBorderImageWidthOverride(side, coord.GetCoordValue());
        } else {
          NS_WARNING("a border-width replacement from border-image "
                     "has a unit that's not eStyleUnit_Coord");
          border->SetBorderImageWidthOverride(side, 0);
        }
      }
      border->mHaveBorderImageWidth = PR_TRUE;
    } else {
      border->mHaveBorderImageWidth = PR_FALSE;
    }

    // stretch/round/repeat keywords
    if (eCSSUnit_Null == arr->Item(9).GetUnit()) {
      // default, both horizontal and vertical are stretch
      border->mBorderImageHFill = NS_STYLE_BORDER_IMAGE_STRETCH;
      border->mBorderImageVFill = NS_STYLE_BORDER_IMAGE_STRETCH;
    } else {
      // have horizontal value
      border->mBorderImageHFill = arr->Item(9).GetIntValue();
      if (eCSSUnit_Null == arr->Item(10).GetUnit()) {
        // vertical same as horizontal
        border->mBorderImageVFill = border->mBorderImageHFill;
      } else {
        // have vertical value
        border->mBorderImageVFill = arr->Item(10).GetIntValue();
      }
    }
  } else if (eCSSUnit_None == marginData.mBorderImage.GetUnit() ||
             eCSSUnit_Initial == marginData.mBorderImage.GetUnit()) {
    border->mHaveBorderImageWidth = PR_FALSE;
    border->SetBorderImage(nsnull);
  } else if (eCSSUnit_Inherit == marginData.mBorderImage.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    NS_FOR_CSS_SIDES(side) {
      border->SetBorderImageWidthOverride(side, parentBorder->mBorderImageWidth.side(side));
    }
    border->mBorderImageSplit = parentBorder->mBorderImageSplit;
    border->mBorderImageHFill = parentBorder->mBorderImageHFill;
    border->mBorderImageVFill = parentBorder->mBorderImageVFill;
    border->mHaveBorderImageWidth = parentBorder->mHaveBorderImageWidth;
    NS_SET_IMAGE_REQUEST(border->SetBorderImage, aContext,
                         parentBorder->GetBorderImage())
  }

  COMPUTE_END_RESET(Border, border)
}

const void*
nsRuleNode::ComputePaddingData(void* aStartStruct,
                               const nsRuleDataStruct& aData,
                               nsStyleContext* aContext,
                               nsRuleNode* aHighestNode,
                               const RuleDetail aRuleDetail,
                               const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_RESET(Padding, (), padding, parentPadding, Margin, marginData)

  // padding: length, percent, inherit
  nsStyleCoord  coord;
  nsCSSRect ourPadding(marginData.mPadding);
  AdjustLogicalBoxProp(aContext,
                       marginData.mPaddingLeftLTRSource,
                       marginData.mPaddingLeftRTLSource,
                       marginData.mPaddingStart, marginData.mPaddingEnd,
                       NS_SIDE_LEFT, ourPadding, canStoreInRuleTree);
  AdjustLogicalBoxProp(aContext,
                       marginData.mPaddingRightLTRSource,
                       marginData.mPaddingRightRTLSource,
                       marginData.mPaddingEnd, marginData.mPaddingStart,
                       NS_SIDE_RIGHT, ourPadding, canStoreInRuleTree);
  NS_FOR_CSS_SIDES(side) {
    nsStyleCoord parentCoord = parentPadding->mPadding.Get(side);
    if (SetCoord(ourPadding.*(nsCSSRect::sides[side]),
                 coord, parentCoord, SETCOORD_LPH | SETCOORD_INITIAL_ZERO,
                 aContext, mPresContext, canStoreInRuleTree)) {
      padding->mPadding.Set(side, coord);
    }
  }

  padding->RecalcData();
  COMPUTE_END_RESET(Padding, padding)
}

const void*
nsRuleNode::ComputeOutlineData(void* aStartStruct,
                               const nsRuleDataStruct& aData,
                               nsStyleContext* aContext,
                               nsRuleNode* aHighestNode,
                               const RuleDetail aRuleDetail,
                               const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_RESET(Outline, (mPresContext), outline, parentOutline,
                      Margin, marginData)

  // outline-width: length, enum, inherit
  if (eCSSUnit_Initial == marginData.mOutlineWidth.GetUnit()) {
    outline->mOutlineWidth =
      nsStyleCoord(NS_STYLE_BORDER_WIDTH_MEDIUM, eStyleUnit_Enumerated);
  }
  else {
    SetCoord(marginData.mOutlineWidth, outline->mOutlineWidth,
             parentOutline->mOutlineWidth, SETCOORD_LEH, aContext,
             mPresContext, canStoreInRuleTree);
  }

  // outline-offset: length, inherit
  nsStyleCoord tempCoord;
  if (SetCoord(marginData.mOutlineOffset, tempCoord,
               nsStyleCoord(parentOutline->mOutlineOffset,
                            nsStyleCoord::CoordConstructor),
               SETCOORD_LH | SETCOORD_INITIAL_ZERO | SETCOORD_CALC_LENGTH_ONLY,
               aContext, mPresContext, canStoreInRuleTree)) {
    outline->mOutlineOffset = tempCoord.GetCoordValue();
  } else {
    NS_ASSERTION(marginData.mOutlineOffset.GetUnit() == eCSSUnit_Null,
                 "unexpected unit");
  }

  // outline-color: color, string, enum, inherit
  nscolor outlineColor;
  nscolor unused = NS_RGB(0,0,0);
  if (eCSSUnit_Inherit == marginData.mOutlineColor.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    if (parentContext) {
      if (parentOutline->GetOutlineColor(outlineColor))
        outline->SetOutlineColor(outlineColor);
      else {
#ifdef GFX_HAS_INVERT
        outline->SetOutlineInitialColor();
#else
        // We want to inherit the color from the parent, not use the
        // color on the element where this chunk of style data will be
        // used.  We can ensure that the data for the parent are fully
        // computed (unlike for the element where this will be used, for
        // which the color could be specified on a more specific rule).
        outline->SetOutlineColor(parentContext->GetStyleColor()->mColor);
#endif
      }
    } else {
      outline->SetOutlineInitialColor();
    }
  }
  else if (SetColor(marginData.mOutlineColor, unused, mPresContext,
                    aContext, outlineColor, canStoreInRuleTree))
    outline->SetOutlineColor(outlineColor);
  else if (eCSSUnit_Enumerated == marginData.mOutlineColor.GetUnit() ||
           eCSSUnit_Initial == marginData.mOutlineColor.GetUnit()) {
    outline->SetOutlineInitialColor();
  }

  // -moz-outline-radius: length, percent, inherit
  {
    nsStyleCoord coord;
    const nsCSSCornerSizes& outlineRadius = marginData.mOutlineRadius;
    NS_FOR_CSS_FULL_CORNERS(corner) {
      int cx = NS_FULL_TO_HALF_CORNER(corner, false);
      int cy = NS_FULL_TO_HALF_CORNER(corner, true);
      const nsCSSValue& radius = outlineRadius.GetCorner(corner);
      nsStyleCoord parentX = parentOutline->mOutlineRadius.Get(cx);
      nsStyleCoord parentY = parentOutline->mOutlineRadius.Get(cy);
      nsStyleCoord coordX, coordY;

      if (SetPairCoords(radius, coordX, coordY, parentX, parentY,
                        SETCOORD_LPH | SETCOORD_INITIAL_ZERO,
                        aContext, mPresContext, canStoreInRuleTree)) {
        outline->mOutlineRadius.Set(cx, coordX);
        outline->mOutlineRadius.Set(cy, coordY);
      }
    }
  }

  // outline-style: enum, inherit, initial
  // cannot use SetDiscrete because of SetOutlineStyle
  nsCSSUnit unit = marginData.mOutlineStyle.GetUnit();
  NS_ABORT_IF_FALSE(eCSSUnit_None != unit && eCSSUnit_Auto != unit,
                    "'none' and 'auto' should be handled as enumerated values");
  if (eCSSUnit_Enumerated == unit) {
    outline->SetOutlineStyle(marginData.mOutlineStyle.GetIntValue());
  } else if (eCSSUnit_Initial == unit) {
    outline->SetOutlineStyle(NS_STYLE_BORDER_STYLE_NONE);
  } else if (eCSSUnit_Inherit == unit) {
    canStoreInRuleTree = PR_FALSE;
    outline->SetOutlineStyle(parentOutline->GetOutlineStyle());
  }

  outline->RecalcData(mPresContext);
  COMPUTE_END_RESET(Outline, outline)
}

const void*
nsRuleNode::ComputeListData(void* aStartStruct,
                            const nsRuleDataStruct& aData,
                            nsStyleContext* aContext,
                            nsRuleNode* aHighestNode,
                            const RuleDetail aRuleDetail,
                            const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_INHERITED(List, (), list, parentList, List, listData)

  // list-style-type: enum, inherit, initial
  SetDiscrete(listData.mType, list->mListStyleType, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentList->mListStyleType,
              NS_STYLE_LIST_STYLE_DISC, 0, 0, 0, 0);

  // list-style-image: url, none, inherit
  if (eCSSUnit_Image == listData.mImage.GetUnit()) {
    NS_SET_IMAGE_REQUEST(list->SetListStyleImage,
                         aContext,
                         listData.mImage.GetImageValue())
  }
  else if (eCSSUnit_None == listData.mImage.GetUnit() ||
           eCSSUnit_Initial == listData.mImage.GetUnit()) {
    list->SetListStyleImage(nsnull);
  }
  else if (eCSSUnit_Inherit == listData.mImage.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    NS_SET_IMAGE_REQUEST(list->SetListStyleImage,
                         aContext,
                         parentList->GetListStyleImage())
  }

  // list-style-position: enum, inherit, initial
  SetDiscrete(listData.mPosition, list->mListStylePosition, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentList->mListStylePosition,
              NS_STYLE_LIST_STYLE_POSITION_OUTSIDE, 0, 0, 0, 0);

  // image region property: length, auto, inherit
  switch (listData.mImageRegion.GetUnit()) {
  case eCSSUnit_Inherit:
    canStoreInRuleTree = PR_FALSE;
    list->mImageRegion = parentList->mImageRegion;
    break;

  case eCSSUnit_Initial:
  case eCSSUnit_Auto:
    list->mImageRegion.SetRect(0,0,0,0);
    break;

  case eCSSUnit_Null:
    break;

  case eCSSUnit_Rect: {
    const nsCSSRect& rgnRect = listData.mImageRegion.GetRectValue();

    if (rgnRect.mTop.GetUnit() == eCSSUnit_Auto)
      list->mImageRegion.y = 0;
    else if (rgnRect.mTop.IsLengthUnit())
      list->mImageRegion.y =
        CalcLength(rgnRect.mTop, aContext, mPresContext, canStoreInRuleTree);

    if (rgnRect.mBottom.GetUnit() == eCSSUnit_Auto)
      list->mImageRegion.height = 0;
    else if (rgnRect.mBottom.IsLengthUnit())
      list->mImageRegion.height =
        CalcLength(rgnRect.mBottom, aContext, mPresContext,
                   canStoreInRuleTree) - list->mImageRegion.y;

    if (rgnRect.mLeft.GetUnit() == eCSSUnit_Auto)
      list->mImageRegion.x = 0;
    else if (rgnRect.mLeft.IsLengthUnit())
      list->mImageRegion.x =
        CalcLength(rgnRect.mLeft, aContext, mPresContext, canStoreInRuleTree);

    if (rgnRect.mRight.GetUnit() == eCSSUnit_Auto)
      list->mImageRegion.width = 0;
    else if (rgnRect.mRight.IsLengthUnit())
      list->mImageRegion.width =
        CalcLength(rgnRect.mRight, aContext, mPresContext,
                   canStoreInRuleTree) - list->mImageRegion.x;
    break;
  }

  default:
    NS_ABORT_IF_FALSE(false, "unrecognized image-region unit");
  }

  COMPUTE_END_INHERITED(List, list)
}

const void*
nsRuleNode::ComputePositionData(void* aStartStruct,
                                const nsRuleDataStruct& aData,
                                nsStyleContext* aContext,
                                nsRuleNode* aHighestNode,
                                const RuleDetail aRuleDetail,
                                const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_RESET(Position, (), pos, parentPos, Position, posData)

  // box offsets: length, percent, calc, auto, inherit
  nsStyleCoord  coord;
  NS_FOR_CSS_SIDES(side) {
    nsStyleCoord parentCoord = parentPos->mOffset.Get(side);
    if (SetCoord(posData.mOffset.*(nsCSSRect::sides[side]),
                 coord, parentCoord,
                 SETCOORD_LPAH | SETCOORD_INITIAL_AUTO | SETCOORD_STORE_CALC,
                 aContext, mPresContext, canStoreInRuleTree)) {
      pos->mOffset.Set(side, coord);
    }
  }

  SetCoord(posData.mWidth, pos->mWidth, parentPos->mWidth,
           SETCOORD_LPAEH | SETCOORD_INITIAL_AUTO | SETCOORD_STORE_CALC,
           aContext, mPresContext, canStoreInRuleTree);
  SetCoord(posData.mMinWidth, pos->mMinWidth, parentPos->mMinWidth,
           SETCOORD_LPEH | SETCOORD_INITIAL_ZERO | SETCOORD_STORE_CALC,
           aContext, mPresContext, canStoreInRuleTree);
  SetCoord(posData.mMaxWidth, pos->mMaxWidth, parentPos->mMaxWidth,
           SETCOORD_LPOEH | SETCOORD_INITIAL_NONE | SETCOORD_STORE_CALC,
           aContext, mPresContext, canStoreInRuleTree);

  SetCoord(posData.mHeight, pos->mHeight, parentPos->mHeight,
           SETCOORD_LPAH | SETCOORD_INITIAL_AUTO | SETCOORD_STORE_CALC,
           aContext, mPresContext, canStoreInRuleTree);
  SetCoord(posData.mMinHeight, pos->mMinHeight, parentPos->mMinHeight,
           SETCOORD_LPH | SETCOORD_INITIAL_ZERO | SETCOORD_STORE_CALC,
           aContext, mPresContext, canStoreInRuleTree);
  SetCoord(posData.mMaxHeight, pos->mMaxHeight, parentPos->mMaxHeight,
           SETCOORD_LPOH | SETCOORD_INITIAL_NONE | SETCOORD_STORE_CALC,
           aContext, mPresContext, canStoreInRuleTree);

  // box-sizing: enum, inherit, initial
  SetDiscrete(posData.mBoxSizing, pos->mBoxSizing, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentPos->mBoxSizing,
              NS_STYLE_BOX_SIZING_CONTENT, 0, 0, 0, 0);

  // z-index
  if (! SetCoord(posData.mZIndex, pos->mZIndex, parentPos->mZIndex,
                 SETCOORD_IA | SETCOORD_INITIAL_AUTO, aContext,
                 nsnull, canStoreInRuleTree)) {
    if (eCSSUnit_Inherit == posData.mZIndex.GetUnit()) {
      // handle inherit, because it's ok to inherit 'auto' here
      canStoreInRuleTree = PR_FALSE;
      pos->mZIndex = parentPos->mZIndex;
    }
  }

  COMPUTE_END_RESET(Position, pos)
}

const void*
nsRuleNode::ComputeTableData(void* aStartStruct,
                             const nsRuleDataStruct& aData,
                             nsStyleContext* aContext,
                             nsRuleNode* aHighestNode,
                             const RuleDetail aRuleDetail,
                             const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_RESET(Table, (), table, parentTable, Table, tableData)

  // table-layout: enum, inherit, initial
  SetDiscrete(tableData.mLayout, table->mLayoutStrategy, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentTable->mLayoutStrategy,
              NS_STYLE_TABLE_LAYOUT_AUTO, 0, 0, 0, 0);

  // cols: enum, int (not a real CSS prop)
  if (eCSSUnit_Enumerated == tableData.mCols.GetUnit() ||
      eCSSUnit_Integer == tableData.mCols.GetUnit())
    table->mCols = tableData.mCols.GetIntValue();

  // span: pixels (not a real CSS prop)
  if (eCSSUnit_Enumerated == tableData.mSpan.GetUnit() ||
      eCSSUnit_Integer == tableData.mSpan.GetUnit())
    table->mSpan = tableData.mSpan.GetIntValue();

  COMPUTE_END_RESET(Table, table)
}

const void*
nsRuleNode::ComputeTableBorderData(void* aStartStruct,
                                   const nsRuleDataStruct& aData,
                                   nsStyleContext* aContext,
                                   nsRuleNode* aHighestNode,
                                   const RuleDetail aRuleDetail,
                                   const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_INHERITED(TableBorder, (mPresContext), table, parentTable,
                          Table, tableData)

  // border-collapse: enum, inherit, initial
  SetDiscrete(tableData.mBorderCollapse, table->mBorderCollapse,
              canStoreInRuleTree,
              SETDSC_ENUMERATED, parentTable->mBorderCollapse,
              NS_STYLE_BORDER_SEPARATE, 0, 0, 0, 0);

  if (tableData.mBorderSpacing.GetUnit() != eCSSUnit_Null) {
    // border-spacing-x/y: length, inherit
    nsStyleCoord parentX(parentTable->mBorderSpacingX,
                         nsStyleCoord::CoordConstructor);
    nsStyleCoord parentY(parentTable->mBorderSpacingY,
                         nsStyleCoord::CoordConstructor);
    nsStyleCoord coordX, coordY;

#ifdef DEBUG
    PRBool result =
#endif
      SetPairCoords(tableData.mBorderSpacing,
                    coordX, coordY, parentX, parentY,
                    SETCOORD_LH | SETCOORD_INITIAL_ZERO |
                    SETCOORD_CALC_LENGTH_ONLY |
                    SETCOORD_CALC_CLAMP_NONNEGATIVE,
                    aContext, mPresContext, canStoreInRuleTree);
    NS_ASSERTION(result, "malformed table border value");
    table->mBorderSpacingX = coordX.GetCoordValue();
    table->mBorderSpacingY = coordY.GetCoordValue();
  }

  // caption-side: enum, inherit, initial
  SetDiscrete(tableData.mCaptionSide, table->mCaptionSide, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentTable->mCaptionSide,
              NS_STYLE_CAPTION_SIDE_TOP, 0, 0, 0, 0);

  // empty-cells: enum, inherit, initial
  SetDiscrete(tableData.mEmptyCells, table->mEmptyCells, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentTable->mEmptyCells,
              (mPresContext->CompatibilityMode() == eCompatibility_NavQuirks)
              ? NS_STYLE_TABLE_EMPTY_CELLS_SHOW_BACKGROUND
              : NS_STYLE_TABLE_EMPTY_CELLS_SHOW,
              0, 0, 0, 0);

  COMPUTE_END_INHERITED(TableBorder, table)
}

const void*
nsRuleNode::ComputeContentData(void* aStartStruct,
                               const nsRuleDataStruct& aData,
                               nsStyleContext* aContext,
                               nsRuleNode* aHighestNode,
                               const RuleDetail aRuleDetail,
                               const PRBool aCanStoreInRuleTree)
{
  PRUint32 count;
  nsAutoString buffer;

  COMPUTE_START_RESET(Content, (), content, parentContent,
                      Content, contentData)

  // content: [string, url, counter, attr, enum]+, normal, none, inherit
  switch (contentData.mContent.GetUnit()) {
  case eCSSUnit_Null:
    break;

  case eCSSUnit_Normal:
  case eCSSUnit_None:
  case eCSSUnit_Initial:
    // "normal", "none", and "initial" all mean no content
    content->AllocateContents(0);
    break;

  case eCSSUnit_Inherit:
    canStoreInRuleTree = PR_FALSE;
    count = parentContent->ContentCount();
    if (NS_SUCCEEDED(content->AllocateContents(count))) {
      while (0 < count--) {
        content->ContentAt(count) = parentContent->ContentAt(count);
      }
    }
    break;

  case eCSSUnit_Enumerated: {
    NS_ABORT_IF_FALSE(contentData.mContent.GetIntValue() ==
                      NS_STYLE_CONTENT_ALT_CONTENT,
                      "unrecognized solitary content keyword");
    content->AllocateContents(1);
    nsStyleContentData& data = content->ContentAt(0);
    data.mType = eStyleContentType_AltContent;
    data.mContent.mString = nsnull;
    break;
  }

  case eCSSUnit_List:
  case eCSSUnit_ListDep: {
    const nsCSSValueList* contentValue = contentData.mContent.GetListValue();
      count = 0;
      while (contentValue) {
        count++;
        contentValue = contentValue->mNext;
      }
      if (NS_SUCCEEDED(content->AllocateContents(count))) {
        const nsAutoString  nullStr;
        count = 0;
        contentValue = contentData.mContent.GetListValue();
        while (contentValue) {
          const nsCSSValue& value = contentValue->mValue;
          nsCSSUnit unit = value.GetUnit();
          nsStyleContentType type;
          nsStyleContentData &data = content->ContentAt(count++);
          switch (unit) {
          case eCSSUnit_String:   type = eStyleContentType_String;    break;
          case eCSSUnit_Image:    type = eStyleContentType_Image;     break;
          case eCSSUnit_Attr:     type = eStyleContentType_Attr;      break;
          case eCSSUnit_Counter:  type = eStyleContentType_Counter;   break;
          case eCSSUnit_Counters: type = eStyleContentType_Counters;  break;
          case eCSSUnit_Enumerated:
            switch (value.GetIntValue()) {
            case NS_STYLE_CONTENT_OPEN_QUOTE:
              type = eStyleContentType_OpenQuote;     break;
            case NS_STYLE_CONTENT_CLOSE_QUOTE:
              type = eStyleContentType_CloseQuote;    break;
            case NS_STYLE_CONTENT_NO_OPEN_QUOTE:
              type = eStyleContentType_NoOpenQuote;   break;
            case NS_STYLE_CONTENT_NO_CLOSE_QUOTE:
              type = eStyleContentType_NoCloseQuote;  break;
            default:
              NS_ERROR("bad content value");
            }
            break;
          default:
            NS_ERROR("bad content type");
          }
          data.mType = type;
          if (type == eStyleContentType_Image) {
            NS_SET_IMAGE_REQUEST(data.SetImage, aContext, value.GetImageValue());
          }
          else if (type <= eStyleContentType_Attr) {
            value.GetStringValue(buffer);
            data.mContent.mString = NS_strdup(buffer.get());
          }
          else if (type <= eStyleContentType_Counters) {
            data.mContent.mCounters = value.GetArrayValue();
            data.mContent.mCounters->AddRef();
          }
          else {
            data.mContent.mString = nsnull;
          }
          contentValue = contentValue->mNext;
        }
      }
      break;
  }

  default:
    NS_ABORT_IF_FALSE(false,
                      nsPrintfCString(64, "unrecognized content unit %d",
                                      contentData.mContent.GetUnit()).get());
  }

  // counter-increment: [string [int]]+, none, inherit
  switch (contentData.mCounterIncrement.GetUnit()) {
  case eCSSUnit_Null:
    break;

  case eCSSUnit_None:
  case eCSSUnit_Initial:
    content->AllocateCounterIncrements(0);
    break;

  case eCSSUnit_Inherit:
    canStoreInRuleTree = PR_FALSE;
    count = parentContent->CounterIncrementCount();
    if (NS_SUCCEEDED(content->AllocateCounterIncrements(count))) {
      while (0 < count--) {
        const nsStyleCounterData *data =
          parentContent->GetCounterIncrementAt(count);
        content->SetCounterIncrementAt(count, data->mCounter, data->mValue);
      }
    }
    break;

  case eCSSUnit_PairList:
  case eCSSUnit_PairListDep: {
    const nsCSSValuePairList* ourIncrement =
      contentData.mCounterIncrement.GetPairListValue();
    NS_ABORT_IF_FALSE(ourIncrement->mXValue.GetUnit() == eCSSUnit_Ident,
                      "unexpected value unit");
    count = ListLength(ourIncrement);
    if (NS_FAILED(content->AllocateCounterIncrements(count))) {
      break;
    }

    count = 0;
    for (const nsCSSValuePairList* p = ourIncrement; p; p = p->mNext, count++) {
      PRInt32 increment;
      if (p->mYValue.GetUnit() == eCSSUnit_Integer) {
        increment = p->mYValue.GetIntValue();
      } else {
        increment = 1;
      }
      p->mXValue.GetStringValue(buffer);
      content->SetCounterIncrementAt(count, buffer, increment);
    }
    break;
  }

  default:
    NS_ABORT_IF_FALSE(false, "unexpected value unit");
  }

  // counter-reset: [string [int]]+, none, inherit
  switch (contentData.mCounterReset.GetUnit()) {
  case eCSSUnit_Null:
    break;

  case eCSSUnit_None:
  case eCSSUnit_Initial:
    content->AllocateCounterResets(0);
    break;

  case eCSSUnit_Inherit:
    canStoreInRuleTree = PR_FALSE;
    count = parentContent->CounterResetCount();
    if (NS_SUCCEEDED(content->AllocateCounterResets(count))) {
      while (0 < count--) {
        const nsStyleCounterData *data =
          parentContent->GetCounterResetAt(count);
        content->SetCounterResetAt(count, data->mCounter, data->mValue);
      }
    }
    break;

  case eCSSUnit_PairList:
  case eCSSUnit_PairListDep: {
    const nsCSSValuePairList* ourReset =
      contentData.mCounterReset.GetPairListValue();
    NS_ABORT_IF_FALSE(ourReset->mXValue.GetUnit() == eCSSUnit_Ident,
                      "unexpected value unit");
    count = ListLength(ourReset);
    if (NS_FAILED(content->AllocateCounterResets(count))) {
      break;
    }

    count = 0;
    for (const nsCSSValuePairList* p = ourReset; p; p = p->mNext, count++) {
      PRInt32 reset;
      if (p->mYValue.GetUnit() == eCSSUnit_Integer) {
        reset = p->mYValue.GetIntValue();
      } else {
        reset = 0;
      }
      p->mXValue.GetStringValue(buffer);
      content->SetCounterResetAt(count, buffer, reset);
    }
    break;
  }

  default:
    NS_ABORT_IF_FALSE(false, "unexpected value unit");
  }

  // marker-offset: length, auto, inherit
  SetCoord(contentData.mMarkerOffset, content->mMarkerOffset, parentContent->mMarkerOffset,
           SETCOORD_LH | SETCOORD_AUTO | SETCOORD_INITIAL_AUTO |
             SETCOORD_CALC_LENGTH_ONLY,
           aContext, mPresContext, canStoreInRuleTree);

  // If we ended up with an image, track it.
  for (PRUint32 i = 0; i < content->ContentCount(); ++i) {
    if ((content->ContentAt(i).mType == eStyleContentType_Image) &&
        content->ContentAt(i).mContent.mImage) {
      content->ContentAt(i).TrackImage(aContext->PresContext());
    }
  }

  COMPUTE_END_RESET(Content, content)
}

const void*
nsRuleNode::ComputeQuotesData(void* aStartStruct,
                              const nsRuleDataStruct& aData,
                              nsStyleContext* aContext,
                              nsRuleNode* aHighestNode,
                              const RuleDetail aRuleDetail,
                              const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_INHERITED(Quotes, (), quotes, parentQuotes,
                          Content, contentData)

  // quotes: inherit, initial, none, [string string]+
  switch (contentData.mQuotes.GetUnit()) {
  case eCSSUnit_Null:
    break;
  case eCSSUnit_Inherit:
    canStoreInRuleTree = PR_FALSE;
    quotes->CopyFrom(*parentQuotes);
    break;
  case eCSSUnit_Initial:
    quotes->SetInitial();
    break;
  case eCSSUnit_None:
    quotes->AllocateQuotes(0);
    break;
  case eCSSUnit_PairList:
  case eCSSUnit_PairListDep: {
    const nsCSSValuePairList* ourQuotes
      = contentData.mQuotes.GetPairListValue();
    nsAutoString buffer;
    nsAutoString closeBuffer;
    PRUint32 count = ListLength(ourQuotes);
    if (NS_FAILED(quotes->AllocateQuotes(count))) {
      break;
    }
    count = 0;
    while (ourQuotes) {
      NS_ABORT_IF_FALSE(ourQuotes->mXValue.GetUnit() == eCSSUnit_String &&
                        ourQuotes->mYValue.GetUnit() == eCSSUnit_String,
                        "improper list contents for quotes");
      ourQuotes->mXValue.GetStringValue(buffer);
      ourQuotes->mYValue.GetStringValue(closeBuffer);
      quotes->SetQuotesAt(count++, buffer, closeBuffer);
      ourQuotes = ourQuotes->mNext;
    }
    break;
  }
  default:
    NS_ABORT_IF_FALSE(false, "unexpected value unit");
  }

  COMPUTE_END_INHERITED(Quotes, quotes)
}

const void*
nsRuleNode::ComputeXULData(void* aStartStruct,
                           const nsRuleDataStruct& aData,
                           nsStyleContext* aContext,
                           nsRuleNode* aHighestNode,
                           const RuleDetail aRuleDetail,
                           const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_RESET(XUL, (), xul, parentXUL, XUL, xulData)

  // box-align: enum, inherit, initial
  SetDiscrete(xulData.mBoxAlign, xul->mBoxAlign, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentXUL->mBoxAlign,
              NS_STYLE_BOX_ALIGN_STRETCH, 0, 0, 0, 0);

  // box-direction: enum, inherit, initial
  SetDiscrete(xulData.mBoxDirection, xul->mBoxDirection, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentXUL->mBoxDirection,
              NS_STYLE_BOX_DIRECTION_NORMAL, 0, 0, 0, 0);

  // box-flex: factor, inherit
  SetFactor(xulData.mBoxFlex, xul->mBoxFlex, canStoreInRuleTree,
            parentXUL->mBoxFlex, 0.0f);

  // box-orient: enum, inherit, initial
  SetDiscrete(xulData.mBoxOrient, xul->mBoxOrient, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentXUL->mBoxOrient,
              NS_STYLE_BOX_ORIENT_HORIZONTAL, 0, 0, 0, 0);

  // box-pack: enum, inherit, initial
  SetDiscrete(xulData.mBoxPack, xul->mBoxPack, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentXUL->mBoxPack,
              NS_STYLE_BOX_PACK_START, 0, 0, 0, 0);

  // box-ordinal-group: integer, inherit, initial
  SetDiscrete(xulData.mBoxOrdinal, xul->mBoxOrdinal, canStoreInRuleTree,
              SETDSC_INTEGER, parentXUL->mBoxOrdinal, 1,
              0, 0, 0, 0);

  if (eCSSUnit_Inherit == xulData.mStackSizing.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    xul->mStretchStack = parentXUL->mStretchStack;
  } else if (eCSSUnit_Initial == xulData.mStackSizing.GetUnit()) {
    xul->mStretchStack = PR_TRUE;
  } else if (eCSSUnit_Enumerated == xulData.mStackSizing.GetUnit()) {
    xul->mStretchStack = xulData.mStackSizing.GetIntValue() ==
      NS_STYLE_STACK_SIZING_STRETCH_TO_FIT;
  }

  COMPUTE_END_RESET(XUL, xul)
}

const void*
nsRuleNode::ComputeColumnData(void* aStartStruct,
                              const nsRuleDataStruct& aData,
                              nsStyleContext* aContext,
                              nsRuleNode* aHighestNode,
                              const RuleDetail aRuleDetail,
                              const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_RESET(Column, (mPresContext), column, parent, Column, columnData)

  // column-width: length, auto, inherit
  SetCoord(columnData.mColumnWidth,
           column->mColumnWidth, parent->mColumnWidth,
           SETCOORD_LAH | SETCOORD_INITIAL_AUTO |
           SETCOORD_CALC_LENGTH_ONLY | SETCOORD_CALC_CLAMP_NONNEGATIVE,
           aContext, mPresContext, canStoreInRuleTree);

  // column-gap: length, percentage, inherit, normal
  SetCoord(columnData.mColumnGap,
           column->mColumnGap, parent->mColumnGap,
           SETCOORD_LPH | SETCOORD_NORMAL | SETCOORD_INITIAL_NORMAL,
           aContext, mPresContext, canStoreInRuleTree);

  // column-count: auto, integer, inherit
  if (eCSSUnit_Auto == columnData.mColumnCount.GetUnit() ||
      eCSSUnit_Initial == columnData.mColumnCount.GetUnit()) {
    column->mColumnCount = NS_STYLE_COLUMN_COUNT_AUTO;
  } else if (eCSSUnit_Integer == columnData.mColumnCount.GetUnit()) {
    column->mColumnCount = columnData.mColumnCount.GetIntValue();
    // Max 1000 columns - wallpaper for bug 345583.
    column->mColumnCount = NS_MIN(column->mColumnCount, 1000U);
  } else if (eCSSUnit_Inherit == columnData.mColumnCount.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    column->mColumnCount = parent->mColumnCount;
  }

  // column-rule-width: length, enum, inherit
  const nsCSSValue& widthValue = columnData.mColumnRuleWidth;
  if (eCSSUnit_Initial == widthValue.GetUnit()) {
    column->SetColumnRuleWidth(
        (mPresContext->GetBorderWidthTable())[NS_STYLE_BORDER_WIDTH_MEDIUM]);
  }
  else if (eCSSUnit_Enumerated == widthValue.GetUnit()) {
    NS_ASSERTION(widthValue.GetIntValue() == NS_STYLE_BORDER_WIDTH_THIN ||
                 widthValue.GetIntValue() == NS_STYLE_BORDER_WIDTH_MEDIUM ||
                 widthValue.GetIntValue() == NS_STYLE_BORDER_WIDTH_THICK,
                 "Unexpected enum value");
    column->SetColumnRuleWidth(
        (mPresContext->GetBorderWidthTable())[widthValue.GetIntValue()]);
  }
  else if (eCSSUnit_Inherit == widthValue.GetUnit()) {
    column->SetColumnRuleWidth(parent->GetComputedColumnRuleWidth());
    canStoreInRuleTree = PR_FALSE;
  }
  else if (widthValue.IsLengthUnit() || widthValue.IsCalcUnit()) {
    nscoord len =
      CalcLength(widthValue, aContext, mPresContext, canStoreInRuleTree);
    if (len < 0) {
      // FIXME: This is untested (by test_value_storage.html) for
      // column-rule-width since it gets covered up by the border
      // rounding code.
      NS_ASSERTION(widthValue.IsCalcUnit(),
                   "parser should have rejected negative length");
      len = 0;
    }
    column->SetColumnRuleWidth(len);
  }

  // column-rule-style: enum, inherit
  const nsCSSValue& styleValue = columnData.mColumnRuleStyle;
  NS_ABORT_IF_FALSE(eCSSUnit_None != styleValue.GetUnit(),
                    "'none' should be handled as enumerated value");
  if (eCSSUnit_Enumerated == styleValue.GetUnit()) {
    column->mColumnRuleStyle = styleValue.GetIntValue();
  }
  else if (eCSSUnit_Initial == styleValue.GetUnit()) {
    column->mColumnRuleStyle = NS_STYLE_BORDER_STYLE_NONE;
  }
  else if (eCSSUnit_Inherit == styleValue.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    column->mColumnRuleStyle = parent->mColumnRuleStyle;
  }

  // column-rule-color: color, inherit
  const nsCSSValue& colorValue = columnData.mColumnRuleColor;
  if (eCSSUnit_Inherit == colorValue.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    column->mColumnRuleColorIsForeground = PR_FALSE;
    if (parent->mColumnRuleColorIsForeground) {
      column->mColumnRuleColor = parentContext->GetStyleColor()->mColor;
    } else {
      column->mColumnRuleColor = parent->mColumnRuleColor;
    }
  }
  else if (eCSSUnit_Initial == colorValue.GetUnit()) {
    column->mColumnRuleColorIsForeground = PR_TRUE;
  }
  else if (SetColor(colorValue, 0, mPresContext, aContext,
                    column->mColumnRuleColor, canStoreInRuleTree)) {
    column->mColumnRuleColorIsForeground = PR_FALSE;
  }

  COMPUTE_END_RESET(Column, column)
}

static void
SetSVGPaint(const nsCSSValue& aValue, const nsStyleSVGPaint& parentPaint,
            nsPresContext* aPresContext, nsStyleContext *aContext,
            nsStyleSVGPaint& aResult, nsStyleSVGPaintType aInitialPaintType,
            PRBool& aCanStoreInRuleTree)
{
  nscolor color;

  if (aValue.GetUnit() == eCSSUnit_Inherit) {
    aResult = parentPaint;
    aCanStoreInRuleTree = PR_FALSE;
  } else if (aValue.GetUnit() == eCSSUnit_None) {
    aResult.SetType(eStyleSVGPaintType_None);
  } else if (aValue.GetUnit() == eCSSUnit_Initial) {
    aResult.SetType(aInitialPaintType);
    aResult.mPaint.mColor = NS_RGB(0, 0, 0);
    aResult.mFallbackColor = NS_RGB(0, 0, 0);
  } else if (SetColor(aValue, NS_RGB(0, 0, 0), aPresContext, aContext,
                      color, aCanStoreInRuleTree)) {
    aResult.SetType(eStyleSVGPaintType_Color);
    aResult.mPaint.mColor = color;
  } else if (aValue.GetUnit() == eCSSUnit_Pair) {
    const nsCSSValuePair& pair = aValue.GetPairValue();
    NS_ABORT_IF_FALSE(pair.mXValue.GetUnit() == eCSSUnit_URL,
                      "malformed paint server value");

    aResult.SetType(eStyleSVGPaintType_Server);
    aResult.mPaint.mPaintServer = pair.mXValue.GetURLValue();
    NS_IF_ADDREF(aResult.mPaint.mPaintServer);

    if (pair.mYValue.GetUnit() == eCSSUnit_None) {
      aResult.mFallbackColor = NS_RGBA(0, 0, 0, 0);
    } else {
      NS_ABORT_IF_FALSE(pair.mYValue.GetUnit() != eCSSUnit_Inherit,
                        "cannot inherit fallback colour");
      SetColor(pair.mYValue, NS_RGB(0, 0, 0), aPresContext, aContext,
               aResult.mFallbackColor, aCanStoreInRuleTree);
    }
  } else {
    NS_ABORT_IF_FALSE(aValue.GetUnit() == eCSSUnit_Null,
                      "malformed paint server value");
  }
}

const void*
nsRuleNode::ComputeSVGData(void* aStartStruct,
                           const nsRuleDataStruct& aData,
                           nsStyleContext* aContext,
                           nsRuleNode* aHighestNode,
                           const RuleDetail aRuleDetail,
                           const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_INHERITED(SVG, (), svg, parentSVG, SVG, SVGData)

  // clip-rule: enum, inherit, initial
  SetDiscrete(SVGData.mClipRule, svg->mClipRule, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentSVG->mClipRule,
              NS_STYLE_FILL_RULE_NONZERO, 0, 0, 0, 0);

  // color-interpolation: enum, inherit, initial
  SetDiscrete(SVGData.mColorInterpolation,
              svg->mColorInterpolation, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentSVG->mColorInterpolation,
              NS_STYLE_COLOR_INTERPOLATION_SRGB, 0, 0, 0, 0);

  // color-interpolation-filters: enum, inherit, initial
  SetDiscrete(SVGData.mColorInterpolationFilters,
              svg->mColorInterpolationFilters, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentSVG->mColorInterpolationFilters,
              NS_STYLE_COLOR_INTERPOLATION_LINEARRGB, 0, 0, 0, 0);

  // fill:
  SetSVGPaint(SVGData.mFill, parentSVG->mFill, mPresContext, aContext,
              svg->mFill, eStyleSVGPaintType_Color, canStoreInRuleTree);

  // fill-opacity: factor, inherit, initial
  SetFactor(SVGData.mFillOpacity, svg->mFillOpacity, canStoreInRuleTree,
            parentSVG->mFillOpacity, 1.0f, SETFCT_OPACITY);

  // fill-rule: enum, inherit, initial
  SetDiscrete(SVGData.mFillRule, svg->mFillRule, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentSVG->mFillRule,
              NS_STYLE_FILL_RULE_NONZERO, 0, 0, 0, 0);

  // image-rendering: enum, inherit
  SetDiscrete(SVGData.mImageRendering, svg->mImageRendering, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentSVG->mImageRendering,
              NS_STYLE_IMAGE_RENDERING_AUTO, 0, 0, 0, 0);

  // marker-end: url, none, inherit
  if (eCSSUnit_URL == SVGData.mMarkerEnd.GetUnit()) {
    svg->mMarkerEnd = SVGData.mMarkerEnd.GetURLValue();
  } else if (eCSSUnit_None == SVGData.mMarkerEnd.GetUnit() ||
             eCSSUnit_Initial == SVGData.mMarkerEnd.GetUnit()) {
    svg->mMarkerEnd = nsnull;
  } else if (eCSSUnit_Inherit == SVGData.mMarkerEnd.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    svg->mMarkerEnd = parentSVG->mMarkerEnd;
  }

  // marker-mid: url, none, inherit
  if (eCSSUnit_URL == SVGData.mMarkerMid.GetUnit()) {
    svg->mMarkerMid = SVGData.mMarkerMid.GetURLValue();
  } else if (eCSSUnit_None == SVGData.mMarkerMid.GetUnit() ||
             eCSSUnit_Initial == SVGData.mMarkerMid.GetUnit()) {
    svg->mMarkerMid = nsnull;
  } else if (eCSSUnit_Inherit == SVGData.mMarkerMid.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    svg->mMarkerMid = parentSVG->mMarkerMid;
  }

  // marker-start: url, none, inherit
  if (eCSSUnit_URL == SVGData.mMarkerStart.GetUnit()) {
    svg->mMarkerStart = SVGData.mMarkerStart.GetURLValue();
  } else if (eCSSUnit_None == SVGData.mMarkerStart.GetUnit() ||
             eCSSUnit_Initial == SVGData.mMarkerStart.GetUnit()) {
    svg->mMarkerStart = nsnull;
  } else if (eCSSUnit_Inherit == SVGData.mMarkerStart.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    svg->mMarkerStart = parentSVG->mMarkerStart;
  }

  // shape-rendering: enum, inherit
  SetDiscrete(SVGData.mShapeRendering, svg->mShapeRendering, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentSVG->mShapeRendering,
              NS_STYLE_SHAPE_RENDERING_AUTO, 0, 0, 0, 0);

  // stroke:
  SetSVGPaint(SVGData.mStroke, parentSVG->mStroke, mPresContext, aContext,
              svg->mStroke, eStyleSVGPaintType_None, canStoreInRuleTree);

  // stroke-dasharray: <dasharray>, none, inherit
  switch (SVGData.mStrokeDasharray.GetUnit()) {
  case eCSSUnit_Null:
    break;

  case eCSSUnit_Inherit:
    canStoreInRuleTree = PR_FALSE;
    // only do the copy if weren't already set up by the copy constructor
    // FIXME Bug 389408: This is broken when aStartStruct is non-null!
    if (!svg->mStrokeDasharray) {
      svg->mStrokeDasharrayLength = parentSVG->mStrokeDasharrayLength;
      if (svg->mStrokeDasharrayLength) {
        svg->mStrokeDasharray = new nsStyleCoord[svg->mStrokeDasharrayLength];
        if (svg->mStrokeDasharray)
          memcpy(svg->mStrokeDasharray,
                 parentSVG->mStrokeDasharray,
                 svg->mStrokeDasharrayLength * sizeof(nsStyleCoord));
        else
          svg->mStrokeDasharrayLength = 0;
      }
    }
    break;

  case eCSSUnit_Initial:
  case eCSSUnit_None:
    delete [] svg->mStrokeDasharray;
    svg->mStrokeDasharray = nsnull;
    svg->mStrokeDasharrayLength = 0;
    break;

  case eCSSUnit_List:
  case eCSSUnit_ListDep: {
    delete [] svg->mStrokeDasharray;
    svg->mStrokeDasharray = nsnull;
    svg->mStrokeDasharrayLength = 0;

    // count number of values
    const nsCSSValueList *value = SVGData.mStrokeDasharray.GetListValue();
    svg->mStrokeDasharrayLength = ListLength(value);

    NS_ASSERTION(svg->mStrokeDasharrayLength != 0, "no dasharray items");

    svg->mStrokeDasharray = new nsStyleCoord[svg->mStrokeDasharrayLength];

    if (svg->mStrokeDasharray) {
      PRUint32 i = 0;
      while (nsnull != value) {
        SetCoord(value->mValue,
                 svg->mStrokeDasharray[i++], nsStyleCoord(),
                 SETCOORD_LP | SETCOORD_FACTOR,
                 aContext, mPresContext, canStoreInRuleTree);
        value = value->mNext;
      }
    } else {
      svg->mStrokeDasharrayLength = 0;
    }
    break;
  }

  default:
    NS_ABORT_IF_FALSE(false, "unrecognized dasharray unit");
  }

  // stroke-dashoffset: <dashoffset>, inherit
  SetCoord(SVGData.mStrokeDashoffset,
           svg->mStrokeDashoffset, parentSVG->mStrokeDashoffset,
           SETCOORD_LPH | SETCOORD_FACTOR | SETCOORD_INITIAL_ZERO,
           aContext, mPresContext, canStoreInRuleTree);

  // stroke-linecap: enum, inherit, initial
  SetDiscrete(SVGData.mStrokeLinecap, svg->mStrokeLinecap, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentSVG->mStrokeLinecap,
              NS_STYLE_STROKE_LINECAP_BUTT, 0, 0, 0, 0);

  // stroke-linejoin: enum, inherit, initial
  SetDiscrete(SVGData.mStrokeLinejoin, svg->mStrokeLinejoin, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentSVG->mStrokeLinejoin,
              NS_STYLE_STROKE_LINEJOIN_MITER, 0, 0, 0, 0);

  // stroke-miterlimit: <miterlimit>, inherit
  SetFactor(SVGData.mStrokeMiterlimit, svg->mStrokeMiterlimit,
            canStoreInRuleTree,
            parentSVG->mStrokeMiterlimit, 4.0f);

  // stroke-opacity:
  SetFactor(SVGData.mStrokeOpacity, svg->mStrokeOpacity, canStoreInRuleTree,
            parentSVG->mStrokeOpacity, 1.0f, SETFCT_OPACITY);

  // stroke-width:
  if (eCSSUnit_Initial == SVGData.mStrokeWidth.GetUnit()) {
    svg->mStrokeWidth.SetCoordValue(nsPresContext::CSSPixelsToAppUnits(1));
  } else {
    SetCoord(SVGData.mStrokeWidth,
             svg->mStrokeWidth, parentSVG->mStrokeWidth,
             SETCOORD_LPH | SETCOORD_FACTOR,
             aContext, mPresContext, canStoreInRuleTree);
  }

  // text-anchor: enum, inherit, initial
  SetDiscrete(SVGData.mTextAnchor, svg->mTextAnchor, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentSVG->mTextAnchor,
              NS_STYLE_TEXT_ANCHOR_START, 0, 0, 0, 0);

  // text-rendering: enum, inherit, initial
  SetDiscrete(SVGData.mTextRendering, svg->mTextRendering, canStoreInRuleTree,
              SETDSC_ENUMERATED, parentSVG->mTextRendering,
              NS_STYLE_TEXT_RENDERING_AUTO, 0, 0, 0, 0);

  COMPUTE_END_INHERITED(SVG, svg)
}

const void*
nsRuleNode::ComputeSVGResetData(void* aStartStruct,
                                const nsRuleDataStruct& aData,
                                nsStyleContext* aContext,
                                nsRuleNode* aHighestNode,
                                const RuleDetail aRuleDetail,
                                const PRBool aCanStoreInRuleTree)
{
  COMPUTE_START_RESET(SVGReset, (), svgReset, parentSVGReset, SVG, SVGData)

  // stop-color:
  if (eCSSUnit_Initial == SVGData.mStopColor.GetUnit()) {
    svgReset->mStopColor = NS_RGB(0, 0, 0);
  } else {
    SetColor(SVGData.mStopColor, parentSVGReset->mStopColor,
             mPresContext, aContext, svgReset->mStopColor, canStoreInRuleTree);
  }

  // flood-color:
  if (eCSSUnit_Initial == SVGData.mFloodColor.GetUnit()) {
    svgReset->mFloodColor = NS_RGB(0, 0, 0);
  } else {
    SetColor(SVGData.mFloodColor, parentSVGReset->mFloodColor,
             mPresContext, aContext, svgReset->mFloodColor, canStoreInRuleTree);
  }

  // lighting-color:
  if (eCSSUnit_Initial == SVGData.mLightingColor.GetUnit()) {
    svgReset->mLightingColor = NS_RGB(255, 255, 255);
  } else {
    SetColor(SVGData.mLightingColor, parentSVGReset->mLightingColor,
             mPresContext, aContext, svgReset->mLightingColor,
             canStoreInRuleTree);
  }

  // clip-path: url, none, inherit
  if (eCSSUnit_URL == SVGData.mClipPath.GetUnit()) {
    svgReset->mClipPath = SVGData.mClipPath.GetURLValue();
  } else if (eCSSUnit_None == SVGData.mClipPath.GetUnit() ||
             eCSSUnit_Initial == SVGData.mClipPath.GetUnit()) {
    svgReset->mClipPath = nsnull;
  } else if (eCSSUnit_Inherit == SVGData.mClipPath.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    svgReset->mClipPath = parentSVGReset->mClipPath;
  }

  // stop-opacity:
  SetFactor(SVGData.mStopOpacity, svgReset->mStopOpacity, canStoreInRuleTree,
            parentSVGReset->mStopOpacity, 1.0f, SETFCT_OPACITY);

  // flood-opacity:
  SetFactor(SVGData.mFloodOpacity, svgReset->mFloodOpacity, canStoreInRuleTree,
            parentSVGReset->mFloodOpacity, 1.0f, SETFCT_OPACITY);

  // dominant-baseline: enum, inherit, initial
  SetDiscrete(SVGData.mDominantBaseline, svgReset->mDominantBaseline,
              canStoreInRuleTree, SETDSC_ENUMERATED,
              parentSVGReset->mDominantBaseline,
              NS_STYLE_DOMINANT_BASELINE_AUTO, 0, 0, 0, 0);

  // filter: url, none, inherit
  if (eCSSUnit_URL == SVGData.mFilter.GetUnit()) {
    svgReset->mFilter = SVGData.mFilter.GetURLValue();
  } else if (eCSSUnit_None == SVGData.mFilter.GetUnit() ||
             eCSSUnit_Initial == SVGData.mFilter.GetUnit()) {
    svgReset->mFilter = nsnull;
  } else if (eCSSUnit_Inherit == SVGData.mFilter.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    svgReset->mFilter = parentSVGReset->mFilter;
  }

  // mask: url, none, inherit
  if (eCSSUnit_URL == SVGData.mMask.GetUnit()) {
    svgReset->mMask = SVGData.mMask.GetURLValue();
  } else if (eCSSUnit_None == SVGData.mMask.GetUnit() ||
             eCSSUnit_Initial == SVGData.mMask.GetUnit()) {
    svgReset->mMask = nsnull;
  } else if (eCSSUnit_Inherit == SVGData.mMask.GetUnit()) {
    canStoreInRuleTree = PR_FALSE;
    svgReset->mMask = parentSVGReset->mMask;
  }

  COMPUTE_END_RESET(SVGReset, svgReset)
}

inline const void*
nsRuleNode::GetParentData(const nsStyleStructID aSID)
{
  NS_PRECONDITION(mDependentBits & nsCachedStyleData::GetBitForSID(aSID),
                  "should be called when node depends on parent data");
  NS_ASSERTION(mStyleData.GetStyleData(aSID) == nsnull,
               "both struct and dependent bits present");
  // Walk up the rule tree from this rule node (towards less specific
  // rules).
  PRUint32 bit = nsCachedStyleData::GetBitForSID(aSID);
  nsRuleNode *ruleNode = mParent;
  while (ruleNode->mDependentBits & bit) {
    NS_ASSERTION(ruleNode->mStyleData.GetStyleData(aSID) == nsnull,
                 "both struct and dependent bits present");
    ruleNode = ruleNode->mParent;
  }

  return ruleNode->mStyleData.GetStyleData(aSID);
}

#define STYLE_STRUCT(name_, checkdata_cb_, ctor_args_)                      \
inline const nsStyle##name_ *                                               \
nsRuleNode::GetParent##name_()                                              \
{                                                                           \
  NS_PRECONDITION(mDependentBits &                                          \
                  nsCachedStyleData::GetBitForSID(eStyleStruct_##name_),    \
                  "should be called when node depends on parent data");     \
  NS_ASSERTION(mStyleData.GetStyle##name_() == nsnull,                      \
               "both struct and dependent bits present");                   \
  /* Walk up the rule tree from this rule node (towards less specific */    \
  /* rules). */                                                             \
  PRUint32 bit = nsCachedStyleData::GetBitForSID(eStyleStruct_##name_);     \
  nsRuleNode *ruleNode = mParent;                                           \
  while (ruleNode->mDependentBits & bit) {                                  \
    NS_ASSERTION(ruleNode->mStyleData.GetStyle##name_() == nsnull,          \
                 "both struct and dependent bits present");                 \
    ruleNode = ruleNode->mParent;                                           \
  }                                                                         \
                                                                            \
  return ruleNode->mStyleData.GetStyle##name_();                            \
}
#include "nsStyleStructList.h"
#undef STYLE_STRUCT

const void*
nsRuleNode::GetStyleData(nsStyleStructID aSID,
                         nsStyleContext* aContext,
                         PRBool aComputeData)
{
  const void *data;
  if (mDependentBits & nsCachedStyleData::GetBitForSID(aSID)) {
    // We depend on an ancestor for this struct since the cached struct
    // it has is also appropriate for this rule node.  Just go up the
    // rule tree and return the first cached struct we find.
    data = GetParentData(aSID);
    NS_ASSERTION(data, "dependent bits set but no cached struct present");
    return data;
  }

  data = mStyleData.GetStyleData(aSID);
  if (NS_LIKELY(data != nsnull))
    return data; // We have a fully specified struct. Just return it.

  if (NS_UNLIKELY(!aComputeData))
    return nsnull;

  // Nothing is cached.  We'll have to delve further and examine our rules.
#define STYLE_STRUCT_TEST aSID
#define STYLE_STRUCT(name, checkdata_cb, ctor_args) \
  data = Get##name##Data(aContext);
#include "nsStyleStructList.h"
#undef STYLE_STRUCT
#undef STYLE_STRUCT_TEST

  if (NS_LIKELY(data != nsnull))
    return data;

  NS_NOTREACHED("could not create style struct");
  // To ensure that |GetStyleData| never returns null (even when we're
  // out of memory), we'll get the style set and get a copy of the
  // default values for the given style struct from the set.  Note that
  // this works fine even if |this| is a rule node that has been
  // destroyed (leftover from a previous rule tree) but is somehow still
  // used.
  return mPresContext->PresShell()->StyleSet()->
    DefaultStyleData()->GetStyleData(aSID);
}

// See comments above in GetStyleData for an explanation of what the
// code below does.
#define STYLE_STRUCT(name_, checkdata_cb_, ctor_args_)                        \
const nsStyle##name_*                                                         \
nsRuleNode::GetStyle##name_(nsStyleContext* aContext, PRBool aComputeData)    \
{                                                                             \
  const nsStyle##name_ *data;                                                 \
  if (mDependentBits &                                                        \
      nsCachedStyleData::GetBitForSID(eStyleStruct_##name_)) {                \
    data = GetParent##name_();                                                \
    NS_ASSERTION(data, "dependent bits set but no cached struct present");    \
    return data;                                                              \
  }                                                                           \
                                                                              \
  data = mStyleData.GetStyle##name_();                                        \
  if (NS_LIKELY(data != nsnull))                                              \
    return data;                                                              \
                                                                              \
  if (NS_UNLIKELY(!aComputeData))                                             \
    return nsnull;                                                            \
                                                                              \
  data =                                                                      \
    static_cast<const nsStyle##name_ *>(Get##name_##Data(aContext));          \
                                                                              \
  if (NS_LIKELY(data != nsnull))                                              \
    return data;                                                              \
                                                                              \
  NS_NOTREACHED("could not create style struct");                             \
  return                                                                      \
    static_cast<const nsStyle##name_ *>(                                      \
                   mPresContext->PresShell()->StyleSet()->                    \
                     DefaultStyleData()->GetStyleData(eStyleStruct_##name_)); \
}
#include "nsStyleStructList.h"
#undef STYLE_STRUCT

void
nsRuleNode::Mark()
{
  for (nsRuleNode *node = this;
       node && !(node->mDependentBits & NS_RULE_NODE_GC_MARK);
       node = node->mParent)
    node->mDependentBits |= NS_RULE_NODE_GC_MARK;
}

static PLDHashOperator
SweepRuleNodeChildren(PLDHashTable *table, PLDHashEntryHdr *hdr,
                      PRUint32 number, void *arg)
{
  ChildrenHashEntry *entry = static_cast<ChildrenHashEntry*>(hdr);
  if (entry->mRuleNode->Sweep())
    return PL_DHASH_REMOVE; // implies NEXT, unless |ed with STOP
  return PL_DHASH_NEXT;
}

PRBool
nsRuleNode::Sweep()
{
  // If we're not marked, then we have to delete ourself.
  // However, we never allow the root node to GC itself, because nsStyleSet
  // wants to hold onto the root node and not worry about re-creating a
  // rule walker if the root node is deleted.
  if (!(mDependentBits & NS_RULE_NODE_GC_MARK) &&
      // Skip this only if we're the *current* root and not an old one.
      !(IsRoot() && mPresContext->StyleSet()->GetRuleTree() == this)) {
    Destroy();
    return PR_TRUE;
  }

  // Clear our mark, for the next time around.
  mDependentBits &= ~NS_RULE_NODE_GC_MARK;

  // Call sweep on the children, since some may not be marked, and
  // remove any deleted children from the child lists.
  if (HaveChildren()) {
    PRUint32 childrenDestroyed;
    if (ChildrenAreHashed()) {
      PLDHashTable *children = ChildrenHash();
      PRUint32 oldChildCount = children->entryCount;
      PL_DHashTableEnumerate(children, SweepRuleNodeChildren, nsnull);
      childrenDestroyed = children->entryCount - oldChildCount;
    } else {
      childrenDestroyed = 0;
      for (nsRuleNode **children = ChildrenListPtr(); *children; ) {
        nsRuleNode *next = (*children)->mNextSibling;
        if ((*children)->Sweep()) {
          // This rule node was destroyed, so implicitly advance by
          // making *children point to the next entry.
          *children = next;
          ++childrenDestroyed;
        } else {
          // Advance.
          children = &(*children)->mNextSibling;
        }
      }
    }
    mRefCnt -= childrenDestroyed;
    NS_POSTCONDITION(IsRoot() || mRefCnt > 0,
                     "We didn't get swept, so we'd better have style contexts "
                     "pointing to us or to one of our descendants, which means "
                     "we'd better have a nonzero mRefCnt here!");
  }
  return PR_FALSE;
}

/* static */ PRBool
nsRuleNode::HasAuthorSpecifiedRules(nsStyleContext* aStyleContext,
                                    PRUint32 ruleTypeMask,
                                    PRBool aAuthorColorsAllowed)
{
  nsRuleDataColor colorData;
  nsRuleDataMargin marginData;
  PRUint32 nValues = 0;

  PRUint32 inheritBits = 0;
  if (ruleTypeMask & NS_AUTHOR_SPECIFIED_BACKGROUND)
    inheritBits |= NS_STYLE_INHERIT_BIT(Background);

  if (ruleTypeMask & NS_AUTHOR_SPECIFIED_BORDER)
    inheritBits |= NS_STYLE_INHERIT_BIT(Border);

  if (ruleTypeMask & NS_AUTHOR_SPECIFIED_PADDING)
    inheritBits |= NS_STYLE_INHERIT_BIT(Padding);

  /* We're relying on the use of |aStyleContext| not mutating it! */
  nsRuleData ruleData(inheritBits,
                      aStyleContext->PresContext(), aStyleContext);
  ruleData.mColorData = &colorData;
  ruleData.mMarginData = &marginData;

  nsCSSValue* backgroundValues[] = {
    &colorData.mBackColor,
    &colorData.mBackImage,
  };

  nsCSSValue* borderValues[] = {
    &marginData.mBorderColor.mTop,
    &marginData.mBorderStyle.mTop,
    &marginData.mBorderWidth.mTop,
    &marginData.mBorderColor.mRight,
    &marginData.mBorderStyle.mRight,
    &marginData.mBorderWidth.mRight,
    &marginData.mBorderColor.mBottom,
    &marginData.mBorderStyle.mBottom,
    &marginData.mBorderWidth.mBottom,
    &marginData.mBorderColor.mLeft,
    &marginData.mBorderStyle.mLeft,
    &marginData.mBorderWidth.mLeft
    // XXX add &marginData.mBorder{Start,End}{Width,Color,Style}
  };

  nsCSSValue* paddingValues[] = {
    &marginData.mPadding.mTop,
    &marginData.mPadding.mRight,
    &marginData.mPadding.mBottom,
    &marginData.mPadding.mLeft,
    &marginData.mPaddingStart,
    &marginData.mPaddingEnd
  };

  nsCSSValue* values[NS_ARRAY_LENGTH(backgroundValues) +
                     NS_ARRAY_LENGTH(borderValues) +
                     NS_ARRAY_LENGTH(paddingValues)];

  if (ruleTypeMask & NS_AUTHOR_SPECIFIED_BACKGROUND) {
    memcpy(&values[nValues], backgroundValues,
           NS_ARRAY_LENGTH(backgroundValues) * sizeof(nsCSSValue*));
    nValues += NS_ARRAY_LENGTH(backgroundValues);
  }

  if (ruleTypeMask & NS_AUTHOR_SPECIFIED_BORDER) {
    memcpy(&values[nValues], borderValues,
           NS_ARRAY_LENGTH(borderValues) * sizeof(nsCSSValue*));
    nValues += NS_ARRAY_LENGTH(borderValues);
  }

  if (ruleTypeMask & NS_AUTHOR_SPECIFIED_PADDING) {
    memcpy(&values[nValues], paddingValues,
           NS_ARRAY_LENGTH(paddingValues) * sizeof(nsCSSValue*));
    nValues += NS_ARRAY_LENGTH(paddingValues);
  }

  nsStyleContext* styleContext = aStyleContext;

  // We need to be careful not to count styles covered up by user-important or
  // UA-important declarations.  But we do want to catch explicit inherit
  // styling in those and check our parent style context to see whether we have
  // user styling for those properties.  Note that we don't care here about
  // inheritance due to lack of a specified value, since all the properties we
  // care about are reset properties.
  PRBool haveExplicitUAInherit;
  do {
    haveExplicitUAInherit = PR_FALSE;
    for (nsRuleNode* ruleNode = styleContext->GetRuleNode(); ruleNode;
         ruleNode = ruleNode->GetParent()) {
      nsIStyleRule *rule = ruleNode->GetRule();
      if (rule) {
        ruleData.mLevel = ruleNode->GetLevel();
        ruleData.mIsImportantRule = ruleNode->IsImportantRule();

        rule->MapRuleInfoInto(&ruleData);

        if (ruleData.mLevel == nsStyleSet::eAgentSheet ||
            ruleData.mLevel == nsStyleSet::eUserSheet) {
          // This is a rule whose effect we want to ignore, so if any of
          // the properties we care about were set, set them to the dummy
          // value that they'll never otherwise get.
          for (PRUint32 i = 0; i < nValues; ++i) {
            nsCSSUnit unit = values[i]->GetUnit();
            if (unit != eCSSUnit_Null &&
                unit != eCSSUnit_Dummy &&
                unit != eCSSUnit_DummyInherit) {
              if (unit == eCSSUnit_Inherit) {
                haveExplicitUAInherit = PR_TRUE;
                values[i]->SetDummyInheritValue();
              } else {
                values[i]->SetDummyValue();
              }
            }
          }
        } else {
          // If any of the values we care about was set by the above rule,
          // we have author style.
          for (PRUint32 i = 0; i < nValues; ++i) {
            if (values[i]->GetUnit() != eCSSUnit_Null &&
                values[i]->GetUnit() != eCSSUnit_Dummy && // see above
                values[i]->GetUnit() != eCSSUnit_DummyInherit) {
              // If author colors are not allowed, only claim to have
              // author-specified rules if we're looking at the background
              // color and it's set to transparent.  Anything else should get
              // set to a dummy value instead.
              if (aAuthorColorsAllowed ||
                  (values[i] == &colorData.mBackColor &&
                   !values[i]->IsNonTransparentColor())) {
                return PR_TRUE;
              }

              values[i]->SetDummyValue();
            }
          }
        }
      }
    }

    if (haveExplicitUAInherit) {
      // reset all the eCSSUnit_Null values to eCSSUnit_Dummy (since they're
      // not styled by the author, or by anyone else), and then reset all the
      // eCSSUnit_DummyInherit values to eCSSUnit_Null (so we will be able to
      // detect them being styled by the author) and move up to our parent
      // style context.
      for (PRUint32 i = 0; i < nValues; ++i)
        if (values[i]->GetUnit() == eCSSUnit_Null)
          values[i]->SetDummyValue();
      for (PRUint32 i = 0; i < nValues; ++i)
        if (values[i]->GetUnit() == eCSSUnit_DummyInherit)
          values[i]->Reset();
      styleContext = styleContext->GetParent();
    }
  } while (haveExplicitUAInherit && styleContext);

  return PR_FALSE;
}
