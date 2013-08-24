/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "nsSVGGeometryFrame.h"

// Keep others in (case-insensitive) order:
#include "gfxContext.h"
#include "nsPresContext.h"
#include "nsSVGEffects.h"
#include "nsSVGPaintServerFrame.h"
#include "nsSVGPathElement.h"
#include "nsSVGUtils.h"

NS_IMPL_FRAMEARENA_HELPERS(nsSVGGeometryFrame)

//----------------------------------------------------------------------
// nsIFrame methods

NS_IMETHODIMP
nsSVGGeometryFrame::Init(nsIContent* aContent,
                         nsIFrame* aParent,
                         nsIFrame* aPrevInFlow)
{
  AddStateBits(aParent->GetStateBits() &
               (NS_STATE_SVG_NONDISPLAY_CHILD | NS_STATE_SVG_CLIPPATH_CHILD));
  nsresult rv = nsSVGGeometryFrameBase::Init(aContent, aParent, aPrevInFlow);
  return rv;
}

//----------------------------------------------------------------------

nsSVGPaintServerFrame *
nsSVGGeometryFrame::GetPaintServer(const nsStyleSVGPaint *aPaint,
                                   const FramePropertyDescriptor *aType)
{
  if (aPaint->mType != eStyleSVGPaintType_Server)
    return nsnull;

  nsIFrame *frame = mContent->IsNodeOfType(nsINode::eTEXT) ?
                      GetParent() : this;
  nsSVGPaintingProperty *property =
    nsSVGEffects::GetPaintingProperty(aPaint->mPaint.mPaintServer, frame, aType);
  if (!property)
    return nsnull;
  nsIFrame *result = property->GetReferencedFrame();
  if (!result)
    return nsnull;

  nsIAtom *type = result->GetType();
  if (type != nsGkAtoms::svgLinearGradientFrame &&
      type != nsGkAtoms::svgRadialGradientFrame &&
      type != nsGkAtoms::svgPatternFrame)
    return nsnull;

  return static_cast<nsSVGPaintServerFrame*>(result);
}

float
nsSVGGeometryFrame::GetStrokeWidth()
{
  nsSVGElement *ctx = static_cast<nsSVGElement*>
                                 (mContent->IsNodeOfType(nsINode::eTEXT) ?
                                     mContent->GetParent() : mContent);

  return
    nsSVGUtils::CoordToFloat(PresContext(),
                             ctx,
                             GetStyleSVG()->mStrokeWidth);
}

bool
nsSVGGeometryFrame::GetStrokeDashData(FallibleTArray<gfxFloat>& dashes,
                                      gfxFloat *dashOffset)
{
  PRUint32 count = GetStyleSVG()->mStrokeDasharrayLength;
  if (!count || !dashes.SetLength(count)) {
    return false;
  }

  gfxFloat pathScale = 1.0;

  if (mContent->Tag() == nsGkAtoms::path) {
    pathScale = static_cast<nsSVGPathElement*>(mContent)->
                  GetPathLengthScale(nsSVGPathElement::eForStroking);
    if (pathScale <= 0) {
      return false;
    }
  }
  
  nsSVGElement *ctx = static_cast<nsSVGElement*>
                                 (mContent->IsNodeOfType(nsINode::eTEXT) ?
                                     mContent->GetParent() : mContent);

  const nsStyleCoord *dasharray = GetStyleSVG()->mStrokeDasharray;
  nsPresContext *presContext = PresContext();
  gfxFloat totalLength = 0.0;

  for (PRUint32 i = 0; i < count; i++) {
    dashes[i] =
      nsSVGUtils::CoordToFloat(presContext,
                               ctx,
                               dasharray[i]) * pathScale;
    if (dashes[i] < 0.0) {
      return false;
    }
    totalLength += dashes[i];
  }

  *dashOffset = nsSVGUtils::CoordToFloat(presContext,
                                         ctx,
                                         GetStyleSVG()->mStrokeDashoffset);

  return (totalLength > 0.0);
}

PRUint16
nsSVGGeometryFrame::GetClipRule()
{
  return GetStyleSVG()->mClipRule;
}

