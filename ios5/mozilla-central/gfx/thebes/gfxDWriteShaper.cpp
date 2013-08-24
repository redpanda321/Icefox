/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxDWriteShaper.h"
#include "gfxWindowsPlatform.h"

#include <dwrite.h>

#include "gfxDWriteTextAnalysis.h"

#include "nsCRT.h"

bool
gfxDWriteShaper::ShapeWord(gfxContext *aContext,
                           gfxShapedWord *aShapedWord,
                           const PRUnichar *aString)
{
    HRESULT hr;
    // TODO: Handle TEST_DISABLE_OPTIONAL_LIGATURES

    DWRITE_READING_DIRECTION readingDirection = 
        aShapedWord->IsRightToLeft()
            ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT
            : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;

    gfxDWriteFont *font = static_cast<gfxDWriteFont*>(mFont);

    gfxShapedWord::CompressedGlyph g;

    IDWriteTextAnalyzer *analyzer =
        gfxWindowsPlatform::GetPlatform()->GetDWriteAnalyzer();
    if (!analyzer) {
        return false;
    }

    /**
     * There's an internal 16-bit limit on some things inside the analyzer,
     * but we never attempt to shape a word longer than 64K characters
     * in a single gfxShapedWord, so we cannot exceed that limit.
     */
    UINT32 length = aShapedWord->Length();

    TextAnalysis analysis(aString, length, NULL, readingDirection);
    TextAnalysis::Run *runHead;
    hr = analysis.GenerateResults(analyzer, &runHead);

    if (FAILED(hr)) {
        NS_WARNING("Analyzer failed to generate results.");
        return false;
    }

    PRUint32 appUnitsPerDevPixel = aShapedWord->AppUnitsPerDevUnit();

    UINT32 maxGlyphs = 0;
trymoreglyphs:
    if ((PR_UINT32_MAX - 3 * length / 2 + 16) < maxGlyphs) {
        // This isn't going to work, we're going to cross the UINT32 upper
        // limit. Give up.
        NS_WARNING("Shaper needs to generate more than 2^32 glyphs?!");
        return false;
    }
    maxGlyphs += 3 * length / 2 + 16;

    nsAutoTArray<UINT16, 400> clusters;
    nsAutoTArray<UINT16, 400> indices;
    nsAutoTArray<DWRITE_SHAPING_TEXT_PROPERTIES, 400> textProperties;
    nsAutoTArray<DWRITE_SHAPING_GLYPH_PROPERTIES, 400> glyphProperties;
    if (!clusters.SetLength(length) ||
        !indices.SetLength(maxGlyphs) || 
        !textProperties.SetLength(maxGlyphs) ||
        !glyphProperties.SetLength(maxGlyphs)) {
        NS_WARNING("Shaper failed to allocate memory.");
        return false;
    }

    UINT32 actualGlyphs;

    hr = analyzer->GetGlyphs(aString, length,
            font->GetFontFace(), FALSE, 
            readingDirection == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT,
            &runHead->mScript, NULL, NULL, NULL, NULL, 0,
            maxGlyphs, clusters.Elements(), textProperties.Elements(),
            indices.Elements(), glyphProperties.Elements(), &actualGlyphs);

    if (hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)) {
        // We increase the amount of glyphs and try again.
        goto trymoreglyphs;
    }
    if (FAILED(hr)) {
        NS_WARNING("Analyzer failed to get glyphs.");
        return false;
    }

    WORD gID = indices[0];
    nsAutoTArray<FLOAT, 400> advances;
    nsAutoTArray<DWRITE_GLYPH_OFFSET, 400> glyphOffsets;
    if (!advances.SetLength(actualGlyphs) || 
        !glyphOffsets.SetLength(actualGlyphs)) {
        NS_WARNING("Shaper failed to allocate memory.");
        return false;
    }

    if (!static_cast<gfxDWriteFont*>(mFont)->mUseSubpixelPositions) {
        hr = analyzer->GetGdiCompatibleGlyphPlacements(
                                          aString,
                                          clusters.Elements(),
                                          textProperties.Elements(),
                                          length,
                                          indices.Elements(),
                                          glyphProperties.Elements(),
                                          actualGlyphs,
                                          font->GetFontFace(),
                                          font->GetAdjustedSize(),
                                          1.0,
                                          nsnull,
                                          FALSE,
                                          FALSE,
                                          FALSE,
                                          &runHead->mScript,
                                          NULL,
                                          NULL,
                                          NULL,
                                          0,
                                          advances.Elements(),
                                          glyphOffsets.Elements());
    } else {
        hr = analyzer->GetGlyphPlacements(aString,
                                          clusters.Elements(),
                                          textProperties.Elements(),
                                          length,
                                          indices.Elements(),
                                          glyphProperties.Elements(),
                                          actualGlyphs,
                                          font->GetFontFace(),
                                          font->GetAdjustedSize(),
                                          FALSE,
                                          FALSE,
                                          &runHead->mScript,
                                          NULL,
                                          NULL,
                                          NULL,
                                          0,
                                          advances.Elements(),
                                          glyphOffsets.Elements());
    }
    if (FAILED(hr)) {
        NS_WARNING("Analyzer failed to get glyph placements.");
        return false;
    }

    nsAutoTArray<gfxTextRun::DetailedGlyph,1> detailedGlyphs;

    for (unsigned int c = 0; c < length; c++) {
        PRUint32 k = clusters[c];
        PRUint32 absC = c;

        if (c > 0 && k == clusters[c - 1]) {
            g.SetComplex(aShapedWord->IsClusterStart(absC), false, 0);
            aShapedWord->SetGlyphs(absC, g, nsnull);
            // This is a cluster continuation. No glyph here.
            continue;
        }

        // Count glyphs for this character
        PRUint32 glyphCount = actualGlyphs - k;
        PRUint32 nextClusterOffset;
        for (nextClusterOffset = c + 1; 
            nextClusterOffset < length; ++nextClusterOffset) {
            if (clusters[nextClusterOffset] > k) {
                glyphCount = clusters[nextClusterOffset] - k;
                break;
            }
        }
        PRInt32 advance = (PRInt32)(advances[k] * appUnitsPerDevPixel);
        if (glyphCount == 1 && advance >= 0 &&
            glyphOffsets[k].advanceOffset == 0 &&
            glyphOffsets[k].ascenderOffset == 0 &&
            aShapedWord->IsClusterStart(absC) &&
            gfxShapedWord::CompressedGlyph::IsSimpleAdvance(advance) &&
            gfxShapedWord::CompressedGlyph::IsSimpleGlyphID(indices[k])) {
              aShapedWord->SetSimpleGlyph(absC, 
                                          g.SetSimpleGlyph(advance, 
                                                           indices[k]));
        } else {
            if (detailedGlyphs.Length() < glyphCount) {
                if (!detailedGlyphs.AppendElements(
                    glyphCount - detailedGlyphs.Length())) {
                    continue;
                }
            }
            float totalAdvance = 0;
            for (unsigned int z = 0; z < glyphCount; z++) {
                detailedGlyphs[z].mGlyphID = indices[k + z];
                detailedGlyphs[z].mAdvance = 
                    (PRInt32)(advances[k + z]
                       * appUnitsPerDevPixel);
                if (readingDirection == 
                    DWRITE_READING_DIRECTION_RIGHT_TO_LEFT) {
                    detailedGlyphs[z].mXOffset = 
                        (totalAdvance + 
                          glyphOffsets[k + z].advanceOffset)
                        * appUnitsPerDevPixel;
                } else {
                    detailedGlyphs[z].mXOffset = 
                        glyphOffsets[k + z].advanceOffset *
                        appUnitsPerDevPixel;
                }
                detailedGlyphs[z].mYOffset = 
                    -glyphOffsets[k + z].ascenderOffset *
                    appUnitsPerDevPixel;
                totalAdvance += advances[k + z];
            }
            aShapedWord->SetGlyphs(
                absC,
                g.SetComplex(aShapedWord->IsClusterStart(absC),
                             true,
                             glyphCount),
                detailedGlyphs.Elements());
        }
    }

    return true;
}
