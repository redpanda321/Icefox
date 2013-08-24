/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Util.h"

#include "nsMathMLElement.h"
#include "nsDOMClassInfoID.h" // for eDOMClassInfo_MathElement_id.
#include "nsGkAtoms.h"
#include "nsCRT.h"
#include "nsRuleData.h"
#include "nsCSSValue.h"
#include "nsMappedAttributes.h"
#include "nsStyleConsts.h"
#include "nsIDocument.h"
#include "nsEventStates.h"
#include "nsIPresShell.h"
#include "nsPresContext.h"
#include "mozAutoDocUpdate.h"

using namespace mozilla;

//----------------------------------------------------------------------
// nsISupports methods:

DOMCI_NODE_DATA(MathMLElement, nsMathMLElement)

NS_INTERFACE_TABLE_HEAD(nsMathMLElement)
  NS_NODE_OFFSET_AND_INTERFACE_TABLE_BEGIN(nsMathMLElement)
    NS_INTERFACE_TABLE_ENTRY(nsMathMLElement, nsIDOMNode)
    NS_INTERFACE_TABLE_ENTRY(nsMathMLElement, nsIDOMElement)
    NS_INTERFACE_TABLE_ENTRY(nsMathMLElement, nsILink)
    NS_INTERFACE_TABLE_ENTRY(nsMathMLElement, Link)
  NS_OFFSET_AND_INTERFACE_TABLE_END
  NS_ELEMENT_INTERFACE_TABLE_TO_MAP_SEGUE
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(MathMLElement)
NS_ELEMENT_INTERFACE_MAP_END

NS_IMPL_ADDREF_INHERITED(nsMathMLElement, nsMathMLElementBase)
NS_IMPL_RELEASE_INHERITED(nsMathMLElement, nsMathMLElementBase)

nsresult
nsMathMLElement::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                            nsIContent* aBindingParent,
                            bool aCompileEventHandlers)
{
  static const char kMathMLStyleSheetURI[] = "resource://gre-resources/mathml.css";

  Link::ResetLinkState(false);

  nsresult rv = nsMathMLElementBase::BindToTree(aDocument, aParent,
                                                aBindingParent,
                                                aCompileEventHandlers);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aDocument) {
    aDocument->RegisterPendingLinkUpdate(this);
    
    if (!aDocument->GetMathMLEnabled()) {
      // Enable MathML and setup the style sheet during binding, not element
      // construction, because we could move a MathML element from the document
      // that created it to another document.
      aDocument->SetMathMLEnabled();
      aDocument->EnsureCatalogStyleSheet(kMathMLStyleSheetURI);

      // Rebuild style data for the presshell, because style system
      // optimizations may have taken place assuming MathML was disabled.
      // (See nsRuleNode::CheckSpecifiedProperties.)
      nsCOMPtr<nsIPresShell> shell = aDocument->GetShell();
      if (shell) {
        shell->GetPresContext()->PostRebuildAllStyleDataEvent(nsChangeHint(0));
      }
    }
  }

  return rv;
}

void
nsMathMLElement::UnbindFromTree(bool aDeep, bool aNullParent)
{
  // If this link is ever reinserted into a document, it might
  // be under a different xml:base, so forget the cached state now.
  Link::ResetLinkState(false);
  
  nsIDocument* doc = GetCurrentDoc();
  if (doc) {
    doc->UnregisterPendingLinkUpdate(this);
  }

  nsMathMLElementBase::UnbindFromTree(aDeep, aNullParent);
}

bool
nsMathMLElement::ParseAttribute(PRInt32 aNamespaceID,
                                nsIAtom* aAttribute,
                                const nsAString& aValue,
                                nsAttrValue& aResult)
{
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::color ||
        aAttribute == nsGkAtoms::mathcolor_ ||
        aAttribute == nsGkAtoms::background ||
        aAttribute == nsGkAtoms::mathbackground_) {
      return aResult.ParseColor(aValue);
    }
  }

  return nsMathMLElementBase::ParseAttribute(aNamespaceID, aAttribute,
                                             aValue, aResult);
}

static nsGenericElement::MappedAttributeEntry sMtableStyles[] = {
  { &nsGkAtoms::width },
  { nsnull }
};

static nsGenericElement::MappedAttributeEntry sTokenStyles[] = {
  { &nsGkAtoms::mathsize_ },
  { &nsGkAtoms::fontsize_ },
  { &nsGkAtoms::color },
  { &nsGkAtoms::fontfamily_ },
  { nsnull }
};

