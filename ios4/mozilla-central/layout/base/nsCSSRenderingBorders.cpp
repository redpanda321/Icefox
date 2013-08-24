/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
// vim:cindent:ts=2:et:sw=2:
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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 *   Mozilla Corporation
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@pobox.com>
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

#include "nsStyleConsts.h"
#include "nsIFrame.h"
#include "nsPoint.h"
#include "nsRect.h"
#include "nsIViewManager.h"
#include "nsFrameManager.h"
#include "nsStyleContext.h"
#include "nsGkAtoms.h"
#include "nsCSSAnonBoxes.h"
#include "nsTransform2D.h"
#include "nsIDeviceContext.h"
#include "nsIContent.h"
#include "nsIDocument.h"
#include "nsIScrollableFrame.h"
#include "imgIRequest.h"
#include "imgIContainer.h"
#include "nsCSSRendering.h"
#include "nsCSSColorUtils.h"
#include "nsITheme.h"
#include "nsThemeConstants.h"
#include "nsIServiceManager.h"
#include "nsIHTMLDocument.h"
#include "nsLayoutUtils.h"
#include "nsINameSpaceManager.h"
#include "nsBlockFrame.h"

#include "gfxContext.h"

#include "nsCSSRenderingBorders.h"

/**
 * nsCSSRendering::PaintBorder
 * nsCSSRendering::PaintOutline
 *   -> DrawBorders
 *
 * DrawBorders
 *   |- separate corners?
 *   |- dashed side mask
 *   |
 *   -> can border be drawn in 1 pass? (e.g., solid border same color all around)
 *      |- DrawBorderSides with all 4 sides
 *   -> more than 1 pass?
 *      |- for each corner
 *         |- clip to DoCornerClipSubPath
 *         |- PushGroup
 *         |- for each side adjacent to corner
 *            |- clip to DoSideClipSubPath
 *            |- DrawBorderSides with one side
 *         |- PopGroup
 *      |- for each side
 *         |- DoSideClipWithoutCornersSubPath
 *         |- DrawDashedSide || DrawBorderSides with one side
 */

static void ComputeBorderCornerDimensions(const gfxRect& aOuterRect,
                                          const gfxRect& aInnerRect,
                                          const gfxCornerSizes& aRadii,
                                          gfxCornerSizes *aDimsResult);

// given a side index, get the previous and next side index
#define NEXT_SIDE(_s) mozilla::css::Side(((_s) + 1) & 3)
#define PREV_SIDE(_s) mozilla::css::Side(((_s) + 3) & 3)

// from the given base color and the background color, turn
// color into a color for the given border pattern style
static gfxRGBA MakeBorderColor(const gfxRGBA& aColor,
                               const gfxRGBA& aBackgroundColor,
                               BorderColorStyle aBorderColorStyle);


// Given a line index (an index starting from the outside of the
// border going inwards) and an array of line styles, calculate the
// color that that stripe of the border should be rendered in.
static gfxRGBA ComputeColorForLine(PRUint32 aLineIndex,
                                   const BorderColorStyle* aBorderColorStyle,
                                   PRUint32 aBorderColorStyleCount,
                                   nscolor aBorderColor,
                                   nscolor aBackgroundColor);

static gfxRGBA ComputeCompositeColorForLine(PRUint32 aLineIndex,
                                            const nsBorderColors* aBorderColors);

// little helper function to check if the array of 4 floats given are
// equal to the given value
static PRBool
CheckFourFloatsEqual(const gfxFloat *vals, gfxFloat k)
{
  return (vals[0] == k &&
          vals[1] == k &&
          vals[2] == k &&
          vals[3] == k);
}

static bool
IsZeroSize(const gfxSize& sz) {
  return sz.width == 0.0 || sz.height == 0.0;
}

static bool
AllCornersZeroSize(const gfxCornerSizes& corners) {
  return IsZeroSize(corners[NS_CORNER_TOP_LEFT]) &&
    IsZeroSize(corners[NS_CORNER_TOP_RIGHT]) &&
    IsZeroSize(corners[NS_CORNER_BOTTOM_RIGHT]) &&
    IsZeroSize(corners[NS_CORNER_BOTTOM_LEFT]);
}

typedef enum {
  // Normal solid square corner.  Will be rectangular, the size of the
  // adjacent sides.  If the corner has a border radius, the corner
  // will always be solid, since we don't do dotted/dashed etc.
  CORNER_NORMAL,

  // Paint the corner in whatever style is not dotted/dashed of the
  // adjacent corners.
  CORNER_SOLID,

  // Paint the corner as a dot, the size of the bigger of the adjacent
  // sides.
  CORNER_DOT
} CornerStyle;

nsCSSBorderRenderer::nsCSSBorderRenderer(PRInt32 aAppUnitsPerPixel,
                                         gfxContext* aDestContext,
                                         gfxRect& aOuterRect,
                                         const PRUint8* aBorderStyles,
                                         const gfxFloat* aBorderWidths,
                                         gfxCornerSizes& aBorderRadii,
                                         const nscolor* aBorderColors,
                                         nsBorderColors* const* aCompositeColors,
                                         PRIntn aSkipSides,
                                         nscolor aBackgroundColor)
  : mContext(aDestContext),
    mOuterRect(aOuterRect),
    mBorderStyles(aBorderStyles),
    mBorderWidths(aBorderWidths),
    mBorderRadii(aBorderRadii),
    mBorderColors(aBorderColors),
    mCompositeColors(aCompositeColors),
    mAUPP(aAppUnitsPerPixel),
    mSkipSides(aSkipSides),
    mBackgroundColor(aBackgroundColor)
{
  if (!mCompositeColors) {
    static nsBorderColors * const noColors[4] = { NULL };
    mCompositeColors = &noColors[0];
  }

  mInnerRect = mOuterRect;
  mInnerRect.Inset(mBorderWidths[0], mBorderWidths[1], mBorderWidths[2], mBorderWidths[3]);

  ComputeBorderCornerDimensions(mOuterRect, mInnerRect, mBorderRadii, &mBorderCornerDimensions);

  mOneUnitBorder = CheckFourFloatsEqual(mBorderWidths, 1.0);
  mNoBorderRadius = AllCornersZeroSize(mBorderRadii);
}

