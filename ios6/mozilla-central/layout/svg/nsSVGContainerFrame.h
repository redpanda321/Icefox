/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NS_SVGCONTAINERFRAME_H
#define NS_SVGCONTAINERFRAME_H

#include "mozilla/Attributes.h"
#include "gfxMatrix.h"
#include "gfxRect.h"
#include "nsContainerFrame.h"
#include "nsFrame.h"
#include "nsIFrame.h"
#include "nsISVGChildFrame.h"
#include "nsQueryFrame.h"
#include "nsRect.h"
#include "nsSVGUtils.h"

class nsFrameList;
class nsIContent;
class nsIPresShell;
class nsRenderingContext;
class nsStyleContext;

struct nsPoint;

typedef nsContainerFrame nsSVGContainerFrameBase;

/**
 * Base class for SVG container frames. Frame sub-classes that do not
 * display their contents directly (such as the frames for <marker> or
 * <pattern>) just inherit this class. Frame sub-classes that do or can
 * display their contents directly (such as the frames for inner-<svg> or
 * <g>) inherit our nsDisplayContainerFrame sub-class.
 *
 *                               *** WARNING ***
 *
 * Do *not* blindly cast to SVG element types in this class's methods (see the
 * warning comment for nsSVGDisplayContainerFrame below). 
 */
class nsSVGContainerFrame : public nsSVGContainerFrameBase
{
  friend nsIFrame* NS_NewSVGContainerFrame(nsIPresShell* aPresShell,
                                           nsStyleContext* aContext);
protected:
  nsSVGContainerFrame(nsStyleContext* aContext)
    : nsSVGContainerFrameBase(aContext)
  {
    AddStateBits(NS_FRAME_SVG_LAYOUT);
  }

public:
  NS_DECL_QUERYFRAME_TARGET(nsSVGContainerFrame)
  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS

  // Returns the transform to our gfxContext (to device pixels, not CSS px)
  virtual gfxMatrix GetCanvasTM(uint32_t aFor) {
    return gfxMatrix();
  }

  /**
   * Returns true if the frame's content has a transform that applies only to
   * its children, and not to the frame itself. For example, an implicit
   * transform introduced by a 'viewBox' attribute, or an explicit transform
   * due to a root-<svg> having its currentScale/currentTransform properties
   * set. If aTransform is non-null, then it will be set to the transform.
   */
  virtual bool HasChildrenOnlyTransform(gfxMatrix *aTransform) const {
    return false;
  }

  // nsIFrame:
  NS_IMETHOD AppendFrames(ChildListID     aListID,
                          nsFrameList&    aFrameList);
  NS_IMETHOD InsertFrames(ChildListID     aListID,
                          nsIFrame*       aPrevFrame,
                          nsFrameList&    aFrameList);
  NS_IMETHOD RemoveFrame(ChildListID     aListID,
                         nsIFrame*       aOldFrame);

  virtual bool IsFrameOfType(uint32_t aFlags) const
  {
    return nsSVGContainerFrameBase::IsFrameOfType(
            aFlags & ~(nsIFrame::eSVG | nsIFrame::eSVGContainer));
  }

  NS_IMETHOD BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                              const nsRect&           aDirtyRect,
                              const nsDisplayListSet& aLists) {
    return NS_OK;
  }

  virtual bool UpdateOverflow();
};

/**
 * Frame class or base-class for SVG containers that can or do display their
 * contents directly.
 *
 *                               *** WARNING ***
 *
 * This class's methods can *not* assume that mContent points to an instance of
 * an SVG element class since this class is inherited by
 * nsSVGGenericContainerFrame which is used for unrecognized elements in the
 * SVG namespace. Do *not* blindly cast to SVG element types.
 */
class nsSVGDisplayContainerFrame : public nsSVGContainerFrame,
                                   public nsISVGChildFrame
{
protected:
  nsSVGDisplayContainerFrame(nsStyleContext* aContext)
    : nsSVGContainerFrame(aContext)
  {
     AddStateBits(NS_FRAME_MAY_BE_TRANSFORMED);
  }

public:
  NS_DECL_QUERYFRAME_TARGET(nsSVGDisplayContainerFrame)
  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS

  // nsIFrame:
  NS_IMETHOD InsertFrames(ChildListID     aListID,
                          nsIFrame*       aPrevFrame,
                          nsFrameList&    aFrameList) MOZ_OVERRIDE;
  NS_IMETHOD RemoveFrame(ChildListID     aListID,
                         nsIFrame*       aOldFrame) MOZ_OVERRIDE;
  NS_IMETHOD Init(nsIContent*      aContent,
                  nsIFrame*        aParent,
                  nsIFrame*        aPrevInFlow);

  NS_IMETHOD BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                              const nsRect&           aDirtyRect,
                              const nsDisplayListSet& aLists) MOZ_OVERRIDE;

  virtual bool IsSVGTransformed(gfxMatrix *aOwnTransform = nullptr,
                                gfxMatrix *aFromParentTransform = nullptr) const;

  // nsISVGChildFrame interface:
  NS_IMETHOD PaintSVG(nsRenderingContext* aContext,
                      const nsIntRect *aDirtyRect);
  NS_IMETHOD_(nsIFrame*) GetFrameForPoint(const nsPoint &aPoint) MOZ_OVERRIDE;
  NS_IMETHOD_(nsRect) GetCoveredRegion() MOZ_OVERRIDE;
  virtual void ReflowSVG() MOZ_OVERRIDE;
  virtual void NotifySVGChanged(uint32_t aFlags) MOZ_OVERRIDE;
  virtual SVGBBox GetBBoxContribution(const gfxMatrix &aToBBoxUserspace,
                                      uint32_t aFlags) MOZ_OVERRIDE;
  NS_IMETHOD_(bool) IsDisplayContainer() MOZ_OVERRIDE { return true; }
};

#endif
