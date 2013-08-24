/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Mozilla Foundation code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@mozilla.com>
 *   Masayuki Nakano <masayuki@d-toybox.com>
 *   Behdad Esfahbod <behdad@gnome.org>
 *   Mats Palmgren <mats.palmgren@bredband.net>
 *   Karl Tomlinson <karlt+@karlt.net>, Mozilla Corporation
 *   Takuro Ashie <ashie@clear-code.com>
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

#include "gfxFT2FontBase.h"
#include "gfxFT2Utils.h"
#include FT_TRUETYPE_TAGS_H
#include FT_TRUETYPE_TABLES_H

#ifdef HAVE_FONTCONFIG_FCFREETYPE_H
#include <fontconfig/fcfreetype.h>
#endif

// aScale is intended for a 16.16 x/y_scale of an FT_Size_Metrics
static inline FT_Long
ScaleRoundDesignUnits(FT_Short aDesignMetric, FT_Fixed aScale)
{
    FT_Long fixed26dot6 = FT_MulFix(aDesignMetric, aScale);
    return ROUND_26_6_TO_INT(fixed26dot6);
}

// Snap a line to pixels while keeping the center and size of the line as
// close to the original position as possible.
//
// Pango does similar snapping for underline and strikethrough when fonts are
// hinted, but nsCSSRendering::GetTextDecorationRectInternal always snaps the
// top and size of lines.  Optimizing the distance between the line and
// baseline is probably good for the gap between text and underline, but
// optimizing the center of the line is better for positioning strikethough.
static void
SnapLineToPixels(gfxFloat& aOffset, gfxFloat& aSize)
{
    gfxFloat snappedSize = PR_MAX(NS_floor(aSize + 0.5), 1.0);
    // Correct offset for change in size
    gfxFloat offset = aOffset - 0.5 * (aSize - snappedSize);
    // Snap offset
    aOffset = NS_floor(offset + 0.5);
    aSize = snappedSize;
}

