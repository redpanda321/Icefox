/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZ_LOGGING
#define FORCE_PR_LOG /* Allow logging in the release build */
#endif
#include "prlog.h"

#include "mozilla/Util.h"

#include "gfxFontUtils.h"

#include "nsServiceManagerUtils.h"

#include "mozilla/Preferences.h"

#include "nsIStreamBufferAccess.h"
#include "nsIUUIDGenerator.h"
#include "nsMemory.h"
#include "nsICharsetConverterManager.h"

#include "plbase64.h"
#include "prlog.h"

#include "woff.h"

#ifdef XP_MACOSX
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef PR_LOGGING

#define LOG(log, args) PR_LOG(gfxPlatform::GetLog(log), \
                               PR_LOG_DEBUG, args)

#endif // PR_LOGGING

#define NO_RANGE_FOUND 126 // bit 126 in the font unicode ranges is required to be 0

#define UNICODE_BMP_LIMIT 0x10000

using namespace mozilla;

/* Unicode subrange table
 *   from: http://msdn.microsoft.com/en-us/library/dd374090
 *
 * Edit the text to extend the initial digit, then use something like:
 * perl -pi -e 's/^(\d+)\t([\dA-Fa-f]+)\s+-\s+([\dA-Fa-f]+)\s+\b([a-zA-Z0-9\(\)\- ]+)/    { \1, 0x\2, 0x\3, \"\4\" },/' < unicoderange.txt
 * to generate the below list.
 */
struct UnicodeRangeTableEntry
{
    uint8_t bit;
    uint32_t start;
    uint32_t end;
    const char *info;
};

static struct UnicodeRangeTableEntry gUnicodeRanges[] = {
    { 0, 0x0000, 0x007F, "Basic Latin" },
    { 1, 0x0080, 0x00FF, "Latin-1 Supplement" },
    { 2, 0x0100, 0x017F, "Latin Extended-A" },
    { 3, 0x0180, 0x024F, "Latin Extended-B" },
    { 4, 0x0250, 0x02AF, "IPA Extensions" },
    { 4, 0x1D00, 0x1D7F, "Phonetic Extensions" },
    { 4, 0x1D80, 0x1DBF, "Phonetic Extensions Supplement" },
    { 5, 0x02B0, 0x02FF, "Spacing Modifier Letters" },
    { 5, 0xA700, 0xA71F, "Modifier Tone Letters" },
    { 6, 0x0300, 0x036F, "Combining Diacritical Marks" },
    { 6, 0x1DC0, 0x1DFF, "Combining Diacritical Marks Supplement" },
    { 7, 0x0370, 0x03FF, "Greek and Coptic" },
    { 8, 0x2C80, 0x2CFF, "Coptic" },
    { 9, 0x0400, 0x04FF, "Cyrillic" },
    { 9, 0x0500, 0x052F, "Cyrillic Supplement" },
    { 9, 0x2DE0, 0x2DFF, "Cyrillic Extended-A" },
    { 9, 0xA640, 0xA69F, "Cyrillic Extended-B" },
    { 10, 0x0530, 0x058F, "Armenian" },
    { 11, 0x0590, 0x05FF, "Hebrew" },
    { 12, 0xA500, 0xA63F, "Vai" },
    { 13, 0x0600, 0x06FF, "Arabic" },
    { 13, 0x0750, 0x077F, "Arabic Supplement" },
    { 14, 0x07C0, 0x07FF, "NKo" },
    { 15, 0x0900, 0x097F, "Devanagari" },
    { 16, 0x0980, 0x09FF, "Bengali" },
    { 17, 0x0A00, 0x0A7F, "Gurmukhi" },
    { 18, 0x0A80, 0x0AFF, "Gujarati" },
    { 19, 0x0B00, 0x0B7F, "Oriya" },
    { 20, 0x0B80, 0x0BFF, "Tamil" },
    { 21, 0x0C00, 0x0C7F, "Telugu" },
    { 22, 0x0C80, 0x0CFF, "Kannada" },
    { 23, 0x0D00, 0x0D7F, "Malayalam" },
    { 24, 0x0E00, 0x0E7F, "Thai" },
    { 25, 0x0E80, 0x0EFF, "Lao" },
    { 26, 0x10A0, 0x10FF, "Georgian" },
    { 26, 0x2D00, 0x2D2F, "Georgian Supplement" },
    { 27, 0x1B00, 0x1B7F, "Balinese" },
    { 28, 0x1100, 0x11FF, "Hangul Jamo" },
    { 29, 0x1E00, 0x1EFF, "Latin Extended Additional" },
    { 29, 0x2C60, 0x2C7F, "Latin Extended-C" },
    { 29, 0xA720, 0xA7FF, "Latin Extended-D" },
    { 30, 0x1F00, 0x1FFF, "Greek Extended" },
    { 31, 0x2000, 0x206F, "General Punctuation" },
    { 31, 0x2E00, 0x2E7F, "Supplemental Punctuation" },
    { 32, 0x2070, 0x209F, "Superscripts And Subscripts" },
    { 33, 0x20A0, 0x20CF, "Currency Symbols" },
    { 34, 0x20D0, 0x20FF, "Combining Diacritical Marks For Symbols" },
    { 35, 0x2100, 0x214F, "Letterlike Symbols" },
    { 36, 0x2150, 0x218F, "Number Forms" },
    { 37, 0x2190, 0x21FF, "Arrows" },
    { 37, 0x27F0, 0x27FF, "Supplemental Arrows-A" },
    { 37, 0x2900, 0x297F, "Supplemental Arrows-B" },
    { 37, 0x2B00, 0x2BFF, "Miscellaneous Symbols and Arrows" },
    { 38, 0x2200, 0x22FF, "Mathematical Operators" },
    { 38, 0x27C0, 0x27EF, "Miscellaneous Mathematical Symbols-A" },
    { 38, 0x2980, 0x29FF, "Miscellaneous Mathematical Symbols-B" },
    { 38, 0x2A00, 0x2AFF, "Supplemental Mathematical Operators" },
    { 39, 0x2300, 0x23FF, "Miscellaneous Technical" },
    { 40, 0x2400, 0x243F, "Control Pictures" },
    { 41, 0x2440, 0x245F, "Optical Character Recognition" },
    { 42, 0x2460, 0x24FF, "Enclosed Alphanumerics" },
    { 43, 0x2500, 0x257F, "Box Drawing" },
    { 44, 0x2580, 0x259F, "Block Elements" },
    { 45, 0x25A0, 0x25FF, "Geometric Shapes" },
    { 46, 0x2600, 0x26FF, "Miscellaneous Symbols" },
    { 47, 0x2700, 0x27BF, "Dingbats" },
    { 48, 0x3000, 0x303F, "CJK Symbols And Punctuation" },
    { 49, 0x3040, 0x309F, "Hiragana" },
    { 50, 0x30A0, 0x30FF, "Katakana" },
    { 50, 0x31F0, 0x31FF, "Katakana Phonetic Extensions" },
    { 51, 0x3100, 0x312F, "Bopomofo" },
    { 50, 0x31A0, 0x31BF, "Bopomofo Extended" },
    { 52, 0x3130, 0x318F, "Hangul Compatibility Jamo" },
    { 53, 0xA840, 0xA87F, "Phags-pa" },
    { 54, 0x3200, 0x32FF, "Enclosed CJK Letters And Months" },
    { 55, 0x3300, 0x33FF, "CJK Compatibility" },
    { 56, 0xAC00, 0xD7AF, "Hangul Syllables" },
    { 57, 0xD800, 0xDFFF, "Non-Plane 0" },
    { 58, 0x10900, 0x1091F, "Phoenician" },
    { 59, 0x2E80, 0x2EFF, "CJK Radicals Supplement" },
    { 59, 0x2F00, 0x2FDF, "Kangxi Radicals" },
    { 59, 0x2FF0, 0x2FFF, "Ideographic Description Characters" },
    { 59, 0x3190, 0x319F, "Kanbun" },
    { 59, 0x3400, 0x4DBF, "CJK Unified Ideographs Extension A" },
    { 59, 0x4E00, 0x9FFF, "CJK Unified Ideographs" },
    { 59, 0x20000, 0x2A6DF, "CJK Unified Ideographs Extension B" },
    { 60, 0xE000, 0xF8FF, "Private Use Area" },
    { 61, 0x31C0, 0x31EF, "CJK Strokes" },
    { 61, 0xF900, 0xFAFF, "CJK Compatibility Ideographs" },
    { 61, 0x2F800, 0x2FA1F, "CJK Compatibility Ideographs Supplement" },
    { 62, 0xFB00, 0xFB4F, "Alphabetic Presentation Forms" },
    { 63, 0xFB50, 0xFDFF, "Arabic Presentation Forms-A" },
    { 64, 0xFE20, 0xFE2F, "Combining Half Marks" },
    { 65, 0xFE10, 0xFE1F, "Vertical Forms" },
    { 65, 0xFE30, 0xFE4F, "CJK Compatibility Forms" },
    { 66, 0xFE50, 0xFE6F, "Small Form Variants" },
    { 67, 0xFE70, 0xFEFF, "Arabic Presentation Forms-B" },
    { 68, 0xFF00, 0xFFEF, "Halfwidth And Fullwidth Forms" },
    { 69, 0xFFF0, 0xFFFF, "Specials" },
    { 70, 0x0F00, 0x0FFF, "Tibetan" },
    { 71, 0x0700, 0x074F, "Syriac" },
    { 72, 0x0780, 0x07BF, "Thaana" },
    { 73, 0x0D80, 0x0DFF, "Sinhala" },
    { 74, 0x1000, 0x109F, "Myanmar" },
    { 75, 0x1200, 0x137F, "Ethiopic" },
    { 75, 0x1380, 0x139F, "Ethiopic Supplement" },
    { 75, 0x2D80, 0x2DDF, "Ethiopic Extended" },
    { 76, 0x13A0, 0x13FF, "Cherokee" },
    { 77, 0x1400, 0x167F, "Unified Canadian Aboriginal Syllabics" },
    { 78, 0x1680, 0x169F, "Ogham" },
    { 79, 0x16A0, 0x16FF, "Runic" },
    { 80, 0x1780, 0x17FF, "Khmer" },
    { 80, 0x19E0, 0x19FF, "Khmer Symbols" },
    { 81, 0x1800, 0x18AF, "Mongolian" },
    { 82, 0x2800, 0x28FF, "Braille Patterns" },
    { 83, 0xA000, 0xA48F, "Yi Syllables" },
    { 83, 0xA490, 0xA4CF, "Yi Radicals" },
    { 84, 0x1700, 0x171F, "Tagalog" },
    { 84, 0x1720, 0x173F, "Hanunoo" },
    { 84, 0x1740, 0x175F, "Buhid" },
    { 84, 0x1760, 0x177F, "Tagbanwa" },
    { 85, 0x10300, 0x1032F, "Old Italic" },
    { 86, 0x10330, 0x1034F, "Gothic" },
    { 87, 0x10400, 0x1044F, "Deseret" },
    { 88, 0x1D000, 0x1D0FF, "Byzantine Musical Symbols" },
    { 88, 0x1D100, 0x1D1FF, "Musical Symbols" },
    { 88, 0x1D200, 0x1D24F, "Ancient Greek Musical Notation" },
    { 89, 0x1D400, 0x1D7FF, "Mathematical Alphanumeric Symbols" },
    { 90, 0xFF000, 0xFFFFD, "Private Use (plane 15)" },
    { 90, 0x100000, 0x10FFFD, "Private Use (plane 16)" },
    { 91, 0xFE00, 0xFE0F, "Variation Selectors" },
    { 91, 0xE0100, 0xE01EF, "Variation Selectors Supplement" },
    { 92, 0xE0000, 0xE007F, "Tags" },
    { 93, 0x1900, 0x194F, "Limbu" },
    { 94, 0x1950, 0x197F, "Tai Le" },
    { 95, 0x1980, 0x19DF, "New Tai Lue" },
    { 96, 0x1A00, 0x1A1F, "Buginese" },
    { 97, 0x2C00, 0x2C5F, "Glagolitic" },
    { 98, 0x2D30, 0x2D7F, "Tifinagh" },
    { 99, 0x4DC0, 0x4DFF, "Yijing Hexagram Symbols" },
    { 100, 0xA800, 0xA82F, "Syloti Nagri" },
    { 101, 0x10000, 0x1007F, "Linear B Syllabary" },
    { 101, 0x10080, 0x100FF, "Linear B Ideograms" },
    { 101, 0x10100, 0x1013F, "Aegean Numbers" },
    { 102, 0x10140, 0x1018F, "Ancient Greek Numbers" },
    { 103, 0x10380, 0x1039F, "Ugaritic" },
    { 104, 0x103A0, 0x103DF, "Old Persian" },
    { 105, 0x10450, 0x1047F, "Shavian" },
    { 106, 0x10480, 0x104AF, "Osmanya" },
    { 107, 0x10800, 0x1083F, "Cypriot Syllabary" },
    { 108, 0x10A00, 0x10A5F, "Kharoshthi" },
    { 109, 0x1D300, 0x1D35F, "Tai Xuan Jing Symbols" },
    { 110, 0x12000, 0x123FF, "Cuneiform" },
    { 110, 0x12400, 0x1247F, "Cuneiform Numbers and Punctuation" },
    { 111, 0x1D360, 0x1D37F, "Counting Rod Numerals" },
    { 112, 0x1B80, 0x1BBF, "Sundanese" },
    { 113, 0x1C00, 0x1C4F, "Lepcha" },
    { 114, 0x1C50, 0x1C7F, "Ol Chiki" },
    { 115, 0xA880, 0xA8DF, "Saurashtra" },
    { 116, 0xA900, 0xA92F, "Kayah Li" },
    { 117, 0xA930, 0xA95F, "Rejang" },
    { 118, 0xAA00, 0xAA5F, "Cham" },
    { 119, 0x10190, 0x101CF, "Ancient Symbols" },
    { 120, 0x101D0, 0x101FF, "Phaistos Disc" },
    { 121, 0x10280, 0x1029F, "Lycian" },
    { 121, 0x102A0, 0x102DF, "Carian" },
    { 121, 0x10920, 0x1093F, "Lydian" },
    { 122, 0x1F000, 0x1F02F, "Mahjong Tiles" },
    { 122, 0x1F030, 0x1F09F, "Domino Tiles" }
};

