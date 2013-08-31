/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "nsSVGMaskFrame.h"

// Keep others in (case-insensitive) order:
#include "gfxContext.h"
#include "gfxImageSurface.h"
#include "nsRenderingContext.h"
#include "nsSVGEffects.h"
#include "nsSVGMaskElement.h"

//----------------------------------------------------------------------
// Implementation

nsIFrame*
NS_NewSVGMaskFrame(nsIPresShell* aPresShell, nsStyleContext* aContext)
{
  return new (aPresShell) nsSVGMaskFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsSVGMaskFrame)

already_AddRefed<gfxPattern>
nsSVGMaskFrame::ComputeMaskAlpha(nsRenderingContext *aContext,
                                 nsIFrame* aParent,
                                 const gfxMatrix &aMatrix,
                                 float aOpacity)
{
  // If the flag is set when we get here, it means this mask frame
  // has already been used painting the current mask, and the document
  // has a mask reference loop.
  if (mInUse) {
    NS_WARNING("Mask loop detected!");
    return nullptr;
  }
  AutoMaskReferencer maskRef(this);

  nsSVGMaskElement *mask = static_cast<nsSVGMaskElement*>(mContent);

  uint16_t units =
    mask->mEnumAttributes[nsSVGMaskElement::MASKUNITS].GetAnimValue();
  gfxRect bbox;
  if (units == nsIDOMSVGUnitTypes::SVG_UNIT_TYPE_OBJECTBOUNDINGBOX) {
    bbox = nsSVGUtils::GetBBox(aParent);
  }

  gfxRect maskArea = nsSVGUtils::GetRelativeRect(units,
    &mask->mLengthAttributes[nsSVGMaskElement::X], bbox, aParent);

  gfxContext *gfx = aContext->ThebesContext();

  // Get the clip extents in device space:
  gfx->Save();
  nsSVGUtils::SetClipRect(gfx, aMatrix, maskArea);
  gfx->IdentityMatrix();
  gfxRect clipExtents = gfx->GetClipExtents();
  clipExtents.RoundOut();
  gfx->Restore();

  bool resultOverflows;
  gfxIntSize surfaceSize =
    nsSVGUtils::ConvertToSurfaceSize(gfxSize(clipExtents.Width(),
                                             clipExtents.Height()),
                                     &resultOverflows);

  // 0 disables mask, < 0 is an error
  if (surfaceSize.width <= 0 || surfaceSize.height <= 0)
    return nullptr;

  if (resultOverflows)
    return nullptr;

  nsRefPtr<gfxImageSurface> image =
    new gfxImageSurface(surfaceSize, gfxASurface::ImageFormatARGB32);
  if (!image || image->CairoStatus())
    return nullptr;

  // We would like to use gfxImageSurface::SetDeviceOffset() to position
  // 'image'. However, we need to set the same matrix on the temporary context
  // and pattern that we create below as is currently set on 'gfx'.
  // Unfortunately, any device offset set by SetDeviceOffset() is affected by
  // the transform passed to the SetMatrix() calls, so to avoid that we account
  // for the device offset in the transform rather than use SetDeviceOffset().
  gfxMatrix matrix =
    gfx->CurrentMatrix() * gfxMatrix().Translate(-clipExtents.TopLeft());

  nsRenderingContext tmpCtx;
  tmpCtx.Init(this->PresContext()->DeviceContext(), image);
  tmpCtx.ThebesContext()->SetMatrix(matrix);

  mMaskParent = aParent;
  if (mMaskParentMatrix) {
    *mMaskParentMatrix = aMatrix;
  } else {
    mMaskParentMatrix = new gfxMatrix(aMatrix);
  }

  for (nsIFrame* kid = mFrames.FirstChild(); kid;
       kid = kid->GetNextSibling()) {
    // The CTM of each frame referencing us can be different
    nsISVGChildFrame* SVGFrame = do_QueryFrame(kid);
    if (SVGFrame) {
      SVGFrame->NotifySVGChanged(nsISVGChildFrame::TRANSFORM_CHANGED);
    }
    nsSVGUtils::PaintFrameWithEffects(&tmpCtx, nullptr, kid);
  }

  uint8_t *data   = image->Data();
  int32_t  stride = image->Stride();

  nsIntRect rect(0, 0, surfaceSize.width, surfaceSize.height);
  nsSVGUtils::UnPremultiplyImageDataAlpha(data, stride, rect);
  if (GetStyleSVG()->mColorInterpolation ==
      NS_STYLE_COLOR_INTERPOLATION_LINEARRGB) {
    nsSVGUtils::ConvertImageDataToLinearRGB(data, stride, rect);
  }

  if (GetStyleSVGReset()->mMaskType == NS_STYLE_MASK_TYPE_LUMINANCE) {
    for (int32_t y = 0; y < surfaceSize.height; y++) {
      for (int32_t x = 0; x < surfaceSize.width; x++) {
        uint8_t *pixel = data + stride * y + 4 * x;

        /* linearRGB -> intensity */
        uint8_t alpha =
          static_cast<uint8_t>
                     ((pixel[GFX_ARGB32_OFFSET_R] * 0.2125 +
                          pixel[GFX_ARGB32_OFFSET_G] * 0.7154 +
                          pixel[GFX_ARGB32_OFFSET_B] * 0.0721) *
                         (pixel[GFX_ARGB32_OFFSET_A] / 255.0) * aOpacity);

        memset(pixel, alpha, 4);
      }
    }
  } else {
    for (int32_t y = 0; y < surfaceSize.height; y++) {
      for (int32_t x = 0; x < surfaceSize.width; x++) {
        uint8_t *pixel = data + stride * y + 4 * x;
        uint8_t alpha = pixel[GFX_ARGB32_OFFSET_A] * aOpacity;
        memset(pixel, alpha, 4);
      }
    }
  }

  gfxPattern *retval = new gfxPattern(image);
  retval->SetMatrix(matrix);
  NS_IF_ADDREF(retval);
  return retval;
}

