/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "nsSVGForeignObjectFrame.h"

// Keep others in (case-insensitive) order:
#include "gfxContext.h"
#include "gfxMatrix.h"
#include "nsGkAtoms.h"
#include "nsIDOMSVGForeignObjectElem.h"
#include "nsINameSpaceManager.h"
#include "nsLayoutUtils.h"
#include "nsRegion.h"
#include "nsRenderingContext.h"
#include "nsSVGContainerFrame.h"
#include "nsSVGEffects.h"
#include "nsSVGForeignObjectElement.h"
#include "nsSVGIntegrationUtils.h"
#include "nsSVGOuterSVGFrame.h"
#include "nsSVGUtils.h"
#include "mozilla/AutoRestore.h"

using namespace mozilla;

//----------------------------------------------------------------------
// Implementation

nsIFrame*
NS_NewSVGForeignObjectFrame(nsIPresShell   *aPresShell,
                            nsStyleContext *aContext)
{
  return new (aPresShell) nsSVGForeignObjectFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsSVGForeignObjectFrame)

nsSVGForeignObjectFrame::nsSVGForeignObjectFrame(nsStyleContext* aContext)
  : nsSVGForeignObjectFrameBase(aContext),
    mInReflow(false)
{
  AddStateBits(NS_FRAME_REFLOW_ROOT | NS_FRAME_MAY_BE_TRANSFORMED |
               NS_FRAME_SVG_LAYOUT);
}

//----------------------------------------------------------------------
// nsIFrame methods

NS_QUERYFRAME_HEAD(nsSVGForeignObjectFrame)
  NS_QUERYFRAME_ENTRY(nsISVGChildFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsSVGForeignObjectFrameBase)

NS_IMETHODIMP
nsSVGForeignObjectFrame::Init(nsIContent* aContent,
                              nsIFrame*   aParent,
                              nsIFrame*   aPrevInFlow)
{
#ifdef DEBUG
  nsCOMPtr<nsIDOMSVGForeignObjectElement> foreignObject = do_QueryInterface(aContent);
  NS_ASSERTION(foreignObject, "Content is not an SVG foreignObject!");
#endif

  nsresult rv = nsSVGForeignObjectFrameBase::Init(aContent, aParent, aPrevInFlow);
  AddStateBits(aParent->GetStateBits() &
               (NS_STATE_SVG_NONDISPLAY_CHILD | NS_STATE_SVG_CLIPPATH_CHILD));
  AddStateBits(NS_FRAME_FONT_INFLATION_CONTAINER |
               NS_FRAME_FONT_INFLATION_FLOW_ROOT);
  return rv;
}

nsIAtom *
nsSVGForeignObjectFrame::GetType() const
{
  return nsGkAtoms::svgForeignObjectFrame;
}

NS_IMETHODIMP
nsSVGForeignObjectFrame::AttributeChanged(PRInt32  aNameSpaceID,
                                          nsIAtom *aAttribute,
                                          PRInt32  aModType)
{
  if (aNameSpaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::width ||
        aAttribute == nsGkAtoms::height) {
      nsSVGUtils::InvalidateAndScheduleBoundsUpdate(this);
      // XXXjwatt: why mark intrinsic widths dirty? can't we just use eResize?
      RequestReflow(nsIPresShell::eStyleChange);
    } else if (aAttribute == nsGkAtoms::x ||
               aAttribute == nsGkAtoms::y ||
               aAttribute == nsGkAtoms::transform) {
      // make sure our cached transform matrix gets (lazily) updated
      mCanvasTM = nsnull;
      nsSVGUtils::InvalidateAndScheduleBoundsUpdate(this);
    } else if (aAttribute == nsGkAtoms::viewBox ||
               aAttribute == nsGkAtoms::preserveAspectRatio) {
      nsSVGUtils::InvalidateBounds(this);
    }
  }

  return NS_OK;
}