#pragma pack(1)

typedef struct {
    AutoSwap_PRUint16 format;
    AutoSwap_PRUint16 reserved;
    AutoSwap_PRUint32 length;
    AutoSwap_PRUint32 language;
    AutoSwap_PRUint32 numGroups;
} Format12CmapHeader;

typedef struct {
    AutoSwap_PRUint32 startCharCode;
    AutoSwap_PRUint32 endCharCode;
    AutoSwap_PRUint32 startGlyphId;
} Format12Group;

#pragma pack()

#if PR_LOGGING
void
gfxSparseBitSet::Dump(const char* aPrefix, eGfxLog aWhichLog) const
{
    NS_ASSERTION(mBlocks.DebugGetHeader(), "mHdr is null, this is bad");
    uint32_t b, numBlocks = mBlocks.Length();

    for (b = 0; b < numBlocks; b++) {
        Block *block = mBlocks[b];
        if (!block) continue;
        char outStr[256];
        int index = 0;
        index += sprintf(&outStr[index], "%s u+%6.6x [", aPrefix, (b << BLOCK_INDEX_SHIFT));
        for (int i = 0; i < 32; i += 4) {
            for (int j = i; j < i + 4; j++) {
                uint8_t bits = block->mBits[j];
                uint8_t flip1 = ((bits & 0xaa) >> 1) | ((bits & 0x55) << 1);
                uint8_t flip2 = ((flip1 & 0xcc) >> 2) | ((flip1 & 0x33) << 2);
                uint8_t flipped = ((flip2 & 0xf0) >> 4) | ((flip2 & 0x0f) << 4);

                index += sprintf(&outStr[index], "%2.2x", flipped);
            }
            if (i + 4 != 32) index += sprintf(&outStr[index], " ");
        }
        index += sprintf(&outStr[index], "]");
        LOG(aWhichLog, ("%s", outStr));
    }
}
#endif


nsresult
gfxFontUtils::ReadCMAPTableFormat12(const uint8_t *aBuf, uint32_t aLength,
                                    gfxSparseBitSet& aCharacterMap) 
{
    // Ensure table is large enough that we can safely read the header
    NS_ENSURE_TRUE(aLength >= sizeof(Format12CmapHeader),
                    NS_ERROR_GFX_CMAP_MALFORMED);

    // Sanity-check header fields
    const Format12CmapHeader *cmap12 =
        reinterpret_cast<const Format12CmapHeader*>(aBuf);
    NS_ENSURE_TRUE(uint16_t(cmap12->format) == 12, 
                   NS_ERROR_GFX_CMAP_MALFORMED);
    NS_ENSURE_TRUE(uint16_t(cmap12->reserved) == 0, 
                   NS_ERROR_GFX_CMAP_MALFORMED);

    uint32_t tablelen = cmap12->length;
    NS_ENSURE_TRUE(tablelen >= sizeof(Format12CmapHeader) &&
                   tablelen <= aLength, NS_ERROR_GFX_CMAP_MALFORMED);

    NS_ENSURE_TRUE(cmap12->language == 0, NS_ERROR_GFX_CMAP_MALFORMED);

    // Check that the table is large enough for the group array
    const uint32_t numGroups = cmap12->numGroups;
    NS_ENSURE_TRUE((tablelen - sizeof(Format12CmapHeader)) /
                       sizeof(Format12Group) >= numGroups,
                   NS_ERROR_GFX_CMAP_MALFORMED);

    // The array of groups immediately follows the subtable header.
    const Format12Group *group =
        reinterpret_cast<const Format12Group*>(aBuf + sizeof(Format12CmapHeader));

    // Check that groups are in correct order and do not overlap,
    // and record character coverage in aCharacterMap.
    uint32_t prevEndCharCode = 0;
    for (uint32_t i = 0; i < numGroups; i++, group++) {
        const uint32_t startCharCode = group->startCharCode;
        const uint32_t endCharCode = group->endCharCode;
        NS_ENSURE_TRUE((prevEndCharCode < startCharCode || i == 0) &&
                       startCharCode <= endCharCode &&
                       endCharCode <= CMAP_MAX_CODEPOINT, 
                       NS_ERROR_GFX_CMAP_MALFORMED);
        aCharacterMap.SetRange(startCharCode, endCharCode);
        prevEndCharCode = endCharCode;
    }

    aCharacterMap.Compact();

    return NS_OK;
}

nsresult 
gfxFontUtils::ReadCMAPTableFormat4(const uint8_t *aBuf, uint32_t aLength,
                                   gfxSparseBitSet& aCharacterMap)
{
    enum {
        OffsetFormat = 0,
        OffsetLength = 2,
        OffsetLanguage = 4,
        OffsetSegCountX2 = 6
    };

    NS_ENSURE_TRUE(ReadShortAt(aBuf, OffsetFormat) == 4, 
                   NS_ERROR_GFX_CMAP_MALFORMED);
    uint16_t tablelen = ReadShortAt(aBuf, OffsetLength);
    NS_ENSURE_TRUE(tablelen <= aLength, NS_ERROR_GFX_CMAP_MALFORMED);
    NS_ENSURE_TRUE(tablelen > 16, NS_ERROR_GFX_CMAP_MALFORMED);
    
    // This field should normally (except for Mac platform subtables) be zero according to
    // the OT spec, but some buggy fonts have lang = 1 (which would be English for MacOS).
    // E.g. Arial Narrow Bold, v. 1.1 (Tiger), Arial Unicode MS (see bug 530614).
    // So accept either zero or one here; the error should be harmless.
    NS_ENSURE_TRUE((ReadShortAt(aBuf, OffsetLanguage) & 0xfffe) == 0, 
                   NS_ERROR_GFX_CMAP_MALFORMED);

    uint16_t segCountX2 = ReadShortAt(aBuf, OffsetSegCountX2);
    NS_ENSURE_TRUE(tablelen >= 16 + (segCountX2 * 4), 
                   NS_ERROR_GFX_CMAP_MALFORMED);

    const uint16_t segCount = segCountX2 / 2;

    const uint16_t *endCounts = reinterpret_cast<const uint16_t*>(aBuf + 14);
    const uint16_t *startCounts = endCounts + 1 /* skip one uint16_t for reservedPad */ + segCount;
    const uint16_t *idDeltas = startCounts + segCount;
    const uint16_t *idRangeOffsets = idDeltas + segCount;
    uint16_t prevEndCount = 0;
    for (uint16_t i = 0; i < segCount; i++) {
        const uint16_t endCount = ReadShortAt16(endCounts, i);
        const uint16_t startCount = ReadShortAt16(startCounts, i);
        const uint16_t idRangeOffset = ReadShortAt16(idRangeOffsets, i);

        // sanity-check range
        // This permits ranges to overlap by 1 character, which is strictly
        // incorrect but occurs in Baskerville on OS X 10.7 (see bug 689087),
        // and appears to be harmless in practice
        NS_ENSURE_TRUE(startCount >= prevEndCount && startCount <= endCount,
                       NS_ERROR_GFX_CMAP_MALFORMED);
        prevEndCount = endCount;

        if (idRangeOffset == 0) {
            aCharacterMap.SetRange(startCount, endCount);
        } else {
            // const uint16_t idDelta = ReadShortAt16(idDeltas, i); // Unused: self-documenting.
            for (uint32_t c = startCount; c <= endCount; ++c) {
                if (c == 0xFFFF)
                    break;

                const uint16_t *gdata = (idRangeOffset/2 
                                         + (c - startCount)
                                         + &idRangeOffsets[i]);

                NS_ENSURE_TRUE((uint8_t*)gdata > aBuf && 
                               (uint8_t*)gdata < aBuf + aLength, 
                               NS_ERROR_GFX_CMAP_MALFORMED);

                // make sure we have a glyph
                if (*gdata != 0) {
                    // The glyph index at this point is:
                    // glyph = (ReadShortAt16(idDeltas, i) + *gdata) % 65536;

                    aCharacterMap.set(c);
                }
            }
        }
    }

    aCharacterMap.Compact();

    return NS_OK;
}

