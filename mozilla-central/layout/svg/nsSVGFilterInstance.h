/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __NS_SVGFILTERINSTANCE_H__
#define __NS_SVGFILTERINSTANCE_H__

#include "gfxMatrix.h"
#include "gfxPoint.h"
#include "gfxRect.h"
#include "nsCOMPtr.h"
#include "nsHashKeys.h"
#include "nsPoint.h"
#include "nsRect.h"
#include "nsSize.h"
#include "nsSVGFilters.h"
#include "nsSVGNumber2.h"
#include "nsSVGNumberPair.h"
#include "nsTArray.h"

class gfxASurface;
class gfxImageSurface;
class nsIFrame;
class nsSVGFilterElement;
class nsSVGFilterPaintCallback;

/**
 * This class performs all filter processing.
 * 
 * We build a graph of the filter image data flow, essentially
 * converting the filter graph to SSA. This lets us easily propagate
 * analysis data (such as bounding-boxes) over the filter primitive graph.
 *
 * Definition of "filter space": filter space is a coordinate system that is
 * aligned with the user space of the filtered element, with its origin located
 * at the top left of the filter region (as specified by our ctor's
 * aFilterRegion, and returned by our GetFilterRegion, specifically), and with
 * one unit equal in size to one pixel of the offscreen surface into which the
 * filter output would/will be painted.
 *
 * The definition of "filter region" can be found here:
 * http://www.w3.org/TR/SVG11/filters.html#FilterEffectsRegion
 */
class NS_STACK_CLASS nsSVGFilterInstance
{
public:
  /**
   * @param aTargetFrame The frame of the filtered element under consideration.
   * @param aPaintCallback [optional] The callback that Render() should use to
   *   paint. Only required if you will call Render().
   * @param aFilterElement The filter element referenced by aTargetFrame's
   *   element.
   * @param aTargetBBox The filtered element's bbox, in the filtered element's
   *   user space.
   * @param aFilterRegion The "filter region", in the filtered element's user
   *   space. The caller must have already expanded the region out so that its
   *   edges coincide with pixel boundaries in the offscreen surface that
   *   would/will be created to paint the filter output.
   * @param aFilterSpaceSize The size of the user specified "filter region",
   *   in filter space units.
   * @param aFilterSpaceToDeviceSpaceTransform The transform from filter
   *   space to outer-<svg> device space.
   * @param aTargetBounds The pre-filter paint bounds of the filtered element,
   *   in filter space.
   * @param aPostFilterDirtyRect [optional] The bounds of the post-filter area
   *   that has to be repainted, in filter space. Only required if you will
   *   call ComputeSourceNeededRect() or Render().
   * @param aPreFilterDirtyRect [optional] The bounds of the pre-filter area of
   *   the filtered element that changed, in filter space. Only required if you
   *   will call ComputePostFilterDirtyRect().
   * @param aPrimitiveUnits The value from the 'primitiveUnits' attribute.
   */
  nsSVGFilterInstance(nsIFrame *aTargetFrame,
                      nsSVGFilterPaintCallback *aPaintCallback,
                      const nsSVGFilterElement *aFilterElement,
                      const gfxRect &aTargetBBox,
                      const gfxRect& aFilterRegion,
                      const nsIntSize& aFilterSpaceSize,
                      const gfxMatrix &aFilterSpaceToDeviceSpaceTransform,
                      const gfxMatrix &aFilterSpaceToFrameSpaceInCSSPxTransform,
                      const nsIntRect& aTargetBounds,
                      const nsIntRect& aPostFilterDirtyRect,
                      const nsIntRect& aPreFilterDirtyRect,
                      uint16_t aPrimitiveUnits) :
    mTargetFrame(aTargetFrame),
    mPaintCallback(aPaintCallback),
    mFilterElement(aFilterElement),
    mTargetBBox(aTargetBBox),
    mFilterSpaceToDeviceSpaceTransform(aFilterSpaceToDeviceSpaceTransform),
    mFilterSpaceToFrameSpaceInCSSPxTransform(aFilterSpaceToFrameSpaceInCSSPxTransform),
    mFilterRegion(aFilterRegion),
    mFilterSpaceSize(aFilterSpaceSize),
    mSurfaceRect(nsIntPoint(0, 0), aFilterSpaceSize),
    mTargetBounds(aTargetBounds),
    mPostFilterDirtyRect(aPostFilterDirtyRect),
    mPreFilterDirtyRect(aPreFilterDirtyRect),
    mPrimitiveUnits(aPrimitiveUnits) {
  }