/* virtual */ void
nsSVGForeignObjectFrame::DidSetStyleContext(nsStyleContext* aOldStyleContext)
{
  nsSVGForeignObjectFrameBase::DidSetStyleContext(aOldStyleContext);

  // No need to invalidate before first reflow - that will happen elsewhere.
  // Moreover we haven't been initialised properly yet so we may not have the
  // right state bits.
  if (!(GetStateBits() & NS_FRAME_FIRST_REFLOW)) {
    // XXXperf: probably only need a bounds update if 'font-size' changed and
    // we have em unit width/height. Or, once we map 'transform' into style,
    // if some transform property changed.
    nsSVGUtils::InvalidateAndScheduleBoundsUpdate(this);
  }
}

NS_IMETHODIMP
nsSVGForeignObjectFrame::Reflow(nsPresContext*           aPresContext,
                                nsHTMLReflowMetrics&     aDesiredSize,
                                const nsHTMLReflowState& aReflowState,
                                nsReflowStatus&          aStatus)
{
  NS_ABORT_IF_FALSE(!(GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD),
                    "Should not have been called");

  // Only InvalidateAndScheduleBoundsUpdate marks us with NS_FRAME_IS_DIRTY,
  // so if that bit is still set we still have a resize pending. If we hit
  // this assertion, then we should get the presShell to skip reflow roots
  // that have a dirty parent since a reflow is going to come via the
  // reflow root's parent anyway.
  NS_ASSERTION(!(GetStateBits() & NS_FRAME_IS_DIRTY),
               "Reflowing while a resize is pending is wasteful");

  // UpdateBounds makes sure mRect is up to date before we're called.

  NS_ASSERTION(!aReflowState.parentReflowState,
               "should only get reflow from being reflow root");
  NS_ASSERTION(aReflowState.ComputedWidth() == GetSize().width &&
               aReflowState.ComputedHeight() == GetSize().height,
               "reflow roots should be reflowed at existing size and "
               "svg.css should ensure we have no padding/border/margin");

  DoReflow();

  aDesiredSize.width = aReflowState.ComputedWidth();
  aDesiredSize.height = aReflowState.ComputedHeight();
  aDesiredSize.SetOverflowAreasToDesiredBounds();
  aStatus = NS_FRAME_COMPLETE;

  return NS_OK;
}

void
nsSVGForeignObjectFrame::InvalidateInternal(const nsRect& aDamageRect,
                                            nscoord aX, nscoord aY,
                                            nsIFrame* aForChild,
                                            PRUint32 aFlags)
{
  // This is called by our descendants when they change.

  if (GetStateBits() & NS_FRAME_IS_DIRTY) {
    // Our entire area has been (or will be) invalidated, so no point
    // keeping track of sub-areas that our descendants dirty.
    return;
  }

  if (GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD) {
    nsSVGEffects::InvalidateRenderingObservers(this);
    return;
  }

  if (!mInReflow) {
    // We can't collect dirty areas, since we don't have a place to reliably
    // call FlushDirtyRegion before we paint, so we have to invalidate now.
    InvalidateDirtyRect(aDamageRect + nsPoint(aX, aY), aFlags, false);
    return;
  }

  nsRegion* region = (aFlags & INVALIDATE_CROSS_DOC)
    ? &mSubDocDirtyRegion : &mSameDocDirtyRegion;
  region->Or(*region, aDamageRect + nsPoint(aX, aY));
}


/**
 * Returns the app unit canvas bounds of a userspace rect.
 *
 * @param aToCanvas Transform from userspace to canvas device space.
 */
static nsRect
ToCanvasBounds(const gfxRect &aUserspaceRect,
               const gfxMatrix &aToCanvas,
               const nsPresContext *presContext)
{
  return nsLayoutUtils::RoundGfxRectToAppRect(
                          aToCanvas.TransformBounds(aUserspaceRect),
                          presContext->AppUnitsPerDevPixel());
}

