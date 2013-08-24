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
 * The Initial Developer of the Original Code is
 * Crocodile Clips Ltd..
 * Portions created by the Initial Developer are Copyright (C) 2002
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Alex Fritze <alex.fritze@crocodile-clips.com> (original author)
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

#include "nsSVGPathGeometryFrame.h"
#include "nsGkAtoms.h"
#include "nsSVGMarkerFrame.h"
#include "nsSVGUtils.h"
#include "nsSVGEffects.h"
#include "nsSVGGraphicElement.h"
#include "nsSVGOuterSVGFrame.h"
#include "nsSVGRect.h"
#include "nsSVGPathGeometryElement.h"
#include "gfxContext.h"
#include "gfxPlatform.h"

//----------------------------------------------------------------------
// Implementation

nsIFrame*
NS_NewSVGPathGeometryFrame(nsIPresShell* aPresShell,
                           nsStyleContext* aContext)
{
  return new (aPresShell) nsSVGPathGeometryFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsSVGPathGeometryFrame)

//----------------------------------------------------------------------
// nsQueryFrame methods

NS_QUERYFRAME_HEAD(nsSVGPathGeometryFrame)
  NS_QUERYFRAME_ENTRY(nsISVGChildFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsSVGPathGeometryFrameBase)

//----------------------------------------------------------------------
// nsIFrame methods

NS_IMETHODIMP
nsSVGPathGeometryFrame::AttributeChanged(PRInt32         aNameSpaceID,
                                         nsIAtom*        aAttribute,
                                         PRInt32         aModType)
{
  if (aNameSpaceID == kNameSpaceID_None &&
      (static_cast<nsSVGPathGeometryElement*>
                  (mContent)->AttributeDefinesGeometry(aAttribute) ||
       aAttribute == nsGkAtoms::transform))
    nsSVGUtils::UpdateGraphic(this);

  return NS_OK;
}

/* virtual */ void
nsSVGPathGeometryFrame::DidSetStyleContext(nsStyleContext* aOldStyleContext)
{
  nsSVGPathGeometryFrameBase::DidSetStyleContext(aOldStyleContext);

  // XXX: we'd like to use the style_hint mechanism and the
  // ContentStateChanged/AttributeChanged functions for style changes
  // to get slightly finer granularity, but unfortunately the
  // style_hints don't map very well onto svg. Here seems to be the
  // best place to deal with style changes:

  nsSVGUtils::UpdateGraphic(this);
}

nsIAtom *
nsSVGPathGeometryFrame::GetType() const
{
  return nsGkAtoms::svgPathGeometryFrame;
}

//----------------------------------------------------------------------
// nsISVGChildFrame methods

NS_IMETHODIMP
nsSVGPathGeometryFrame::PaintSVG(nsSVGRenderState *aContext,
                                 const nsIntRect *aDirtyRect)
{
  if (!GetStyleVisibility()->IsVisible())
    return NS_OK;

  /* render */
  Render(aContext);

  if (static_cast<nsSVGPathGeometryElement*>(mContent)->IsMarkable()) {
    MarkerProperties properties = GetMarkerProperties(this);
      
    if (properties.MarkersExist()) {
      float strokeWidth = GetStrokeWidth();
        
      nsTArray<nsSVGMark> marks;
      static_cast<nsSVGPathGeometryElement*>
                 (mContent)->GetMarkPoints(&marks);
        
      PRUint32 num = marks.Length();

      if (num) {
        nsSVGMarkerFrame *frame = properties.GetMarkerStartFrame();
        if (frame)
          frame->PaintMark(aContext, this, &marks[0], strokeWidth);

        frame = properties.GetMarkerMidFrame();
        if (frame) {
          for (PRUint32 i = 1; i < num - 1; i++)
            frame->PaintMark(aContext, this, &marks[i], strokeWidth);
        }

        frame = properties.GetMarkerEndFrame();
        if (frame)
          frame->PaintMark(aContext, this, &marks[num-1], strokeWidth);
      }
    }
  }

  return NS_OK;
}

