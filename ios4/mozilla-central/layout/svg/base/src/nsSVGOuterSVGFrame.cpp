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

#include "nsSVGOuterSVGFrame.h"
#include "nsIDOMSVGSVGElement.h"
#include "nsSVGSVGElement.h"
#include "nsSVGTextFrame.h"
#include "nsSVGForeignObjectFrame.h"
#include "nsDisplayList.h"
#include "nsStubMutationObserver.h"
#include "gfxContext.h"
#include "nsIContentViewer.h"
#include "nsIDocShell.h"
#include "nsIDOMDocument.h"
#include "nsIDOMWindowInternal.h"
#include "nsPIDOMWindow.h"
#include "nsIObjectLoadingContent.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsSVGMatrix.h"

namespace dom = mozilla::dom;

class nsSVGMutationObserver : public nsStubMutationObserver
{
public:
  // nsIMutationObserver interface
  NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED

  // nsISupports interface:
  NS_IMETHOD QueryInterface(const nsIID& aIID, void** aInstancePtr);
private:
  NS_IMETHOD_(nsrefcnt) AddRef() { return 1; }
  NS_IMETHOD_(nsrefcnt) Release() { return 1; }

  static void UpdateTextFragmentTrees(nsIFrame *aFrame);
};

//----------------------------------------------------------------------
// nsISupports methods

NS_INTERFACE_MAP_BEGIN(nsSVGMutationObserver)
  NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
NS_INTERFACE_MAP_END

static nsSVGMutationObserver sSVGMutationObserver;

//----------------------------------------------------------------------
// nsIMutationObserver methods

void
nsSVGMutationObserver::AttributeChanged(nsIDocument* aDocument,
                                        dom::Element* aElement,
                                        PRInt32 aNameSpaceID,
                                        nsIAtom* aAttribute,
                                        PRInt32 aModType)
{
  if (aNameSpaceID != kNameSpaceID_XML || aAttribute != nsGkAtoms::space) {
    return;
  }

  nsIFrame* frame = aElement->GetPrimaryFrame();
  if (!frame) {
    return;
  }

  // is the content a child of a text element
  nsSVGTextContainerFrame* containerFrame = do_QueryFrame(frame);
  if (containerFrame) {
    containerFrame->NotifyGlyphMetricsChange();
    return;
  }
  // if not, are there text elements amongst its descendents
  UpdateTextFragmentTrees(frame);
}

//----------------------------------------------------------------------
// Implementation helpers

void
nsSVGMutationObserver::UpdateTextFragmentTrees(nsIFrame *aFrame)
{
  nsIFrame* kid = aFrame->GetFirstChild(nsnull);
  while (kid) {
    if (kid->GetType() == nsGkAtoms::svgTextFrame) {
      nsSVGTextFrame* textFrame = static_cast<nsSVGTextFrame*>(kid);
      textFrame->NotifyGlyphMetricsChange();
    } else {
      UpdateTextFragmentTrees(kid);
    }
    kid = kid->GetNextSibling();
  }
}

//----------------------------------------------------------------------
// Implementation

nsIFrame*
NS_NewSVGOuterSVGFrame(nsIPresShell* aPresShell, nsStyleContext* aContext)
{  
  return new (aPresShell) nsSVGOuterSVGFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsSVGOuterSVGFrame)

nsSVGOuterSVGFrame::nsSVGOuterSVGFrame(nsStyleContext* aContext)
    : nsSVGOuterSVGFrameBase(aContext)
    ,  mRedrawSuspendCount(0)
    , mFullZoom(0)
    , mViewportInitialized(PR_FALSE)
#ifdef XP_MACOSX
    , mEnableBitmapFallback(PR_FALSE)
#endif
    , mIsRootContent(PR_FALSE)
{
}

NS_IMETHODIMP
nsSVGOuterSVGFrame::Init(nsIContent* aContent,
                         nsIFrame* aParent,
                         nsIFrame* aPrevInFlow)
{
#ifdef DEBUG
  nsCOMPtr<nsIDOMSVGSVGElement> svgElement = do_QueryInterface(aContent);
  NS_ASSERTION(svgElement, "Content is not an SVG 'svg' element!");
#endif

  AddStateBits(NS_STATE_IS_OUTER_SVG);

  nsresult rv = nsSVGOuterSVGFrameBase::Init(aContent, aParent, aPrevInFlow);

  nsIDocument* doc = mContent->GetCurrentDoc();
  if (doc) {
    // we only care about our content's zoom and pan values if it's the root element
    if (doc->GetRootElement() == mContent) {
      mIsRootContent = PR_TRUE;
    }
    // sSVGMutationObserver has the same lifetime as the document so does
    // not need to be removed
    doc->AddMutationObserverUnlessExists(&sSVGMutationObserver);
  }

  SuspendRedraw();  // UnsuspendRedraw is in DidReflow

  return rv;
}