NS_IMETHODIMP
nsSVGForeignObjectFrame::PaintSVG(nsRenderingContext *aContext,
                                  const nsIntRect *aDirtyRect)
{
  if (IsDisabled())
    return NS_OK;

  nsIFrame* kid = GetFirstPrincipalChild();
  if (!kid)
    return NS_OK;

  gfxMatrix canvasTM = GetCanvasTM(FOR_PAINTING);

  if (canvasTM.IsSingular()) {
    NS_WARNING("Can't render foreignObject element!");
    return NS_ERROR_FAILURE;
  }

  nsRect kidDirtyRect = kid->GetVisualOverflowRect();

  /* Check if we need to draw anything. */
  if (aDirtyRect) {
    // Transform the dirty rect into app units in our userspace.
    gfxMatrix invmatrix = canvasTM;
    invmatrix.Invert();
    NS_ASSERTION(!invmatrix.IsSingular(),
                 "inverse of non-singular matrix should be non-singular");

    gfxRect transDirtyRect = gfxRect(aDirtyRect->x, aDirtyRect->y,
                                     aDirtyRect->width, aDirtyRect->height);
    transDirtyRect = invmatrix.TransformBounds(transDirtyRect);

    kidDirtyRect.IntersectRect(kidDirtyRect,
      nsLayoutUtils::RoundGfxRectToAppRect(transDirtyRect,
                       PresContext()->AppUnitsPerCSSPixel()));

    // XXX after bug 614732 is fixed, we will compare mRect with aDirtyRect,
    // not with kidDirtyRect. I.e.
    // PRInt32 appUnitsPerDevPx = PresContext()->AppUnitsPerDevPixel();
    // mRect.ToOutsidePixels(appUnitsPerDevPx).Intersects(*aDirtyRect)
    if (kidDirtyRect.IsEmpty())
      return NS_OK;
  }

  gfxContext *gfx = aContext->ThebesContext();

  gfx->Save();

  if (GetStyleDisplay()->IsScrollableOverflow()) {
    float x, y, width, height;
    static_cast<nsSVGElement*>(mContent)->
      GetAnimatedLengthValues(&x, &y, &width, &height, nsnull);

    gfxRect clipRect =
      nsSVGUtils::GetClipRectForFrame(this, 0.0f, 0.0f, width, height);
    nsSVGUtils::SetClipRect(gfx, canvasTM, clipRect);
  }

  // SVG paints in CSS px, but normally frames paint in dev pixels. Here we
  // multiply a CSS-px-to-dev-pixel factor onto canvasTM so our children paint
  // correctly.
  float cssPxPerDevPx = PresContext()->
    AppUnitsToFloatCSSPixels(PresContext()->AppUnitsPerDevPixel());
  gfxMatrix canvasTMForChildren = canvasTM;
  canvasTMForChildren.Scale(cssPxPerDevPx, cssPxPerDevPx);

  gfx->Multiply(canvasTMForChildren);

  PRUint32 flags = nsLayoutUtils::PAINT_IN_TRANSFORM;
  if (SVGAutoRenderState::IsPaintingToWindow(aContext)) {
    flags |= nsLayoutUtils::PAINT_TO_WINDOW;
  }
  nsresult rv = nsLayoutUtils::PaintFrame(aContext, kid, nsRegion(kidDirtyRect),
                                          NS_RGBA(0,0,0,0), flags);

  gfx->Restore();

  return rv;
}

NS_IMETHODIMP_(nsIFrame*)
nsSVGForeignObjectFrame::GetFrameForPoint(const nsPoint &aPoint)
{
  if (IsDisabled() || (GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD))
    return nsnull;

  nsIFrame* kid = GetFirstPrincipalChild();
  if (!kid)
    return nsnull;

  float x, y, width, height;
  static_cast<nsSVGElement*>(mContent)->
    GetAnimatedLengthValues(&x, &y, &width, &height, nsnull);

  gfxMatrix tm = GetCanvasTM(FOR_HIT_TESTING).Invert();
  if (tm.IsSingular())
    return nsnull;
  
  // Convert aPoint from app units in canvas space to user space:

  gfxPoint pt = gfxPoint(aPoint.x, aPoint.y) / PresContext()->AppUnitsPerDevPixel();
  pt = tm.Transform(pt);

  if (!gfxRect(0.0f, 0.0f, width, height).Contains(pt))
    return nsnull;

  // Convert pt to app units in *local* space:

  pt = pt * nsPresContext::AppUnitsPerCSSPixel();
  nsPoint point = nsPoint(NSToIntRound(pt.x), NSToIntRound(pt.y));

  nsIFrame *frame = nsLayoutUtils::GetFrameForPoint(kid, point);
  if (frame && nsSVGUtils::HitTestClip(this, aPoint))
    return frame;

  return nsnull;
}