  /**
   * Returns the user specified "filter region", in the filtered element's user
   * space, after it has been adjusted out (if necessary) so that its edges
   * coincide with pixel boundaries of the offscreen surface into which the
   * filtered output would/will be painted.
   */
  gfxRect GetFilterRegion() const { return mFilterRegion; }

  /**
   * Returns the size of the user specified "filter region", in filter space.
   * The size will be {filterRes.x by filterRes.y}, whether the user specified
   * the filter's filterRes attribute explicitly, or the implementation chose
   * the filterRes values. (The top-left of the filter region is the origin of
   * filter space, which is why this method returns an nsIntSize and not an
   * nsIntRect.)
   */
  const nsIntSize& GetFilterSpaceSize() { return mFilterSpaceSize; }
  uint32_t GetFilterResX() const { return mFilterSpaceSize.width; }
  uint32_t GetFilterResY() const { return mFilterSpaceSize.height; }

  /**
   * Returns the dimensions of the offscreen surface that is required for the
   * output from the current filter operation, in filter space. This rect is
   * clipped to, and therefore guaranteed to be fully contained by, the filter
   * region.
   */
  const nsIntRect& GetSurfaceRect() const { return mSurfaceRect; }
  int32_t GetSurfaceWidth() const { return mSurfaceRect.width; }
  int32_t GetSurfaceHeight() const { return mSurfaceRect.height; }

  /**
   * Allocates a gfxASurface, renders the filtered element into the surface,
   * and then returns the surface via the aOutput outparam. The area that
   * needs to be painted must have been specified before calling this method
   * by passing it as the aPostFilterDirtyRect argument to the
   * nsSVGFilterInstance constructor.
   */
  nsresult Render(gfxASurface** aOutput);

  /**
   * Sets the aPostFilterDirtyRect outparam to the post-filter bounds in filter
   * space of the area that would be dirtied by mTargetFrame when a given
   * pre-filter area of mTargetFrame is dirtied. The pre-filter area must have
   * been specified before calling this method by passing it as the
   * aPreFilterDirtyRect argument to the nsSVGFilterInstance constructor.
   */
  nsresult ComputePostFilterDirtyRect(nsIntRect* aPostFilterDirtyRect);

  /**
   * Sets the aDirty outparam to the pre-filter bounds in filter space of the
   * area of mTargetFrame that is needed in order to paint the filtered output
   * for a given post-filter dirtied area. The post-filter area must have been
   * specified before calling this method by passing it as the aPostFilterDirtyRect
   * argument to the nsSVGFilterInstance constructor.
   */
  nsresult ComputeSourceNeededRect(nsIntRect* aDirty);

  /**
   * Sets the aDirty outparam to the post-filter bounds in filter space of the
   * area that would be dirtied by mTargetFrame if its entire pre-filter area
   * is dirtied.
   */
  nsresult ComputeOutputBBox(nsIntRect* aBBox);

  float GetPrimitiveNumber(uint8_t aCtxType, const nsSVGNumber2 *aNumber) const
  {
    return GetPrimitiveNumber(aCtxType, aNumber->GetAnimValue());
  }
  float GetPrimitiveNumber(uint8_t aCtxType, const nsSVGNumberPair *aNumberPair,
                           nsSVGNumberPair::PairIndex aIndex) const
  {
    return GetPrimitiveNumber(aCtxType, aNumberPair->GetAnimValue(aIndex));
  }

  /**
   * Converts a userSpaceOnUse/objectBoundingBoxUnits unitless point and length
   * into filter space, depending on the value of mPrimitiveUnits. (For
   * objectBoundingBoxUnits, the bounding box offset is applied to the point.)
   */
  void ConvertLocation(float aValues[3]) const;

  /**
   * Returns the transform from the filtered element's user space to filter
   * space. This will be a simple translation and/or scale.
   */
  gfxMatrix GetUserSpaceToFilterSpaceTransform() const;

  /**
   * Returns the transform from filter space to outer-<svg> device space.
   */
  gfxMatrix GetFilterSpaceToDeviceSpaceTransform() const {
    return mFilterSpaceToDeviceSpaceTransform;
  }

  gfxPoint FilterSpaceToUserSpace(const gfxPoint& aPt) const;