static void
SetupCairoColor(gfxContext *aContext, nscolor aRGB, float aOpacity)
{
  aContext->SetColor(gfxRGBA(NS_GET_R(aRGB)/255.0,
                             NS_GET_G(aRGB)/255.0,
                             NS_GET_B(aRGB)/255.0,
                             NS_GET_A(aRGB)/255.0 * aOpacity));
}

static void
SetupFallbackOrPaintColor(gfxContext *aContext, nsStyleContext *aStyleContext,
                          nsStyleSVGPaint nsStyleSVG::*aFillOrStroke,
                          float aOpacity)
{
  nscolor color;
  nsSVGUtils::GetFallbackOrPaintColor(aContext, aStyleContext, aFillOrStroke,
                                      &aOpacity, &color);

  SetupCairoColor(aContext, color, aOpacity);
}

float
nsSVGGeometryFrame::MaybeOptimizeOpacity(float aFillOrStrokeOpacity)
{
  float opacity = GetStyleDisplay()->mOpacity;
  if (opacity < 1 && nsSVGUtils::CanOptimizeOpacity(this)) {
    return aFillOrStrokeOpacity * opacity;
  }
  return aFillOrStrokeOpacity;
}

bool
nsSVGGeometryFrame::SetupCairoFill(gfxContext *aContext)
{
  const nsStyleSVG* style = GetStyleSVG();
  if (style->mFill.mType == eStyleSVGPaintType_None)
    return false;

  if (style->mFillRule == NS_STYLE_FILL_RULE_EVENODD)
    aContext->SetFillRule(gfxContext::FILL_RULE_EVEN_ODD);
  else
    aContext->SetFillRule(gfxContext::FILL_RULE_WINDING);

  float opacity = MaybeOptimizeOpacity(style->mFillOpacity);

  nsSVGPaintServerFrame *ps =
    GetPaintServer(&style->mFill, nsSVGEffects::FillProperty());
  if (ps && ps->SetupPaintServer(aContext, this, &nsStyleSVG::mFill, opacity))
    return true;

  // On failure, use the fallback colour in case we have an
  // objectBoundingBox where the width or height of the object is zero.
  // See http://www.w3.org/TR/SVG11/coords.html#ObjectBoundingBox
  SetupFallbackOrPaintColor(aContext, GetStyleContext(),
                            &nsStyleSVG::mFill, opacity);

  return true;
}

bool
nsSVGGeometryFrame::HasStroke()
{
  const nsStyleSVG *style = GetStyleSVG();
  return style->mStroke.mType != eStyleSVGPaintType_None &&
         style->mStrokeOpacity > 0 &&
         GetStrokeWidth() > 0;
}

void
nsSVGGeometryFrame::SetupCairoStrokeGeometry(gfxContext *aContext)
{
  float width = GetStrokeWidth();
  if (width <= 0)
    return;
  aContext->SetLineWidth(width);

  // Apply any stroke-specific transform
  aContext->Multiply(nsSVGUtils::GetStrokeTransform(this));

  const nsStyleSVG* style = GetStyleSVG();
  
  switch (style->mStrokeLinecap) {
  case NS_STYLE_STROKE_LINECAP_BUTT:
    aContext->SetLineCap(gfxContext::LINE_CAP_BUTT);
    break;
  case NS_STYLE_STROKE_LINECAP_ROUND:
    aContext->SetLineCap(gfxContext::LINE_CAP_ROUND);
    break;
  case NS_STYLE_STROKE_LINECAP_SQUARE:
    aContext->SetLineCap(gfxContext::LINE_CAP_SQUARE);
    break;
  }

  aContext->SetMiterLimit(style->mStrokeMiterlimit);

  switch (style->mStrokeLinejoin) {
  case NS_STYLE_STROKE_LINEJOIN_MITER:
    aContext->SetLineJoin(gfxContext::LINE_JOIN_MITER);
    break;
  case NS_STYLE_STROKE_LINEJOIN_ROUND:
    aContext->SetLineJoin(gfxContext::LINE_JOIN_ROUND);
    break;
  case NS_STYLE_STROKE_LINEJOIN_BEVEL:
    aContext->SetLineJoin(gfxContext::LINE_JOIN_BEVEL);
    break;
  }
}