/* static */ void
nsCSSBorderRenderer::ComputeInnerRadii(const gfxCornerSizes& aRadii,
                                       const gfxFloat *aBorderSizes,
                                       gfxCornerSizes *aInnerRadiiRet)
{
  gfxCornerSizes& iRadii = *aInnerRadiiRet;

  iRadii[C_TL].width = NS_MAX(0.0, aRadii[C_TL].width - aBorderSizes[NS_SIDE_LEFT]);
  iRadii[C_TL].height = NS_MAX(0.0, aRadii[C_TL].height - aBorderSizes[NS_SIDE_TOP]);

  iRadii[C_TR].width = NS_MAX(0.0, aRadii[C_TR].width - aBorderSizes[NS_SIDE_RIGHT]);
  iRadii[C_TR].height = NS_MAX(0.0, aRadii[C_TR].height - aBorderSizes[NS_SIDE_TOP]);

  iRadii[C_BR].width = NS_MAX(0.0, aRadii[C_BR].width - aBorderSizes[NS_SIDE_RIGHT]);
  iRadii[C_BR].height = NS_MAX(0.0, aRadii[C_BR].height - aBorderSizes[NS_SIDE_BOTTOM]);

  iRadii[C_BL].width = NS_MAX(0.0, aRadii[C_BL].width - aBorderSizes[NS_SIDE_LEFT]);
  iRadii[C_BL].height = NS_MAX(0.0, aRadii[C_BL].height - aBorderSizes[NS_SIDE_BOTTOM]);
}

/*static*/ void
ComputeBorderCornerDimensions(const gfxRect& aOuterRect,
                              const gfxRect& aInnerRect,
                              const gfxCornerSizes& aRadii,
                              gfxCornerSizes *aDimsRet)
{
  gfxFloat topWidth = aInnerRect.pos.y - aOuterRect.pos.y;
  gfxFloat leftWidth = aInnerRect.pos.x - aOuterRect.pos.x;
  gfxFloat rightWidth = aOuterRect.size.width - aInnerRect.size.width - leftWidth;
  gfxFloat bottomWidth = aOuterRect.size.height - aInnerRect.size.height - topWidth;

  if (AllCornersZeroSize(aRadii)) {
    // These will always be in pixel units from CSS
    (*aDimsRet)[C_TL] = gfxSize(leftWidth, topWidth);
    (*aDimsRet)[C_TR] = gfxSize(rightWidth, topWidth);
    (*aDimsRet)[C_BR] = gfxSize(rightWidth, bottomWidth);
    (*aDimsRet)[C_BL] = gfxSize(leftWidth, bottomWidth);
  } else {
    // Always round up to whole pixels for the corners; it's safe to
    // make the corners bigger than necessary, and this way we ensure
    // that we avoid seams.
    (*aDimsRet)[C_TL] = gfxSize(ceil(NS_MAX(leftWidth, aRadii[C_TL].width)),
                                ceil(NS_MAX(topWidth, aRadii[C_TL].height)));
    (*aDimsRet)[C_TR] = gfxSize(ceil(NS_MAX(rightWidth, aRadii[C_TR].width)),
                                ceil(NS_MAX(topWidth, aRadii[C_TR].height)));
    (*aDimsRet)[C_BR] = gfxSize(ceil(NS_MAX(rightWidth, aRadii[C_BR].width)),
                                ceil(NS_MAX(bottomWidth, aRadii[C_BR].height)));
    (*aDimsRet)[C_BL] = gfxSize(ceil(NS_MAX(leftWidth, aRadii[C_BL].width)),
                                ceil(NS_MAX(bottomWidth, aRadii[C_BL].height)));
  }
}

PRBool
nsCSSBorderRenderer::AreBorderSideFinalStylesSame(PRUint8 aSides)
{
  NS_ASSERTION(aSides != 0 && (aSides & ~SIDE_BITS_ALL) == 0,
               "AreBorderSidesSame: invalid whichSides!");

  /* First check if the specified styles and colors are the same for all sides */
  int firstStyle = 0;
  NS_FOR_CSS_SIDES (i) {
    if (firstStyle == i) {
      if (((1 << i) & aSides) == 0)
        firstStyle++;
      continue;
    }

    if (((1 << i) & aSides) == 0) {
      continue;
    }

    if (mBorderStyles[firstStyle] != mBorderStyles[i] ||
        mBorderColors[firstStyle] != mBorderColors[i] ||
        !nsBorderColors::Equal(mCompositeColors[firstStyle],
                               mCompositeColors[i]))
      return PR_FALSE;
  }

  /* Then if it's one of the two-tone styles and we're not
   * just comparing the TL or BR sides */
  switch (mBorderStyles[firstStyle]) {
    case NS_STYLE_BORDER_STYLE_GROOVE:
    case NS_STYLE_BORDER_STYLE_RIDGE:
    case NS_STYLE_BORDER_STYLE_INSET:
    case NS_STYLE_BORDER_STYLE_OUTSET:
      return ((aSides & ~(SIDE_BIT_TOP | SIDE_BIT_LEFT)) == 0 ||
              (aSides & ~(SIDE_BIT_BOTTOM | SIDE_BIT_RIGHT)) == 0);
  }

  return PR_TRUE;
}

PRBool
nsCSSBorderRenderer::IsSolidCornerStyle(PRUint8 aStyle, mozilla::css::Corner aCorner)
{
  switch (aStyle) {
    case NS_STYLE_BORDER_STYLE_DOTTED:
    case NS_STYLE_BORDER_STYLE_DASHED:
    case NS_STYLE_BORDER_STYLE_SOLID:
      return PR_TRUE;

    case NS_STYLE_BORDER_STYLE_INSET:
    case NS_STYLE_BORDER_STYLE_OUTSET:
      return (aCorner == NS_CORNER_TOP_LEFT || aCorner == NS_CORNER_BOTTOM_RIGHT);

    case NS_STYLE_BORDER_STYLE_GROOVE:
    case NS_STYLE_BORDER_STYLE_RIDGE:
      return mOneUnitBorder && (aCorner == NS_CORNER_TOP_LEFT || aCorner == NS_CORNER_BOTTOM_RIGHT);

    case NS_STYLE_BORDER_STYLE_DOUBLE:
      return mOneUnitBorder;

    default:
      return PR_FALSE;
  }
}