void
gfxFT2LockedFace::GetMetrics(gfxFont::Metrics* aMetrics,
                             PRUint32* aSpaceGlyph)
{
    NS_PRECONDITION(aMetrics != NULL, "aMetrics must not be NULL");
    NS_PRECONDITION(aSpaceGlyph != NULL, "aSpaceGlyph must not be NULL");

    if (NS_UNLIKELY(!mFace)) {
        // No face.  This unfortunate situation might happen if the font
        // file is (re)moved at the wrong time.
        aMetrics->emHeight = mGfxFont->GetStyle()->size;
        aMetrics->emAscent = 0.8 * aMetrics->emHeight;
        aMetrics->emDescent = 0.2 * aMetrics->emHeight;
        aMetrics->maxAscent = aMetrics->emAscent;
        aMetrics->maxDescent = aMetrics->maxDescent;
        aMetrics->maxHeight = aMetrics->emHeight;
        aMetrics->internalLeading = 0.0;
        aMetrics->externalLeading = 0.2 * aMetrics->emHeight;
        aSpaceGlyph = 0;
        aMetrics->spaceWidth = 0.5 * aMetrics->emHeight;
        aMetrics->maxAdvance = aMetrics->spaceWidth;
        aMetrics->aveCharWidth = aMetrics->spaceWidth;
        aMetrics->zeroOrAveCharWidth = aMetrics->spaceWidth;
        aMetrics->xHeight = 0.5 * aMetrics->emHeight;
        aMetrics->underlineSize = aMetrics->emHeight / 14.0;
        aMetrics->underlineOffset = -aMetrics->underlineSize;
        aMetrics->strikeoutOffset = 0.25 * aMetrics->emHeight;
        aMetrics->strikeoutSize = aMetrics->underlineSize;
        aMetrics->superscriptOffset = aMetrics->xHeight;
        aMetrics->subscriptOffset = aMetrics->xHeight;

        return;
    }

    const FT_Size_Metrics& ftMetrics = mFace->size->metrics;

    gfxFloat emHeight;
    // Scale for vertical design metric conversion: pixels per design unit.
    gfxFloat yScale;
    if (FT_IS_SCALABLE(mFace)) {
        // Prefer FT_Size_Metrics::x_scale to x_ppem as x_ppem does not
        // have subpixel accuracy.
        //
        // FT_Size_Metrics::y_scale is in 16.16 fixed point format.  Its
        // (fractional) value is a factor that converts vertical metrics from
        // design units to units of 1/64 pixels, so that the result may be
        // interpreted as pixels in 26.6 fixed point format.
        yScale = FLOAT_FROM_26_6(FLOAT_FROM_16_16(ftMetrics.y_scale));
        emHeight = mFace->units_per_EM * yScale;
    } else { // Not scalable.
        // FT_Size_Metrics doc says x_scale is "only relevant for scalable
        // font formats".
        gfxFloat emUnit = mFace->units_per_EM;
        emHeight = ftMetrics.y_ppem;
        yScale = emHeight / emUnit;
    }

    TT_OS2 *os2 =
        static_cast<TT_OS2*>(FT_Get_Sfnt_Table(mFace, ft_sfnt_os2));

    aMetrics->maxAscent = FLOAT_FROM_26_6(ftMetrics.ascender);
    aMetrics->maxDescent = -FLOAT_FROM_26_6(ftMetrics.descender);
    aMetrics->maxAdvance = FLOAT_FROM_26_6(ftMetrics.max_advance);

    gfxFloat lineHeight;
    if (os2 && os2->sTypoAscender) {
        aMetrics->emAscent = os2->sTypoAscender * yScale;
        aMetrics->emDescent = -os2->sTypoDescender * yScale;
        FT_Short typoHeight =
            os2->sTypoAscender - os2->sTypoDescender + os2->sTypoLineGap;
        lineHeight = typoHeight * yScale;

        // maxAscent/maxDescent get used for frame heights, and some fonts
        // don't have the HHEA table ascent/descent set (bug 279032).
        if (aMetrics->emAscent > aMetrics->maxAscent)
            aMetrics->maxAscent = aMetrics->emAscent;
        if (aMetrics->emDescent > aMetrics->maxDescent)
            aMetrics->maxDescent = aMetrics->emDescent;
    } else {
        aMetrics->emAscent = aMetrics->maxAscent;
        aMetrics->emDescent = aMetrics->maxDescent;
        lineHeight = FLOAT_FROM_26_6(ftMetrics.height);
    }

    cairo_text_extents_t extents;
    *aSpaceGlyph = GetCharExtents(' ', &extents);
    if (*aSpaceGlyph) {
        aMetrics->spaceWidth = extents.x_advance;
    } else {
        aMetrics->spaceWidth = aMetrics->maxAdvance; // guess
    }

    aMetrics->zeroOrAveCharWidth = 0.0;
    if (GetCharExtents('0', &extents)) {
        aMetrics->zeroOrAveCharWidth = extents.x_advance;
    }

    // Prefering a measured x over sxHeight because sxHeight doesn't consider
    // hinting, but maybe the x extents are not quite right in some fancy
    // script fonts.  CSS 2.1 suggests possibly using the height of an "o",
    // which would have a more consistent glyph across fonts.
    if (GetCharExtents('x', &extents) && extents.y_bearing < 0.0) {
        aMetrics->xHeight = -extents.y_bearing;
        aMetrics->aveCharWidth = extents.x_advance;
    } else {
        if (os2 && os2->sxHeight) {
            aMetrics->xHeight = os2->sxHeight * yScale;
        } else {
            // CSS 2.1, section 4.3.2 Lengths: "In the cases where it is
            // impossible or impractical to determine the x-height, a value of
            // 0.5em should be used."
            aMetrics->xHeight = 0.5 * emHeight;
        }
        aMetrics->aveCharWidth = 0.0; // updated below
    }
    // aveCharWidth is used for the width of text input elements so be
    // liberal rather than conservative in the estimate.
    if (os2 && os2->xAvgCharWidth) {
        // Round to pixels as this is compared with maxAdvance to guess
        // whether this is a fixed width font.
        gfxFloat avgCharWidth =
            ScaleRoundDesignUnits(os2->xAvgCharWidth, ftMetrics.x_scale);
        aMetrics->aveCharWidth =
            PR_MAX(aMetrics->aveCharWidth, avgCharWidth);
    }
    aMetrics->aveCharWidth =
        PR_MAX(aMetrics->aveCharWidth, aMetrics->zeroOrAveCharWidth);
    if (aMetrics->aveCharWidth == 0.0) {
        aMetrics->aveCharWidth = aMetrics->spaceWidth;
    }
    if (aMetrics->zeroOrAveCharWidth == 0.0) {
        aMetrics->zeroOrAveCharWidth = aMetrics->aveCharWidth;
    }
    // Apparently hinting can mean that max_advance is not always accurate.
    aMetrics->maxAdvance =
        PR_MAX(aMetrics->maxAdvance, aMetrics->aveCharWidth);

    // gfxFont::Metrics::underlineOffset is the position of the top of the
    // underline.
    //
    // FT_FaceRec documentation describes underline_position as "the
    // center of the underlining stem".  This was the original definition
    // of the PostScript metric, but in the PostScript table of OpenType
    // fonts the metric is "the top of the underline"
    // (http://www.microsoft.com/typography/otspec/post.htm), and FreeType
    // (up to version 2.3.7) doesn't make any adjustment.
    //
    // Therefore get the underline position directly from the table
    // ourselves when this table exists.  Use FreeType's metrics for
    // other (including older PostScript) fonts.
    if (mFace->underline_position && mFace->underline_thickness) {
        aMetrics->underlineSize = mFace->underline_thickness * yScale;
        TT_Postscript *post = static_cast<TT_Postscript*>
            (FT_Get_Sfnt_Table(mFace, ft_sfnt_post));
        if (post && post->underlinePosition) {
            aMetrics->underlineOffset = post->underlinePosition * yScale;
        } else {
            aMetrics->underlineOffset = mFace->underline_position * yScale
                + 0.5 * aMetrics->underlineSize;
        }
    } else { // No underline info.
        // Imitate Pango.
        aMetrics->underlineSize = emHeight / 14.0;
        aMetrics->underlineOffset = -aMetrics->underlineSize;
    }

    if (os2 && os2->yStrikeoutSize && os2->yStrikeoutPosition) {
        aMetrics->strikeoutSize = os2->yStrikeoutSize * yScale;
        aMetrics->strikeoutOffset = os2->yStrikeoutPosition * yScale;
    } else { // No strikeout info.
        aMetrics->strikeoutSize = aMetrics->underlineSize;
        // Use OpenType spec's suggested position for Roman font.
        aMetrics->strikeoutOffset = emHeight * 409.0 / 2048.0
            + 0.5 * aMetrics->strikeoutSize;
    }
    SnapLineToPixels(aMetrics->strikeoutOffset, aMetrics->strikeoutSize);

    if (os2 && os2->ySuperscriptYOffset) {
        gfxFloat val = ScaleRoundDesignUnits(os2->ySuperscriptYOffset,
                                             ftMetrics.y_scale);
        aMetrics->superscriptOffset = PR_MAX(1.0, val);
    } else {
        aMetrics->superscriptOffset = aMetrics->xHeight;
    }
    
    if (os2 && os2->ySubscriptYOffset) {
        gfxFloat val = ScaleRoundDesignUnits(os2->ySubscriptYOffset,
                                             ftMetrics.y_scale);
        // some fonts have the incorrect sign. 
        val = fabs(val);
        aMetrics->subscriptOffset = PR_MAX(1.0, val);
    } else {
        aMetrics->subscriptOffset = aMetrics->xHeight;
    }

    aMetrics->maxHeight = aMetrics->maxAscent + aMetrics->maxDescent;

    // Make the line height an integer number of pixels so that lines will be
    // equally spaced (rather than just being snapped to pixels, some up and
    // some down).  Layout calculates line height from the emHeight +
    // internalLeading + externalLeading, but first each of these is rounded
    // to layout units.  To ensure that the result is an integer number of
    // pixels, round each of the components to pixels.
    aMetrics->emHeight = NS_floor(emHeight + 0.5);

    // maxHeight will normally be an integer, but round anyway in case
    // FreeType is configured differently.
    aMetrics->internalLeading =
        NS_floor(aMetrics->maxHeight - aMetrics->emHeight + 0.5);

    // Text input boxes currently don't work well with lineHeight
    // significantly less than maxHeight (with Verdana, for example).
    lineHeight = NS_floor(PR_MAX(lineHeight, aMetrics->maxHeight) + 0.5);
    aMetrics->externalLeading =
        lineHeight - aMetrics->internalLeading - aMetrics->emHeight;

    // Ensure emAscent + emDescent == emHeight
    gfxFloat sum = aMetrics->emAscent + aMetrics->emDescent;
    aMetrics->emAscent = sum > 0.0 ?
        aMetrics->emAscent * aMetrics->emHeight / sum : 0.0;
    aMetrics->emDescent = aMetrics->emHeight - aMetrics->emAscent;
}

