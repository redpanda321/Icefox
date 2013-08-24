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
 * Scooter Morris.
 * Portions created by the Initial Developer are Copyright (C) 2004
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Scooter Morris <scootermorris@comcast.net>
 *   Jonathan Watt <jwatt@jwatt.org>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "nsIDOMSVGAnimatedNumber.h"
#include "nsIDOMSVGAnimTransformList.h"
#include "nsSVGTransformList.h"
#include "nsSVGMatrix.h"
#include "nsSVGEffects.h"
#include "nsIDOMSVGStopElement.h"
#include "nsSVGGradientElement.h"
#include "nsSVGGeometryFrame.h"
#include "nsSVGGradientFrame.h"
#include "gfxContext.h"
#include "gfxPattern.h"

//----------------------------------------------------------------------
// Implementation

nsSVGGradientFrame::nsSVGGradientFrame(nsStyleContext* aContext) :
  nsSVGGradientFrameBase(aContext),
  mLoopFlag(PR_FALSE),
  mNoHRefURI(PR_FALSE)
{
}

NS_IMPL_FRAMEARENA_HELPERS(nsSVGGradientFrame)

//----------------------------------------------------------------------
// nsIFrame methods:

/* virtual */ void
nsSVGGradientFrame::DidSetStyleContext(nsStyleContext* aOldStyleContext)
{
  nsSVGEffects::InvalidateRenderingObservers(this);
  nsSVGGradientFrameBase::DidSetStyleContext(aOldStyleContext);
}

NS_IMETHODIMP
nsSVGGradientFrame::AttributeChanged(PRInt32         aNameSpaceID,
                                     nsIAtom*        aAttribute,
                                     PRInt32         aModType)
{
  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::gradientUnits ||
       aAttribute == nsGkAtoms::gradientTransform ||
       aAttribute == nsGkAtoms::spreadMethod)) {
    nsSVGEffects::InvalidateRenderingObservers(this);
  } else if (aNameSpaceID == kNameSpaceID_XLink &&
             aAttribute == nsGkAtoms::href) {
    // Blow away our reference, if any
    Properties().Delete(nsSVGEffects::HrefProperty());
    mNoHRefURI = PR_FALSE;
    // And update whoever references us
    nsSVGEffects::InvalidateRenderingObservers(this);
  }

  return nsSVGGradientFrameBase::AttributeChanged(aNameSpaceID,
                                                  aAttribute, aModType);
}

//----------------------------------------------------------------------

PRUint32
nsSVGGradientFrame::GetStopCount()
{
  return GetStopFrame(-1, nsnull);
}

void
nsSVGGradientFrame::GetStopInformation(PRInt32 aIndex,
                                       float *aOffset,
                                       nscolor *aStopColor,
                                       float *aStopOpacity)
{
  *aOffset = 0.0f;
  *aStopColor = NS_RGBA(0, 0, 0, 0);
  *aStopOpacity = 1.0f;

  nsIFrame *stopFrame = nsnull;
  GetStopFrame(aIndex, &stopFrame);
  nsCOMPtr<nsIDOMSVGStopElement> stopElement =
    do_QueryInterface(stopFrame->GetContent());

  if (stopElement) {
    nsCOMPtr<nsIDOMSVGAnimatedNumber> aNum;
    stopElement->GetOffset(getter_AddRefs(aNum));

    aNum->GetAnimVal(aOffset);
    if (*aOffset < 0.0f)
      *aOffset = 0.0f;
    else if (*aOffset > 1.0f)
      *aOffset = 1.0f;
  }

  if (stopFrame) {
    *aStopColor   = stopFrame->GetStyleSVGReset()->mStopColor;
    *aStopOpacity = stopFrame->GetStyleSVGReset()->mStopOpacity;
  }
#ifdef DEBUG
  // One way or another we have an implementation problem if we get here
  else if (stopElement) {
    NS_WARNING("We *do* have a stop but can't use it because it doesn't have "
               "a frame - we need frame free gradients and stops!");
  }
  else {
    NS_ERROR("Don't call me with an invalid stop index!");
  }
#endif
}