nsresult
gfxFontUtils::ReadCMAPTableFormat14(const uint8_t *aBuf, uint32_t aLength,
                                    uint8_t*& aTable)
{
    enum {
        OffsetFormat = 0,
        OffsetTableLength = 2,
        OffsetNumVarSelectorRecords = 6,
        OffsetVarSelectorRecords = 10,

        SizeOfVarSelectorRecord = 11,
        VSRecOffsetVarSelector = 0,
        VSRecOffsetDefUVSOffset = 3,
        VSRecOffsetNonDefUVSOffset = 7,

        SizeOfDefUVSTable = 4,
        DefUVSOffsetStartUnicodeValue = 0,
        DefUVSOffsetAdditionalCount = 3,

        SizeOfNonDefUVSTable = 5,
        NonDefUVSOffsetUnicodeValue = 0,
        NonDefUVSOffsetGlyphID = 3
    };
    NS_ENSURE_TRUE(aLength >= OffsetVarSelectorRecords,
                   NS_ERROR_GFX_CMAP_MALFORMED);

    NS_ENSURE_TRUE(ReadShortAt(aBuf, OffsetFormat) == 14, 
                   NS_ERROR_GFX_CMAP_MALFORMED);

    uint32_t tablelen = ReadLongAt(aBuf, OffsetTableLength);
    NS_ENSURE_TRUE(tablelen <= aLength, NS_ERROR_GFX_CMAP_MALFORMED);
    NS_ENSURE_TRUE(tablelen >= OffsetVarSelectorRecords,
                   NS_ERROR_GFX_CMAP_MALFORMED);

    const uint32_t numVarSelectorRecords = ReadLongAt(aBuf, OffsetNumVarSelectorRecords);
    NS_ENSURE_TRUE((tablelen - OffsetVarSelectorRecords) /
                   SizeOfVarSelectorRecord >= numVarSelectorRecords,
                   NS_ERROR_GFX_CMAP_MALFORMED);

    const uint8_t *records = aBuf + OffsetVarSelectorRecords;
    for (uint32_t i = 0; i < numVarSelectorRecords; 
         i++, records += SizeOfVarSelectorRecord) {
        const uint32_t varSelector = ReadUint24At(records, VSRecOffsetVarSelector);
        const uint32_t defUVSOffset = ReadLongAt(records, VSRecOffsetDefUVSOffset);
        const uint32_t nonDefUVSOffset = ReadLongAt(records, VSRecOffsetNonDefUVSOffset);
        NS_ENSURE_TRUE(varSelector <= CMAP_MAX_CODEPOINT &&
                       defUVSOffset <= tablelen - 4 &&
                       nonDefUVSOffset <= tablelen - 4, 
                       NS_ERROR_GFX_CMAP_MALFORMED);

        if (defUVSOffset) {
            const uint32_t numUnicodeValueRanges = ReadLongAt(aBuf, defUVSOffset);
            NS_ENSURE_TRUE((tablelen - defUVSOffset) /
                           SizeOfDefUVSTable >= numUnicodeValueRanges,
                           NS_ERROR_GFX_CMAP_MALFORMED);
            const uint8_t *tables = aBuf + defUVSOffset + 4;
            uint32_t prevEndUnicode = 0;
            for (uint32_t j = 0; j < numUnicodeValueRanges; j++, tables += SizeOfDefUVSTable) {
                const uint32_t startUnicode = ReadUint24At(tables, DefUVSOffsetStartUnicodeValue);
                const uint32_t endUnicode = startUnicode + tables[DefUVSOffsetAdditionalCount];
                NS_ENSURE_TRUE((prevEndUnicode < startUnicode || j == 0) &&
                               endUnicode <= CMAP_MAX_CODEPOINT, 
                               NS_ERROR_GFX_CMAP_MALFORMED);
                prevEndUnicode = endUnicode;
            }
        }

        if (nonDefUVSOffset) {
            const uint32_t numUVSMappings = ReadLongAt(aBuf, nonDefUVSOffset);
            NS_ENSURE_TRUE((tablelen - nonDefUVSOffset) /
                           SizeOfNonDefUVSTable >= numUVSMappings,
                           NS_ERROR_GFX_CMAP_MALFORMED);
            const uint8_t *tables = aBuf + nonDefUVSOffset + 4;
            uint32_t prevUnicode = 0;
            for (uint32_t j = 0; j < numUVSMappings; j++, tables += SizeOfNonDefUVSTable) {
                const uint32_t unicodeValue = ReadUint24At(tables, NonDefUVSOffsetUnicodeValue);
                NS_ENSURE_TRUE((prevUnicode < unicodeValue || j == 0) &&
                               unicodeValue <= CMAP_MAX_CODEPOINT, 
                               NS_ERROR_GFX_CMAP_MALFORMED);
                prevUnicode = unicodeValue;
            }
        }
    }

    aTable = new uint8_t[tablelen];
    memcpy(aTable, aBuf, tablelen);

    return NS_OK;
}

// Windows requires fonts to have a format-4 cmap with a Microsoft ID (3).  On the Mac, fonts either have
// a format-4 cmap with Microsoft platform/encoding id or they have one with a platformID == Unicode (0)
// For fonts with two format-4 tables, the first one (Unicode platform) is preferred on the Mac.

#if defined(XP_MACOSX)
    #define acceptableFormat4(p,e,k) (((p) == PLATFORM_ID_MICROSOFT && (e) == EncodingIDMicrosoft && !(k)) || \
                                      ((p) == PLATFORM_ID_UNICODE))

    #define acceptableUCS4Encoding(p, e, k) \
        (((p) == PLATFORM_ID_MICROSOFT && (e) == EncodingIDUCS4ForMicrosoftPlatform) && (k) != 12 || \
         ((p) == PLATFORM_ID_UNICODE   && \
          ((e) == EncodingIDDefaultForUnicodePlatform || (e) >= EncodingIDUCS4ForUnicodePlatform)))
#else
    #define acceptableFormat4(p,e,k) ((p) == PLATFORM_ID_MICROSOFT && (e) == EncodingIDMicrosoft)

    #define acceptableUCS4Encoding(p, e, k) \
        ((p) == PLATFORM_ID_MICROSOFT && (e) == EncodingIDUCS4ForMicrosoftPlatform)
#endif

#define acceptablePlatform(p) ((p) == PLATFORM_ID_UNICODE || (p) == PLATFORM_ID_MICROSOFT)
#define isSymbol(p,e)         ((p) == PLATFORM_ID_MICROSOFT && (e) == EncodingIDSymbol)
#define isUVSEncoding(p, e)   ((p) == PLATFORM_ID_UNICODE && (e) == EncodingIDUVSForUnicodePlatform)

uint32_t
gfxFontUtils::FindPreferredSubtable(const uint8_t *aBuf, uint32_t aBufLength,
                                    uint32_t *aTableOffset,
                                    uint32_t *aUVSTableOffset,
                                    bool *aSymbolEncoding)
{
    enum {
        OffsetVersion = 0,
        OffsetNumTables = 2,
        SizeOfHeader = 4,

        TableOffsetPlatformID = 0,
        TableOffsetEncodingID = 2,
        TableOffsetOffset = 4,
        SizeOfTable = 8,

        SubtableOffsetFormat = 0
    };
    enum {
        EncodingIDSymbol = 0,
        EncodingIDMicrosoft = 1,
        EncodingIDDefaultForUnicodePlatform = 0,
        EncodingIDUCS4ForUnicodePlatform = 3,
        EncodingIDUVSForUnicodePlatform = 5,
        EncodingIDUCS4ForMicrosoftPlatform = 10
    };

    if (aUVSTableOffset) {
        *aUVSTableOffset = 0;
    }

    if (!aBuf || aBufLength < SizeOfHeader) {
        // cmap table is missing, or too small to contain header fields!
        return 0;
    }

    // uint16_t version = ReadShortAt(aBuf, OffsetVersion); // Unused: self-documenting.
    uint16_t numTables = ReadShortAt(aBuf, OffsetNumTables);
    if (aBufLength < uint32_t(SizeOfHeader + numTables * SizeOfTable)) {
        return 0;
    }

    // save the format we want here
    uint32_t keepFormat = 0;

    const uint8_t *table = aBuf + SizeOfHeader;
    for (uint16_t i = 0; i < numTables; ++i, table += SizeOfTable) {
        const uint16_t platformID = ReadShortAt(table, TableOffsetPlatformID);
        if (!acceptablePlatform(platformID))
            continue;

        const uint16_t encodingID = ReadShortAt(table, TableOffsetEncodingID);
        const uint32_t offset = ReadLongAt(table, TableOffsetOffset);
        if (aBufLength - 2 < offset) {
            // this subtable is not valid - beyond end of buffer
            return 0;
        }

        const uint8_t *subtable = aBuf + offset;
        const uint16_t format = ReadShortAt(subtable, SubtableOffsetFormat);

        if (isSymbol(platformID, encodingID)) {
            keepFormat = format;
            *aTableOffset = offset;
            *aSymbolEncoding = true;
            break;
        } else if (format == 4 && acceptableFormat4(platformID, encodingID, keepFormat)) {
            keepFormat = format;
            *aTableOffset = offset;
            *aSymbolEncoding = false;
        } else if (format == 12 && acceptableUCS4Encoding(platformID, encodingID, keepFormat)) {
            keepFormat = format;
            *aTableOffset = offset;
            *aSymbolEncoding = false;
            if (platformID > PLATFORM_ID_UNICODE || !aUVSTableOffset || *aUVSTableOffset) {
                break; // we don't want to try anything else when this format is available.
            }
        } else if (format == 14 && isUVSEncoding(platformID, encodingID) && aUVSTableOffset) {
            *aUVSTableOffset = offset;
            if (keepFormat == 12) {
                break;
            }
        }
    }

    return keepFormat;
}

nsresult
gfxFontUtils::ReadCMAP(const uint8_t *aBuf, uint32_t aBufLength,
                       gfxSparseBitSet& aCharacterMap,
                       uint32_t& aUVSOffset,
                       bool& aUnicodeFont, bool& aSymbolFont)
{
    uint32_t offset;
    bool     symbol;
    uint32_t format = FindPreferredSubtable(aBuf, aBufLength,
                                            &offset, &aUVSOffset, &symbol);

    if (format == 4) {
        if (symbol) {
            aUnicodeFont = false;
            aSymbolFont = true;
        } else {
            aUnicodeFont = true;
            aSymbolFont = false;
        }
        return ReadCMAPTableFormat4(aBuf + offset, aBufLength - offset,
                                    aCharacterMap);
    }

    if (format == 12) {
        aUnicodeFont = true;
        aSymbolFont = false;
        return ReadCMAPTableFormat12(aBuf + offset, aBufLength - offset,
                                     aCharacterMap);
    }

    return NS_ERROR_FAILURE;
}

#pragma pack(1)

typedef struct {
    AutoSwap_PRUint16 format;
    AutoSwap_PRUint16 length;
    AutoSwap_PRUint16 language;
    AutoSwap_PRUint16 segCountX2;
    AutoSwap_PRUint16 searchRange;
    AutoSwap_PRUint16 entrySelector;
    AutoSwap_PRUint16 rangeShift;

    AutoSwap_PRUint16 arrays[1];
} Format4Cmap;

typedef struct {
    AutoSwap_PRUint16 format;
    AutoSwap_PRUint32 length;
    AutoSwap_PRUint32 numVarSelectorRecords;

    typedef struct {
        AutoSwap_PRUint24 varSelector;
        AutoSwap_PRUint32 defaultUVSOffset;
        AutoSwap_PRUint32 nonDefaultUVSOffset;
    } VarSelectorRecord;

    VarSelectorRecord varSelectorRecords[1];
} Format14Cmap;

typedef struct {
    AutoSwap_PRUint32 numUVSMappings;

    typedef struct {
        AutoSwap_PRUint24 unicodeValue;
        AutoSwap_PRUint16 glyphID;
    } UVSMapping;

    UVSMapping uvsMappings[1];
} NonDefUVSTable;

#pragma pack()

uint32_t
gfxFontUtils::MapCharToGlyphFormat4(const uint8_t *aBuf, PRUnichar aCh)
{
    const Format4Cmap *cmap4 = reinterpret_cast<const Format4Cmap*>(aBuf);
    uint16_t segCount;
    const AutoSwap_PRUint16 *endCodes;
    const AutoSwap_PRUint16 *startCodes;
    const AutoSwap_PRUint16 *idDelta;
    const AutoSwap_PRUint16 *idRangeOffset;
    uint16_t probe;
    uint16_t rangeShiftOver2;
    uint16_t index;

    segCount = (uint16_t)(cmap4->segCountX2) / 2;

    endCodes = &cmap4->arrays[0];
    startCodes = &cmap4->arrays[segCount + 1]; // +1 for reserved word between arrays
    idDelta = &startCodes[segCount];
    idRangeOffset = &idDelta[segCount];

    probe = 1 << (uint16_t)(cmap4->entrySelector);
    rangeShiftOver2 = (uint16_t)(cmap4->rangeShift) / 2;

    if ((uint16_t)(startCodes[rangeShiftOver2]) <= aCh) {
        index = rangeShiftOver2;
    } else {
        index = 0;
    }

    while (probe > 1) {
        probe >>= 1;
        if ((uint16_t)(startCodes[index + probe]) <= aCh) {
            index += probe;
        }
    }

    if (aCh >= (uint16_t)(startCodes[index]) && aCh <= (uint16_t)(endCodes[index])) {
        uint16_t result;
        if ((uint16_t)(idRangeOffset[index]) == 0) {
            result = aCh;
        } else {
            uint16_t offset = aCh - (uint16_t)(startCodes[index]);
            const AutoSwap_PRUint16 *glyphIndexTable =
                (const AutoSwap_PRUint16*)((const char*)&idRangeOffset[index] +
                                           (uint16_t)(idRangeOffset[index]));
            result = glyphIndexTable[offset];
        }

        // note that this is unsigned 16-bit arithmetic, and may wrap around
        result += (uint16_t)(idDelta[index]);
        return result;
    }

    return 0;
}