  /**
   * Returns the transform from filter space to frame space, in CSS px. This
   * transform does not transform to frame space in its normal app units, since
   * app units are ints, requiring appropriate rounding which can't be done by
   * a transform matrix. Callers have to do that themselves as appropriate for
   * their needs.
   */
  gfxMatrix GetFilterSpaceToFrameSpaceInCSSPxTransform() const {
    return mFilterSpaceToFrameSpaceInCSSPxTransform;
  }

  int32_t AppUnitsPerCSSPixel() const {
    return mTargetFrame->PresContext()->AppUnitsPerCSSPixel();
  }

private:
  typedef nsSVGFE::Image Image;
  typedef nsSVGFE::ColorModel ColorModel;

  struct PrimitiveInfo {
    /// Pointer to the filter primitive element.
    nsSVGFE*  mFE;

    /**
     * The filter space bounds of this filter primitive's output, were a full
     * repaint of mTargetFrame to occur. Note that a filter primitive's output
     * (and hence this rect) is always clipped to both the filter region and
     * to the filter primitive subregion.
     * XXX maybe rename this to mMaxBounds?
     */
    nsIntRect mResultBoundingBox;

    /**
     * The filter space bounds of this filter primitive's output, were we to
     * repaint a given post-filter dirty area, and were we to minimize
     * repainting for that dirty area. In other words this is the part of the
     * primitive's output that is needed by other primitives or the final
     * filtered output in order to repaint that area. This rect is guaranteed
     * to be contained within mResultBoundingBox and, if we're only painting
     * part of the filtered output, may be smaller. This rect is used when
     * calling Render() or ComputeSourceNeededRect().
     * XXX maybe rename this to just mNeededBounds?
     */
    nsIntRect mResultNeededBox;

    /**
     * The filter space bounds of this filter primitive's output, were only
     * part of mTargetFrame's pre-filter output to be dirtied, and were we to
     * minimize repainting for that dirty area. This is used when calculating
     * the area that needs to be invalidated when only part of a filtered
     * element is dirtied. This rect is guaranteed to be contained within
     * mResultBoundingBox.
     */
    nsIntRect mResultChangeBox;

    Image     mImage;

    /**
     * The number of times that this filter primitive's output is used as an
     * input by other filter primitives in the filter graph.
     * XXX seems like we could better use this to avoid creating images for
     * primitives that are not used, or whose ouput in not used during the
     * current operation.
     */
    int32_t   mImageUsers;
  
    // Can't use nsAutoTArray here, because these Info objects
    // live in nsTArrays themselves and nsTArray moves the elements
    // around in memory, which breaks nsAutoTArray.
    nsTArray<PrimitiveInfo*> mInputs;

    PrimitiveInfo() : mFE(nullptr), mImageUsers(0) {}
  };

  class ImageAnalysisEntry : public nsStringHashKey {
  public:
    ImageAnalysisEntry(KeyTypePointer aStr) : nsStringHashKey(aStr) { }
    ImageAnalysisEntry(const ImageAnalysisEntry& toCopy) : nsStringHashKey(toCopy),
      mInfo(toCopy.mInfo) { }

    PrimitiveInfo* mInfo;
  };

  /**
   * Initializes the keyword nodes e.g. SourceGraphic (i.e. sets
   * .mImage.mFilterPrimitiveSubregion and .mResultBoundingBox on
   * mSourceColorAlpha and mSourceAlpha).
   */
  nsresult BuildSources();

  /**
   * Creates a gfxImageSurface for either the FillPaint or StrokePaint graph
   * nodes
   */
  nsresult BuildSourcePaint(PrimitiveInfo *aPrimitive);

  /**
   * Creates a gfxImageSurface for either the FillPaint and StrokePaint graph
   * nodes, fills its contents and assigns it to mFillPaint.mImage.mImage and
   * mStrokePaint.mImage.mImage respectively.
   */
  nsresult BuildSourcePaints();

  /**
   * Creates the gfxImageSurfaces for the SourceGraphic and SourceAlpha graph
   * nodes, paints their contents, and assigns them to
   * mSourceColorAlpha.mImage.mImage and mSourceAlpha.mImage.mImage
   * respectively.
   */
  nsresult BuildSourceImages();

  /**
   * Build the graph of PrimitiveInfo nodes that describes the filter's filter
   * primitives and their connections. This populates mPrimitives, and sets
   * each PrimitiveInfo's mFE, mInputs, mImageUsers, mFilterPrimitiveSubregion,
   * etc.
   */
  nsresult BuildPrimitives();

