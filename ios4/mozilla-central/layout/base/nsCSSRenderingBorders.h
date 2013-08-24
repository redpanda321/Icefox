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

#ifndef NS_CSS_RENDERING_BORDERS_H
#define NS_CSS_RENDERING_BORDERS_H

#include "nsColor.h"
#include "nsStyleStruct.h"

#include "gfxContext.h"

// define this to enable a bunch of debug dump info
#undef DEBUG_NEW_BORDERS

//thickness of dashed line relative to dotted line
#define DOT_LENGTH  1           //square
#define DASH_LENGTH 3           //3 times longer than dot

//some shorthand for side bits
#define SIDE_BIT_TOP (1 << NS_SIDE_TOP)
#define SIDE_BIT_RIGHT (1 << NS_SIDE_RIGHT)
#define SIDE_BIT_BOTTOM (1 << NS_SIDE_BOTTOM)
#define SIDE_BIT_LEFT (1 << NS_SIDE_LEFT)
#define SIDE_BITS_ALL (SIDE_BIT_TOP|SIDE_BIT_RIGHT|SIDE_BIT_BOTTOM|SIDE_BIT_LEFT)

#define C_TL NS_CORNER_TOP_LEFT
#define C_TR NS_CORNER_TOP_RIGHT
#define C_BR NS_CORNER_BOTTOM_RIGHT
#define C_BL NS_CORNER_BOTTOM_LEFT

/*
 * Helper class that handles border rendering.
 *
 * appUnitsPerPixel -- current value of AUPP
 * destContext -- the gfxContext to which the border should be rendered
 * outsideRect -- the rectangle on the outer edge of the border
 *
 * For any parameter where an array of side values is passed in,
 * they are in top, right, bottom, left order.
 *
 * borderStyles -- one border style enum per side
 * borderWidths -- one border width per side
 * borderRadii -- a gfxCornerSizes struct describing the w/h for each rounded corner.
 *    If the corner doesn't have a border radius, 0,0 should be given for it.
 * borderColors -- one nscolor per side
 * compositeColors -- a pointer to an array of composite color structs, or NULL if none
 *
 * skipSides -- a bit mask specifying which sides, if any, to skip
 * backgroundColor -- the background color of the element.
 *    Used in calculating colors for 2-tone borders, such as inset and outset
 * gapRect - a rectangle that should be clipped out to leave a gap in a border,
 *    or nsnull if none.
 */

typedef enum {
  BorderColorStyleNone,
  BorderColorStyleSolid,
  BorderColorStyleLight,
  BorderColorStyleDark
} BorderColorStyle;

struct nsCSSBorderRenderer {
  nsCSSBorderRenderer(PRInt32 aAppUnitsPerPixel,
                      gfxContext* aDestContext,
                      gfxRect& aOuterRect,
                      const PRUint8* aBorderStyles,
                      const gfxFloat* aBorderWidths,
                      gfxCornerSizes& aBorderRadii,
                      const nscolor* aBorderColors,
                      nsBorderColors* const* aCompositeColors,
                      PRIntn aSkipSides,
                      nscolor aBackgroundColor);

  gfxCornerSizes mBorderCornerDimensions;

  // destination context
  gfxContext* mContext;

  // the rectangle of the outside and the inside of the border
  gfxRect mOuterRect;
  gfxRect mInnerRect;

  // the style and size of the border
  const PRUint8* mBorderStyles;
  const gfxFloat* mBorderWidths;
  gfxCornerSizes mBorderRadii;

  // colors
  const nscolor* mBorderColors;
  nsBorderColors* const* mCompositeColors;

  // core app units per pixel
  PRInt32 mAUPP;

  // misc -- which sides to skip, the background color
  PRIntn mSkipSides;
  nscolor mBackgroundColor;

  // calculated values
  PRPackedBool mOneUnitBorder;
  PRPackedBool mNoBorderRadius;

  // For all the sides in the bitmask, would they be rendered
  // in an identical color and style?
  PRBool AreBorderSideFinalStylesSame(PRUint8 aSides);

  // For the given style, is the given corner a solid color?
  PRBool IsSolidCornerStyle(PRUint8 aStyle, mozilla::css::Corner aCorner);

  // For the given solid corner, what color style should be used?
  BorderColorStyle BorderColorStyleForSolidCorner(PRUint8 aStyle, mozilla::css::Corner aCorner);