NS_IMETHODIMP_(nsRect)
nsSVGForeignObjectFrame::GetCoveredRegion()
{
  // See bug 614732 comment 32:
  //return nsSVGUtils::TransformFrameRectToOuterSVG(mRect, GetCanvasTM(), PresContext());
  return mCoveredRegion;
}

void
nsSVGForeignObjectFrame::UpdateBounds()
{
  NS_ASSERTION(nsSVGUtils::OuterSVGIsCallingUpdateBounds(this),
               "This call is probaby a wasteful mistake");

  NS_ABORT_IF_FALSE(!(GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD),
                    "UpdateBounds mechanism not designed for this");

  if (!nsSVGUtils::NeedsUpdatedBounds(this)) {
    return;
  }

  // We update mRect before the DoReflow call so that DoReflow uses the
  // correct dimensions:

  float x, y, w, h;
  static_cast<nsSVGForeignObjectElement*>(mContent)->
    GetAnimatedLengthValues(&x, &y, &w, &h, nsnull);

  // If mRect's width or height are negative, reflow blows up! We must clamp!
  if (w < 0.0f) w = 0.0f;
  if (h < 0.0f) h = 0.0f;

  mRect = nsLayoutUtils::RoundGfxRectToAppRect(
                           gfxRect(x, y, w, h),
                           PresContext()->AppUnitsPerCSSPixel());
  // GetCanvasTM includes the x,y translation
  mCoveredRegion = ToCanvasBounds(gfxRect(0.0, 0.0, w, h),
                     GetCanvasTM(FOR_OUTERSVG_TM), PresContext());

  // Since we'll invalidate our entire area at the end of this method, we
  // empty our cached dirty regions to prevent FlushDirtyRegion under DoReflow
  // from wasting time invalidating:
  mSameDocDirtyRegion.SetEmpty();
  mSubDocDirtyRegion.SetEmpty();

  // Fully mark our kid dirty so that it gets resized if necessary
  // (NS_FRAME_HAS_DIRTY_CHILDREN isn't enough in that case):
  nsIFrame* kid = GetFirstPrincipalChild();
  kid->AddStateBits(NS_FRAME_IS_DIRTY);

  // Make sure to not allow interrupts if we're not being reflown as a root:
  nsPresContext::InterruptPreventer noInterrupts(PresContext());

  DoReflow();

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

  // TODO: once we support |overflow:visible| on foreignObject, then we will
  // need to take account of our descendants here.
  nsRect overflow = nsRect(nsPoint(0,0), mRect.Size());
  nsOverflowAreas overflowAreas(overflow, overflow);
  FinishAndStoreOverflow(overflowAreas, mRect.Size());

  // Now unset the various reflow bits:
  mState &= ~(NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_DIRTY |
              NS_FRAME_HAS_DIRTY_CHILDREN);

  if (invalidate) {
    // XXXSDL Let FinishAndStoreOverflow do this.
    nsSVGUtils::InvalidateBounds(this, true);
  }
}

