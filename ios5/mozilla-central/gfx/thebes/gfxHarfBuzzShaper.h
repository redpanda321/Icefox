/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_HARFBUZZSHAPER_H
#define GFX_HARFBUZZSHAPER_H

#include "gfxTypes.h"
#include "gfxFont.h"
#include "nsDataHashtable.h"
#include "nsPoint.h"

#include "harfbuzz/hb.h"

class gfxHarfBuzzShaper : public gfxFontShaper {
public:
    gfxHarfBuzzShaper(gfxFont *aFont);
    virtual ~gfxHarfBuzzShaper();

    virtual bool ShapeWord(gfxContext *aContext,
                           gfxShapedWord *aShapedWord,
                           const PRUnichar *aText);

    // get a given font table in harfbuzz blob form
    hb_blob_t * GetFontTable(hb_tag_t aTag) const;

    // map unicode character to glyph ID
    hb_codepoint_t GetGlyph(hb_codepoint_t unicode,
                            hb_codepoint_t variation_selector) const;

    // get harfbuzz glyph advance, in font design units
    hb_position_t GetGlyphHAdvance(gfxContext *aContext,
                                   hb_codepoint_t glyph) const;

    hb_position_t GetHKerning(PRUint16 aFirstGlyph,
                              PRUint16 aSecondGlyph) const;

protected:
    nsresult SetGlyphsFromRun(gfxContext *aContext,
                              gfxShapedWord *aShapedWord,
                              hb_buffer_t *aBuffer);

    // retrieve glyph positions, applying advance adjustments and attachments
    // returns results in appUnits
    nscoord GetGlyphPositions(gfxContext *aContext,
                              hb_buffer_t *aBuffer,
                              nsTArray<nsPoint>& aPositions,
                              PRUint32 aAppUnitsPerDevUnit);

    // harfbuzz face object, created on first use (caches font tables)
    hb_face_t         *mHBFace;

    // Following table references etc are declared "mutable" because the
    // harfbuzz callback functions take a const ptr to the shaper, but
    // wish to cache tables here to avoid repeatedly looking them up
    // in the font.

    // Old-style TrueType kern table, if we're not doing GPOS kerning
    mutable hb_blob_t *mKernTable;

    // Cached copy of the hmtx table and numLongMetrics field from hhea,
    // for use when looking up glyph metrics; initialized to 0 by the
    // constructor so we can tell it hasn't been set yet.
    // This is a signed value so that we can use -1 to indicate
    // an error (if the hhea table was not available).
    mutable hb_blob_t *mHmtxTable;
    mutable PRInt32    mNumLongMetrics;

    // Cached pointer to cmap subtable to be used for char-to-glyph mapping.
    // This comes from GetFontTablePtr; if it is non-null, our destructor
    // must call ReleaseFontTablePtr to avoid permanently caching the table.
    mutable hb_blob_t *mCmapTable;
    mutable PRInt32    mCmapFormat;
    mutable PRUint32   mSubtableOffset;
    mutable PRUint32   mUVSTableOffset;

    // Whether the font implements GetGlyph, or we should read tables
    // directly
    bool mUseFontGetGlyph;
    // Whether the font implements GetGlyphWidth, or we should read tables
    // directly to get ideal widths
    bool mUseFontGlyphWidths;
};

#endif /* GFX_HARFBUZZSHAPER_H */