//----------------------------------------------------------------------
// nsQueryFrame methods

NS_QUERYFRAME_HEAD(nsSVGOuterSVGFrame)
  NS_QUERYFRAME_ENTRY(nsISVGSVGFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsSVGOuterSVGFrameBase)

//----------------------------------------------------------------------
// nsIFrame methods
  
//----------------------------------------------------------------------
// reflowing

/* virtual */ nscoord
nsSVGOuterSVGFrame::GetMinWidth(nsIRenderingContext *aRenderingContext)
{
  nscoord result;
  DISPLAY_MIN_WIDTH(this, result);

  result = nscoord(0);

  return result;
}

/* virtual */ nscoord
nsSVGOuterSVGFrame::GetPrefWidth(nsIRenderingContext *aRenderingContext)
{
  nscoord result;
  DISPLAY_PREF_WIDTH(this, result);

  nsSVGSVGElement *svg = static_cast<nsSVGSVGElement*>(mContent);
  nsSVGLength2 &width = svg->mLengthAttributes[nsSVGSVGElement::WIDTH];

  if (width.IsPercentage()) {
    // It looks like our containing block's width may depend on our width. In
    // that case our behavior is undefined according to CSS 2.1 section 10.3.2,
    // so return zero.
    result = nscoord(0);
  } else {
    result = nsPresContext::CSSPixelsToAppUnits(width.GetAnimValue(svg));
    if (result < 0) {
      result = nscoord(0);
    }
  }

  return result;
}

/* virtual */ nsIFrame::IntrinsicSize
nsSVGOuterSVGFrame::GetIntrinsicSize()
{
  // XXXjwatt Note that here we want to return the CSS width/height if they're
  // specified and we're embedded inside an nsIObjectLoadingContent.

  IntrinsicSize intrinsicSize;

  nsSVGSVGElement *content = static_cast<nsSVGSVGElement*>(mContent);
  nsSVGLength2 &width  = content->mLengthAttributes[nsSVGSVGElement::WIDTH];
  nsSVGLength2 &height = content->mLengthAttributes[nsSVGSVGElement::HEIGHT];

  if (width.IsPercentage()) {
    float val = width.GetAnimValInSpecifiedUnits() / 100.0f;
    if (val < 0.0f) val = 0.0f;
    intrinsicSize.width.SetPercentValue(val);
  } else {
    nscoord val = nsPresContext::CSSPixelsToAppUnits(width.GetAnimValue(content));
    if (val < 0) val = 0;
    intrinsicSize.width.SetCoordValue(val);
  }

  if (height.IsPercentage()) {
    float val = height.GetAnimValInSpecifiedUnits() / 100.0f;
    if (val < 0.0f) val = 0.0f;
    intrinsicSize.height.SetPercentValue(val);
  } else {
    nscoord val = nsPresContext::CSSPixelsToAppUnits(height.GetAnimValue(content));
    if (val < 0) val = 0;
    intrinsicSize.height.SetCoordValue(val);
  }

  return intrinsicSize;
}

/* virtual */ nsSize
nsSVGOuterSVGFrame::GetIntrinsicRatio()
{
  // We only have an intrinsic size/ratio if our width and height attributes
  // are both specified and set to non-percentage values, or we have a viewBox
  // rect: http://www.w3.org/TR/SVGMobile12/coords.html#IntrinsicSizing

  nsSVGSVGElement *content = static_cast<nsSVGSVGElement*>(mContent);
  nsSVGLength2 &width  = content->mLengthAttributes[nsSVGSVGElement::WIDTH];
  nsSVGLength2 &height = content->mLengthAttributes[nsSVGSVGElement::HEIGHT];

  if (!width.IsPercentage() && !height.IsPercentage()) {
    nsSize ratio(width.GetAnimValue(content), height.GetAnimValue(content));
    if (ratio.width < 0) {
      ratio.width = 0;
    }
    if (ratio.height < 0) {
      ratio.height = 0;
    }
    return ratio;
  }

  if (content->mViewBox.IsValid()) {
    const nsSVGViewBoxRect viewbox = content->mViewBox.GetAnimValue();
    float viewBoxWidth = viewbox.width;
    float viewBoxHeight = viewbox.height;

    if (viewBoxWidth < 0.0f) {
      viewBoxWidth = 0.0f;
    }
    if (viewBoxHeight < 0.0f) {
      viewBoxHeight = 0.0f;
    }
    return nsSize(viewBoxWidth, viewBoxHeight);
  }

  return nsSVGOuterSVGFrameBase::GetIntrinsicRatio();
}