BorderColorStyle
nsCSSBorderRenderer::BorderColorStyleForSolidCorner(PRUint8 aStyle, mozilla::css::Corner aCorner)
{
  // note that this function assumes that the corner is already solid,
  // as per the earlier function
  switch (aStyle) {
    case NS_STYLE_BORDER_STYLE_DOTTED:
    case NS_STYLE_BORDER_STYLE_DASHED:
    case NS_STYLE_BORDER_STYLE_SOLID:
    case NS_STYLE_BORDER_STYLE_DOUBLE:
      return BorderColorStyleSolid;

    case NS_STYLE_BORDER_STYLE_INSET:
    case NS_STYLE_BORDER_STYLE_GROOVE:
      if (aCorner == NS_CORNER_TOP_LEFT)
        return BorderColorStyleDark;
      else if (aCorner == NS_CORNER_BOTTOM_RIGHT)
        return BorderColorStyleLight;
      break;

    case NS_STYLE_BORDER_STYLE_OUTSET:
    case NS_STYLE_BORDER_STYLE_RIDGE:
      if (aCorner == NS_CORNER_TOP_LEFT)
        return BorderColorStyleLight;
      else if (aCorner == NS_CORNER_BOTTOM_RIGHT)
        return BorderColorStyleDark;
      break;
  }

  return BorderColorStyleNone;
}

void
nsCSSBorderRenderer::DoCornerSubPath(mozilla::css::Corner aCorner)
{
  gfxPoint offset(0.0, 0.0);

  if (aCorner == C_TR || aCorner == C_BR)
    offset.x = mOuterRect.size.width - mBorderCornerDimensions[aCorner].width;
  if (aCorner == C_BR || aCorner == C_BL)
    offset.y = mOuterRect.size.height - mBorderCornerDimensions[aCorner].height;

  mContext->Rectangle(gfxRect(mOuterRect.pos + offset,
                              mBorderCornerDimensions[aCorner]));
}

void
nsCSSBorderRenderer::DoSideClipWithoutCornersSubPath(mozilla::css::Side aSide)
{
  gfxPoint offset(0.0, 0.0);

  // The offset from the outside rect to the start of this side's
  // box.  For the top and bottom sides, the height of the box
  // must be the border height; the x start must take into account
  // the corner size (which may be bigger than the right or left
  // side's width).  The same applies to the right and left sides.
  if (aSide == NS_SIDE_TOP) {
    offset.x = mBorderCornerDimensions[C_TL].width;
  } else if (aSide == NS_SIDE_RIGHT) {
    offset.x = mOuterRect.size.width - mBorderWidths[NS_SIDE_RIGHT];
    offset.y = mBorderCornerDimensions[C_TR].height;
  } else if (aSide == NS_SIDE_BOTTOM) {
    offset.x = mBorderCornerDimensions[C_BL].width;
    offset.y = mOuterRect.size.height - mBorderWidths[NS_SIDE_BOTTOM];
  } else if (aSide == NS_SIDE_LEFT) {
    offset.y = mBorderCornerDimensions[C_TL].height;
  }

  // The sum of the width & height of the corners adjacent to the
  // side.  This relies on the relationship between side indexing and
  // corner indexing; that is, 0 == SIDE_TOP and 0 == CORNER_TOP_LEFT,
  // with both proceeding clockwise.
  gfxSize sideCornerSum = mBorderCornerDimensions[mozilla::css::Corner(aSide)]
                        + mBorderCornerDimensions[mozilla::css::Corner(NEXT_SIDE(aSide))];
  gfxRect rect(mOuterRect.pos + offset,
               mOuterRect.size - sideCornerSum);

  if (aSide == NS_SIDE_TOP || aSide == NS_SIDE_BOTTOM)
    rect.size.height = mBorderWidths[aSide];
  else
    rect.size.width = mBorderWidths[aSide];

  mContext->Rectangle(rect);
}

// The side border type and the adjacent border types are
// examined and one of the different types of clipping (listed
// below) is selected.

typedef enum {
  // clip to the trapezoid formed by the corners of the
  // inner and outer rectangles for the given side
  SIDE_CLIP_TRAPEZOID,

  // clip to the trapezoid formed by the outer rectangle
  // corners and the center of the region, making sure
  // that diagonal lines all go directly from the outside
  // corner to the inside corner, but that they then continue on
  // to the middle.
  //
  // This is needed for correctly clipping rounded borders,
  // which might extend past the SIDE_CLIP_TRAPEZOID trap.
  SIDE_CLIP_TRAPEZOID_FULL,

  // clip to the rectangle formed by the given side; a specific
  // overlap algorithm is used; see the function for details.
  // this is currently used for dashing.
  SIDE_CLIP_RECTANGLE
} SideClipType;

// Given three points, p0, p1, and midPoint, if p0 and p1 do not form
// a horizontal or vertical line move p1 to the point nearest the
// midpoint, while maintaing the slope of the line.
static void
MaybeMoveToMidPoint(gfxPoint& aP0, gfxPoint& aP1, const gfxPoint& aMidPoint)
{
  gfxPoint ps = aP1 - aP0;

  if (ps.x != 0.0 && ps.y != 0.0) {
    gfxFloat k = NS_MIN((aMidPoint.x - aP0.x) / ps.x,
                        (aMidPoint.y - aP1.y) / ps.y);
    aP1 = aP0 + ps * k;
  }
}