uint32_t
gfxFontUtils::MapCharToGlyphFormat12(const uint8_t *aBuf, uint32_t aCh)
{
    const Format12CmapHeader *cmap12 =
        reinterpret_cast<const Format12CmapHeader*>(aBuf);

    // We know that numGroups is within range for the subtable size
    // because it was checked by ReadCMAPTableFormat12.
    uint32_t numGroups = cmap12->numGroups;

    // The array of groups immediately follows the subtable header.
    const Format12Group *groups =
        reinterpret_cast<const Format12Group*>(aBuf + sizeof(Format12CmapHeader));

    // For most efficient binary search, we want to work on a range that
    // is a power of 2 so that we can always halve it by shifting.
    // So we find the largest power of 2 that is <= numGroups.
    // We will offset this range by rangeOffset so as to reach the end
    // of the table, provided that doesn't put us beyond the target
    // value from the outset.
    uint32_t powerOf2 = mozilla::FindHighestBit(numGroups);
    uint32_t rangeOffset = numGroups - powerOf2;
    uint32_t range = 0;
    uint32_t startCharCode;

    if (groups[rangeOffset].startCharCode <= aCh) {
        range = rangeOffset;
    }

    // Repeatedly halve the size of the range until we find the target group
    while (powerOf2 > 1) {
        powerOf2 >>= 1;
        if (groups[range + powerOf2].startCharCode <= aCh) {
            range += powerOf2;
        }
    }

    // Check if the character is actually present in the range and return
    // the corresponding glyph ID
    startCharCode = groups[range].startCharCode;
    if (startCharCode <= aCh && groups[range].endCharCode >= aCh) {
        return groups[range].startGlyphId + aCh - startCharCode;
    }

    // Else it's not present, so return the .notdef glyph
    return 0;
}

uint16_t
gfxFontUtils::MapUVSToGlyphFormat14(const uint8_t *aBuf, uint32_t aCh, uint32_t aVS)
{
    const Format14Cmap *cmap14 = reinterpret_cast<const Format14Cmap*>(aBuf);

    // binary search in varSelectorRecords
    uint32_t min = 0;
    uint32_t max = cmap14->numVarSelectorRecords;
    uint32_t nonDefUVSOffset = 0;
    while (min < max) {
        uint32_t index = (min + max) >> 1;
        uint32_t varSelector = cmap14->varSelectorRecords[index].varSelector;
        if (aVS == varSelector) {
            nonDefUVSOffset = cmap14->varSelectorRecords[index].nonDefaultUVSOffset;
            break;
        }
        if (aVS < varSelector) {
            max = index;
        } else {
            min = index + 1;
        }
    }
    if (!nonDefUVSOffset) {
        return 0;
    }

    const NonDefUVSTable *table = reinterpret_cast<const NonDefUVSTable*>
                                      (aBuf + nonDefUVSOffset);

    // binary search in uvsMappings
    min = 0;
    max = table->numUVSMappings;
    while (min < max) {
        uint32_t index = (min + max) >> 1;
        uint32_t unicodeValue = table->uvsMappings[index].unicodeValue;
        if (aCh == unicodeValue) {
            return table->uvsMappings[index].glyphID;
        }
        if (aCh < unicodeValue) {
            max = index;
        } else {
            min = index + 1;
        }
    }

    return 0;
}

uint32_t
gfxFontUtils::MapCharToGlyph(const uint8_t *aCmapBuf, uint32_t aBufLength,
                             uint32_t aUnicode, uint32_t aVarSelector)
{
    uint32_t offset, uvsOffset;
    bool     symbol;
    uint32_t format = FindPreferredSubtable(aCmapBuf, aBufLength, &offset,
                                            &uvsOffset, &symbol);

    uint32_t gid;
    switch (format) {
    case 4:
        gid = aUnicode < UNICODE_BMP_LIMIT ?
            MapCharToGlyphFormat4(aCmapBuf + offset, PRUnichar(aUnicode)) : 0;
        break;
    case 12:
        gid = MapCharToGlyphFormat12(aCmapBuf + offset, aUnicode);
        break;
    default:
        NS_WARNING("unsupported cmap format, glyphs will be missing");
        gid = 0;
    }

    if (aVarSelector && uvsOffset && gid) {
        uint32_t varGID =
            gfxFontUtils::MapUVSToGlyphFormat14(aCmapBuf + uvsOffset,
                                                aUnicode, aVarSelector);
        if (varGID) {
            gid = varGID;
        }
        // else the variation sequence was not supported, use default mapping
        // of the character code alone
    }

    return gid;
}

uint8_t gfxFontUtils::CharRangeBit(uint32_t ch) {
    const uint32_t n = sizeof(gUnicodeRanges) / sizeof(struct UnicodeRangeTableEntry);

    for (uint32_t i = 0; i < n; ++i)
        if (ch >= gUnicodeRanges[i].start && ch <= gUnicodeRanges[i].end)
            return gUnicodeRanges[i].bit;

    return NO_RANGE_FOUND;
}

void gfxFontUtils::GetPrefsFontList(const char *aPrefName, nsTArray<nsString>& aFontList)
{
    const PRUnichar kComma = PRUnichar(',');
    
    aFontList.Clear();
    
    // get the list of single-face font families
    nsAdoptingString fontlistValue = Preferences::GetString(aPrefName);
    if (!fontlistValue) {
        return;
    }

    // append each font name to the list
    nsAutoString fontname;
    const PRUnichar *p, *p_end;
    fontlistValue.BeginReading(p);
    fontlistValue.EndReading(p_end);

     while (p < p_end) {
        const PRUnichar *nameStart = p;
        while (++p != p_end && *p != kComma)
        /* nothing */ ;

        // pull out a single name and clean out leading/trailing whitespace        
        fontname = Substring(nameStart, p);
        fontname.CompressWhitespace(true, true);
        
        // append it to the list
        aFontList.AppendElement(fontname);
        ++p;
    }

}

// produce a unique font name that is (1) a valid Postscript name and (2) less
// than 31 characters in length.  Using AddFontMemResourceEx on Windows fails 
// for names longer than 30 characters in length.

#define MAX_B64_LEN 32

nsresult gfxFontUtils::MakeUniqueUserFontName(nsAString& aName)
{
    nsCOMPtr<nsIUUIDGenerator> uuidgen =
      do_GetService("@mozilla.org/uuid-generator;1");
    NS_ENSURE_TRUE(uuidgen, NS_ERROR_OUT_OF_MEMORY);

    nsID guid;

    NS_ASSERTION(sizeof(guid) * 2 <= MAX_B64_LEN, "size of nsID has changed!");

    nsresult rv = uuidgen->GenerateUUIDInPlace(&guid);
    NS_ENSURE_SUCCESS(rv, rv);

    char guidB64[MAX_B64_LEN] = {0};

    if (!PL_Base64Encode(reinterpret_cast<char*>(&guid), sizeof(guid), guidB64))
        return NS_ERROR_FAILURE;

    // all b64 characters except for '/' are allowed in Postscript names, so convert / ==> -
    char *p;
    for (p = guidB64; *p; p++) {
        if (*p == '/')
            *p = '-';
    }

    aName.Assign(NS_LITERAL_STRING("uf"));
    aName.AppendASCII(guidB64);
    return NS_OK;
}


// TrueType/OpenType table handling code

// need byte aligned structs
#pragma pack(1)

// name table stores set of name record structures, followed by
// large block containing all the strings.  name record offset and length
// indicates the offset and length within that block.
// http://www.microsoft.com/typography/otspec/name.htm
struct NameRecordData {
    uint32_t  offset;
    uint32_t  length;
};

#pragma pack()

static bool
IsValidSFNTVersion(uint32_t version)
{
    // normally 0x00010000, CFF-style OT fonts == 'OTTO' and Apple TT fonts = 'true'
    // 'typ1' is also possible for old Type 1 fonts in a SFNT container but not supported
    return version == 0x10000 ||
           version == TRUETYPE_TAG('O','T','T','O') ||
           version == TRUETYPE_TAG('t','r','u','e');
}

// copy and swap UTF-16 values, assume no surrogate pairs, can be in place
static void
CopySwapUTF16(const uint16_t *aInBuf, uint16_t *aOutBuf, uint32_t aLen)
{
    const uint16_t *end = aInBuf + aLen;
    while (aInBuf < end) {
        uint16_t value = *aInBuf;
        *aOutBuf = (value >> 8) | (value & 0xff) << 8;
        aOutBuf++;
        aInBuf++;
    }
}

static bool
ValidateKernTable(const uint8_t *aKernTable, uint32_t aKernLength)
{
    // -- kern table can cause crashes if invalid, so do some basic sanity-checking
    const KernTableVersion0 *kernTable0 = reinterpret_cast<const KernTableVersion0*>(aKernTable);
    if (aKernLength < sizeof(KernTableVersion0)) {
        return false;
    }
    if (uint16_t(kernTable0->version) == 0) {
        if (aKernLength < sizeof(KernTableVersion0) +
                            uint16_t(kernTable0->nTables) * sizeof(KernTableSubtableHeaderVersion0)) {
            return false;
        }
        // at least the table is big enough to contain the subtable headers;
        // we could go further and check the actual subtable sizes....
        // for now, assume this is OK
        return true;
    }

    const KernTableVersion1 *kernTable1 = reinterpret_cast<const KernTableVersion1*>(aKernTable);
    if (aKernLength < sizeof(KernTableVersion1)) {
        return false;
    }
    if (kernTable1->version == 0x00010000) {
        if (aKernLength < sizeof(KernTableVersion1) +
                            kernTable1->nTables * sizeof(KernTableSubtableHeaderVersion1)) {
            return false;
        }
        // at least the table is big enough to contain the subtable headers;
        // we could go further and check the actual subtable sizes....
        // for now, assume this is OK
        return true;
    }

    // neither the old Windows version nor the newer Apple one; refuse to use it
    return false;
}

static bool
ValidateLocaTable(const uint8_t* aLocaTable, uint32_t aLocaLen,
                  uint32_t aGlyfLen, int16_t aLocaFormat, uint16_t aNumGlyphs)
{
    if (aLocaFormat == 0) {
        if (aLocaLen < uint32_t(aNumGlyphs + 1) * sizeof(uint16_t)) {
            return false;
        }
        const AutoSwap_PRUint16 *p =
            reinterpret_cast<const AutoSwap_PRUint16*>(aLocaTable);
        uint32_t prev = 0;
        for (uint32_t i = 0; i <= aNumGlyphs; ++i) {
            uint32_t current = uint16_t(*p++) * 2;
            if (current < prev || current > aGlyfLen) {
                return false;
            }
            prev = current;
        }
        return true;
    }
    if (aLocaFormat == 1) {
        if (aLocaLen < (aNumGlyphs + 1) * sizeof(uint32_t)) {
            return false;
        }
        const AutoSwap_PRUint32 *p =
            reinterpret_cast<const AutoSwap_PRUint32*>(aLocaTable);
        uint32_t prev = 0;
        for (uint32_t i = 0; i <= aNumGlyphs; ++i) {
            uint32_t current = *p++;
            if (current < prev || current > aGlyfLen) {
                return false;
            }
            prev = current;
        }
        return true;
    }
    return false;
}

gfxUserFontType
gfxFontUtils::DetermineFontDataType(const uint8_t *aFontData, uint32_t aFontDataLength)
{
    // test for OpenType font data
    // problem: EOT-Lite with 0x10000 length will look like TrueType!
    if (aFontDataLength >= sizeof(SFNTHeader)) {
        const SFNTHeader *sfntHeader = reinterpret_cast<const SFNTHeader*>(aFontData);
        uint32_t sfntVersion = sfntHeader->sfntVersion;
        if (IsValidSFNTVersion(sfntVersion)) {
            return GFX_USERFONT_OPENTYPE;
        }
    }
    
    // test for WOFF
    if (aFontDataLength >= sizeof(AutoSwap_PRUint32)) {
        const AutoSwap_PRUint32 *version = 
            reinterpret_cast<const AutoSwap_PRUint32*>(aFontData);
        if (uint32_t(*version) == TRUETYPE_TAG('w','O','F','F')) {
            return GFX_USERFONT_WOFF;
        }
    }
    
    // tests for other formats here
    
    return GFX_USERFONT_UNKNOWN;
}