/* virtual */ nsSize
nsSVGOuterSVGFrame::ComputeSize(nsIRenderingContext *aRenderingContext,
                                nsSize aCBSize, nscoord aAvailableWidth,
                                nsSize aMargin, nsSize aBorder, nsSize aPadding,
                                PRBool aShrinkWrap)
{
  if (mContent->HasAttr(kNameSpaceID_None, nsGkAtoms::viewBox) &&
      EmbeddedByReference()) {
    // The embedding element has done the replaced element sizing, using our
    // intrinsic dimensions as necessary. We just need to fill the viewport.
    return aCBSize;
  }

  return nsLayoutUtils::ComputeSizeWithIntrinsicDimensions(
                            aRenderingContext, this,
                            GetIntrinsicSize(), GetIntrinsicRatio(), aCBSize,
                            aMargin, aBorder, aPadding);
}

NS_IMETHODIMP
nsSVGOuterSVGFrame::Reflow(nsPresContext*           aPresContext,
                           nsHTMLReflowMetrics&     aDesiredSize,
                           const nsHTMLReflowState& aReflowState,
                           nsReflowStatus&          aStatus)
{
  DO_GLOBAL_REFLOW_COUNT("nsSVGOuterSVGFrame");
  DISPLAY_REFLOW(aPresContext, this, aReflowState, aDesiredSize, aStatus);
  NS_FRAME_TRACE(NS_FRAME_TRACE_CALLS,
                  ("enter nsSVGOuterSVGFrame::Reflow: availSize=%d,%d",
                  aReflowState.availableWidth, aReflowState.availableHeight));

  NS_PRECONDITION(mState & NS_FRAME_IN_REFLOW, "frame is not in reflow");

  aStatus = NS_FRAME_COMPLETE;

  aDesiredSize.width  = aReflowState.ComputedWidth() +
                          aReflowState.mComputedBorderPadding.LeftRight();
  aDesiredSize.height = aReflowState.ComputedHeight() +
                          aReflowState.mComputedBorderPadding.TopBottom();

  NS_ASSERTION(!GetPrevInFlow(), "SVG can't currently be broken across pages.");

  // Make sure we scroll if we're too big:
  // XXX Use the bounding box of our descendants? (See bug 353460 comment 14.)
  aDesiredSize.mOverflowArea.SetRect(0, 0, aDesiredSize.width, aDesiredSize.height);
  FinishAndStoreOverflow(&aDesiredSize);

  // If our SVG viewport has changed, update our content and notify.
  // http://www.w3.org/TR/SVG11/coords.html#ViewportSpace

  svgFloatSize newViewportSize(
    nsPresContext::AppUnitsToFloatCSSPixels(aReflowState.ComputedWidth()),
    nsPresContext::AppUnitsToFloatCSSPixels(aReflowState.ComputedHeight()));

  nsSVGSVGElement *svgElem = static_cast<nsSVGSVGElement*>(mContent);

  if (newViewportSize != svgElem->GetViewportSize() ||
      mFullZoom != PresContext()->GetFullZoom()) {
    svgElem->SetViewportSize(newViewportSize);
    mViewportInitialized = PR_TRUE;
    mFullZoom = PresContext()->GetFullZoom();
    NotifyViewportChange();
  }

  NS_FRAME_TRACE(NS_FRAME_TRACE_CALLS,
                  ("exit nsSVGOuterSVGFrame::Reflow: size=%d,%d",
                  aDesiredSize.width, aDesiredSize.height));
  NS_FRAME_SET_TRUNCATION(aStatus, aReflowState, aDesiredSize);
  return NS_OK;
}

