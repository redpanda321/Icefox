/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __NS_SVGPAINTSERVERFRAME_H__
#define __NS_SVGPAINTSERVERFRAME_H__

#include "nsCOMPtr.h"
#include "nsFrame.h"
#include "nsIFrame.h"
#include "nsQueryFrame.h"
#include "nsSVGContainerFrame.h"
#include "nsSVGUtils.h"

class gfxContext;
class gfxPattern;
class nsStyleContext;
class nsSVGGeometryFrame;

struct gfxRect;

typedef nsSVGContainerFrame nsSVGPaintServerFrameBase;

class nsSVGPaintServerFrame : public nsSVGPaintServerFrameBase
{
protected:
  nsSVGPaintServerFrame(nsStyleContext* aContext)
    : nsSVGPaintServerFrameBase(aContext)
  {
    AddStateBits(NS_STATE_SVG_NONDISPLAY_CHILD);
  }

public:
  NS_DECL_FRAMEARENA_HELPERS

  /**
   * Constructs a gfxPattern of the paint server rendering.
   *
   * @param aContextMatrix The transform matrix that is currently applied to
   *   the gfxContext that is being drawn to. This is needed by SVG patterns so
   *   that surfaces of the correct size can be created. (SVG gradients are
   *   vector based, so it's not used there.)
   */
  virtual already_AddRefed<gfxPattern>
    GetPaintServerPattern(nsIFrame *aSource,
                          const gfxMatrix& aContextMatrix,
                          nsStyleSVGPaint nsStyleSVG::*aFillOrStroke,
                          float aOpacity,
                          const gfxRect *aOverrideBounds = nullptr) = 0;

  /**
   * Configure paint server prior to rendering
   * @return false to skip rendering
   */
  virtual bool SetupPaintServer(gfxContext *aContext,
                                nsIFrame *aSource,
                                nsStyleSVGPaint nsStyleSVG::*aFillOrStroke,
                                float aOpacity);

  // nsIFrame methods:
  NS_IMETHOD BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                              const nsRect&           aDirtyRect,
                              const nsDisplayListSet& aLists) {
    return NS_OK;
  }

  virtual bool IsFrameOfType(uint32_t aFlags) const
  {
    return nsSVGPaintServerFrameBase::IsFrameOfType(aFlags & ~nsIFrame::eSVGPaintServer);
  }
};

#endif // __NS_SVGPAINTSERVERFRAME_H__