static nsGenericElement::MappedAttributeEntry sEnvironmentStyles[] = {
  { &nsGkAtoms::scriptlevel_ },
  { &nsGkAtoms::scriptminsize_ },
  { &nsGkAtoms::scriptsizemultiplier_ },
  { &nsGkAtoms::background },
  { nsnull }
};

static nsGenericElement::MappedAttributeEntry sCommonPresStyles[] = {
  { &nsGkAtoms::mathcolor_ },
  { &nsGkAtoms::mathbackground_ },
  { nsnull }
};

bool
nsMathMLElement::IsAttributeMapped(const nsIAtom* aAttribute) const
{
  static const MappedAttributeEntry* const mtableMap[] = {
    sMtableStyles,
    sCommonPresStyles
  };
  static const MappedAttributeEntry* const tokenMap[] = {
    sTokenStyles,
    sCommonPresStyles
  };
  static const MappedAttributeEntry* const mstyleMap[] = {
    sTokenStyles,
    sEnvironmentStyles,
    sCommonPresStyles
  };
  static const MappedAttributeEntry* const commonPresMap[] = {
    sCommonPresStyles
  };
  
  // We don't support mglyph (yet).
  nsIAtom* tag = Tag();
  if (tag == nsGkAtoms::ms_ || tag == nsGkAtoms::mi_ ||
      tag == nsGkAtoms::mn_ || tag == nsGkAtoms::mo_ ||
      tag == nsGkAtoms::mtext_ || tag == nsGkAtoms::mspace_)
    return FindAttributeDependence(aAttribute, tokenMap);
  if (tag == nsGkAtoms::mstyle_ ||
      tag == nsGkAtoms::math)
    return FindAttributeDependence(aAttribute, mstyleMap);

  if (tag == nsGkAtoms::mtable_)
    return FindAttributeDependence(aAttribute, mtableMap);

  if (tag == nsGkAtoms::maction_ ||
      tag == nsGkAtoms::maligngroup_ ||
      tag == nsGkAtoms::malignmark_ ||
      tag == nsGkAtoms::menclose_ ||
      tag == nsGkAtoms::merror_ ||
      tag == nsGkAtoms::mfenced_ ||
      tag == nsGkAtoms::mfrac_ ||
      tag == nsGkAtoms::mover_ ||
      tag == nsGkAtoms::mpadded_ ||
      tag == nsGkAtoms::mphantom_ ||
      tag == nsGkAtoms::mprescripts_ ||
      tag == nsGkAtoms::mroot_ ||
      tag == nsGkAtoms::mrow_ ||
      tag == nsGkAtoms::msqrt_ ||
      tag == nsGkAtoms::msub_ ||
      tag == nsGkAtoms::msubsup_ ||
      tag == nsGkAtoms::msup_ ||
      tag == nsGkAtoms::mtd_ ||
      tag == nsGkAtoms::mtr_ ||
      tag == nsGkAtoms::munder_ ||
      tag == nsGkAtoms::munderover_ ||
      tag == nsGkAtoms::none) {
    return FindAttributeDependence(aAttribute, commonPresMap);
  }

  return false;
}

nsMapRuleToAttributesFunc
nsMathMLElement::GetAttributeMappingFunction() const
{
  // It doesn't really matter what our tag is here, because only attributes
  // that satisfy IsAttributeMapped will be stored in the mapped attributes
  // list and available to the mapping function
  return &MapMathMLAttributesInto;
}

