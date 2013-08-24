/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "nsSVGPathGeometryFrame.h"

// Keep others in (case-insensitive) order:
#include "gfxContext.h"
#include "gfxPlatform.h"
#include "nsGkAtoms.h"
#include "nsRenderingContext.h"
#include "nsSVGEffects.h"
#include "nsSVGGraphicElement.h"
#include "nsSVGIntegrationUtils.h"
#include "nsSVGMarkerFrame.h"
#include "nsSVGPathGeometryElement.h"
#include "nsSVGUtils.h"
#include "SVGAnimatedTransformList.h"

using namespace mozilla;

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
    nsSVGUtils::InvalidateAndScheduleBoundsUpdate(this);

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

  nsSVGUtils::InvalidateAndScheduleBoundsUpdate(this);
}

nsIAtom *
nsSVGPathGeometryFrame::GetType() const
{
  return nsGkAtoms::svgPathGeometryFrame;
}

bool
nsSVGPathGeometryFrame::IsSVGTransformed(gfxMatrix *aOwnTransform,
                                         gfxMatrix *aFromParentTransform) const
{
  bool foundTransform = false;

  // Check if our parent has children-only transforms:
  nsIFrame *parent = GetParent();
  if (parent &&
      parent->IsFrameOfType(nsIFrame::eSVG | nsIFrame::eSVGContainer)) {
    foundTransform = static_cast<nsSVGContainerFrame*>(parent)->
                       HasChildrenOnlyTransform(aFromParentTransform);
  }

  nsSVGElement *content = static_cast<nsSVGElement*>(mContent);
  const SVGAnimatedTransformList *list = content->GetAnimatedTransformList();
  if ((list && !list->GetAnimValue().IsEmpty()) ||
      content->GetAnimateMotionTransform()) {
    if (aOwnTransform) {
      *aOwnTransform = content->PrependLocalTransformsTo(gfxMatrix(),
                                  nsSVGElement::eUserSpaceToParent);
    }
    foundTransform = true;
  }
  return foundTransform;
}

//----------------------------------------------------------------------
// nsISVGChildFrame methods

NS_IMETHODIMP
nsSVGPathGeometryFrame::PaintSVG(nsRenderingContext *aContext,
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
  gfxMatrix canvasTM = GetCanvasTM(FOR_HIT_TESTING);
  PRUint16 fillRule, hitTestFlags;
  if (GetStateBits() & NS_STATE_SVG_CLIPPATH_CHILD) {
    hitTestFlags = SVG_HIT_TEST_FILL;
    fillRule = GetClipRule();
  } else {
    hitTestFlags = GetHitTestFlags();
    // XXX once bug 614732 is fixed, aPoint won't need any conversion in order
    // to compare it with mRect.
    if (canvasTM.IsSingular()) {
      return nsnull;
    }
    nsPoint point =
      nsSVGUtils::TransformOuterSVGPointToChildFrame(aPoint, canvasTM, PresContext());
    if (!hitTestFlags || ((hitTestFlags & SVG_HIT_TEST_CHECK_MRECT) &&
                          !mRect.Contains(point)))
      return nsnull;
    fillRule = GetStyleSVG()->mFillRule;
  }

  bool isHit = false;

  nsRefPtr<gfxContext> tmpCtx =
    new gfxContext(gfxPlatform::GetPlatform()->ScreenReferenceSurface());

  GeneratePath(tmpCtx, canvasTM);
  gfxPoint userSpacePoint =
    tmpCtx->DeviceToUser(gfxPoint(PresContext()->AppUnitsToGfxUnits(aPoint.x),
                                  PresContext()->AppUnitsToGfxUnits(aPoint.y)));

  if (fillRule == NS_STYLE_FILL_RULE_EVENODD)
    tmpCtx->SetFillRule(gfxContext::FILL_RULE_EVEN_ODD);
  else
    tmpCtx->SetFillRule(gfxContext::FILL_RULE_WINDING);

  if (hitTestFlags & SVG_HIT_TEST_FILL)
    isHit = tmpCtx->PointInFill(userSpacePoint);
  if (!isHit && (hitTestFlags & SVG_HIT_TEST_STROKE)) {
    SetupCairoStrokeHitGeometry(tmpCtx);
    isHit = tmpCtx->PointInStroke(userSpacePoint);
  }

  if (isHit && nsSVGUtils::HitTestClip(this, aPoint))
    return this;

  return nsnull;
}

NS_IMETHODIMP_(nsRect)
nsSVGPathGeometryFrame::GetCoveredRegion()
{
  // See bug 614732 comment 32:
  //return nsSVGUtils::TransformFrameRectToOuterSVG(mRect, GetCanvasTM(), PresContext());
  return mCoveredRegion;
}