gfxMatrix
nsSVGGradientFrame::GetGradientTransform(nsIFrame *aSource,
                                         const gfxRect *aOverrideBounds)
{
  gfxMatrix bboxMatrix;

  PRUint16 gradientUnits = GetGradientUnits();
  if (gradientUnits == nsIDOMSVGUnitTypes::SVG_UNIT_TYPE_USERSPACEONUSE) {
    // If this gradient is applied to text, our caller
    // will be the glyph, which is not a container, so we
    // need to get the parent
    if (aSource->GetContent()->IsNodeOfType(nsINode::eTEXT))
      mSource = aSource->GetParent();
    else
      mSource = aSource;
  } else {
    NS_ASSERTION(gradientUnits == nsIDOMSVGUnitTypes::SVG_UNIT_TYPE_OBJECTBOUNDINGBOX,
                 "Unknown gradientUnits type");
    // objectBoundingBox is the default anyway

    gfxRect bbox = aOverrideBounds ? *aOverrideBounds : nsSVGUtils::GetBBox(aSource);
    bboxMatrix = gfxMatrix(bbox.Width(), 0, 0, bbox.Height(), bbox.X(), bbox.Y());
  }

  nsSVGGradientElement *element =
    GetGradientWithAttr(nsGkAtoms::gradientTransform, mContent);

  if (!element->mGradientTransform)
    return bboxMatrix;

  nsCOMPtr<nsIDOMSVGTransformList> trans;
  element->mGradientTransform->GetAnimVal(getter_AddRefs(trans));
  nsCOMPtr<nsIDOMSVGMatrix> gradientTransform =
    nsSVGTransformList::GetConsolidationMatrix(trans);

  if (!gradientTransform)
    return bboxMatrix;

  return bboxMatrix.PreMultiply(nsSVGUtils::ConvertSVGMatrixToThebes(gradientTransform));
}

PRUint16
nsSVGGradientFrame::GetSpreadMethod()
{
  nsSVGGradientElement *element =
    GetGradientWithAttr(nsGkAtoms::spreadMethod, mContent);

  return element->mEnumAttributes[nsSVGGradientElement::SPREADMETHOD].GetAnimValue();
}

//----------------------------------------------------------------------
// nsSVGPaintServerFrame methods:

already_AddRefed<gfxPattern>
nsSVGGradientFrame::GetPaintServerPattern(nsIFrame *aSource,
                                           float aGraphicOpacity,
                                           const gfxRect *aOverrideBounds)
{
  // Get the transform list (if there is one)
  gfxMatrix patternMatrix = GetGradientTransform(aSource, aOverrideBounds);

  if (patternMatrix.IsSingular())
    return nsnull;

  PRUint32 nStops = GetStopCount();

  // SVG specification says that no stops should be treated like
  // the corresponding fill or stroke had "none" specified.
  if (nStops == 0) {
    nsRefPtr<gfxPattern> pattern = new gfxPattern(gfxRGBA(0, 0, 0, 0));
    return pattern.forget();
  }

  patternMatrix.Invert();

  nsRefPtr<gfxPattern> gradient = CreateGradient();
  if (!gradient || gradient->CairoStatus())
    return nsnull;

  PRUint16 aSpread = GetSpreadMethod();
  if (aSpread == nsIDOMSVGGradientElement::SVG_SPREADMETHOD_PAD)
    gradient->SetExtend(gfxPattern::EXTEND_PAD);
  else if (aSpread == nsIDOMSVGGradientElement::SVG_SPREADMETHOD_REFLECT)
    gradient->SetExtend(gfxPattern::EXTEND_REFLECT);
  else if (aSpread == nsIDOMSVGGradientElement::SVG_SPREADMETHOD_REPEAT)
    gradient->SetExtend(gfxPattern::EXTEND_REPEAT);

  gradient->SetMatrix(patternMatrix);

  // setup stops
  float lastOffset = 0.0f;

  for (PRUint32 i = 0; i < nStops; i++) {
    float offset, stopOpacity;
    nscolor stopColor;

    GetStopInformation(i, &offset, &stopColor, &stopOpacity);

    if (offset < lastOffset)
      offset = lastOffset;
    else
      lastOffset = offset;

    gradient->AddColorStop(offset,
                           gfxRGBA(NS_GET_R(stopColor)/255.0,
                                   NS_GET_G(stopColor)/255.0,
                                   NS_GET_B(stopColor)/255.0,
                                   NS_GET_A(stopColor)/255.0 *
                                     stopOpacity * aGraphicOpacity));
  }

  return gradient.forget();
}

// Private (helper) methods