static PLDHashOperator
ReflowForeignObject(nsVoidPtrHashKey *aEntry, void* aUserArg)
{
  static_cast<nsSVGForeignObjectFrame*>
    (const_cast<void*>(aEntry->GetKey()))->MaybeReflowFromOuterSVGFrame();
  return PL_DHASH_NEXT;
}

NS_IMETHODIMP
nsSVGOuterSVGFrame::DidReflow(nsPresContext*   aPresContext,
                              const nsHTMLReflowState*  aReflowState,
                              nsDidReflowStatus aStatus)
{
  PRBool firstReflow = (GetStateBits() & NS_FRAME_FIRST_REFLOW) != 0;

  nsresult rv = nsSVGOuterSVGFrameBase::DidReflow(aPresContext,aReflowState,aStatus);

  if (firstReflow) {
    // call InitialUpdate() on all frames:
    nsIFrame* kid = mFrames.FirstChild();
    while (kid) {
      nsISVGChildFrame* SVGFrame = do_QueryFrame(kid);
      if (SVGFrame) {
        SVGFrame->InitialUpdate(); 
      }
      kid = kid->GetNextSibling();
    }
    
    UnsuspendRedraw(); // For the SuspendRedraw in InitSVG
  } else {
    // Now that all viewport establishing descendants have their correct size,
    // tell our foreignObject descendants to reflow their children.
    if (mForeignObjectHash.IsInitialized()) {
#ifdef DEBUG
      PRUint32 count =
#endif
        mForeignObjectHash.EnumerateEntries(ReflowForeignObject, nsnull);
      NS_ASSERTION(count == mForeignObjectHash.Count(),
                   "We didn't reflow all our nsSVGForeignObjectFrames!");
    }
  }
  
  return rv;
}

//----------------------------------------------------------------------
// container methods

class nsDisplaySVG : public nsDisplayItem {
public:
  nsDisplaySVG(nsDisplayListBuilder* aBuilder,
               nsSVGOuterSVGFrame* aFrame) :
    nsDisplayItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplaySVG);
  }
#ifdef NS_BUILD_REFCNT_LOGGING
  virtual ~nsDisplaySVG() {
    MOZ_COUNT_DTOR(nsDisplaySVG);
  }
#endif

  virtual void HitTest(nsDisplayListBuilder* aBuilder, const nsRect& aRect,
                       HitTestState* aState, nsTArray<nsIFrame*> *aOutFrames);
  virtual void Paint(nsDisplayListBuilder* aBuilder,
                     nsIRenderingContext* aCtx);
  NS_DISPLAY_DECL_NAME("SVGEventReceiver", TYPE_SVG_EVENT_RECEIVER)
};

void
nsDisplaySVG::HitTest(nsDisplayListBuilder* aBuilder, const nsRect& aRect,
                      HitTestState* aState, nsTArray<nsIFrame*> *aOutFrames)
{
  nsSVGOuterSVGFrame *outerSVGFrame = static_cast<nsSVGOuterSVGFrame*>(mFrame);
  nsRect rectAtOrigin = aRect - ToReferenceFrame();
  nsRect thisRect(nsPoint(0,0), outerSVGFrame->GetSize());
  if (!thisRect.Intersects(rectAtOrigin))
    return;

  nsPoint rectCenter(rectAtOrigin.x + rectAtOrigin.width / 2,
                     rectAtOrigin.y + rectAtOrigin.height / 2);

  nsIFrame* frame = nsSVGUtils::HitTestChildren(
    outerSVGFrame, rectCenter + outerSVGFrame->GetPosition() -
                   outerSVGFrame->GetContentRect().TopLeft());
  if (frame) {
    aOutFrames->AppendElement(frame);
  }
}

void
nsDisplaySVG::Paint(nsDisplayListBuilder* aBuilder,
                    nsIRenderingContext* aCtx)
{
  static_cast<nsSVGOuterSVGFrame*>(mFrame)->
    Paint(*aCtx, mVisibleRect, ToReferenceFrame());
}

// helper
static inline PRBool
DependsOnIntrinsicSize(const nsIFrame* aEmbeddingFrame)
{
  const nsStylePosition *pos = aEmbeddingFrame->GetStylePosition();
  const nsStyleCoord &width = pos->mWidth;
  const nsStyleCoord &height = pos->mHeight;

  // XXX it would be nice to know if the size of aEmbeddingFrame's containing
  // block depends on aEmbeddingFrame, then we'd know if we can return false
  // for eStyleUnit_Percent too.
  return !width.ConvertsToLength() ||
         !height.ConvertsToLength();
}