void
nsCSSBorderRenderer::DoSideClipSubPath(mozilla::css::Side aSide)
{
  // the clip proceeds clockwise from the top left corner;
  // so "start" in each case is the start of the region from that side.
  //
  // the final path will be formed like:
  // s0 ------- e0
  // |         /
  // s1 ----- e1
  //
  // that is, the second point will always be on the inside

  gfxPoint start[2];
  gfxPoint end[2];

#define IS_DASHED_OR_DOTTED(_s)  ((_s) == NS_STYLE_BORDER_STYLE_DASHED || (_s) == NS_STYLE_BORDER_STYLE_DOTTED)
  PRBool isDashed      = IS_DASHED_OR_DOTTED(mBorderStyles[aSide]);
  PRBool startIsDashed = IS_DASHED_OR_DOTTED(mBorderStyles[PREV_SIDE(aSide)]);
  PRBool endIsDashed   = IS_DASHED_OR_DOTTED(mBorderStyles[NEXT_SIDE(aSide)]);
#undef IS_DASHED_OR_DOTTED

  SideClipType startType = SIDE_CLIP_TRAPEZOID;
  SideClipType endType = SIDE_CLIP_TRAPEZOID;

  if (!IsZeroSize(mBorderRadii[mozilla::css::Corner(aSide)]))
    startType = SIDE_CLIP_TRAPEZOID_FULL;
  else if (startIsDashed && isDashed)
    startType = SIDE_CLIP_RECTANGLE;

  if (!IsZeroSize(mBorderRadii[mozilla::css::Corner(NEXT_SIDE(aSide))]))
    endType = SIDE_CLIP_TRAPEZOID_FULL;
  else if (endIsDashed && isDashed)
    endType = SIDE_CLIP_RECTANGLE;

  gfxPoint midPoint = mInnerRect.pos + mInnerRect.size / 2.0;

  start[0] = mOuterRect.CCWCorner(aSide);
  start[1] = mInnerRect.CCWCorner(aSide);

  end[0] = mOuterRect.CWCorner(aSide);
  end[1] = mInnerRect.CWCorner(aSide);

  if (startType == SIDE_CLIP_TRAPEZOID_FULL) {
    MaybeMoveToMidPoint(start[0], start[1], midPoint);
  } else if (startType == SIDE_CLIP_RECTANGLE) {
    if (aSide == NS_SIDE_TOP || aSide == NS_SIDE_BOTTOM)
      start[1] = gfxPoint(mOuterRect.CCWCorner(aSide).x, mInnerRect.CCWCorner(aSide).y);
    else
      start[1] = gfxPoint(mInnerRect.CCWCorner(aSide).x, mOuterRect.CCWCorner(aSide).y);
  }

  if (endType == SIDE_CLIP_TRAPEZOID_FULL) {
    MaybeMoveToMidPoint(end[0], end[1], midPoint);
  } else if (endType == SIDE_CLIP_RECTANGLE) {
    if (aSide == NS_SIDE_TOP || aSide == NS_SIDE_BOTTOM)
      end[0] = gfxPoint(mInnerRect.CWCorner(aSide).x, mOuterRect.CWCorner(aSide).y);
    else
      end[0] = gfxPoint(mOuterRect.CWCorner(aSide).x, mInnerRect.CWCorner(aSide).y);
  }

  mContext->MoveTo(start[0]);
  mContext->LineTo(end[0]);
  mContext->LineTo(end[1]);
  mContext->LineTo(start[1]);
  mContext->ClosePath();
}

void
nsCSSBorderRenderer::FillSolidBorder(const gfxRect& aOuterRect,
                                     const gfxRect& aInnerRect,
                                     const gfxCornerSizes& aBorderRadii,
                                     const gfxFloat *aBorderSizes,
                                     PRIntn aSides,
                                     const gfxRGBA& aColor)
{
  mContext->SetColor(aColor);
  // Note that this function is allowed to draw more than just the
  // requested sides.

  // If we have a border radius, do full rounded rectangles
  // and fill, regardless of what sides we're asked to draw.
  if (!AllCornersZeroSize(aBorderRadii)) {
    gfxCornerSizes innerRadii;
    ComputeInnerRadii(aBorderRadii, aBorderSizes, &innerRadii);

    mContext->NewPath();

    // do the outer border
    mContext->RoundedRectangle(aOuterRect, aBorderRadii, PR_TRUE);

    // then do the inner border CCW
    mContext->RoundedRectangle(aInnerRect, innerRadii, PR_FALSE);

    mContext->Fill();

    return;
  }

  // If we're asked to draw all sides of an equal-sized border,
  // stroking is fastest.  This is a fairly common path, but partial
  // sides is probably second in the list -- there are a bunch of
  // common border styles, such as inset and outset, that are
  // top-left/bottom-right split.
  if (aSides == SIDE_BITS_ALL &&
      CheckFourFloatsEqual(aBorderSizes, aBorderSizes[0]))
  {
    gfxRect r(aOuterRect);
    r.Inset(aBorderSizes[0] / 2.0);
    mContext->SetLineWidth(aBorderSizes[0]);

    mContext->NewPath();
    mContext->Rectangle(r);
    mContext->Stroke();

    return;
  }

  // Otherwise, we have unequal sized borders or we're only
  // drawing some sides; create rectangles for each side
  // and fill them.

  gfxRect r[4];

  // compute base rects for each side
  if (aSides & SIDE_BIT_TOP) {
    r[NS_SIDE_TOP].pos = aOuterRect.TopLeft();
    r[NS_SIDE_TOP].size.width = aOuterRect.size.width;
    r[NS_SIDE_TOP].size.height = aBorderSizes[NS_SIDE_TOP];
  }

  if (aSides & SIDE_BIT_BOTTOM) {
    r[NS_SIDE_BOTTOM].pos = aOuterRect.BottomLeft();
    r[NS_SIDE_BOTTOM].pos.y -= aBorderSizes[NS_SIDE_BOTTOM];
    r[NS_SIDE_BOTTOM].size.width = aOuterRect.size.width;
    r[NS_SIDE_BOTTOM].size.height = aBorderSizes[NS_SIDE_BOTTOM];
  }

  if (aSides & SIDE_BIT_LEFT) {
    r[NS_SIDE_LEFT].pos = aOuterRect.TopLeft();
    r[NS_SIDE_LEFT].size.width = aBorderSizes[NS_SIDE_LEFT];
    r[NS_SIDE_LEFT].size.height = aOuterRect.size.height;
  }

  if (aSides & SIDE_BIT_RIGHT) {
    r[NS_SIDE_RIGHT].pos = aOuterRect.TopRight();
    r[NS_SIDE_RIGHT].pos.x -= aBorderSizes[NS_SIDE_RIGHT];
    r[NS_SIDE_RIGHT].size.width = aBorderSizes[NS_SIDE_RIGHT];
    r[NS_SIDE_RIGHT].size.height = aOuterRect.size.height;
  }

  // If two sides meet at a corner that we're rendering, then
  // make sure that we adjust one of the sides to avoid overlap.
  // This is especially important in the case of colors with
  // an alpha channel.

  if ((aSides & (SIDE_BIT_TOP | SIDE_BIT_LEFT)) == (SIDE_BIT_TOP | SIDE_BIT_LEFT)) {
    // adjust the left's top down a bit
    r[NS_SIDE_LEFT].pos.y += aBorderSizes[NS_SIDE_TOP];
    r[NS_SIDE_LEFT].size.height -= aBorderSizes[NS_SIDE_TOP];
  }

  if ((aSides & (SIDE_BIT_TOP | SIDE_BIT_RIGHT)) == (SIDE_BIT_TOP | SIDE_BIT_RIGHT)) {
    // adjust the top's left a bit
    r[NS_SIDE_TOP].size.width -= aBorderSizes[NS_SIDE_RIGHT];
  }

  if ((aSides & (SIDE_BIT_BOTTOM | SIDE_BIT_RIGHT)) == (SIDE_BIT_BOTTOM | SIDE_BIT_RIGHT)) {
    // adjust the right's bottom a bit
    r[NS_SIDE_RIGHT].size.height -= aBorderSizes[NS_SIDE_BOTTOM];
  }

  if ((aSides & (SIDE_BIT_BOTTOM | SIDE_BIT_LEFT)) == (SIDE_BIT_BOTTOM | SIDE_BIT_LEFT)) {
    // adjust the bottom's left a bit
    r[NS_SIDE_BOTTOM].pos.x += aBorderSizes[NS_SIDE_LEFT];
    r[NS_SIDE_BOTTOM].size.width -= aBorderSizes[NS_SIDE_LEFT];
  }

  // Filling these one by one is faster than filling them all at once.
  for (PRUint32 i = 0; i < 4; i++) {
    if (aSides & (1 << i)) {
      mContext->NewPath();
      mContext->Rectangle(r[i]);
      mContext->Fill();
    }
  }
}

