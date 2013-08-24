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
 * Portions created by the Initial Developer are Copyright (C) 2001
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

#include "nsSVGForeignObjectFrame.h"

#include "nsIDOMSVGForeignObjectElem.h"
#include "nsIDOMSVGSVGElement.h"
#include "nsSVGOuterSVGFrame.h"
#include "nsRegion.h"
#include "nsGkAtoms.h"
#include "nsLayoutUtils.h"
#include "nsSVGUtils.h"
#include "nsIURI.h"
#include "nsSVGRect.h"
#include "nsSVGMatrix.h"
#include "nsINameSpaceManager.h"
#include "nsSVGForeignObjectElement.h"
#include "nsSVGContainerFrame.h"
#include "gfxContext.h"
#include "gfxMatrix.h"

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
    mInReflow(PR_FALSE)
{
  AddStateBits(NS_FRAME_REFLOW_ROOT | NS_FRAME_MAY_BE_TRANSFORMED);
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
  AddStateBits(NS_STATE_SVG_PROPAGATE_TRANSFORM | 
               (aParent->GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD));
  if (NS_SUCCEEDED(rv)) {
    nsSVGUtils::GetOuterSVGFrame(this)->RegisterForeignObject(this);
  }
  return rv;
}

void nsSVGForeignObjectFrame::DestroyFrom(nsIFrame* aDestructRoot)
{
  nsSVGUtils::GetOuterSVGFrame(this)->UnregisterForeignObject(this);
  nsSVGForeignObjectFrameBase::DestroyFrom(aDestructRoot);
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
      UpdateGraphic(); // update mRect before requesting reflow
      // XXXjwatt: why mark intrinsic widths dirty? can't we just use eResize?
      RequestReflow(nsIPresShell::eStyleChange);
    } else if (aAttribute == nsGkAtoms::x ||
               aAttribute == nsGkAtoms::y ||
               aAttribute == nsGkAtoms::transform) {
      // make sure our cached transform matrix gets (lazily) updated
      mCanvasTM = nsnull;
      UpdateGraphic();
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsSVGForeignObjectFrame::Reflow(nsPresContext*           aPresContext,
                                nsHTMLReflowMetrics&     aDesiredSize,
                                const nsHTMLReflowState& aReflowState,
                                nsReflowStatus&          aStatus)
{
  // InitialUpdate and AttributeChanged make sure mRect is up to date before
  // we're called (UpdateCoveredRegion sets mRect).

  NS_ASSERTION(!aReflowState.parentReflowState,
               "should only get reflow from being reflow root");
  NS_ASSERTION(aReflowState.ComputedWidth() == GetSize().width &&
               aReflowState.ComputedHeight() == GetSize().height,
               "reflow roots should be reflowed at existing size and "
               "svg.css should ensure we have no padding/border/margin");

  DoReflow();

  aDesiredSize.width = aReflowState.ComputedWidth();
  aDesiredSize.height = aReflowState.ComputedHeight();
  aDesiredSize.mOverflowArea =
    nsRect(0, 0, aReflowState.ComputedWidth(), aReflowState.ComputedHeight());
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

  if (GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD)
    return;

  nsRegion* region = (aFlags & INVALIDATE_CROSS_DOC)
    ? &mSubDocDirtyRegion : &mSameDocDirtyRegion;
  region->Or(*region, aDamageRect + nsPoint(aX, aY));
  FlushDirtyRegion();
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
nsSVGForeignObjectFrame::PaintSVG(nsSVGRenderState *aContext,
                                  const nsIntRect *aDirtyRect)
{
  if (IsDisabled())
    return NS_OK;

  nsIFrame* kid = GetFirstChild(nsnull);
  if (!kid)
    return NS_OK;

  gfxMatrix matrixForChildren = GetCanvasTMForChildren();
  gfxMatrix matrix = GetCanvasTM();

  nsIRenderingContext *ctx = aContext->GetRenderingContext(this);

  if (!ctx || matrixForChildren.IsSingular()) {
    NS_WARNING("Can't render foreignObject element!");
    return NS_ERROR_FAILURE;
  }

  /* Check if we need to draw anything. */
  if (aDirtyRect) {
    PRInt32 appUnitsPerDevPx = PresContext()->AppUnitsPerDevPixel();
    if (!mRect.ToOutsidePixels(appUnitsPerDevPx).Intersects(*aDirtyRect))
      return NS_OK;
  }

  gfxContext *gfx = aContext->GetGfxContext();

  gfx->Save();

  if (GetStyleDisplay()->IsScrollableOverflow()) {
    float x, y, width, height;
    static_cast<nsSVGElement*>(mContent)->
      GetAnimatedLengthValues(&x, &y, &width, &height, nsnull);

    gfxRect clipRect =
      nsSVGUtils::GetClipRectForFrame(this, 0.0f, 0.0f, width, height);
    nsSVGUtils::SetClipRect(gfx, matrix, clipRect);
  }

  gfx->Multiply(matrixForChildren);

  // Transform the dirty rect into the rectangle containing the
  // transformed dirty rect.
  gfxMatrix invmatrix = matrix.Invert();
  NS_ASSERTION(!invmatrix.IsSingular(),
               "inverse of non-singular matrix should be non-singular");

  gfxRect transDirtyRect = gfxRect(aDirtyRect->x, aDirtyRect->y,
                                   aDirtyRect->width, aDirtyRect->height);
  transDirtyRect = invmatrix.TransformBounds(transDirtyRect);

  transDirtyRect.Scale(nsPresContext::AppUnitsPerCSSPixel());
  nsPoint tl(NSToCoordFloor(transDirtyRect.X()),
             NSToCoordFloor(transDirtyRect.Y()));
  nsPoint br(NSToCoordCeil(transDirtyRect.XMost()),
             NSToCoordCeil(transDirtyRect.YMost()));
  nsRect kidDirtyRect(tl.x, tl.y, br.x - tl.x, br.y - tl.y);

  kidDirtyRect.IntersectRect(kidDirtyRect, kid->GetRect());

  nsresult rv = nsLayoutUtils::PaintFrame(ctx, kid, nsRegion(kidDirtyRect),
                                          NS_RGBA(0,0,0,0),
                                          nsLayoutUtils::PAINT_IN_TRANSFORM);

  gfx->Restore();

  return rv;
}

gfxMatrix
nsSVGForeignObjectFrame::GetTransformMatrix(nsIFrame **aOutAncestor)
{
  NS_PRECONDITION(aOutAncestor, "We need an ancestor to write to!");

  /* Set the ancestor to be the outer frame. */
  *aOutAncestor = nsSVGUtils::GetOuterSVGFrame(this);
  NS_ASSERTION(*aOutAncestor, "How did we end up without an outer frame?");

  /* Return the matrix back to the root, factoring in the x and y offsets. */
  return GetCanvasTMForChildren();
}
 
NS_IMETHODIMP_(nsIFrame*)
nsSVGForeignObjectFrame::GetFrameForPoint(const nsPoint &aPoint)
{
  if (IsDisabled() || (GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD))
    return nsnull;

  nsIFrame* kid = GetFirstChild(nsnull);
  if (!kid)
    return nsnull;

  float x, y, width, height;
  static_cast<nsSVGElement*>(mContent)->
    GetAnimatedLengthValues(&x, &y, &width, &height, nsnull);

  gfxMatrix tm = GetCanvasTM().Invert();
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
  return mRect;
}

NS_IMETHODIMP
nsSVGForeignObjectFrame::UpdateCoveredRegion()
{
  if (GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD)
    return NS_ERROR_FAILURE;

  float x, y, w, h;
  static_cast<nsSVGForeignObjectElement*>(mContent)->
    GetAnimatedLengthValues(&x, &y, &w, &h, nsnull);

  // If mRect's width or height are negative, reflow blows up! We must clamp!
  if (w < 0.0f) w = 0.0f;
  if (h < 0.0f) h = 0.0f;

  // GetCanvasTM includes the x,y translation
  mRect = ToCanvasBounds(gfxRect(0.0, 0.0, w, h), GetCanvasTM(), PresContext());
  
  return NS_OK;
}

NS_IMETHODIMP
nsSVGForeignObjectFrame::InitialUpdate()
{
  NS_ASSERTION(GetStateBits() & NS_FRAME_FIRST_REFLOW,
               "Yikes! We've been called already! Hopefully we weren't called "
               "before our nsSVGOuterSVGFrame's initial Reflow()!!!");

  UpdateCoveredRegion();

  // Make sure to not allow interrupts if we're not being reflown as a root
  nsPresContext::InterruptPreventer noInterrupts(PresContext());
  DoReflow();

  NS_ASSERTION(!(mState & NS_FRAME_IN_REFLOW),
               "We don't actually participate in reflow");
  
  // Do unset the various reflow bits, though.
  mState &= ~(NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_DIRTY |
              NS_FRAME_HAS_DIRTY_CHILDREN);

  return NS_OK;
}

void
nsSVGForeignObjectFrame::NotifySVGChanged(PRUint32 aFlags)
{
  PRBool reflow = PR_FALSE;

  if (aFlags & TRANSFORM_CHANGED) {
    // In an ideal world we would reflow when our CTM changes. This is because
    // glyph metrics do not necessarily scale uniformly with change in scale
    // and, as a result, CTM changes may require text to break at different
    // points. The problem would be how to keep performance acceptable when
    // e.g. the transform of an ancestor is animated.
    // We also seem to get some sort of infinite loop post bug 421584 if we
    // reflow.
    mCanvasTM = nsnull;
    if (!(aFlags & SUPPRESS_INVALIDATION)) {
      UpdateGraphic();
    }

  } else if (aFlags & COORD_CONTEXT_CHANGED) {
    // Our coordinate context's width/height has changed. If we have a
    // percentage width/height our dimensions will change so we must reflow.
    nsSVGForeignObjectElement *fO =
      static_cast<nsSVGForeignObjectElement*>(mContent);
    if (fO->mLengthAttributes[nsSVGForeignObjectElement::WIDTH].IsPercentage() ||
        fO->mLengthAttributes[nsSVGForeignObjectElement::HEIGHT].IsPercentage()) {
      reflow = PR_TRUE;
    }
  }

  if (reflow) {
    // If we're called while the PresShell is handling reflow events then we
    // must have been called as a result of the NotifyViewportChange() call in
    // our nsSVGOuterSVGFrame's Reflow() method. We must not call RequestReflow
    // at this point (i.e. during reflow) because it could confuse the
    // PresShell and prevent it from reflowing us properly in future. Besides
    // that, nsSVGOuterSVGFrame::DidReflow will take care of reflowing us
    // synchronously, so there's no need.
    if (!PresContext()->PresShell()->IsReflowLocked()) {
      UpdateGraphic(); // update mRect before requesting reflow
      RequestReflow(nsIPresShell::eResize);
    }
  }
}

NS_IMETHODIMP
nsSVGForeignObjectFrame::NotifyRedrawSuspended()
{
  return NS_OK;
}

NS_IMETHODIMP
nsSVGForeignObjectFrame::NotifyRedrawUnsuspended()
{
  if (!(GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD)) {
    if (GetStateBits() & NS_STATE_SVG_DIRTY) {
      UpdateGraphic(); // invalidate our entire area
    } else {
      FlushDirtyRegion(); // only invalidate areas dirtied by our descendants
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsSVGForeignObjectFrame::SetMatrixPropagation(PRBool aPropagate)
{
  if (aPropagate) {
    AddStateBits(NS_STATE_SVG_PROPAGATE_TRANSFORM);
  } else {
    RemoveStateBits(NS_STATE_SVG_PROPAGATE_TRANSFORM);
  }
  return NS_OK;
}

PRBool
nsSVGForeignObjectFrame::GetMatrixPropagation()
{
  return (GetStateBits() & NS_STATE_SVG_PROPAGATE_TRANSFORM) != 0;
}

gfxRect
nsSVGForeignObjectFrame::GetBBoxContribution(const gfxMatrix &aToBBoxUserspace)
{
  NS_ASSERTION(!(GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD),
               "Should not be calling this on a non-display child");

  nsSVGForeignObjectElement *content =
    static_cast<nsSVGForeignObjectElement*>(mContent);

  float x, y, w, h;
  content->GetAnimatedLengthValues(&x, &y, &w, &h, nsnull);

  if (w < 0.0f) w = 0.0f;
  if (h < 0.0f) h = 0.0f;

  if (aToBBoxUserspace.IsSingular()) {
    // XXX ReportToConsole
    return gfxRect(0.0, 0.0, 0.0, 0.0);
  }
  return aToBBoxUserspace.TransformBounds(gfxRect(0.0, 0.0, w, h));
}

//----------------------------------------------------------------------

gfxMatrix
nsSVGForeignObjectFrame::GetCanvasTM()
{
  if (!mCanvasTM) {
    NS_ASSERTION(mParent, "null parent");

    nsSVGContainerFrame *parent = static_cast<nsSVGContainerFrame*>(mParent);
    nsSVGForeignObjectElement *content =
      static_cast<nsSVGForeignObjectElement*>(mContent);

    gfxMatrix tm = content->PrependLocalTransformTo(parent->GetCanvasTM());

    mCanvasTM = NS_NewSVGMatrix(tm);
  }
  return nsSVGUtils::ConvertSVGMatrixToThebes(mCanvasTM);
}

//----------------------------------------------------------------------
// Implementation helpers

gfxMatrix
nsSVGForeignObjectFrame::GetCanvasTMForChildren()
{
  float cssPxPerDevPx = PresContext()->
    AppUnitsToFloatCSSPixels(PresContext()->AppUnitsPerDevPixel());

  return GetCanvasTM().Scale(cssPxPerDevPx, cssPxPerDevPx);
}

void nsSVGForeignObjectFrame::RequestReflow(nsIPresShell::IntrinsicDirty aType)
{
  if (GetStateBits() & NS_FRAME_FIRST_REFLOW)
    // If we haven't had an InitialUpdate yet, nothing to do.
    return;

  nsIFrame* kid = GetFirstChild(nsnull);
  if (!kid)
    return;

  PresContext()->PresShell()->FrameNeedsReflow(kid, aType, NS_FRAME_IS_DIRTY);
}

void nsSVGForeignObjectFrame::UpdateGraphic()
{
  nsSVGUtils::UpdateGraphic(this);

  // We just invalidated our entire area, so clear the caches of areas dirtied
  // by our descendants:
  mSameDocDirtyRegion.SetEmpty();
  mSubDocDirtyRegion.SetEmpty();
}

void
nsSVGForeignObjectFrame::MaybeReflowFromOuterSVGFrame()
{
  nsIFrame* kid = GetFirstChild(nsnull);

  // If we're already scheduled to reflow (if we or our kid is dirty) we don't
  // want to reflow now or else our presShell will do extra work trying to
  // reflow us a second time. (It will also complain if it finds that a reflow
  // root scheduled for reflow isn't dirty).

  if (kid->GetStateBits() & NS_FRAME_IS_DIRTY) {
    return;
  }
  kid->AddStateBits(NS_FRAME_IS_DIRTY); // we must be fully marked dirty
  if (kid->GetStateBits() & NS_FRAME_HAS_DIRTY_CHILDREN) {
    return;
  }

  // Make sure to not allow interrupts if we're not being reflown as a root
  nsPresContext::InterruptPreventer noInterrupts(PresContext());
  DoReflow();
}

void
nsSVGForeignObjectFrame::DoReflow()
{
#ifdef DEBUG
  printf("**nsSVGForeignObjectFrame::DoReflow()\n");
#endif

  NS_ASSERTION(!(nsSVGUtils::GetOuterSVGFrame(this)->
                             GetStateBits() & NS_FRAME_FIRST_REFLOW),
               "Calling InitialUpdate too early - must not call DoReflow!!!");

  if (IsDisabled())
    return;

  if (GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD)
    return;

  nsPresContext *presContext = PresContext();
  nsIFrame* kid = GetFirstChild(nsnull);
  if (!kid)
    return;

  // initiate a synchronous reflow here and now:  
  nsSize availableSpace(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  nsIPresShell* presShell = presContext->PresShell();
  NS_ASSERTION(presShell, "null presShell");
  nsCOMPtr<nsIRenderingContext> renderingContext =
    presShell->GetReferenceRenderingContext();
  if (!renderingContext)
    return;

  nsSVGForeignObjectElement *fO = static_cast<nsSVGForeignObjectElement*>
                                             (mContent);

  float width =
    fO->mLengthAttributes[nsSVGForeignObjectElement::WIDTH].GetAnimValue(fO);
  float height =
    fO->mLengthAttributes[nsSVGForeignObjectElement::HEIGHT].GetAnimValue(fO);

  nsSize size(nsPresContext::CSSPixelsToAppUnits(width),
              nsPresContext::CSSPixelsToAppUnits(height));

  mInReflow = PR_TRUE;

  nsHTMLReflowState reflowState(presContext, kid,
                                renderingContext,
                                nsSize(size.width, NS_UNCONSTRAINEDSIZE));
  nsHTMLReflowMetrics desiredSize;
  nsReflowStatus status;

  // We don't use size.height above because that tells the child to do
  // page/column breaking at that height.
  NS_ASSERTION(reflowState.mComputedBorderPadding == nsMargin(0, 0, 0, 0) &&
               reflowState.mComputedMargin == nsMargin(0, 0, 0, 0),
               "style system should ensure that :-moz-svg-foreign content "
               "does not get styled");
  NS_ASSERTION(reflowState.ComputedWidth() == size.width,
               "reflow state made child wrong size");
  reflowState.SetComputedHeight(size.height);
  
  ReflowChild(kid, presContext, desiredSize, reflowState, 0, 0,
              NS_FRAME_NO_MOVE_FRAME, status);
  NS_ASSERTION(size.width == desiredSize.width &&
               size.height == desiredSize.height, "unexpected size");
  FinishReflowChild(kid, presContext, &reflowState, desiredSize, 0, 0,
                    NS_FRAME_NO_MOVE_FRAME);
  
  mInReflow = PR_FALSE;
  FlushDirtyRegion();
}

void
nsSVGForeignObjectFrame::InvalidateDirtyRect(nsSVGOuterSVGFrame* aOuter,
    const nsRect& aRect, PRUint32 aFlags)
{
  if (aRect.IsEmpty())
    return;

  // The areas dirtied by children are in app units, relative to this frame.
  // We need to convert the rect to userspace to use IntersectRect.

  gfxRect r(aRect.x, aRect.y, aRect.width, aRect.height);
  r.Scale(1.0 / nsPresContext::AppUnitsPerCSSPixel());

  nsRect rect = ToCanvasBounds(r, GetCanvasTM(), PresContext());

  // Don't invalidate areas outside our bounds:
  rect.IntersectRect(rect, mRect);
  if (rect.IsEmpty())
    return;

  rect = nsSVGUtils::FindFilterInvalidation(this, rect);
  aOuter->InvalidateWithFlags(rect, aFlags);
}

void
nsSVGForeignObjectFrame::FlushDirtyRegion()
{
  if ((mSameDocDirtyRegion.IsEmpty() && mSubDocDirtyRegion.IsEmpty()) ||
      mInReflow)
    return;

  nsSVGOuterSVGFrame *outerSVGFrame = nsSVGUtils::GetOuterSVGFrame(this);
  if (!outerSVGFrame) {
    NS_ERROR("null outerSVGFrame");
    return;
  }

  if (outerSVGFrame->IsRedrawSuspended())
    return;

  InvalidateDirtyRect(outerSVGFrame, mSameDocDirtyRegion.GetBounds(), 0);
  InvalidateDirtyRect(outerSVGFrame, mSubDocDirtyRegion.GetBounds(), INVALIDATE_CROSS_DOC);

  mSameDocDirtyRegion.SetEmpty();
  mSubDocDirtyRegion.SetEmpty();
}