/* static */ bool
nsMathMLElement::ParseNamedSpaceValue(const nsString& aString,
                                      nsCSSValue&     aCSSValue,
                                      PRUint32        aFlags)
{
   PRInt32 i = 0;
   // See if it is one of the 'namedspace' (ranging -7/18em, -6/18, ... 7/18em)
   if (aString.EqualsLiteral("veryverythinmathspace")) {
     i = 1;
   } else if (aString.EqualsLiteral("verythinmathspace")) {
     i = 2;
   } else if (aString.EqualsLiteral("thinmathspace")) {
     i = 3;
   } else if (aString.EqualsLiteral("mediummathspace")) {
     i = 4;
   } else if (aString.EqualsLiteral("thickmathspace")) {
     i = 5;
   } else if (aString.EqualsLiteral("verythickmathspace")) {
     i = 6;
   } else if (aString.EqualsLiteral("veryverythickmathspace")) {
     i = 7;
   } else if (aFlags & PARSE_ALLOW_NEGATIVE) {
     if (aString.EqualsLiteral("negativeveryverythinmathspace")) {
       i = -1;
     } else if (aString.EqualsLiteral("negativeverythinmathspace")) {
       i = -2;
     } else if (aString.EqualsLiteral("negativethinmathspace")) {
       i = -3;
     } else if (aString.EqualsLiteral("negativemediummathspace")) {
       i = -4;
     } else if (aString.EqualsLiteral("negativethickmathspace")) {
       i = -5;
     } else if (aString.EqualsLiteral("negativeverythickmathspace")) {
       i = -6;
     } else if (aString.EqualsLiteral("negativeveryverythickmathspace")) {
       i = -7;
     }
   }
   if (0 != i) { 
     aCSSValue.SetFloatValue(float(i)/float(18), eCSSUnit_EM);
     return true;
   }
   
   return false;
}
 
// The REC says:
//
// "Most presentation elements have attributes that accept values representing
// lengths to be used for size, spacing or similar properties. The syntax of a
// length is specified as
//
// number | number unit | namedspace
//
// There should be no space between the number and the unit of a length."
// 
// "A trailing '%' represents a percent of the default value. The default
// value, or how it is obtained, is listed in the table of attributes for each
// element. [...] A number without a unit is intepreted as a multiple of the
// default value."
//
// "The possible units in MathML are:
//  
// Unit Description
// em   an em (font-relative unit traditionally used for horizontal lengths)
// ex   an ex (font-relative unit traditionally used for vertical lengths)
// px   pixels, or size of a pixel in the current display
// in   inches (1 inch = 2.54 centimeters)
// cm   centimeters
// mm   millimeters
// pt   points (1 point = 1/72 inch)
// pc   picas (1 pica = 12 points)
// %    percentage of default value"
//
// The numbers are defined that way:
// - unsigned-number: "a string of decimal digits with up to one decimal point
//   (U+002E), representing a non-negative terminating decimal number (a type of
//   rational number)"
// - number: "an optional prefix of '-' (U+002D), followed by an unsigned
//   number, representing a terminating decimal number (a type of rational
//   number)"
//
/* static */ bool
nsMathMLElement::ParseNumericValue(const nsString& aString,
                                   nsCSSValue&     aCSSValue,
                                   PRUint32        aFlags)
{
  nsAutoString str(aString);
  str.CompressWhitespace(); // aString is const in this code...

  PRInt32 stringLength = str.Length();
  if (!stringLength)
    return false;

  if (ParseNamedSpaceValue(aString, aCSSValue, aFlags)) {
    return true;
  }

  nsAutoString number, unit;

  // see if the negative sign is there
  PRInt32 i = 0;
  PRUnichar c = str[0];
  if (c == '-') {
    number.Append(c);
    i++;
  }

  // Gather up characters that make up the number
  bool gotDot = false;
  for ( ; i < stringLength; i++) {
    c = str[i];
    if (gotDot && c == '.')
      return false;  // two dots encountered
    else if (c == '.')
      gotDot = true;
    else if (!nsCRT::IsAsciiDigit(c)) {
      str.Right(unit, stringLength - i);
      // some authors leave blanks before the unit, but that shouldn't
      // be allowed, so don't CompressWhitespace on 'unit'.
      break;
    }
    number.Append(c);
  }

  // Convert number to floating point
  PRInt32 errorCode;
  float floatValue = number.ToFloat(&errorCode);
  if (NS_FAILED(errorCode))
    return false;
  if (floatValue < 0 && !(aFlags & PARSE_ALLOW_NEGATIVE))
    return false;

  nsCSSUnit cssUnit;
  if (unit.IsEmpty()) {
    if (aFlags & PARSE_ALLOW_UNITLESS) {
      // no explicit unit, this is a number that will act as a multiplier
      cssUnit = eCSSUnit_Number;
    } else {
      // We are supposed to have a unit, but there isn't one.
      // If the value is 0 we can just call it "pixels" otherwise
      // this is illegal.
      if (floatValue != 0.0)
        return false;
      cssUnit = eCSSUnit_Pixel;
    }
  }
  else if (unit.EqualsLiteral("%")) {
    aCSSValue.SetPercentValue(floatValue / 100.0f);
    return true;
  }
  else if (unit.EqualsLiteral("em")) cssUnit = eCSSUnit_EM;
  else if (unit.EqualsLiteral("ex")) cssUnit = eCSSUnit_XHeight;
  else if (unit.EqualsLiteral("px")) cssUnit = eCSSUnit_Pixel;
  else if (unit.EqualsLiteral("in")) cssUnit = eCSSUnit_Inch;
  else if (unit.EqualsLiteral("cm")) cssUnit = eCSSUnit_Centimeter;
  else if (unit.EqualsLiteral("mm")) cssUnit = eCSSUnit_Millimeter;
  else if (unit.EqualsLiteral("pt")) cssUnit = eCSSUnit_Point;
  else if (unit.EqualsLiteral("pc")) cssUnit = eCSSUnit_Pica;
  else // unexpected unit
    return false;

  aCSSValue.SetFloatValue(floatValue, cssUnit);
  return true;
}