NS_IMETHODIMP_(nsIFrame*)
nsSVGPathGeometryFrame::GetFrameForPoint(const nsPoint &aPoint)
{
  PRUint16 fillRule, mask;
  // check if we're a clipPath - cheaper than IsClipChild(), and we shouldn't
  // get in here for other nondisplay children
  if (GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD) {
    NS_ASSERTION(IsClipChild(), "should be in clipPath but we're not");
    mask = HITTEST_MASK_FILL;
    fillRule = GetClipRule();
  } else {
    mask = GetHittestMask();
    if (!mask || (!(mask & HITTEST_MASK_FORCE_TEST) &&
                  !mRect.Contains(aPoint)))
      return nsnull;
    fillRule = GetStyleSVG()->mFillRule;
  }

  PRBool isHit = PR_FALSE;

  gfxContext context(gfxPlatform::GetPlatform()->ScreenReferenceSurface());

  GeneratePath(&context);
  gfxPoint userSpacePoint =
    context.DeviceToUser(gfxPoint(PresContext()->AppUnitsToGfxUnits(aPoint.x),
                                  PresContext()->AppUnitsToGfxUnits(aPoint.y)));

  if (fillRule == NS_STYLE_FILL_RULE_EVENODD)
    context.SetFillRule(gfxContext::FILL_RULE_EVEN_ODD);
  else
    context.SetFillRule(gfxContext::FILL_RULE_WINDING);

  if (mask & HITTEST_MASK_FILL)
    isHit = context.PointInFill(userSpacePoint);
  if (!isHit && (mask & HITTEST_MASK_STROKE)) {
    SetupCairoStrokeHitGeometry(&context);
    isHit = context.PointInStroke(userSpacePoint);
  }

  if (isHit && nsSVGUtils::HitTestClip(this, aPoint))
    return this;

  return nsnull;
}

NS_IMETHODIMP_(nsRect)
nsSVGPathGeometryFrame::GetCoveredRegion()
{
  // XXX why are we adding in markers here each time someone gets the covered
  // region? Isn't UpdateCoveredRegion called whenever markers change?
  // And why are the answers to these questions not documented here??!!

  if (static_cast<nsSVGPathGeometryElement*>(mContent)->IsMarkable()) {
    MarkerProperties properties = GetMarkerProperties(this);

    if (!properties.MarkersExist())
      return mRect;

    nsRect rect(mRect);

    float strokeWidth = GetStrokeWidth();

    nsTArray<nsSVGMark> marks;
    static_cast<nsSVGPathGeometryElement*>(mContent)->GetMarkPoints(&marks);

    PRUint32 num = marks.Length();

    if (num) {
      nsSVGMarkerFrame *frame = properties.GetMarkerStartFrame();
      if (frame) {
        nsRect mark = frame->RegionMark(this, &marks[0], strokeWidth);
        rect.UnionRect(rect, mark);
      }

      frame = properties.GetMarkerMidFrame();
      if (frame) {
        for (PRUint32 i = 1; i < num - 1; i++) {
          nsRect mark = frame->RegionMark(this, &marks[i], strokeWidth);
          rect.UnionRect(rect, mark);
        }
      }

      frame = properties.GetMarkerEndFrame();
      if (frame) {
        nsRect mark = frame->RegionMark(this, &marks[num-1], strokeWidth);
        rect.UnionRect(rect, mark);
      }
    }

    return rect;
  }

  return mRect;
}

NS_IMETHODIMP
nsSVGPathGeometryFrame::UpdateCoveredRegion()
{
  mRect.Empty();

  gfxContext context(gfxPlatform::GetPlatform()->ScreenReferenceSurface());

  GeneratePath(&context);
  context.IdentityMatrix();

  gfxRect extent = context.GetUserPathExtent();

  // Be careful when replacing the following logic to get the fill and stroke
  // extents independently (instead of computing the stroke extents from the
  // path extents). You may think that you can just use the stroke extents if
  // there is both a fill and a stroke. In reality it's necessary to calculate
  // both the fill and stroke extents, and take the union of the two. There are
  // two reasons for this:
  //
  // # Due to stroke dashing, in certain cases the fill extents could actually
  //   extend outside the stroke extents.
  // # If the stroke is very thin, cairo won't paint any stroke, and so the
  //   stroke bounds that it will return will be empty.

  if (HasStroke()) {
    SetupCairoStrokeGeometry(&context);
    if (extent.Width() <= 0 && extent.Height() <= 0) {
      // If 'extent' is empty, its position will not be set. Although
      // GetUserStrokeExtent gets the extents wrong we can still use it
      // to get the device space position of zero length stroked paths.
      extent = context.GetUserStrokeExtent();
      extent.pos.x += extent.size.width / 2;
      extent.pos.y += extent.size.height / 2;
      extent.size.width = 0;
      extent.size.height = 0;
    }
    extent = nsSVGUtils::PathExtentsToMaxStrokeExtents(extent, this);
  } else if (GetStyleSVG()->mFill.mType == eStyleSVGPaintType_None) {
    extent = gfxRect(0, 0, 0, 0);
  }

  if (!extent.IsEmpty()) {
    mRect = nsSVGUtils::ToAppPixelRect(PresContext(), extent);
  }

  // Add in markers
  mRect = GetCoveredRegion();

  return NS_OK;
}