gfxRGBA
MakeBorderColor(const gfxRGBA& aColor, const gfxRGBA& aBackgroundColor, BorderColorStyle aBorderColorStyle)
{
  nscolor colors[2];
  int k = 0;

  switch (aBorderColorStyle) {
    case BorderColorStyleNone:
      return gfxRGBA(0.0, 0.0, 0.0, 0.0);

    case BorderColorStyleLight:
      k = 1;
      /* fall through */
    case BorderColorStyleDark:
      NS_GetSpecial3DColors(colors, aBackgroundColor.Packed(), aColor.Packed());
      return gfxRGBA(colors[k]);

    case BorderColorStyleSolid:
    default:
      return aColor;
  }
}

gfxRGBA
ComputeColorForLine(PRUint32 aLineIndex,
                    const BorderColorStyle* aBorderColorStyle,
                    PRUint32 aBorderColorStyleCount,
                    nscolor aBorderColor,
                    nscolor aBackgroundColor)
{
  NS_ASSERTION(aLineIndex < aBorderColorStyleCount, "Invalid lineIndex given");

  return MakeBorderColor(gfxRGBA(aBorderColor), gfxRGBA(aBackgroundColor), aBorderColorStyle[aLineIndex]);
}

gfxRGBA
ComputeCompositeColorForLine(PRUint32 aLineIndex,
                             const nsBorderColors* aBorderColors)
{
  while (aLineIndex-- && aBorderColors->mNext)
    aBorderColors = aBorderColors->mNext;

  return gfxRGBA(aBorderColors->mColor);
}

void
nsCSSBorderRenderer::DrawBorderSidesCompositeColors(PRIntn aSides, const nsBorderColors *aCompositeColors)
{
  gfxCornerSizes radii = mBorderRadii;

  // the generic composite colors path; each border is 1px in size
  gfxRect soRect = mOuterRect;
  gfxRect siRect;
  gfxFloat maxBorderWidth = 0;
  NS_FOR_CSS_SIDES (i) {
    maxBorderWidth = NS_MAX(maxBorderWidth, mBorderWidths[i]);
  }

  gfxFloat fakeBorderSizes[4];

  gfxRGBA lineColor;
  gfxPoint tl, br;

  gfxPoint itl = mInnerRect.TopLeft();
  gfxPoint ibr = mInnerRect.BottomRight();

  for (PRUint32 i = 0; i < PRUint32(maxBorderWidth); i++) {
    lineColor = ComputeCompositeColorForLine(i, aCompositeColors);

    siRect = soRect;
    siRect.Inset(1.0, 1.0, 1.0, 1.0);

    // now cap the rects to the real mInnerRect
    tl = siRect.TopLeft();
    br = siRect.BottomRight();

    tl.x = NS_MIN(tl.x, itl.x);
    tl.y = NS_MIN(tl.y, itl.y);

    br.x = NS_MAX(br.x, ibr.x);
    br.y = NS_MAX(br.y, ibr.y);

    siRect.pos = tl;
    siRect.size.width = br.x - tl.x;
    siRect.size.height = br.y - tl.y;

    fakeBorderSizes[NS_SIDE_TOP] = siRect.TopLeft().y - soRect.TopLeft().y;
    fakeBorderSizes[NS_SIDE_RIGHT] = soRect.TopRight().x - siRect.TopRight().x;
    fakeBorderSizes[NS_SIDE_BOTTOM] = soRect.BottomRight().y - siRect.BottomRight().y;
    fakeBorderSizes[NS_SIDE_LEFT] = siRect.BottomLeft().x - soRect.BottomLeft().x;

    FillSolidBorder(soRect, siRect, radii, fakeBorderSizes, aSides, lineColor);

    soRect = siRect;

    ComputeInnerRadii(radii, fakeBorderSizes, &radii);
  }
}

