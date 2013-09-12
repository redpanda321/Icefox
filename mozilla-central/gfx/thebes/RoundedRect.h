/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxRect.h"

namespace mozilla {
/* A rounded rectangle abstraction.
 *
 * This can represent a rectangle with a different pair of radii on each corner.
 *
 * Note: CoreGraphics and Direct2D only support rounded rectangle with the same
 * radii on all corners. However, supporting CSS's border-radius requires the extra flexibility. */
struct RoundedRect {
    RoundedRect(gfxRect &aRect, gfxCornerSizes &aCorners) : rect(aRect), corners(aCorners) { }
    void Deflate(gfxFloat aTopWidth, gfxFloat aBottomWidth, gfxFloat aLeftWidth, gfxFloat aRightWidth) {
        // deflate the internal rect
        rect.x += aLeftWidth;
        rect.y += aTopWidth;
        rect.width = gfx::gfx_max(0., rect.width - aLeftWidth - aRightWidth);
        rect.height = gfx::gfx_max(0., rect.height - aTopWidth - aBottomWidth);

        corners.sizes[NS_CORNER_TOP_LEFT].width  = gfx::gfx_max(0., corners.sizes[NS_CORNER_TOP_LEFT].width - aLeftWidth);
        corners.sizes[NS_CORNER_TOP_LEFT].height = gfx::gfx_max(0., corners.sizes[NS_CORNER_TOP_LEFT].height - aTopWidth);

        corners.sizes[NS_CORNER_TOP_RIGHT].width  = gfx::gfx_max(0., corners.sizes[NS_CORNER_TOP_RIGHT].width - aRightWidth);
        corners.sizes[NS_CORNER_TOP_RIGHT].height = gfx::gfx_max(0., corners.sizes[NS_CORNER_TOP_RIGHT].height - aTopWidth);

        corners.sizes[NS_CORNER_BOTTOM_LEFT].width  = gfx::gfx_max(0., corners.sizes[NS_CORNER_BOTTOM_LEFT].width - aLeftWidth);
        corners.sizes[NS_CORNER_BOTTOM_LEFT].height = gfx::gfx_max(0., corners.sizes[NS_CORNER_BOTTOM_LEFT].height - aBottomWidth);

        corners.sizes[NS_CORNER_BOTTOM_RIGHT].width  = gfx::gfx_max(0., corners.sizes[NS_CORNER_BOTTOM_RIGHT].width - aRightWidth);
        corners.sizes[NS_CORNER_BOTTOM_RIGHT].height = gfx::gfx_max(0., corners.sizes[NS_CORNER_BOTTOM_RIGHT].height - aBottomWidth);
    }
    gfxRect rect;
    gfxCornerSizes corners;
};

} // namespace mozilla