NS_IMETHODIMP
nsSVGOuterSVGFrame::AttributeChanged(PRInt32  aNameSpaceID,
                                     nsIAtom* aAttribute,
                                     PRInt32  aModType)
{
  if (aNameSpaceID == kNameSpaceID_None &&
      !(GetStateBits() & NS_FRAME_FIRST_REFLOW) &&
      (aAttribute == nsGkAtoms::width || aAttribute == nsGkAtoms::height)) {
    nsIFrame* embeddingFrame;
    EmbeddedByReference(&embeddingFrame);
    if (embeddingFrame) {
      if (DependsOnIntrinsicSize(embeddingFrame)) {
        // Tell embeddingFrame's presShell it needs to be reflowed (which takes
        // care of reflowing us too).
        embeddingFrame->PresContext()->PresShell()->
          FrameNeedsReflow(embeddingFrame, nsIPresShell::eStyleChange, NS_FRAME_IS_DIRTY);
      }
      // else our width and height is overridden - don't reflow anything
    } else {
      // We are not embedded by reference, so our 'width' and 'height'
      // attributes are not overridden - we need to reflow.
      PresContext()->PresShell()->
        FrameNeedsReflow(this, nsIPresShell::eStyleChange, NS_FRAME_IS_DIRTY);
    }
  }

  return NS_OK;
}

nsIFrame*
nsSVGOuterSVGFrame::GetFrameForPoint(const nsPoint& aPoint)
{
  nsRect thisRect(nsPoint(0,0), GetSize());
  if (!thisRect.Contains(aPoint)) {
    return nsnull;
  }

  return nsSVGUtils::HitTestChildren(
    this, aPoint + GetPosition() - GetContentRect().TopLeft());
}

//----------------------------------------------------------------------
// painting

NS_IMETHODIMP
nsSVGOuterSVGFrame::BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                                     const nsRect&           aDirtyRect,
                                     const nsDisplayListSet& aLists)
{
  nsresult rv = DisplayBorderBackgroundOutline(aBuilder, aLists);
  NS_ENSURE_SUCCESS(rv, rv);

  return aLists.Content()->AppendNewToTop(
      new (aBuilder) nsDisplaySVG(aBuilder, this));
}

void
nsSVGOuterSVGFrame::Paint(nsIRenderingContext& aRenderingContext,
                          const nsRect& aDirtyRect, nsPoint aPt)
{
  // initialize Mozilla rendering context
  aRenderingContext.PushState();

  nsRect viewportRect = GetContentRect();
  nsPoint viewportOffset = aPt + viewportRect.TopLeft() - GetPosition();
  viewportRect.MoveTo(viewportOffset);

  nsRect clipRect;
  clipRect.IntersectRect(aDirtyRect, viewportRect);
  aRenderingContext.SetClipRect(clipRect, nsClipCombine_kIntersect);
  aRenderingContext.Translate(viewportRect.x, viewportRect.y);
  nsRect dirtyRect = clipRect - viewportOffset;

#if defined(DEBUG) && defined(SVG_DEBUG_PAINT_TIMING)
  PRTime start = PR_Now();
#endif

  nsIntRect dirtyPxRect = dirtyRect.ToOutsidePixels(PresContext()->AppUnitsPerDevPixel());

  nsSVGRenderState ctx(&aRenderingContext);

#ifdef XP_MACOSX
  if (mEnableBitmapFallback) {
    // nquartz fallback paths, which svg tends to trigger, need
    // a non-window context target
    ctx.GetGfxContext()->PushGroup(gfxASurface::CONTENT_COLOR_ALPHA);
  }
#endif

  nsSVGUtils::PaintFrameWithEffects(&ctx, &dirtyPxRect, this);

#ifdef XP_MACOSX
  if (mEnableBitmapFallback) {
    // show the surface we pushed earlier for fallbacks
    ctx.GetGfxContext()->PopGroupToSource();
    ctx.GetGfxContext()->Paint();
  }
  
  if (ctx.GetGfxContext()->HasError() && !mEnableBitmapFallback) {
    mEnableBitmapFallback = PR_TRUE;
    // It's not really clear what area to invalidate here. We might have
    // stuffed up rendering for the entire window in this paint pass,
    // so we can't just invalidate our own rect. Invalidate everything
    // in sight.
    // This won't work for printing, by the way, but failure to print the
    // odd document is probably no worse than printing horribly for all
    // documents. Better to fix things so we don't need fallback.
    nsIFrame* frame = this;
    PRUint32 flags = 0;
    while (PR_TRUE) {
      nsIFrame* next = nsLayoutUtils::GetCrossDocParentFrame(frame);
      if (!next)
        break;
      if (frame->GetParent() != next) {
        // We're crossing a document boundary. Logically, the invalidation is
        // being triggered by a subdocument of the root document. This will
        // prevent an untrusted root document being told about invalidation
        // that happened because a child was using SVG...
        flags |= INVALIDATE_CROSS_DOC;
      }
      frame = next;
    }
    frame->InvalidateWithFlags(nsRect(nsPoint(0, 0), frame->GetSize()), flags);
  }
#endif

#if defined(DEBUG) && defined(SVG_DEBUG_PAINT_TIMING)
  PRTime end = PR_Now();
  printf("SVG Paint Timing: %f ms\n", (end-start)/1000.0);
#endif
  
  aRenderingContext.PopState();
}