  /**
   * Compute the filter space bounds of the output from each primitive, were we
   * to do a full repaint of mTargetFrame. This sets mResultBoundingBox on the
   * items in the filter graph, based on the mResultBoundingBox of each item's
   * inputs, and clipped to the filter region and each primitive's filter
   * primitive subregion.
   */
  void ComputeResultBoundingBoxes();

  /**
   * Computes the filter space bounds of the areas that we actually *need* from
   * each filter primitive's output, based on the value of mPostFilterDirtyRect.
   * This sets mResultNeededBox on the items in the filter graph.
   */
   void ComputeNeededBoxes();

  /**
   * Computes the filter space bounds of the area of each filter primitive
   * that will change, based on the value of mPreFilterDirtyRect.
   * This sets mResultChangeBox on the items in the filter graph.
   */
  void ComputeResultChangeBoxes();

  /**
   * Computes and returns the union of all mResultNeededBox rects in the filter
   * graph. This is useful for deciding the size of the offscreen surface that
   * needs to be created for the filter operation.
   */
  nsIntRect ComputeUnionOfAllNeededBoxes();

  /**
   * Allocates and returns a surface of mSurfaceRect.Size(), and with a device
   * offset of -mSurfaceRect.TopLeft(). The surface is cleared to transparent
   * black.
   */
  already_AddRefed<gfxImageSurface> CreateImage();

  /**
   * Computes and sets mFilterPrimitiveSubregion for the given primitive.
   */
  void ComputeFilterPrimitiveSubregion(PrimitiveInfo* aInfo);

  /**
   * If the color model of the pixel data in the aPrimitive's image isn't
   * already aColorModel, then this method converts its pixel data to that
   * color model.
   */
  void EnsureColorModel(PrimitiveInfo* aPrimitive,
                        ColorModel aColorModel);

  /**
   * Scales a numeric filter primitive length in the X, Y or "XY" directions
   * into a length in filter space (no offset is applied).
   */
  float GetPrimitiveNumber(uint8_t aCtxType, float aValue) const;

  gfxRect UserSpaceToFilterSpace(const gfxRect& aUserSpace) const;

  /**
   * Clip the filter space rect aRect to the filter region.
   */
  void ClipToFilterSpace(nsIntRect* aRect) const
  {
    nsIntRect filterSpace(nsIntPoint(0, 0), mFilterSpaceSize);
    aRect->IntersectRect(*aRect, filterSpace);
  }

  /**
   * The frame for the element that is currently being filtered.
   */
  nsIFrame*               mTargetFrame;

  nsSVGFilterPaintCallback* mPaintCallback;
  const nsSVGFilterElement* mFilterElement;

  /**
   * The SVG bbox of the element that is being filtered, in user space.
   */
  gfxRect                 mTargetBBox;

  gfxMatrix               mFilterSpaceToDeviceSpaceTransform;
  gfxMatrix               mFilterSpaceToFrameSpaceInCSSPxTransform;
  gfxRect                 mFilterRegion;
  nsIntSize               mFilterSpaceSize;
  nsIntRect               mSurfaceRect;

  /**
   * Pre-filter paint bounds of the element that is being filtered, in filter
   * space.
   */
  nsIntRect               mTargetBounds;

  /**
   * If set, this is the filter space bounds of the outer-<svg> device space
   * bounds of the dirty area that needs to be repainted. (As bounds-of-bounds,
   * this may be a fair bit bigger than we actually need, unfortunately.)
   */
  nsIntRect               mPostFilterDirtyRect;

  /**
   * If set, this is the filter space bounds of the outer-<svg> device bounds
   * of the pre-filter area of the filtered element that changed. (As
   * bounds-of-bounds, this may be a fair bit bigger than we actually need,
   * unfortunately.)
   */
  nsIntRect               mPreFilterDirtyRect;

  /**
   * The 'primitiveUnits' attribute value (objectBoundingBox or userSpaceOnUse).
   */
  uint16_t                mPrimitiveUnits;

  PrimitiveInfo           mSourceColorAlpha;
  PrimitiveInfo           mSourceAlpha;
  PrimitiveInfo           mFillPaint;
  PrimitiveInfo           mStrokePaint;
  nsTArray<PrimitiveInfo> mPrimitives;
};

#endif