bool
gfxFontUtils::ValidateSFNTHeaders(const uint8_t *aFontData, 
                                  uint32_t aFontDataLength)
{
    NS_ASSERTION(aFontData, "null font data");

    uint64_t dataLength(aFontDataLength);
    
    // read in the sfnt header
    if (sizeof(SFNTHeader) > aFontDataLength) {
        NS_WARNING("invalid font (insufficient data)");
        return false;
    }
    
    const SFNTHeader *sfntHeader = reinterpret_cast<const SFNTHeader*>(aFontData);
    uint32_t sfntVersion = sfntHeader->sfntVersion;
    if (!IsValidSFNTVersion(sfntVersion)) {
        NS_WARNING("invalid font (SFNT version)");
        return false;
    }
    
    // iterate through the table headers to find the head, name and OS/2 tables
#ifdef XP_WIN
    bool foundOS2 = false;
#endif
    bool foundHead = false, foundName = false;
    bool foundGlyphs = false, foundCFF = false, foundKern = false;
    bool foundLoca = false, foundMaxp = false;
    uint32_t headOffset = 0, headLen, nameOffset = 0, kernOffset = 0,
        kernLen = 0, glyfLen = 0, locaOffset = 0, locaLen = 0,
        maxpOffset = 0, maxpLen;
    uint32_t i, numTables;

    numTables = sfntHeader->numTables;
    uint32_t headerLen = sizeof(SFNTHeader) + sizeof(TableDirEntry) * numTables;
    if (headerLen > aFontDataLength) {
        NS_WARNING("invalid font (table directory)");
        return false;
    }
    
    // table directory entries begin immediately following SFNT header
    const TableDirEntry *dirEntry = 
        reinterpret_cast<const TableDirEntry*>(aFontData + sizeof(SFNTHeader));
    uint32_t checksum = 0;
    
    // checksum for font = (checksum of header) + (checksum of tables)
    const AutoSwap_PRUint32 *headerData = 
        reinterpret_cast<const AutoSwap_PRUint32*>(aFontData);

    // header length is in bytes, checksum calculated in longwords
    for (i = 0; i < (headerLen >> 2); i++, headerData++) {
        checksum += *headerData;
    }
    
    for (i = 0; i < numTables; i++, dirEntry++) {
    
        // sanity check on offset, length values
        if (uint64_t(dirEntry->offset) + uint64_t(dirEntry->length) > dataLength) {
            NS_WARNING("invalid font (table directory entry)");
            return false;
        }

        checksum += dirEntry->checkSum;
        
        switch (dirEntry->tag) {

        case TRUETYPE_TAG('h','e','a','d'):
            foundHead = true;
            headOffset = dirEntry->offset;
            headLen = dirEntry->length;
            if (headLen < sizeof(HeadTable)) {
                NS_WARNING("invalid font (head table length)");
                return false;
            }
            break;

        case TRUETYPE_TAG('k','e','r','n'):
            foundKern = true;
            kernOffset = dirEntry->offset;
            kernLen = dirEntry->length;
            break;

        case TRUETYPE_TAG('n','a','m','e'):
            foundName = true;
            nameOffset = dirEntry->offset;
            break;

        case TRUETYPE_TAG('O','S','/','2'):
#ifdef XP_WIN
            foundOS2 = true;
#endif
            break;

        case TRUETYPE_TAG('g','l','y','f'):  // TrueType-style quadratic glyph table
            foundGlyphs = true;
            glyfLen = dirEntry->length;
            break;

        case TRUETYPE_TAG('l','o','c','a'):  // glyph location table
            foundLoca = true;
            locaOffset = dirEntry->offset;
            locaLen = dirEntry->length;
            break;

        case TRUETYPE_TAG('m','a','x','p'):  // max profile
            foundMaxp = true;
            maxpOffset = dirEntry->offset;
            maxpLen = dirEntry->length;
            if (maxpLen < sizeof(MaxpTableHeader)) {
                NS_WARNING("invalid font (maxp table length)");
                return false;
            }
            break;

        case TRUETYPE_TAG('C','F','F',' '):  // PS-style cubic glyph table
            foundCFF = true;
            break;

        default:
            break;
        }

    }

    // simple sanity checks
    
    // -- fonts need head, name, maxp tables
    if (!foundHead || !foundName || !foundMaxp) {
        NS_WARNING("invalid font (missing head/name/maxp table)");
        return false;
    }
    
    // -- on Windows need OS/2 table
#ifdef XP_WIN
    if (!foundOS2) {
        NS_WARNING("invalid font (missing OS/2 table)");
        return false;
    }
#endif

    // -- head table data
    const HeadTable *headData = reinterpret_cast<const HeadTable*>(aFontData + headOffset);

    if (headData->tableVersionNumber != HeadTable::HEAD_VERSION) {
        NS_WARNING("invalid font (head table version)");
        return false;
    }

    if (headData->magicNumber != HeadTable::HEAD_MAGIC_NUMBER) {
        NS_WARNING("invalid font (head magic number)");
        return false;
    }

    if (headData->checkSumAdjustment != (HeadTable::HEAD_CHECKSUM_CALC_CONST - checksum)) {
        NS_WARNING("invalid font (bad checksum)");
        // Bug 483459 - warn about a bad checksum but allow the font to be 
        // used, since a small percentage of fonts don't calculate this 
        // correctly and font systems aren't fussy about this
        // return false;
    }
    
    // need glyf or CFF table based on sfnt version
    if (sfntVersion == TRUETYPE_TAG('O','T','T','O')) {
        if (!foundCFF) {
            NS_WARNING("invalid font (missing CFF table)");
            return false;
        }
    } else {
        if (!foundGlyphs || !foundLoca) {
            NS_WARNING("invalid font (missing glyf or loca table)");
            return false;
        }

        // sanity-check 'loca' offsets
        const MaxpTableHeader *maxpData =
            reinterpret_cast<const MaxpTableHeader*>(aFontData + maxpOffset);
        if (!ValidateLocaTable(aFontData + locaOffset, locaLen, glyfLen,
                               headData->indexToLocFormat,
                               maxpData->numGlyphs)) {
            NS_WARNING("invalid font (loca table offsets)");
            return false;
        }
    }
    
    // -- name table data
    const NameHeader *nameHeader = reinterpret_cast<const NameHeader*>(aFontData + nameOffset);

    uint32_t nameCount = nameHeader->count;

    // -- sanity check the number of name records
    if (uint64_t(nameCount) * sizeof(NameRecord) + uint64_t(nameOffset) > dataLength) {
        NS_WARNING("invalid font (name records)");
        return false;
    }
    
    // -- iterate through name records
    const NameRecord *nameRecord = reinterpret_cast<const NameRecord*>
                                       (aFontData + nameOffset + sizeof(NameHeader));
    uint64_t nameStringsBase = uint64_t(nameOffset) + uint64_t(nameHeader->stringOffset);

    for (i = 0; i < nameCount; i++, nameRecord++) {
        uint32_t namelen = nameRecord->length;
        uint32_t nameoff = nameRecord->offset;  // offset from base of string storage

        if (nameStringsBase + uint64_t(nameoff) + uint64_t(namelen) > dataLength) {
            NS_WARNING("invalid font (name table strings)");
            return false;
        }
    }

    // -- sanity-check the kern table, if present (see bug 487549)
    if (foundKern) {
        if (!ValidateKernTable(aFontData + kernOffset, kernLen)) {
            NS_WARNING("invalid font (kern table)");
            return false;
        }
    }

    // everything seems consistent
    return true;
}

nsresult
gfxFontUtils::RenameFont(const nsAString& aName, const uint8_t *aFontData, 
                         uint32_t aFontDataLength, FallibleTArray<uint8_t> *aNewFont)
{
    NS_ASSERTION(aNewFont, "null font data array");
    
    uint64_t dataLength(aFontDataLength);

    // new name table
    static const uint32_t neededNameIDs[] = {NAME_ID_FAMILY, 
                                             NAME_ID_STYLE,
                                             NAME_ID_UNIQUE,
                                             NAME_ID_FULL,
                                             NAME_ID_POSTSCRIPT};

    // calculate new name table size
    uint16_t nameCount = ArrayLength(neededNameIDs);

    // leave room for null-terminator
    uint16_t nameStrLength = (aName.Length() + 1) * sizeof(PRUnichar); 

    // round name table size up to 4-byte multiple
    uint32_t nameTableSize = (sizeof(NameHeader) +
                              sizeof(NameRecord) * nameCount +
                              nameStrLength +
                              3) & ~3;
                              
    if (dataLength + nameTableSize > UINT32_MAX)
        return NS_ERROR_FAILURE;
        
    // bug 505386 - need to handle unpadded font length
    uint32_t paddedFontDataSize = (aFontDataLength + 3) & ~3;
    uint32_t adjFontDataSize = paddedFontDataSize + nameTableSize;

    // create new buffer: old font data plus new name table
    if (!aNewFont->AppendElements(adjFontDataSize))
        return NS_ERROR_OUT_OF_MEMORY;

    // copy the old font data
    uint8_t *newFontData = reinterpret_cast<uint8_t*>(aNewFont->Elements());
    
    // null the last four bytes in case the font length is not a multiple of 4
    memset(newFontData + aFontDataLength, 0, paddedFontDataSize - aFontDataLength);

    // copy font data
    memcpy(newFontData, aFontData, aFontDataLength);
    
    // null out the last 4 bytes for checksum calculations
    memset(newFontData + adjFontDataSize - 4, 0, 4);
    
    NameHeader *nameHeader = reinterpret_cast<NameHeader*>(newFontData +
                                                            paddedFontDataSize);
    
    // -- name header
    nameHeader->format = 0;
    nameHeader->count = nameCount;
    nameHeader->stringOffset = sizeof(NameHeader) + nameCount * sizeof(NameRecord);
    
    // -- name records
    uint32_t i;
    NameRecord *nameRecord = reinterpret_cast<NameRecord*>(nameHeader + 1);
    
    for (i = 0; i < nameCount; i++, nameRecord++) {
        nameRecord->platformID = PLATFORM_ID_MICROSOFT;
        nameRecord->encodingID = ENCODING_ID_MICROSOFT_UNICODEBMP;
        nameRecord->languageID = LANG_ID_MICROSOFT_EN_US;
        nameRecord->nameID = neededNameIDs[i];
        nameRecord->offset = 0;
        nameRecord->length = nameStrLength;
    }
    
    // -- string data, located after the name records, stored in big-endian form
    PRUnichar *strData = reinterpret_cast<PRUnichar*>(nameRecord);

    const PRUnichar *nameStr = aName.BeginReading();
    const PRUnichar *nameStrEnd = aName.EndReading();
    while (nameStr < nameStrEnd) {
        PRUnichar ch = *nameStr++;
        *strData++ = NS_SWAP16(ch);
    }
    *strData = 0; // add null termination
    
    // adjust name table header to point to the new name table
    SFNTHeader *sfntHeader = reinterpret_cast<SFNTHeader*>(newFontData);

    // table directory entries begin immediately following SFNT header
    TableDirEntry *dirEntry = 
        reinterpret_cast<TableDirEntry*>(newFontData + sizeof(SFNTHeader));

    uint32_t numTables = sfntHeader->numTables;
    
    for (i = 0; i < numTables; i++, dirEntry++) {
        if (dirEntry->tag == TRUETYPE_TAG('n','a','m','e')) {
            break;
        }
    }
    
    // function only called if font validates, so this should always be true
    NS_ASSERTION(i < numTables, "attempt to rename font with no name table");

    // note: dirEntry now points to name record
    
    // recalculate name table checksum
    uint32_t checkSum = 0;
    AutoSwap_PRUint32 *nameData = reinterpret_cast<AutoSwap_PRUint32*> (nameHeader);
    AutoSwap_PRUint32 *nameDataEnd = nameData + (nameTableSize >> 2);
    
    while (nameData < nameDataEnd)
        checkSum = checkSum + *nameData++;
    
    // adjust name table entry to point to new name table
    dirEntry->offset = paddedFontDataSize;
    dirEntry->length = nameTableSize;
    dirEntry->checkSum = checkSum;
    
    // fix up checksums
    uint32_t checksum = 0;
    
    // checksum for font = (checksum of header) + (checksum of tables)
    uint32_t headerLen = sizeof(SFNTHeader) + sizeof(TableDirEntry) * numTables;
    const AutoSwap_PRUint32 *headerData = 
        reinterpret_cast<const AutoSwap_PRUint32*>(newFontData);

    // header length is in bytes, checksum calculated in longwords
    for (i = 0; i < (headerLen >> 2); i++, headerData++) {
        checksum += *headerData;
    }
    
    uint32_t headOffset = 0;
    dirEntry = reinterpret_cast<TableDirEntry*>(newFontData + sizeof(SFNTHeader));

    for (i = 0; i < numTables; i++, dirEntry++) {
        if (dirEntry->tag == TRUETYPE_TAG('h','e','a','d')) {
            headOffset = dirEntry->offset;
        }
        checksum += dirEntry->checkSum;
    }
    
    NS_ASSERTION(headOffset != 0, "no head table for font");
    
    HeadTable *headData = reinterpret_cast<HeadTable*>(newFontData + headOffset);

    headData->checkSumAdjustment = HeadTable::HEAD_CHECKSUM_CALC_CONST - checksum;

    return NS_OK;
}