void
nsMathMLElement::MapMathMLAttributesInto(const nsMappedAttributes* aAttributes,
                                         nsRuleData* aData)
{
  if (aData->mSIDs & NS_STYLE_INHERIT_BIT(Font)) {
    // scriptsizemultiplier
    //
    // "Specifies the multiplier to be used to adjust font size due to changes
    // in scriptlevel.
    //
    // values: number
    // default: 0.71
    //
    const nsAttrValue* value =
      aAttributes->GetAttr(nsGkAtoms::scriptsizemultiplier_);
    nsCSSValue* scriptSizeMultiplier =
      aData->ValueForScriptSizeMultiplier();
    if (value && value->Type() == nsAttrValue::eString &&
        scriptSizeMultiplier->GetUnit() == eCSSUnit_Null) {
      nsAutoString str(value->GetStringValue());
      str.CompressWhitespace();
      // MathML numbers can't have leading '+'
      if (str.Length() > 0 && str.CharAt(0) != '+') {
        PRInt32 errorCode;
        float floatValue = str.ToFloat(&errorCode);
        // Negative scriptsizemultipliers are not parsed
        if (NS_SUCCEEDED(errorCode) && floatValue >= 0.0f) {
          scriptSizeMultiplier->SetFloatValue(floatValue, eCSSUnit_Number);
        }
      }
    }

    // scriptminsize
    //
    // "Specifies the minimum font size allowed due to changes in scriptlevel.
    // Note that this does not limit the font size due to changes to mathsize."
    //
    // values: length
    // default: 8pt
    //
    // We don't allow negative values.
    // XXXfredw Should we allow unitless values? (bug 411227)
    // XXXfredw Does a relative unit give a multiple of the default value?
    //
    value = aAttributes->GetAttr(nsGkAtoms::scriptminsize_);
    nsCSSValue* scriptMinSize = aData->ValueForScriptMinSize();
    if (value && value->Type() == nsAttrValue::eString &&
        scriptMinSize->GetUnit() == eCSSUnit_Null) {
      ParseNumericValue(value->GetStringValue(), *scriptMinSize, 0);
    }

    // scriptlevel
    // 
    // "Changes the scriptlevel in effect for the children. When the value is
    // given without a sign, it sets scriptlevel to the specified value; when a
    // sign is given, it increments ("+") or decrements ("-") the current
    // value. (Note that large decrements can result in negative values of
    // scriptlevel, but these values are considered legal.)"
    //
    // values: ( "+" | "-" )? unsigned-integer
    // default: inherited
    //
    value = aAttributes->GetAttr(nsGkAtoms::scriptlevel_);
    nsCSSValue* scriptLevel = aData->ValueForScriptLevel();
    if (value && value->Type() == nsAttrValue::eString &&
        scriptLevel->GetUnit() == eCSSUnit_Null) {
      nsAutoString str(value->GetStringValue());
      str.CompressWhitespace();
      if (str.Length() > 0) {
        PRInt32 errorCode;
        PRInt32 intValue = str.ToInteger(&errorCode);
        if (NS_SUCCEEDED(errorCode)) {
          // This is kind of cheesy ... if the scriptlevel has a sign,
          // then it's a relative value and we store the nsCSSValue as an
          // Integer to indicate that. Otherwise we store it as a Number
          // to indicate that the scriptlevel is absolute.
          PRUnichar ch = str.CharAt(0);
          if (ch == '+' || ch == '-') {
            scriptLevel->SetIntValue(intValue, eCSSUnit_Integer);
          } else {
            scriptLevel->SetFloatValue(intValue, eCSSUnit_Number);
          }
        }
      }
    }

    // mathsize
    //
    // "Specifies the size to display the token content. The values 'small' and
    // 'big' choose a size smaller or larger than the current font size, but
    // leave the exact proportions unspecified; 'normal' is allowed for
    // completeness, but since it is equivalent to '100%' or '1em', it has no
    // effect."
    //
    // values: "small" | "normal" | "big" | length
    // default: inherited
    //
    // fontsize
    //
    // "Specified the size for the token. Deprecated in favor of mathsize."
    //
    // values: length
    // default: inherited
    //
    // In both cases, we don't allow negative values.
    // XXXfredw Should we allow unitless values? (bug 411227)
    // XXXfredw Does a relative unit give a multiple of the default value?
    //  
    bool parseSizeKeywords = true;
    value = aAttributes->GetAttr(nsGkAtoms::mathsize_);
    if (!value) {
      parseSizeKeywords = false;
      value = aAttributes->GetAttr(nsGkAtoms::fontsize_);
    }
    nsCSSValue* fontSize = aData->ValueForFontSize();
    if (value && value->Type() == nsAttrValue::eString &&
        fontSize->GetUnit() == eCSSUnit_Null) {
      nsAutoString str(value->GetStringValue());
      if (!ParseNumericValue(str, *fontSize, 0) &&
          parseSizeKeywords) {
        static const char sizes[3][7] = { "small", "normal", "big" };
        static const PRInt32 values[NS_ARRAY_LENGTH(sizes)] = {
          NS_STYLE_FONT_SIZE_SMALL, NS_STYLE_FONT_SIZE_MEDIUM,
          NS_STYLE_FONT_SIZE_LARGE
        };
        str.CompressWhitespace();
        for (PRUint32 i = 0; i < ArrayLength(sizes); ++i) {
          if (str.EqualsASCII(sizes[i])) {
            fontSize->SetIntValue(values[i], eCSSUnit_Enumerated);
            break;
          }
        }
      }
    }

    // fontfamily
    //
    // "Should be the name of a font that may be available to a MathML renderer,
    // or a CSS font specification; See Section 6.5 Using CSS with MathML and
    // CSS for more information. Deprecated in favor of mathvariant."
    //
    // values: string
    // 
    value = aAttributes->GetAttr(nsGkAtoms::fontfamily_);
    nsCSSValue* fontFamily = aData->ValueForFontFamily();
    if (value && value->Type() == nsAttrValue::eString &&
        fontFamily->GetUnit() == eCSSUnit_Null) {
      fontFamily->SetStringValue(value->GetStringValue(), eCSSUnit_Families);
    }
  }

  // mathbackground
  // 
  // "Specifies the background color to be used to fill in the bounding box of
  // the element and its children. The default, 'transparent', lets the
  // background color, if any, used in the current rendering context to show
  // through."
  // 
  // values: color | "transparent" 
  // default: "transparent"
  //
  // background
  //
  // "Specified the background color to be used to fill in the bounding box of
  // the element and its children. Deprecated in favor of mathbackground."
  //
  // values: color | "transparent"
  // default: "transparent"
  //
  if (aData->mSIDs & NS_STYLE_INHERIT_BIT(Background)) {
    const nsAttrValue* value =
      aAttributes->GetAttr(nsGkAtoms::mathbackground_);
    if (!value) {
      value = aAttributes->GetAttr(nsGkAtoms::background);
    }
    nsCSSValue* backgroundColor = aData->ValueForBackgroundColor();
    if (value && backgroundColor->GetUnit() == eCSSUnit_Null) {
      nscolor color;
      if (value->GetColorValue(color)) {
        backgroundColor->SetColorValue(color);
      }
    }
  }

  // mathcolor
  //
  // "Specifies the foreground color to use when drawing the components of this
  // element, such as the content for token elements or any lines, surds, or
  // other decorations. It also establishes the default mathcolor used for
  // child elements when used on a layout element."
  //
  // values: color
  // default: inherited
  //
  // color
  // 
  // "Specified the color for the token. Deprecated in favor of mathcolor." 
  //
  // values: color
  // default: inherited
  //
  if (aData->mSIDs & NS_STYLE_INHERIT_BIT(Color)) {
    const nsAttrValue* value = aAttributes->GetAttr(nsGkAtoms::mathcolor_);
    if (!value) {
      value = aAttributes->GetAttr(nsGkAtoms::color);
    }
    nscolor color;
    nsCSSValue* colorValue = aData->ValueForColor();
    if (value && value->GetColorValue(color) &&
        colorValue->GetUnit() == eCSSUnit_Null) {
      colorValue->SetColorValue(color);
    }
  }

  if (aData->mSIDs & NS_STYLE_INHERIT_BIT(Position)) {
    // width: value
    nsCSSValue* width = aData->ValueForWidth();
    if (width->GetUnit() == eCSSUnit_Null) {
      const nsAttrValue* value = aAttributes->GetAttr(nsGkAtoms::width);
      // This does not handle auto and unitless values
      if (value && value->Type() == nsAttrValue::eString) {
        ParseNumericValue(value->GetStringValue(), *width, 0);
      }
    }
  }

}