/* virtual */ void
nsSVGMaskFrame::DidSetStyleContext(nsStyleContext* aOldStyleContext)
{
  nsSVGEffects::InvalidateDirectRenderingObservers(this);
  nsSVGMaskFrameBase::DidSetStyleContext(aOldStyleContext);
}

NS_IMETHODIMP
nsSVGMaskFrame::AttributeChanged(int32_t  aNameSpaceID,
                                 nsIAtom* aAttribute,
                                 int32_t  aModType)
{
  if (aNameSpaceID == kNameSpaceID_None &&
      (aAttribute == nsGkAtoms::x ||
       aAttribute == nsGkAtoms::y ||
       aAttribute == nsGkAtoms::width ||
       aAttribute == nsGkAtoms::height||
       aAttribute == nsGkAtoms::maskUnits ||
       aAttribute == nsGkAtoms::maskContentUnits)) {
    nsSVGEffects::InvalidateDirectRenderingObservers(this);
  }

  return nsSVGMaskFrameBase::AttributeChanged(aNameSpaceID,
                                              aAttribute, aModType);
}

#ifdef DEBUG
NS_IMETHODIMP
nsSVGMaskFrame::Init(nsIContent* aContent,
                     nsIFrame* aParent,
                     nsIFrame* aPrevInFlow)
{
  nsCOMPtr<nsIDOMSVGMaskElement> mask = do_QueryInterface(aContent);
  NS_ASSERTION(mask, "Content is not an SVG mask");

  return nsSVGMaskFrameBase::Init(aContent, aParent, aPrevInFlow);
}
#endif /* DEBUG */

nsIAtom *
nsSVGMaskFrame::GetType() const
{
  return nsGkAtoms::svgMaskFrame;
}

gfxMatrix
nsSVGMaskFrame::GetCanvasTM(uint32_t aFor)
{
  NS_ASSERTION(mMaskParentMatrix, "null parent matrix");

  nsSVGMaskElement *mask = static_cast<nsSVGMaskElement*>(mContent);

  return nsSVGUtils::AdjustMatrixForUnits(
    mMaskParentMatrix ? *mMaskParentMatrix : gfxMatrix(),
    &mask->mEnumAttributes[nsSVGMaskElement::MASKCONTENTUNITS],
    mMaskParent);
}