NS_IMETHODIMP
nsSVGPathGeometryFrame::InitialUpdate()
{
  NS_ASSERTION(GetStateBits() & NS_FRAME_FIRST_REFLOW,
               "Yikes! We've been called already! Hopefully we weren't called "
               "before our nsSVGOuterSVGFrame's initial Reflow()!!!");

  nsSVGUtils::UpdateGraphic(this);

  NS_ASSERTION(!(mState & NS_FRAME_IN_REFLOW),
               "We don't actually participate in reflow");
  
  // Do unset the various reflow bits, though.
  mState &= ~(NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_DIRTY |
              NS_FRAME_HAS_DIRTY_CHILDREN);
  return NS_OK;
}

void
nsSVGPathGeometryFrame::NotifySVGChanged(PRUint32 aFlags)
{
  if (!(aFlags & SUPPRESS_INVALIDATION)) {
    nsSVGUtils::UpdateGraphic(this);
  }
}

NS_IMETHODIMP
nsSVGPathGeometryFrame::NotifyRedrawSuspended()
{
  // XXX should we cache the fact that redraw is suspended?
  return NS_OK;
}

NS_IMETHODIMP
nsSVGPathGeometryFrame::NotifyRedrawUnsuspended()
{
  if (GetStateBits() & NS_STATE_SVG_DIRTY)
    nsSVGUtils::UpdateGraphic(this);

  return NS_OK;
}

NS_IMETHODIMP
nsSVGPathGeometryFrame::SetMatrixPropagation(PRBool aPropagate)
{
  if (aPropagate) {
    AddStateBits(NS_STATE_SVG_PROPAGATE_TRANSFORM);
  } else {
    RemoveStateBits(NS_STATE_SVG_PROPAGATE_TRANSFORM);
  }
  return NS_OK;
}

PRBool
nsSVGPathGeometryFrame::GetMatrixPropagation()
{
  return (GetStateBits() & NS_STATE_SVG_PROPAGATE_TRANSFORM) != 0;
}

gfxRect
nsSVGPathGeometryFrame::GetBBoxContribution(const gfxMatrix &aToBBoxUserspace)
{
  if (aToBBoxUserspace.IsSingular()) {
    // XXX ReportToConsole
    return gfxRect(0.0, 0.0, 0.0, 0.0);
  }
  gfxContext context(gfxPlatform::GetPlatform()->ScreenReferenceSurface());
  GeneratePath(&context, &aToBBoxUserspace);
  context.IdentityMatrix();
  return context.GetUserPathExtent();
}

//----------------------------------------------------------------------
// nsSVGGeometryFrame methods:

gfxMatrix
nsSVGPathGeometryFrame::GetCanvasTM()
{
  NS_ASSERTION(mParent, "null parent");

  nsSVGContainerFrame *parent = static_cast<nsSVGContainerFrame*>(mParent);
  nsSVGGraphicElement *content = static_cast<nsSVGGraphicElement*>(mContent);

  return content->PrependLocalTransformTo(parent->GetCanvasTM());
}

//----------------------------------------------------------------------
// nsSVGPathGeometryFrame methods:

nsSVGPathGeometryFrame::MarkerProperties
nsSVGPathGeometryFrame::GetMarkerProperties(nsSVGPathGeometryFrame *aFrame)
{
  NS_ASSERTION(!aFrame->GetPrevContinuation(), "aFrame should be first continuation");

  MarkerProperties result;
  const nsStyleSVG *style = aFrame->GetStyleSVG();
  result.mMarkerStart =
    nsSVGEffects::GetMarkerProperty(style->mMarkerStart, aFrame,
                                    nsSVGEffects::MarkerBeginProperty());
  result.mMarkerMid =
    nsSVGEffects::GetMarkerProperty(style->mMarkerMid, aFrame,
                                    nsSVGEffects::MarkerMiddleProperty());
  result.mMarkerEnd =
    nsSVGEffects::GetMarkerProperty(style->mMarkerEnd, aFrame,
                                    nsSVGEffects::MarkerEndProperty());
  return result;
}

nsSVGMarkerFrame *
nsSVGPathGeometryFrame::MarkerProperties::GetMarkerStartFrame()
{
  if (!mMarkerStart)
    return nsnull;
  return static_cast<nsSVGMarkerFrame *>
    (mMarkerStart->GetReferencedFrame(nsGkAtoms::svgMarkerFrame, nsnull));
}

nsSVGMarkerFrame *
nsSVGPathGeometryFrame::MarkerProperties::GetMarkerMidFrame()
{
  if (!mMarkerMid)
    return nsnull;
  return static_cast<nsSVGMarkerFrame *>
    (mMarkerMid->GetReferencedFrame(nsGkAtoms::svgMarkerFrame, nsnull));
}

nsSVGMarkerFrame *
nsSVGPathGeometryFrame::MarkerProperties::GetMarkerEndFrame()
{
  if (!mMarkerEnd)
    return nsnull;
  return static_cast<nsSVGMarkerFrame *>
    (mMarkerEnd->GetReferencedFrame(nsGkAtoms::svgMarkerFrame, nsnull));
}