// This is only called after the basic validity of the downloaded sfnt
// data has been checked, so it should never fail to find the name table
// (though it might fail to read it, if memory isn't available);
// other checks here are just for extra paranoia.
nsresult
gfxFontUtils::GetFullNameFromSFNT(const uint8_t* aFontData, uint32_t aLength,
                                  nsAString& aFullName)
{
    aFullName.AssignLiteral("(MISSING NAME)"); // should always get replaced

    NS_ENSURE_TRUE(aLength >= sizeof(SFNTHeader), NS_ERROR_UNEXPECTED);
    const SFNTHeader *sfntHeader =
        reinterpret_cast<const SFNTHeader*>(aFontData);
    const TableDirEntry *dirEntry =
        reinterpret_cast<const TableDirEntry*>(aFontData + sizeof(SFNTHeader));
    uint32_t numTables = sfntHeader->numTables;
    NS_ENSURE_TRUE(aLength >=
                   sizeof(SFNTHeader) + numTables * sizeof(TableDirEntry),
                   NS_ERROR_UNEXPECTED);
    bool foundName = false;
    for (uint32_t i = 0; i < numTables; i++, dirEntry++) {
        if (dirEntry->tag == TRUETYPE_TAG('n','a','m','e')) {
            foundName = true;
            break;
        }
    }
    
    // should never fail, as we're only called after font validation succeeded
    NS_ENSURE_TRUE(foundName, NS_ERROR_NOT_AVAILABLE);

    uint32_t len = dirEntry->length;
    NS_ENSURE_TRUE(aLength > len && aLength - len >= dirEntry->offset,
                   NS_ERROR_UNEXPECTED);
    FallibleTArray<uint8_t> nameTable;
    if (!nameTable.SetLength(len)) {
        return NS_ERROR_OUT_OF_MEMORY;
    }
    memcpy(nameTable.Elements(), aFontData + dirEntry->offset, len);

    return GetFullNameFromTable(nameTable, aFullName);
}

nsresult
gfxFontUtils::GetFullNameFromTable(FallibleTArray<uint8_t>& aNameTable,
                                   nsAString& aFullName)
{
    nsAutoString name;
    nsresult rv =
        gfxFontUtils::ReadCanonicalName(aNameTable,
                                        gfxFontUtils::NAME_ID_FULL,
                                        name);
    if (NS_SUCCEEDED(rv) && !name.IsEmpty()) {
        aFullName = name;
        return NS_OK;
    }
    rv = gfxFontUtils::ReadCanonicalName(aNameTable,
                                         gfxFontUtils::NAME_ID_FAMILY,
                                         name);
    if (NS_SUCCEEDED(rv) && !name.IsEmpty()) {
        nsAutoString styleName;
        rv = gfxFontUtils::ReadCanonicalName(aNameTable,
                                             gfxFontUtils::NAME_ID_STYLE,
                                             styleName);
        if (NS_SUCCEEDED(rv) && !styleName.IsEmpty()) {
            name.AppendLiteral(" ");
            name.Append(styleName);
            aFullName = name;
        }
        return NS_OK;
    }

    return NS_ERROR_NOT_AVAILABLE;
}

nsresult
gfxFontUtils::GetFamilyNameFromTable(FallibleTArray<uint8_t>& aNameTable,
                                     nsAString& aFullName)
{
    nsAutoString name;
    nsresult rv =
        gfxFontUtils::ReadCanonicalName(aNameTable,
                                        gfxFontUtils::NAME_ID_FAMILY,
                                        name);
    if (NS_SUCCEEDED(rv) && !name.IsEmpty()) {
        aFullName = name;
        return NS_OK;
    }
    return NS_ERROR_NOT_AVAILABLE;
}

enum {
#if defined(XP_MACOSX)
    CANONICAL_LANG_ID = gfxFontUtils::LANG_ID_MAC_ENGLISH,
    PLATFORM_ID       = gfxFontUtils::PLATFORM_ID_MAC
#else
    CANONICAL_LANG_ID = gfxFontUtils::LANG_ID_MICROSOFT_EN_US,
    PLATFORM_ID       = gfxFontUtils::PLATFORM_ID_MICROSOFT
#endif
};    

nsresult
gfxFontUtils::ReadNames(FallibleTArray<uint8_t>& aNameTable, uint32_t aNameID, 
                        int32_t aPlatformID, nsTArray<nsString>& aNames)
{
    return ReadNames(aNameTable, aNameID, LANG_ALL, aPlatformID, aNames);
}

nsresult
gfxFontUtils::ReadCanonicalName(FallibleTArray<uint8_t>& aNameTable, uint32_t aNameID, 
                                nsString& aName)
{
    nsresult rv;
    
    nsTArray<nsString> names;
    
    // first, look for the English name (this will succeed 99% of the time)
    rv = ReadNames(aNameTable, aNameID, CANONICAL_LANG_ID, PLATFORM_ID, names);
    NS_ENSURE_SUCCESS(rv, rv);
        
    // otherwise, grab names for all languages
    if (names.Length() == 0) {
        rv = ReadNames(aNameTable, aNameID, LANG_ALL, PLATFORM_ID, names);
        NS_ENSURE_SUCCESS(rv, rv);
    }
    
#if defined(XP_MACOSX)
    // may be dealing with font that only has Microsoft name entries
    if (names.Length() == 0) {
        rv = ReadNames(aNameTable, aNameID, LANG_ID_MICROSOFT_EN_US, 
                       PLATFORM_ID_MICROSOFT, names);
        NS_ENSURE_SUCCESS(rv, rv);
        
        // getting really desperate now, take anything!
        if (names.Length() == 0) {
            rv = ReadNames(aNameTable, aNameID, LANG_ALL, 
                           PLATFORM_ID_MICROSOFT, names);
            NS_ENSURE_SUCCESS(rv, rv);
        }
    }
#endif

    // return the first name (99.9% of the time names will
    // contain a single English name)
    if (names.Length()) {
        aName.Assign(names[0]);
        return NS_OK;
    }
        
    return NS_ERROR_FAILURE;
}

// Charsets to use for decoding Mac platform font names.
// This table is sorted by {encoding, language}, with the wildcard "ANY" being
// greater than any defined values for each field; we use a binary search on both
// fields, and fall back to matching only encoding if necessary

// Some "redundant" entries for specific combinations are included such as
// encoding=roman, lang=english, in order that common entries will be found
// on the first search.

#define ANY 0xffff
const gfxFontUtils::MacFontNameCharsetMapping gfxFontUtils::gMacFontNameCharsets[] =
{
    { ENCODING_ID_MAC_ROMAN,        LANG_ID_MAC_ENGLISH,      "macintosh"       },
    { ENCODING_ID_MAC_ROMAN,        LANG_ID_MAC_ICELANDIC,    "x-mac-icelandic" },
    { ENCODING_ID_MAC_ROMAN,        LANG_ID_MAC_TURKISH,      "x-mac-turkish"   },
    { ENCODING_ID_MAC_ROMAN,        LANG_ID_MAC_POLISH,       "x-mac-ce"        },
    { ENCODING_ID_MAC_ROMAN,        LANG_ID_MAC_ROMANIAN,     "x-mac-romanian"  },
    { ENCODING_ID_MAC_ROMAN,        LANG_ID_MAC_CZECH,        "x-mac-ce"        },
    { ENCODING_ID_MAC_ROMAN,        LANG_ID_MAC_SLOVAK,       "x-mac-ce"        },
    { ENCODING_ID_MAC_ROMAN,        ANY,                      "macintosh"       },
    { ENCODING_ID_MAC_JAPANESE,     LANG_ID_MAC_JAPANESE,     "Shift_JIS"       },
    { ENCODING_ID_MAC_JAPANESE,     ANY,                      "Shift_JIS"       },
    { ENCODING_ID_MAC_TRAD_CHINESE, LANG_ID_MAC_TRAD_CHINESE, "Big5"            },
    { ENCODING_ID_MAC_TRAD_CHINESE, ANY,                      "Big5"            },
    { ENCODING_ID_MAC_KOREAN,       LANG_ID_MAC_KOREAN,       "EUC-KR"          },
    { ENCODING_ID_MAC_KOREAN,       ANY,                      "EUC-KR"          },
    { ENCODING_ID_MAC_ARABIC,       LANG_ID_MAC_ARABIC,       "x-mac-arabic"    },
    { ENCODING_ID_MAC_ARABIC,       LANG_ID_MAC_URDU,         "x-mac-farsi"     },
    { ENCODING_ID_MAC_ARABIC,       LANG_ID_MAC_FARSI,        "x-mac-farsi"     },
    { ENCODING_ID_MAC_ARABIC,       ANY,                      "x-mac-arabic"    },
    { ENCODING_ID_MAC_HEBREW,       LANG_ID_MAC_HEBREW,       "x-mac-hebrew"    },
    { ENCODING_ID_MAC_HEBREW,       ANY,                      "x-mac-hebrew"    },
    { ENCODING_ID_MAC_GREEK,        ANY,                      "x-mac-greek"     },
    { ENCODING_ID_MAC_CYRILLIC,     ANY,                      "x-mac-cyrillic"  },
    { ENCODING_ID_MAC_DEVANAGARI,   ANY,                      "x-mac-devanagari"},
    { ENCODING_ID_MAC_GURMUKHI,     ANY,                      "x-mac-gurmukhi"  },
    { ENCODING_ID_MAC_GUJARATI,     ANY,                      "x-mac-gujarati"  },
    { ENCODING_ID_MAC_SIMP_CHINESE, LANG_ID_MAC_SIMP_CHINESE, "GB2312"          },
    { ENCODING_ID_MAC_SIMP_CHINESE, ANY,                      "GB2312"          }
};

const char* gfxFontUtils::gISOFontNameCharsets[] = 
{
    /* 0 */ "us-ascii"   ,
    /* 1 */ nullptr       , /* spec says "ISO 10646" but does not specify encoding form! */
    /* 2 */ "ISO-8859-1"
};

const char* gfxFontUtils::gMSFontNameCharsets[] =
{
    /* [0] ENCODING_ID_MICROSOFT_SYMBOL */      ""          ,
    /* [1] ENCODING_ID_MICROSOFT_UNICODEBMP */  ""          ,
    /* [2] ENCODING_ID_MICROSOFT_SHIFTJIS */    "Shift_JIS" ,
    /* [3] ENCODING_ID_MICROSOFT_PRC */         nullptr      ,
    /* [4] ENCODING_ID_MICROSOFT_BIG5 */        "Big5"      ,
    /* [5] ENCODING_ID_MICROSOFT_WANSUNG */     nullptr      ,
    /* [6] ENCODING_ID_MICROSOFT_JOHAB */       "x-johab"   ,
    /* [7] reserved */                          nullptr      ,
    /* [8] reserved */                          nullptr      ,
    /* [9] reserved */                          nullptr      ,
    /*[10] ENCODING_ID_MICROSOFT_UNICODEFULL */ ""
};

#define ARRAY_SIZE(A) (sizeof(A) / sizeof(A[0]))

// Return the name of the charset we should use to decode a font name
// given the name table attributes.
// Special return values:
//    ""       charset is UTF16BE, no need for a converter
//    nullptr   unknown charset, do not attempt conversion
const char*
gfxFontUtils::GetCharsetForFontName(uint16_t aPlatform, uint16_t aScript, uint16_t aLanguage)
{
    switch (aPlatform)
    {
    case PLATFORM_ID_UNICODE:
        return "";

    case PLATFORM_ID_MAC:
        {
            uint32_t lo = 0, hi = ARRAY_SIZE(gMacFontNameCharsets);
            MacFontNameCharsetMapping searchValue = { aScript, aLanguage, nullptr };
            for (uint32_t i = 0; i < 2; ++i) {
                // binary search; if not found, set language to ANY and try again
                while (lo < hi) {
                    uint32_t mid = (lo + hi) / 2;
                    const MacFontNameCharsetMapping& entry = gMacFontNameCharsets[mid];
                    if (entry < searchValue) {
                        lo = mid + 1;
                        continue;
                    }
                    if (searchValue < entry) {
                        hi = mid;
                        continue;
                    }
                    // found a match
                    return entry.mCharsetName;
                }

                // no match, so reset high bound for search and re-try
                hi = ARRAY_SIZE(gMacFontNameCharsets);
                searchValue.mLanguage = ANY;
            }
        }
        break;

    case PLATFORM_ID_ISO:
        if (aScript < ARRAY_SIZE(gISOFontNameCharsets)) {
            return gISOFontNameCharsets[aScript];
        }
        break;

    case PLATFORM_ID_MICROSOFT:
        if (aScript < ARRAY_SIZE(gMSFontNameCharsets)) {
            return gMSFontNameCharsets[aScript];
        }
        break;
    }

    return nullptr;
}

// convert a raw name from the name table to an nsString, if possible;
// return value indicates whether conversion succeeded
bool
gfxFontUtils::DecodeFontName(const uint8_t *aNameData, int32_t aByteLen, 
                             uint32_t aPlatformCode, uint32_t aScriptCode,
                             uint32_t aLangCode, nsAString& aName)
{
    NS_ASSERTION(aByteLen > 0, "bad length for font name data");

    const char *csName = GetCharsetForFontName(aPlatformCode, aScriptCode, aLangCode);

    if (!csName) {
        // nullptr -> unknown charset
#ifdef DEBUG
        char warnBuf[128];
        if (aByteLen > 64)
            aByteLen = 64;
        sprintf(warnBuf, "skipping font name, unknown charset %d:%d:%d for <%.*s>",
                aPlatformCode, aScriptCode, aLangCode, aByteLen, aNameData);
        NS_WARNING(warnBuf);
#endif
        return false;
    }

    if (csName[0] == 0) {
        // empty charset name: data is utf16be, no need to instantiate a converter
        uint32_t strLen = aByteLen / 2;
#ifdef IS_LITTLE_ENDIAN
        aName.SetLength(strLen);
        CopySwapUTF16(reinterpret_cast<const uint16_t*>(aNameData),
                      reinterpret_cast<uint16_t*>(aName.BeginWriting()), strLen);
#else
        aName.Assign(reinterpret_cast<const PRUnichar*>(aNameData), strLen);
#endif    
        return true;
    }

    nsresult rv;
    nsCOMPtr<nsICharsetConverterManager> ccm =
        do_GetService(NS_CHARSETCONVERTERMANAGER_CONTRACTID, &rv);
    NS_ASSERTION(NS_SUCCEEDED(rv), "failed to get charset converter manager");
    if (NS_FAILED(rv)) {
        return false;
    }

    nsCOMPtr<nsIUnicodeDecoder> decoder;
    rv = ccm->GetUnicodeDecoderRaw(csName, getter_AddRefs(decoder));
    if (NS_FAILED(rv)) {
        NS_WARNING("failed to get the decoder for a font name string");
        return false;
    }

    int32_t destLength;
    rv = decoder->GetMaxLength(reinterpret_cast<const char*>(aNameData), aByteLen, &destLength);
    if (NS_FAILED(rv)) {
        NS_WARNING("decoder->GetMaxLength failed, invalid font name?");
        return false;
    }

    // make space for the converted string
    aName.SetLength(destLength);
    rv = decoder->Convert(reinterpret_cast<const char*>(aNameData), &aByteLen,
                          aName.BeginWriting(), &destLength);
    if (NS_FAILED(rv)) {
        NS_WARNING("decoder->Convert failed, invalid font name?");
        return false;
    }
    aName.Truncate(destLength); // set the actual length

    return true;
}

nsresult
gfxFontUtils::ReadNames(FallibleTArray<uint8_t>& aNameTable, uint32_t aNameID, 
                        int32_t aLangID, int32_t aPlatformID,
                        nsTArray<nsString>& aNames)
{
    uint32_t nameTableLen = aNameTable.Length();
    NS_ASSERTION(nameTableLen != 0, "null name table");

    if (nameTableLen == 0)
        return NS_ERROR_FAILURE;

    uint8_t *nameTable = aNameTable.Elements();

    // -- name table data
    const NameHeader *nameHeader = reinterpret_cast<const NameHeader*>(nameTable);

    uint32_t nameCount = nameHeader->count;

    // -- sanity check the number of name records
    if (uint64_t(nameCount) * sizeof(NameRecord) > nameTableLen) {
        NS_WARNING("invalid font (name table data)");
        return NS_ERROR_FAILURE;
    }
    
    // -- iterate through name records
    const NameRecord *nameRecord 
        = reinterpret_cast<const NameRecord*>(nameTable + sizeof(NameHeader));
    uint64_t nameStringsBase = uint64_t(nameHeader->stringOffset);

    uint32_t i;
    for (i = 0; i < nameCount; i++, nameRecord++) {
        uint32_t platformID;
        
        // skip over unwanted nameID's
        if (uint32_t(nameRecord->nameID) != aNameID)
            continue;

        // skip over unwanted platform data
        platformID = nameRecord->platformID;
        if (aPlatformID != PLATFORM_ALL 
            && uint32_t(nameRecord->platformID) != PLATFORM_ID)
            continue;
            
        // skip over unwanted languages
        if (aLangID != LANG_ALL 
              && uint32_t(nameRecord->languageID) != uint32_t(aLangID))
            continue;
        
        // add name to names array
        
        // -- calculate string location
        uint32_t namelen = nameRecord->length;
        uint32_t nameoff = nameRecord->offset;  // offset from base of string storage

        if (nameStringsBase + uint64_t(nameoff) + uint64_t(namelen) 
                > nameTableLen) {
            NS_WARNING("invalid font (name table strings)");
            return NS_ERROR_FAILURE;
        }
        
        // -- decode if necessary and make nsString
        nsAutoString name;
        
        DecodeFontName(nameTable + nameStringsBase + nameoff, namelen,
                       platformID, uint32_t(nameRecord->encodingID),
                       uint32_t(nameRecord->languageID), name);
            
        uint32_t k, numNames;
        bool foundName = false;
        
        numNames = aNames.Length();
        for (k = 0; k < numNames; k++) {
            if (name.Equals(aNames[k])) {
                foundName = true;
                break;
            }    
        }
        
        if (!foundName)
            aNames.AppendElement(name);                          

    }

    return NS_OK;
}

#ifdef XP_WIN

// Embedded OpenType (EOT) handling
// needed for dealing with downloadable fonts on Windows
//
// EOT version 0x00020001
// based on http://www.w3.org/Submission/2008/SUBM-EOT-20080305/
//
// EOT header consists of a fixed-size portion containing general font
// info, followed by a variable-sized portion containing name data,
// followed by the actual TT/OT font data (non-byte values are always
// stored in big-endian format)
//
// EOT header is stored in *little* endian order!!

#pragma pack(1)

struct EOTFixedHeader {

    uint32_t      eotSize;            // Total structure length in PRUint8s (including string and font data)
    uint32_t      fontDataSize;       // Length of the OpenType font (FontData) in PRUint8s
    uint32_t      version;            // Version number of this format - 0x00010000
    uint32_t      flags;              // Processing Flags
    uint8_t       panose[10];         // The PANOSE value for this font - See http://www.microsoft.com/typography/otspec/os2.htm#pan
    uint8_t       charset;            // In Windows this is derived from TEXTMETRIC.tmCharSet. This value specifies the character set of the font. DEFAULT_CHARSET (0x01) indicates no preference. - See http://msdn2.microsoft.com/en-us/library/ms534202.aspx
    uint8_t       italic;             // If the bit for ITALIC is set in OS/2.fsSelection, the value will be 0x01 - See http://www.microsoft.com/typography/otspec/os2.htm#fss
    uint32_t      weight;             // The weight value for this font - See http://www.microsoft.com/typography/otspec/os2.htm#wtc
    uint16_t      fsType;             // Type flags that provide information about embedding permissions - See http://www.microsoft.com/typography/otspec/os2.htm#fst
    uint16_t      magicNumber;        // Magic number for EOT file - 0x504C. Used to check for data corruption.
    uint32_t      unicodeRange1;      // OS/2.UnicodeRange1 (bits 0-31) - See http://www.microsoft.com/typography/otspec/os2.htm#ur
    uint32_t      unicodeRange2;      // OS/2.UnicodeRange2 (bits 32-63) - See http://www.microsoft.com/typography/otspec/os2.htm#ur
    uint32_t      unicodeRange3;      // OS/2.UnicodeRange3 (bits 64-95) - See http://www.microsoft.com/typography/otspec/os2.htm#ur
    uint32_t      unicodeRange4;      // OS/2.UnicodeRange4 (bits 96-127) - See http://www.microsoft.com/typography/otspec/os2.htm#ur
    uint32_t      codePageRange1;     // CodePageRange1 (bits 0-31) - See http://www.microsoft.com/typography/otspec/os2.htm#cpr
    uint32_t      codePageRange2;     // CodePageRange2 (bits 32-63) - See http://www.microsoft.com/typography/otspec/os2.htm#cpr
    uint32_t      checkSumAdjustment; // head.CheckSumAdjustment - See http://www.microsoft.com/typography/otspec/head.htm
    uint32_t      reserved[4];        // Reserved - must be 0
    uint16_t      padding1;           // Padding to maintain long alignment. Padding value must always be set to 0x0000.

    enum {
        EOT_VERSION = 0x00020001,
        EOT_MAGIC_NUMBER = 0x504c,
        EOT_DEFAULT_CHARSET = 0x01,
        EOT_EMBED_PRINT_PREVIEW = 0x0004,
        EOT_FAMILY_NAME_INDEX = 0,    // order of names in variable portion of EOT header
        EOT_STYLE_NAME_INDEX = 1,
        EOT_VERSION_NAME_INDEX = 2,
        EOT_FULL_NAME_INDEX = 3,
        EOT_NUM_NAMES = 4
    };

};

#pragma pack()

// EOT headers are only used on Windows

// EOT variable-sized header (version 0x00020001 - contains 4 name
// fields, each with the structure):
//
//   // number of bytes in the name array
//   uint16_t size;
//   // array of UTF-16 chars, total length = <size> bytes
//   // note: english version of name record string
//   uint8_t  name[size]; 
//
// This structure is used for the following names, each separated by two
// bytes of padding (always 0 with no padding after the rootString):
//
//   familyName  - based on name ID = 1
//   styleName   - based on name ID = 2
//   versionName - based on name ID = 5
//   fullName    - based on name ID = 4
//   rootString  - used to restrict font usage to a specific domain
//

#if DEBUG
static void 
DumpEOTHeader(uint8_t *aHeader, uint32_t aHeaderLen)
{
    uint32_t offset = 0;
    uint8_t *ch = aHeader;

    printf("\n\nlen == %d\n\n", aHeaderLen);
    while (offset < aHeaderLen) {
        printf("%7.7x    ", offset);
        int i;
        for (i = 0; i < 16; i++) {
            printf("%2.2x  ", *ch++);
        }
        printf("\n");
        offset += 16;
    }
}
#endif

nsresult
gfxFontUtils::MakeEOTHeader(const uint8_t *aFontData, uint32_t aFontDataLength,
                            FallibleTArray<uint8_t> *aHeader,
                            FontDataOverlay *aOverlay)
{
    NS_ASSERTION(aFontData && aFontDataLength != 0, "null font data");
    NS_ASSERTION(aHeader, "null header");
    NS_ASSERTION(aHeader->Length() == 0, "non-empty header passed in");
    NS_ASSERTION(aOverlay, "null font overlay struct passed in");

    aOverlay->overlaySrc = 0;
    
    if (!aHeader->AppendElements(sizeof(EOTFixedHeader)))
        return NS_ERROR_OUT_OF_MEMORY;

    EOTFixedHeader *eotHeader = reinterpret_cast<EOTFixedHeader*>(aHeader->Elements());
    memset(eotHeader, 0, sizeof(EOTFixedHeader));

    uint32_t fontDataSize = aFontDataLength;

    // set up header fields
    eotHeader->fontDataSize = fontDataSize;
    eotHeader->version = EOTFixedHeader::EOT_VERSION;
    eotHeader->flags = 0;  // don't specify any special processing
    eotHeader->charset = EOTFixedHeader::EOT_DEFAULT_CHARSET;
    eotHeader->fsType = EOTFixedHeader::EOT_EMBED_PRINT_PREVIEW;
    eotHeader->magicNumber = EOTFixedHeader::EOT_MAGIC_NUMBER;

    // read in the sfnt header
    if (sizeof(SFNTHeader) > aFontDataLength)
        return NS_ERROR_FAILURE;
    
    const SFNTHeader *sfntHeader = reinterpret_cast<const SFNTHeader*>(aFontData);
    if (!IsValidSFNTVersion(sfntHeader->sfntVersion))
        return NS_ERROR_FAILURE;

    // iterate through the table headers to find the head, name and OS/2 tables
    bool foundHead = false, foundOS2 = false, foundName = false, foundGlyphs = false;
    uint32_t headOffset, headLen, nameOffset, nameLen, os2Offset, os2Len;
    uint32_t i, numTables;

    numTables = sfntHeader->numTables;
    if (sizeof(SFNTHeader) + sizeof(TableDirEntry) * numTables > aFontDataLength)
        return NS_ERROR_FAILURE;
    
    uint64_t dataLength(aFontDataLength);
    
    // table directory entries begin immediately following SFNT header
    const TableDirEntry *dirEntry = reinterpret_cast<const TableDirEntry*>(aFontData + sizeof(SFNTHeader));
    
    for (i = 0; i < numTables; i++, dirEntry++) {
    
        // sanity check on offset, length values
        if (uint64_t(dirEntry->offset) + uint64_t(dirEntry->length) > dataLength)
            return NS_ERROR_FAILURE;

        switch (dirEntry->tag) {

        case TRUETYPE_TAG('h','e','a','d'):
            foundHead = true;
            headOffset = dirEntry->offset;
            headLen = dirEntry->length;
            if (headLen < sizeof(HeadTable))
                return NS_ERROR_FAILURE;
            break;

        case TRUETYPE_TAG('n','a','m','e'):
            foundName = true;
            nameOffset = dirEntry->offset;
            nameLen = dirEntry->length;
            break;

        case TRUETYPE_TAG('O','S','/','2'):
            foundOS2 = true;
            os2Offset = dirEntry->offset;
            os2Len = dirEntry->length;
            break;

        case TRUETYPE_TAG('g','l','y','f'):  // TrueType-style quadratic glyph table
            foundGlyphs = true;
            break;

        case TRUETYPE_TAG('C','F','F',' '):  // PS-style cubic glyph table
            foundGlyphs = true;
            break;

        default:
            break;
        }

        if (foundHead && foundName && foundOS2 && foundGlyphs)
            break;
    }

    // require these three tables on Windows
    if (!foundHead || !foundName || !foundOS2)
        return NS_ERROR_FAILURE;

    // at this point, all table offset/length values are within bounds
    
    // read in the data from those tables

    // -- head table data
    const HeadTable  *headData = reinterpret_cast<const HeadTable*>(aFontData + headOffset);

    if (headData->tableVersionNumber != HeadTable::HEAD_VERSION ||
        headData->magicNumber != HeadTable::HEAD_MAGIC_NUMBER) {
        return NS_ERROR_FAILURE;
    }

    eotHeader->checkSumAdjustment = headData->checkSumAdjustment;

    // -- name table data

    // -- first, read name table header
    const NameHeader *nameHeader = reinterpret_cast<const NameHeader*>(aFontData + nameOffset);
    uint32_t nameStringsBase = uint32_t(nameHeader->stringOffset);

    uint32_t nameCount = nameHeader->count;

    // -- sanity check the number of name records
    if (uint64_t(nameCount) * sizeof(NameRecord) + uint64_t(nameOffset) > dataLength)
        return NS_ERROR_FAILURE;

    // bug 496573 -- dummy names in case the font didn't contain English names
    const nsString dummyNames[EOTFixedHeader::EOT_NUM_NAMES] = {
        NS_LITERAL_STRING("Unknown"),
        NS_LITERAL_STRING("Regular"),
        EmptyString(),
        dummyNames[EOTFixedHeader::EOT_FAMILY_NAME_INDEX]
    };

    // -- iterate through name records, look for specific name ids with
    //    matching platform/encoding/etc. and store offset/lengths
    NameRecordData names[EOTFixedHeader::EOT_NUM_NAMES] = {0};
    const NameRecord *nameRecord = reinterpret_cast<const NameRecord*>(aFontData + nameOffset + sizeof(NameHeader));
    uint32_t needNames = (1 << EOTFixedHeader::EOT_FAMILY_NAME_INDEX) | 
                         (1 << EOTFixedHeader::EOT_STYLE_NAME_INDEX) | 
                         (1 << EOTFixedHeader::EOT_FULL_NAME_INDEX) | 
                         (1 << EOTFixedHeader::EOT_VERSION_NAME_INDEX);

    for (i = 0; i < nameCount; i++, nameRecord++) {

        // looking for Microsoft English US name strings, skip others
        if (uint32_t(nameRecord->platformID) != PLATFORM_ID_MICROSOFT || 
                uint32_t(nameRecord->encodingID) != ENCODING_ID_MICROSOFT_UNICODEBMP || 
                uint32_t(nameRecord->languageID) != LANG_ID_MICROSOFT_EN_US)
            continue;

        uint32_t index;
        switch ((uint32_t)nameRecord->nameID) {

        case NAME_ID_FAMILY:
            index = EOTFixedHeader::EOT_FAMILY_NAME_INDEX;
            break;

        case NAME_ID_STYLE:
            index = EOTFixedHeader::EOT_STYLE_NAME_INDEX;
            break;

        case NAME_ID_FULL:
            index = EOTFixedHeader::EOT_FULL_NAME_INDEX;
            break;

        case NAME_ID_VERSION:
            index = EOTFixedHeader::EOT_VERSION_NAME_INDEX;
            break;

        default:
            continue;
        }

        names[index].offset = nameRecord->offset;
        names[index].length = nameRecord->length;
        needNames &= ~(1 << index);

        if (needNames == 0)
            break;
    }

    // -- expand buffer if needed to include variable-length portion
    uint32_t eotVariableLength = 0;
    for (i = 0; i < EOTFixedHeader::EOT_NUM_NAMES; i++) {
        if (!(needNames & (1 << i))) {
            eotVariableLength += names[i].length & (~1);
        } else {
            eotVariableLength += dummyNames[i].Length() * sizeof(PRUnichar);
        }
    }
    eotVariableLength += EOTFixedHeader::EOT_NUM_NAMES * (2 /* size */ 
                                                          + 2 /* padding */) +
                         2 /* null root string size */;

    if (!aHeader->AppendElements(eotVariableLength))
        return NS_ERROR_OUT_OF_MEMORY;

    // append the string data to the end of the EOT header
    uint8_t *eotEnd = aHeader->Elements() + sizeof(EOTFixedHeader);
    uint32_t strOffset, strLen;

    for (i = 0; i < EOTFixedHeader::EOT_NUM_NAMES; i++) {
        if (!(needNames & (1 << i))) {
            uint32_t namelen = names[i].length;
            uint32_t nameoff = names[i].offset;  // offset from base of string storage

            // sanity check the name string location
            if (uint64_t(nameOffset) + uint64_t(nameStringsBase) +
                uint64_t(nameoff) + uint64_t(namelen) > dataLength) {
                return NS_ERROR_FAILURE;
            }

            strOffset = nameOffset + nameStringsBase + nameoff;

            // output 2-byte str size
            strLen = namelen & (~1);  // UTF-16 string len must be even
            *((uint16_t*) eotEnd) = uint16_t(strLen);
            eotEnd += 2;

            // length is number of UTF-16 chars, not bytes
            CopySwapUTF16(reinterpret_cast<const uint16_t*>(aFontData + strOffset),
                          reinterpret_cast<uint16_t*>(eotEnd),
                          (strLen >> 1));
        } else {
            // bug 496573 -- English names are not present.
            // supply an artificial one.
            strLen = dummyNames[i].Length() * sizeof(PRUnichar);
            *((uint16_t*) eotEnd) = uint16_t(strLen);
            eotEnd += 2;

            memcpy(eotEnd, dummyNames[i].BeginReading(), strLen);
        }
        eotEnd += strLen;

        // add 2-byte zero padding to the end of each string
        *eotEnd++ = 0;
        *eotEnd++ = 0;

        // Note: Microsoft's WEFT tool produces name strings which
        // include an extra null at the end of each string, in addition
        // to the 2-byte zero padding that separates the string fields. 
        // Don't think this is important to imitate...
    }

    // append null root string size
    *eotEnd++ = 0;
    *eotEnd++ = 0;

    NS_ASSERTION(eotEnd == aHeader->Elements() + aHeader->Length(), 
                 "header length calculation incorrect");
                 
    // bug 496573 -- fonts with a fullname that does not begin with the 
    // family name cause the EOT font loading API to hiccup
    uint32_t famOff = names[EOTFixedHeader::EOT_FAMILY_NAME_INDEX].offset;
    uint32_t famLen = names[EOTFixedHeader::EOT_FAMILY_NAME_INDEX].length;
    uint32_t fullOff = names[EOTFixedHeader::EOT_FULL_NAME_INDEX].offset;
    uint32_t fullLen = names[EOTFixedHeader::EOT_FULL_NAME_INDEX].length;
    
    const uint8_t *nameStrings = aFontData + nameOffset + nameStringsBase;

    // assure that the start of the fullname matches the family name
    if (famLen <= fullLen 
        && memcmp(nameStrings + famOff, nameStrings + fullOff, famLen)) {
        aOverlay->overlaySrc = nameOffset + nameStringsBase + famOff;
        aOverlay->overlaySrcLen = famLen;
        aOverlay->overlayDest = nameOffset + nameStringsBase + fullOff;
    }

    // -- OS/2 table data
    const OS2Table *os2Data = reinterpret_cast<const OS2Table*>(aFontData + os2Offset);

    memcpy(eotHeader->panose, os2Data->panose, sizeof(eotHeader->panose));

    eotHeader->italic = (uint16_t) os2Data->fsSelection & 0x01;
    eotHeader->weight = os2Data->usWeightClass;
    eotHeader->unicodeRange1 = os2Data->unicodeRange1;
    eotHeader->unicodeRange2 = os2Data->unicodeRange2;
    eotHeader->unicodeRange3 = os2Data->unicodeRange3;
    eotHeader->unicodeRange4 = os2Data->unicodeRange4;
    eotHeader->codePageRange1 = os2Data->codePageRange1;
    eotHeader->codePageRange2 = os2Data->codePageRange2;

    eotHeader->eotSize = aHeader->Length() + fontDataSize;

    // DumpEOTHeader(aHeader->Elements(), aHeader->Length());

    return NS_OK;
}

/* static */
bool
gfxFontUtils::IsCffFont(const uint8_t* aFontData, bool& hasVertical)
{
    // this is only called after aFontData has passed basic validation,
    // so we know there is enough data present to allow us to read the version!
    const SFNTHeader *sfntHeader = reinterpret_cast<const SFNTHeader*>(aFontData);

    uint32_t i;
    uint32_t numTables = sfntHeader->numTables;
    const TableDirEntry *dirEntry = 
        reinterpret_cast<const TableDirEntry*>(aFontData + sizeof(SFNTHeader));
    hasVertical = false;
    for (i = 0; i < numTables; i++, dirEntry++) {
        if (dirEntry->tag == TRUETYPE_TAG('v','h','e','a')) {
            hasVertical = true;
            break;
        }
    }

    return (sfntHeader->sfntVersion == TRUETYPE_TAG('O','T','T','O'));
}

#endif