void
nsSVGPathGeometryFrame::UpdateBounds()
{
  NS_ASSERTION(nsSVGUtils::OuterSVGIsCallingUpdateBounds(this),
               "This call is probaby a wasteful mistake");

  NS_ABORT_IF_FALSE(!(GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD),
                    "UpdateBounds mechanism not designed for this");

  if (!nsSVGUtils::NeedsUpdatedBounds(this)) {
    return;
  }

  PRUint32 flags = nsSVGUtils::eBBoxIncludeFill |
                   nsSVGUtils::eBBoxIncludeStroke |
                   nsSVGUtils::eBBoxIncludeMarkers;
  // Our "visual" overflow rect needs to be valid for building display lists
  // for hit testing, which means that for certain values of 'pointer-events'
  // it needs to include the geometry of the fill or stroke even when the fill/
  // stroke don't actually render (e.g. when stroke="none" or
  // stroke-opacity="0"). GetHitTestFlags() accounts for 'pointer-events'.
  PRUint16 hitTestFlags = GetHitTestFlags();
  if ((hitTestFlags & SVG_HIT_TEST_FILL)) {
   flags |= nsSVGUtils::eBBoxIncludeFillGeometry;
  }
  if ((hitTestFlags & SVG_HIT_TEST_STROKE)) {
   flags |= nsSVGUtils::eBBoxIncludeStrokeGeometry;
  }
  gfxRect extent = GetBBoxContribution(gfxMatrix(), flags);
  mRect = nsLayoutUtils::RoundGfxRectToAppRect(extent,
            PresContext()->AppUnitsPerCSSPixel());

  // See bug 614732 comment 32.
  mCoveredRegion = nsSVGUtils::TransformFrameRectToOuterSVG(
    mRect, GetCanvasTM(FOR_OUTERSVG_TM), PresContext());

  if (mState & NS_FRAME_FIRST_REFLOW) {
    // Make sure we have our filter property (if any) before calling
    // FinishAndStoreOverflow (subsequent filter changes are handled off
    // nsChangeHint_UpdateEffects):
    nsSVGEffects::UpdateEffects(this);
  }

  // We only invalidate if we are dirty, if our outer-<svg> has already had its
  // initial reflow (since if it hasn't, its entire area will be invalidated
  // when it gets that initial reflow), and if our parent is not dirty (since
  // if it is, then it will invalidate its entire new area, which will include
  // our new area).
  bool invalidate = (mState & NS_FRAME_IS_DIRTY) &&
    !(GetParent()->GetStateBits() &
       (NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_DIRTY));

  nsRect overflow = nsRect(nsPoint(0,0), mRect.Size());
  nsOverflowAreas overflowAreas(overflow, overflow);
  FinishAndStoreOverflow(overflowAreas, mRect.Size());

  mState &= ~(NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_DIRTY |
              NS_FRAME_HAS_DIRTY_CHILDREN);

  if (invalidate) {
    // XXXSDL Let FinishAndStoreOverflow do this.
    nsSVGUtils::InvalidateBounds(this, true);
  }
}

void
nsSVGPathGeometryFrame::NotifySVGChanged(PRUint32 aFlags)
{
  NS_ABORT_IF_FALSE(!(aFlags & DO_NOT_NOTIFY_RENDERING_OBSERVERS) ||
                    (GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD),
                    "Must be NS_STATE_SVG_NONDISPLAY_CHILD!");

  NS_ABORT_IF_FALSE(aFlags & (TRANSFORM_CHANGED | COORD_CONTEXT_CHANGED),
                    "Invalidation logic may need adjusting");

  // Ancestor changes can't affect how we render from the perspective of
  // any rendering observers that we may have, so we don't need to
  // invalidate them. We also don't need to invalidate ourself, since our
  // changed ancestor will have invalidated its entire area, which includes
  // our area.
  nsSVGUtils::ScheduleBoundsUpdate(this);
}