void
nsCSSBorderRenderer::DrawBorderSides(PRIntn aSides)
{
  if (aSides == 0 || (aSides & ~SIDE_BITS_ALL) != 0) {
    NS_WARNING("DrawBorderSides: invalid sides!");
    return;
  }

  PRUint8 borderRenderStyle;
  nscolor borderRenderColor;
  const nsBorderColors *compositeColors = nsnull;

  PRUint32 borderColorStyleCount = 0;
  BorderColorStyle borderColorStyleTopLeft[3], borderColorStyleBottomRight[3];
  BorderColorStyle *borderColorStyle = nsnull;

  NS_FOR_CSS_SIDES (i) {
    if ((aSides & (1 << i)) == 0)
      continue;
    borderRenderStyle = mBorderStyles[i];
    borderRenderColor = mBorderColors[i];
    compositeColors = mCompositeColors[i];
    break;
  }

  if (borderRenderStyle == NS_STYLE_BORDER_STYLE_NONE ||
      borderRenderStyle == NS_STYLE_BORDER_STYLE_HIDDEN)
    return;

  // -moz-border-colors is a hack; if we have it for a border, then
  // it's always drawn solid, and each color is given 1px.  The last
  // color is used for the remainder of the border's size.  Just
  // hand off to another function to do all that.
  if (compositeColors) {
    DrawBorderSidesCompositeColors(aSides, compositeColors);
    return;
  }

  // We're not doing compositeColors, so we can calculate the
  // borderColorStyle based on the specified style.  The
  // borderColorStyle array goes from the outer to the inner style.
  //
  // If the border width is 1, we need to change the borderRenderStyle
  // a bit to make sure that we get the right colors -- e.g. 'ridge'
  // with a 1px border needs to look like solid, not like 'outset'.
  if (mOneUnitBorder &&
      (borderRenderStyle == NS_STYLE_BORDER_STYLE_RIDGE ||
       borderRenderStyle == NS_STYLE_BORDER_STYLE_GROOVE ||
       borderRenderStyle == NS_STYLE_BORDER_STYLE_DOUBLE))
    borderRenderStyle = NS_STYLE_BORDER_STYLE_SOLID;

  switch (borderRenderStyle) {
    case NS_STYLE_BORDER_STYLE_SOLID:
    case NS_STYLE_BORDER_STYLE_DASHED:
    case NS_STYLE_BORDER_STYLE_DOTTED:
      borderColorStyleTopLeft[0] = BorderColorStyleSolid;

      borderColorStyleBottomRight[0] = BorderColorStyleSolid;

      borderColorStyleCount = 1;
      break;

    case NS_STYLE_BORDER_STYLE_GROOVE:
      borderColorStyleTopLeft[0] = BorderColorStyleDark;
      borderColorStyleTopLeft[1] = BorderColorStyleLight;

      borderColorStyleBottomRight[0] = BorderColorStyleLight;
      borderColorStyleBottomRight[1] = BorderColorStyleDark;

      borderColorStyleCount = 2;
      break;

    case NS_STYLE_BORDER_STYLE_RIDGE:
      borderColorStyleTopLeft[0] = BorderColorStyleLight;
      borderColorStyleTopLeft[1] = BorderColorStyleDark;

      borderColorStyleBottomRight[0] = BorderColorStyleDark;
      borderColorStyleBottomRight[1] = BorderColorStyleLight;

      borderColorStyleCount = 2;
      break;

    case NS_STYLE_BORDER_STYLE_DOUBLE:
      borderColorStyleTopLeft[0] = BorderColorStyleSolid;
      borderColorStyleTopLeft[1] = BorderColorStyleNone;
      borderColorStyleTopLeft[2] = BorderColorStyleSolid;

      borderColorStyleBottomRight[0] = BorderColorStyleSolid;
      borderColorStyleBottomRight[1] = BorderColorStyleNone;
      borderColorStyleBottomRight[2] = BorderColorStyleSolid;

      borderColorStyleCount = 3;
      break;

    case NS_STYLE_BORDER_STYLE_INSET:
      borderColorStyleTopLeft[0] = BorderColorStyleDark;
      borderColorStyleBottomRight[0] = BorderColorStyleLight;

      borderColorStyleCount = 1;
      break;

    case NS_STYLE_BORDER_STYLE_OUTSET:
      borderColorStyleTopLeft[0] = BorderColorStyleLight;
      borderColorStyleBottomRight[0] = BorderColorStyleDark;

      borderColorStyleCount = 1;
      break;

    default:
      NS_NOTREACHED("Unhandled border style!!");
      break;
  }

  // The only way to get to here is by having a
  // borderColorStyleCount < 1 or > 3; this should never happen,
  // since -moz-border-colors doesn't get handled here.
  NS_ASSERTION(borderColorStyleCount > 0 && borderColorStyleCount < 4,
               "Non-border-colors case with borderColorStyleCount < 1 or > 3; what happened?");

  // The caller should never give us anything with a mix
  // of TL/BR if the border style would require a
  // TL/BR split.
  if (aSides & (SIDE_BIT_BOTTOM | SIDE_BIT_RIGHT))
    borderColorStyle = borderColorStyleBottomRight;
  else
    borderColorStyle = borderColorStyleTopLeft;

  // Distribute the border across the available space.
  gfxFloat borderWidths[3][4];

  if (borderColorStyleCount == 1) {
    NS_FOR_CSS_SIDES (i) {
      borderWidths[0][i] = mBorderWidths[i];
    }
  } else if (borderColorStyleCount == 2) {
    // with 2 color styles, any extra pixel goes to the outside
    NS_FOR_CSS_SIDES (i) {
      borderWidths[0][i] = PRInt32(mBorderWidths[i]) / 2 + PRInt32(mBorderWidths[i]) % 2;
      borderWidths[1][i] = PRInt32(mBorderWidths[i]) / 2;
    }
  } else if (borderColorStyleCount == 3) {
    // with 3 color styles, any extra pixel (or lack of extra pixel)
    // goes to the middle
    NS_FOR_CSS_SIDES (i) {
      if (mBorderWidths[i] == 1.0) {
        borderWidths[0][i] = 1.0;
        borderWidths[1][i] = borderWidths[2][i] = 0.0;
      } else {
        PRInt32 rest = PRInt32(mBorderWidths[i]) % 3;
        borderWidths[0][i] = borderWidths[2][i] = borderWidths[1][i] = (PRInt32(mBorderWidths[i]) - rest) / 3;

        if (rest == 1) {
          borderWidths[1][i] += 1.0;
        } else if (rest == 2) {
          borderWidths[0][i] += 1.0;
          borderWidths[2][i] += 1.0;
        }
      }
    }
  }

  // make a copy that we can modify
  gfxCornerSizes radii = mBorderRadii;

  gfxRect soRect(mOuterRect);
  gfxRect siRect(mOuterRect);

  for (unsigned int i = 0; i < borderColorStyleCount; i++) {
    // walk siRect inwards at the start of the loop to get the
    // correct inner rect.
    siRect.Inset(borderWidths[i]);

    if (borderColorStyle[i] != BorderColorStyleNone) {
      gfxRGBA color = ComputeColorForLine(i,
                                          borderColorStyle, borderColorStyleCount,
                                          borderRenderColor, mBackgroundColor);

      FillSolidBorder(soRect, siRect, radii, borderWidths[i], aSides, color);
    }

    ComputeInnerRadii(radii, borderWidths[i], &radii);

    // And now soRect is the same as siRect, for the next line in.
    soRect = siRect;
  }
}