nsresult
nsMathMLElement::PreHandleEvent(nsEventChainPreVisitor& aVisitor)
{
  nsresult rv = nsGenericElement::PreHandleEvent(aVisitor);
  NS_ENSURE_SUCCESS(rv, rv);

  return PreHandleEventForLinks(aVisitor);
}

nsresult
nsMathMLElement::PostHandleEvent(nsEventChainPostVisitor& aVisitor)
{
  return PostHandleEventForLinks(aVisitor);
}

NS_IMPL_ELEMENT_CLONE(nsMathMLElement)

nsEventStates
nsMathMLElement::IntrinsicState() const
{
  return Link::LinkState() | nsMathMLElementBase::IntrinsicState() |
    (mIncrementScriptLevel ? NS_EVENT_STATE_INCREMENT_SCRIPT_LEVEL : nsEventStates());
}

bool
nsMathMLElement::IsNodeOfType(PRUint32 aFlags) const
{
  return !(aFlags & ~eCONTENT);
}

void
nsMathMLElement::SetIncrementScriptLevel(bool aIncrementScriptLevel,
                                         bool aNotify)
{
  if (aIncrementScriptLevel == mIncrementScriptLevel)
    return;
  mIncrementScriptLevel = aIncrementScriptLevel;

  NS_ASSERTION(aNotify, "We always notify!");

  UpdateState(true);
}