void
nsSVGForeignObjectFrame::NotifySVGChanged(PRUint32 aFlags)
{
  NS_ABORT_IF_FALSE(!(aFlags & DO_NOT_NOTIFY_RENDERING_OBSERVERS) ||
                    (GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD),
                    "Must be NS_STATE_SVG_NONDISPLAY_CHILD!");

  NS_ABORT_IF_FALSE(aFlags & (TRANSFORM_CHANGED | COORD_CONTEXT_CHANGED),
                    "Invalidation logic may need adjusting");

  bool needNewBounds = false; // i.e. mRect or visual overflow rect
  bool needReflow = false;
  bool needNewCanvasTM = false;

  if (aFlags & COORD_CONTEXT_CHANGED) {
    nsSVGForeignObjectElement *fO =
      static_cast<nsSVGForeignObjectElement*>(mContent);
    // Coordinate context changes affect mCanvasTM if we have a
    // percentage 'x' or 'y'
    if (fO->mLengthAttributes[nsSVGForeignObjectElement::X].IsPercentage() ||
        fO->mLengthAttributes[nsSVGForeignObjectElement::Y].IsPercentage()) {
      needNewBounds = true;
      needNewCanvasTM = true;
    }
    // Our coordinate context's width/height has changed. If we have a
    // percentage width/height our dimensions will change so we must reflow.
    if (fO->mLengthAttributes[nsSVGForeignObjectElement::WIDTH].IsPercentage() ||
        fO->mLengthAttributes[nsSVGForeignObjectElement::HEIGHT].IsPercentage()) {
      needNewBounds = true;
      needReflow = true;
    }
  }

  if (aFlags & TRANSFORM_CHANGED) {
    if (mCanvasTM && mCanvasTM->IsSingular()) {
      needNewBounds = true; // old bounds are bogus
    }
    needNewCanvasTM = true;
    // In an ideal world we would reflow when our CTM changes. This is because
    // glyph metrics do not necessarily scale uniformly with change in scale
    // and, as a result, CTM changes may require text to break at different
    // points. The problem would be how to keep performance acceptable when
    // e.g. the transform of an ancestor is animated.
    // We also seem to get some sort of infinite loop post bug 421584 if we
    // reflow.
  }

  if (needNewBounds) {
    // Ancestor changes can't affect how we render from the perspective of
    // any rendering observers that we may have, so we don't need to
    // invalidate them. We also don't need to invalidate ourself, since our
    // changed ancestor will have invalidated its entire area, which includes
    // our area.
    nsSVGUtils::ScheduleBoundsUpdate(this);
  }

  // If we're called while the PresShell is handling reflow events then we
  // must have been called as a result of the NotifyViewportChange() call in
  // our nsSVGOuterSVGFrame's Reflow() method. We must not call RequestReflow
  // at this point (i.e. during reflow) because it could confuse the
  // PresShell and prevent it from reflowing us properly in future. Besides
  // that, nsSVGOuterSVGFrame::DidReflow will take care of reflowing us
  // synchronously, so there's no need.
  if (needReflow && !PresContext()->PresShell()->IsReflowLocked()) {
    RequestReflow(nsIPresShell::eResize);
  }

  if (needNewCanvasTM) {
    // Do this after calling InvalidateAndScheduleBoundsUpdate in case we
    // change the code and it needs to use it.
    mCanvasTM = nsnull;
  }
}

SVGBBox
nsSVGForeignObjectFrame::GetBBoxContribution(const gfxMatrix &aToBBoxUserspace,
                                             PRUint32 aFlags)
{
  nsSVGForeignObjectElement *content =
    static_cast<nsSVGForeignObjectElement*>(mContent);

  float x, y, w, h;
  content->GetAnimatedLengthValues(&x, &y, &w, &h, nsnull);

  if (w < 0.0f) w = 0.0f;
  if (h < 0.0f) h = 0.0f;

  if (aToBBoxUserspace.IsSingular()) {
    // XXX ReportToConsole
    return SVGBBox();
  }
  return aToBBoxUserspace.TransformBounds(gfxRect(0.0, 0.0, w, h));
}

//----------------------------------------------------------------------

gfxMatrix
nsSVGForeignObjectFrame::GetCanvasTM(PRUint32 aFor)
{
  if (!(GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD)) {
    if ((aFor == FOR_PAINTING && NS_SVGDisplayListPaintingEnabled()) ||
        (aFor == FOR_HIT_TESTING && NS_SVGDisplayListHitTestingEnabled())) {
      return nsSVGIntegrationUtils::GetCSSPxToDevPxMatrix(this);
    }
  }
  if (!mCanvasTM) {
    NS_ASSERTION(mParent, "null parent");

    nsSVGContainerFrame *parent = static_cast<nsSVGContainerFrame*>(mParent);
    nsSVGForeignObjectElement *content =
      static_cast<nsSVGForeignObjectElement*>(mContent);

    gfxMatrix tm = content->PrependLocalTransformsTo(parent->GetCanvasTM(aFor));

    mCanvasTM = new gfxMatrix(tm);
  }
  return *mCanvasTM;
}

//----------------------------------------------------------------------
// Implementation helpers