void
nsCSSBorderRenderer::DrawDashedSide(mozilla::css::Side aSide)
{
  gfxFloat dashWidth;
  gfxFloat dash[2];

  PRUint8 style = mBorderStyles[aSide];
  gfxFloat borderWidth = mBorderWidths[aSide];
  nscolor borderColor = mBorderColors[aSide];

  if (borderWidth == 0.0)
    return;

  if (style == NS_STYLE_BORDER_STYLE_NONE ||
      style == NS_STYLE_BORDER_STYLE_HIDDEN)
    return;

  if (style == NS_STYLE_BORDER_STYLE_DASHED) {
    dashWidth = gfxFloat(borderWidth * DOT_LENGTH * DASH_LENGTH);

    dash[0] = dashWidth;
    dash[1] = dashWidth;

    mContext->SetLineCap(gfxContext::LINE_CAP_BUTT);
  } else if (style == NS_STYLE_BORDER_STYLE_DOTTED) {
    dashWidth = gfxFloat(borderWidth * DOT_LENGTH);

    if (borderWidth > 2.0) {
      dash[0] = 0.0;
      dash[1] = dashWidth * 2.0;

      mContext->SetLineCap(gfxContext::LINE_CAP_ROUND);
    } else {
      dash[0] = dashWidth;
      dash[1] = dashWidth;
    }
  } else {
    SF("DrawDashedSide: style: %d!!\n", style);
    NS_ERROR("DrawDashedSide called with style other than DASHED or DOTTED; someone's not playing nice");
    return;
  }

  SF("dash: %f %f\n", dash[0], dash[1]);

  mContext->SetDash(dash, 2, 0.0);

  gfxPoint start = mOuterRect.CCWCorner(aSide);
  gfxPoint end = mOuterRect.CWCorner(aSide);

  if (aSide == NS_SIDE_TOP) {
    start.x += mBorderCornerDimensions[C_TL].width;
    end.x -= mBorderCornerDimensions[C_TR].width;

    start.y += borderWidth / 2.0;
    end.y += borderWidth / 2.0;
  } else if (aSide == NS_SIDE_RIGHT) {
    start.x -= borderWidth / 2.0;
    end.x -= borderWidth / 2.0;

    start.y += mBorderCornerDimensions[C_TR].height;
    end.y -= mBorderCornerDimensions[C_BR].height;
  } else if (aSide == NS_SIDE_BOTTOM) {
    start.x -= mBorderCornerDimensions[C_BR].width;
    end.x += mBorderCornerDimensions[C_BL].width;

    start.y -= borderWidth / 2.0;
    end.y -= borderWidth / 2.0;
  } else if (aSide == NS_SIDE_LEFT) {
    start.x += borderWidth / 2.0;
    end.x += borderWidth / 2.0;

    start.y -= mBorderCornerDimensions[C_BL].height;
    end.y += mBorderCornerDimensions[C_TL].height;
  }

  mContext->NewPath();
  mContext->MoveTo(start);
  mContext->LineTo(end);
  mContext->SetLineWidth(borderWidth);
  mContext->SetColor(gfxRGBA(borderColor));
  //mContext->SetColor(gfxRGBA(1.0, 0.0, 0.0, 1.0));
  mContext->Stroke();
}