void
nsSVGGeometryFrame::SetupCairoStrokeHitGeometry(gfxContext *aContext)
{
  SetupCairoStrokeGeometry(aContext);

  AutoFallibleTArray<gfxFloat, 10> dashes;
  gfxFloat dashOffset;
  if (GetStrokeDashData(dashes, &dashOffset)) {
    aContext->SetDash(dashes.Elements(), dashes.Length(), dashOffset);
  }
}

bool
nsSVGGeometryFrame::SetupCairoStroke(gfxContext *aContext)
{
  if (!HasStroke()) {
    return false;
  }
  SetupCairoStrokeHitGeometry(aContext);

  const nsStyleSVG* style = GetStyleSVG();
  float opacity = MaybeOptimizeOpacity(style->mStrokeOpacity);

  nsSVGPaintServerFrame *ps =
    GetPaintServer(&style->mStroke, nsSVGEffects::StrokeProperty());
  if (ps && ps->SetupPaintServer(aContext, this, &nsStyleSVG::mStroke, opacity))
    return true;

  // On failure, use the fallback colour in case we have an
  // objectBoundingBox where the width or height of the object is zero.
  // See http://www.w3.org/TR/SVG11/coords.html#ObjectBoundingBox
  SetupFallbackOrPaintColor(aContext, GetStyleContext(),
                            &nsStyleSVG::mStroke, opacity);

  return true;
}

PRUint16
nsSVGGeometryFrame::GetHitTestFlags()
{
  PRUint16 flags = 0;

  switch(GetStyleVisibility()->mPointerEvents) {
  case NS_STYLE_POINTER_EVENTS_NONE:
    break;
  case NS_STYLE_POINTER_EVENTS_AUTO:
  case NS_STYLE_POINTER_EVENTS_VISIBLEPAINTED:
    if (GetStyleVisibility()->IsVisible()) {
      if (GetStyleSVG()->mFill.mType != eStyleSVGPaintType_None)
        flags |= SVG_HIT_TEST_FILL;
      if (GetStyleSVG()->mStroke.mType != eStyleSVGPaintType_None)
        flags |= SVG_HIT_TEST_STROKE;
      if (GetStyleSVG()->mStrokeOpacity > 0)
        flags |= SVG_HIT_TEST_CHECK_MRECT;
    }
    break;
  case NS_STYLE_POINTER_EVENTS_VISIBLEFILL:
    if (GetStyleVisibility()->IsVisible()) {
      flags |= SVG_HIT_TEST_FILL;
    }
    break;
  case NS_STYLE_POINTER_EVENTS_VISIBLESTROKE:
    if (GetStyleVisibility()->IsVisible()) {
      flags |= SVG_HIT_TEST_STROKE;
    }
    break;
  case NS_STYLE_POINTER_EVENTS_VISIBLE:
    if (GetStyleVisibility()->IsVisible()) {
      flags |= SVG_HIT_TEST_FILL | SVG_HIT_TEST_STROKE;
    }
    break;
  case NS_STYLE_POINTER_EVENTS_PAINTED:
    if (GetStyleSVG()->mFill.mType != eStyleSVGPaintType_None)
      flags |= SVG_HIT_TEST_FILL;
    if (GetStyleSVG()->mStroke.mType != eStyleSVGPaintType_None)
      flags |= SVG_HIT_TEST_STROKE;
    if (GetStyleSVG()->mStrokeOpacity)
      flags |= SVG_HIT_TEST_CHECK_MRECT;
    break;
  case NS_STYLE_POINTER_EVENTS_FILL:
    flags |= SVG_HIT_TEST_FILL;
    break;
  case NS_STYLE_POINTER_EVENTS_STROKE:
    flags |= SVG_HIT_TEST_STROKE;
    break;
  case NS_STYLE_POINTER_EVENTS_ALL:
    flags |= SVG_HIT_TEST_FILL | SVG_HIT_TEST_STROKE;
    break;
  default:
    NS_ERROR("not reached");
    break;
  }

  return flags;
}