nsSVGGradientFrame *
nsSVGGradientFrame::GetReferencedGradient()
{
  if (mNoHRefURI)
    return nsnull;

  nsSVGPaintingProperty *property = static_cast<nsSVGPaintingProperty*>
    (Properties().Get(nsSVGEffects::HrefProperty()));

  if (!property) {
    // Fetch our gradient element's xlink:href attribute
    nsSVGGradientElement *grad = static_cast<nsSVGGradientElement *>(mContent);
    nsAutoString href;
    grad->mStringAttributes[nsSVGGradientElement::HREF].GetAnimValue(href, grad);
    if (href.IsEmpty()) {
      mNoHRefURI = PR_TRUE;
      return nsnull; // no URL
    }

    // Convert href to an nsIURI
    nsCOMPtr<nsIURI> targetURI;
    nsCOMPtr<nsIURI> base = mContent->GetBaseURI();
    nsContentUtils::NewURIWithDocumentCharset(getter_AddRefs(targetURI), href,
                                              mContent->GetCurrentDoc(), base);

    property =
      nsSVGEffects::GetPaintingProperty(targetURI, this, nsSVGEffects::HrefProperty());
    if (!property)
      return nsnull;
  }

  nsIFrame *result = property->GetReferencedFrame();
  if (!result)
    return nsnull;

  nsIAtom* frameType = result->GetType();
  if (frameType != nsGkAtoms::svgLinearGradientFrame &&
      frameType != nsGkAtoms::svgRadialGradientFrame)
    return nsnull;

  return static_cast<nsSVGGradientFrame*>(result);
}

nsSVGGradientElement *
nsSVGGradientFrame::GetGradientWithAttr(nsIAtom *aAttrName, nsIContent *aDefault)
{
  if (mContent->HasAttr(kNameSpaceID_None, aAttrName))
    return static_cast<nsSVGGradientElement *>(mContent);

  nsSVGGradientElement *grad = static_cast<nsSVGGradientElement *>(aDefault);

  nsSVGGradientFrame *next = GetReferencedGradient();
  if (!next)
    return grad;

  // Set mLoopFlag before checking mNextGrad->mLoopFlag in case we are mNextGrad
  mLoopFlag = PR_TRUE;
  // XXXjwatt: we should really send an error to the JavaScript Console here:
  NS_WARN_IF_FALSE(!next->mLoopFlag, "gradient reference loop detected "
                                     "while inheriting attribute!");
  if (!next->mLoopFlag)
    grad = next->GetGradientWithAttr(aAttrName, aDefault);
  mLoopFlag = PR_FALSE;

  return grad;
}

nsSVGGradientElement *
nsSVGGradientFrame::GetGradientWithAttr(nsIAtom *aAttrName, nsIAtom *aGradType,
                                        nsIContent *aDefault)
{
  if (GetType() == aGradType && mContent->HasAttr(kNameSpaceID_None, aAttrName))
    return static_cast<nsSVGGradientElement *>(mContent);

  nsSVGGradientElement *grad = static_cast<nsSVGGradientElement *>(aDefault);

  nsSVGGradientFrame *next = GetReferencedGradient();
  if (!next)
    return grad;

  // Set mLoopFlag before checking mNextGrad->mLoopFlag in case we are mNextGrad
  mLoopFlag = PR_TRUE;
  // XXXjwatt: we should really send an error to the JavaScript Console here:
  NS_WARN_IF_FALSE(!next->mLoopFlag, "gradient reference loop detected "
                                     "while inheriting attribute!");
  if (!next->mLoopFlag)
    grad = next->GetGradientWithAttr(aAttrName, aGradType, aDefault);
  mLoopFlag = PR_FALSE;

  return grad;
}

PRInt32
nsSVGGradientFrame::GetStopFrame(PRInt32 aIndex, nsIFrame * *aStopFrame)
{
  PRInt32 stopCount = 0;
  nsIFrame *stopFrame = nsnull;
  for (stopFrame = mFrames.FirstChild(); stopFrame;
       stopFrame = stopFrame->GetNextSibling()) {
    if (stopFrame->GetType() == nsGkAtoms::svgStopFrame) {
      // Is this the one we're looking for?
      if (stopCount++ == aIndex)
        break; // Yes, break out of the loop
    }
  }
  if (stopCount > 0) {
    if (aStopFrame)
      *aStopFrame = stopFrame;
    return stopCount;
  }

  // Our gradient element doesn't have stops - try to "inherit" them

  nsSVGGradientFrame *next = GetReferencedGradient();
  if (!next) {
    if (aStopFrame)
      *aStopFrame = nsnull;
    return 0;
  }

  // Set mLoopFlag before checking mNextGrad->mLoopFlag in case we are mNextGrad
  mLoopFlag = PR_TRUE;
  // XXXjwatt: we should really send an error to the JavaScript Console here:
  NS_WARN_IF_FALSE(!next->mLoopFlag, "gradient reference loop detected "
                                     "while inheriting stop!");
  if (!next->mLoopFlag)
    stopCount = next->GetStopFrame(aIndex, aStopFrame);
  mLoopFlag = PR_FALSE;

  return stopCount;
}

