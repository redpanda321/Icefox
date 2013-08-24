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
 * The Initial Developer of the Original Code is IBM Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2004
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#include "nsIDOMSVGAnimatedRect.h"
#include "nsIDOMSVGRect.h"
#include "nsIDocument.h"
#include "nsSVGMarkerFrame.h"
#include "nsSVGPathGeometryFrame.h"
#include "nsSVGEffects.h"
#include "nsSVGMarkerElement.h"
#include "nsSVGPathGeometryElement.h"
#include "gfxContext.h"

nsIFrame*
NS_NewSVGMarkerFrame(nsIPresShell* aPresShell, nsStyleContext* aContext)
{
  return new (aPresShell) nsSVGMarkerFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsSVGMarkerFrame)

//----------------------------------------------------------------------
// nsIFrame methods:

NS_IMETHODIMP
nsSVGMarkerFrame::AttributeChanged(PRInt32  aNameSpaceID,
                                   nsIAtom* aAttribute,
                                   PRInt32  aModType)
{
  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::markerUnits ||
       aAttribute == nsGkAtoms::refX ||
       aAttribute == nsGkAtoms::refY ||
       aAttribute == nsGkAtoms::markerWidth ||
       aAttribute == nsGkAtoms::markerHeight ||
       aAttribute == nsGkAtoms::orient ||
       aAttribute == nsGkAtoms::preserveAspectRatio ||
       aAttribute == nsGkAtoms::viewBox)) {
    nsSVGEffects::InvalidateRenderingObservers(this);
  }

  return nsSVGMarkerFrameBase::AttributeChanged(aNameSpaceID,
                                                aAttribute, aModType);
}

#ifdef DEBUG
NS_IMETHODIMP
nsSVGMarkerFrame::Init(nsIContent* aContent,
                       nsIFrame* aParent,
                       nsIFrame* aPrevInFlow)
{
  nsCOMPtr<nsIDOMSVGMarkerElement> marker = do_QueryInterface(aContent);
  NS_ASSERTION(marker, "Content is not an SVG marker");

  return nsSVGMarkerFrameBase::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

nsIAtom *
nsSVGMarkerFrame::GetType() const
{
  return nsGkAtoms::svgMarkerFrame;
}

//----------------------------------------------------------------------
// nsSVGContainerFrame methods:

gfxMatrix
nsSVGMarkerFrame::GetCanvasTM()
{
  NS_ASSERTION(mMarkedFrame, "null nsSVGPathGeometry frame");

  if (mInUse2) {
    // We're going to be bailing drawing the marker, so return an identity.
    return gfxMatrix();
  }

  nsSVGMarkerElement *content = static_cast<nsSVGMarkerElement*>(mContent);
  
  mInUse2 = PR_TRUE;
  gfxMatrix markedTM = mMarkedFrame->GetCanvasTM();
  mInUse2 = PR_FALSE;

  gfxMatrix markerTM = content->GetMarkerTransform(mStrokeWidth, mX, mY, mAutoAngle);
  gfxMatrix viewBoxTM = content->GetViewBoxTransform();

  return viewBoxTM * markerTM * markedTM;
}


nsresult
nsSVGMarkerFrame::PaintMark(nsSVGRenderState *aContext,
                            nsSVGPathGeometryFrame *aMarkedFrame,
                            nsSVGMark *aMark, float aStrokeWidth)
{
  // If the flag is set when we get here, it means this marker frame
  // has already been used painting the current mark, and the document
  // has a marker reference loop.
  if (mInUse)
    return NS_OK;

  nsSVGMarkerElement *marker = static_cast<nsSVGMarkerElement*>(mContent);

  nsCOMPtr<nsIDOMSVGAnimatedRect> arect;
  nsresult rv = marker->GetViewBox(getter_AddRefs(arect));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDOMSVGRect> rect;
  rv = arect->GetAnimVal(getter_AddRefs(rect));
  NS_ENSURE_SUCCESS(rv, rv);

  float x, y, width, height;
  rect->GetX(&x);
  rect->GetY(&y);
  rect->GetWidth(&width);
  rect->GetHeight(&height);

  if (width <= 0.0f || height <= 0.0f) {
    // We must disable rendering if the viewBox width or height are zero.
    return NS_OK;
  }

  AutoMarkerReferencer markerRef(this, aMarkedFrame);

  mStrokeWidth = aStrokeWidth;
  mX = aMark->x;
  mY = aMark->y;
  mAutoAngle = aMark->angle;

  gfxContext *gfx = aContext->GetGfxContext();

  if (GetStyleDisplay()->IsScrollableOverflow()) {
    gfx->Save();
    gfxRect clipRect =
      nsSVGUtils::GetClipRectForFrame(this, x, y, width, height);
    nsSVGUtils::SetClipRect(gfx, GetCanvasTM(), clipRect);
  }

  for (nsIFrame* kid = mFrames.FirstChild(); kid;
       kid = kid->GetNextSibling()) {
    nsISVGChildFrame* SVGFrame = do_QueryFrame(kid);
    if (SVGFrame) {
      // The CTM of each frame referencing us may be different.
      SVGFrame->NotifySVGChanged(nsISVGChildFrame::SUPPRESS_INVALIDATION |
                                 nsISVGChildFrame::TRANSFORM_CHANGED);
      nsSVGUtils::PaintFrameWithEffects(aContext, nsnull, kid);
    }
  }

  if (GetStyleDisplay()->IsScrollableOverflow())
    gfx->Restore();

  return NS_OK;
}


nsRect
nsSVGMarkerFrame::RegionMark(nsSVGPathGeometryFrame *aMarkedFrame,
                             const nsSVGMark *aMark, float aStrokeWidth)
{
  // If the flag is set when we get here, it means this marker frame
  // has already been used in calculating the current mark region, and
  // the document has a marker reference loop.
  if (mInUse)
    return nsRect(0,0,0,0);

  AutoMarkerReferencer markerRef(this, aMarkedFrame);

  mStrokeWidth = aStrokeWidth;
  mX = aMark->x;
  mY = aMark->y;
  mAutoAngle = aMark->angle;

  // Force children to update their covered region
  for (nsIFrame* kid = mFrames.FirstChild();
       kid;
       kid = kid->GetNextSibling()) {
    nsISVGChildFrame* child = do_QueryFrame(kid);
    if (child)
      child->UpdateCoveredRegion();
  }

  // Now get the combined covered region
  return nsSVGUtils::GetCoveredRegion(mFrames);
}

void
nsSVGMarkerFrame::SetParentCoordCtxProvider(nsSVGSVGElement *aContext)
{
  nsSVGMarkerElement *marker = static_cast<nsSVGMarkerElement*>(mContent);
  marker->SetParentCoordCtxProvider(aContext);
}

//----------------------------------------------------------------------
// helper class

nsSVGMarkerFrame::AutoMarkerReferencer::AutoMarkerReferencer(
    nsSVGMarkerFrame *aFrame,
    nsSVGPathGeometryFrame *aMarkedFrame)
      : mFrame(aFrame)
{
  mFrame->mInUse = PR_TRUE;
  mFrame->mMarkedFrame = aMarkedFrame;

  nsSVGSVGElement *ctx =
    static_cast<nsSVGElement*>(aMarkedFrame->GetContent())->GetCtx();
  mFrame->SetParentCoordCtxProvider(ctx);
}

nsSVGMarkerFrame::AutoMarkerReferencer::~AutoMarkerReferencer()
{
  mFrame->SetParentCoordCtxProvider(nsnull);

  mFrame->mMarkedFrame = nsnull;
  mFrame->mInUse = PR_FALSE;
}