PRUint32
gfxFT2LockedFace::GetGlyph(PRUint32 aCharCode)
{
    if (NS_UNLIKELY(!mFace))
        return 0;

#ifdef HAVE_FONTCONFIG_FCFREETYPE_H
    // FcFreeTypeCharIndex will search starting from the most recently
    // selected charmap.  This can cause non-determistic behavior when more
    // than one charmap supports a character but with different glyphs, as
    // with older versions of MS Gothic, for example.  Always prefer a Unicode
    // charmap, if there is one.  (FcFreeTypeCharIndex usually does the
    // appropriate Unicode conversion, but some fonts have non-Roman glyphs
    // for FT_ENCODING_APPLE_ROMAN characters.)
    if (!mFace->charmap || mFace->charmap->encoding != FT_ENCODING_UNICODE) {
        FT_Select_Charmap(mFace, FT_ENCODING_UNICODE);
    }

    return FcFreeTypeCharIndex(mFace, aCharCode);
#else
    return FT_Get_Char_Index(mFace, aCharCode);
#endif
}

PRUint32
gfxFT2LockedFace::GetCharExtents(char aChar, cairo_text_extents_t* aExtents)
{
    NS_PRECONDITION(aExtents != NULL, "aExtents must not be NULL");

    if (!mFace)
        return 0;

    FT_UInt gid = mGfxFont->GetGlyph(aChar);
    if (gid) {
        mGfxFont->GetGlyphExtents(gid, aExtents);
    }

    return gid;
}