PRUint16
nsSVGGradientFrame::GetGradientUnits()
{
  // This getter is called every time the others are called - maybe cache it?

  nsSVGGradientElement *element =
    GetGradientWithAttr(nsGkAtoms::gradientUnits, mContent);
  return element->mEnumAttributes[nsSVGGradientElement::GRADIENTUNITS].GetAnimValue();
}

// -------------------------------------------------------------------------
// Linear Gradients
// -------------------------------------------------------------------------

#ifdef DEBUG
NS_IMETHODIMP
nsSVGLinearGradientFrame::Init(nsIContent* aContent,
                               nsIFrame* aParent,
                               nsIFrame* aPrevInFlow)
{
  nsCOMPtr<nsIDOMSVGLinearGradientElement> grad = do_QueryInterface(aContent);
  NS_ASSERTION(grad, "Content is not an SVG linearGradient");

  return nsSVGLinearGradientFrameBase::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

nsIAtom*
nsSVGLinearGradientFrame::GetType() const
{
  return nsGkAtoms::svgLinearGradientFrame;
}

NS_IMETHODIMP
nsSVGLinearGradientFrame::AttributeChanged(PRInt32         aNameSpaceID,
                                           nsIAtom*        aAttribute,
                                           PRInt32         aModType)
{
  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::x1 ||
       aAttribute == nsGkAtoms::y1 ||
       aAttribute == nsGkAtoms::x2 ||
       aAttribute == nsGkAtoms::y2)) {
    nsSVGEffects::InvalidateRenderingObservers(this);
  }

  return nsSVGGradientFrame::AttributeChanged(aNameSpaceID,
                                              aAttribute, aModType);
}

//----------------------------------------------------------------------

float
nsSVGLinearGradientFrame::GradientLookupAttribute(nsIAtom *aAtomName,
                                                  PRUint16 aEnumName)
{
  nsSVGLinearGradientElement *element =
    GetLinearGradientWithAttr(aAtomName, mContent);

  // Object bounding box units are handled by setting the appropriate
  // transform in GetGradientTransform, but we need to handle user
  // space units as part of the individual Get* routines.  Fixes 323669.

  PRUint16 gradientUnits = GetGradientUnits();
  if (gradientUnits == nsIDOMSVGUnitTypes::SVG_UNIT_TYPE_USERSPACEONUSE) {
    return nsSVGUtils::UserSpace(mSource,
                                 &element->mLengthAttributes[aEnumName]);
  }

  NS_ASSERTION(gradientUnits == nsIDOMSVGUnitTypes::SVG_UNIT_TYPE_OBJECTBOUNDINGBOX,
               "Unknown gradientUnits type");

  return element->mLengthAttributes[aEnumName].
    GetAnimValue(static_cast<nsSVGSVGElement*>(nsnull));
}

already_AddRefed<gfxPattern>
nsSVGLinearGradientFrame::CreateGradient()
{
  float x1, y1, x2, y2;

  x1 = GradientLookupAttribute(nsGkAtoms::x1, nsSVGLinearGradientElement::X1);
  y1 = GradientLookupAttribute(nsGkAtoms::y1, nsSVGLinearGradientElement::Y1);
  x2 = GradientLookupAttribute(nsGkAtoms::x2, nsSVGLinearGradientElement::X2);
  y2 = GradientLookupAttribute(nsGkAtoms::y2, nsSVGLinearGradientElement::Y2);

  gfxPattern *pattern = new gfxPattern(x1, y1, x2, y2);
  NS_IF_ADDREF(pattern);
  return pattern;
}

// -------------------------------------------------------------------------
// Radial Gradients
// -------------------------------------------------------------------------

#ifdef DEBUG
NS_IMETHODIMP
nsSVGRadialGradientFrame::Init(nsIContent* aContent,
                               nsIFrame* aParent,
                               nsIFrame* aPrevInFlow)
{
  nsCOMPtr<nsIDOMSVGRadialGradientElement> grad = do_QueryInterface(aContent);
  NS_ASSERTION(grad, "Content is not an SVG radialGradient");

  return nsSVGRadialGradientFrameBase::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

nsIAtom*
nsSVGRadialGradientFrame::GetType() const
{
  return nsGkAtoms::svgRadialGradientFrame;
}

NS_IMETHODIMP
nsSVGRadialGradientFrame::AttributeChanged(PRInt32         aNameSpaceID,
                                           nsIAtom*        aAttribute,
                                           PRInt32         aModType)
{
  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::r ||
       aAttribute == nsGkAtoms::cx ||
       aAttribute == nsGkAtoms::cy ||
       aAttribute == nsGkAtoms::fx ||
       aAttribute == nsGkAtoms::fy)) {
    nsSVGEffects::InvalidateRenderingObservers(this);
  }

  return nsSVGGradientFrame::AttributeChanged(aNameSpaceID,
                                              aAttribute, aModType);
}