  //
  // Path generation functions
  //

  // add the path for drawing the given corner to the context
  void DoCornerSubPath(mozilla::css::Corner aCorner);
  // add the path for drawing the given side without any adjacent corners to the context
  void DoSideClipWithoutCornersSubPath(mozilla::css::Side aSide);

  // Create a clip path for the wedge that this side of
  // the border should take up.  This is only called
  // when we're drawing separate border sides, so we know
  // that ADD compositing is taking place.
  //
  // This code needs to make sure that the individual pieces
  // don't ever (mathematically) overlap; the pixel overlap
  // is taken care of by the ADD compositing.
  void DoSideClipSubPath(mozilla::css::Side aSide);

  // Given a set of sides to fill and a color, do so in the fastest way.
  //
  // Stroke tends to be faster for smaller borders because it doesn't go
  // through the tessellator, which has initialization overhead.  If
  // we're rendering all sides, we can use stroke at any thickness; we
  // also do TL/BR pairs at 1px thickness using stroke.
  //
  // If we can't stroke, then if it's a TL/BR pair, we use the specific
  // TL/BR paths.  Otherwise, we do the full path and fill.
  //
  // Calling code is expected to only set up a clip as necessary; no
  // clip is needed if we can render the entire border in 1 or 2 passes.
  void FillSolidBorder(const gfxRect& aOuterRect,
                       const gfxRect& aInnerRect,
                       const gfxCornerSizes& aBorderRadii,
                       const gfxFloat *aBorderSizes,
                       PRIntn aSides,
                       const gfxRGBA& aColor);

  //
  // core rendering
  //

  // draw the border for the given sides, using the style of the first side
  // present in the bitmask
  void DrawBorderSides (PRIntn aSides);

  // function used by the above to handle -moz-border-colors
  void DrawBorderSidesCompositeColors(PRIntn aSides, const nsBorderColors *compositeColors);

  // draw the given dashed side
  void DrawDashedSide (mozilla::css::Side aSide);

  // draw the entire border
  void DrawBorders ();

  // utility function used for background painting as well as borders
  static void ComputeInnerRadii(const gfxCornerSizes& aRadii,
                                const gfxFloat *aBorderSizes,
                                gfxCornerSizes *aInnerRadiiRet);
};

#ifdef DEBUG_NEW_BORDERS
#include <stdarg.h>

static inline void S(const gfxPoint& p) {
  fprintf (stderr, "[%f,%f]", p.x, p.y);
}

static inline void S(const gfxSize& s) {
  fprintf (stderr, "[%f %f]", s.width, s.height);
}

static inline void S(const gfxRect& r) {
  fprintf (stderr, "[%f %f %f %f]", r.pos.x, r.pos.y, r.size.width, r.size.height);
}

static inline void S(const gfxFloat f) {
  fprintf (stderr, "%f", f);
}

static inline void S(const char *s) {
  fprintf (stderr, "%s", s);
}

static inline void SN(const char *s = nsnull) {
  if (s)
    fprintf (stderr, "%s", s);
  fprintf (stderr, "\n");
  fflush (stderr);
}

static inline void SF(const char *fmt, ...) {
  va_list vl;
  va_start(vl, fmt);
  vfprintf (stderr, fmt, vl);
  va_end(vl);
}

static inline void SX(gfxContext *ctx) {
  gfxPoint p = ctx->CurrentPoint();
  fprintf (stderr, "p: %f %f\n", p.x, p.y);
  return;
  ctx->MoveTo(p + gfxPoint(-2, -2)); ctx->LineTo(p + gfxPoint(2, 2));
  ctx->MoveTo(p + gfxPoint(-2, 2)); ctx->LineTo(p + gfxPoint(2, -2));
  ctx->MoveTo(p);
}


#else
static inline void S(const gfxPoint& p) {}
static inline void S(const gfxSize& s) {}
static inline void S(const gfxRect& r) {}
static inline void S(const gfxFloat f) {}
static inline void S(const char *s) {}
static inline void SN(const char *s = nsnull) {}
static inline void SF(const char *fmt, ...) {}
static inline void SX(gfxContext *ctx) {}
#endif

#endif /* NS_CSS_RENDERING_BORDERS_H */