SVGBBox
nsSVGPathGeometryFrame::GetBBoxContribution(const gfxMatrix &aToBBoxUserspace,
                                            PRUint32 aFlags)
{
  SVGBBox bbox;

  if (aToBBoxUserspace.IsSingular()) {
    // XXX ReportToConsole
    return bbox;
  }

  nsRefPtr<gfxContext> tmpCtx =
    new gfxContext(gfxPlatform::GetPlatform()->ScreenReferenceSurface());

  GeneratePath(tmpCtx, aToBBoxUserspace);
  tmpCtx->IdentityMatrix();

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

  gfxRect pathExtents = tmpCtx->GetUserPathExtent();

  // Account for fill:
  if ((aFlags & nsSVGUtils::eBBoxIncludeFillGeometry) ||
      ((aFlags & nsSVGUtils::eBBoxIncludeFill) &&
       GetStyleSVG()->mFill.mType != eStyleSVGPaintType_None)) {
    bbox = pathExtents;
  }

  // Account for stroke:
  if ((aFlags & nsSVGUtils::eBBoxIncludeStrokeGeometry) ||
      ((aFlags & nsSVGUtils::eBBoxIncludeStroke) && HasStroke())) {
    // We can't use tmpCtx->GetUserStrokeExtent() since it doesn't work for
    // device space extents. Instead we approximate the stroke extents from
    // pathExtents using PathExtentsToMaxStrokeExtents.
    if (pathExtents.Width() <= 0 && pathExtents.Height() <= 0) {
      // We have a zero length path, but it may still have non-empty stroke
      // bounds depending on the value of stroke-linecap. We need to fix up
      // pathExtents before it can be used with PathExtentsToMaxStrokeExtents
      // though, because if pathExtents is empty, its position will not have
      // been set. Happily we can use tmpCtx->GetUserStrokeExtent() to find
      // the center point of the extents even though it gets the extents wrong.
      SetupCairoStrokeGeometry(tmpCtx);
      pathExtents.MoveTo(tmpCtx->GetUserStrokeExtent().Center());
      pathExtents.SizeTo(0, 0);
    }
    bbox.UnionEdges(nsSVGUtils::PathExtentsToMaxStrokeExtents(pathExtents,
                                                              this,
                                                              aToBBoxUserspace));
  }

  // Account for markers:
  if ((aFlags & nsSVGUtils::eBBoxIncludeMarkers) != 0 &&
      static_cast<nsSVGPathGeometryElement*>(mContent)->IsMarkable()) {

    float strokeWidth = GetStrokeWidth();
    MarkerProperties properties = GetMarkerProperties(this);

    if (properties.MarkersExist()) {
      nsTArray<nsSVGMark> marks;
      static_cast<nsSVGPathGeometryElement*>(mContent)->GetMarkPoints(&marks);
      PRUint32 num = marks.Length();

      if (num) {
        nsSVGMarkerFrame *frame = properties.GetMarkerStartFrame();
        if (frame) {
          SVGBBox mbbox =
            frame->GetMarkBBoxContribution(aToBBoxUserspace, aFlags, this,
                                           &marks[0], strokeWidth);
          bbox.UnionEdges(mbbox);
        }

        frame = properties.GetMarkerMidFrame();
        if (frame) {
          for (PRUint32 i = 1; i < num - 1; i++) {
            SVGBBox mbbox =
              frame->GetMarkBBoxContribution(aToBBoxUserspace, aFlags, this,
                                             &marks[i], strokeWidth);
            bbox.UnionEdges(mbbox);
          }
        }

        frame = properties.GetMarkerEndFrame();
        if (frame) {
          SVGBBox mbbox =
            frame->GetMarkBBoxContribution(aToBBoxUserspace, aFlags, this,
                                           &marks[num-1], strokeWidth);
          bbox.UnionEdges(mbbox);
        }
      }
    }
  }

  return bbox;
}

//----------------------------------------------------------------------
// nsSVGGeometryFrame methods:

gfxMatrix
nsSVGPathGeometryFrame::GetCanvasTM(PRUint32 aFor)
{
  if (!(GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD)) {
    if ((aFor == FOR_PAINTING && NS_SVGDisplayListPaintingEnabled()) ||
        (aFor == FOR_HIT_TESTING && NS_SVGDisplayListHitTestingEnabled())) {
      return nsSVGIntegrationUtils::GetCSSPxToDevPxMatrix(this);
    }
  }

  NS_ASSERTION(mParent, "null parent");

  nsSVGContainerFrame *parent = static_cast<nsSVGContainerFrame*>(mParent);
  nsSVGGraphicElement *content = static_cast<nsSVGGraphicElement*>(mContent);

  return content->PrependLocalTransformsTo(parent->GetCanvasTM(aFor));
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
nsSVGPathGeometryFrame::Render(nsRenderingContext *aContext)
{
  gfxContext *gfx = aContext->ThebesContext();

  PRUint16 renderMode = SVGAutoRenderState::GetRenderMode(aContext);

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

  GeneratePath(gfx, GetCanvasTM(FOR_PAINTING));

  if (renderMode != SVGAutoRenderState::NORMAL) {
    NS_ABORT_IF_FALSE(renderMode == SVGAutoRenderState::CLIP ||
                      renderMode == SVGAutoRenderState::CLIP_MASK,
                      "Unknown render mode");
    gfx->Restore();

    if (GetClipRule() == NS_STYLE_FILL_RULE_EVENODD)
      gfx->SetFillRule(gfxContext::FILL_RULE_EVEN_ODD);
    else
      gfx->SetFillRule(gfxContext::FILL_RULE_WINDING);

    if (renderMode == SVGAutoRenderState::CLIP_MASK) {
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
                                     const gfxMatrix &aTransform)
{
  if (aTransform.IsSingular()) {
    aContext->IdentityMatrix();
    aContext->NewPath();
    return;
  }

  aContext->Multiply(aTransform);

  // Hack to let SVGPathData::ConstructPath know if we have square caps:
  const nsStyleSVG* style = GetStyleSVG();
  if (style->mStrokeLinecap == NS_STYLE_STROKE_LINECAP_SQUARE) {
    aContext->SetLineCap(gfxContext::LINE_CAP_SQUARE);
  }

  aContext->NewPath();
  static_cast<nsSVGPathGeometryElement*>(mContent)->ConstructPath(aContext);
}