void
nsCSSBorderRenderer::DrawBorders()
{
  PRBool forceSeparateCorners = PR_FALSE;

  // Examine the border style to figure out if we can draw it in one
  // go or not.
  PRBool tlBordersSame = AreBorderSideFinalStylesSame(SIDE_BIT_TOP | SIDE_BIT_LEFT);
  PRBool brBordersSame = AreBorderSideFinalStylesSame(SIDE_BIT_BOTTOM | SIDE_BIT_RIGHT);
  PRBool allBordersSame = AreBorderSideFinalStylesSame(SIDE_BITS_ALL);
  if (allBordersSame &&
      mCompositeColors[0] == NULL &&
      (mBorderStyles[0] == NS_STYLE_BORDER_STYLE_NONE ||
       mBorderStyles[0] == NS_STYLE_BORDER_STYLE_HIDDEN ||
       mBorderColors[0] == NS_RGBA(0,0,0,0)))
  {
    // All borders are the same style, and the style is either none or hidden, or the color
    // is transparent.
    // This doesn't check if the composite colors happen to be all transparent, but that should
    // happen very rarely in practice.
    return;
  }

  // If we have composite colors -and- border radius,
  // then use separate corners so we get OPERATOR_ADD for the corners.
  // Otherwise, we'll get artifacts as we draw stacked 1px-wide curves.
  if (allBordersSame && mCompositeColors[0] != nsnull && !mNoBorderRadius)
    forceSeparateCorners = PR_TRUE;

  // round mOuterRect and mInnerRect; they're already an integer
  // number of pixels apart and should stay that way after
  // rounding.
  mOuterRect.Round();
  mInnerRect.Round();

  S(" mOuterRect: "), S(mOuterRect), SN();
  S(" mInnerRect: "), S(mInnerRect), SN();
  SF(" mBorderColors: 0x%08x 0x%08x 0x%08x 0x%08x\n", mBorderColors[0], mBorderColors[1], mBorderColors[2], mBorderColors[3]);

  // if conditioning the outside rect failed, then bail -- the outside
  // rect is supposed to enclose the entire border
  mOuterRect.Condition();
  if (mOuterRect.IsEmpty())
    return;

  mInnerRect.Condition();

  PRIntn dashedSides = 0;
  NS_FOR_CSS_SIDES(i) {
    PRUint8 style = mBorderStyles[i];
    if (style == NS_STYLE_BORDER_STYLE_DASHED ||
        style == NS_STYLE_BORDER_STYLE_DOTTED)
    {
      // pretend that all borders aren't the same; we need to draw
      // things separately for dashed/dotting
      allBordersSame = PR_FALSE;
      dashedSides |= (1 << i);
    }
  }

  SF(" allBordersSame: %d dashedSides: 0x%02x\n", allBordersSame, dashedSides);

  // Clamp the CTM to be pixel-aligned; we do this only
  // for translation-only matrices now, but we could do it
  // if the matrix has just a scale as well.  We should not
  // do it if there's a rotation.
  gfxMatrix mat = mContext->CurrentMatrix();
  if (!mat.HasNonTranslation()) {
    mat.x0 = floor(mat.x0 + 0.5);
    mat.y0 = floor(mat.y0 + 0.5);
    mContext->SetMatrix(mat);
  }

  if (allBordersSame && !forceSeparateCorners) {
    /* Draw everything in one go */
    DrawBorderSides(SIDE_BITS_ALL);
    SN("---------------- (1)");
  } else {
    /* We have more than one pass to go.  Draw the corners separately from the sides. */

    /*
     * If we have a 1px-wide border, the corners are going to be
     * negligible, so don't bother doing anything fancy.  Just extend
     * the top and bottom borders to the right 1px and the left border
     * to the bottom 1px.  We do this by twiddling the corner dimensions,
     * which causes the right to happen later on.  Only do this if we have
     * a 1.0 unit border all around and no border radius.
     */

    NS_FOR_CSS_CORNERS(corner) {
      const mozilla::css::Side sides[2] = { mozilla::css::Side(corner), PREV_SIDE(corner) };

      if (!IsZeroSize(mBorderRadii[corner]))
        continue;

      if (mBorderWidths[sides[0]] == 1.0 && mBorderWidths[sides[1]] == 1.0) {
        if (corner == NS_CORNER_TOP_LEFT || corner == NS_CORNER_TOP_RIGHT)
          mBorderCornerDimensions[corner].width = 0.0;
        else
          mBorderCornerDimensions[corner].height = 0.0;
      }
    }

    // First, the corners
    NS_FOR_CSS_CORNERS(corner) {
      // if there's no corner, don't do all this work for it
      if (IsZeroSize(mBorderCornerDimensions[corner]))
        continue;

      const PRIntn sides[2] = { corner, PREV_SIDE(corner) };
      PRIntn sideBits = (1 << sides[0]) | (1 << sides[1]);

      PRBool simpleCornerStyle = mCompositeColors[sides[0]] == NULL &&
                                 mCompositeColors[sides[1]] == NULL &&
                                 AreBorderSideFinalStylesSame(sideBits);

      // If we don't have anything complex going on in this corner,
      // then we can just fill the corner with a solid color, and avoid
      // the potentially expensive clip.
      if (simpleCornerStyle &&
          IsZeroSize(mBorderRadii[corner]) &&
          IsSolidCornerStyle(mBorderStyles[sides[0]], corner))
      {
        mContext->NewPath();
        DoCornerSubPath(corner);
        mContext->SetColor(MakeBorderColor(mBorderColors[sides[0]],
                                           mBackgroundColor,
                                           BorderColorStyleForSolidCorner(mBorderStyles[sides[0]], corner)));
        mContext->Fill();
        continue;
      }

      mContext->Save();

      // clip to the corner
      mContext->NewPath();
      DoCornerSubPath(corner);
      mContext->Clip();

      if (simpleCornerStyle) {
        // we don't need a group for this corner, the sides are the same,
        // but we weren't able to render just a solid block for the corner.
        DrawBorderSides(sideBits);
      } else {
        // Sides are different.  We need to draw using OPERATOR_ADD to
        // get correct color blending behaviour at the seam.  We need
        // to do it in an offscreen surface to ensure that we're
        // always compositing on transparent black.  If the colors
        // don't have transparency and the current destination surface
        // has an alpha channel, we could just clear the region and
        // avoid the temporary, but that situation doesn't happen all
        // that often in practice (we double buffer to no-alpha
        // surfaces).

        mContext->PushGroup(gfxASurface::CONTENT_COLOR_ALPHA);
        mContext->SetOperator(gfxContext::OPERATOR_ADD);

        for (int cornerSide = 0; cornerSide < 2; cornerSide++) {
          mozilla::css::Side side = mozilla::css::Side(sides[cornerSide]);
          PRUint8 style = mBorderStyles[side];

          SF("corner: %d cornerSide: %d side: %d style: %d\n", corner, cornerSide, side, style);

          mContext->Save();

          mContext->NewPath();
          DoSideClipSubPath(side);
          mContext->Clip();

          DrawBorderSides(1 << side);

          mContext->Restore();
        }

        mContext->PopGroupToSource();
        mContext->SetOperator(gfxContext::OPERATOR_OVER);
        mContext->Paint();
      }

      mContext->Restore();

      SN();
    }

    // in the case of a single-unit border, we already munged the
    // corners up above; so we can just draw the top left and bottom
    // right sides separately, if they're the same.
    //
    // We need to check for mNoBorderRadius, because when there is
    // one, FillSolidBorder always draws the full rounded rectangle
    // and expects there to be a clip in place.
    PRIntn alreadyDrawnSides = 0;
    if (mOneUnitBorder &&
        mNoBorderRadius &&
        (dashedSides & (SIDE_BIT_TOP | SIDE_BIT_LEFT)) == 0)
    {
      if (tlBordersSame) {
        DrawBorderSides(SIDE_BIT_TOP | SIDE_BIT_LEFT);
        alreadyDrawnSides |= (SIDE_BIT_TOP | SIDE_BIT_LEFT);
      }

      if (brBordersSame && (dashedSides & (SIDE_BIT_BOTTOM | SIDE_BIT_RIGHT)) == 0) {
        DrawBorderSides(SIDE_BIT_BOTTOM | SIDE_BIT_RIGHT);
        alreadyDrawnSides |= (SIDE_BIT_BOTTOM | SIDE_BIT_RIGHT);
      }
    }

    // We're done with the corners, now draw the sides.
    NS_FOR_CSS_SIDES (side) {
      // if we drew it above, skip it
      if (alreadyDrawnSides & (1 << side))
        continue;

      // If there's no border on this side, skip it
      if (mBorderWidths[side] == 0.0 ||
          mBorderStyles[side] == NS_STYLE_BORDER_STYLE_HIDDEN ||
          mBorderStyles[side] == NS_STYLE_BORDER_STYLE_NONE)
        continue;


      if (dashedSides & (1 << side)) {
        // Dashed sides will always draw just the part ignoring the
        // corners for the side, so no need to clip.
        DrawDashedSide (side);

        SN("---------------- (d)");
        continue;
      }

      // Undashed sides will currently draw the entire side,
      // including parts that would normally be covered by a corner,
      // so we need to clip.
      //
      // XXX Optimization -- it would be good to make this work like
      // DrawDashedSide, and have a DrawOneSide function that just
      // draws one side and not the corners, because then we can
      // avoid the potentially expensive clip.
      mContext->Save();
      mContext->NewPath();
      DoSideClipWithoutCornersSubPath(side);
      mContext->Clip();

      DrawBorderSides(1 << side);

      mContext->Restore();

      SN("---------------- (*)");
    }
  }
}