nsSplittableType
nsSVGOuterSVGFrame::GetSplittableType() const
{
  return NS_FRAME_NOT_SPLITTABLE;
}

nsIAtom *
nsSVGOuterSVGFrame::GetType() const
{
  return nsGkAtoms::svgOuterSVGFrame;
}

//----------------------------------------------------------------------
// nsSVGOuterSVGFrame methods:

void
nsSVGOuterSVGFrame::InvalidateCoveredRegion(nsIFrame *aFrame)
{
  nsISVGChildFrame *svgFrame = do_QueryFrame(aFrame);
  if (!svgFrame)
    return;

  nsRect rect = nsSVGUtils::FindFilterInvalidation(aFrame, svgFrame->GetCoveredRegion());
  Invalidate(rect);
}

PRBool
nsSVGOuterSVGFrame::UpdateAndInvalidateCoveredRegion(nsIFrame *aFrame)
{
  nsISVGChildFrame *svgFrame = do_QueryFrame(aFrame);
  if (!svgFrame)
    return PR_FALSE;

  nsRect oldRegion = svgFrame->GetCoveredRegion();
  Invalidate(nsSVGUtils::FindFilterInvalidation(aFrame, oldRegion));
  svgFrame->UpdateCoveredRegion();
  nsRect newRegion = svgFrame->GetCoveredRegion();
  if (oldRegion == newRegion)
    return PR_FALSE;

  Invalidate(nsSVGUtils::FindFilterInvalidation(aFrame, newRegion));
  return PR_TRUE;
}

PRBool
nsSVGOuterSVGFrame::IsRedrawSuspended()
{
  return (mRedrawSuspendCount>0) || !mViewportInitialized;
}

//----------------------------------------------------------------------
// nsISVGSVGFrame methods:


