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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Daniel Glazman <glazman@netscape.com>
 *   Mats Palmgren <mats.palmgren@bredband.net>
 *   Jonathon Jongsma <jonathon.jongsma@collabora.co.uk>, Collabora Ltd.
 *   L. David Baron <dbaron@dbaron.org>, Mozilla Corporation
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
 * representation of a declaration block (or style attribute) in a CSS
 * stylesheet
 */

#include "mozilla/css/Declaration.h"
#include "nsPrintfCString.h"

namespace mozilla {
namespace css {

// check that we can fit all the CSS properties into a PRUint8
// for the mOrder array - if not, might need to use PRUint16!
PR_STATIC_ASSERT(eCSSProperty_COUNT_no_shorthands - 1 <= PR_UINT8_MAX);

Declaration::Declaration()
  : mImmutable(PR_FALSE)
{
  MOZ_COUNT_CTOR(mozilla::css::Declaration);
}

Declaration::Declaration(const Declaration& aCopy)
  : mOrder(aCopy.mOrder),
    mData(aCopy.mData ? aCopy.mData->Clone() : nsnull),
    mImportantData(aCopy.mImportantData
                   ? aCopy.mImportantData->Clone() : nsnull),
    mImmutable(PR_FALSE)
{
  MOZ_COUNT_CTOR(mozilla::css::Declaration);
}

Declaration::~Declaration()
{
  MOZ_COUNT_DTOR(mozilla::css::Declaration);
}

void
Declaration::ValueAppended(nsCSSProperty aProperty)
{
  NS_ABORT_IF_FALSE(!mData && !mImportantData,
                    "should only be called while expanded");
  NS_ABORT_IF_FALSE(!nsCSSProps::IsShorthand(aProperty),
                    "shorthands forbidden");
  // order IS important for CSS, so remove and add to the end
  mOrder.RemoveElement(aProperty);
  mOrder.AppendElement(aProperty);
}

void
Declaration::RemoveProperty(nsCSSProperty aProperty)
{
  nsCSSExpandedDataBlock data;
  ExpandTo(&data);
  NS_ABORT_IF_FALSE(!mData && !mImportantData, "Expand didn't null things out");

  if (nsCSSProps::IsShorthand(aProperty)) {
    CSSPROPS_FOR_SHORTHAND_SUBPROPERTIES(p, aProperty) {
      data.ClearLonghandProperty(*p);
      mOrder.RemoveElement(*p);
    }
  } else {
    data.ClearLonghandProperty(aProperty);
    mOrder.RemoveElement(aProperty);
  }

  CompressFrom(&data);
}

PRBool
Declaration::AppendValueToString(nsCSSProperty aProperty,
                                 nsAString& aResult) const
{
  NS_ABORT_IF_FALSE(0 <= aProperty &&
                    aProperty < eCSSProperty_COUNT_no_shorthands,
                    "property ID out of range");

  nsCSSCompressedDataBlock *data = GetValueIsImportant(aProperty)
                                      ? mImportantData : mData;
  const nsCSSValue *val = data->ValueFor(aProperty);
  if (!val) {
    return PR_FALSE;
  }

  val->AppendToString(aProperty, aResult);
  return PR_TRUE;
}

void
Declaration::GetValue(nsCSSProperty aProperty, nsAString& aValue) const
{
  aValue.Truncate(0);

  // simple properties are easy.
  if (!nsCSSProps::IsShorthand(aProperty)) {
    AppendValueToString(aProperty, aValue);
    return;
  }

  // DOM Level 2 Style says (when describing CSS2Properties, although
  // not CSSStyleDeclaration.getPropertyValue):
  //   However, if there is no shorthand declaration that could be added
  //   to the ruleset without changing in any way the rules already
  //   declared in the ruleset (i.e., by adding longhand rules that were
  //   previously not declared in the ruleset), then the empty string
  //   should be returned for the shorthand property.
  // This means we need to check a number of cases:
  //   (1) Since a shorthand sets all sub-properties, if some of its
  //       subproperties were not specified, we must return the empty
  //       string.
  //   (2) Since 'inherit' and 'initial' can only be specified as the
  //       values for entire properties, we need to return the empty
  //       string if some but not all of the subproperties have one of
  //       those values.
  //   (3) Since a single value only makes sense with or without
  //       !important, we return the empty string if some values are
  //       !important and some are not.
  // Since we're doing this check for 'inherit' and 'initial' up front,
  // we can also simplify the property serialization code by serializing
  // those values up front as well.
  PRUint32 totalCount = 0, importantCount = 0,
           initialCount = 0, inheritCount = 0;
  CSSPROPS_FOR_SHORTHAND_SUBPROPERTIES(p, aProperty) {
    if (*p == eCSSProperty__x_system_font ||
         nsCSSProps::PropHasFlags(*p, CSS_PROPERTY_DIRECTIONAL_SOURCE)) {
      // The system-font subproperty and the *-source properties don't count.
      continue;
    }
    ++totalCount;
    const nsCSSValue *val = mData->ValueFor(*p);
    NS_ABORT_IF_FALSE(!val || !mImportantData || !mImportantData->ValueFor(*p),
                      "can't be in both blocks");
    if (!val && mImportantData) {
      ++importantCount;
      val = mImportantData->ValueFor(*p);
    }
    if (!val) {
      // Case (1) above: some subproperties not specified.
      return;
    }
    if (val->GetUnit() == eCSSUnit_Inherit) {
      ++inheritCount;
    } else if (val->GetUnit() == eCSSUnit_Initial) {
      ++initialCount;
    }
  }
  if (importantCount != 0 && importantCount != totalCount) {
    // Case (3), no consistent importance.
    return;
  }
  if (initialCount == totalCount) {
    // Simplify serialization below by serializing initial up-front.
    nsCSSValue(eCSSUnit_Initial).AppendToString(eCSSProperty_UNKNOWN, aValue);
    return;
  }
  if (inheritCount == totalCount) {
    // Simplify serialization below by serializing inherit up-front.
    nsCSSValue(eCSSUnit_Inherit).AppendToString(eCSSProperty_UNKNOWN, aValue);
    return;
  }
  if (initialCount != 0 || inheritCount != 0) {
    // Case (2): partially initial or inherit.
    return;
  }

  nsCSSCompressedDataBlock *data = importantCount ? mImportantData : mData;
  switch (aProperty) {
    case eCSSProperty_margin: 
    case eCSSProperty_padding: 
    case eCSSProperty_border_color: 
    case eCSSProperty_border_style: 
    case eCSSProperty_border_width: {
      const nsCSSProperty* subprops =
        nsCSSProps::SubpropertyEntryFor(aProperty);
      NS_ABORT_IF_FALSE(nsCSSProps::GetStringValue(subprops[0]).Find("-top") !=
                        kNotFound, "first subprop must be top");
      NS_ABORT_IF_FALSE(nsCSSProps::GetStringValue(subprops[1]).Find("-right") !=
                        kNotFound, "second subprop must be right");
      NS_ABORT_IF_FALSE(nsCSSProps::GetStringValue(subprops[2]).Find("-bottom") !=
                        kNotFound, "third subprop must be bottom");
      NS_ABORT_IF_FALSE(nsCSSProps::GetStringValue(subprops[3]).Find("-left") !=
                        kNotFound, "fourth subprop must be left");
      const nsCSSValue &topValue = *data->ValueFor(subprops[0]);
      const nsCSSValue &rightValue = *data->ValueFor(subprops[1]);
      const nsCSSValue &bottomValue = *data->ValueFor(subprops[2]);
      const nsCSSValue &leftValue = *data->ValueFor(subprops[3]);

      NS_ABORT_IF_FALSE(topValue.GetUnit() != eCSSUnit_Null, "null top");
      topValue.AppendToString(subprops[0], aValue);
      if (topValue != rightValue || topValue != leftValue ||
          topValue != bottomValue) {
        aValue.Append(PRUnichar(' '));
        NS_ABORT_IF_FALSE(rightValue.GetUnit() != eCSSUnit_Null, "null right");
        rightValue.AppendToString(subprops[1], aValue);
        if (topValue != bottomValue || rightValue != leftValue) {
          aValue.Append(PRUnichar(' '));
          NS_ABORT_IF_FALSE(bottomValue.GetUnit() != eCSSUnit_Null, "null bottom");
          bottomValue.AppendToString(subprops[2], aValue);
          if (rightValue != leftValue) {
            aValue.Append(PRUnichar(' '));
            NS_ABORT_IF_FALSE(leftValue.GetUnit() != eCSSUnit_Null, "null left");
            leftValue.AppendToString(subprops[3], aValue);
          }
        }
      }
      break;
    }
    case eCSSProperty__moz_border_radius:
    case eCSSProperty__moz_outline_radius: {
      const nsCSSProperty* subprops =
        nsCSSProps::SubpropertyEntryFor(aProperty);
      const nsCSSValue* vals[4] = {
        data->ValueFor(subprops[0]),
        data->ValueFor(subprops[1]),
        data->ValueFor(subprops[2]),
        data->ValueFor(subprops[3])
      };

      // For compatibility, only write a slash and the y-values
      // if they're not identical to the x-values.
      PRBool needY = PR_FALSE;
      for (int i = 0; i < 4; i++) {
        if (vals[i]->GetUnit() == eCSSUnit_Pair) {
          needY = PR_TRUE;
          vals[i]->GetPairValue().mXValue.AppendToString(subprops[i], aValue);
        } else {
          vals[i]->AppendToString(subprops[i], aValue);
        }
        if (i < 3)
          aValue.Append(PRUnichar(' '));
      }

      if (needY) {
        aValue.AppendLiteral(" / ");
        for (int i = 0; i < 4; i++) {
          if (vals[i]->GetUnit() == eCSSUnit_Pair) {
            vals[i]->GetPairValue().mYValue.AppendToString(subprops[i], aValue);
          } else {
            vals[i]->AppendToString(subprops[i], aValue);
          }
          if (i < 3)
            aValue.Append(PRUnichar(' '));
        }
      }
      break;
    }
    case eCSSProperty_border: {
      const nsCSSProperty* subproptables[3] = {
        nsCSSProps::SubpropertyEntryFor(eCSSProperty_border_color),
        nsCSSProps::SubpropertyEntryFor(eCSSProperty_border_style),
        nsCSSProps::SubpropertyEntryFor(eCSSProperty_border_width)
      };
      PRBool match = PR_TRUE;
      for (const nsCSSProperty** subprops = subproptables,
               **subprops_end = subproptables + NS_ARRAY_LENGTH(subproptables);
           subprops < subprops_end; ++subprops) {
        // Check only the first four subprops in each table, since the
        // others are extras for dimensional box properties.
        const nsCSSValue *firstSide = data->ValueFor((*subprops)[0]);
        for (PRInt32 side = 1; side < 4; ++side) {
          const nsCSSValue *otherSide =
            data->ValueFor((*subprops)[side]);
          if (*firstSide != *otherSide)
            match = PR_FALSE;
        }
      }
      if (!match) {
        // We can't express what we have in the border shorthand
        break;
      }
      // tweak aProperty and fall through
      aProperty = eCSSProperty_border_top;
    }
    case eCSSProperty_border_top:
    case eCSSProperty_border_right:
    case eCSSProperty_border_bottom:
    case eCSSProperty_border_left:
    case eCSSProperty_border_start:
    case eCSSProperty_border_end:
    case eCSSProperty__moz_column_rule:
    case eCSSProperty_outline: {
      const nsCSSProperty* subprops =
        nsCSSProps::SubpropertyEntryFor(aProperty);
      NS_ABORT_IF_FALSE(StringEndsWith(nsCSSProps::GetStringValue(subprops[2]),
                                       NS_LITERAL_CSTRING("-color")) ||
                        StringEndsWith(nsCSSProps::GetStringValue(subprops[2]),
                                       NS_LITERAL_CSTRING("-color-value")),
                        "third subprop must be the color property");
      const nsCSSValue *colorValue = data->ValueFor(subprops[2]);
      PRBool isMozUseTextColor =
        colorValue->GetUnit() == eCSSUnit_Enumerated &&
        colorValue->GetIntValue() == NS_STYLE_COLOR_MOZ_USE_TEXT_COLOR;
      if (!AppendValueToString(subprops[0], aValue) ||
          !(aValue.Append(PRUnichar(' ')),
            AppendValueToString(subprops[1], aValue)) ||
          // Don't output a third value when it's -moz-use-text-color.
          !(isMozUseTextColor ||
            (aValue.Append(PRUnichar(' ')),
             AppendValueToString(subprops[2], aValue)))) {
        aValue.Truncate();
      }
      break;
    }
    case eCSSProperty_margin_left:
    case eCSSProperty_margin_right:
    case eCSSProperty_margin_start:
    case eCSSProperty_margin_end:
    case eCSSProperty_padding_left:
    case eCSSProperty_padding_right:
    case eCSSProperty_padding_start:
    case eCSSProperty_padding_end:
    case eCSSProperty_border_left_color:
    case eCSSProperty_border_left_style:
    case eCSSProperty_border_left_width:
    case eCSSProperty_border_right_color:
    case eCSSProperty_border_right_style:
    case eCSSProperty_border_right_width:
    case eCSSProperty_border_start_color:
    case eCSSProperty_border_start_style:
    case eCSSProperty_border_start_width:
    case eCSSProperty_border_end_color:
    case eCSSProperty_border_end_style:
    case eCSSProperty_border_end_width: {
      const nsCSSProperty* subprops =
        nsCSSProps::SubpropertyEntryFor(aProperty);
      NS_ABORT_IF_FALSE(subprops[3] == eCSSProperty_UNKNOWN,
                        "not box property with physical vs. logical cascading");
      AppendValueToString(subprops[0], aValue);
      break;
    }
    case eCSSProperty_background: {
      // We know from above that all subproperties were specified.
      // However, we still can't represent that in the shorthand unless
      // they're all lists of the same length.  So if they're different
      // lengths, we need to bail out.
      // We also need to bail out if an item has background-clip and
      // background-origin that are different and not the default
      // values.  (We omit them if they're both default.)
      const nsCSSValueList *image =
        data->ValueFor(eCSSProperty_background_image)->
        GetListValue();
      const nsCSSValueList *repeat =
        data->ValueFor(eCSSProperty_background_repeat)->
        GetListValue();
      const nsCSSValueList *attachment =
        data->ValueFor(eCSSProperty_background_attachment)->
        GetListValue();
      const nsCSSValuePairList *position =
        data->ValueFor(eCSSProperty_background_position)->
        GetPairListValue();
      const nsCSSValueList *clip =
        data->ValueFor(eCSSProperty_background_clip)->
        GetListValue();
      const nsCSSValueList *origin =
        data->ValueFor(eCSSProperty_background_origin)->
        GetListValue();
      const nsCSSValuePairList *size =
        data->ValueFor(eCSSProperty_background_size)->
        GetPairListValue();
      for (;;) {
        if (size->mXValue.GetUnit() != eCSSUnit_Auto ||
            size->mYValue.GetUnit() != eCSSUnit_Auto) {
          // Non-default background-size, so can't be serialized as shorthand.
          aValue.Truncate();
          return;
        }
        image->mValue.AppendToString(eCSSProperty_background_image, aValue);
        aValue.Append(PRUnichar(' '));
        repeat->mValue.AppendToString(eCSSProperty_background_repeat, aValue);
        aValue.Append(PRUnichar(' '));
        attachment->mValue.AppendToString(eCSSProperty_background_attachment,
                                          aValue);
        aValue.Append(PRUnichar(' '));
        position->mXValue.AppendToString(eCSSProperty_background_position,
                                         aValue);
        aValue.Append(PRUnichar(' '));
        position->mYValue.AppendToString(eCSSProperty_background_position,
                                         aValue);
        NS_ABORT_IF_FALSE(clip->mValue.GetUnit() == eCSSUnit_Enumerated &&
                          origin->mValue.GetUnit() == eCSSUnit_Enumerated,
                          "should not be inherit/initial within list and "
                          "should have returned early for real inherit/initial");
        if (clip->mValue.GetIntValue() != NS_STYLE_BG_CLIP_BORDER ||
            origin->mValue.GetIntValue() != NS_STYLE_BG_ORIGIN_PADDING) {
          PR_STATIC_ASSERT(NS_STYLE_BG_CLIP_BORDER ==
                           NS_STYLE_BG_ORIGIN_BORDER);
          PR_STATIC_ASSERT(NS_STYLE_BG_CLIP_PADDING ==
                           NS_STYLE_BG_ORIGIN_PADDING);
          PR_STATIC_ASSERT(NS_STYLE_BG_CLIP_CONTENT ==
                           NS_STYLE_BG_ORIGIN_CONTENT);
          // The shorthand only has a single clip/origin value which
          // sets both properties.  So if they're different (and
          // non-default), we can't represent the state using the
          // shorthand.
          if (clip->mValue != origin->mValue) {
            aValue.Truncate();
            return;
          }

          aValue.Append(PRUnichar(' '));
          clip->mValue.AppendToString(eCSSProperty_background_clip, aValue);
        }

        image = image->mNext;
        repeat = repeat->mNext;
        attachment = attachment->mNext;
        position = position->mNext;
        clip = clip->mNext;
        origin = origin->mNext;
        size = size->mNext;

        if (!image) {
          if (repeat || attachment || position || clip || origin || size) {
            // Uneven length lists, so can't be serialized as shorthand.
            aValue.Truncate();
            return;
          }
          break;
        }
        if (!repeat || !attachment || !position || !clip || !origin || !size) {
          // Uneven length lists, so can't be serialized as shorthand.
          aValue.Truncate();
          return;
        }
        aValue.Append(PRUnichar(','));
        aValue.Append(PRUnichar(' '));
      }

      aValue.Append(PRUnichar(' '));
      AppendValueToString(eCSSProperty_background_color, aValue);
      break;
    }
    case eCSSProperty_cue: {
      if (AppendValueToString(eCSSProperty_cue_before, aValue)) {
        aValue.Append(PRUnichar(' '));
        if (!AppendValueToString(eCSSProperty_cue_after, aValue))
          aValue.Truncate();
      }
      break;
    }
    case eCSSProperty_font: {
      // systemFont might not be present; the others are guaranteed to be
      // based on the shorthand check at the beginning of the function
      const nsCSSValue *systemFont =
        data->ValueFor(eCSSProperty__x_system_font);
      const nsCSSValue &style =
        *data->ValueFor(eCSSProperty_font_style);
      const nsCSSValue &variant =
        *data->ValueFor(eCSSProperty_font_variant);
      const nsCSSValue &weight =
        *data->ValueFor(eCSSProperty_font_weight);
      const nsCSSValue &size =
        *data->ValueFor(eCSSProperty_font_size);
      const nsCSSValue &lh =
        *data->ValueFor(eCSSProperty_line_height);
      const nsCSSValue &family =
        *data->ValueFor(eCSSProperty_font_family);
      const nsCSSValue &stretch =
        *data->ValueFor(eCSSProperty_font_stretch);
      const nsCSSValue &sizeAdjust =
        *data->ValueFor(eCSSProperty_font_size_adjust);
      const nsCSSValue &featureSettings =
        *data->ValueFor(eCSSProperty_font_feature_settings);
      const nsCSSValue &languageOverride =
        *data->ValueFor(eCSSProperty_font_language_override);

      if (systemFont &&
          systemFont->GetUnit() != eCSSUnit_None &&
          systemFont->GetUnit() != eCSSUnit_Null) {
        if (style.GetUnit() != eCSSUnit_System_Font ||
            variant.GetUnit() != eCSSUnit_System_Font ||
            weight.GetUnit() != eCSSUnit_System_Font ||
            size.GetUnit() != eCSSUnit_System_Font ||
            lh.GetUnit() != eCSSUnit_System_Font ||
            family.GetUnit() != eCSSUnit_System_Font ||
            stretch.GetUnit() != eCSSUnit_System_Font ||
            sizeAdjust.GetUnit() != eCSSUnit_System_Font ||
            featureSettings.GetUnit() != eCSSUnit_System_Font ||
            languageOverride.GetUnit() != eCSSUnit_System_Font) {
          // This can't be represented as a shorthand.
          return;
        }
        systemFont->AppendToString(eCSSProperty__x_system_font, aValue);
      } else {
        // The font-stretch, font-size-adjust,
        // -moz-font-feature-settings, and -moz-font-language-override
        // properties are reset by this shorthand property to their
        // initial values, but can't be represented in its syntax.
        if (stretch.GetUnit() != eCSSUnit_Enumerated ||
            stretch.GetIntValue() != NS_STYLE_FONT_STRETCH_NORMAL ||
            sizeAdjust.GetUnit() != eCSSUnit_None ||
            featureSettings.GetUnit() != eCSSUnit_Normal ||
            languageOverride.GetUnit() != eCSSUnit_Normal) {
          return;
        }

        if (style.GetUnit() != eCSSUnit_Enumerated ||
            style.GetIntValue() != NS_FONT_STYLE_NORMAL) {
          style.AppendToString(eCSSProperty_font_style, aValue);
          aValue.Append(PRUnichar(' '));
        }
        if (variant.GetUnit() != eCSSUnit_Enumerated ||
            variant.GetIntValue() != NS_FONT_VARIANT_NORMAL) {
          variant.AppendToString(eCSSProperty_font_variant, aValue);
          aValue.Append(PRUnichar(' '));
        }
        if (weight.GetUnit() != eCSSUnit_Enumerated ||
            weight.GetIntValue() != NS_FONT_WEIGHT_NORMAL) {
          weight.AppendToString(eCSSProperty_font_weight, aValue);
          aValue.Append(PRUnichar(' '));
        }
        size.AppendToString(eCSSProperty_font_size, aValue);
        if (lh.GetUnit() != eCSSUnit_Normal) {
          aValue.Append(PRUnichar('/'));
          lh.AppendToString(eCSSProperty_line_height, aValue);
        }
        aValue.Append(PRUnichar(' '));
        family.AppendToString(eCSSProperty_font_family, aValue);
      }
      break;
    }
    case eCSSProperty_list_style:
      if (AppendValueToString(eCSSProperty_list_style_type, aValue))
        aValue.Append(PRUnichar(' '));
      if (AppendValueToString(eCSSProperty_list_style_position, aValue))
        aValue.Append(PRUnichar(' '));
      AppendValueToString(eCSSProperty_list_style_image, aValue);
      break;
    case eCSSProperty_overflow: {
      const nsCSSValue &xValue =
        *data->ValueFor(eCSSProperty_overflow_x);
      const nsCSSValue &yValue =
        *data->ValueFor(eCSSProperty_overflow_y);
      if (xValue == yValue)
        xValue.AppendToString(eCSSProperty_overflow_x, aValue);
      break;
    }
    case eCSSProperty_pause: {
      if (AppendValueToString(eCSSProperty_pause_before, aValue)) {
        aValue.Append(PRUnichar(' '));
        if (!AppendValueToString(eCSSProperty_pause_after, aValue))
          aValue.Truncate();
      }
      break;
    }
    case eCSSProperty_transition: {
      const nsCSSValue &transProp =
        *data->ValueFor(eCSSProperty_transition_property);
      const nsCSSValue &transDuration =
        *data->ValueFor(eCSSProperty_transition_duration);
      const nsCSSValue &transTiming =
        *data->ValueFor(eCSSProperty_transition_timing_function);
      const nsCSSValue &transDelay =
        *data->ValueFor(eCSSProperty_transition_delay);

      NS_ABORT_IF_FALSE(transDuration.GetUnit() == eCSSUnit_List ||
                        transDuration.GetUnit() == eCSSUnit_ListDep,
                        nsPrintfCString(32, "bad t-duration unit %d",
                                        transDuration.GetUnit()).get());
      NS_ABORT_IF_FALSE(transTiming.GetUnit() == eCSSUnit_List ||
                        transTiming.GetUnit() == eCSSUnit_ListDep,
                        nsPrintfCString(32, "bad t-timing unit %d",
                                        transTiming.GetUnit()).get());
      NS_ABORT_IF_FALSE(transDelay.GetUnit() == eCSSUnit_List ||
                        transDelay.GetUnit() == eCSSUnit_ListDep,
                        nsPrintfCString(32, "bad t-delay unit %d",
                                        transDelay.GetUnit()).get());

      const nsCSSValueList* dur = transDuration.GetListValue();
      const nsCSSValueList* tim = transTiming.GetListValue();
      const nsCSSValueList* del = transDelay.GetListValue();

      if (transProp.GetUnit() == eCSSUnit_None ||
          transProp.GetUnit() == eCSSUnit_All) {
        // If any of the other three lists has more than one element,
        // we can't use the shorthand.
        if (!dur->mNext && !tim->mNext && !del->mNext) {
          transProp.AppendToString(eCSSProperty_transition_property, aValue);
          aValue.Append(PRUnichar(' '));
          dur->mValue.AppendToString(eCSSProperty_transition_duration,aValue);
          aValue.Append(PRUnichar(' '));
          tim->mValue.AppendToString(eCSSProperty_transition_timing_function,
                                     aValue);
          aValue.Append(PRUnichar(' '));
          del->mValue.AppendToString(eCSSProperty_transition_delay, aValue);
          aValue.Append(PRUnichar(' '));
        } else {
          aValue.Truncate();
        }
      } else {
        NS_ABORT_IF_FALSE(transProp.GetUnit() == eCSSUnit_List ||
                          transProp.GetUnit() == eCSSUnit_ListDep,
                          nsPrintfCString(32, "bad t-prop unit %d",
                                          transProp.GetUnit()).get());
        const nsCSSValueList* pro = transProp.GetListValue();
        for (;;) {
          pro->mValue.AppendToString(eCSSProperty_transition_property,
                                        aValue);
          aValue.Append(PRUnichar(' '));
          dur->mValue.AppendToString(eCSSProperty_transition_duration,
                                        aValue);
          aValue.Append(PRUnichar(' '));
          tim->mValue.AppendToString(eCSSProperty_transition_timing_function,
                                        aValue);
          aValue.Append(PRUnichar(' '));
          del->mValue.AppendToString(eCSSProperty_transition_delay,
                                        aValue);
          pro = pro->mNext;
          dur = dur->mNext;
          tim = tim->mNext;
          del = del->mNext;
          if (!pro || !dur || !tim || !del) {
            break;
          }
          aValue.AppendLiteral(", ");
        }
        if (pro || dur || tim || del) {
          // Lists not all the same length, can't use shorthand.
          aValue.Truncate();
        }
      }
      break;
    }

    case eCSSProperty_marker: {
      const nsCSSValue &endValue =
        *data->ValueFor(eCSSProperty_marker_end);
      const nsCSSValue &midValue =
        *data->ValueFor(eCSSProperty_marker_mid);
      const nsCSSValue &startValue =
        *data->ValueFor(eCSSProperty_marker_start);
      if (endValue == midValue && midValue == startValue)
        AppendValueToString(eCSSProperty_marker_end, aValue);
      break;
    }
    default:
      NS_ABORT_IF_FALSE(false, "no other shorthands");
      break;
  }
}

PRBool
Declaration::GetValueIsImportant(const nsAString& aProperty) const
{
  nsCSSProperty propID = nsCSSProps::LookupProperty(aProperty);
  return GetValueIsImportant(propID);
}

PRBool
Declaration::GetValueIsImportant(nsCSSProperty aProperty) const
{
  if (!mImportantData)
    return PR_FALSE;

  // Calling ValueFor is inefficient, but we can assume '!important' is rare.

  if (!nsCSSProps::IsShorthand(aProperty)) {
    return mImportantData->ValueFor(aProperty) != nsnull;
  }

  CSSPROPS_FOR_SHORTHAND_SUBPROPERTIES(p, aProperty) {
    if (*p == eCSSProperty__x_system_font) {
      // The system_font subproperty doesn't count.
      continue;
    }
    if (!mImportantData->ValueFor(*p)) {
      return PR_FALSE;
    }
  }
  return PR_TRUE;
}

void
Declaration::AppendPropertyAndValueToString(nsCSSProperty aProperty,
                                            nsAutoString& aValue,
                                            nsAString& aResult) const
{
  NS_ABORT_IF_FALSE(0 <= aProperty && aProperty < eCSSProperty_COUNT,
                    "property enum out of range");
  NS_ABORT_IF_FALSE((aProperty < eCSSProperty_COUNT_no_shorthands) ==
                    aValue.IsEmpty(),
                    "aValue should be given for shorthands but not longhands");
  AppendASCIItoUTF16(nsCSSProps::GetStringValue(aProperty), aResult);
  aResult.AppendLiteral(": ");
  if (aValue.IsEmpty())
    AppendValueToString(aProperty, aResult);
  else
    aResult.Append(aValue);
  if (GetValueIsImportant(aProperty)) {
    aResult.AppendLiteral(" ! important");
  }
  aResult.AppendLiteral("; ");
}

void
Declaration::ToString(nsAString& aString) const
{
  // Someone cares about this declaration's contents, so don't let it
  // change from under them.  See e.g. bug 338679.
  SetImmutable();

  nsCSSCompressedDataBlock *systemFontData =
    GetValueIsImportant(eCSSProperty__x_system_font) ? mImportantData : mData;
  const nsCSSValue *systemFont =
    systemFontData->ValueFor(eCSSProperty__x_system_font);
  const PRBool haveSystemFont = systemFont &&
                                systemFont->GetUnit() != eCSSUnit_None &&
                                systemFont->GetUnit() != eCSSUnit_Null;
  PRBool didSystemFont = PR_FALSE;

  PRInt32 count = mOrder.Length();
  PRInt32 index;
  nsAutoTArray<nsCSSProperty, 16> shorthandsUsed;
  for (index = 0; index < count; index++) {
    nsCSSProperty property = OrderValueAt(index);
    PRBool doneProperty = PR_FALSE;

    // If we already used this property in a shorthand, skip it.
    if (shorthandsUsed.Length() > 0) {
      for (const nsCSSProperty *shorthands =
             nsCSSProps::ShorthandsContaining(property);
           *shorthands != eCSSProperty_UNKNOWN; ++shorthands) {
        if (shorthandsUsed.Contains(*shorthands)) {
          doneProperty = PR_TRUE;
          break;
        }
      }
      if (doneProperty)
        continue;
    }

    // Try to use this property in a shorthand.
    nsAutoString value;
    for (const nsCSSProperty *shorthands =
           nsCSSProps::ShorthandsContaining(property);
         *shorthands != eCSSProperty_UNKNOWN; ++shorthands) {
      // ShorthandsContaining returns the shorthands in order from those
      // that contain the most subproperties to those that contain the
      // least, which is exactly the order we want to test them.
      nsCSSProperty shorthand = *shorthands;

      // If GetValue gives us a non-empty string back, we can use that
      // value; otherwise it's not possible to use this shorthand.
      GetValue(shorthand, value);
      if (!value.IsEmpty()) {
        AppendPropertyAndValueToString(shorthand, value, aString);
        shorthandsUsed.AppendElement(shorthand);
        doneProperty = PR_TRUE;
        break;
      }

      NS_ABORT_IF_FALSE(shorthand != eCSSProperty_font ||
                        *(shorthands + 1) == eCSSProperty_UNKNOWN,
                        "font should always be the only containing shorthand");
      if (shorthand == eCSSProperty_font) {
        if (haveSystemFont && !didSystemFont) {
          // Output the shorthand font declaration that we will
          // partially override later.  But don't add it to
          // |shorthandsUsed|, since we will have to override it.
          systemFont->AppendToString(eCSSProperty__x_system_font, value);
          AppendPropertyAndValueToString(eCSSProperty_font, value, aString);
          value.Truncate();
          didSystemFont = PR_TRUE;
        }

        // That we output the system font is enough for this property if:
        //   (1) it's the hidden system font subproperty (which either
        //       means we output it or we don't have it), or
        //   (2) its value is the hidden system font value and it matches
        //       the hidden system font subproperty in importance, and
        //       we output the system font subproperty.
        const nsCSSValue *val = systemFontData->ValueFor(property);
        if (property == eCSSProperty__x_system_font ||
            (haveSystemFont && val && val->GetUnit() == eCSSUnit_System_Font)) {
          doneProperty = PR_TRUE;
        }
      }
    }
    if (doneProperty)
      continue;

    NS_ABORT_IF_FALSE(value.IsEmpty(), "value should be empty now");
    AppendPropertyAndValueToString(property, value, aString);
  }
  if (! aString.IsEmpty()) {
    // if the string is not empty, we have trailing whitespace we
    // should remove
    aString.Truncate(aString.Length() - 1);
  }
}

#ifdef DEBUG
void
Declaration::List(FILE* out, PRInt32 aIndent) const
{
  for (PRInt32 index = aIndent; --index >= 0; ) fputs("  ", out);

  fputs("{ ", out);
  nsAutoString s;
  ToString(s);
  fputs(NS_ConvertUTF16toUTF8(s).get(), out);
  fputs("}", out);
}
#endif

void
Declaration::GetNthProperty(PRUint32 aIndex, nsAString& aReturn) const
{
  aReturn.Truncate();
  if (aIndex < mOrder.Length()) {
    nsCSSProperty property = OrderValueAt(aIndex);
    if (0 <= property) {
      AppendASCIItoUTF16(nsCSSProps::GetStringValue(property), aReturn);
    }
  }
}

void
Declaration::InitializeEmpty()
{
  NS_ABORT_IF_FALSE(!mData && !mImportantData, "already initialized");
  mData = nsCSSCompressedDataBlock::CreateEmptyBlock();
}

Declaration*
Declaration::EnsureMutable()
{
  NS_ABORT_IF_FALSE(mData, "should only be called when not expanded");
  if (!IsMutable()) {
    return new Declaration(*this);
  } else {
    return this;
  }
}

} // namespace mozilla::css
} // namespace mozilla