void nsSVGForeignObjectFrame::RequestReflow(nsIPresShell::IntrinsicDirty aType)
{
  if (GetStateBits() & NS_FRAME_FIRST_REFLOW)
    // If we haven't had a UpdateBounds yet, nothing to do.
    return;

  nsIFrame* kid = GetFirstPrincipalChild();
  if (!kid)
    return;

  PresContext()->PresShell()->FrameNeedsReflow(kid, aType, NS_FRAME_IS_DIRTY);
}

void
nsSVGForeignObjectFrame::DoReflow()
{
  // Skip reflow if we're zero-sized, unless this is our first reflow.
  if (IsDisabled() &&
      !(GetStateBits() & NS_FRAME_FIRST_REFLOW))
    return;

  nsPresContext *presContext = PresContext();
  nsIFrame* kid = GetFirstPrincipalChild();
  if (!kid)
    return;

  // initiate a synchronous reflow here and now:  
  nsIPresShell* presShell = presContext->PresShell();
  NS_ASSERTION(presShell, "null presShell");
  nsRefPtr<nsRenderingContext> renderingContext =
    presShell->GetReferenceRenderingContext();
  if (!renderingContext)
    return;

  mInReflow = true;

  nsHTMLReflowState reflowState(presContext, kid,
                                renderingContext,
                                nsSize(mRect.width, NS_UNCONSTRAINEDSIZE));
  nsHTMLReflowMetrics desiredSize;
  nsReflowStatus status;

  // We don't use mRect.height above because that tells the child to do
  // page/column breaking at that height.
  NS_ASSERTION(reflowState.mComputedBorderPadding == nsMargin(0, 0, 0, 0) &&
               reflowState.mComputedMargin == nsMargin(0, 0, 0, 0),
               "style system should ensure that :-moz-svg-foreign-content "
               "does not get styled");
  NS_ASSERTION(reflowState.ComputedWidth() == mRect.width,
               "reflow state made child wrong size");
  reflowState.SetComputedHeight(mRect.height);

  ReflowChild(kid, presContext, desiredSize, reflowState, 0, 0,
              NS_FRAME_NO_MOVE_FRAME, status);
  NS_ASSERTION(mRect.width == desiredSize.width &&
               mRect.height == desiredSize.height, "unexpected size");
  FinishReflowChild(kid, presContext, &reflowState, desiredSize, 0, 0,
                    NS_FRAME_NO_MOVE_FRAME);
  
  mInReflow = false;

  if (!(GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD)) {
    // Since we're a reflow root and can be reflowed independently of our
    // outer-<svg>, we can't just blindly pass 'true' here.
    FlushDirtyRegion(0, nsSVGUtils::OuterSVGIsCallingUpdateBounds(this));
  }
}

void
nsSVGForeignObjectFrame::InvalidateDirtyRect(const nsRect& aRect,
                                             PRUint32 aFlags,
                                             bool aDuringReflowSVG)
{
  if (aRect.IsEmpty())
    return;

  // Don't invalidate areas outside our bounds:
  nsRect rect = aRect.Intersect(nsRect(nsPoint(0,0), mRect.Size()));
  if (rect.IsEmpty())
    return;

  nsSVGUtils::InvalidateBounds(this, aDuringReflowSVG, &rect, aFlags);
}

void
nsSVGForeignObjectFrame::FlushDirtyRegion(PRUint32 aFlags,
                                          bool aDuringReflowSVG)
{
  NS_ABORT_IF_FALSE(!(GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD),
                    "Should not have been called");

  NS_ASSERTION(!mInReflow,
               "We shouldn't be flushing while we have a pending flush");

  if (mSameDocDirtyRegion.IsEmpty() && mSubDocDirtyRegion.IsEmpty()) {
    return;
  }

  InvalidateDirtyRect(mSameDocDirtyRegion.GetBounds(),
                      aFlags,
                      aDuringReflowSVG);
  InvalidateDirtyRect(mSubDocDirtyRegion.GetBounds(),
                      aFlags | INVALIDATE_CROSS_DOC,
                      aDuringReflowSVG);

  mSameDocDirtyRegion.SetEmpty();
  mSubDocDirtyRegion.SetEmpty();
}