NS_IMETHODIMP
nsSVGOuterSVGFrame::SuspendRedraw()
{
#ifdef DEBUG
  //printf("suspend redraw (count=%d)\n", mRedrawSuspendCount);
#endif
  if (++mRedrawSuspendCount != 1)
    return NS_OK;

  for (nsIFrame* kid = mFrames.FirstChild(); kid;
       kid = kid->GetNextSibling()) {
    nsISVGChildFrame* SVGFrame = do_QueryFrame(kid);
    if (SVGFrame) {
      SVGFrame->NotifyRedrawSuspended();
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsSVGOuterSVGFrame::UnsuspendRedraw()
{
#ifdef DEBUG
//  printf("unsuspend redraw (count=%d)\n", mRedrawSuspendCount);
#endif

  NS_ASSERTION(mRedrawSuspendCount >=0, "unbalanced suspend count!");

  if (--mRedrawSuspendCount > 0)
    return NS_OK;

  for (nsIFrame* kid = mFrames.FirstChild(); kid;
       kid = kid->GetNextSibling()) {
    nsISVGChildFrame* SVGFrame = do_QueryFrame(kid);
    if (SVGFrame) {
      SVGFrame->NotifyRedrawUnsuspended();
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsSVGOuterSVGFrame::NotifyViewportChange()
{
  // no point in doing anything when were not init'ed yet:
  if (!mViewportInitialized) {
    return NS_OK;
  }

  PRUint32 flags = COORD_CONTEXT_CHANGED;

  // viewport changes only affect our transform if we have a viewBox attribute
#if 1
  {
#else
  // XXX this caused reftest failures (bug 413960)
  if (mContent->HasAttr(kNameSpaceID_None, nsGkAtoms::viewBox)) {
#endif
    // make sure canvas transform matrix gets (lazily) recalculated:
    mCanvasTM = nsnull;

    flags |= TRANSFORM_CHANGED;
  }

  // inform children
  SuspendRedraw();
  nsSVGUtils::NotifyChildrenOfSVGChange(this, flags);
  UnsuspendRedraw();
  return NS_OK;
}

//----------------------------------------------------------------------
// nsSVGContainerFrame methods:

gfxMatrix
nsSVGOuterSVGFrame::GetCanvasTM()
{
  if (!mCanvasTM) {
    nsSVGSVGElement *content = static_cast<nsSVGSVGElement*>(mContent);

    float devPxPerCSSPx =
      1.0f / PresContext()->AppUnitsToFloatCSSPixels(
                                PresContext()->AppUnitsPerDevPixel());

    gfxMatrix viewBoxTM = content->GetViewBoxTransform();

    gfxMatrix zoomPanTM;
    if (mIsRootContent) {
      const nsSVGTranslatePoint& translate = content->GetCurrentTranslate();
      zoomPanTM.Translate(gfxPoint(translate.GetX(), translate.GetY()));
      zoomPanTM.Scale(content->GetCurrentScale(), content->GetCurrentScale());
    }

    gfxMatrix TM = viewBoxTM * zoomPanTM * gfxMatrix().Scale(devPxPerCSSPx, devPxPerCSSPx);
    mCanvasTM = NS_NewSVGMatrix(TM);
  }
  return nsSVGUtils::ConvertSVGMatrixToThebes(mCanvasTM);
}

//----------------------------------------------------------------------
// Implementation helpers

void
nsSVGOuterSVGFrame::RegisterForeignObject(nsSVGForeignObjectFrame* aFrame)
{
  NS_ASSERTION(aFrame, "Who on earth is calling us?!");

  if (!mForeignObjectHash.IsInitialized()) {
    if (!mForeignObjectHash.Init()) {
      NS_ERROR("Failed to initialize foreignObject hash.");
      return;
    }
  }

  NS_ASSERTION(!mForeignObjectHash.GetEntry(aFrame),
               "nsSVGForeignObjectFrame already registered!");

  mForeignObjectHash.PutEntry(aFrame);

  NS_ASSERTION(mForeignObjectHash.GetEntry(aFrame),
               "Failed to register nsSVGForeignObjectFrame!");
}

void
nsSVGOuterSVGFrame::UnregisterForeignObject(nsSVGForeignObjectFrame* aFrame)
{
  NS_ASSERTION(aFrame, "Who on earth is calling us?!");
  NS_ASSERTION(mForeignObjectHash.GetEntry(aFrame),
               "nsSVGForeignObjectFrame not in registry!");
  return mForeignObjectHash.RemoveEntry(aFrame);
}

PRBool
nsSVGOuterSVGFrame::EmbeddedByReference(nsIFrame **aEmbeddingFrame)
{
  if (mContent->GetParent() == nsnull) {
    // Our content is the document element
    nsCOMPtr<nsISupports> container = PresContext()->GetContainer();
    nsCOMPtr<nsIDOMWindowInternal> window = do_GetInterface(container);
    if (window) {
      nsCOMPtr<nsIDOMElement> frameElement;
      window->GetFrameElement(getter_AddRefs(frameElement));
      nsCOMPtr<nsIObjectLoadingContent> olc = do_QueryInterface(frameElement);
      if (olc) {
        // Our document is inside an HTML 'object', 'embed' or 'applet' element
        if (aEmbeddingFrame) {
          nsCOMPtr<nsIContent> element = do_QueryInterface(frameElement);
          *aEmbeddingFrame =
            static_cast<nsGenericElement*>(element.get())->GetPrimaryFrame();
          NS_ASSERTION(*aEmbeddingFrame, "Yikes, no embedding frame!");
        }
        return PR_TRUE;
      }
    }
  }
  if (aEmbeddingFrame) {
    *aEmbeddingFrame = nsnull;
  }
  return PR_FALSE;
}
