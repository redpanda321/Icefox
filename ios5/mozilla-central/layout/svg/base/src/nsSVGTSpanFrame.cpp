/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "nsSVGTSpanFrame.h"

// Keep others in (case-insensitive) order:
#include "nsIDOMSVGTSpanElement.h"
#include "nsIDOMSVGAltGlyphElement.h"
#include "nsSVGIntegrationUtils.h"
#include "nsSVGUtils.h"

//----------------------------------------------------------------------
// Implementation

nsIFrame*
NS_NewSVGTSpanFrame(nsIPresShell* aPresShell, nsStyleContext* aContext)
{
  return new (aPresShell) nsSVGTSpanFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsSVGTSpanFrame)

nsIAtom *
nsSVGTSpanFrame::GetType() const
{
  return nsGkAtoms::svgTSpanFrame;
}

//----------------------------------------------------------------------
// nsQueryFrame methods

NS_QUERYFRAME_HEAD(nsSVGTSpanFrame)
  NS_QUERYFRAME_ENTRY(nsISVGGlyphFragmentNode)
NS_QUERYFRAME_TAIL_INHERITING(nsSVGTSpanFrameBase)

//----------------------------------------------------------------------
// nsIFrame methods

#ifdef DEBUG
NS_IMETHODIMP
nsSVGTSpanFrame::Init(nsIContent* aContent,
                      nsIFrame* aParent,
                      nsIFrame* aPrevInFlow)
{
  NS_ASSERTION(aParent, "null parent");

  // Some of our subclasses have an aContent that's not a <svg:tspan> or are
  // allowed to be constructed even when there is no nsISVGTextContentMetrics
  // ancestor.  For example, nsSVGAFrame inherits from us but may have nothing
  // to do with text.
  if (GetType() == nsGkAtoms::svgTSpanFrame) {
    nsIFrame* ancestorFrame = nsSVGUtils::GetFirstNonAAncestorFrame(aParent);
    NS_ASSERTION(ancestorFrame, "Must have ancestor");

    nsSVGTextContainerFrame *metrics = do_QueryFrame(ancestorFrame);
    NS_ASSERTION(metrics,
                 "trying to construct an SVGTSpanFrame for an invalid "
                 "container");

    nsCOMPtr<nsIDOMSVGTSpanElement> tspan = do_QueryInterface(aContent);
    nsCOMPtr<nsIDOMSVGAltGlyphElement> altGlyph = do_QueryInterface(aContent);
    NS_ASSERTION(tspan || altGlyph, "Content is not an SVG tspan or altGlyph");
  }

  return nsSVGTSpanFrameBase::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

NS_IMETHODIMP
nsSVGTSpanFrame::AttributeChanged(PRInt32         aNameSpaceID,
                                  nsIAtom*        aAttribute,
                                  PRInt32         aModType)
{
  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::x ||
       aAttribute == nsGkAtoms::y ||
       aAttribute == nsGkAtoms::dx ||
       aAttribute == nsGkAtoms::dy ||
       aAttribute == nsGkAtoms::rotate)) {
    nsSVGUtils::InvalidateAndScheduleBoundsUpdate(this);
    NotifyGlyphMetricsChange();
  }

  return NS_OK;
}

//----------------------------------------------------------------------
// nsSVGContainerFrame methods:

gfxMatrix
nsSVGTSpanFrame::GetCanvasTM(PRUint32 aFor)
{
  if (!(GetStateBits() & NS_STATE_SVG_NONDISPLAY_CHILD)) {
    if ((aFor == FOR_PAINTING && NS_SVGDisplayListPaintingEnabled()) ||
        (aFor == FOR_HIT_TESTING && NS_SVGDisplayListHitTestingEnabled())) {
      return nsSVGIntegrationUtils::GetCSSPxToDevPxMatrix(this);
    }
  }
  NS_ASSERTION(mParent, "null parent");
  return static_cast<nsSVGContainerFrame*>(mParent)->GetCanvasTM(aFor);
}

//----------------------------------------------------------------------
// nsISVGGlyphFragmentNode methods:

PRUint32
nsSVGTSpanFrame::GetNumberOfChars()
{
  return nsSVGTSpanFrameBase::GetNumberOfChars();
}

float
nsSVGTSpanFrame::GetComputedTextLength()
{
  return nsSVGTSpanFrameBase::GetComputedTextLength();
}

float
nsSVGTSpanFrame::GetSubStringLength(PRUint32 charnum, PRUint32 nchars)
{
  return nsSVGTSpanFrameBase::GetSubStringLength(charnum, nchars);
}

PRInt32
nsSVGTSpanFrame::GetCharNumAtPosition(nsIDOMSVGPoint *point)
{
  return nsSVGTSpanFrameBase::GetCharNumAtPosition(point);
}

NS_IMETHODIMP_(nsSVGGlyphFrame *)
nsSVGTSpanFrame::GetFirstGlyphFrame()
{
  // try children first:
  nsIFrame* kid = mFrames.FirstChild();
  while (kid) {
    nsISVGGlyphFragmentNode *node = do_QueryFrame(kid);
    if (node)
      return node->GetFirstGlyphFrame();
    kid = kid->GetNextSibling();
  }

  // nope. try siblings:
  return GetNextGlyphFrame();

}

NS_IMETHODIMP_(nsSVGGlyphFrame *)
nsSVGTSpanFrame::GetNextGlyphFrame()
{
  nsIFrame* sibling = GetNextSibling();
  while (sibling) {
    nsISVGGlyphFragmentNode *node = do_QueryFrame(sibling);
    if (node)
      return node->GetFirstGlyphFrame();
    sibling = sibling->GetNextSibling();
  }

  // no more siblings. go back up the tree.
  
  NS_ASSERTION(GetParent(), "null parent");
  nsISVGGlyphFragmentNode *node = do_QueryFrame(GetParent());
  return node ? node->GetNextGlyphFrame() : nsnull;
}

NS_IMETHODIMP_(void)
nsSVGTSpanFrame::SetWhitespaceCompression(bool)
{
  nsSVGTSpanFrameBase::SetWhitespaceCompression();
}