bool
nsMathMLElement::IsFocusable(PRInt32 *aTabIndex, bool aWithMouse)
{
  nsCOMPtr<nsIURI> uri;
  if (IsLink(getter_AddRefs(uri))) {
    if (aTabIndex) {
      *aTabIndex = ((sTabFocusModel & eTabFocus_linksMask) == 0 ? -1 : 0);
    }
    return true;
  }

  if (aTabIndex) {
    *aTabIndex = -1;
  }

  return false;
}

bool
nsMathMLElement::IsLink(nsIURI** aURI) const
{
  // http://www.w3.org/TR/2010/REC-MathML3-20101021/chapter6.html#interf.link
  // The REC says that the following elements should not be linking elements:
  nsIAtom* tag = Tag();
  if (tag == nsGkAtoms::mprescripts_ ||
      tag == nsGkAtoms::none         ||
      tag == nsGkAtoms::malignmark_  ||
      tag == nsGkAtoms::maligngroup_) {
    *aURI = nsnull;
    return false;
  }

  bool hasHref = false;
  const nsAttrValue* href = mAttrsAndChildren.GetAttr(nsGkAtoms::href,
                                                      kNameSpaceID_None);
  if (href) {
    // MathML href
    // The REC says: "When user agents encounter MathML elements with both href
    // and xlink:href attributes, the href attribute should take precedence."
    hasHref = true;
  } else {
    // To be a clickable XLink for styling and interaction purposes, we require:
    //
    //   xlink:href    - must be set
    //   xlink:type    - must be unset or set to "" or set to "simple"
    //   xlink:show    - must be unset or set to "", "new" or "replace"
    //   xlink:actuate - must be unset or set to "" or "onRequest"
    //
    // For any other values, we're either not a *clickable* XLink, or the end
    // result is poorly specified. Either way, we return false.
    
    static nsIContent::AttrValuesArray sTypeVals[] =
      { &nsGkAtoms::_empty, &nsGkAtoms::simple, nsnull };
    
    static nsIContent::AttrValuesArray sShowVals[] =
      { &nsGkAtoms::_empty, &nsGkAtoms::_new, &nsGkAtoms::replace, nsnull };
    
    static nsIContent::AttrValuesArray sActuateVals[] =
      { &nsGkAtoms::_empty, &nsGkAtoms::onRequest, nsnull };
    
    // Optimization: check for href first for early return
    href = mAttrsAndChildren.GetAttr(nsGkAtoms::href,
                                     kNameSpaceID_XLink);
    if (href &&
        FindAttrValueIn(kNameSpaceID_XLink, nsGkAtoms::type,
                        sTypeVals, eCaseMatters) !=
        nsIContent::ATTR_VALUE_NO_MATCH &&
        FindAttrValueIn(kNameSpaceID_XLink, nsGkAtoms::show,
                        sShowVals, eCaseMatters) !=
        nsIContent::ATTR_VALUE_NO_MATCH &&
        FindAttrValueIn(kNameSpaceID_XLink, nsGkAtoms::actuate,
                        sActuateVals, eCaseMatters) !=
        nsIContent::ATTR_VALUE_NO_MATCH) {
      hasHref = true;
    }
  }

  if (hasHref) {
    nsCOMPtr<nsIURI> baseURI = GetBaseURI();
    // Get absolute URI
    nsAutoString hrefStr;
    href->ToString(hrefStr); 
    nsContentUtils::NewURIWithDocumentCharset(aURI, hrefStr,
                                              OwnerDoc(), baseURI);
    // must promise out param is non-null if we return true
    return !!*aURI;
  }

  *aURI = nsnull;
  return false;
}