//----------------------------------------------------------------------

float
nsSVGRadialGradientFrame::GradientLookupAttribute(nsIAtom *aAtomName,
                                                  PRUint16 aEnumName,
                                                  nsSVGRadialGradientElement *aElement)
{
  nsSVGRadialGradientElement *element;

  if (aElement) {
    element = aElement;
  } else {
    element = GetRadialGradientWithAttr(aAtomName, mContent);
  }

  // Object bounding box units are handled by setting the appropriate
  // transform in GetGradientTransform, but we need to handle user
  // space units as part of the individual Get* routines.  Fixes 323669.

  PRUint16 gradientUnits = GetGradientUnits();
  if (gradientUnits == nsIDOMSVGUnitTypes::SVG_UNIT_TYPE_USERSPACEONUSE) {
    return nsSVGUtils::UserSpace(mSource,
                                 &element->mLengthAttributes[aEnumName]);
  }

  NS_ASSERTION(gradientUnits == nsIDOMSVGUnitTypes::SVG_UNIT_TYPE_OBJECTBOUNDINGBOX,
               "Unknown gradientUnits type");

  return element->mLengthAttributes[aEnumName].
    GetAnimValue(static_cast<nsSVGSVGElement*>(nsnull));
}

already_AddRefed<gfxPattern>
nsSVGRadialGradientFrame::CreateGradient()
{
  float cx, cy, r, fx, fy;

  cx = GradientLookupAttribute(nsGkAtoms::cx, nsSVGRadialGradientElement::CX);
  cy = GradientLookupAttribute(nsGkAtoms::cy, nsSVGRadialGradientElement::CY);
  r  = GradientLookupAttribute(nsGkAtoms::r,  nsSVGRadialGradientElement::R);

  nsSVGRadialGradientElement *gradient;

  if (!(gradient = GetRadialGradientWithAttr(nsGkAtoms::fx, nsnull)))
    fx = cx;  // if fx isn't set, we must use cx
  else
    fx = GradientLookupAttribute(nsGkAtoms::fx, nsSVGRadialGradientElement::FX, gradient);

  if (!(gradient = GetRadialGradientWithAttr(nsGkAtoms::fy, nsnull)))
    fy = cy;  // if fy isn't set, we must use cy
  else
    fy = GradientLookupAttribute(nsGkAtoms::fy, nsSVGRadialGradientElement::FY, gradient);

  if (fx != cx || fy != cy) {
    // The focal point (fFx and fFy) must be clamped to be *inside* - not on -
    // the circumference of the gradient or we'll get rendering anomalies. We
    // calculate the distance from the focal point to the gradient center and
    // make sure it is *less* than the gradient radius.
    // 1/128 is the limit of the fractional part of cairo's 24.8 fixed point
    // representation divided by 2 to ensure that we get different cairo
    // fractions
    double dMax = NS_MAX(0.0, r - 1.0/128);
    float dx = fx - cx;
    float dy = fy - cy;
    double d = sqrt((dx * dx) + (dy * dy));
    if (d > dMax) {
      double angle = atan2(dy, dx);
      fx = (float)(dMax * cos(angle)) + cx;
      fy = (float)(dMax * sin(angle)) + cy;
    }
  }

  gfxPattern *pattern = new gfxPattern(fx, fy, 0, cx, cy, r);
  NS_IF_ADDREF(pattern);
  return pattern;
}

// -------------------------------------------------------------------------
// Public functions
// -------------------------------------------------------------------------

nsIFrame*
NS_NewSVGLinearGradientFrame(nsIPresShell*   aPresShell,
                             nsStyleContext* aContext)
{
  return new (aPresShell) nsSVGLinearGradientFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsSVGLinearGradientFrame)

nsIFrame*
NS_NewSVGRadialGradientFrame(nsIPresShell*   aPresShell,
                             nsStyleContext* aContext)
{
  return new (aPresShell) nsSVGRadialGradientFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsSVGRadialGradientFrame)
