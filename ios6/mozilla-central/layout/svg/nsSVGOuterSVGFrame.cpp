/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "nsSVGOuterSVGFrame.h"

// Keep others in (case-insensitive) order:
#include "DOMSVGTests.h"
#include "gfxMatrix.h"
#include "nsDisplayList.h"
#include "nsIDocument.h"
#include "nsIDOMSVGSVGElement.h"
#include "nsIDOMWindow.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIObjectLoadingContent.h"
#include "nsRenderingContext.h"
#include "nsStubMutationObserver.h"
#include "nsSVGIntegrationUtils.h"
#include "nsSVGForeignObjectFrame.h"
#include "nsSVGSVGElement.h"
#include "nsSVGTextFrame.h"
#include "nsSVGViewElement.h"
#include "nsSubDocumentFrame.h"

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
                                        int32_t aNameSpaceID,
                                        nsIAtom* aAttribute,
                                        int32_t aModType)
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
nsSVGOuterSVGFrame::RegisterForeignObject(nsSVGForeignObjectFrame* aFrame)
{
  NS_ASSERTION(aFrame, "Who on earth is calling us?!");

  if (!mForeignObjectHash.IsInitialized()) {
    mForeignObjectHash.Init();
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

void
nsSVGMutationObserver::UpdateTextFragmentTrees(nsIFrame *aFrame)
{
  nsIFrame* kid = aFrame->GetFirstPrincipalChild();
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
    , mFullZoom(aContext->PresContext()->GetFullZoom())
    , mViewportInitialized(false)
    , mIsRootContent(false)
{
  // Outer-<svg> has CSS layout, so remove this bit:
  RemoveStateBits(NS_FRAME_SVG_LAYOUT);
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

  AddStateBits(NS_STATE_IS_OUTER_SVG |
               NS_FRAME_FONT_INFLATION_CONTAINER |
               NS_FRAME_FONT_INFLATION_FLOW_ROOT);

  // Check for conditional processing attributes here rather than in
  // nsCSSFrameConstructor::FindSVGData because we want to avoid
  // simply giving failing outer <svg> elements an nsSVGContainerFrame.
  // We don't create other SVG frames if PassesConditionalProcessingTests
  // returns false, but since we do create nsSVGOuterSVGFrame frames we
  // prevent them from painting by [ab]use NS_STATE_SVG_NONDISPLAY_CHILD. The
  // frame will be recreated via an nsChangeHint_ReconstructFrame restyle if
  // the value returned by PassesConditionalProcessingTests changes.
  nsSVGSVGElement *svg = static_cast<nsSVGSVGElement*>(aContent);
  if (!svg->PassesConditionalProcessingTests()) {
    AddStateBits(NS_STATE_SVG_NONDISPLAY_CHILD);
  }

  nsresult rv = nsSVGOuterSVGFrameBase::Init(aContent, aParent, aPrevInFlow);

  nsIDocument* doc = mContent->GetCurrentDoc();
  if (doc) {
    // we only care about our content's zoom and pan values if it's the root element
    if (doc->GetRootElement() == mContent) {
      mIsRootContent = true;
    }
    // sSVGMutationObserver has the same lifetime as the document so does
    // not need to be removed
    doc->AddMutationObserverUnlessExists(&sSVGMutationObserver);
  }

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
nsSVGOuterSVGFrame::GetMinWidth(nsRenderingContext *aRenderingContext)
{
  nscoord result;
  DISPLAY_MIN_WIDTH(this, result);

  result = nscoord(0);

  return result;
}

/* virtual */ nscoord
nsSVGOuterSVGFrame::GetPrefWidth(nsRenderingContext *aRenderingContext)
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

  if (!width.IsPercentage()) {
    nscoord val = nsPresContext::CSSPixelsToAppUnits(width.GetAnimValue(content));
    if (val < 0) val = 0;
    intrinsicSize.width.SetCoordValue(val);
  }

  if (!height.IsPercentage()) {
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
    nsSize ratio(NSToCoordRoundWithClamp(width.GetAnimValue(content)),
                 NSToCoordRoundWithClamp(height.GetAnimValue(content)));
    if (ratio.width < 0) {
      ratio.width = 0;
    }
    if (ratio.height < 0) {
      ratio.height = 0;
    }
    return ratio;
  }

  nsSVGViewElement* viewElement = content->GetCurrentViewElement();
  const nsSVGViewBoxRect* viewbox = nullptr;

  // The logic here should match HasViewBox().
  if (viewElement && viewElement->mViewBox.IsExplicitlySet()) {
    viewbox = &viewElement->mViewBox.GetAnimValue();
  } else if (content->mViewBox.IsExplicitlySet()) {
    viewbox = &content->mViewBox.GetAnimValue();
  }

  if (viewbox) {
    float viewBoxWidth = viewbox->width;
    float viewBoxHeight = viewbox->height;

    if (viewBoxWidth < 0.0f) {
      viewBoxWidth = 0.0f;
    }
    if (viewBoxHeight < 0.0f) {
      viewBoxHeight = 0.0f;
    }
    return nsSize(NSToCoordRoundWithClamp(viewBoxWidth),
                  NSToCoordRoundWithClamp(viewBoxHeight));
  }

  return nsSVGOuterSVGFrameBase::GetIntrinsicRatio();
}

/* virtual */ nsSize
nsSVGOuterSVGFrame::ComputeSize(nsRenderingContext *aRenderingContext,
                                nsSize aCBSize, nscoord aAvailableWidth,
                                nsSize aMargin, nsSize aBorder, nsSize aPadding,
                                uint32_t aFlags)
{
  if (IsRootOfImage() || IsRootOfReplacedElementSubDoc()) {
    // The embedding element has sized itself using the CSS replaced element
    // sizing rules, using our intrinsic dimensions as necessary. The SVG spec
    // says that the width and height of embedded SVG is overridden by the
    // width and height of the embedding element, so we just need to size to
    // the viewport that the embedding element has established for us.
    return aCBSize;
  }

  nsSize cbSize = aCBSize;
  IntrinsicSize intrinsicSize = GetIntrinsicSize();

  if (!mContent->GetParent()) {
    // We're the root of the outermost browsing context, so we need to scale
    // cbSize by the full-zoom so that SVGs with percentage width/height zoom:

    NS_ASSERTION(aCBSize.width  != NS_AUTOHEIGHT &&
                 aCBSize.height != NS_AUTOHEIGHT,
                 "root should not have auto-width/height containing block");
    cbSize.width  *= PresContext()->GetFullZoom();
    cbSize.height *= PresContext()->GetFullZoom();

    // We also need to honour the width and height attributes' default values
    // of 100% when we're the root of a browsing context.  (GetIntrinsicSize()
    // doesn't report these since there's no such thing as a percentage
    // intrinsic size.  Also note that explicit percentage values are mapped
    // into style, so the following isn't for them.)

    nsSVGSVGElement* content = static_cast<nsSVGSVGElement*>(mContent);

    nsSVGLength2 &width =
      content->mLengthAttributes[nsSVGSVGElement::WIDTH];
    if (width.IsPercentage()) {
      NS_ABORT_IF_FALSE(intrinsicSize.width.GetUnit() == eStyleUnit_None,
                        "GetIntrinsicSize should have reported no "
                        "intrinsic width");
      float val = width.GetAnimValInSpecifiedUnits() / 100.0f;
      if (val < 0.0f) val = 0.0f;
      intrinsicSize.width.SetCoordValue(val * cbSize.width);
    }

    nsSVGLength2 &height =
      content->mLengthAttributes[nsSVGSVGElement::HEIGHT];
    NS_ASSERTION(aCBSize.height != NS_AUTOHEIGHT,
                 "root should not have auto-height containing block");
    if (height.IsPercentage()) {
      NS_ABORT_IF_FALSE(intrinsicSize.height.GetUnit() == eStyleUnit_None,
                        "GetIntrinsicSize should have reported no "
                        "intrinsic height");
      float val = height.GetAnimValInSpecifiedUnits() / 100.0f;
      if (val < 0.0f) val = 0.0f;
      intrinsicSize.height.SetCoordValue(val * cbSize.height);
    }
    NS_ABORT_IF_FALSE(intrinsicSize.height.GetUnit() == eStyleUnit_Coord &&
                      intrinsicSize.width.GetUnit() == eStyleUnit_Coord,
                      "We should have just handled the only situation where"
                      "we lack an intrinsic height or width.");
  }

  return nsLayoutUtils::ComputeSizeWithIntrinsicDimensions(
                            aRenderingContext, this,
                            intrinsicSize, GetIntrinsicRatio(), cbSize,
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

  nsSVGSVGElement *svgElem = static_cast<nsSVGSVGElement*>(mContent);

  nsSVGOuterSVGAnonChildFrame *anonKid =
    static_cast<nsSVGOuterSVGAnonChildFrame*>(GetFirstPrincipalChild());

  if (mState & NS_FRAME_FIRST_REFLOW) {
    // Initialize
    svgElem->UpdateHasChildrenOnlyTransform();
  }

  // If our SVG viewport has changed, update our content and notify.
  // http://www.w3.org/TR/SVG11/coords.html#ViewportSpace

  svgFloatSize newViewportSize(
    nsPresContext::AppUnitsToFloatCSSPixels(aReflowState.ComputedWidth()),
    nsPresContext::AppUnitsToFloatCSSPixels(aReflowState.ComputedHeight()));

  svgFloatSize oldViewportSize = svgElem->GetViewportSize();

  uint32_t changeBits = 0;
  if (newViewportSize != oldViewportSize) {
    if (oldViewportSize.width <= 0.0f || oldViewportSize.height <= 0.0f) {
      // The overflow rects of our child frames will be empty if we had a
      // [synthetic] viewBox during our last reflow, since under
      // FinishAndStoreOverflow() the nsDisplayTransform::TransformRect call
      // will have ended up calling nsSVGSVGElement::GetViewBoxTransform()
      // which will have returned the identity matrix due to our viewport
      // having been zero-sized. Mark all our child frames as dirty so that we
      // reflow them below and update their overflow rects:
      nsIFrame* anonChild = GetFirstPrincipalChild();
      anonChild->AddStateBits(NS_FRAME_IS_DIRTY);
      for (nsIFrame* child = anonChild->GetFirstPrincipalChild(); child;
           child = child->GetNextSibling()) {
        child->AddStateBits(NS_FRAME_IS_DIRTY);
      }
    }
    changeBits |= COORD_CONTEXT_CHANGED;
    svgElem->SetViewportSize(newViewportSize);
  }
  if (mFullZoom != PresContext()->GetFullZoom()) {
    changeBits |= FULL_ZOOM_CHANGED;
    mFullZoom = PresContext()->GetFullZoom();
  }
  if (changeBits) {
    NotifyViewportOrTransformChanged(changeBits);
  }
  mViewportInitialized = true;

  if (!(GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD)) {
    // Now that we've marked the necessary children as dirty, call
    // ReflowSVG() on them:

    mCallingReflowSVG = true;

    // Update the mRects and visual overflow rects of all our descendants,
    // including our anonymous wrapper kid:
    anonKid->ReflowSVG();
    NS_ABORT_IF_FALSE(!anonKid->GetNextSibling(),
      "We should have one anonymous child frame wrapping our real children");

    mCallingReflowSVG = false;
  }

  // Make sure we scroll if we're too big:
  // XXX Use the bounding box of our descendants? (See bug 353460 comment 14.)
  aDesiredSize.SetOverflowAreasToDesiredBounds();
  FinishAndStoreOverflow(&aDesiredSize);

  // Set our anonymous kid's offset from our border box:
  anonKid->SetPosition(GetContentRectRelativeToSelf().TopLeft());

  NS_FRAME_TRACE(NS_FRAME_TRACE_CALLS,
                  ("exit nsSVGOuterSVGFrame::Reflow: size=%d,%d",
                  aDesiredSize.width, aDesiredSize.height));
  NS_FRAME_SET_TRUNCATION(aStatus, aReflowState, aDesiredSize);
  return NS_OK;
}

NS_IMETHODIMP
nsSVGOuterSVGFrame::DidReflow(nsPresContext*   aPresContext,
                              const nsHTMLReflowState*  aReflowState,
                              nsDidReflowStatus aStatus)
{
  nsresult rv = nsSVGOuterSVGFrameBase::DidReflow(aPresContext,aReflowState,aStatus);

  // Make sure elements styled by :hover get updated if script/animation moves
  // them under or out from under the pointer:
  PresContext()->PresShell()->SynthesizeMouseMove(false);

  return rv;
}

//----------------------------------------------------------------------
// container methods

/**
 * Used to paint/hit-test SVG when SVG display lists are disabled.
 */
class nsDisplayOuterSVG : public nsDisplayItem {
public:
  nsDisplayOuterSVG(nsDisplayListBuilder* aBuilder,
                    nsSVGOuterSVGFrame* aFrame) :
    nsDisplayItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplayOuterSVG);
  }
#ifdef NS_BUILD_REFCNT_LOGGING
  virtual ~nsDisplayOuterSVG() {
    MOZ_COUNT_DTOR(nsDisplayOuterSVG);
  }
#endif

  virtual void HitTest(nsDisplayListBuilder* aBuilder, const nsRect& aRect,
                       HitTestState* aState, nsTArray<nsIFrame*> *aOutFrames);
  virtual void Paint(nsDisplayListBuilder* aBuilder,
                     nsRenderingContext* aCtx);

  virtual void ComputeInvalidationRegion(nsDisplayListBuilder* aBuilder,
                                         const nsDisplayItemGeometry* aGeometry,
                                         nsRegion* aInvalidRegion);

  NS_DISPLAY_DECL_NAME("SVGOuterSVG", TYPE_SVG_OUTER_SVG)
};

void
nsDisplayOuterSVG::HitTest(nsDisplayListBuilder* aBuilder, const nsRect& aRect,
                           HitTestState* aState, nsTArray<nsIFrame*> *aOutFrames)
{
  nsSVGOuterSVGFrame *outerSVGFrame = static_cast<nsSVGOuterSVGFrame*>(mFrame);
  nsRect rectAtOrigin = aRect - ToReferenceFrame();
  nsRect thisRect(nsPoint(0,0), outerSVGFrame->GetSize());
  if (!thisRect.Intersects(rectAtOrigin))
    return;

  nsPoint rectCenter(rectAtOrigin.x + rectAtOrigin.width / 2,
                     rectAtOrigin.y + rectAtOrigin.height / 2);

  nsSVGOuterSVGAnonChildFrame *anonKid =
    static_cast<nsSVGOuterSVGAnonChildFrame*>(
      outerSVGFrame->GetFirstPrincipalChild());
  nsIFrame* frame = nsSVGUtils::HitTestChildren(
    anonKid, rectCenter + outerSVGFrame->GetPosition() -
               outerSVGFrame->GetContentRect().TopLeft());
  if (frame) {
    aOutFrames->AppendElement(frame);
  }
}

void
nsDisplayOuterSVG::Paint(nsDisplayListBuilder* aBuilder,
                         nsRenderingContext* aContext)
{
#if defined(DEBUG) && defined(SVG_DEBUG_PAINT_TIMING)
  PRTime start = PR_Now();
#endif

  // Create an SVGAutoRenderState so we can call SetPaintingToWindow on
  // it, but do so without changing the render mode:
  SVGAutoRenderState state(aContext, SVGAutoRenderState::GetRenderMode(aContext));

  if (aBuilder->IsPaintingToWindow()) {
    state.SetPaintingToWindow(true);
  }

  nsRect viewportRect =
    mFrame->GetContentRectRelativeToSelf() + ToReferenceFrame();

  nsRect clipRect = mVisibleRect.Intersect(viewportRect);

  nsIntRect contentAreaDirtyRect =
    (clipRect - viewportRect.TopLeft()).
      ToOutsidePixels(mFrame->PresContext()->AppUnitsPerDevPixel());

  aContext->PushState();
  aContext->Translate(viewportRect.TopLeft());
  nsSVGUtils::PaintFrameWithEffects(aContext, &contentAreaDirtyRect, mFrame);
  aContext->PopState();

  NS_ASSERTION(!aContext->ThebesContext()->HasError(), "Cairo in error state");

#if defined(DEBUG) && defined(SVG_DEBUG_PAINT_TIMING)
  PRTime end = PR_Now();
  printf("SVG Paint Timing: %f ms\n", (end-start)/1000.0);
#endif
}

static PLDHashOperator CheckForeignObjectInvalidatedArea(nsPtrHashKey<nsSVGForeignObjectFrame>* aEntry, void* aData)
{
  nsRegion* region = static_cast<nsRegion*>(aData);
  region->Or(*region, aEntry->GetKey()->GetInvalidRegion());
  return PL_DHASH_NEXT;
}

nsRegion
nsSVGOuterSVGFrame::FindInvalidatedForeignObjectFrameChildren(nsIFrame* aFrame)
{
  nsRegion result;
  if (mForeignObjectHash.Count()) {
    mForeignObjectHash.EnumerateEntries(CheckForeignObjectInvalidatedArea, &result);
  }
  return result;
}

void
nsDisplayOuterSVG::ComputeInvalidationRegion(nsDisplayListBuilder* aBuilder,
                                             const nsDisplayItemGeometry* aGeometry,
                                             nsRegion* aInvalidRegion)
{
  nsSVGOuterSVGFrame *frame = static_cast<nsSVGOuterSVGFrame*>(mFrame);
  frame->InvalidateSVG(frame->FindInvalidatedForeignObjectFrameChildren(frame));

  nsRegion result = frame->GetInvalidRegion();
  result.MoveBy(ToReferenceFrame());
  frame->ClearInvalidRegion();

  nsDisplayItem::ComputeInvalidationRegion(aBuilder, aGeometry, aInvalidRegion);
  aInvalidRegion->Or(*aInvalidRegion, result);
}

// helper
static inline bool
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
nsSVGOuterSVGFrame::AttributeChanged(int32_t  aNameSpaceID,
                                     nsIAtom* aAttribute,
                                     int32_t  aModType)
{
  if (aNameSpaceID == kNameSpaceID_None &&
      !(GetStateBits() & (NS_FRAME_FIRST_REFLOW | NS_STATE_SVG_NONDISPLAY_CHILD))) {
    if (aAttribute == nsGkAtoms::viewBox ||
        aAttribute == nsGkAtoms::preserveAspectRatio ||
        aAttribute == nsGkAtoms::transform) {

      // make sure our cached transform matrix gets (lazily) updated
      mCanvasTM = nullptr;

      nsSVGUtils::NotifyChildrenOfSVGChange(GetFirstPrincipalChild(),
                aAttribute == nsGkAtoms::viewBox ?
                  TRANSFORM_CHANGED | COORD_CONTEXT_CHANGED : TRANSFORM_CHANGED);

      static_cast<nsSVGSVGElement*>(mContent)->ChildrenOnlyTransformChanged();

    } else if (aAttribute == nsGkAtoms::width ||
               aAttribute == nsGkAtoms::height) {

      // Don't call ChildrenOnlyTransformChanged() here, since we call it
      // under Reflow if the width/height actually changed.

      nsIFrame* embeddingFrame;
      if (IsRootOfReplacedElementSubDoc(&embeddingFrame) && embeddingFrame) {
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
  }

  return NS_OK;
}

//----------------------------------------------------------------------
// painting

NS_IMETHODIMP
nsSVGOuterSVGFrame::BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                                     const nsRect&           aDirtyRect,
                                     const nsDisplayListSet& aLists)
{
  if (GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD) {
    return NS_OK;
  }

  nsresult rv = DisplayBorderBackgroundOutline(aBuilder, aLists);
  NS_ENSURE_SUCCESS(rv, rv);

  nsDisplayList childItems;

  if ((aBuilder->IsForEventDelivery() &&
       NS_SVGDisplayListHitTestingEnabled()) ||
      NS_SVGDisplayListPaintingEnabled()) {
    nsDisplayList *nonContentList = &childItems;
    nsDisplayListSet set(nonContentList, nonContentList, nonContentList,
                         &childItems, nonContentList, nonContentList);
    nsresult rv =
      BuildDisplayListForNonBlockChildren(aBuilder, aDirtyRect, set);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    rv = childItems.AppendNewToTop(
           new (aBuilder) nsDisplayOuterSVG(aBuilder, this));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Clip to our _content_ box:
  nsRect clipRect =
    GetContentRectRelativeToSelf() + aBuilder->ToReferenceFrame(this);
  nsDisplayClip* item =
    new (aBuilder) nsDisplayClip(aBuilder, this, &childItems, clipRect);
  rv = childItems.AppendNewToTop(item);
  NS_ENSURE_SUCCESS(rv, rv);

  WrapReplacedContentForBorderRadius(aBuilder, &childItems, aLists);

  return NS_OK;
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
// nsISVGSVGFrame methods:

void
nsSVGOuterSVGFrame::NotifyViewportOrTransformChanged(uint32_t aFlags)
{
  NS_ABORT_IF_FALSE(aFlags &&
                    !(aFlags & ~(COORD_CONTEXT_CHANGED | TRANSFORM_CHANGED |
                                 FULL_ZOOM_CHANGED)),
                    "Unexpected aFlags value");

  // No point in doing anything when were not init'ed yet:
  if (!mViewportInitialized) {
    return;
  }

  nsSVGSVGElement *content = static_cast<nsSVGSVGElement*>(mContent);

  if (aFlags & COORD_CONTEXT_CHANGED) {
    if (content->HasViewBox()) {
      // Percentage lengths on children resolve against the viewBox rect so we
      // don't need to notify them of the viewport change, but the viewBox
      // transform will have changed, so we need to notify them of that instead.
      aFlags = TRANSFORM_CHANGED;
    }
    else if (content->ShouldSynthesizeViewBox()) {
      // In the case of a synthesized viewBox, the synthetic viewBox's rect
      // changes as the viewport changes. As a result we need to maintain the
      // COORD_CONTEXT_CHANGED flag.
      aFlags |= TRANSFORM_CHANGED;
    }
    else if (mCanvasTM && mCanvasTM->IsSingular()) {
      // A width/height of zero will result in us having a singular mCanvasTM
      // even when we don't have a viewBox. So we also want to recompute our
      // mCanvasTM for this width/height change even though we don't have a
      // viewBox.
      aFlags |= TRANSFORM_CHANGED;
    }
  }

  bool haveNonFulLZoomTransformChange = (aFlags & TRANSFORM_CHANGED);

  if (aFlags & FULL_ZOOM_CHANGED) {
    // Convert FULL_ZOOM_CHANGED to TRANSFORM_CHANGED:
    aFlags = (aFlags & ~FULL_ZOOM_CHANGED) | TRANSFORM_CHANGED;
  }

  if (aFlags & TRANSFORM_CHANGED) {
    // Make sure our canvas transform matrix gets (lazily) recalculated:
    mCanvasTM = nullptr;

    if (haveNonFulLZoomTransformChange &&
        !(mState & NS_STATE_SVG_NONDISPLAY_CHILD)) {
      uint32_t flags = (mState & NS_FRAME_IN_REFLOW) ?
                         nsSVGSVGElement::eDuringReflow : 0;
      content->ChildrenOnlyTransformChanged(flags);
    }
  }

  nsSVGUtils::NotifyChildrenOfSVGChange(GetFirstPrincipalChild(), aFlags);
}

//----------------------------------------------------------------------
// nsISVGChildFrame methods:

NS_IMETHODIMP
nsSVGOuterSVGFrame::PaintSVG(nsRenderingContext* aContext,
                             const nsIntRect *aDirtyRect)
{
  NS_ASSERTION(GetFirstPrincipalChild()->GetType() ==
                 nsGkAtoms::svgOuterSVGAnonChildFrame &&
               !GetFirstPrincipalChild()->GetNextSibling(),
               "We should have a single, anonymous, child");
  nsSVGOuterSVGAnonChildFrame *anonKid =
    static_cast<nsSVGOuterSVGAnonChildFrame*>(GetFirstPrincipalChild());
  return anonKid->PaintSVG(aContext, aDirtyRect);
}

SVGBBox
nsSVGOuterSVGFrame::GetBBoxContribution(const gfxMatrix &aToBBoxUserspace,
                                        uint32_t aFlags)
{
  NS_ASSERTION(GetFirstPrincipalChild()->GetType() ==
                 nsGkAtoms::svgOuterSVGAnonChildFrame &&
               !GetFirstPrincipalChild()->GetNextSibling(),
               "We should have a single, anonymous, child");
  // We must defer to our child so that we don't include our
  // content->PrependLocalTransformsTo() transforms.
  nsSVGOuterSVGAnonChildFrame *anonKid =
    static_cast<nsSVGOuterSVGAnonChildFrame*>(GetFirstPrincipalChild());
  return anonKid->GetBBoxContribution(aToBBoxUserspace, aFlags);
}

//----------------------------------------------------------------------
// nsSVGContainerFrame methods:

gfxMatrix
nsSVGOuterSVGFrame::GetCanvasTM(uint32_t aFor)
{
  if (!(GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD)) {
    if ((aFor == FOR_PAINTING && NS_SVGDisplayListPaintingEnabled()) ||
        (aFor == FOR_HIT_TESTING && NS_SVGDisplayListHitTestingEnabled())) {
      return nsSVGIntegrationUtils::GetCSSPxToDevPxMatrix(this);
    }
  }
  if (!mCanvasTM) {
    nsSVGSVGElement *content = static_cast<nsSVGSVGElement*>(mContent);

    float devPxPerCSSPx =
      1.0f / PresContext()->AppUnitsToFloatCSSPixels(
                                PresContext()->AppUnitsPerDevPixel());

    gfxMatrix tm = content->PrependLocalTransformsTo(
                     gfxMatrix().Scale(devPxPerCSSPx, devPxPerCSSPx));
    mCanvasTM = new gfxMatrix(tm);
  }
  return *mCanvasTM;
}

//----------------------------------------------------------------------
// Implementation helpers

bool
nsSVGOuterSVGFrame::IsRootOfReplacedElementSubDoc(nsIFrame **aEmbeddingFrame)
{
  if (!mContent->GetParent()) {
    // Our content is the document element
    nsCOMPtr<nsISupports> container = PresContext()->GetContainer();
    nsCOMPtr<nsIDOMWindow> window = do_GetInterface(container);
    if (window) {
      nsCOMPtr<nsIDOMElement> frameElement;
      window->GetFrameElement(getter_AddRefs(frameElement));
      nsCOMPtr<nsIObjectLoadingContent> olc = do_QueryInterface(frameElement);
      if (olc) {
        // Our document is inside an HTML 'object', 'embed' or 'applet' element
        if (aEmbeddingFrame) {
          nsCOMPtr<nsIContent> element = do_QueryInterface(frameElement);
          *aEmbeddingFrame = element->GetPrimaryFrame();
          NS_ASSERTION(*aEmbeddingFrame, "Yikes, no embedding frame!");
        }
        return true;
      }
    }
  }
  if (aEmbeddingFrame) {
    *aEmbeddingFrame = nullptr;
  }
  return false;
}

bool
nsSVGOuterSVGFrame::IsRootOfImage()
{
  if (!mContent->GetParent()) {
    // Our content is the document element
    nsIDocument* doc = mContent->GetCurrentDoc();
    if (doc && doc->IsBeingUsedAsImage()) {
      // Our document is being used as an image
      return true;
    }
  }

  return false;
}

bool
nsSVGOuterSVGFrame::VerticalScrollbarNotNeeded() const
{
  nsSVGLength2 &height = static_cast<nsSVGSVGElement*>(mContent)->
                           mLengthAttributes[nsSVGSVGElement::HEIGHT];
  return height.IsPercentage() && height.GetBaseValInSpecifiedUnits() <= 100;
}


//----------------------------------------------------------------------
// Implementation of nsSVGOuterSVGAnonChildFrame

nsIFrame*
NS_NewSVGOuterSVGAnonChildFrame(nsIPresShell* aPresShell,
                                nsStyleContext* aContext)
{
  return new (aPresShell) nsSVGOuterSVGAnonChildFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsSVGOuterSVGAnonChildFrame)

#ifdef DEBUG
NS_IMETHODIMP
nsSVGOuterSVGAnonChildFrame::Init(nsIContent* aContent,
                                  nsIFrame* aParent,
                                  nsIFrame* aPrevInFlow)
{
  NS_ABORT_IF_FALSE(aParent->GetType() == nsGkAtoms::svgOuterSVGFrame,
                    "Unexpected parent");
  return nsSVGOuterSVGAnonChildFrameBase::Init(aContent, aParent, aPrevInFlow);
}
#endif

nsIAtom *
nsSVGOuterSVGAnonChildFrame::GetType() const
{
  return nsGkAtoms::svgOuterSVGAnonChildFrame;
}

bool
nsSVGOuterSVGAnonChildFrame::HasChildrenOnlyTransform(gfxMatrix *aTransform) const
{
  // We must claim our nsSVGOuterSVGFrame's children-only transforms as our own
  // so that the children we are used to wrap are transformed properly.

  nsSVGSVGElement *content = static_cast<nsSVGSVGElement*>(mContent);

  bool hasTransform = content->HasChildrenOnlyTransform();

  if (hasTransform && aTransform) {
    // Outer-<svg> doesn't use x/y, so we can pass eChildToUserSpace here.
    gfxMatrix identity;
    *aTransform =
      content->PrependLocalTransformsTo(identity,
                                        nsSVGElement::eChildToUserSpace);
  }

  return hasTransform;
}