void
nsMathMLElement::GetLinkTarget(nsAString& aTarget)
{
  const nsAttrValue* target = mAttrsAndChildren.GetAttr(nsGkAtoms::target,
                                                        kNameSpaceID_XLink);
  if (target) {
    target->ToString(aTarget);
  }

  if (aTarget.IsEmpty()) {

    static nsIContent::AttrValuesArray sShowVals[] =
      { &nsGkAtoms::_new, &nsGkAtoms::replace, nsnull };
    
    switch (FindAttrValueIn(kNameSpaceID_XLink, nsGkAtoms::show,
                            sShowVals, eCaseMatters)) {
    case 0:
      aTarget.AssignLiteral("_blank");
      return;
    case 1:
      return;
    }
    OwnerDoc()->GetBaseTarget(aTarget);
  }
}

nsLinkState
nsMathMLElement::GetLinkState() const
{
  return Link::GetLinkState();
}

already_AddRefed<nsIURI>
nsMathMLElement::GetHrefURI() const
{
  nsCOMPtr<nsIURI> hrefURI;
  return IsLink(getter_AddRefs(hrefURI)) ? hrefURI.forget() : nsnull;
}

nsresult
nsMathMLElement::SetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                         nsIAtom* aPrefix, const nsAString& aValue,
                         bool aNotify)
{
  nsresult rv = nsMathMLElementBase::SetAttr(aNameSpaceID, aName, aPrefix,
                                           aValue, aNotify);

  // The ordering of the parent class's SetAttr call and Link::ResetLinkState
  // is important here!  The attribute is not set until SetAttr returns, and
  // we will need the updated attribute value because notifying the document
  // that content states have changed will call IntrinsicState, which will try
  // to get updated information about the visitedness from Link.
  if (aName == nsGkAtoms::href &&
      (aNameSpaceID == kNameSpaceID_None ||
       aNameSpaceID == kNameSpaceID_XLink)) {
    Link::ResetLinkState(!!aNotify);
  }

  return rv;
}

nsresult
nsMathMLElement::UnsetAttr(PRInt32 aNameSpaceID, nsIAtom* aAttr,
                           bool aNotify)
{
  nsresult rv = nsMathMLElementBase::UnsetAttr(aNameSpaceID, aAttr, aNotify);

  // The ordering of the parent class's UnsetAttr call and Link::ResetLinkState
  // is important here!  The attribute is not unset until UnsetAttr returns, and
  // we will need the updated attribute value because notifying the document
  // that content states have changed will call IntrinsicState, which will try
  // to get updated information about the visitedness from Link.
  if (aAttr == nsGkAtoms::href &&
      (aNameSpaceID == kNameSpaceID_None ||
       aNameSpaceID == kNameSpaceID_XLink)) {
    Link::ResetLinkState(!!aNotify);
  }

  return rv;
}