void
nsSVGPathGeometryFrame::Render(nsSVGRenderState *aContext)
{
  gfxContext *gfx = aContext->GetGfxContext();

  PRUint16 renderMode = aContext->GetRenderMode();

  switch (GetStyleSVG()->mShapeRendering) {
  case NS_STYLE_SHAPE_RENDERING_OPTIMIZESPEED:
  case NS_STYLE_SHAPE_RENDERING_CRISPEDGES:
    gfx->SetAntialiasMode(gfxContext::MODE_ALIASED);
    break;
  default:
    gfx->SetAntialiasMode(gfxContext::MODE_COVERAGE);
    break;
  }

  /* save/restore the state so we don't screw up the xform */
  gfx->Save();

  GeneratePath(gfx);

  if (renderMode != nsSVGRenderState::NORMAL) {
    gfx->Restore();

    if (GetClipRule() == NS_STYLE_FILL_RULE_EVENODD)
      gfx->SetFillRule(gfxContext::FILL_RULE_EVEN_ODD);
    else
      gfx->SetFillRule(gfxContext::FILL_RULE_WINDING);

    if (renderMode == nsSVGRenderState::CLIP_MASK) {
      gfx->SetColor(gfxRGBA(1.0f, 1.0f, 1.0f, 1.0f));
      gfx->Fill();
      gfx->NewPath();
    }

    return;
  }

  if (SetupCairoFill(gfx)) {
    gfx->Fill();
  }

  if (SetupCairoStroke(gfx)) {
    gfx->Stroke();
  }

  gfx->NewPath();

  gfx->Restore();
}

void
nsSVGPathGeometryFrame::GeneratePath(gfxContext* aContext,
                                     const gfxMatrix *aOverrideTransform)
{
  gfxMatrix matrix;
  if (aOverrideTransform) {
    matrix = *aOverrideTransform;
  } else {
    matrix = GetCanvasTM();
  }

  if (matrix.IsSingular()) {
    aContext->IdentityMatrix();
    aContext->NewPath();
    return;
  }

  aContext->Multiply(matrix);

  aContext->NewPath();
  static_cast<nsSVGPathGeometryElement*>(mContent)->ConstructPath(aContext);
}

PRUint16
nsSVGPathGeometryFrame::GetHittestMask()
{
  PRUint16 mask = 0;

  switch(GetStyleVisibility()->mPointerEvents) {
    case NS_STYLE_POINTER_EVENTS_NONE:
      break;
    case NS_STYLE_POINTER_EVENTS_VISIBLEPAINTED:
    case NS_STYLE_POINTER_EVENTS_AUTO:
      if (GetStyleVisibility()->IsVisible()) {
        if (GetStyleSVG()->mFill.mType != eStyleSVGPaintType_None)
          mask |= HITTEST_MASK_FILL;
        if (GetStyleSVG()->mStroke.mType != eStyleSVGPaintType_None)
          mask |= HITTEST_MASK_STROKE;
      }
      break;
    case NS_STYLE_POINTER_EVENTS_VISIBLEFILL:
      if (GetStyleVisibility()->IsVisible()) {
        mask |= HITTEST_MASK_FILL | HITTEST_MASK_FORCE_TEST;
      }
      break;
    case NS_STYLE_POINTER_EVENTS_VISIBLESTROKE:
      if (GetStyleVisibility()->IsVisible()) {
        mask |= HITTEST_MASK_STROKE | HITTEST_MASK_FORCE_TEST;
      }
      break;
    case NS_STYLE_POINTER_EVENTS_VISIBLE:
      if (GetStyleVisibility()->IsVisible()) {
        mask |=
          HITTEST_MASK_FILL |
          HITTEST_MASK_STROKE |
          HITTEST_MASK_FORCE_TEST;
      }
      break;
    case NS_STYLE_POINTER_EVENTS_PAINTED:
      if (GetStyleSVG()->mFill.mType != eStyleSVGPaintType_None)
        mask |= HITTEST_MASK_FILL;
      if (GetStyleSVG()->mStroke.mType != eStyleSVGPaintType_None)
        mask |= HITTEST_MASK_STROKE;
      break;
    case NS_STYLE_POINTER_EVENTS_FILL:
      mask |= HITTEST_MASK_FILL | HITTEST_MASK_FORCE_TEST;
      break;
    case NS_STYLE_POINTER_EVENTS_STROKE:
      mask |= HITTEST_MASK_STROKE | HITTEST_MASK_FORCE_TEST;
      break;
    case NS_STYLE_POINTER_EVENTS_ALL:
      mask |=
        HITTEST_MASK_FILL |
        HITTEST_MASK_STROKE |
        HITTEST_MASK_FORCE_TEST;
      break;
    default:
      NS_ERROR("not reached");
      break;
  }

  return mask;
}
