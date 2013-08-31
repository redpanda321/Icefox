/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/DebugOnly.h"

#ifdef MOZ_LOGGING
#define FORCE_PR_LOG /* Allow logging in the release build */
#endif
#include "prlog.h"

#include "nsServiceManagerUtils.h"
#include "nsReadableUtils.h"
#include "nsExpirationTracker.h"
#include "nsILanguageAtomService.h"
#include "nsITimer.h"

#include "gfxFont.h"
#include "gfxPlatform.h"
#include "nsGkAtoms.h"

#include "gfxTypes.h"
#include "nsAlgorithm.h"
#include "gfxContext.h"
#include "gfxFontMissingGlyphs.h"
#include "gfxUserFontSet.h"
#include "gfxPlatformFontList.h"
#include "gfxScriptItemizer.h"
#include "nsUnicodeProperties.h"
#include "nsMathUtils.h"
#include "nsBidiUtils.h"
#include "nsUnicodeRange.h"
#include "nsStyleConsts.h"
#include "mozilla/FloatingPoint.h"
#include "mozilla/Likely.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/Telemetry.h"
#include "gfxSVGGlyphs.h"

#include "cairo.h"
#include "gfxFontTest.h"

#include "harfbuzz/hb.h"

#include "nsCRT.h"
#include "sampler.h"

#include <algorithm>
#include <cstdlib> // for std::abs(int/long)
#include <cmath> // for std::abs(float/double)

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::unicode;
using mozilla::services::GetObserverService;

gfxFontCache *gfxFontCache::gGlobalCache = nullptr;

#ifdef DEBUG_roc
#define DEBUG_TEXT_RUN_STORAGE_METRICS
#endif

#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
static uint32_t gTextRunStorageHighWaterMark = 0;
static uint32_t gTextRunStorage = 0;
static uint32_t gFontCount = 0;
static uint32_t gGlyphExtentsCount = 0;
static uint32_t gGlyphExtentsWidthsTotalSize = 0;
static uint32_t gGlyphExtentsSetupEagerSimple = 0;
static uint32_t gGlyphExtentsSetupEagerTight = 0;
static uint32_t gGlyphExtentsSetupLazyTight = 0;
static uint32_t gGlyphExtentsSetupFallBackToTight = 0;
#endif

void
gfxCharacterMap::NotifyReleased()
{
    gfxPlatformFontList *fontlist = gfxPlatformFontList::PlatformFontList();
    if (mShared) {
        fontlist->RemoveCmap(this);
    }
    delete this;
}

gfxFontEntry::~gfxFontEntry()
{
    // For downloaded fonts, we need to tell the user font cache that this
    // entry is being deleted.
    if (!mIsProxy && IsUserFont() && !IsLocalUserFont()) {
        gfxUserFontSet::UserFontCache::ForgetFont(this);
    }

    if (mSVGGlyphs) {
        delete mSVGGlyphs;
    }
    delete mUserFontData;
}

bool gfxFontEntry::IsSymbolFont() 
{
    return mSymbolFont;
}

bool gfxFontEntry::TestCharacterMap(uint32_t aCh)
{
    if (!mCharacterMap) {
        ReadCMAP();
        NS_ASSERTION(mCharacterMap, "failed to initialize character map");
    }
    return mCharacterMap->test(aCh);
}

nsresult gfxFontEntry::InitializeUVSMap()
{
    // mUVSOffset will not be initialized
    // until cmap is initialized.
    if (!mCharacterMap) {
        ReadCMAP();
        NS_ASSERTION(mCharacterMap, "failed to initialize character map");
    }

    if (!mUVSOffset) {
        return NS_ERROR_FAILURE;
    }

    if (!mUVSData) {
        const uint32_t kCmapTag = TRUETYPE_TAG('c','m','a','p');
        AutoFallibleTArray<uint8_t,16384> buffer;
        if (GetFontTable(kCmapTag, buffer) != NS_OK) {
            mUVSOffset = 0; // don't bother to read the table again
            return NS_ERROR_FAILURE;
        }

        uint8_t* uvsData;
        nsresult rv = gfxFontUtils::ReadCMAPTableFormat14(
                          buffer.Elements() + mUVSOffset,
                          buffer.Length() - mUVSOffset,
                          uvsData);
        if (NS_FAILED(rv)) {
            mUVSOffset = 0; // don't bother to read the table again
            return rv;
        }

        mUVSData = uvsData;
    }

    return NS_OK;
}

uint16_t gfxFontEntry::GetUVSGlyph(uint32_t aCh, uint32_t aVS)
{
    InitializeUVSMap();

    if (mUVSData) {
        return gfxFontUtils::MapUVSToGlyphFormat14(mUVSData, aCh, aVS);
    }

    return 0;
}

nsresult gfxFontEntry::ReadCMAP()
{
    NS_ASSERTION(false, "using default no-op implementation of ReadCMAP");
    mCharacterMap = new gfxCharacterMap();
    return NS_OK;
}

nsString
gfxFontEntry::FamilyName()
{
    FallibleTArray<uint8_t> nameTable;
    nsresult rv = GetFontTable(TRUETYPE_TAG('n','a','m','e'), nameTable);
    if (NS_SUCCEEDED(rv)) {
        nsAutoString name;
        rv = gfxFontUtils::GetFamilyNameFromTable(nameTable, name);
        if (NS_SUCCEEDED(rv)) {
            return name;
        }
    }
    return Name();
}

nsString
gfxFontEntry::RealFaceName()
{
    FallibleTArray<uint8_t> nameTable;
    nsresult rv = GetFontTable(TRUETYPE_TAG('n','a','m','e'), nameTable);
    if (NS_SUCCEEDED(rv)) {
        nsAutoString name;
        rv = gfxFontUtils::GetFullNameFromTable(nameTable, name);
        if (NS_SUCCEEDED(rv)) {
            return name;
        }
    }
    return Name();
}

already_AddRefed<gfxFont>
gfxFontEntry::FindOrMakeFont(const gfxFontStyle *aStyle, bool aNeedsBold)
{
    // the font entry name is the psname, not the family name
    nsRefPtr<gfxFont> font = gfxFontCache::GetCache()->Lookup(this, aStyle);

    if (!font) {
        gfxFont *newFont = CreateFontInstance(aStyle, aNeedsBold);
        if (!newFont)
            return nullptr;
        if (!newFont->Valid()) {
            delete newFont;
            return nullptr;
        }
        font = newFont;
        gfxFontCache::GetCache()->AddNew(font);
    }
    gfxFont *f = nullptr;
    font.swap(f);
    return f;
}

bool
gfxFontEntry::HasSVGGlyph(uint32_t aGlyphId)
{
    NS_ASSERTION(mSVGInitialized, "SVG data has not yet been loaded. TryGetSVGData() first.");
    return mSVGGlyphs->HasSVGGlyph(aGlyphId);
}

bool
gfxFontEntry::GetSVGGlyphExtents(gfxContext *aContext, uint32_t aGlyphId,
                                 gfxRect *aResult)
{
    NS_ABORT_IF_FALSE(mSVGInitialized, "SVG data has not yet been loaded. TryGetSVGData() first.");

    gfxContextAutoSaveRestore matrixRestore(aContext);
    cairo_matrix_t fontMatrix;
    cairo_get_font_matrix(aContext->GetCairo(), &fontMatrix);

    gfxMatrix svgToAppSpace = *reinterpret_cast<gfxMatrix*>(&fontMatrix);
    svgToAppSpace.Scale(1.0f / gfxSVGGlyphs::SVG_UNITS_PER_EM,
                        1.0f / gfxSVGGlyphs::SVG_UNITS_PER_EM);

    return mSVGGlyphs->GetGlyphExtents(aGlyphId, svgToAppSpace, aResult);
}

bool
gfxFontEntry::RenderSVGGlyph(gfxContext *aContext, uint32_t aGlyphId,
                             int aDrawMode, gfxTextObjectPaint *aObjectPaint)
{
    NS_ASSERTION(mSVGInitialized, "SVG data has not yet been loaded. TryGetSVGData() first.");
    return mSVGGlyphs->RenderGlyph(aContext, aGlyphId, gfxFont::DrawMode(aDrawMode),
                                   aObjectPaint);
}

bool
gfxFontEntry::TryGetSVGData()
{
    if (!mSVGInitialized) {
        mSVGInitialized = true;

        bool svgEnabled;
        nsresult rv =
            Preferences::GetBool("gfx.font_rendering.opentype_svg.enabled",
                                 &svgEnabled);

        if (NS_FAILED(rv) || !svgEnabled) {
            return false;
        }

        FallibleTArray<uint8_t> svgTable;
        rv = GetFontTable(TRUETYPE_TAG('S', 'V', 'G', ' '), svgTable);
        if (NS_FAILED(rv)) {
            return false;
        }

        FallibleTArray<uint8_t> cmapTable;
        rv = GetFontTable(TRUETYPE_TAG('c', 'm', 'a', 'p'), cmapTable);
        NS_ENSURE_SUCCESS(rv, false);

        mSVGGlyphs = new gfxSVGGlyphs(svgTable, cmapTable);
    }

    return !!mSVGGlyphs;
}

/**
 * FontTableBlobData
 *
 * See FontTableHashEntry for the general strategy.
 */

class gfxFontEntry::FontTableBlobData {
public:
    // Adopts the content of aBuffer.
    // Pass a non-null aHashEntry only if it should be cleared if/when this
    // FontTableBlobData is deleted.
    FontTableBlobData(FallibleTArray<uint8_t>& aBuffer,
                      FontTableHashEntry *aHashEntry)
        : mHashEntry(aHashEntry), mHashtable()
    {
        MOZ_COUNT_CTOR(FontTableBlobData);
        mTableData.SwapElements(aBuffer);
    }

    ~FontTableBlobData() {
        MOZ_COUNT_DTOR(FontTableBlobData);
        if (mHashEntry) {
            if (mHashtable) {
                mHashtable->RemoveEntry(mHashEntry->GetKey());
            } else {
                mHashEntry->Clear();
            }
        }
    }

    // Useful for creating blobs
    const char *GetTable() const
    {
        return reinterpret_cast<const char*>(mTableData.Elements());
    }
    uint32_t GetTableLength() const { return mTableData.Length(); }

    // Tell this FontTableBlobData to remove the HashEntry when this is
    // destroyed.
    void ManageHashEntry(nsTHashtable<FontTableHashEntry> *aHashtable)
    {
        mHashtable = aHashtable;
    }

    // Disconnect from the HashEntry (because the blob has already been
    // removed from the hashtable).
    void ForgetHashEntry()
    {
        mHashEntry = nullptr;
    }

    size_t SizeOfExcludingThis(nsMallocSizeOfFun aMallocSizeOf) const {
        return mTableData.SizeOfExcludingThis(aMallocSizeOf);
    }
    size_t SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf) const {
        return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
    }

private:
    // The font table data block, owned (via adoption)
    FallibleTArray<uint8_t> mTableData;
    // The blob destroy function needs to know the hashtable entry,
    FontTableHashEntry *mHashEntry;
    // and the owning hashtable, so that it can remove the entry.
    nsTHashtable<FontTableHashEntry> *mHashtable;

    // not implemented
    FontTableBlobData(const FontTableBlobData&);
};

void
gfxFontEntry::FontTableHashEntry::SaveTable(FallibleTArray<uint8_t>& aTable)
{
    Clear();
    // adopts elements of aTable
    FontTableBlobData *data = new FontTableBlobData(aTable, nullptr);
    mBlob = hb_blob_create(data->GetTable(), data->GetTableLength(),
                           HB_MEMORY_MODE_READONLY,
                           data, DeleteFontTableBlobData);    
}

hb_blob_t *
gfxFontEntry::FontTableHashEntry::
ShareTableAndGetBlob(FallibleTArray<uint8_t>& aTable,
                     nsTHashtable<FontTableHashEntry> *aHashtable)
{
    Clear();
    // adopts elements of aTable
    mSharedBlobData = new FontTableBlobData(aTable, this);
    mBlob = hb_blob_create(mSharedBlobData->GetTable(),
                           mSharedBlobData->GetTableLength(),
                           HB_MEMORY_MODE_READONLY,
                           mSharedBlobData, DeleteFontTableBlobData);
    if (!mSharedBlobData) {
        // The FontTableBlobData was destroyed during hb_blob_create().
        // The (empty) blob is still be held in the hashtable with a strong
        // reference.
        return hb_blob_reference(mBlob);
    }

    // Tell the FontTableBlobData to remove this hash entry when destroyed.
    // The hashtable does not keep a strong reference.
    mSharedBlobData->ManageHashEntry(aHashtable);
    return mBlob;
}

void
gfxFontEntry::FontTableHashEntry::Clear()
{
    // If the FontTableBlobData is managing the hash entry, then the blob is
    // not owned by this HashEntry; otherwise there is strong reference to the
    // blob that must be removed.
    if (mSharedBlobData) {
        mSharedBlobData->ForgetHashEntry();
        mSharedBlobData = nullptr;
    } else if (mBlob) {
        hb_blob_destroy(mBlob);
    }
    mBlob = nullptr;
}

// a hb_destroy_func for hb_blob_create

/* static */ void
gfxFontEntry::FontTableHashEntry::DeleteFontTableBlobData(void *aBlobData)
{
    delete static_cast<FontTableBlobData*>(aBlobData);
}

hb_blob_t *
gfxFontEntry::FontTableHashEntry::GetBlob() const
{
    return hb_blob_reference(mBlob);
}

bool
gfxFontEntry::GetExistingFontTable(uint32_t aTag, hb_blob_t **aBlob)
{
    if (!mFontTableCache.IsInitialized()) {
        // we do this here rather than on fontEntry construction
        // because not all shapers will access the table cache at all
        mFontTableCache.Init(10);
    }

    FontTableHashEntry *entry = mFontTableCache.GetEntry(aTag);
    if (!entry) {
        return false;
    }

    *aBlob = entry->GetBlob();
    return true;
}

hb_blob_t *
gfxFontEntry::ShareFontTableAndGetBlob(uint32_t aTag,
                                       FallibleTArray<uint8_t>* aBuffer)
{
    if (MOZ_UNLIKELY(!mFontTableCache.IsInitialized())) {
        // we do this here rather than on fontEntry construction
        // because not all shapers will access the table cache at all
        mFontTableCache.Init(10);
    }

    FontTableHashEntry *entry = mFontTableCache.PutEntry(aTag);
    if (MOZ_UNLIKELY(!entry)) { // OOM
        return nullptr;
    }

    if (!aBuffer) {
        // ensure the entry is null
        entry->Clear();
        return nullptr;
    }

    return entry->ShareTableAndGetBlob(*aBuffer, &mFontTableCache);
}

#ifdef MOZ_GRAPHITE
void
gfxFontEntry::CheckForGraphiteTables()
{
    AutoFallibleTArray<uint8_t,16384> buffer;
    mHasGraphiteTables =
        NS_SUCCEEDED(GetFontTable(TRUETYPE_TAG('S','i','l','f'), buffer));
}
#endif

/* static */ size_t
gfxFontEntry::FontTableHashEntry::SizeOfEntryExcludingThis
    (FontTableHashEntry *aEntry,
     nsMallocSizeOfFun   aMallocSizeOf,
     void*               aUserArg)
{
    FontListSizes *sizes = static_cast<FontListSizes*>(aUserArg);
    if (aEntry->mBlob) {
        sizes->mFontTableCacheSize += aMallocSizeOf(aEntry->mBlob);
    }
    if (aEntry->mSharedBlobData) {
        sizes->mFontTableCacheSize +=
            aEntry->mSharedBlobData->SizeOfIncludingThis(aMallocSizeOf);
    }

    // the size of the table is recorded in the FontListSizes record,
    // so we return 0 here for the function result
    return 0;
}

void
gfxFontEntry::SizeOfExcludingThis(nsMallocSizeOfFun aMallocSizeOf,
                                  FontListSizes*    aSizes) const
{
    aSizes->mFontListSize += mName.SizeOfExcludingThisIfUnshared(aMallocSizeOf);

    // cmaps are shared so only non-shared cmaps are included here
    if (mCharacterMap && mCharacterMap->mBuildOnTheFly) {
        aSizes->mCharMapsSize +=
            mCharacterMap->SizeOfIncludingThis(aMallocSizeOf);
    }
    aSizes->mFontTableCacheSize +=
        mFontTableCache.SizeOfExcludingThis(
            FontTableHashEntry::SizeOfEntryExcludingThis,
            aMallocSizeOf, aSizes);
}

void
gfxFontEntry::SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf,
                                  FontListSizes*    aSizes) const
{
    aSizes->mFontListSize += aMallocSizeOf(this);
    SizeOfExcludingThis(aMallocSizeOf, aSizes);
}

//////////////////////////////////////////////////////////////////////////////
//
// class gfxFontFamily
//
//////////////////////////////////////////////////////////////////////////////

// we consider faces with mStandardFace == true to be "greater than" those with false,
// because during style matching, later entries will replace earlier ones
class FontEntryStandardFaceComparator {
  public:
    bool Equals(const nsRefPtr<gfxFontEntry>& a, const nsRefPtr<gfxFontEntry>& b) const {
        return a->mStandardFace == b->mStandardFace;
    }
    bool LessThan(const nsRefPtr<gfxFontEntry>& a, const nsRefPtr<gfxFontEntry>& b) const {
        return (a->mStandardFace == false && b->mStandardFace == true);
    }
};

void
gfxFontFamily::SortAvailableFonts()
{
    mAvailableFonts.Sort(FontEntryStandardFaceComparator());
}

bool
gfxFontFamily::HasOtherFamilyNames()
{
    // need to read in other family names to determine this
    if (!mOtherFamilyNamesInitialized) {
        ReadOtherFamilyNames(gfxPlatformFontList::PlatformFontList());  // sets mHasOtherFamilyNames
    }
    return mHasOtherFamilyNames;
}

gfxFontEntry*
gfxFontFamily::FindFontForStyle(const gfxFontStyle& aFontStyle, 
                                bool& aNeedsSyntheticBold)
{
    if (!mHasStyles)
        FindStyleVariations(); // collect faces for the family, if not already done

    NS_ASSERTION(mAvailableFonts.Length() > 0, "font family with no faces!");

    aNeedsSyntheticBold = false;

    int8_t baseWeight = aFontStyle.ComputeWeight();
    bool wantBold = baseWeight >= 6;

    // If the family has only one face, we simply return it; no further checking needed
    if (mAvailableFonts.Length() == 1) {
        gfxFontEntry *fe = mAvailableFonts[0];
        aNeedsSyntheticBold = wantBold && !fe->IsBold();
        return fe;
    }

    bool wantItalic = (aFontStyle.style &
                       (NS_FONT_STYLE_ITALIC | NS_FONT_STYLE_OBLIQUE)) != 0;

    // Most families are "simple", having just Regular/Bold/Italic/BoldItalic,
    // or some subset of these. In this case, we have exactly 4 entries in mAvailableFonts,
    // stored in the above order; note that some of the entries may be NULL.
    // We can then pick the required entry based on whether the request is for
    // bold or non-bold, italic or non-italic, without running the more complex
    // matching algorithm used for larger families with many weights and/or widths.

    if (mIsSimpleFamily) {
        // Family has no more than the "standard" 4 faces, at fixed indexes;
        // calculate which one we want.
        // Note that we cannot simply return it as not all 4 faces are necessarily present.
        uint8_t faceIndex = (wantItalic ? kItalicMask : 0) |
                            (wantBold ? kBoldMask : 0);

        // if the desired style is available, return it directly
        gfxFontEntry *fe = mAvailableFonts[faceIndex];
        if (fe) {
            // no need to set aNeedsSyntheticBold here as we matched the boldness request
            return fe;
        }

        // order to check fallback faces in a simple family, depending on requested style
        static const uint8_t simpleFallbacks[4][3] = {
            { kBoldFaceIndex, kItalicFaceIndex, kBoldItalicFaceIndex },   // fallbacks for Regular
            { kRegularFaceIndex, kBoldItalicFaceIndex, kItalicFaceIndex },// Bold
            { kBoldItalicFaceIndex, kRegularFaceIndex, kBoldFaceIndex },  // Italic
            { kItalicFaceIndex, kBoldFaceIndex, kRegularFaceIndex }       // BoldItalic
        };
        const uint8_t *order = simpleFallbacks[faceIndex];

        for (uint8_t trial = 0; trial < 3; ++trial) {
            // check remaining faces in order of preference to find the first that actually exists
            fe = mAvailableFonts[order[trial]];
            if (fe) {
                aNeedsSyntheticBold = wantBold && !fe->IsBold();
                return fe;
            }
        }

        // this can't happen unless we have totally broken the font-list manager!
        NS_NOTREACHED("no face found in simple font family!");
        return nullptr;
    }

    // This is a large/rich font family, so we do full style- and weight-matching:
    // first collect a list of weights that are the best match for the requested
    // font-stretch and font-style, then pick the best weight match among those
    // available.

    gfxFontEntry *weightList[10] = { 0 };
    bool foundWeights = FindWeightsForStyle(weightList, wantItalic, aFontStyle.stretch);
    if (!foundWeights) {
        return nullptr;
    }

    // First find a match for the best weight
    int8_t matchBaseWeight = 0;
    int8_t i = baseWeight;

    // Need to special case when normal face doesn't exist but medium does.
    // In that case, use medium otherwise weights < 400
    if (baseWeight == 4 && !weightList[4]) {
        i = 5; // medium
    }

    // Loop through weights, since one exists loop will terminate
    int8_t direction = (baseWeight > 5) ? 1 : -1;
    for (; ; i += direction) {
        if (weightList[i]) {
            matchBaseWeight = i;
            break;
        }

        // If we've reached one side without finding a font,
        // start over and go the other direction until we find a match
        if (i == 1 || i == 9) {
            i = baseWeight;
            direction = -direction;
        }
    }

    NS_ASSERTION(matchBaseWeight != 0, 
                 "weight mapping should always find at least one font in a family");

    gfxFontEntry *matchFE = weightList[matchBaseWeight];

    NS_ASSERTION(matchFE,
                 "weight mapping should always find at least one font in a family");

    if (!matchFE->IsBold() && baseWeight >= 6)
    {
        aNeedsSyntheticBold = true;
    }

    return matchFE;
}

void
gfxFontFamily::CheckForSimpleFamily()
{
    uint32_t count = mAvailableFonts.Length();
    if (count > 4 || count == 0) {
        return; // can't be "simple" if there are >4 faces;
                // if none then the family is unusable anyway
    }

    if (count == 1) {
        mIsSimpleFamily = true;
        return;
    }

    int16_t firstStretch = mAvailableFonts[0]->Stretch();

    gfxFontEntry *faces[4] = { 0 };
    for (uint8_t i = 0; i < count; ++i) {
        gfxFontEntry *fe = mAvailableFonts[i];
        if (fe->Stretch() != firstStretch) {
            return; // font-stretch doesn't match, don't treat as simple family
        }
        uint8_t faceIndex = (fe->IsItalic() ? kItalicMask : 0) |
                            (fe->Weight() >= 600 ? kBoldMask : 0);
        if (faces[faceIndex]) {
            return; // two faces resolve to the same slot; family isn't "simple"
        }
        faces[faceIndex] = fe;
    }

    // we have successfully slotted the available faces into the standard
    // 4-face framework
    mAvailableFonts.SetLength(4);
    for (uint8_t i = 0; i < 4; ++i) {
        if (mAvailableFonts[i].get() != faces[i]) {
            mAvailableFonts[i].swap(faces[i]);
        }
    }

    mIsSimpleFamily = true;
}

static inline uint32_t
StyleDistance(gfxFontEntry *aFontEntry,
              bool anItalic, int16_t aStretch)
{
    // Compute a measure of the "distance" between the requested style
    // and the given fontEntry,
    // considering italicness and font-stretch but not weight.

    int32_t distance = 0;
    if (aStretch != aFontEntry->mStretch) {
        // stretch values are in the range -4 .. +4
        // if aStretch is positive, we prefer more-positive values;
        // if zero or negative, prefer more-negative
        if (aStretch > 0) {
            distance = (aFontEntry->mStretch - aStretch) * 2;
        } else {
            distance = (aStretch - aFontEntry->mStretch) * 2;
        }
        // if the computed "distance" here is negative, it means that
        // aFontEntry lies in the "non-preferred" direction from aStretch,
        // so we treat that as larger than any preferred-direction distance
        // (max possible is 8) by adding an extra 10 to the absolute value
        if (distance < 0) {
            distance = -distance + 10;
        }
    }
    if (aFontEntry->IsItalic() != anItalic) {
        distance += 1;
    }
    return uint32_t(distance);
}

bool
gfxFontFamily::FindWeightsForStyle(gfxFontEntry* aFontsForWeights[],
                                   bool anItalic, int16_t aStretch)
{
    uint32_t foundWeights = 0;
    uint32_t bestMatchDistance = 0xffffffff;

    uint32_t count = mAvailableFonts.Length();
    for (uint32_t i = 0; i < count; i++) {
        // this is not called for "simple" families, and therefore it does not
        // need to check the mAvailableFonts entries for NULL
        gfxFontEntry *fe = mAvailableFonts[i];
        uint32_t distance = StyleDistance(fe, anItalic, aStretch);
        if (distance <= bestMatchDistance) {
            int8_t wt = fe->mWeight / 100;
            NS_ASSERTION(wt >= 1 && wt < 10, "invalid weight in fontEntry");
            if (!aFontsForWeights[wt]) {
                // record this as a possible candidate for weight matching
                aFontsForWeights[wt] = fe;
                ++foundWeights;
            } else {
                uint32_t prevDistance =
                    StyleDistance(aFontsForWeights[wt], anItalic, aStretch);
                if (prevDistance >= distance) {
                    // replacing a weight we already found,
                    // so don't increment foundWeights
                    aFontsForWeights[wt] = fe;
                }
            }
            bestMatchDistance = distance;
        }
    }

    NS_ASSERTION(foundWeights > 0, "Font family containing no faces?");

    if (foundWeights == 1) {
        // no need to cull entries if we only found one weight
        return true;
    }

    // we might have recorded some faces that were a partial style match, but later found
    // others that were closer; in this case, we need to cull the poorer matches from the
    // weight list we'll return
    for (uint32_t i = 0; i < 10; ++i) {
        if (aFontsForWeights[i] &&
            StyleDistance(aFontsForWeights[i], anItalic, aStretch) > bestMatchDistance)
        {
            aFontsForWeights[i] = 0;
        }
    }

    return (foundWeights > 0);
}


void gfxFontFamily::LocalizedName(nsAString& aLocalizedName)
{
    // just return the primary name; subclasses should override
    aLocalizedName = mName;
}

// metric for how close a given font matches a style
static int32_t
CalcStyleMatch(gfxFontEntry *aFontEntry, const gfxFontStyle *aStyle)
{
    int32_t rank = 0;
    if (aStyle) {
         // italics
         bool wantItalic =
             (aStyle->style & (NS_FONT_STYLE_ITALIC | NS_FONT_STYLE_OBLIQUE)) != 0;
         if (aFontEntry->IsItalic() == wantItalic) {
             rank += 10;
         }

        // measure of closeness of weight to the desired value
        rank += 9 - abs(aFontEntry->Weight() / 100 - aStyle->ComputeWeight());
    } else {
        // if no font to match, prefer non-bold, non-italic fonts
        if (!aFontEntry->IsItalic()) {
            rank += 3;
        }
        if (!aFontEntry->IsBold()) {
            rank += 2;
        }
    }

    return rank;
}

#define RANK_MATCHED_CMAP   20

void
gfxFontFamily::FindFontForChar(GlobalFontMatch *aMatchData)
{
    if (mFamilyCharacterMapInitialized && !TestCharacterMap(aMatchData->mCh)) {
        // none of the faces in the family support the required char,
        // so bail out immediately
        return;
    }

    bool needsBold;
    gfxFontStyle normal;
    gfxFontEntry *fe = FindFontForStyle(
                  (aMatchData->mStyle == nullptr) ? *aMatchData->mStyle : normal,
                  needsBold);

    if (fe && !fe->SkipDuringSystemFallback()) {
        int32_t rank = 0;

        if (fe->TestCharacterMap(aMatchData->mCh)) {
            rank += RANK_MATCHED_CMAP;
            aMatchData->mCount++;
#ifdef PR_LOGGING
            PRLogModuleInfo *log = gfxPlatform::GetLog(eGfxLog_textrun);

            if (MOZ_UNLIKELY(log)) {
                uint32_t charRange = gfxFontUtils::CharRangeBit(aMatchData->mCh);
                uint32_t unicodeRange = FindCharUnicodeRange(aMatchData->mCh);
                uint32_t script = GetScriptCode(aMatchData->mCh);
                PR_LOG(log, PR_LOG_DEBUG,\
                       ("(textrun-systemfallback-fonts) char: u+%6.6x "
                        "char-range: %d unicode-range: %d script: %d match: [%s]\n",
                        aMatchData->mCh,
                        charRange, unicodeRange, script,
                        NS_ConvertUTF16toUTF8(fe->Name()).get()));
            }
#endif
        }

        aMatchData->mCmapsTested++;
        if (rank == 0) {
            return;
        }

         // omitting from original windows code -- family name, lang group, pitch
         // not available in current FontEntry implementation
        rank += CalcStyleMatch(fe, aMatchData->mStyle);

        // xxx - add whether AAT font with morphing info for specific lang groups

        if (rank > aMatchData->mMatchRank
            || (rank == aMatchData->mMatchRank &&
                Compare(fe->Name(), aMatchData->mBestMatch->Name()) > 0))
        {
            aMatchData->mBestMatch = fe;
            aMatchData->mMatchedFamily = this;
            aMatchData->mMatchRank = rank;
        }
    }
}

void
gfxFontFamily::SearchAllFontsForChar(GlobalFontMatch *aMatchData)
{
    uint32_t i, numFonts = mAvailableFonts.Length();
    for (i = 0; i < numFonts; i++) {
        gfxFontEntry *fe = mAvailableFonts[i];
        if (fe && fe->TestCharacterMap(aMatchData->mCh)) {
            int32_t rank = RANK_MATCHED_CMAP;
            rank += CalcStyleMatch(fe, aMatchData->mStyle);
            if (rank > aMatchData->mMatchRank
                || (rank == aMatchData->mMatchRank &&
                    Compare(fe->Name(), aMatchData->mBestMatch->Name()) > 0))
            {
                aMatchData->mBestMatch = fe;
                aMatchData->mMatchedFamily = this;
                aMatchData->mMatchRank = rank;
            }
        }
    }
}

// returns true if other names were found, false otherwise
bool
gfxFontFamily::ReadOtherFamilyNamesForFace(gfxPlatformFontList *aPlatformFontList,
                                           FallibleTArray<uint8_t>& aNameTable,
                                           bool useFullName)
{
    const uint8_t *nameData = aNameTable.Elements();
    uint32_t dataLength = aNameTable.Length();
    const gfxFontUtils::NameHeader *nameHeader =
        reinterpret_cast<const gfxFontUtils::NameHeader*>(nameData);

    uint32_t nameCount = nameHeader->count;
    if (nameCount * sizeof(gfxFontUtils::NameRecord) > dataLength) {
        NS_WARNING("invalid font (name records)");
        return false;
    }
    
    const gfxFontUtils::NameRecord *nameRecord =
        reinterpret_cast<const gfxFontUtils::NameRecord*>(nameData + sizeof(gfxFontUtils::NameHeader));
    uint32_t stringsBase = uint32_t(nameHeader->stringOffset);

    bool foundNames = false;
    for (uint32_t i = 0; i < nameCount; i++, nameRecord++) {
        uint32_t nameLen = nameRecord->length;
        uint32_t nameOff = nameRecord->offset;  // offset from base of string storage

        if (stringsBase + nameOff + nameLen > dataLength) {
            NS_WARNING("invalid font (name table strings)");
            return false;
        }

        uint16_t nameID = nameRecord->nameID;
        if ((useFullName && nameID == gfxFontUtils::NAME_ID_FULL) ||
            (!useFullName && (nameID == gfxFontUtils::NAME_ID_FAMILY ||
                              nameID == gfxFontUtils::NAME_ID_PREFERRED_FAMILY))) {
            nsAutoString otherFamilyName;
            bool ok = gfxFontUtils::DecodeFontName(nameData + stringsBase + nameOff,
                                                     nameLen,
                                                     uint32_t(nameRecord->platformID),
                                                     uint32_t(nameRecord->encodingID),
                                                     uint32_t(nameRecord->languageID),
                                                     otherFamilyName);
            // add if not same as canonical family name
            if (ok && otherFamilyName != mName) {
                aPlatformFontList->AddOtherFamilyName(this, otherFamilyName);
                foundNames = true;
            }
        }
    }

    return foundNames;
}


void
gfxFontFamily::ReadOtherFamilyNames(gfxPlatformFontList *aPlatformFontList)
{
    if (mOtherFamilyNamesInitialized) 
        return;
    mOtherFamilyNamesInitialized = true;

    FindStyleVariations();

    // read in other family names for the first face in the list
    uint32_t i, numFonts = mAvailableFonts.Length();
    const uint32_t kNAME = TRUETYPE_TAG('n','a','m','e');
    AutoFallibleTArray<uint8_t,8192> buffer;

    for (i = 0; i < numFonts; ++i) {
        gfxFontEntry *fe = mAvailableFonts[i];
        if (!fe)
            continue;

        if (fe->GetFontTable(kNAME, buffer) != NS_OK)
            continue;

        mHasOtherFamilyNames = ReadOtherFamilyNamesForFace(aPlatformFontList,
                                                           buffer);
        break;
    }

    // read in other names for the first face in the list with the assumption
    // that if extra names don't exist in that face then they don't exist in
    // other faces for the same font
    if (!mHasOtherFamilyNames) 
        return;

    // read in names for all faces, needed to catch cases where fonts have
    // family names for individual weights (e.g. Hiragino Kaku Gothic Pro W6)
    for ( ; i < numFonts; i++) {
        gfxFontEntry *fe = mAvailableFonts[i];
        if (!fe)
            continue;

        if (fe->GetFontTable(kNAME, buffer) != NS_OK)
            continue;

        ReadOtherFamilyNamesForFace(aPlatformFontList, buffer);
    }
}

void
gfxFontFamily::ReadFaceNames(gfxPlatformFontList *aPlatformFontList, 
                             bool aNeedFullnamePostscriptNames)
{
    // if all needed names have already been read, skip
    if (mOtherFamilyNamesInitialized &&
        (mFaceNamesInitialized || !aNeedFullnamePostscriptNames))
        return;

    FindStyleVariations();

    uint32_t i, numFonts = mAvailableFonts.Length();
    const uint32_t kNAME = TRUETYPE_TAG('n','a','m','e');
    AutoFallibleTArray<uint8_t,8192> buffer;
    nsAutoString fullname, psname;

    bool firstTime = true, readAllFaces = false;
    for (i = 0; i < numFonts; ++i) {
        gfxFontEntry *fe = mAvailableFonts[i];
        if (!fe)
            continue;

        if (fe->GetFontTable(kNAME, buffer) != NS_OK)
            continue;

        if (aNeedFullnamePostscriptNames) {
            if (gfxFontUtils::ReadCanonicalName(
                    buffer, gfxFontUtils::NAME_ID_FULL, fullname) == NS_OK)
            {
                aPlatformFontList->AddFullname(fe, fullname);
            }

            if (gfxFontUtils::ReadCanonicalName(
                    buffer, gfxFontUtils::NAME_ID_POSTSCRIPT, psname) == NS_OK)
            {
                aPlatformFontList->AddPostscriptName(fe, psname);
            }
        }

       if (!mOtherFamilyNamesInitialized && (firstTime || readAllFaces)) {
           bool foundOtherName = ReadOtherFamilyNamesForFace(aPlatformFontList,
                                                               buffer);

           // if the first face has a different name, scan all faces, otherwise
           // assume the family doesn't have other names
           if (firstTime && foundOtherName) {
               mHasOtherFamilyNames = true;
               readAllFaces = true;
           }
           firstTime = false;
       }

       // if not reading in any more names, skip other faces
       if (!readAllFaces && !aNeedFullnamePostscriptNames)
           break;
    }

    mFaceNamesInitialized = true;
    mOtherFamilyNamesInitialized = true;
}


gfxFontEntry*
gfxFontFamily::FindFont(const nsAString& aPostscriptName)
{
    // find the font using a simple linear search
    uint32_t numFonts = mAvailableFonts.Length();
    for (uint32_t i = 0; i < numFonts; i++) {
        gfxFontEntry *fe = mAvailableFonts[i].get();
        if (fe && fe->Name() == aPostscriptName)
            return fe;
    }
    return nullptr;
}

void
gfxFontFamily::SizeOfExcludingThis(nsMallocSizeOfFun aMallocSizeOf,
                                   FontListSizes*    aSizes) const
{
    aSizes->mFontListSize +=
        mName.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
    aSizes->mCharMapsSize +=
        mFamilyCharacterMap.SizeOfExcludingThis(aMallocSizeOf);

    aSizes->mFontListSize +=
        mAvailableFonts.SizeOfExcludingThis(aMallocSizeOf);
    for (uint32_t i = 0; i < mAvailableFonts.Length(); ++i) {
        gfxFontEntry *fe = mAvailableFonts[i];
        if (fe) {
            fe->SizeOfIncludingThis(aMallocSizeOf, aSizes);
        }
    }
}

void
gfxFontFamily::SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf,
                                   FontListSizes*    aSizes) const
{
    aSizes->mFontListSize += aMallocSizeOf(this);
    SizeOfExcludingThis(aMallocSizeOf, aSizes);
}
 
/*
 * gfxFontCache - global cache of gfxFont instances.
 * Expires unused fonts after a short interval;
 * notifies fonts to age their cached shaped-word records;
 * observes memory-pressure notification and tells fonts to clear their
 * shaped-word caches to free up memory.
 */

NS_IMPL_ISUPPORTS1(gfxFontCache::MemoryReporter, nsIMemoryMultiReporter)

NS_MEMORY_REPORTER_MALLOC_SIZEOF_FUN(FontCacheMallocSizeOf, "font-cache")

NS_IMETHODIMP
gfxFontCache::MemoryReporter::GetName(nsACString &aName)
{
    aName.AssignLiteral("font-cache");
    return NS_OK;
}

NS_IMETHODIMP
gfxFontCache::MemoryReporter::CollectReports
    (nsIMemoryMultiReporterCallback* aCb,
     nsISupports* aClosure)
{
    FontCacheSizes sizes;

    gfxFontCache::GetCache()->SizeOfIncludingThis(&FontCacheMallocSizeOf,
                                                  &sizes);

    aCb->Callback(EmptyCString(),
                  NS_LITERAL_CSTRING("explicit/gfx/font-cache"),
                  nsIMemoryReporter::KIND_HEAP, nsIMemoryReporter::UNITS_BYTES,
                  sizes.mFontInstances,
                  NS_LITERAL_CSTRING("Memory used for active font instances."),
                  aClosure);

    aCb->Callback(EmptyCString(),
                  NS_LITERAL_CSTRING("explicit/gfx/font-shaped-words"),
                  nsIMemoryReporter::KIND_HEAP, nsIMemoryReporter::UNITS_BYTES,
                  sizes.mShapedWords,
                  NS_LITERAL_CSTRING("Memory used to cache shaped glyph data."),
                  aClosure);

    return NS_OK;
}

NS_IMETHODIMP
gfxFontCache::MemoryReporter::GetExplicitNonHeap(int64_t* aAmount)
{
    // This reporter only measures heap memory.
    *aAmount = 0;
    return NS_OK;
}

// Observer for the memory-pressure notification, to trigger
// flushing of the shaped-word caches
class MemoryPressureObserver MOZ_FINAL : public nsIObserver,
                                         public nsSupportsWeakReference
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOBSERVER
};

NS_IMPL_ISUPPORTS2(MemoryPressureObserver, nsIObserver, nsISupportsWeakReference)

NS_IMETHODIMP
MemoryPressureObserver::Observe(nsISupports *aSubject,
                                const char *aTopic,
                                const PRUnichar *someData)
{
    if (!nsCRT::strcmp(aTopic, "memory-pressure")) {
        gfxFontCache *fontCache = gfxFontCache::GetCache();
        if (fontCache) {
            fontCache->FlushShapedWordCaches();
        }
    }
    return NS_OK;
}

nsresult
gfxFontCache::Init()
{
    NS_ASSERTION(!gGlobalCache, "Where did this come from?");
    gGlobalCache = new gfxFontCache();
    if (!gGlobalCache) {
        return NS_ERROR_OUT_OF_MEMORY;
    }
    NS_RegisterMemoryMultiReporter(new MemoryReporter);
    return NS_OK;
}

void
gfxFontCache::Shutdown()
{
    delete gGlobalCache;
    gGlobalCache = nullptr;

#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
    printf("Textrun storage high water mark=%d\n", gTextRunStorageHighWaterMark);
    printf("Total number of fonts=%d\n", gFontCount);
    printf("Total glyph extents allocated=%d (size %d)\n", gGlyphExtentsCount,
            int(gGlyphExtentsCount*sizeof(gfxGlyphExtents)));
    printf("Total glyph extents width-storage size allocated=%d\n", gGlyphExtentsWidthsTotalSize);
    printf("Number of simple glyph extents eagerly requested=%d\n", gGlyphExtentsSetupEagerSimple);
    printf("Number of tight glyph extents eagerly requested=%d\n", gGlyphExtentsSetupEagerTight);
    printf("Number of tight glyph extents lazily requested=%d\n", gGlyphExtentsSetupLazyTight);
    printf("Number of simple glyph extent setups that fell back to tight=%d\n", gGlyphExtentsSetupFallBackToTight);
#endif
}

gfxFontCache::gfxFontCache()
    : nsExpirationTracker<gfxFont,3>(FONT_TIMEOUT_SECONDS * 1000)
{
    mFonts.Init();

    nsCOMPtr<nsIObserverService> obs = GetObserverService();
    if (obs) {
        obs->AddObserver(new MemoryPressureObserver, "memory-pressure", false);
    }

#if 0 // disabled due to crashiness, see bug 717175
    mWordCacheExpirationTimer = do_CreateInstance("@mozilla.org/timer;1");
    if (mWordCacheExpirationTimer) {
        mWordCacheExpirationTimer->
            InitWithFuncCallback(WordCacheExpirationTimerCallback, this,
                                 SHAPED_WORD_TIMEOUT_SECONDS * 1000,
                                 nsITimer::TYPE_REPEATING_SLACK);
    }
#endif
}

gfxFontCache::~gfxFontCache()
{
    // Ensure the user font cache releases its references to font entries,
    // so they aren't kept alive after the font instances and font-list
    // have been shut down.
    gfxUserFontSet::UserFontCache::Shutdown();

    if (mWordCacheExpirationTimer) {
        mWordCacheExpirationTimer->Cancel();
        mWordCacheExpirationTimer = nullptr;
    }

    // Expire everything that has a zero refcount, so we don't leak them.
    AgeAllGenerations();
    // All fonts should be gone.
    NS_WARN_IF_FALSE(mFonts.Count() == 0,
                     "Fonts still alive while shutting down gfxFontCache");
    // Note that we have to delete everything through the expiration
    // tracker, since there might be fonts not in the hashtable but in
    // the tracker.
}

bool
gfxFontCache::HashEntry::KeyEquals(const KeyTypePointer aKey) const
{
    return aKey->mFontEntry == mFont->GetFontEntry() &&
           aKey->mStyle->Equals(*mFont->GetStyle());
}

already_AddRefed<gfxFont>
gfxFontCache::Lookup(const gfxFontEntry *aFontEntry,
                     const gfxFontStyle *aStyle)
{
    Key key(aFontEntry, aStyle);
    HashEntry *entry = mFonts.GetEntry(key);

    Telemetry::Accumulate(Telemetry::FONT_CACHE_HIT, entry != nullptr);
    if (!entry)
        return nullptr;

    gfxFont *font = entry->mFont;
    NS_ADDREF(font);
    return font;
}

void
gfxFontCache::AddNew(gfxFont *aFont)
{
    Key key(aFont->GetFontEntry(), aFont->GetStyle());
    HashEntry *entry = mFonts.PutEntry(key);
    if (!entry)
        return;
    gfxFont *oldFont = entry->mFont;
    entry->mFont = aFont;
    // Assert that we can find the entry we just put in (this fails if the key
    // has a NaN float value in it, e.g. 'sizeAdjust').
    MOZ_ASSERT(entry == mFonts.GetEntry(key));
    // If someone's asked us to replace an existing font entry, then that's a
    // bit weird, but let it happen, and expire the old font if it's not used.
    if (oldFont && oldFont->GetExpirationState()->IsTracked()) {
        // if oldFont == aFont, recount should be > 0,
        // so we shouldn't be here.
        NS_ASSERTION(aFont != oldFont, "new font is tracked for expiry!");
        NotifyExpired(oldFont);
    }
}

void
gfxFontCache::NotifyReleased(gfxFont *aFont)
{
    nsresult rv = AddObject(aFont);
    if (NS_FAILED(rv)) {
        // We couldn't track it for some reason. Kill it now.
        DestroyFont(aFont);
    }
    // Note that we might have fonts that aren't in the hashtable, perhaps because
    // of OOM adding to the hashtable or because someone did an AddNew where
    // we already had a font. These fonts are added to the expiration tracker
    // anyway, even though Lookup can't resurrect them. Eventually they will
    // expire and be deleted.
}

void
gfxFontCache::NotifyExpired(gfxFont *aFont)
{
    aFont->ClearCachedWords();
    RemoveObject(aFont);
    DestroyFont(aFont);
}

void
gfxFontCache::DestroyFont(gfxFont *aFont)
{
    Key key(aFont->GetFontEntry(), aFont->GetStyle());
    HashEntry *entry = mFonts.GetEntry(key);
    if (entry && entry->mFont == aFont) {
        mFonts.RemoveEntry(key);
    }
    NS_ASSERTION(aFont->GetRefCount() == 0,
                 "Destroying with non-zero ref count!");
    delete aFont;
}

/*static*/
PLDHashOperator
gfxFontCache::AgeCachedWordsForFont(HashEntry* aHashEntry, void* aUserData)
{
    aHashEntry->mFont->AgeCachedWords();
    return PL_DHASH_NEXT;
}

/*static*/
void
gfxFontCache::WordCacheExpirationTimerCallback(nsITimer* aTimer, void* aCache)
{
    gfxFontCache* cache = static_cast<gfxFontCache*>(aCache);
    cache->mFonts.EnumerateEntries(AgeCachedWordsForFont, nullptr);
}

/*static*/
PLDHashOperator
gfxFontCache::ClearCachedWordsForFont(HashEntry* aHashEntry, void* aUserData)
{
    aHashEntry->mFont->ClearCachedWords();
    return PL_DHASH_NEXT;
}

/*static*/
size_t
gfxFontCache::SizeOfFontEntryExcludingThis(HashEntry*        aHashEntry,
                                           nsMallocSizeOfFun aMallocSizeOf,
                                           void*             aUserArg)
{
    HashEntry *entry = static_cast<HashEntry*>(aHashEntry);
    FontCacheSizes *sizes = static_cast<FontCacheSizes*>(aUserArg);
    entry->mFont->SizeOfExcludingThis(aMallocSizeOf, sizes);

    // The font records its size in the |sizes| parameter, so we return zero
    // here to the hashtable enumerator.
    return 0;
}

void
gfxFontCache::SizeOfExcludingThis(nsMallocSizeOfFun aMallocSizeOf,
                                  FontCacheSizes*   aSizes) const
{
    // TODO: add the overhead of the expiration tracker (generation arrays)

    mFonts.SizeOfExcludingThis(SizeOfFontEntryExcludingThis,
                               aMallocSizeOf, aSizes);
}

void
gfxFontCache::SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf,
                                  FontCacheSizes*   aSizes) const
{
    aSizes->mFontInstances += aMallocSizeOf(this);
    SizeOfExcludingThis(aMallocSizeOf, aSizes);
}

/* static */ bool
gfxFontShaper::MergeFontFeatures(
    const nsTArray<gfxFontFeature>& aStyleRuleFeatures,
    const nsTArray<gfxFontFeature>& aFontFeatures,
    bool aDisableLigatures,
    nsDataHashtable<nsUint32HashKey,uint32_t>& aMergedFeatures)
{
    // bail immediately if nothing to do
    if (aStyleRuleFeatures.IsEmpty() &&
        aFontFeatures.IsEmpty() &&
        !aDisableLigatures) {
        return false;
    }

    aMergedFeatures.Init();

    // Ligature features are enabled by default in the generic shaper,
    // so we explicitly turn them off if necessary (for letter-spacing)
    if (aDisableLigatures) {
        aMergedFeatures.Put(HB_TAG('l','i','g','a'), 0);
        aMergedFeatures.Put(HB_TAG('c','l','i','g'), 0);
    }

    // add feature values from font
    uint32_t i, count;

    count = aFontFeatures.Length();
    for (i = 0; i < count; i++) {
        const gfxFontFeature& feature = aFontFeatures.ElementAt(i);
        aMergedFeatures.Put(feature.mTag, feature.mValue);
    }

    // add feature values from style rules
    count = aStyleRuleFeatures.Length();
    for (i = 0; i < count; i++) {
        const gfxFontFeature& feature = aStyleRuleFeatures.ElementAt(i);
        aMergedFeatures.Put(feature.mTag, feature.mValue);
    }

    return aMergedFeatures.Count() != 0;
}

void
gfxFont::RunMetrics::CombineWith(const RunMetrics& aOther, bool aOtherIsOnLeft)
{
    mAscent = NS_MAX(mAscent, aOther.mAscent);
    mDescent = NS_MAX(mDescent, aOther.mDescent);
    if (aOtherIsOnLeft) {
        mBoundingBox =
            (mBoundingBox + gfxPoint(aOther.mAdvanceWidth, 0)).Union(aOther.mBoundingBox);
    } else {
        mBoundingBox =
            mBoundingBox.Union(aOther.mBoundingBox + gfxPoint(mAdvanceWidth, 0));
    }
    mAdvanceWidth += aOther.mAdvanceWidth;
}

gfxFont::gfxFont(gfxFontEntry *aFontEntry, const gfxFontStyle *aFontStyle,
                 AntialiasOption anAAOption, cairo_scaled_font_t *aScaledFont) :
    mScaledFont(aScaledFont),
    mFontEntry(aFontEntry), mIsValid(true),
    mApplySyntheticBold(false),
    mStyle(*aFontStyle),
    mAdjustedSize(0.0),
    mFUnitsConvFactor(0.0f),
    mAntialiasOption(anAAOption)
{
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
    ++gFontCount;
#endif
}

gfxFont::~gfxFont()
{
    uint32_t i, count = mGlyphExtentsArray.Length();
    // We destroy the contents of mGlyphExtentsArray explicitly instead of
    // using nsAutoPtr because VC++ can't deal with nsTArrays of nsAutoPtrs
    // of classes that lack a proper copy constructor
    for (i = 0; i < count; ++i) {
        delete mGlyphExtentsArray[i];
    }
}

/*static*/
PLDHashOperator
gfxFont::AgeCacheEntry(CacheHashEntry *aEntry, void *aUserData)
{
    if (!aEntry->mShapedWord) {
        NS_ASSERTION(aEntry->mShapedWord, "cache entry has no gfxShapedWord!");
        return PL_DHASH_REMOVE;
    }
    if (aEntry->mShapedWord->IncrementAge() == kShapedWordCacheMaxAge) {
        return PL_DHASH_REMOVE;
    }
    return PL_DHASH_NEXT;
}

hb_blob_t *
gfxFont::GetFontTable(uint32_t aTag) {
    hb_blob_t *blob;
    if (mFontEntry->GetExistingFontTable(aTag, &blob))
        return blob;

    FallibleTArray<uint8_t> buffer;
    bool haveTable = NS_SUCCEEDED(mFontEntry->GetFontTable(aTag, buffer));

    return mFontEntry->ShareFontTableAndGetBlob(aTag,
                                                haveTable ? &buffer : nullptr);
}

/**
 * A helper function in case we need to do any rounding or other
 * processing here.
 */
#define ToDeviceUnits(aAppUnits, aDevUnitsPerAppUnit) \
    (double(aAppUnits)*double(aDevUnitsPerAppUnit))

struct GlyphBuffer {
#define GLYPH_BUFFER_SIZE (2048/sizeof(cairo_glyph_t))
    cairo_glyph_t mGlyphBuffer[GLYPH_BUFFER_SIZE];
    unsigned int mNumGlyphs;

    GlyphBuffer()
        : mNumGlyphs(0) { }

    cairo_glyph_t *AppendGlyph() {
        return &mGlyphBuffer[mNumGlyphs++];
    }

    void Flush(cairo_t *aCR, gfxFont::DrawMode aDrawMode, bool aReverse,
               gfxTextObjectPaint *aObjectPaint,
               const gfxMatrix& aGlobalMatrix, bool aFinish = false) {
        // Ensure there's enough room for a glyph to be added to the buffer
        // and we actually have glyphs to draw
        if ((!aFinish && mNumGlyphs < GLYPH_BUFFER_SIZE) || !mNumGlyphs) {
            return;
        }

        if (aReverse) {
            for (uint32_t i = 0; i < mNumGlyphs/2; ++i) {
                cairo_glyph_t tmp = mGlyphBuffer[i];
                mGlyphBuffer[i] = mGlyphBuffer[mNumGlyphs - 1 - i];
                mGlyphBuffer[mNumGlyphs - 1 - i] = tmp;
            }
        }

        if (aDrawMode == gfxFont::GLYPH_PATH) {
            cairo_glyph_path(aCR, mGlyphBuffer, mNumGlyphs);
        } else {
            if (aDrawMode & gfxFont::GLYPH_FILL) {
                SAMPLE_LABEL("GlyphBuffer", "cairo_show_glyphs");
                nsRefPtr<gfxPattern> pattern;
                if (aObjectPaint &&
                    !!(pattern = aObjectPaint->GetFillPattern(aGlobalMatrix))) {
                    cairo_save(aCR);
                    cairo_set_source(aCR, pattern->CairoPattern());
                }

                cairo_show_glyphs(aCR, mGlyphBuffer, mNumGlyphs);

                if (pattern) {
                    cairo_restore(aCR);
                }
            }

            if (aDrawMode & gfxFont::GLYPH_STROKE) {
                nsRefPtr<gfxPattern> pattern;
                if (aObjectPaint &&
                    !!(pattern = aObjectPaint->GetStrokePattern(aGlobalMatrix))) {
                    cairo_save(aCR);
                    cairo_set_source(aCR, pattern->CairoPattern());
                }

                cairo_new_path(aCR);
                cairo_glyph_path(aCR, mGlyphBuffer, mNumGlyphs);
                cairo_stroke(aCR);

                if (pattern) {
                    cairo_restore(aCR);
                }
            }
        }

        mNumGlyphs = 0;
    }
#undef GLYPH_BUFFER_SIZE
};

static AntialiasMode Get2DAAMode(gfxFont::AntialiasOption aAAOption) {
  switch (aAAOption) {
  case gfxFont::kAntialiasSubpixel:
    return AA_SUBPIXEL;
  case gfxFont::kAntialiasGrayscale:
    return AA_GRAY;
  case gfxFont::kAntialiasNone:
    return AA_NONE;
  default:
    return AA_DEFAULT;
  }
}

struct GlyphBufferAzure {
#define GLYPH_BUFFER_SIZE (2048/sizeof(Glyph))
    Glyph mGlyphBuffer[GLYPH_BUFFER_SIZE];
    unsigned int mNumGlyphs;

    GlyphBufferAzure()
        : mNumGlyphs(0) { }

    Glyph *AppendGlyph() {
        return &mGlyphBuffer[mNumGlyphs++];
    }

    void Flush(DrawTarget *aDT, gfxTextObjectPaint *aObjectPaint, ScaledFont *aFont,
               gfxFont::DrawMode aDrawMode, bool aReverse, const GlyphRenderingOptions *aOptions,
               gfxContext *aThebesContext, const Matrix *aInvFontMatrix, const DrawOptions &aDrawOptions,
               bool aFinish = false)
    {
        // Ensure there's enough room for a glyph to be added to the buffer
        if ((!aFinish && mNumGlyphs < GLYPH_BUFFER_SIZE) || !mNumGlyphs) {
            return;
        }

        if (aReverse) {
            Glyph *begin = &mGlyphBuffer[0];
            Glyph *end = &mGlyphBuffer[mNumGlyphs];
            std::reverse(begin, end);
        }
        
        gfx::GlyphBuffer buf;
        buf.mGlyphs = mGlyphBuffer;
        buf.mNumGlyphs = mNumGlyphs;

        gfxContext::AzureState state = aThebesContext->CurrentState();
        if (aDrawMode & gfxFont::GLYPH_FILL) {
            if (state.pattern || aObjectPaint) {
                Pattern *pat;

                nsRefPtr<gfxPattern> fillPattern;
                if (!aObjectPaint ||
                    !(fillPattern = aObjectPaint->GetFillPattern(aThebesContext->CurrentMatrix()))) {
                    pat = state.pattern->GetPattern(aDT, state.patternTransformChanged ? &state.patternTransform : nullptr);
                } else {
                    pat = fillPattern->GetPattern(aDT);
                }

                Matrix saved;
                Matrix *mat = nullptr;
                if (aInvFontMatrix) {
                    // The brush matrix needs to be multiplied with the inverted matrix
                    // as well, to move the brush into the space of the glyphs. Before
                    // the render target transformation

                    // This relies on the returned Pattern not to be reused by
                    // others, but regenerated on GetPattern calls. This is true!
                    if (pat->GetType() == PATTERN_LINEAR_GRADIENT) {
                        mat = &static_cast<LinearGradientPattern*>(pat)->mMatrix;
                    } else if (pat->GetType() == PATTERN_RADIAL_GRADIENT) {
                        mat = &static_cast<RadialGradientPattern*>(pat)->mMatrix;
                    } else if (pat->GetType() == PATTERN_SURFACE) {
                        mat = &static_cast<SurfacePattern*>(pat)->mMatrix;
                    }

                    if (mat) {
                        saved = *mat;
                        *mat = (*mat) * (*aInvFontMatrix);
                    }
                }

                aDT->FillGlyphs(aFont, buf, *pat,
                                aDrawOptions, aOptions);

                if (mat) {
                    *mat = saved;
                }
            } else if (state.sourceSurface) {
                aDT->FillGlyphs(aFont, buf, SurfacePattern(state.sourceSurface,
                                                           EXTEND_CLAMP,
                                                           state.surfTransform),
                                aDrawOptions, aOptions);
            } else {
                aDT->FillGlyphs(aFont, buf, ColorPattern(state.color),
                                aDrawOptions, aOptions);
            }
        }
        if (aDrawMode & gfxFont::GLYPH_PATH) {
            aThebesContext->EnsurePathBuilder();
            aFont->CopyGlyphsToBuilder(buf, aThebesContext->mPathBuilder);
        }
        if (aDrawMode & gfxFont::GLYPH_STROKE) {
            RefPtr<Path> path = aFont->GetPathForGlyphs(buf, aDT);
            if (aObjectPaint) {
                nsRefPtr<gfxPattern> strokePattern =
                  aObjectPaint->GetStrokePattern(aThebesContext->CurrentMatrix());
                if (strokePattern) {
                    aDT->Stroke(path, *strokePattern->GetPattern(aDT), state.strokeOptions);
                }
            }
        }

        mNumGlyphs = 0;
    }
#undef GLYPH_BUFFER_SIZE
};

// Bug 674909. When synthetic bolding text by drawing twice, need to
// render using a pixel offset in device pixels, otherwise text
// doesn't appear bolded, it appears as if a bad text shadow exists
// when a non-identity transform exists.  Use an offset factor so that
// the second draw occurs at a constant offset in device pixels.

double
gfxFont::CalcXScale(gfxContext *aContext)
{
    // determine magnitude of a 1px x offset in device space
    gfxSize t = aContext->UserToDevice(gfxSize(1.0, 0.0));
    if (t.width == 1.0 && t.height == 0.0) {
        // short-circuit the most common case to avoid sqrt() and division
        return 1.0;
    }

    double m = sqrt(t.width * t.width + t.height * t.height);

    NS_ASSERTION(m != 0.0, "degenerate transform while synthetic bolding");
    if (m == 0.0) {
        return 0.0; // effectively disables offset
    }

    // scale factor so that offsets are 1px in device pixels
    return 1.0 / m;
}

void
gfxFont::Draw(gfxTextRun *aTextRun, uint32_t aStart, uint32_t aEnd,
              gfxContext *aContext, DrawMode aDrawMode, gfxPoint *aPt,
              Spacing *aSpacing, gfxTextObjectPaint *aObjectPaint)
{
    NS_ASSERTION(aDrawMode <= gfxFont::GLYPH_PATH, "GLYPH_PATH cannot be used with GLYPH_FILL or GLYPH_STROKE");

    if (aStart >= aEnd)
        return;

    const gfxTextRun::CompressedGlyph *charGlyphs = aTextRun->GetCharacterGlyphs();
    const uint32_t appUnitsPerDevUnit = aTextRun->GetAppUnitsPerDevUnit();
    const double devUnitsPerAppUnit = 1.0/double(appUnitsPerDevUnit);
    bool isRTL = aTextRun->IsRightToLeft();
    double direction = aTextRun->GetDirection();
    gfxMatrix globalMatrix = aContext->CurrentMatrix();

    bool haveSVGGlyphs = GetFontEntry()->TryGetSVGData();
    nsAutoPtr<gfxTextObjectPaint> objectPaint;
    if (haveSVGGlyphs && !aObjectPaint) {
        // If no pattern is specified for fill, use the current pattern
        NS_ASSERTION((aDrawMode & GLYPH_STROKE) == 0, "no pattern supplied for stroking text");
        nsRefPtr<gfxPattern> fillPattern = aContext->GetPattern();
        objectPaint = new SimpleTextObjectPaint(fillPattern, nullptr,
                                                aContext->CurrentMatrix());
        aObjectPaint = objectPaint;
    }

    // synthetic-bold strikes are each offset one device pixel in run direction
    // (these values are only needed if IsSyntheticBold() is true)
    double synBoldOnePixelOffset = 0;
    int32_t strikes = 0;
    if (IsSyntheticBold()) {
        double xscale = CalcXScale(aContext);
        synBoldOnePixelOffset = direction * xscale;
        // use as many strikes as needed for the the increased advance
        strikes = NS_lroundf(GetSyntheticBoldOffset() / xscale);
    }

    uint32_t i;
    // Current position in appunits
    double x = aPt->x;
    double y = aPt->y;

    cairo_t *cr = aContext->GetCairo();
    RefPtr<DrawTarget> dt = aContext->GetDrawTarget();

    if (aContext->IsCairo()) {
      bool success = SetupCairoFont(aContext);
      if (MOZ_UNLIKELY(!success))
          return;

      ::GlyphBuffer glyphs;
      cairo_glyph_t *glyph;

      if (aSpacing) {
          x += direction*aSpacing[0].mBefore;
      }
      for (i = aStart; i < aEnd; ++i) {
          const gfxTextRun::CompressedGlyph *glyphData = &charGlyphs[i];
          if (glyphData->IsSimpleGlyph()) {
              double advance = glyphData->GetSimpleAdvance();
              double glyphX;
              if (isRTL) {
                  x -= advance;
                  glyphX = x;
              } else {
                  glyphX = x;
                  x += advance;
              }

              if (haveSVGGlyphs) {
                  gfxPoint point(ToDeviceUnits(glyphX, devUnitsPerAppUnit),
                                 ToDeviceUnits(y, devUnitsPerAppUnit));
                  if (RenderSVGGlyph(aContext, point, aDrawMode,
                                     glyphData->GetSimpleGlyph(),
                                     aObjectPaint)) {
                      continue;
                  }
              }

              // Perhaps we should put a scale in the cairo context instead of
              // doing this scaling here...
              // Multiplying by the reciprocal may introduce tiny error here,
              // but we assume cairo is going to round coordinates at some stage
              // and this is faster
              glyph = glyphs.AppendGlyph();
              glyph->index = glyphData->GetSimpleGlyph();
              glyph->x = ToDeviceUnits(glyphX, devUnitsPerAppUnit);
              glyph->y = ToDeviceUnits(y, devUnitsPerAppUnit);
              glyphs.Flush(cr, aDrawMode, isRTL, aObjectPaint, globalMatrix);
            
              // synthetic bolding by multi-striking with 1-pixel offsets
              // at least once, more if there's room (large font sizes)
              if (IsSyntheticBold()) {
                  double strikeOffset = synBoldOnePixelOffset;
                  int32_t strikeCount = strikes;
                  do {
                      cairo_glyph_t *doubleglyph;
                      doubleglyph = glyphs.AppendGlyph();
                      doubleglyph->index = glyph->index;
                      doubleglyph->x =
                          ToDeviceUnits(glyphX + strikeOffset * appUnitsPerDevUnit,
                                        devUnitsPerAppUnit);
                      doubleglyph->y = glyph->y;
                      strikeOffset += synBoldOnePixelOffset;
                      glyphs.Flush(cr, aDrawMode, isRTL, aObjectPaint, globalMatrix);
                  } while (--strikeCount > 0);
              }
          } else {
              uint32_t glyphCount = glyphData->GetGlyphCount();
              if (glyphCount > 0) {
                  const gfxTextRun::DetailedGlyph *details =
                      aTextRun->GetDetailedGlyphs(i);
                  NS_ASSERTION(details, "detailedGlyph should not be missing!");
                  for (uint32_t j = 0; j < glyphCount; ++j, ++details) {
                      double advance = details->mAdvance;
                      if (glyphData->IsMissing()) {
                          // default ignorable characters will have zero advance width.
                          // we don't have to draw the hexbox for them
                          if (aDrawMode != gfxFont::GLYPH_PATH && advance > 0) {
                              double glyphX = x;
                              if (isRTL) {
                                  glyphX -= advance;
                              }
                              gfxPoint pt(ToDeviceUnits(glyphX, devUnitsPerAppUnit),
                                          ToDeviceUnits(y, devUnitsPerAppUnit));
                              gfxFloat advanceDevUnits = ToDeviceUnits(advance, devUnitsPerAppUnit);
                              gfxFloat height = GetMetrics().maxAscent;
                              gfxRect glyphRect(pt.x, pt.y - height, advanceDevUnits, height);
                              gfxFontMissingGlyphs::DrawMissingGlyph(aContext,
                                                                     glyphRect,
                                                                     details->mGlyphID);
                          }
                      } else {
                          double glyphX = x + details->mXOffset;
                          if (isRTL) {
                              glyphX -= advance;
                          }

                          gfxPoint point(ToDeviceUnits(glyphX, devUnitsPerAppUnit),
                                         ToDeviceUnits(y, devUnitsPerAppUnit));

                          if (!haveSVGGlyphs ||
                              !RenderSVGGlyph(aContext, point, aDrawMode,
                                              details->mGlyphID, aObjectPaint)) {
                              glyph = glyphs.AppendGlyph();
                              glyph->index = details->mGlyphID;
                              glyph->x = ToDeviceUnits(glyphX, devUnitsPerAppUnit);
                              glyph->y = ToDeviceUnits(y + details->mYOffset, devUnitsPerAppUnit);
                              glyphs.Flush(cr, aDrawMode, isRTL, aObjectPaint, globalMatrix);

                              if (IsSyntheticBold()) {
                                  double strikeOffset = synBoldOnePixelOffset;
                                  int32_t strikeCount = strikes;
                                  do {
                                      cairo_glyph_t *doubleglyph;
                                      doubleglyph = glyphs.AppendGlyph();
                                      doubleglyph->index = glyph->index;
                                      doubleglyph->x =
                                          ToDeviceUnits(glyphX + strikeOffset *
                                                  appUnitsPerDevUnit,
                                                  devUnitsPerAppUnit);
                                      doubleglyph->y = glyph->y;
                                      strikeOffset += synBoldOnePixelOffset;
                                      glyphs.Flush(cr, aDrawMode, isRTL, aObjectPaint, globalMatrix);
                                  } while (--strikeCount > 0);
                              }
                          }
                      }
                      x += direction*advance;
                  }
              }
          }

          if (aSpacing) {
              double space = aSpacing[i - aStart].mAfter;
              if (i + 1 < aEnd) {
                  space += aSpacing[i + 1 - aStart].mBefore;
              }
              x += direction*space;
          }
      }

      if (gfxFontTestStore::CurrentStore()) {
          /* This assumes that the tests won't have anything that results
           * in more than GLYPH_BUFFER_SIZE glyphs.  Do this before we
           * flush, since that'll blow away the num_glyphs.
           */
          gfxFontTestStore::CurrentStore()->AddItem(GetName(),
                                                    glyphs.mGlyphBuffer,
                                                    glyphs.mNumGlyphs);
      }

      // draw any remaining glyphs
      glyphs.Flush(cr, aDrawMode, isRTL, aObjectPaint, globalMatrix, true);

    } else {
      RefPtr<ScaledFont> scaledFont = GetScaledFont(dt);

      if (!scaledFont) {
        return;
      }

      bool oldSubpixelAA = dt->GetPermitSubpixelAA();

      if (!AllowSubpixelAA()) {
          dt->SetPermitSubpixelAA(false);
      }

      GlyphBufferAzure glyphs;
      Glyph *glyph;

      Matrix mat, matInv;
      Matrix oldMat = dt->GetTransform();

      // This is NULL when we have inverse-transformed glyphs and we need to
      // transform the Brush inside flush.
      Matrix *passedInvMatrix = nullptr;

      RefPtr<GlyphRenderingOptions> renderingOptions =
        GetGlyphRenderingOptions();

      DrawOptions drawOptions;
      drawOptions.mAntialiasMode = Get2DAAMode(mAntialiasOption);

      if (mScaledFont) {
        cairo_matrix_t matrix;
        cairo_scaled_font_get_font_matrix(mScaledFont, &matrix);
        if (matrix.xy != 0) {
          // If this matrix applies a skew, which can happen when drawing
          // oblique fonts, we will set the DrawTarget matrix to apply the
          // skew. We'll need to move the glyphs by the inverse of the skew to
          // get the glyphs positioned correctly in the new device space
          // though, since the font matrix should only be applied to drawing
          // the glyphs, and not to their position.
          mat = ToMatrix(*reinterpret_cast<gfxMatrix*>(&matrix));

          mat._11 = mat._22 = 1.0;
          mat._21 /= mAdjustedSize;

          dt->SetTransform(mat * oldMat);

          matInv = mat;
          matInv.Invert();

          passedInvMatrix = &matInv;
        }
      }

      if (aSpacing) {
          x += direction*aSpacing[0].mBefore;
      }
      for (i = aStart; i < aEnd; ++i) {
          const gfxTextRun::CompressedGlyph *glyphData = &charGlyphs[i];
          if (glyphData->IsSimpleGlyph()) {
              double advance = glyphData->GetSimpleAdvance();
              double glyphX;
              if (isRTL) {
                  x -= advance;
                  glyphX = x;
              } else {
                  glyphX = x;
                  x += advance;
              }

              if (haveSVGGlyphs) {
                  gfxPoint point(ToDeviceUnits(glyphX, devUnitsPerAppUnit),
                                 ToDeviceUnits(y, devUnitsPerAppUnit));
                  if (RenderSVGGlyph(aContext, point, aDrawMode,
                                     glyphData->GetSimpleGlyph(),
                                     aObjectPaint)) {
                      continue;
                  }
              }

              // Perhaps we should put a scale in the cairo context instead of
              // doing this scaling here...
              // Multiplying by the reciprocal may introduce tiny error here,
              // but we assume cairo is going to round coordinates at some stage
              // and this is faster
              glyph = glyphs.AppendGlyph();
              glyph->mIndex = glyphData->GetSimpleGlyph();
              glyph->mPosition.x = ToDeviceUnits(glyphX, devUnitsPerAppUnit);
              glyph->mPosition.y = ToDeviceUnits(y, devUnitsPerAppUnit);
              glyph->mPosition = matInv * glyph->mPosition;
              glyphs.Flush(dt, aObjectPaint, scaledFont,
                           aDrawMode, isRTL, renderingOptions,
                           aContext, passedInvMatrix,
                           drawOptions);

              // synthetic bolding by multi-striking with 1-pixel offsets
              // at least once, more if there's room (large font sizes)
              if (IsSyntheticBold()) {
                  double strikeOffset = synBoldOnePixelOffset;
                  int32_t strikeCount = strikes;
                  do {
                      Glyph *doubleglyph;
                      doubleglyph = glyphs.AppendGlyph();
                      doubleglyph->mIndex = glyph->mIndex;
                      doubleglyph->mPosition.x =
                          ToDeviceUnits(glyphX + strikeOffset * appUnitsPerDevUnit,
                                        devUnitsPerAppUnit);
                      doubleglyph->mPosition.y = glyph->mPosition.y;
                      doubleglyph->mPosition = matInv * doubleglyph->mPosition;
                      strikeOffset += synBoldOnePixelOffset;
                      glyphs.Flush(dt, aObjectPaint, scaledFont,
                                   aDrawMode, isRTL, renderingOptions,
                                   aContext, passedInvMatrix,
                                   drawOptions);
                  } while (--strikeCount > 0);
              }
          } else {
              uint32_t glyphCount = glyphData->GetGlyphCount();
              if (glyphCount > 0) {
                  const gfxTextRun::DetailedGlyph *details =
                      aTextRun->GetDetailedGlyphs(i);
                  NS_ASSERTION(details, "detailedGlyph should not be missing!");
                  for (uint32_t j = 0; j < glyphCount; ++j, ++details) {
                      double advance = details->mAdvance;
                      if (glyphData->IsMissing()) {
                          // default ignorable characters will have zero advance width.
                          // we don't have to draw the hexbox for them
                          if (aDrawMode != gfxFont::GLYPH_PATH && advance > 0) {
                              double glyphX = x;
                              if (isRTL) {
                                  glyphX -= advance;
                              }
                              gfxPoint pt(ToDeviceUnits(glyphX, devUnitsPerAppUnit),
                                          ToDeviceUnits(y, devUnitsPerAppUnit));
                              gfxFloat advanceDevUnits = ToDeviceUnits(advance, devUnitsPerAppUnit);
                              gfxFloat height = GetMetrics().maxAscent;
                              gfxRect glyphRect(pt.x, pt.y - height, advanceDevUnits, height);
                              gfxFontMissingGlyphs::DrawMissingGlyph(aContext,
                                                                     glyphRect,
                                                                     details->mGlyphID);
                          }
                      } else {
                          double glyphX = x + details->mXOffset;
                          if (isRTL) {
                              glyphX -= advance;
                          }

                          gfxPoint point(ToDeviceUnits(glyphX, devUnitsPerAppUnit),
                                         ToDeviceUnits(y, devUnitsPerAppUnit));

                          if (!haveSVGGlyphs ||
                              !RenderSVGGlyph(aContext, point, aDrawMode,
                                              details->mGlyphID, aObjectPaint)) {
                              glyph = glyphs.AppendGlyph();
                              glyph->mIndex = details->mGlyphID;
                              glyph->mPosition.x = ToDeviceUnits(glyphX, devUnitsPerAppUnit);
                              glyph->mPosition.y = ToDeviceUnits(y + details->mYOffset, devUnitsPerAppUnit);
                              glyph->mPosition = matInv * glyph->mPosition;
                              glyphs.Flush(dt, aObjectPaint, scaledFont, aDrawMode,
                                           isRTL, renderingOptions, aContext, passedInvMatrix,
                                           drawOptions);

                              if (IsSyntheticBold()) {
                                  double strikeOffset = synBoldOnePixelOffset;
                                  int32_t strikeCount = strikes;
                                  do {
                                      Glyph *doubleglyph;
                                      doubleglyph = glyphs.AppendGlyph();
                                      doubleglyph->mIndex = glyph->mIndex;
                                      doubleglyph->mPosition.x =
                                          ToDeviceUnits(glyphX + strikeOffset *
                                                        appUnitsPerDevUnit,
                                                        devUnitsPerAppUnit);
                                      doubleglyph->mPosition.y = glyph->mPosition.y;
                                      strikeOffset += synBoldOnePixelOffset;
                                      doubleglyph->mPosition = matInv * doubleglyph->mPosition;
                                      glyphs.Flush(dt, aObjectPaint, scaledFont,
                                                   aDrawMode, isRTL, renderingOptions,
                                                   aContext, passedInvMatrix, drawOptions);
                                  } while (--strikeCount > 0);
                              }
                          }
                      }
                      x += direction*advance;
                  }
              }
          }

          if (aSpacing) {
              double space = aSpacing[i - aStart].mAfter;
              if (i + 1 < aEnd) {
                  space += aSpacing[i + 1 - aStart].mBefore;
              }
              x += direction*space;
          }
      }

      glyphs.Flush(dt, aObjectPaint, scaledFont, aDrawMode, isRTL,
                   renderingOptions, aContext, passedInvMatrix,
                   drawOptions, true);

      dt->SetTransform(oldMat);

      dt->SetPermitSubpixelAA(oldSubpixelAA);
    }

    *aPt = gfxPoint(x, y);
}

bool
gfxFont::RenderSVGGlyph(gfxContext *aContext, gfxPoint aPoint, DrawMode aDrawMode,
                        uint32_t aGlyphId, gfxTextObjectPaint *aObjectPaint)
{
    if (!GetFontEntry()->HasSVGGlyph(aGlyphId)) {
        return false;
    }

    const gfxFloat devUnitsPerSVGUnit = GetStyle()->size / gfxSVGGlyphs::SVG_UNITS_PER_EM;
    gfxContextMatrixAutoSaveRestore matrixRestore(aContext);

    aContext->Translate(gfxPoint(aPoint.x, aPoint.y));
    aContext->Scale(devUnitsPerSVGUnit, devUnitsPerSVGUnit);

    aObjectPaint->InitStrokeGeometry(aContext, devUnitsPerSVGUnit);

    return GetFontEntry()->RenderSVGGlyph(aContext, aGlyphId, aDrawMode,
                                          aObjectPaint);
}

static void
UnionRange(gfxFloat aX, gfxFloat* aDestMin, gfxFloat* aDestMax)
{
    *aDestMin = NS_MIN(*aDestMin, aX);
    *aDestMax = NS_MAX(*aDestMax, aX);
}

// We get precise glyph extents if the textrun creator requested them, or
// if the font is a user font --- in which case the author may be relying
// on overflowing glyphs.
static bool
NeedsGlyphExtents(gfxFont *aFont, gfxTextRun *aTextRun)
{
    return (aTextRun->GetFlags() & gfxTextRunFactory::TEXT_NEED_BOUNDING_BOX) ||
        aFont->GetFontEntry()->IsUserFont();
}

static bool
NeedsGlyphExtents(gfxTextRun *aTextRun)
{
    if (aTextRun->GetFlags() & gfxTextRunFactory::TEXT_NEED_BOUNDING_BOX)
        return true;
    uint32_t numRuns;
    const gfxTextRun::GlyphRun *glyphRuns = aTextRun->GetGlyphRuns(&numRuns);
    for (uint32_t i = 0; i < numRuns; ++i) {
        if (glyphRuns[i].mFont->GetFontEntry()->IsUserFont())
            return true;
    }
    return false;
}

gfxFont::RunMetrics
gfxFont::Measure(gfxTextRun *aTextRun,
                 uint32_t aStart, uint32_t aEnd,
                 BoundingBoxType aBoundingBoxType,
                 gfxContext *aRefContext,
                 Spacing *aSpacing)
{
    // If aBoundingBoxType is TIGHT_HINTED_OUTLINE_EXTENTS
    // and the underlying cairo font may be antialiased,
    // we need to create a copy in order to avoid getting cached extents.
    // This is only used by MathML layout at present.
    if (aBoundingBoxType == TIGHT_HINTED_OUTLINE_EXTENTS &&
        mAntialiasOption != kAntialiasNone) {
        if (!mNonAAFont) {
            mNonAAFont = CopyWithAntialiasOption(kAntialiasNone);
        }
        // if font subclass doesn't implement CopyWithAntialiasOption(),
        // it will return null and we'll proceed to use the existing font
        if (mNonAAFont) {
            return mNonAAFont->Measure(aTextRun, aStart, aEnd,
                                       TIGHT_HINTED_OUTLINE_EXTENTS,
                                       aRefContext, aSpacing);
        }
    }

    const uint32_t appUnitsPerDevUnit = aTextRun->GetAppUnitsPerDevUnit();
    // Current position in appunits
    const gfxFont::Metrics& fontMetrics = GetMetrics();

    RunMetrics metrics;
    metrics.mAscent = fontMetrics.maxAscent*appUnitsPerDevUnit;
    metrics.mDescent = fontMetrics.maxDescent*appUnitsPerDevUnit;
    if (aStart == aEnd) {
        // exit now before we look at aSpacing[0], which is undefined
        metrics.mBoundingBox = gfxRect(0, -metrics.mAscent, 0, metrics.mAscent + metrics.mDescent);
        return metrics;
    }

    gfxFloat advanceMin = 0, advanceMax = 0;
    const gfxTextRun::CompressedGlyph *charGlyphs = aTextRun->GetCharacterGlyphs();
    bool isRTL = aTextRun->IsRightToLeft();
    double direction = aTextRun->GetDirection();
    bool needsGlyphExtents = NeedsGlyphExtents(this, aTextRun);
    gfxGlyphExtents *extents =
        (aBoundingBoxType == LOOSE_INK_EXTENTS &&
            !needsGlyphExtents &&
            !aTextRun->HasDetailedGlyphs()) ? nullptr
        : GetOrCreateGlyphExtents(aTextRun->GetAppUnitsPerDevUnit());
    double x = 0;
    if (aSpacing) {
        x += direction*aSpacing[0].mBefore;
    }
    uint32_t i;
    for (i = aStart; i < aEnd; ++i) {
        const gfxTextRun::CompressedGlyph *glyphData = &charGlyphs[i];
        if (glyphData->IsSimpleGlyph()) {
            double advance = glyphData->GetSimpleAdvance();
            // Only get the real glyph horizontal extent if we were asked
            // for the tight bounding box or we're in quality mode
            if ((aBoundingBoxType != LOOSE_INK_EXTENTS || needsGlyphExtents) &&
                extents) {
                uint32_t glyphIndex = glyphData->GetSimpleGlyph();
                uint16_t extentsWidth = extents->GetContainedGlyphWidthAppUnits(glyphIndex);
                if (extentsWidth != gfxGlyphExtents::INVALID_WIDTH &&
                    aBoundingBoxType == LOOSE_INK_EXTENTS) {
                    UnionRange(x, &advanceMin, &advanceMax);
                    UnionRange(x + direction*extentsWidth, &advanceMin, &advanceMax);
                } else {
                    gfxRect glyphRect;
                    if (!extents->GetTightGlyphExtentsAppUnits(this,
                            aRefContext, glyphIndex, &glyphRect)) {
                        glyphRect = gfxRect(0, metrics.mBoundingBox.Y(),
                            advance, metrics.mBoundingBox.Height());
                    }
                    if (isRTL) {
                        glyphRect -= gfxPoint(advance, 0);
                    }
                    glyphRect += gfxPoint(x, 0);
                    metrics.mBoundingBox = metrics.mBoundingBox.Union(glyphRect);
                }
            }
            x += direction*advance;
        } else {
            uint32_t glyphCount = glyphData->GetGlyphCount();
            if (glyphCount > 0) {
                const gfxTextRun::DetailedGlyph *details =
                    aTextRun->GetDetailedGlyphs(i);
                NS_ASSERTION(details != nullptr,
                             "detaiedGlyph record should not be missing!");
                uint32_t j;
                for (j = 0; j < glyphCount; ++j, ++details) {
                    uint32_t glyphIndex = details->mGlyphID;
                    gfxPoint glyphPt(x + details->mXOffset, details->mYOffset);
                    double advance = details->mAdvance;
                    gfxRect glyphRect;
                    if (glyphData->IsMissing() || !extents ||
                        !extents->GetTightGlyphExtentsAppUnits(this,
                                aRefContext, glyphIndex, &glyphRect)) {
                        // We might have failed to get glyph extents due to
                        // OOM or something
                        glyphRect = gfxRect(0, -metrics.mAscent,
                            advance, metrics.mAscent + metrics.mDescent);
                    }
                    if (isRTL) {
                        glyphRect -= gfxPoint(advance, 0);
                    }
                    glyphRect += gfxPoint(x, 0);
                    metrics.mBoundingBox = metrics.mBoundingBox.Union(glyphRect);
                    x += direction*advance;
                }
            }
        }
        // Every other glyph type is ignored
        if (aSpacing) {
            double space = aSpacing[i - aStart].mAfter;
            if (i + 1 < aEnd) {
                space += aSpacing[i + 1 - aStart].mBefore;
            }
            x += direction*space;
        }
    }

    if (aBoundingBoxType == LOOSE_INK_EXTENTS) {
        UnionRange(x, &advanceMin, &advanceMax);
        gfxRect fontBox(advanceMin, -metrics.mAscent,
                        advanceMax - advanceMin, metrics.mAscent + metrics.mDescent);
        metrics.mBoundingBox = metrics.mBoundingBox.Union(fontBox);
    }
    if (isRTL) {
        metrics.mBoundingBox -= gfxPoint(x, 0);
    }

    metrics.mAdvanceWidth = x*direction;
    return metrics;
}

#define MAX_SHAPING_LENGTH  32760 // slightly less than 32K, trying to avoid
                                  // over-stressing platform shapers

#define BACKTRACK_LIMIT  1024 // If we can't find a space or a cluster start
                              // within 1K chars, just chop arbitrarily.
                              // Limiting backtrack here avoids pathological
                              // behavior on long runs with no whitespace.

static bool
IsBoundarySpace(PRUnichar aChar, PRUnichar aNextChar)
{
    return (aChar == ' ' || aChar == 0x00A0) && !IsClusterExtender(aNextChar);
}

static inline uint32_t
HashMix(uint32_t aHash, PRUnichar aCh)
{
    return (aHash >> 28) ^ (aHash << 4) ^ aCh;
}

template<typename T>
gfxShapedWord*
gfxFont::GetShapedWord(gfxContext *aContext,
                       const T *aText,
                       uint32_t aLength,
                       uint32_t aHash,
                       int32_t aRunScript,
                       int32_t aAppUnitsPerDevUnit,
                       uint32_t aFlags)
{
    // if the cache is getting too big, flush it and start over
    if (mWordCache.Count() > 10000) {
        NS_WARNING("flushing shaped-word cache");
        ClearCachedWords();
    }

    // if there's a cached entry for this word, just return it
    CacheHashKey key(aText, aLength, aHash,
                     aRunScript,
                     aAppUnitsPerDevUnit,
                     aFlags);

    CacheHashEntry *entry = mWordCache.PutEntry(key);
    if (!entry) {
        NS_WARNING("failed to create word cache entry - expect missing text");
        return nullptr;
    }
    gfxShapedWord *sw = entry->mShapedWord;

    if (sw) {
        sw->ResetAge();
        Telemetry::Accumulate(Telemetry::WORD_CACHE_HITS, aLength);
        return sw;
    }

    Telemetry::Accumulate(Telemetry::WORD_CACHE_MISSES, aLength);
    sw = entry->mShapedWord = gfxShapedWord::Create(aText, aLength,
                                                    aRunScript,
                                                    aAppUnitsPerDevUnit,
                                                    aFlags);
    if (!sw) {
        NS_WARNING("failed to create gfxShapedWord - expect missing text");
        return nullptr;
    }

    DebugOnly<bool> ok = false;
    if (sizeof(T) == sizeof(PRUnichar)) {
        ok = ShapeWord(aContext, sw, (const PRUnichar*)aText);
    } else {
        nsAutoString utf16;
        AppendASCIItoUTF16(nsDependentCSubstring((const char*)aText, aLength),
                           utf16);
        if (utf16.Length() == aLength) {
            ok = ShapeWord(aContext, sw, utf16.BeginReading());
        }
    }
    NS_WARN_IF_FALSE(ok, "failed to shape word - expect garbled text");

    for (uint32_t i = 0; i < aLength; ++i) {
        if (aText[i] == ' ') {
            sw->SetIsSpace(i);
        } else if (i > 0 &&
                   NS_IS_HIGH_SURROGATE(aText[i - 1]) &&
                   NS_IS_LOW_SURROGATE(aText[i])) {
            sw->SetIsLowSurrogate(i);
        }
    }

    return sw;
}

bool
gfxFont::CacheHashEntry::KeyEquals(const KeyTypePointer aKey) const
{
    const gfxShapedWord *sw = mShapedWord;
    if (!sw) {
        return false;
    }
    if (sw->Length() != aKey->mLength ||
        sw->Flags() != aKey->mFlags ||
        sw->AppUnitsPerDevUnit() != aKey->mAppUnitsPerDevUnit ||
        sw->Script() != aKey->mScript) {
        return false;
    }
    if (sw->TextIs8Bit()) {
        if (aKey->mTextIs8Bit) {
            return (0 == memcmp(sw->Text8Bit(), aKey->mText.mSingle,
                                aKey->mLength * sizeof(uint8_t)));
        }
        // The key has 16-bit text, even though all the characters are < 256,
        // so the TEXT_IS_8BIT flag was set and the cached ShapedWord we're
        // comparing with will have 8-bit text.
        const uint8_t   *s1 = sw->Text8Bit();
        const PRUnichar *s2 = aKey->mText.mDouble;
        const PRUnichar *s2end = s2 + aKey->mLength;
        while (s2 < s2end) {
            if (*s1++ != *s2++) {
                return false;
            }
        }
        return true;
    }
    NS_ASSERTION((aKey->mFlags & gfxTextRunFactory::TEXT_IS_8BIT) == 0 &&
                 !aKey->mTextIs8Bit, "didn't expect 8-bit text here");
    return (0 == memcmp(sw->TextUnicode(), aKey->mText.mDouble,
                        aKey->mLength * sizeof(PRUnichar)));
}

bool
gfxFont::ShapeWord(gfxContext *aContext,
                   gfxShapedWord *aShapedWord,
                   const PRUnichar *aText,
                   bool aPreferPlatformShaping)
{
    bool ok = false;

#ifdef MOZ_GRAPHITE
    if (mGraphiteShaper && gfxPlatform::GetPlatform()->UseGraphiteShaping()) {
        ok = mGraphiteShaper->ShapeWord(aContext, aShapedWord, aText);
    }
#endif

    if (!ok && mHarfBuzzShaper && !aPreferPlatformShaping) {
        if (gfxPlatform::GetPlatform()->UseHarfBuzzForScript(aShapedWord->Script())) {
            ok = mHarfBuzzShaper->ShapeWord(aContext, aShapedWord, aText);
        }
    }

    if (!ok) {
        if (!mPlatformShaper) {
            CreatePlatformShaper();
            NS_ASSERTION(mPlatformShaper, "no platform shaper available!");
        }
        if (mPlatformShaper) {
            ok = mPlatformShaper->ShapeWord(aContext, aShapedWord, aText);
        }
    }

    if (ok && IsSyntheticBold()) {
        float synBoldOffset =
                GetSyntheticBoldOffset() * CalcXScale(aContext);
        aShapedWord->AdjustAdvancesForSyntheticBold(synBoldOffset);
    }

    return ok;
}

inline static bool IsChar8Bit(uint8_t /*aCh*/) { return true; }
inline static bool IsChar8Bit(PRUnichar aCh) { return aCh < 0x100; }

template<typename T>
bool
gfxFont::SplitAndInitTextRun(gfxContext *aContext,
                             gfxTextRun *aTextRun,
                             const T *aString,
                             uint32_t aRunStart,
                             uint32_t aRunLength,
                             int32_t aRunScript)
{
    if (aRunLength == 0) {
        return true;
    }

    InitWordCache();

    // the only flags we care about for ShapedWord construction/caching
    uint32_t flags = aTextRun->GetFlags() &
        (gfxTextRunFactory::TEXT_IS_RTL |
         gfxTextRunFactory::TEXT_DISABLE_OPTIONAL_LIGATURES);
    if (sizeof(T) == sizeof(uint8_t)) {
        flags |= gfxTextRunFactory::TEXT_IS_8BIT;
    }

    const T *text = aString + aRunStart;
    uint32_t wordStart = 0;
    uint32_t hash = 0;
    bool wordIs8Bit = true;
    int32_t appUnitsPerDevUnit = aTextRun->GetAppUnitsPerDevUnit();

    T nextCh = text[0];
    for (uint32_t i = 0; i <= aRunLength; ++i) {
        T ch = nextCh;
        nextCh = (i < aRunLength - 1) ? text[i + 1] : '\n';
        bool boundary = IsBoundarySpace(ch, nextCh);
        bool invalid = !boundary && gfxFontGroup::IsInvalidChar(ch);
        uint32_t length = i - wordStart;

        // break into separate ShapedWords when we hit an invalid char,
        // or a boundary space (always handled individually),
        // or the first non-space after a space
        bool breakHere = boundary || invalid;

        if (!breakHere) {
            // if we're approaching the max ShapedWord length, break anyway...
            if (sizeof(T) == sizeof(uint8_t)) {
                // in 8-bit text, no clusters or surrogates to worry about
                if (length >= gfxShapedWord::kMaxLength) {
                    breakHere = true;
                }
            } else {
                // try to avoid breaking before combining mark or low surrogate
                if (length >= gfxShapedWord::kMaxLength - 15) {
                    if (!NS_IS_LOW_SURROGATE(ch)) {
                        if (!IsClusterExtender(ch)) {
                            breakHere = true;
                        }
                    }
                    if (!breakHere && length >= gfxShapedWord::kMaxLength - 3) {
                        if (!NS_IS_LOW_SURROGATE(ch)) {
                            breakHere = true;
                        }
                    }
                    if (!breakHere && length >= gfxShapedWord::kMaxLength) {
                        breakHere = true;
                    }
                }
            }
        }

        if (!breakHere) {
            if (!IsChar8Bit(ch)) {
                wordIs8Bit = false;
            }
            // include this character in the hash, and move on to next
            hash = HashMix(hash, ch);
            continue;
        }

        // We've decided to break here (i.e. we're at the end of a "word",
        // or the word is becoming excessively long): shape the word and
        // add it to the textrun
        if (length > 0) {
            uint32_t wordFlags = flags;
            // in the 8-bit version of this method, TEXT_IS_8BIT was
            // already set as part of |flags|, so no need for a per-word
            // adjustment here
            if (sizeof(T) == sizeof(PRUnichar)) {
                if (wordIs8Bit) {
                    wordFlags |= gfxTextRunFactory::TEXT_IS_8BIT;
                }
            }
            gfxShapedWord *sw = GetShapedWord(aContext,
                                              text + wordStart, length,
                                              hash, aRunScript,
                                              appUnitsPerDevUnit,
                                              wordFlags);
            if (sw) {
                aTextRun->CopyGlyphDataFrom(sw, aRunStart + wordStart);
            } else {
                return false; // failed, presumably out of memory?
            }
        }

        if (boundary) {
            // word was terminated by a space: add that to the textrun
            if (!aTextRun->SetSpaceGlyphIfSimple(this, aContext,
                                                 aRunStart + i, ch))
            {
                static const uint8_t space = ' ';
                gfxShapedWord *sw =
                    GetShapedWord(aContext,
                                  &space, 1,
                                  HashMix(0, ' '), aRunScript,
                                  appUnitsPerDevUnit,
                                  flags | gfxTextRunFactory::TEXT_IS_8BIT);
                if (sw) {
                    aTextRun->CopyGlyphDataFrom(sw, aRunStart + i);
                } else {
                    return false;
                }
            }
            hash = 0;
            wordStart = i + 1;
            wordIs8Bit = true;
            continue;
        }

        if (i == aRunLength) {
            break;
        }

        if (invalid) {
            // word was terminated by an invalid char: skip it,
            // but record where TAB or NEWLINE occur
            if (ch == '\t') {
                aTextRun->SetIsTab(aRunStart + i);
            } else if (ch == '\n') {
                aTextRun->SetIsNewline(aRunStart + i);
            }
            hash = 0;
            wordStart = i + 1;
            wordIs8Bit = true;
            continue;
        }

        // word was forcibly broken, so current char will begin next word
        hash = HashMix(0, ch);
        wordStart = i;
        wordIs8Bit = IsChar8Bit(ch);
    }

    return true;
}

gfxGlyphExtents *
gfxFont::GetOrCreateGlyphExtents(uint32_t aAppUnitsPerDevUnit) {
    uint32_t i, count = mGlyphExtentsArray.Length();
    for (i = 0; i < count; ++i) {
        if (mGlyphExtentsArray[i]->GetAppUnitsPerDevUnit() == aAppUnitsPerDevUnit)
            return mGlyphExtentsArray[i];
    }
    gfxGlyphExtents *glyphExtents = new gfxGlyphExtents(aAppUnitsPerDevUnit);
    if (glyphExtents) {
        mGlyphExtentsArray.AppendElement(glyphExtents);
        // Initialize the extents of a space glyph, assuming that spaces don't
        // render anything!
        glyphExtents->SetContainedGlyphWidthAppUnits(GetSpaceGlyph(), 0);
    }
    return glyphExtents;
}

void
gfxFont::SetupGlyphExtents(gfxContext *aContext, uint32_t aGlyphID, bool aNeedTight,
                           gfxGlyphExtents *aExtents)
{
    gfxContextMatrixAutoSaveRestore matrixRestore(aContext);
    aContext->IdentityMatrix();
    cairo_glyph_t glyph;
    glyph.index = aGlyphID;
    glyph.x = 0;
    glyph.y = 0;
    cairo_text_extents_t extents;
    cairo_glyph_extents(aContext->GetCairo(), &glyph, 1, &extents);

    const Metrics& fontMetrics = GetMetrics();
    uint32_t appUnitsPerDevUnit = aExtents->GetAppUnitsPerDevUnit();
    if (!aNeedTight && extents.x_bearing >= 0 &&
        extents.y_bearing >= -fontMetrics.maxAscent &&
        extents.height + extents.y_bearing <= fontMetrics.maxDescent) {
        uint32_t appUnitsWidth =
            uint32_t(ceil((extents.x_bearing + extents.width)*appUnitsPerDevUnit));
        if (appUnitsWidth < gfxGlyphExtents::INVALID_WIDTH) {
            aExtents->SetContainedGlyphWidthAppUnits(aGlyphID, uint16_t(appUnitsWidth));
            return;
        }
    }
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
    if (!aNeedTight) {
        ++gGlyphExtentsSetupFallBackToTight;
    }
#endif

    gfxFloat d2a = appUnitsPerDevUnit;
    gfxRect bounds(extents.x_bearing*d2a, extents.y_bearing*d2a,
                   extents.width*d2a, extents.height*d2a);

    gfxRect svgBounds;
    if (mFontEntry->TryGetSVGData() &&
        mFontEntry->HasSVGGlyph(aGlyphID) &&
        mFontEntry->GetSVGGlyphExtents(aContext, aGlyphID, &svgBounds)) {

        bounds = bounds.Union(gfxRect(svgBounds.x * d2a, svgBounds.y * d2a,
                                      svgBounds.width * d2a, svgBounds.height * d2a));
    }

    aExtents->SetTightGlyphExtents(aGlyphID, bounds);
}

// Try to initialize font metrics by reading sfnt tables directly;
// set mIsValid=TRUE and return TRUE on success.
// Return FALSE if the gfxFontEntry subclass does not
// implement GetFontTable(), or for non-sfnt fonts where tables are
// not available.
bool
gfxFont::InitMetricsFromSfntTables(Metrics& aMetrics)
{
    mIsValid = false; // font is NOT valid in case of early return

    const uint32_t kHeadTableTag = TRUETYPE_TAG('h','e','a','d');
    const uint32_t kHheaTableTag = TRUETYPE_TAG('h','h','e','a');
    const uint32_t kPostTableTag = TRUETYPE_TAG('p','o','s','t');
    const uint32_t kOS_2TableTag = TRUETYPE_TAG('O','S','/','2');

    if (mFUnitsConvFactor == 0.0) {
        // If the conversion factor from FUnits is not yet set,
        // 'head' table is required; otherwise we cannot read any metrics
        // because we don't know unitsPerEm
        AutoFallibleTArray<uint8_t,sizeof(HeadTable)> headData;
        if (NS_FAILED(mFontEntry->GetFontTable(kHeadTableTag, headData)) ||
            headData.Length() < sizeof(HeadTable)) {
            return false; // no 'head' table -> not an sfnt
        }
        HeadTable *head = reinterpret_cast<HeadTable*>(headData.Elements());
        uint32_t unitsPerEm = head->unitsPerEm;
        if (!unitsPerEm) {
            return true; // is an sfnt, but not valid
        }
        mFUnitsConvFactor = mAdjustedSize / unitsPerEm;
    }

    // 'hhea' table is required to get vertical extents
    AutoFallibleTArray<uint8_t,sizeof(HheaTable)> hheaData;
    if (NS_FAILED(mFontEntry->GetFontTable(kHheaTableTag, hheaData)) ||
        hheaData.Length() < sizeof(HheaTable)) {
        return false; // no 'hhea' table -> not an sfnt
    }
    HheaTable *hhea = reinterpret_cast<HheaTable*>(hheaData.Elements());

#define SET_UNSIGNED(field,src) aMetrics.field = uint16_t(src) * mFUnitsConvFactor
#define SET_SIGNED(field,src)   aMetrics.field = int16_t(src) * mFUnitsConvFactor

    SET_UNSIGNED(maxAdvance, hhea->advanceWidthMax);
    SET_SIGNED(maxAscent, hhea->ascender);
    SET_SIGNED(maxDescent, -int16_t(hhea->descender));
    SET_SIGNED(externalLeading, hhea->lineGap);

    // 'post' table is required for underline metrics
    AutoFallibleTArray<uint8_t,sizeof(PostTable)> postData;
    if (NS_FAILED(mFontEntry->GetFontTable(kPostTableTag, postData))) {
        return true; // no 'post' table -> sfnt is not valid
    }
    if (postData.Length() <
        offsetof(PostTable, underlineThickness) + sizeof(uint16_t)) {
        return true; // bad post table -> sfnt is not valid
    }
    PostTable *post = reinterpret_cast<PostTable*>(postData.Elements());

    SET_SIGNED(underlineOffset, post->underlinePosition);
    SET_UNSIGNED(underlineSize, post->underlineThickness);

    // 'OS/2' table is optional, if not found we'll estimate xHeight
    // and aveCharWidth by measuring glyphs
    AutoFallibleTArray<uint8_t,sizeof(OS2Table)> os2data;
    if (NS_SUCCEEDED(mFontEntry->GetFontTable(kOS_2TableTag, os2data))) {
        OS2Table *os2 = reinterpret_cast<OS2Table*>(os2data.Elements());

        if (os2data.Length() >= offsetof(OS2Table, sxHeight) +
                                sizeof(int16_t) &&
            uint16_t(os2->version) >= 2) {
            // version 2 and later includes the x-height field
            SET_SIGNED(xHeight, os2->sxHeight);
            // std::abs because of negative xHeight seen in Kokonor (Tibetan) font
            aMetrics.xHeight = std::abs(aMetrics.xHeight);
        }
        // this should always be present
        if (os2data.Length() >= offsetof(OS2Table, yStrikeoutPosition) +
                                sizeof(int16_t)) {
            SET_SIGNED(aveCharWidth, os2->xAvgCharWidth);
            SET_SIGNED(subscriptOffset, os2->ySubscriptYOffset);
            SET_SIGNED(superscriptOffset, os2->ySuperscriptYOffset);
            SET_SIGNED(strikeoutSize, os2->yStrikeoutSize);
            SET_SIGNED(strikeoutOffset, os2->yStrikeoutPosition);
        }
    }

    mIsValid = true;

    return true;
}

static double
RoundToNearestMultiple(double aValue, double aFraction)
{
    return floor(aValue/aFraction + 0.5) * aFraction;
}

void gfxFont::CalculateDerivedMetrics(Metrics& aMetrics)
{
    aMetrics.maxAscent =
        ceil(RoundToNearestMultiple(aMetrics.maxAscent, 1/1024.0));
    aMetrics.maxDescent =
        ceil(RoundToNearestMultiple(aMetrics.maxDescent, 1/1024.0));

    if (aMetrics.xHeight <= 0) {
        // only happens if we couldn't find either font metrics
        // or a char to measure;
        // pick an arbitrary value that's better than zero
        aMetrics.xHeight = aMetrics.maxAscent * DEFAULT_XHEIGHT_FACTOR;
    }

    aMetrics.maxHeight = aMetrics.maxAscent + aMetrics.maxDescent;

    if (aMetrics.maxHeight - aMetrics.emHeight > 0.0) {
        aMetrics.internalLeading = aMetrics.maxHeight - aMetrics.emHeight;
    } else {
        aMetrics.internalLeading = 0.0;
    }

    aMetrics.emAscent = aMetrics.maxAscent * aMetrics.emHeight
                            / aMetrics.maxHeight;
    aMetrics.emDescent = aMetrics.emHeight - aMetrics.emAscent;

    if (GetFontEntry()->IsFixedPitch()) {
        // Some Quartz fonts are fixed pitch, but there's some glyph with a bigger
        // advance than the average character width... this forces
        // those fonts to be recognized like fixed pitch fonts by layout.
        aMetrics.maxAdvance = aMetrics.aveCharWidth;
    }

    if (!aMetrics.subscriptOffset) {
        aMetrics.subscriptOffset = aMetrics.xHeight;
    }
    if (!aMetrics.superscriptOffset) {
        aMetrics.superscriptOffset = aMetrics.xHeight;
    }

    if (!aMetrics.strikeoutOffset) {
        aMetrics.strikeoutOffset = aMetrics.xHeight * 0.5;
    }
    if (!aMetrics.strikeoutSize) {
        aMetrics.strikeoutSize = aMetrics.underlineSize;
    }
}

void
gfxFont::SanitizeMetrics(gfxFont::Metrics *aMetrics, bool aIsBadUnderlineFont)
{
    // Even if this font size is zero, this font is created with non-zero size.
    // However, for layout and others, we should return the metrics of zero size font.
    if (mStyle.size == 0.0) {
        memset(aMetrics, 0, sizeof(gfxFont::Metrics));
        return;
    }

    // MS (P)Gothic and MS (P)Mincho are not having suitable values in their super script offset.
    // If the values are not suitable, we should use x-height instead of them.
    // See https://bugzilla.mozilla.org/show_bug.cgi?id=353632
    if (aMetrics->superscriptOffset <= 0 ||
        aMetrics->superscriptOffset >= aMetrics->maxAscent) {
        aMetrics->superscriptOffset = aMetrics->xHeight;
    }
    // And also checking the case of sub script offset. The old gfx for win has checked this too.
    if (aMetrics->subscriptOffset <= 0 ||
        aMetrics->subscriptOffset >= aMetrics->maxAscent) {
        aMetrics->subscriptOffset = aMetrics->xHeight;
    }

    aMetrics->underlineSize = NS_MAX(1.0, aMetrics->underlineSize);
    aMetrics->strikeoutSize = NS_MAX(1.0, aMetrics->strikeoutSize);

    aMetrics->underlineOffset = NS_MIN(aMetrics->underlineOffset, -1.0);

    if (aMetrics->maxAscent < 1.0) {
        // We cannot draw strikeout line and overline in the ascent...
        aMetrics->underlineSize = 0;
        aMetrics->underlineOffset = 0;
        aMetrics->strikeoutSize = 0;
        aMetrics->strikeoutOffset = 0;
        return;
    }

    /**
     * Some CJK fonts have bad underline offset. Therefore, if this is such font,
     * we need to lower the underline offset to bottom of *em* descent.
     * However, if this is system font, we should not do this for the rendering compatibility with
     * another application's UI on the platform.
     * XXX Should not use this hack if the font size is too small?
     *     Such text cannot be read, this might be used for tight CSS rendering? (E.g., Acid2)
     */
    if (!mStyle.systemFont && aIsBadUnderlineFont) {
        // First, we need 2 pixels between baseline and underline at least. Because many CJK characters
        // put their glyphs on the baseline, so, 1 pixel is too close for CJK characters.
        aMetrics->underlineOffset = NS_MIN(aMetrics->underlineOffset, -2.0);

        // Next, we put the underline to bottom of below of the descent space.
        if (aMetrics->internalLeading + aMetrics->externalLeading > aMetrics->underlineSize) {
            aMetrics->underlineOffset = NS_MIN(aMetrics->underlineOffset, -aMetrics->emDescent);
        } else {
            aMetrics->underlineOffset = NS_MIN(aMetrics->underlineOffset,
                                               aMetrics->underlineSize - aMetrics->emDescent);
        }
    }
    // If underline positioned is too far from the text, descent position is preferred so that underline
    // will stay within the boundary.
    else if (aMetrics->underlineSize - aMetrics->underlineOffset > aMetrics->maxDescent) {
        if (aMetrics->underlineSize > aMetrics->maxDescent)
            aMetrics->underlineSize = NS_MAX(aMetrics->maxDescent, 1.0);
        // The max underlineOffset is 1px (the min underlineSize is 1px, and min maxDescent is 0px.)
        aMetrics->underlineOffset = aMetrics->underlineSize - aMetrics->maxDescent;
    }

    // If strikeout line is overflowed from the ascent, the line should be resized and moved for
    // that being in the ascent space.
    // Note that the strikeoutOffset is *middle* of the strikeout line position.
    gfxFloat halfOfStrikeoutSize = floor(aMetrics->strikeoutSize / 2.0 + 0.5);
    if (halfOfStrikeoutSize + aMetrics->strikeoutOffset > aMetrics->maxAscent) {
        if (aMetrics->strikeoutSize > aMetrics->maxAscent) {
            aMetrics->strikeoutSize = NS_MAX(aMetrics->maxAscent, 1.0);
            halfOfStrikeoutSize = floor(aMetrics->strikeoutSize / 2.0 + 0.5);
        }
        gfxFloat ascent = floor(aMetrics->maxAscent + 0.5);
        aMetrics->strikeoutOffset = NS_MAX(halfOfStrikeoutSize, ascent / 2.0);
    }

    // If overline is larger than the ascent, the line should be resized.
    if (aMetrics->underlineSize > aMetrics->maxAscent) {
        aMetrics->underlineSize = aMetrics->maxAscent;
    }
}

gfxFloat
gfxFont::SynthesizeSpaceWidth(uint32_t aCh)
{
    // return an appropriate width for various Unicode space characters
    // that we "fake" if they're not actually present in the font;
    // returns negative value if the char is not a known space.
    switch (aCh) {
    case 0x2000:                                 // en quad
    case 0x2002: return GetAdjustedSize() / 2;   // en space
    case 0x2001:                                 // em quad
    case 0x2003: return GetAdjustedSize();       // em space
    case 0x2004: return GetAdjustedSize() / 3;   // three-per-em space
    case 0x2005: return GetAdjustedSize() / 4;   // four-per-em space
    case 0x2006: return GetAdjustedSize() / 6;   // six-per-em space
    case 0x2007: return GetMetrics().zeroOrAveCharWidth; // figure space
    case 0x2008: return GetMetrics().spaceWidth; // punctuation space 
    case 0x2009: return GetAdjustedSize() / 5;   // thin space
    case 0x200a: return GetAdjustedSize() / 10;  // hair space
    case 0x202f: return GetAdjustedSize() / 5;   // narrow no-break space
    default: return -1.0;
    }
}

/*static*/ size_t
gfxFont::WordCacheEntrySizeOfExcludingThis(CacheHashEntry*   aHashEntry,
                                           nsMallocSizeOfFun aMallocSizeOf,
                                           void*             aUserArg)
{
    return aMallocSizeOf(aHashEntry->mShapedWord.get());
}

void
gfxFont::SizeOfExcludingThis(nsMallocSizeOfFun aMallocSizeOf,
                             FontCacheSizes*   aSizes) const
{
    for (uint32_t i = 0; i < mGlyphExtentsArray.Length(); ++i) {
        aSizes->mFontInstances +=
            mGlyphExtentsArray[i]->SizeOfIncludingThis(aMallocSizeOf);
    }
    aSizes->mShapedWords +=
        mWordCache.SizeOfExcludingThis(WordCacheEntrySizeOfExcludingThis,
                                       aMallocSizeOf);
}

void
gfxFont::SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf,
                             FontCacheSizes*   aSizes) const
{
    aSizes->mFontInstances += aMallocSizeOf(this);
    SizeOfExcludingThis(aMallocSizeOf, aSizes);
}

gfxGlyphExtents::~gfxGlyphExtents()
{
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
    gGlyphExtentsWidthsTotalSize +=
        mContainedGlyphWidths.SizeOfExcludingThis(&FontCacheMallocSizeOf);
    gGlyphExtentsCount++;
#endif
    MOZ_COUNT_DTOR(gfxGlyphExtents);
}

bool
gfxGlyphExtents::GetTightGlyphExtentsAppUnits(gfxFont *aFont,
    gfxContext *aContext, uint32_t aGlyphID, gfxRect *aExtents)
{
    HashEntry *entry = mTightGlyphExtents.GetEntry(aGlyphID);
    if (!entry) {
        if (!aContext) {
            NS_WARNING("Could not get glyph extents (no aContext)");
            return false;
        }

        if (aFont->SetupCairoFont(aContext)) {
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
            ++gGlyphExtentsSetupLazyTight;
#endif
            aFont->SetupGlyphExtents(aContext, aGlyphID, true, this);
            entry = mTightGlyphExtents.GetEntry(aGlyphID);
        }
        if (!entry) {
            NS_WARNING("Could not get glyph extents");
            return false;
        }
    }

    *aExtents = gfxRect(entry->x, entry->y, entry->width, entry->height);
    return true;
}

gfxGlyphExtents::GlyphWidths::~GlyphWidths()
{
    uint32_t i, count = mBlocks.Length();
    for (i = 0; i < count; ++i) {
        uintptr_t bits = mBlocks[i];
        if (bits && !(bits & 0x1)) {
            delete[] reinterpret_cast<uint16_t *>(bits);
        }
    }
}

uint32_t
gfxGlyphExtents::GlyphWidths::SizeOfExcludingThis(nsMallocSizeOfFun aMallocSizeOf) const
{
    uint32_t i;
    uint32_t size = mBlocks.SizeOfExcludingThis(aMallocSizeOf);
    for (i = 0; i < mBlocks.Length(); ++i) {
        uintptr_t bits = mBlocks[i];
        if (bits && !(bits & 0x1)) {
            size += aMallocSizeOf(reinterpret_cast<void*>(bits));
        }
    }
    return size;
}

void
gfxGlyphExtents::GlyphWidths::Set(uint32_t aGlyphID, uint16_t aWidth)
{
    uint32_t block = aGlyphID >> BLOCK_SIZE_BITS;
    uint32_t len = mBlocks.Length();
    if (block >= len) {
        uintptr_t *elems = mBlocks.AppendElements(block + 1 - len);
        if (!elems)
            return;
        memset(elems, 0, sizeof(uintptr_t)*(block + 1 - len));
    }

    uintptr_t bits = mBlocks[block];
    uint32_t glyphOffset = aGlyphID & (BLOCK_SIZE - 1);
    if (!bits) {
        mBlocks[block] = MakeSingle(glyphOffset, aWidth);
        return;
    }

    uint16_t *newBlock;
    if (bits & 0x1) {
        // Expand the block to a real block. We could avoid this by checking
        // glyphOffset == GetGlyphOffset(bits), but that never happens so don't bother
        newBlock = new uint16_t[BLOCK_SIZE];
        if (!newBlock)
            return;
        uint32_t i;
        for (i = 0; i < BLOCK_SIZE; ++i) {
            newBlock[i] = INVALID_WIDTH;
        }
        newBlock[GetGlyphOffset(bits)] = GetWidth(bits);
        mBlocks[block] = reinterpret_cast<uintptr_t>(newBlock);
    } else {
        newBlock = reinterpret_cast<uint16_t *>(bits);
    }
    newBlock[glyphOffset] = aWidth;
}

void
gfxGlyphExtents::SetTightGlyphExtents(uint32_t aGlyphID, const gfxRect& aExtentsAppUnits)
{
    HashEntry *entry = mTightGlyphExtents.PutEntry(aGlyphID);
    if (!entry)
        return;
    entry->x = aExtentsAppUnits.X();
    entry->y = aExtentsAppUnits.Y();
    entry->width = aExtentsAppUnits.Width();
    entry->height = aExtentsAppUnits.Height();
}

size_t
gfxGlyphExtents::SizeOfExcludingThis(nsMallocSizeOfFun aMallocSizeOf) const
{
    return mContainedGlyphWidths.SizeOfExcludingThis(aMallocSizeOf) +
        mTightGlyphExtents.SizeOfExcludingThis(nullptr, aMallocSizeOf);
}

size_t
gfxGlyphExtents::SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf) const
{
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}

gfxFontGroup::gfxFontGroup(const nsAString& aFamilies, const gfxFontStyle *aStyle, gfxUserFontSet *aUserFontSet)
    : mFamilies(aFamilies), mStyle(*aStyle), mUnderlineOffset(UNDERLINE_OFFSET_NOT_SET)
{
    mUserFontSet = nullptr;
    SetUserFontSet(aUserFontSet);

    mSkipDrawing = false;

    mPageLang = gfxPlatform::GetFontPrefLangFor(mStyle.language);
    BuildFontList();
}

void
gfxFontGroup::BuildFontList()
{
// "#if" to be removed once all platforms are moved to gfxPlatformFontList interface
// and subclasses of gfxFontGroup eliminated
#if defined(XP_MACOSX) || defined(XP_IOS) || defined(XP_WIN) || defined(ANDROID)
    ForEachFont(FindPlatformFont, this);

    if (mFonts.Length() == 0) {
        bool needsBold;
        gfxPlatformFontList *pfl = gfxPlatformFontList::PlatformFontList();
        gfxFontFamily *defaultFamily = pfl->GetDefaultFont(&mStyle);
        NS_ASSERTION(defaultFamily,
                     "invalid default font returned by GetDefaultFont");

        if (defaultFamily) {
            gfxFontEntry *fe = defaultFamily->FindFontForStyle(mStyle,
                                                               needsBold);
            if (fe) {
                nsRefPtr<gfxFont> font = fe->FindOrMakeFont(&mStyle,
                                                            needsBold);
                if (font) {
                    mFonts.AppendElement(FamilyFace(defaultFamily, font));
                }
            }
        }

        if (mFonts.Length() == 0) {
            // Try for a "font of last resort...."
            // Because an empty font list would be Really Bad for later code
            // that assumes it will be able to get valid metrics for layout,
            // just look for the first usable font and put in the list.
            // (see bug 554544)
            nsAutoTArray<nsRefPtr<gfxFontFamily>,200> families;
            pfl->GetFontFamilyList(families);
            uint32_t count = families.Length();
            for (uint32_t i = 0; i < count; ++i) {
                gfxFontEntry *fe = families[i]->FindFontForStyle(mStyle,
                                                                 needsBold);
                if (fe) {
                    nsRefPtr<gfxFont> font = fe->FindOrMakeFont(&mStyle,
                                                                needsBold);
                    if (font) {
                        mFonts.AppendElement(FamilyFace(families[i], font));
                        break;
                    }
                }
            }
        }

        if (mFonts.Length() == 0) {
            // an empty font list at this point is fatal; we're not going to
            // be able to do even the most basic layout operations
            char msg[256]; // CHECK buffer length if revising message below
            sprintf(msg, "unable to find a usable font (%.220s)",
                    NS_ConvertUTF16toUTF8(mFamilies).get());
            NS_RUNTIMEABORT(msg);
        }
    }

    if (!mStyle.systemFont) {
        uint32_t count = mFonts.Length();
        for (uint32_t i = 0; i < count; ++i) {
            gfxFont* font = mFonts[i].Font();
            if (font->GetFontEntry()->mIsBadUnderlineFont) {
                gfxFloat first = mFonts[0].Font()->GetMetrics().underlineOffset;
                gfxFloat bad = font->GetMetrics().underlineOffset;
                mUnderlineOffset = NS_MIN(first, bad);
                break;
            }
        }
    }
#endif
}

bool
gfxFontGroup::FindPlatformFont(const nsAString& aName,
                               const nsACString& aGenericName,
                               bool aUseFontSet,
                               void *aClosure)
{
    gfxFontGroup *fontGroup = static_cast<gfxFontGroup*>(aClosure);
    const gfxFontStyle *fontStyle = fontGroup->GetStyle();

    bool needsBold;
    gfxFontFamily *family = nullptr;
    gfxFontEntry *fe = nullptr;

    if (aUseFontSet) {
        // First, look up in the user font set...
        // If the fontSet matches the family, we must not look for a platform
        // font of the same name, even if we fail to actually get a fontEntry
        // here; we'll fall back to the next name in the CSS font-family list.
        gfxUserFontSet *fs = fontGroup->GetUserFontSet();
        if (fs) {
            // If the fontSet matches the family, but the font has not yet finished
            // loading (nor has its load timeout fired), the fontGroup should wait
            // for the download, and not actually draw its text yet.
            family = fs->GetFamily(aName);
            if (family) {
                bool waitForUserFont = false;
                fe = fs->FindFontEntry(family, *fontStyle,
                                       needsBold, waitForUserFont);
                if (!fe && waitForUserFont) {
                    fontGroup->mSkipDrawing = true;
                }
            }
        }
    }

    // Not known in the user font set ==> check system fonts
    if (!family) {
        gfxPlatformFontList *fontList = gfxPlatformFontList::PlatformFontList();
        family = fontList->FindFamily(aName);
        if (family) {
            fe = family->FindFontForStyle(*fontStyle, needsBold);
        }
    }

    // add to the font group, unless it's already there
    if (fe && !fontGroup->HasFont(fe)) {
        nsRefPtr<gfxFont> font = fe->FindOrMakeFont(fontStyle, needsBold);
        if (font) {
            fontGroup->mFonts.AppendElement(FamilyFace(family, font));
        }
    }

    return true;
}

bool
gfxFontGroup::HasFont(const gfxFontEntry *aFontEntry)
{
    uint32_t count = mFonts.Length();
    for (uint32_t i = 0; i < count; ++i) {
        if (mFonts[i].Font()->GetFontEntry() == aFontEntry)
            return true;
    }
    return false;
}

gfxFontGroup::~gfxFontGroup() {
    mFonts.Clear();
    SetUserFontSet(nullptr);
}

gfxFontGroup *
gfxFontGroup::Copy(const gfxFontStyle *aStyle)
{
    return new gfxFontGroup(mFamilies, aStyle, mUserFontSet);
}

bool 
gfxFontGroup::IsInvalidChar(uint8_t ch)
{
    return ((ch & 0x7f) < 0x20);
}

bool 
gfxFontGroup::IsInvalidChar(PRUnichar ch)
{
    // All printable 7-bit ASCII values are OK
    if (ch >= ' ' && ch < 0x80) {
        return false;
    }
    // No point in sending non-printing control chars through font shaping
    if (ch <= 0x9f) {
        return true;
    }
    return ((ch & 0xFF00) == 0x2000 /* Unicode control character */ &&
         (ch == 0x200B/*ZWSP*/ || ch == 0x2028/*LSEP*/ || ch == 0x2029/*PSEP*/ ||
          IS_BIDI_CONTROL_CHAR(ch)));
}

bool
gfxFontGroup::ForEachFont(FontCreationCallback fc,
                          void *closure)
{
    return ForEachFontInternal(mFamilies, mStyle.language,
                               true, true, true, fc, closure);
}

bool
gfxFontGroup::ForEachFont(const nsAString& aFamilies,
                          nsIAtom *aLanguage,
                          FontCreationCallback fc,
                          void *closure)
{
    return ForEachFontInternal(aFamilies, aLanguage,
                               false, true, true, fc, closure);
}

struct ResolveData {
    ResolveData(gfxFontGroup::FontCreationCallback aCallback,
                nsACString& aGenericFamily,
                bool aUseFontSet,
                void *aClosure) :
        mCallback(aCallback),
        mGenericFamily(aGenericFamily),
        mUseFontSet(aUseFontSet),
        mClosure(aClosure) {
    }
    gfxFontGroup::FontCreationCallback mCallback;
    nsCString mGenericFamily;
    bool mUseFontSet;
    void *mClosure;
};

bool
gfxFontGroup::ForEachFontInternal(const nsAString& aFamilies,
                                  nsIAtom *aLanguage,
                                  bool aResolveGeneric,
                                  bool aResolveFontName,
                                  bool aUseFontSet,
                                  FontCreationCallback fc,
                                  void *closure)
{
    const PRUnichar kSingleQuote  = PRUnichar('\'');
    const PRUnichar kDoubleQuote  = PRUnichar('\"');
    const PRUnichar kComma        = PRUnichar(',');

    nsIAtom *groupAtom = nullptr;
    nsAutoCString groupString;
    if (aLanguage) {
        if (!gLangService) {
            CallGetService(NS_LANGUAGEATOMSERVICE_CONTRACTID, &gLangService);
        }
        if (gLangService) {
            nsresult rv;
            groupAtom = gLangService->GetLanguageGroup(aLanguage, &rv);
        }
    }
    if (!groupAtom) {
        groupAtom = nsGkAtoms::Unicode;
    }
    groupAtom->ToUTF8String(groupString);

    nsPromiseFlatString families(aFamilies);
    const PRUnichar *p, *p_end;
    families.BeginReading(p);
    families.EndReading(p_end);
    nsAutoString family;
    nsAutoCString lcFamily;
    nsAutoString genericFamily;

    while (p < p_end) {
        while (nsCRT::IsAsciiSpace(*p) || *p == kComma)
            if (++p == p_end)
                return true;

        bool generic;
        if (*p == kSingleQuote || *p == kDoubleQuote) {
            // quoted font family
            PRUnichar quoteMark = *p;
            if (++p == p_end)
                return true;
            const PRUnichar *nameStart = p;

            // XXX What about CSS character escapes?
            while (*p != quoteMark)
                if (++p == p_end)
                    return true;

            family = Substring(nameStart, p);
            generic = false;
            genericFamily.SetIsVoid(true);

            while (++p != p_end && *p != kComma)
                /* nothing */ ;

        } else {
            // unquoted font family
            const PRUnichar *nameStart = p;
            while (++p != p_end && *p != kComma)
                /* nothing */ ;

            family = Substring(nameStart, p);
            family.CompressWhitespace(false, true);

            if (aResolveGeneric &&
                (family.LowerCaseEqualsLiteral("serif") ||
                 family.LowerCaseEqualsLiteral("sans-serif") ||
                 family.LowerCaseEqualsLiteral("monospace") ||
                 family.LowerCaseEqualsLiteral("cursive") ||
                 family.LowerCaseEqualsLiteral("fantasy")))
            {
                generic = true;

                ToLowerCase(NS_LossyConvertUTF16toASCII(family), lcFamily);

                nsAutoCString prefName("font.name.");
                prefName.Append(lcFamily);
                prefName.AppendLiteral(".");
                prefName.Append(groupString);

                nsAdoptingString value = Preferences::GetString(prefName.get());
                if (value) {
                    CopyASCIItoUTF16(lcFamily, genericFamily);
                    family = value;
                }
            } else {
                generic = false;
                genericFamily.SetIsVoid(true);
            }
        }

        NS_LossyConvertUTF16toASCII gf(genericFamily);
        if (generic) {
            ForEachFontInternal(family, groupAtom, false,
                                aResolveFontName, false,
                                fc, closure);
        } else if (!family.IsEmpty()) {
            if (aResolveFontName) {
                ResolveData data(fc, gf, aUseFontSet, closure);
                bool aborted = false, needsBold;
                nsresult rv = NS_OK;
                bool foundFamily = false;
                bool waitForUserFont = false;
                gfxFontEntry *fe = nullptr;
                if (aUseFontSet && mUserFontSet) {
                    gfxFontFamily *fam = mUserFontSet->GetFamily(family);
                    if (fam) {
                        fe = mUserFontSet->FindFontEntry(fam, mStyle,
                                                         needsBold,
                                                         waitForUserFont);
                    }
                }
                if (fe) {
                    gfxFontGroup::FontResolverProc(family, &data);
                } else {
                    if (waitForUserFont) {
                        mSkipDrawing = true;
                    }
                    if (!foundFamily) {
                        gfxPlatform *pf = gfxPlatform::GetPlatform();
                        rv = pf->ResolveFontName(family,
                                                 gfxFontGroup::FontResolverProc,
                                                 &data, aborted);
                    }
                }
                if (NS_FAILED(rv) || aborted)
                    return false;
            }
            else {
                if (!fc(family, gf, aUseFontSet, closure))
                    return false;
            }
        }

        if (generic && aResolveGeneric) {
            nsAutoCString prefName("font.name-list.");
            prefName.Append(lcFamily);
            prefName.AppendLiteral(".");
            prefName.Append(groupString);
            nsAdoptingString value = Preferences::GetString(prefName.get());
            if (value) {
                ForEachFontInternal(value, groupAtom, false,
                                    aResolveFontName, false,
                                    fc, closure);
            }
        }

        ++p; // may advance past p_end
    }

    return true;
}

bool
gfxFontGroup::FontResolverProc(const nsAString& aName, void *aClosure)
{
    ResolveData *data = reinterpret_cast<ResolveData*>(aClosure);
    return (data->mCallback)(aName, data->mGenericFamily, data->mUseFontSet,
                             data->mClosure);
}

gfxTextRun *
gfxFontGroup::MakeEmptyTextRun(const Parameters *aParams, uint32_t aFlags)
{
    aFlags |= TEXT_IS_8BIT | TEXT_IS_ASCII | TEXT_IS_PERSISTENT;
    return gfxTextRun::Create(aParams, 0, this, aFlags);
}

gfxTextRun *
gfxFontGroup::MakeSpaceTextRun(const Parameters *aParams, uint32_t aFlags)
{
    aFlags |= TEXT_IS_8BIT | TEXT_IS_ASCII | TEXT_IS_PERSISTENT;

    gfxTextRun *textRun = gfxTextRun::Create(aParams, 1, this, aFlags);
    if (!textRun) {
        return nullptr;
    }

    gfxFont *font = GetFontAt(0);
    if (MOZ_UNLIKELY(GetStyle()->size == 0)) {
        // Short-circuit for size-0 fonts, as Windows and ATSUI can't handle
        // them, and always create at least size 1 fonts, i.e. they still
        // render something for size 0 fonts.
        textRun->AddGlyphRun(font, gfxTextRange::kFontGroup, 0, false);
    }
    else {
        textRun->SetSpaceGlyph(font, aParams->mContext, 0);
    }

    // Note that the gfxGlyphExtents glyph bounds storage for the font will
    // always contain an entry for the font's space glyph, so we don't have
    // to call FetchGlyphExtents here.
    return textRun;
}

gfxTextRun *
gfxFontGroup::MakeBlankTextRun(uint32_t aLength,
                               const Parameters *aParams, uint32_t aFlags)
{
    gfxTextRun *textRun =
        gfxTextRun::Create(aParams, aLength, this, aFlags);
    if (!textRun) {
        return nullptr;
    }

    textRun->AddGlyphRun(GetFontAt(0), gfxTextRange::kFontGroup, 0, false);
    return textRun;
}

gfxTextRun *
gfxFontGroup::MakeTextRun(const uint8_t *aString, uint32_t aLength,
                          const Parameters *aParams, uint32_t aFlags)
{
    if (aLength == 0) {
        return MakeEmptyTextRun(aParams, aFlags);
    }
    if (aLength == 1 && aString[0] == ' ') {
        return MakeSpaceTextRun(aParams, aFlags);
    }

    aFlags |= TEXT_IS_8BIT;

    if (GetStyle()->size == 0) {
        // Short-circuit for size-0 fonts, as Windows and ATSUI can't handle
        // them, and always create at least size 1 fonts, i.e. they still
        // render something for size 0 fonts.
        return MakeBlankTextRun(aLength, aParams, aFlags);
    }

    gfxTextRun *textRun = gfxTextRun::Create(aParams, aLength,
                                             this, aFlags);
    if (!textRun) {
        return nullptr;
    }

    InitTextRun(aParams->mContext, textRun, aString, aLength);

    textRun->FetchGlyphExtents(aParams->mContext);

    return textRun;
}

gfxTextRun *
gfxFontGroup::MakeTextRun(const PRUnichar *aString, uint32_t aLength,
                          const Parameters *aParams, uint32_t aFlags)
{
    if (aLength == 0) {
        return MakeEmptyTextRun(aParams, aFlags);
    }
    if (aLength == 1 && aString[0] == ' ') {
        return MakeSpaceTextRun(aParams, aFlags);
    }
    if (GetStyle()->size == 0) {
        return MakeBlankTextRun(aLength, aParams, aFlags);
    }

    gfxTextRun *textRun = gfxTextRun::Create(aParams, aLength,
                                             this, aFlags);
    if (!textRun) {
        return nullptr;
    }

    InitTextRun(aParams->mContext, textRun, aString, aLength);

    textRun->FetchGlyphExtents(aParams->mContext);

    return textRun;
}

template<typename T>
void
gfxFontGroup::InitTextRun(gfxContext *aContext,
                          gfxTextRun *aTextRun,
                          const T *aString,
                          uint32_t aLength)
{
    NS_ASSERTION(aLength > 0, "don't call InitTextRun for a zero-length run");

    // we need to do numeral processing even on 8-bit text,
    // in case we're converting Western to Hindi/Arabic digits
    int32_t numOption = gfxPlatform::GetPlatform()->GetBidiNumeralOption();
    nsAutoArrayPtr<PRUnichar> transformedString;
    if (numOption != IBMBIDI_NUMERAL_NOMINAL) {
        // scan the string for numerals that may need to be transformed;
        // if we find any, we'll make a local copy here and use that for
        // font matching and glyph generation/shaping
        bool prevIsArabic =
            (aTextRun->GetFlags() & gfxTextRunFactory::TEXT_INCOMING_ARABICCHAR) != 0;
        for (uint32_t i = 0; i < aLength; ++i) {
            PRUnichar origCh = aString[i];
            PRUnichar newCh = HandleNumberInChar(origCh, prevIsArabic, numOption);
            if (newCh != origCh) {
                if (!transformedString) {
                    transformedString = new PRUnichar[aLength];
                    if (sizeof(T) == sizeof(PRUnichar)) {
                        memcpy(transformedString.get(), aString, i * sizeof(PRUnichar));
                    } else {
                        for (uint32_t j = 0; j < i; ++j) {
                            transformedString[j] = aString[j];
                        }
                    }
                }
            }
            if (transformedString) {
                transformedString[i] = newCh;
            }
            prevIsArabic = IS_ARABIC_CHAR(newCh);
        }
    }

    if (sizeof(T) == sizeof(uint8_t) && !transformedString) {
        // the text is still purely 8-bit; bypass the script-run itemizer
        // and treat it as a single Latin run
        InitScriptRun(aContext, aTextRun, aString,
                      0, aLength, MOZ_SCRIPT_LATIN);
    } else {
        const PRUnichar *textPtr;
        if (transformedString) {
            textPtr = transformedString.get();
        } else {
            // typecast to avoid compilation error for the 8-bit version,
            // even though this is dead code in that case
            textPtr = reinterpret_cast<const PRUnichar*>(aString);
        }

        // split into script runs so that script can potentially influence
        // the font matching process below
        gfxScriptItemizer scriptRuns(textPtr, aLength);

#ifdef PR_LOGGING
        PRLogModuleInfo *log = (mStyle.systemFont ?
                                gfxPlatform::GetLog(eGfxLog_textrunui) :
                                gfxPlatform::GetLog(eGfxLog_textrun));
#endif

        uint32_t runStart = 0, runLimit = aLength;
        int32_t runScript = MOZ_SCRIPT_LATIN;
        while (scriptRuns.Next(runStart, runLimit, runScript)) {

#ifdef PR_LOGGING
            if (MOZ_UNLIKELY(log)) {
                nsAutoCString lang;
                mStyle.language->ToUTF8String(lang);
                uint32_t runLen = runLimit - runStart;
                PR_LOG(log, PR_LOG_WARNING,\
                       ("(%s) fontgroup: [%s] lang: %s script: %d len %d "
                        "weight: %d width: %d style: %s "
                        "TEXTRUN [%s] ENDTEXTRUN\n",
                        (mStyle.systemFont ? "textrunui" : "textrun"),
                        NS_ConvertUTF16toUTF8(mFamilies).get(),
                        lang.get(), runScript, runLen,
                        uint32_t(mStyle.weight), uint32_t(mStyle.stretch),
                        (mStyle.style & NS_FONT_STYLE_ITALIC ? "italic" :
                        (mStyle.style & NS_FONT_STYLE_OBLIQUE ? "oblique" :
                                                                "normal")),
                        NS_ConvertUTF16toUTF8(textPtr + runStart, runLen).get()));
            }
#endif

            InitScriptRun(aContext, aTextRun, textPtr,
                          runStart, runLimit, runScript);
        }
    }

    if (sizeof(T) == sizeof(PRUnichar) && aLength > 0) {
        gfxTextRun::CompressedGlyph *glyph = aTextRun->GetCharacterGlyphs();
        if (!glyph->IsSimpleGlyph()) {
            glyph->SetClusterStart(true);
        }
    }

    // It's possible for CoreText to omit glyph runs if it decides they contain
    // only invisibles (e.g., U+FEFF, see reftest 474417-1). In this case, we
    // need to eliminate them from the glyph run array to avoid drawing "partial
    // ligatures" with the wrong font.
    // We don't do this during InitScriptRun (or gfxFont::InitTextRun) because
    // it will iterate back over all glyphruns in the textrun, which leads to
    // pathologically-bad perf in the case where a textrun contains many script
    // changes (see bug 680402) - we'd end up re-sanitizing all the earlier runs
    // every time a new script subrun is processed.
    aTextRun->SanitizeGlyphRuns();

    aTextRun->SortGlyphRuns();
}

template<typename T>
void
gfxFontGroup::InitScriptRun(gfxContext *aContext,
                            gfxTextRun *aTextRun,
                            const T *aString,
                            uint32_t aScriptRunStart,
                            uint32_t aScriptRunEnd,
                            int32_t aRunScript)
{
    NS_ASSERTION(aScriptRunEnd > aScriptRunStart,
                 "don't call InitScriptRun for a zero-length run");

    gfxFont *mainFont = GetFontAt(0);

    uint32_t runStart = aScriptRunStart;
    nsAutoTArray<gfxTextRange,3> fontRanges;
    ComputeRanges(fontRanges, aString + aScriptRunStart,
                  aScriptRunEnd - aScriptRunStart, aRunScript);
    uint32_t numRanges = fontRanges.Length();

    for (uint32_t r = 0; r < numRanges; r++) {
        const gfxTextRange& range = fontRanges[r];
        uint32_t matchedLength = range.Length();
        gfxFont *matchedFont = (range.font ? range.font.get() : nullptr);

        // create the glyph run for this range
        if (matchedFont) {
            aTextRun->AddGlyphRun(matchedFont, range.matchType,
                                  runStart, (matchedLength > 0));
            // do glyph layout and record the resulting positioned glyphs
            if (!matchedFont->SplitAndInitTextRun(aContext, aTextRun, aString,
                                                  runStart, matchedLength,
                                                  aRunScript)) {
                // glyph layout failed! treat as missing glyphs
                matchedFont = nullptr;
            }
        } else {
            aTextRun->AddGlyphRun(mainFont, gfxTextRange::kFontGroup,
                                  runStart, (matchedLength > 0));
        }

        if (!matchedFont) {
            // for PRUnichar text, we need to set cluster boundaries so that
            // surrogate pairs, combining characters, etc behave properly,
            // even if we don't have glyphs for them
            if (sizeof(T) == sizeof(PRUnichar)) {
                gfxShapedWord::SetupClusterBoundaries(aTextRun->GetCharacterGlyphs() + runStart,
                                                      reinterpret_cast<const PRUnichar*>(aString) + runStart,
                                                      matchedLength);
            }

            // various "missing" characters may need special handling,
            // so we check for them here
            uint32_t runLimit = runStart + matchedLength;
            for (uint32_t index = runStart; index < runLimit; index++) {
                T ch = aString[index];

                // tab and newline are not to be displayed as hexboxes,
                // but do need to be recorded in the textrun
                if (ch == '\n') {
                    aTextRun->SetIsNewline(index);
                    continue;
                }
                if (ch == '\t') {
                    aTextRun->SetIsTab(index);
                    continue;
                }

                // for 16-bit textruns only, check for surrogate pairs and
                // special Unicode spaces; omit these checks in 8-bit runs
                if (sizeof(T) == sizeof(PRUnichar)) {
                    if (NS_IS_HIGH_SURROGATE(ch) &&
                        index + 1 < aScriptRunEnd &&
                        NS_IS_LOW_SURROGATE(aString[index + 1]))
                    {
                        aTextRun->SetMissingGlyph(index,
                                                  SURROGATE_TO_UCS4(ch,
                                                                    aString[index + 1]));
                        index++;
                        aTextRun->SetIsLowSurrogate(index);
                        continue;
                    }

                    // check if this is a known Unicode whitespace character that
                    // we can render using the space glyph with a custom width
                    gfxFloat wid = mainFont->SynthesizeSpaceWidth(ch);
                    if (wid >= 0.0) {
                        nscoord advance =
                            aTextRun->GetAppUnitsPerDevUnit() * floor(wid + 0.5);
                        gfxTextRun::CompressedGlyph g;
                        if (gfxTextRun::CompressedGlyph::IsSimpleAdvance(advance)) {
                            aTextRun->SetSimpleGlyph(index,
                                                     g.SetSimpleGlyph(advance,
                                                         mainFont->GetSpaceGlyph()));
                        } else {
                            gfxTextRun::DetailedGlyph detailedGlyph;
                            detailedGlyph.mGlyphID = mainFont->GetSpaceGlyph();
                            detailedGlyph.mAdvance = advance;
                            detailedGlyph.mXOffset = detailedGlyph.mYOffset = 0;
                            g.SetComplex(true, true, 1);
                            aTextRun->SetGlyphs(index,
                                                g, &detailedGlyph);
                        }
                        continue;
                    }
                }

                if (IsInvalidChar(ch)) {
                    // invalid chars are left as zero-width/invisible
                    continue;
                }

                // record char code so we can draw a box with the Unicode value
                aTextRun->SetMissingGlyph(index, ch);
            }
        }

        runStart += matchedLength;
    }
}

already_AddRefed<gfxFont>
gfxFontGroup::TryAllFamilyMembers(gfxFontFamily* aFamily, uint32_t aCh)
{
    if (!aFamily->TestCharacterMap(aCh)) {
        return nullptr;
    }

    // Note that we don't need the actual runScript in matchData for
    // gfxFontFamily::SearchAllFontsForChar, it's only used for the
    // system-fallback case. So we can just set it to 0 here.
    GlobalFontMatch matchData(aCh, 0, &mStyle);
    aFamily->SearchAllFontsForChar(&matchData);
    gfxFontEntry *fe = matchData.mBestMatch;
    if (!fe) {
        return nullptr;
    }

    bool needsBold = mStyle.weight >= 600 && !fe->IsBold();
    nsRefPtr<gfxFont> font = fe->FindOrMakeFont(&mStyle, needsBold);
    return font.forget();
}

already_AddRefed<gfxFont>
gfxFontGroup::FindFontForChar(uint32_t aCh, uint32_t aPrevCh,
                              int32_t aRunScript, gfxFont *aPrevMatchedFont,
                              uint8_t *aMatchType)
{
    // To optimize common cases, try the first font in the font-group
    // before going into the more detailed checks below
    uint32_t nextIndex = 0;
    bool isJoinControl = gfxFontUtils::IsJoinControl(aCh);
    bool wasJoinCauser = gfxFontUtils::IsJoinCauser(aPrevCh);
    bool isVarSelector = gfxFontUtils::IsVarSelector(aCh);

    if (!isJoinControl && !wasJoinCauser && !isVarSelector) {
        gfxFont *firstFont = mFonts[0].Font();
        if (firstFont->HasCharacter(aCh)) {
            *aMatchType = gfxTextRange::kFontGroup;
            firstFont->AddRef();
            return firstFont;
        }
        // It's possible that another font in the family (e.g. regular face,
        // where the requested style was italic) will support the character
        nsRefPtr<gfxFont> font = TryAllFamilyMembers(mFonts[0].Family(), aCh);
        if (font) {
            *aMatchType = gfxTextRange::kFontGroup;
            return font.forget();
        }
        // we don't need to check the first font again below
        ++nextIndex;
    }

    if (aPrevMatchedFont) {
        // Don't switch fonts for control characters, regardless of
        // whether they are present in the current font, as they won't
        // actually be rendered (see bug 716229)
        uint8_t category = GetGeneralCategory(aCh);
        if (category == HB_UNICODE_GENERAL_CATEGORY_CONTROL) {
            aPrevMatchedFont->AddRef();
            return aPrevMatchedFont;
        }

        // if this character is a join-control or the previous is a join-causer,
        // use the same font as the previous range if we can
        if (isJoinControl || wasJoinCauser) {
            if (aPrevMatchedFont->HasCharacter(aCh)) {
                aPrevMatchedFont->AddRef();
                return aPrevMatchedFont;
            }
        }
    }

    // if this character is a variation selector,
    // use the previous font regardless of whether it supports VS or not.
    // otherwise the text run will be divided.
    if (isVarSelector) {
        if (aPrevMatchedFont) {
            aPrevMatchedFont->AddRef();
            return aPrevMatchedFont;
        }
        // VS alone. it's meaningless to search different fonts
        return nullptr;
    }

    // 1. check remaining fonts in the font group
    uint32_t fontListLength = FontListLength();
    for (uint32_t i = nextIndex; i < fontListLength; i++) {
        nsRefPtr<gfxFont> font = mFonts[i].Font();
        if (font->HasCharacter(aCh)) {
            *aMatchType = gfxTextRange::kFontGroup;
            return font.forget();
        }

        font = TryAllFamilyMembers(mFonts[i].Family(), aCh);
        if (font) {
            *aMatchType = gfxTextRange::kFontGroup;
            return font.forget();
        }
    }

    // if character is in Private Use Area, don't do matching against pref or system fonts
    if ((aCh >= 0xE000  && aCh <= 0xF8FF) || (aCh >= 0xF0000 && aCh <= 0x10FFFD))
        return nullptr;

    // 2. search pref fonts
    nsRefPtr<gfxFont> font = WhichPrefFontSupportsChar(aCh);
    if (font) {
        *aMatchType = gfxTextRange::kPrefsFallback;
        return font.forget();
    }

    // 3. use fallback fonts
    // -- before searching for something else check the font used for the previous character
    if (aPrevMatchedFont && aPrevMatchedFont->HasCharacter(aCh)) {
        *aMatchType = gfxTextRange::kSystemFallback;
        aPrevMatchedFont->AddRef();
        return aPrevMatchedFont;
    }

    // never fall back for characters from unknown scripts
    if (aRunScript == HB_SCRIPT_UNKNOWN) {
        return nullptr;
    }

    // for known "space" characters, don't do a full system-fallback search;
    // we'll synthesize appropriate-width spaces instead of missing-glyph boxes
    if (GetGeneralCategory(aCh) ==
            HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR &&
        GetFontAt(0)->SynthesizeSpaceWidth(aCh) >= 0.0)
    {
        return nullptr;
    }

    // -- otherwise look for other stuff
    *aMatchType = gfxTextRange::kSystemFallback;
    font = WhichSystemFontSupportsChar(aCh, aRunScript);
    return font.forget();
}

template<typename T>
void gfxFontGroup::ComputeRanges(nsTArray<gfxTextRange>& aRanges,
                                 const T *aString, uint32_t aLength,
                                 int32_t aRunScript)
{
    NS_ASSERTION(aRanges.Length() == 0, "aRanges must be initially empty");
    NS_ASSERTION(aLength > 0, "don't call ComputeRanges for zero-length text");

    uint32_t prevCh = 0;
    uint8_t matchType = 0;
    int32_t lastRangeIndex = -1;

    // initialize prevFont to the group's primary font, so that this will be
    // used for string-initial control chars, etc rather than risk hitting font
    // fallback for these (bug 716229)
    gfxFont *prevFont = GetFontAt(0);

    for (uint32_t i = 0; i < aLength; i++) {

        const uint32_t origI = i; // save off in case we increase for surrogate

        // set up current ch
        uint32_t ch = aString[i];

        // in 16-bit case only, check for surrogate pair
        if (sizeof(T) == sizeof(PRUnichar)) {
            if ((i + 1 < aLength) && NS_IS_HIGH_SURROGATE(ch) &&
                                 NS_IS_LOW_SURROGATE(aString[i + 1])) {
                i++;
                ch = SURROGATE_TO_UCS4(ch, aString[i]);
            }
        }

        if (ch == 0xa0) {
            ch = ' ';
        }

        // find the font for this char
        nsRefPtr<gfxFont> font =
            FindFontForChar(ch, prevCh, aRunScript, prevFont, &matchType);

        prevCh = ch;

        if (lastRangeIndex == -1) {
            // first char ==> make a new range
            aRanges.AppendElement(gfxTextRange(0, 1, font, matchType));
            lastRangeIndex++;
            prevFont = font;
        } else {
            // if font has changed, make a new range
            gfxTextRange& prevRange = aRanges[lastRangeIndex];
            if (prevRange.font != font || prevRange.matchType != matchType) {
                // close out the previous range
                prevRange.end = origI;
                aRanges.AppendElement(gfxTextRange(origI, i + 1,
                                                   font, matchType));
                lastRangeIndex++;

                // update prevFont for the next match, *unless* we switched
                // fonts on a ZWJ, in which case propagating the changed font
                // is probably not a good idea (see bug 619511)
                if (sizeof(T) == sizeof(uint8_t) ||
                    !gfxFontUtils::IsJoinCauser(ch))
                {
                    prevFont = font;
                }
            }
        }
    }

    aRanges[lastRangeIndex].end = aLength;
}

gfxUserFontSet* 
gfxFontGroup::GetUserFontSet()
{
    return mUserFontSet;
}

void 
gfxFontGroup::SetUserFontSet(gfxUserFontSet *aUserFontSet)
{
    NS_IF_RELEASE(mUserFontSet);
    mUserFontSet = aUserFontSet;
    NS_IF_ADDREF(mUserFontSet);
    mCurrGeneration = GetGeneration();
}

uint64_t
gfxFontGroup::GetGeneration()
{
    if (!mUserFontSet)
        return 0;
    return mUserFontSet->GetGeneration();
}

void
gfxFontGroup::UpdateFontList()
{
    if (mUserFontSet && mCurrGeneration != GetGeneration()) {
        // xxx - can probably improve this to detect when all fonts were found, so no need to update list
        mFonts.Clear();
        mUnderlineOffset = UNDERLINE_OFFSET_NOT_SET;
        mSkipDrawing = false;

        // bug 548184 - need to clean up FT2, OS/2 platform code to use BuildFontList
#if defined(XP_MACOSX) || defined(XP_IOS) || defined(XP_WIN) || defined(ANDROID)
        BuildFontList();
#else
        ForEachFont(FindPlatformFont, this);
#endif
        mCurrGeneration = GetGeneration();
    }
}

struct PrefFontCallbackData {
    PrefFontCallbackData(nsTArray<nsRefPtr<gfxFontFamily> >& aFamiliesArray)
        : mPrefFamilies(aFamiliesArray)
    {}

    nsTArray<nsRefPtr<gfxFontFamily> >& mPrefFamilies;

    static bool AddFontFamilyEntry(eFontPrefLang aLang, const nsAString& aName, void *aClosure)
    {
        PrefFontCallbackData *prefFontData = static_cast<PrefFontCallbackData*>(aClosure);

        gfxFontFamily *family = gfxPlatformFontList::PlatformFontList()->FindFamily(aName);
        if (family) {
            prefFontData->mPrefFamilies.AppendElement(family);
        }
        return true;
    }
};

already_AddRefed<gfxFont>
gfxFontGroup::WhichPrefFontSupportsChar(uint32_t aCh)
{
    gfxFont *font;

    // get the pref font list if it hasn't been set up already
    uint32_t unicodeRange = FindCharUnicodeRange(aCh);
    eFontPrefLang charLang = gfxPlatform::GetPlatform()->GetFontPrefLangFor(unicodeRange);

    // if the last pref font was the first family in the pref list, no need to recheck through a list of families
    if (mLastPrefFont && charLang == mLastPrefLang &&
        mLastPrefFirstFont && mLastPrefFont->HasCharacter(aCh)) {
        font = mLastPrefFont;
        NS_ADDREF(font);
        return font;
    }

    // based on char lang and page lang, set up list of pref lang fonts to check
    eFontPrefLang prefLangs[kMaxLenPrefLangList];
    uint32_t i, numLangs = 0;

    gfxPlatform::GetPlatform()->GetLangPrefs(prefLangs, numLangs, charLang, mPageLang);

    for (i = 0; i < numLangs; i++) {
        nsAutoTArray<nsRefPtr<gfxFontFamily>, 5> families;
        eFontPrefLang currentLang = prefLangs[i];

        gfxPlatformFontList *fontList = gfxPlatformFontList::PlatformFontList();

        // get the pref families for a single pref lang
        if (!fontList->GetPrefFontFamilyEntries(currentLang, &families)) {
            eFontPrefLang prefLangsToSearch[1] = { currentLang };
            PrefFontCallbackData prefFontData(families);
            gfxPlatform::ForEachPrefFont(prefLangsToSearch, 1, PrefFontCallbackData::AddFontFamilyEntry,
                                           &prefFontData);
            fontList->SetPrefFontFamilyEntries(currentLang, families);
        }

        // find the first pref font that includes the character
        uint32_t  j, numPrefs;
        numPrefs = families.Length();
        for (j = 0; j < numPrefs; j++) {
            // look up the appropriate face
            gfxFontFamily *family = families[j];
            if (!family) continue;

            // if a pref font is used, it's likely to be used again in the same text run.
            // the style doesn't change so the face lookup can be cached rather than calling
            // FindOrMakeFont repeatedly.  speeds up FindFontForChar lookup times for subsequent
            // pref font lookups
            if (family == mLastPrefFamily && mLastPrefFont->HasCharacter(aCh)) {
                font = mLastPrefFont;
                NS_ADDREF(font);
                return font;
            }

            bool needsBold;
            gfxFontEntry *fe = family->FindFontForStyle(mStyle, needsBold);
            // if ch in cmap, create and return a gfxFont
            if (fe && fe->TestCharacterMap(aCh)) {
                nsRefPtr<gfxFont> prefFont = fe->FindOrMakeFont(&mStyle, needsBold);
                if (!prefFont) continue;
                mLastPrefFamily = family;
                mLastPrefFont = prefFont;
                mLastPrefLang = charLang;
                mLastPrefFirstFont = (i == 0 && j == 0);
                return prefFont.forget();
            }

        }
    }

    return nullptr;
}

already_AddRefed<gfxFont>
gfxFontGroup::WhichSystemFontSupportsChar(uint32_t aCh, int32_t aRunScript)
{
    gfxFontEntry *fe = 
        gfxPlatformFontList::PlatformFontList()->
            SystemFindFontForChar(aCh, aRunScript, &mStyle);
    if (fe) {
        bool wantBold = mStyle.ComputeWeight() >= 6;
        nsRefPtr<gfxFont> font =
            fe->FindOrMakeFont(&mStyle, wantBold && !fe->IsBold());
        return font.forget();
    }

    return nullptr;
}

/*static*/ void
gfxFontGroup::Shutdown()
{
    NS_IF_RELEASE(gLangService);
}

nsILanguageAtomService* gfxFontGroup::gLangService = nullptr;


#define DEFAULT_PIXEL_FONT_SIZE 16.0f

/*static*/ uint32_t
gfxFontStyle::ParseFontLanguageOverride(const nsString& aLangTag)
{
  if (!aLangTag.Length() || aLangTag.Length() > 4) {
    return NO_FONT_LANGUAGE_OVERRIDE;
  }
  uint32_t index, result = 0;
  for (index = 0; index < aLangTag.Length(); ++index) {
    PRUnichar ch = aLangTag[index];
    if (!nsCRT::IsAscii(ch)) { // valid tags are pure ASCII
      return NO_FONT_LANGUAGE_OVERRIDE;
    }
    result = (result << 8) + ch;
  }
  while (index++ < 4) {
    result = (result << 8) + 0x20;
  }
  return result;
}

gfxFontStyle::gfxFontStyle() :
    language(nsGkAtoms::x_western),
    size(DEFAULT_PIXEL_FONT_SIZE), sizeAdjust(0.0f),
    languageOverride(NO_FONT_LANGUAGE_OVERRIDE),
    weight(NS_FONT_WEIGHT_NORMAL), stretch(NS_FONT_STRETCH_NORMAL),
    systemFont(true), printerFont(false), 
    style(NS_FONT_STYLE_NORMAL)
{
}

gfxFontStyle::gfxFontStyle(uint8_t aStyle, uint16_t aWeight, int16_t aStretch,
                           gfxFloat aSize, nsIAtom *aLanguage,
                           float aSizeAdjust, bool aSystemFont,
                           bool aPrinterFont,
                           const nsString& aLanguageOverride):
    language(aLanguage),
    size(aSize), sizeAdjust(aSizeAdjust),
    languageOverride(ParseFontLanguageOverride(aLanguageOverride)),
    weight(aWeight), stretch(aStretch),
    systemFont(aSystemFont), printerFont(aPrinterFont),
    style(aStyle)
{
    MOZ_ASSERT(!MOZ_DOUBLE_IS_NaN(size));
    MOZ_ASSERT(!MOZ_DOUBLE_IS_NaN(sizeAdjust));

    if (weight > 900)
        weight = 900;
    if (weight < 100)
        weight = 100;

    if (size >= FONT_MAX_SIZE) {
        size = FONT_MAX_SIZE;
        sizeAdjust = 0.0;
    } else if (size < 0.0) {
        NS_WARNING("negative font size");
        size = 0.0;
    }

    if (!language) {
        NS_WARNING("null language");
        language = nsGkAtoms::x_western;
    }
}

gfxFontStyle::gfxFontStyle(const gfxFontStyle& aStyle) :
    language(aStyle.language),
    size(aStyle.size), sizeAdjust(aStyle.sizeAdjust),
    languageOverride(aStyle.languageOverride),
    weight(aStyle.weight), stretch(aStyle.stretch),
    systemFont(aStyle.systemFont), printerFont(aStyle.printerFont),
    style(aStyle.style)
{
    featureSettings.AppendElements(aStyle.featureSettings);
}

int8_t
gfxFontStyle::ComputeWeight() const
{
    int8_t baseWeight = (weight + 50) / 100;

    if (baseWeight < 0)
        baseWeight = 0;
    if (baseWeight > 9)
        baseWeight = 9;

    return baseWeight;
}

// This is not a member function of gfxShapedWord because it is also used
// by gfxFontGroup on missing-glyph runs, where we don't actually "shape"
// anything but still need to set cluster info.
/*static*/ void
gfxShapedWord::SetupClusterBoundaries(CompressedGlyph *aGlyphs,
                                      const PRUnichar *aString, uint32_t aLength)
{
    gfxTextRun::CompressedGlyph extendCluster;
    extendCluster.SetComplex(false, true, 0);

    ClusterIterator iter(aString, aLength);

    // the ClusterIterator won't be able to tell us if the string
    // _begins_ with a cluster-extender, so we handle that here
    if (aLength && IsClusterExtender(*aString)) {
        *aGlyphs = extendCluster;
    }

    while (!iter.AtEnd()) {
        // advance iter to the next cluster-start (or end of text)
        iter.Next();
        // step past the first char of the cluster
        aString++;
        aGlyphs++;
        // mark all the rest as cluster-continuations
        while (aString < iter) {
            *aGlyphs++ = extendCluster;
            aString++;
        }
    }
}

gfxShapedWord::DetailedGlyph *
gfxShapedWord::AllocateDetailedGlyphs(uint32_t aIndex, uint32_t aCount)
{
    NS_ASSERTION(aIndex < Length(), "Index out of range");

    if (!mDetailedGlyphs) {
        mDetailedGlyphs = new DetailedGlyphStore();
    }

    DetailedGlyph *details = mDetailedGlyphs->Allocate(aIndex, aCount);
    if (!details) {
        mCharacterGlyphs[aIndex].SetMissing(0);
        return nullptr;
    }

    return details;
}

void
gfxShapedWord::SetGlyphs(uint32_t aIndex, CompressedGlyph aGlyph,
                         const DetailedGlyph *aGlyphs)
{
    NS_ASSERTION(!aGlyph.IsSimpleGlyph(), "Simple glyphs not handled here");
    NS_ASSERTION(aIndex > 0 || aGlyph.IsLigatureGroupStart(),
                 "First character can't be a ligature continuation!");

    uint32_t glyphCount = aGlyph.GetGlyphCount();
    if (glyphCount > 0) {
        DetailedGlyph *details = AllocateDetailedGlyphs(aIndex, glyphCount);
        if (!details) {
            return;
        }
        memcpy(details, aGlyphs, sizeof(DetailedGlyph)*glyphCount);
    }
    mCharacterGlyphs[aIndex] = aGlyph;
}

#define ZWNJ 0x200C
#define ZWJ  0x200D
static inline bool
IsDefaultIgnorable(uint32_t aChar)
{
    return GetIdentifierModification(aChar) == XIDMOD_DEFAULT_IGNORABLE ||
           aChar == ZWNJ || aChar == ZWJ;
}

void
gfxShapedWord::SetMissingGlyph(uint32_t aIndex, uint32_t aChar, gfxFont *aFont)
{
    DetailedGlyph *details = AllocateDetailedGlyphs(aIndex, 1);
    if (!details) {
        return;
    }

    details->mGlyphID = aChar;
    if (IsDefaultIgnorable(aChar)) {
        // Setting advance width to zero will prevent drawing the hexbox
        details->mAdvance = 0;
    } else {
        gfxFloat width = NS_MAX(aFont->GetMetrics().aveCharWidth,
                                gfxFontMissingGlyphs::GetDesiredMinWidth(aChar));
        details->mAdvance = uint32_t(width * mAppUnitsPerDevUnit);
    }
    details->mXOffset = 0;
    details->mYOffset = 0;
    mCharacterGlyphs[aIndex].SetMissing(1);
}

bool
gfxShapedWord::FilterIfIgnorable(uint32_t aIndex)
{
    uint32_t ch = GetCharAt(aIndex);
    if (IsDefaultIgnorable(ch)) {
        DetailedGlyph *details = AllocateDetailedGlyphs(aIndex, 1);
        if (details) {
            details->mGlyphID = ch;
            details->mAdvance = 0;
            details->mXOffset = 0;
            details->mYOffset = 0;
            mCharacterGlyphs[aIndex].SetMissing(1);
            return true;
        }
    }
    return false;
}

void
gfxShapedWord::AdjustAdvancesForSyntheticBold(float aSynBoldOffset)
{
    uint32_t synAppUnitOffset = aSynBoldOffset * mAppUnitsPerDevUnit;
    for (uint32_t i = 0; i < Length(); ++i) {
         CompressedGlyph *glyphData = &mCharacterGlyphs[i];
         if (glyphData->IsSimpleGlyph()) {
             // simple glyphs ==> just add the advance
             int32_t advance = glyphData->GetSimpleAdvance() + synAppUnitOffset;
             if (CompressedGlyph::IsSimpleAdvance(advance)) {
                 glyphData->SetSimpleGlyph(advance, glyphData->GetSimpleGlyph());
             } else {
                 // rare case, tested by making this the default
                 uint32_t glyphIndex = glyphData->GetSimpleGlyph();
                 glyphData->SetComplex(true, true, 1);
                 DetailedGlyph detail = {glyphIndex, advance, 0, 0};
                 SetGlyphs(i, *glyphData, &detail);
             }
         } else {
             // complex glyphs ==> add offset at cluster/ligature boundaries
             uint32_t detailedLength = glyphData->GetGlyphCount();
             if (detailedLength) {
                 DetailedGlyph *details = GetDetailedGlyphs(i);
                 if (!details) {
                     continue;
                 }
                 if (IsRightToLeft()) {
                     details[0].mAdvance += synAppUnitOffset;
                 } else {
                     details[detailedLength - 1].mAdvance += synAppUnitOffset;
                 }
             }
         }
    }
}

bool
gfxTextRun::GlyphRunIterator::NextRun()  {
    if (mNextIndex >= mTextRun->mGlyphRuns.Length())
        return false;
    mGlyphRun = &mTextRun->mGlyphRuns[mNextIndex];
    if (mGlyphRun->mCharacterOffset >= mEndOffset)
        return false;

    mStringStart = NS_MAX(mStartOffset, mGlyphRun->mCharacterOffset);
    uint32_t last = mNextIndex + 1 < mTextRun->mGlyphRuns.Length()
        ? mTextRun->mGlyphRuns[mNextIndex + 1].mCharacterOffset : mTextRun->mCharacterCount;
    mStringEnd = NS_MIN(mEndOffset, last);

    ++mNextIndex;
    return true;
}

#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
static void
AccountStorageForTextRun(gfxTextRun *aTextRun, int32_t aSign)
{
    // Ignores detailed glyphs... we don't know when those have been constructed
    // Also ignores gfxSkipChars dynamic storage (which won't be anything
    // for preformatted text)
    // Also ignores GlyphRun array, again because it hasn't been constructed
    // by the time this gets called. If there's only one glyphrun that's stored
    // directly in the textrun anyway so no additional overhead.
    uint32_t length = aTextRun->GetLength();
    int32_t bytes = length * sizeof(gfxTextRun::CompressedGlyph);
    bytes += sizeof(gfxTextRun);
    gTextRunStorage += bytes*aSign;
    gTextRunStorageHighWaterMark = NS_MAX(gTextRunStorageHighWaterMark, gTextRunStorage);
}
#endif

// Helper for textRun creation to preallocate storage for glyph records;
// this function returns a pointer to the newly-allocated glyph storage.
// Returns nullptr if allocation fails.
void *
gfxTextRun::AllocateStorageForTextRun(size_t aSize, uint32_t aLength)
{
    // Allocate the storage we need, returning nullptr on failure rather than
    // throwing an exception (because web content can create huge runs).
    void *storage = moz_malloc(aSize + aLength * sizeof(CompressedGlyph));
    if (!storage) {
        NS_WARNING("failed to allocate storage for text run!");
        return nullptr;
    }

    // Initialize the glyph storage (beyond aSize) to zero
    memset(reinterpret_cast<char*>(storage) + aSize, 0,
           aLength * sizeof(CompressedGlyph));

    return storage;
}

gfxTextRun *
gfxTextRun::Create(const gfxTextRunFactory::Parameters *aParams,
                   uint32_t aLength, gfxFontGroup *aFontGroup, uint32_t aFlags)
{
    void *storage = AllocateStorageForTextRun(sizeof(gfxTextRun), aLength);
    if (!storage) {
        return nullptr;
    }

    return new (storage) gfxTextRun(aParams, aLength, aFontGroup, aFlags);
}

gfxTextRun::gfxTextRun(const gfxTextRunFactory::Parameters *aParams,
                       uint32_t aLength, gfxFontGroup *aFontGroup, uint32_t aFlags)
  : mUserData(aParams->mUserData),
    mFontGroup(aFontGroup),
    mAppUnitsPerDevUnit(aParams->mAppUnitsPerDevUnit),
    mFlags(aFlags), mCharacterCount(aLength)
{
    NS_ASSERTION(mAppUnitsPerDevUnit != 0, "Invalid app unit scale");
    MOZ_COUNT_CTOR(gfxTextRun);
    NS_ADDREF(mFontGroup);

    mCharacterGlyphs = reinterpret_cast<CompressedGlyph*>(this + 1);

    if (aParams->mSkipChars) {
        mSkipChars.TakeFrom(aParams->mSkipChars);
    }

#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
    AccountStorageForTextRun(this, 1);
#endif

    mSkipDrawing = mFontGroup->ShouldSkipDrawing();
}

gfxTextRun::~gfxTextRun()
{
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
    AccountStorageForTextRun(this, -1);
#endif
#ifdef DEBUG
    // Make it easy to detect a dead text run
    mFlags = 0xFFFFFFFF;
#endif

    NS_RELEASE(mFontGroup);
    MOZ_COUNT_DTOR(gfxTextRun);
}

bool
gfxTextRun::SetPotentialLineBreaks(uint32_t aStart, uint32_t aLength,
                                   uint8_t *aBreakBefore,
                                   gfxContext *aRefContext)
{
    NS_ASSERTION(aStart + aLength <= mCharacterCount, "Overflow");

    uint32_t changed = 0;
    uint32_t i;
    CompressedGlyph *charGlyphs = mCharacterGlyphs + aStart;
    for (i = 0; i < aLength; ++i) {
        uint8_t canBreak = aBreakBefore[i];
        if (canBreak && !charGlyphs[i].IsClusterStart()) {
            // This can happen ... there is no guarantee that our linebreaking rules
            // align with the platform's idea of what constitutes a cluster.
            NS_WARNING("Break suggested inside cluster!");
            canBreak = CompressedGlyph::FLAG_BREAK_TYPE_NONE;
        }
        changed |= charGlyphs[i].SetCanBreakBefore(canBreak);
    }
    return changed != 0;
}

gfxTextRun::LigatureData
gfxTextRun::ComputeLigatureData(uint32_t aPartStart, uint32_t aPartEnd,
                                PropertyProvider *aProvider)
{
    NS_ASSERTION(aPartStart < aPartEnd, "Computing ligature data for empty range");
    NS_ASSERTION(aPartEnd <= mCharacterCount, "Character length overflow");
  
    LigatureData result;
    CompressedGlyph *charGlyphs = mCharacterGlyphs;

    uint32_t i;
    for (i = aPartStart; !charGlyphs[i].IsLigatureGroupStart(); --i) {
        NS_ASSERTION(i > 0, "Ligature at the start of the run??");
    }
    result.mLigatureStart = i;
    for (i = aPartStart + 1; i < mCharacterCount && !charGlyphs[i].IsLigatureGroupStart(); ++i) {
    }
    result.mLigatureEnd = i;

    int32_t ligatureWidth =
        GetAdvanceForGlyphs(result.mLigatureStart, result.mLigatureEnd);
    // Count the number of started clusters we have seen
    uint32_t totalClusterCount = 0;
    uint32_t partClusterIndex = 0;
    uint32_t partClusterCount = 0;
    for (i = result.mLigatureStart; i < result.mLigatureEnd; ++i) {
        // Treat the first character of the ligature as the start of a
        // cluster for our purposes of allocating ligature width to its
        // characters.
        if (i == result.mLigatureStart || charGlyphs[i].IsClusterStart()) {
            ++totalClusterCount;
            if (i < aPartStart) {
                ++partClusterIndex;
            } else if (i < aPartEnd) {
                ++partClusterCount;
            }
        }
    }
    NS_ASSERTION(totalClusterCount > 0, "Ligature involving no clusters??");
    result.mPartAdvance = partClusterIndex * (ligatureWidth / totalClusterCount);
    result.mPartWidth = partClusterCount * (ligatureWidth / totalClusterCount);

    // Any rounding errors are apportioned to the final part of the ligature,
    // so that measuring all parts of a ligature and summing them is equal to
    // the ligature width.
    if (aPartEnd == result.mLigatureEnd) {
        gfxFloat allParts = totalClusterCount * (ligatureWidth / totalClusterCount);
        result.mPartWidth += ligatureWidth - allParts;
    }

    if (partClusterCount == 0) {
        // nothing to draw
        result.mClipBeforePart = result.mClipAfterPart = true;
    } else {
        // Determine whether we should clip before or after this part when
        // drawing its slice of the ligature.
        // We need to clip before the part if any cluster is drawn before
        // this part.
        result.mClipBeforePart = partClusterIndex > 0;
        // We need to clip after the part if any cluster is drawn after
        // this part.
        result.mClipAfterPart = partClusterIndex + partClusterCount < totalClusterCount;
    }

    if (aProvider && (mFlags & gfxTextRunFactory::TEXT_ENABLE_SPACING)) {
        gfxFont::Spacing spacing;
        if (aPartStart == result.mLigatureStart) {
            aProvider->GetSpacing(aPartStart, 1, &spacing);
            result.mPartWidth += spacing.mBefore;
        }
        if (aPartEnd == result.mLigatureEnd) {
            aProvider->GetSpacing(aPartEnd - 1, 1, &spacing);
            result.mPartWidth += spacing.mAfter;
        }
    }

    return result;
}

gfxFloat
gfxTextRun::ComputePartialLigatureWidth(uint32_t aPartStart, uint32_t aPartEnd,
                                        PropertyProvider *aProvider)
{
    if (aPartStart >= aPartEnd)
        return 0;
    LigatureData data = ComputeLigatureData(aPartStart, aPartEnd, aProvider);
    return data.mPartWidth;
}

int32_t
gfxTextRun::GetAdvanceForGlyphs(uint32_t aStart, uint32_t aEnd)
{
    const CompressedGlyph *glyphData = mCharacterGlyphs + aStart;
    int32_t advance = 0;
    uint32_t i;
    for (i = aStart; i < aEnd; ++i, ++glyphData) {
        if (glyphData->IsSimpleGlyph()) {
            advance += glyphData->GetSimpleAdvance();   
        } else {
            uint32_t glyphCount = glyphData->GetGlyphCount();
            if (glyphCount == 0) {
                continue;
            }
            const DetailedGlyph *details = GetDetailedGlyphs(i);
            if (details) {
                uint32_t j;
                for (j = 0; j < glyphCount; ++j, ++details) {
                    advance += details->mAdvance;
                }
            }
        }
    }
    return advance;
}

static void
GetAdjustedSpacing(gfxTextRun *aTextRun, uint32_t aStart, uint32_t aEnd,
                   gfxTextRun::PropertyProvider *aProvider,
                   gfxTextRun::PropertyProvider::Spacing *aSpacing)
{
    if (aStart >= aEnd)
        return;

    aProvider->GetSpacing(aStart, aEnd - aStart, aSpacing);

#ifdef DEBUG
    // Check to see if we have spacing inside ligatures

    const gfxTextRun::CompressedGlyph *charGlyphs = aTextRun->GetCharacterGlyphs();
    uint32_t i;

    for (i = aStart; i < aEnd; ++i) {
        if (!charGlyphs[i].IsLigatureGroupStart()) {
            NS_ASSERTION(i == aStart || aSpacing[i - aStart].mBefore == 0,
                         "Before-spacing inside a ligature!");
            NS_ASSERTION(i - 1 <= aStart || aSpacing[i - 1 - aStart].mAfter == 0,
                         "After-spacing inside a ligature!");
        }
    }
#endif
}

bool
gfxTextRun::GetAdjustedSpacingArray(uint32_t aStart, uint32_t aEnd,
                                    PropertyProvider *aProvider,
                                    uint32_t aSpacingStart, uint32_t aSpacingEnd,
                                    nsTArray<PropertyProvider::Spacing> *aSpacing)
{
    if (!aProvider || !(mFlags & gfxTextRunFactory::TEXT_ENABLE_SPACING))
        return false;
    if (!aSpacing->AppendElements(aEnd - aStart))
        return false;
    memset(aSpacing->Elements(), 0, sizeof(gfxFont::Spacing)*(aSpacingStart - aStart));
    GetAdjustedSpacing(this, aSpacingStart, aSpacingEnd, aProvider,
                       aSpacing->Elements() + aSpacingStart - aStart);
    memset(aSpacing->Elements() + aSpacingEnd - aStart, 0, sizeof(gfxFont::Spacing)*(aEnd - aSpacingEnd));
    return true;
}

void
gfxTextRun::ShrinkToLigatureBoundaries(uint32_t *aStart, uint32_t *aEnd)
{
    if (*aStart >= *aEnd)
        return;
  
    CompressedGlyph *charGlyphs = mCharacterGlyphs;

    while (*aStart < *aEnd && !charGlyphs[*aStart].IsLigatureGroupStart()) {
        ++(*aStart);
    }
    if (*aEnd < mCharacterCount) {
        while (*aEnd > *aStart && !charGlyphs[*aEnd].IsLigatureGroupStart()) {
            --(*aEnd);
        }
    }
}

void
gfxTextRun::DrawGlyphs(gfxFont *aFont, gfxContext *aContext,
                       gfxFont::DrawMode aDrawMode, gfxPoint *aPt,
                       gfxTextObjectPaint *aObjectPaint,
                       uint32_t aStart, uint32_t aEnd,
                       PropertyProvider *aProvider,
                       uint32_t aSpacingStart, uint32_t aSpacingEnd)
{
    nsAutoTArray<PropertyProvider::Spacing,200> spacingBuffer;
    bool haveSpacing = GetAdjustedSpacingArray(aStart, aEnd, aProvider,
        aSpacingStart, aSpacingEnd, &spacingBuffer);
    aFont->Draw(this, aStart, aEnd, aContext, aDrawMode, aPt,
                haveSpacing ? spacingBuffer.Elements() : nullptr, aObjectPaint);
}

static void
ClipPartialLigature(gfxTextRun *aTextRun, gfxFloat *aLeft, gfxFloat *aRight,
                    gfxFloat aXOrigin, gfxTextRun::LigatureData *aLigature)
{
    if (aLigature->mClipBeforePart) {
        if (aTextRun->IsRightToLeft()) {
            *aRight = NS_MIN(*aRight, aXOrigin);
        } else {
            *aLeft = NS_MAX(*aLeft, aXOrigin);
        }
    }
    if (aLigature->mClipAfterPart) {
        gfxFloat endEdge = aXOrigin + aTextRun->GetDirection()*aLigature->mPartWidth;
        if (aTextRun->IsRightToLeft()) {
            *aLeft = NS_MAX(*aLeft, endEdge);
        } else {
            *aRight = NS_MIN(*aRight, endEdge);
        }
    }    
}

void
gfxTextRun::DrawPartialLigature(gfxFont *aFont, gfxContext *aCtx,
                                uint32_t aStart, uint32_t aEnd,
                                gfxPoint *aPt,
                                PropertyProvider *aProvider,
                                gfxTextRun::DrawCallbacks *aCallbacks)
{
    if (aStart >= aEnd)
        return;

    // Draw partial ligature. We hack this by clipping the ligature.
    LigatureData data = ComputeLigatureData(aStart, aEnd, aProvider);
    gfxRect clipExtents = aCtx->GetClipExtents();
    gfxFloat left = clipExtents.X()*mAppUnitsPerDevUnit;
    gfxFloat right = clipExtents.XMost()*mAppUnitsPerDevUnit;
    ClipPartialLigature(this, &left, &right, aPt->x, &data);

    {
      // Need to preserve the path, otherwise this can break canvas text-on-path;
      // in general it seems like a good thing, as naive callers probably won't
      // expect gfxTextRun::Draw to implicitly destroy the current path.
      gfxContextPathAutoSaveRestore savePath(aCtx);

      // use division here to ensure that when the rect is aligned on multiples
      // of mAppUnitsPerDevUnit, we clip to true device unit boundaries.
      // Also, make sure we snap the rectangle to device pixels.
      aCtx->Save();
      aCtx->NewPath();
      aCtx->Rectangle(gfxRect(left / mAppUnitsPerDevUnit,
                              clipExtents.Y(),
                              (right - left) / mAppUnitsPerDevUnit,
                              clipExtents.Height()), true);
      aCtx->Clip();
    }

    gfxFloat direction = GetDirection();
    gfxPoint pt(aPt->x - direction*data.mPartAdvance, aPt->y);
    DrawGlyphs(aFont, aCtx,
               aCallbacks ? gfxFont::GLYPH_PATH : gfxFont::GLYPH_FILL, &pt,
               nullptr, data.mLigatureStart, data.mLigatureEnd, aProvider,
               aStart, aEnd);
    if (aCallbacks) {
      aCallbacks->NotifyGlyphPathEmitted();
    }
    aCtx->Restore();

    aPt->x += direction*data.mPartWidth;
}

// returns true if a glyph run is using a font with synthetic bolding enabled, false otherwise
static bool
HasSyntheticBold(gfxTextRun *aRun, uint32_t aStart, uint32_t aLength)
{
    gfxTextRun::GlyphRunIterator iter(aRun, aStart, aLength);
    while (iter.NextRun()) {
        gfxFont *font = iter.GetGlyphRun()->mFont;
        if (font && font->IsSyntheticBold()) {
            return true;
        }
    }

    return false;
}

// returns true if color is non-opaque (i.e. alpha != 1.0) or completely transparent, false otherwise
// if true, color is set on output
static bool
HasNonOpaqueColor(gfxContext *aContext, gfxRGBA& aCurrentColor)
{
    if (aContext->GetDeviceColor(aCurrentColor)) {
        if (aCurrentColor.a < 1.0 && aCurrentColor.a > 0.0) {
            return true;
        }
    }
        
    return false;
}

// helper class for double-buffering drawing with non-opaque color
struct BufferAlphaColor {
    BufferAlphaColor(gfxContext *aContext)
        : mContext(aContext)
    {

    }

    ~BufferAlphaColor() {}

    void PushSolidColor(const gfxRect& aBounds, const gfxRGBA& aAlphaColor, uint32_t appsPerDevUnit)
    {
        mContext->Save();
        mContext->NewPath();
        mContext->Rectangle(gfxRect(aBounds.X() / appsPerDevUnit,
                    aBounds.Y() / appsPerDevUnit,
                    aBounds.Width() / appsPerDevUnit,
                    aBounds.Height() / appsPerDevUnit), true);
        mContext->Clip();
        mContext->SetColor(gfxRGBA(aAlphaColor.r, aAlphaColor.g, aAlphaColor.b));
        mContext->PushGroup(gfxASurface::CONTENT_COLOR_ALPHA);
        mAlpha = aAlphaColor.a;
    }

    void PopAlpha()
    {
        // pop the text, using the color alpha as the opacity
        mContext->PopGroupToSource();
        mContext->SetOperator(gfxContext::OPERATOR_OVER);
        mContext->Paint(mAlpha);
        mContext->Restore();
    }

    gfxContext *mContext;
    gfxFloat mAlpha;
};

void
gfxTextRun::Draw(gfxContext *aContext, gfxPoint aPt, gfxFont::DrawMode aDrawMode,
                 uint32_t aStart, uint32_t aLength,
                 PropertyProvider *aProvider, gfxFloat *aAdvanceWidth,
                 gfxTextObjectPaint *aObjectPaint,
                 gfxTextRun::DrawCallbacks *aCallbacks)
{
    NS_ASSERTION(aStart + aLength <= mCharacterCount, "Substring out of range");
    NS_ASSERTION(aDrawMode <= gfxFont::GLYPH_PATH, "GLYPH_PATH cannot be used with GLYPH_FILL or GLYPH_STROKE");
    NS_ASSERTION(aDrawMode == gfxFont::GLYPH_PATH || !aCallbacks, "callback must not be specified unless using GLYPH_PATH");

    gfxFloat direction = GetDirection();

    if (mSkipDrawing) {
        // We're waiting for a user font to finish downloading;
        // but if the caller wants advance width, we need to compute it here
        if (aAdvanceWidth) {
            gfxTextRun::Metrics metrics = MeasureText(aStart, aLength,
                                                      gfxFont::LOOSE_INK_EXTENTS,
                                                      aContext, aProvider);
            *aAdvanceWidth = metrics.mAdvanceWidth * direction;
        }

        // return without drawing
        return;
    }

    gfxPoint pt = aPt;

    // synthetic bolding draws glyphs twice ==> colors with opacity won't draw correctly unless first drawn without alpha
    BufferAlphaColor syntheticBoldBuffer(aContext);
    gfxRGBA currentColor;
    bool needToRestore = false;

    if (aDrawMode == gfxFont::GLYPH_FILL && HasNonOpaqueColor(aContext, currentColor)
                                         && HasSyntheticBold(this, aStart, aLength)) {
        needToRestore = true;
        // measure text, use the bounding box
        gfxTextRun::Metrics metrics = MeasureText(aStart, aLength, gfxFont::LOOSE_INK_EXTENTS,
                                                  aContext, aProvider);
        metrics.mBoundingBox.MoveBy(aPt);
        syntheticBoldBuffer.PushSolidColor(metrics.mBoundingBox, currentColor, GetAppUnitsPerDevUnit());
    }

    GlyphRunIterator iter(this, aStart, aLength);
    while (iter.NextRun()) {
        gfxFont *font = iter.GetGlyphRun()->mFont;
        uint32_t start = iter.GetStringStart();
        uint32_t end = iter.GetStringEnd();
        uint32_t ligatureRunStart = start;
        uint32_t ligatureRunEnd = end;
        ShrinkToLigatureBoundaries(&ligatureRunStart, &ligatureRunEnd);
        
        bool drawPartial = aDrawMode == gfxFont::GLYPH_FILL ||
                           (aDrawMode == gfxFont::GLYPH_PATH && aCallbacks);

        if (drawPartial) {
            DrawPartialLigature(font, aContext, start, ligatureRunStart, &pt,
                                aProvider, aCallbacks);
        }

        DrawGlyphs(font, aContext, aDrawMode, &pt, aObjectPaint, ligatureRunStart,
                   ligatureRunEnd, aProvider, ligatureRunStart, ligatureRunEnd);

        if (aCallbacks) {
          aCallbacks->NotifyGlyphPathEmitted();
        }

        if (drawPartial) {
            DrawPartialLigature(font, aContext, ligatureRunEnd, end, &pt,
                                aProvider, aCallbacks);
        }
    }

    // composite result when synthetic bolding used
    if (needToRestore) {
        syntheticBoldBuffer.PopAlpha();
    }

    if (aAdvanceWidth) {
        *aAdvanceWidth = (pt.x - aPt.x)*direction;
    }
}

void
gfxTextRun::AccumulateMetricsForRun(gfxFont *aFont,
                                    uint32_t aStart, uint32_t aEnd,
                                    gfxFont::BoundingBoxType aBoundingBoxType,
                                    gfxContext *aRefContext,
                                    PropertyProvider *aProvider,
                                    uint32_t aSpacingStart, uint32_t aSpacingEnd,
                                    Metrics *aMetrics)
{
    nsAutoTArray<PropertyProvider::Spacing,200> spacingBuffer;
    bool haveSpacing = GetAdjustedSpacingArray(aStart, aEnd, aProvider,
        aSpacingStart, aSpacingEnd, &spacingBuffer);
    Metrics metrics = aFont->Measure(this, aStart, aEnd, aBoundingBoxType, aRefContext,
                                     haveSpacing ? spacingBuffer.Elements() : nullptr);
    aMetrics->CombineWith(metrics, IsRightToLeft());
}

void
gfxTextRun::AccumulatePartialLigatureMetrics(gfxFont *aFont,
    uint32_t aStart, uint32_t aEnd,
    gfxFont::BoundingBoxType aBoundingBoxType, gfxContext *aRefContext,
    PropertyProvider *aProvider, Metrics *aMetrics)
{
    if (aStart >= aEnd)
        return;

    // Measure partial ligature. We hack this by clipping the metrics in the
    // same way we clip the drawing.
    LigatureData data = ComputeLigatureData(aStart, aEnd, aProvider);

    // First measure the complete ligature
    Metrics metrics;
    AccumulateMetricsForRun(aFont, data.mLigatureStart, data.mLigatureEnd,
                            aBoundingBoxType, aRefContext,
                            aProvider, aStart, aEnd, &metrics);

    // Clip the bounding box to the ligature part
    gfxFloat bboxLeft = metrics.mBoundingBox.X();
    gfxFloat bboxRight = metrics.mBoundingBox.XMost();
    // Where we are going to start "drawing" relative to our left baseline origin
    gfxFloat origin = IsRightToLeft() ? metrics.mAdvanceWidth - data.mPartAdvance : 0;
    ClipPartialLigature(this, &bboxLeft, &bboxRight, origin, &data);
    metrics.mBoundingBox.x = bboxLeft;
    metrics.mBoundingBox.width = bboxRight - bboxLeft;

    // mBoundingBox is now relative to the left baseline origin for the entire
    // ligature. Shift it left.
    metrics.mBoundingBox.x -=
        IsRightToLeft() ? metrics.mAdvanceWidth - (data.mPartAdvance + data.mPartWidth)
            : data.mPartAdvance;    
    metrics.mAdvanceWidth = data.mPartWidth;

    aMetrics->CombineWith(metrics, IsRightToLeft());
}

gfxTextRun::Metrics
gfxTextRun::MeasureText(uint32_t aStart, uint32_t aLength,
                        gfxFont::BoundingBoxType aBoundingBoxType,
                        gfxContext *aRefContext,
                        PropertyProvider *aProvider)
{
    NS_ASSERTION(aStart + aLength <= mCharacterCount, "Substring out of range");

    Metrics accumulatedMetrics;
    GlyphRunIterator iter(this, aStart, aLength);
    while (iter.NextRun()) {
        gfxFont *font = iter.GetGlyphRun()->mFont;
        uint32_t start = iter.GetStringStart();
        uint32_t end = iter.GetStringEnd();
        uint32_t ligatureRunStart = start;
        uint32_t ligatureRunEnd = end;
        ShrinkToLigatureBoundaries(&ligatureRunStart, &ligatureRunEnd);

        AccumulatePartialLigatureMetrics(font, start, ligatureRunStart,
            aBoundingBoxType, aRefContext, aProvider, &accumulatedMetrics);

        // XXX This sucks. We have to get glyph extents just so we can detect
        // glyphs outside the font box, even when aBoundingBoxType is LOOSE,
        // even though in almost all cases we could get correct results just
        // by getting some ascent/descent from the font and using our stored
        // advance widths.
        AccumulateMetricsForRun(font,
            ligatureRunStart, ligatureRunEnd, aBoundingBoxType,
            aRefContext, aProvider, ligatureRunStart, ligatureRunEnd,
            &accumulatedMetrics);

        AccumulatePartialLigatureMetrics(font, ligatureRunEnd, end,
            aBoundingBoxType, aRefContext, aProvider, &accumulatedMetrics);
    }

    return accumulatedMetrics;
}

#define MEASUREMENT_BUFFER_SIZE 100

uint32_t
gfxTextRun::BreakAndMeasureText(uint32_t aStart, uint32_t aMaxLength,
                                bool aLineBreakBefore, gfxFloat aWidth,
                                PropertyProvider *aProvider,
                                bool aSuppressInitialBreak,
                                gfxFloat *aTrimWhitespace,
                                Metrics *aMetrics,
                                gfxFont::BoundingBoxType aBoundingBoxType,
                                gfxContext *aRefContext,
                                bool *aUsedHyphenation,
                                uint32_t *aLastBreak,
                                bool aCanWordWrap,
                                gfxBreakPriority *aBreakPriority)
{
    aMaxLength = NS_MIN(aMaxLength, mCharacterCount - aStart);

    NS_ASSERTION(aStart + aMaxLength <= mCharacterCount, "Substring out of range");

    uint32_t bufferStart = aStart;
    uint32_t bufferLength = NS_MIN<uint32_t>(aMaxLength, MEASUREMENT_BUFFER_SIZE);
    PropertyProvider::Spacing spacingBuffer[MEASUREMENT_BUFFER_SIZE];
    bool haveSpacing = aProvider && (mFlags & gfxTextRunFactory::TEXT_ENABLE_SPACING) != 0;
    if (haveSpacing) {
        GetAdjustedSpacing(this, bufferStart, bufferStart + bufferLength, aProvider,
                           spacingBuffer);
    }
    bool hyphenBuffer[MEASUREMENT_BUFFER_SIZE];
    bool haveHyphenation = aProvider &&
        (aProvider->GetHyphensOption() == NS_STYLE_HYPHENS_AUTO ||
         (aProvider->GetHyphensOption() == NS_STYLE_HYPHENS_MANUAL &&
          (mFlags & gfxTextRunFactory::TEXT_ENABLE_HYPHEN_BREAKS) != 0));
    if (haveHyphenation) {
        aProvider->GetHyphenationBreaks(bufferStart, bufferLength,
                                        hyphenBuffer);
    }

    gfxFloat width = 0;
    gfxFloat advance = 0;
    // The number of space characters that can be trimmed
    uint32_t trimmableChars = 0;
    // The amount of space removed by ignoring trimmableChars
    gfxFloat trimmableAdvance = 0;
    int32_t lastBreak = -1;
    int32_t lastBreakTrimmableChars = -1;
    gfxFloat lastBreakTrimmableAdvance = -1;
    bool aborted = false;
    uint32_t end = aStart + aMaxLength;
    bool lastBreakUsedHyphenation = false;

    uint32_t ligatureRunStart = aStart;
    uint32_t ligatureRunEnd = end;
    ShrinkToLigatureBoundaries(&ligatureRunStart, &ligatureRunEnd);

    uint32_t i;
    for (i = aStart; i < end; ++i) {
        if (i >= bufferStart + bufferLength) {
            // Fetch more spacing and hyphenation data
            bufferStart = i;
            bufferLength = NS_MIN(aStart + aMaxLength, i + MEASUREMENT_BUFFER_SIZE) - i;
            if (haveSpacing) {
                GetAdjustedSpacing(this, bufferStart, bufferStart + bufferLength, aProvider,
                                   spacingBuffer);
            }
            if (haveHyphenation) {
                aProvider->GetHyphenationBreaks(bufferStart, bufferLength,
                                                hyphenBuffer);
            }
        }

        // There can't be a word-wrap break opportunity at the beginning of the
        // line: if the width is too small for even one character to fit, it 
        // could be the first and last break opportunity on the line, and that
        // would trigger an infinite loop.
        if (!aSuppressInitialBreak || i > aStart) {
            bool lineBreakHere = mCharacterGlyphs[i].CanBreakBefore() == 1;
            bool hyphenation = haveHyphenation && hyphenBuffer[i - bufferStart];
            bool wordWrapping =
                aCanWordWrap && mCharacterGlyphs[i].IsClusterStart() &&
                *aBreakPriority <= eWordWrapBreak;

            if (lineBreakHere || hyphenation || wordWrapping) {
                gfxFloat hyphenatedAdvance = advance;
                if (!lineBreakHere && !wordWrapping) {
                    hyphenatedAdvance += aProvider->GetHyphenWidth();
                }
            
                if (lastBreak < 0 || width + hyphenatedAdvance - trimmableAdvance <= aWidth) {
                    // We can break here.
                    lastBreak = i;
                    lastBreakTrimmableChars = trimmableChars;
                    lastBreakTrimmableAdvance = trimmableAdvance;
                    lastBreakUsedHyphenation = !lineBreakHere && !wordWrapping;
                    *aBreakPriority = hyphenation || lineBreakHere ?
                        eNormalBreak : eWordWrapBreak;
                }

                width += advance;
                advance = 0;
                if (width - trimmableAdvance > aWidth) {
                    // No more text fits. Abort
                    aborted = true;
                    break;
                }
            }
        }
        
        gfxFloat charAdvance;
        if (i >= ligatureRunStart && i < ligatureRunEnd) {
            charAdvance = GetAdvanceForGlyphs(i, i + 1);
            if (haveSpacing) {
                PropertyProvider::Spacing *space = &spacingBuffer[i - bufferStart];
                charAdvance += space->mBefore + space->mAfter;
            }
        } else {
            charAdvance = ComputePartialLigatureWidth(i, i + 1, aProvider);
        }
        
        advance += charAdvance;
        if (aTrimWhitespace) {
            if (mCharacterGlyphs[i].CharIsSpace()) {
                ++trimmableChars;
                trimmableAdvance += charAdvance;
            } else {
                trimmableAdvance = 0;
                trimmableChars = 0;
            }
        }
    }

    if (!aborted) {
        width += advance;
    }

    // There are three possibilities:
    // 1) all the text fit (width <= aWidth)
    // 2) some of the text fit up to a break opportunity (width > aWidth && lastBreak >= 0)
    // 3) none of the text fits before a break opportunity (width > aWidth && lastBreak < 0)
    uint32_t charsFit;
    bool usedHyphenation = false;
    if (width - trimmableAdvance <= aWidth) {
        charsFit = aMaxLength;
    } else if (lastBreak >= 0) {
        charsFit = lastBreak - aStart;
        trimmableChars = lastBreakTrimmableChars;
        trimmableAdvance = lastBreakTrimmableAdvance;
        usedHyphenation = lastBreakUsedHyphenation;
    } else {
        charsFit = aMaxLength;
    }

    if (aMetrics) {
        *aMetrics = MeasureText(aStart, charsFit - trimmableChars,
            aBoundingBoxType, aRefContext, aProvider);
    }
    if (aTrimWhitespace) {
        *aTrimWhitespace = trimmableAdvance;
    }
    if (aUsedHyphenation) {
        *aUsedHyphenation = usedHyphenation;
    }
    if (aLastBreak && charsFit == aMaxLength) {
        if (lastBreak < 0) {
            *aLastBreak = UINT32_MAX;
        } else {
            *aLastBreak = lastBreak - aStart;
        }
    }

    return charsFit;
}

gfxFloat
gfxTextRun::GetAdvanceWidth(uint32_t aStart, uint32_t aLength,
                            PropertyProvider *aProvider)
{
    NS_ASSERTION(aStart + aLength <= mCharacterCount, "Substring out of range");

    uint32_t ligatureRunStart = aStart;
    uint32_t ligatureRunEnd = aStart + aLength;
    ShrinkToLigatureBoundaries(&ligatureRunStart, &ligatureRunEnd);

    gfxFloat result = ComputePartialLigatureWidth(aStart, ligatureRunStart, aProvider) +
                      ComputePartialLigatureWidth(ligatureRunEnd, aStart + aLength, aProvider);

    // Account for all remaining spacing here. This is more efficient than
    // processing it along with the glyphs.
    if (aProvider && (mFlags & gfxTextRunFactory::TEXT_ENABLE_SPACING)) {
        uint32_t i;
        nsAutoTArray<PropertyProvider::Spacing,200> spacingBuffer;
        if (spacingBuffer.AppendElements(aLength)) {
            GetAdjustedSpacing(this, ligatureRunStart, ligatureRunEnd, aProvider,
                               spacingBuffer.Elements());
            for (i = 0; i < ligatureRunEnd - ligatureRunStart; ++i) {
                PropertyProvider::Spacing *space = &spacingBuffer[i];
                result += space->mBefore + space->mAfter;
            }
        }
    }

    return result + GetAdvanceForGlyphs(ligatureRunStart, ligatureRunEnd);
}

bool
gfxTextRun::SetLineBreaks(uint32_t aStart, uint32_t aLength,
                          bool aLineBreakBefore, bool aLineBreakAfter,
                          gfxFloat *aAdvanceWidthDelta,
                          gfxContext *aRefContext)
{
    // Do nothing because our shaping does not currently take linebreaks into
    // account. There is no change in advance width.
    if (aAdvanceWidthDelta) {
        *aAdvanceWidthDelta = 0;
    }
    return false;
}

uint32_t
gfxTextRun::FindFirstGlyphRunContaining(uint32_t aOffset)
{
    NS_ASSERTION(aOffset <= mCharacterCount, "Bad offset looking for glyphrun");
    NS_ASSERTION(mCharacterCount == 0 || mGlyphRuns.Length() > 0,
                 "non-empty text but no glyph runs present!");
    if (aOffset == mCharacterCount)
        return mGlyphRuns.Length();
    uint32_t start = 0;
    uint32_t end = mGlyphRuns.Length();
    while (end - start > 1) {
        uint32_t mid = (start + end)/2;
        if (mGlyphRuns[mid].mCharacterOffset <= aOffset) {
            start = mid;
        } else {
            end = mid;
        }
    }
    NS_ASSERTION(mGlyphRuns[start].mCharacterOffset <= aOffset,
                 "Hmm, something went wrong, aOffset should have been found");
    return start;
}

nsresult
gfxTextRun::AddGlyphRun(gfxFont *aFont, uint8_t aMatchType,
                        uint32_t aUTF16Offset, bool aForceNewRun)
{
    NS_ASSERTION(aFont, "adding glyph run for null font!");
    if (!aFont) {
        return NS_OK;
    }    
    uint32_t numGlyphRuns = mGlyphRuns.Length();
    if (!aForceNewRun && numGlyphRuns > 0) {
        GlyphRun *lastGlyphRun = &mGlyphRuns[numGlyphRuns - 1];

        NS_ASSERTION(lastGlyphRun->mCharacterOffset <= aUTF16Offset,
                     "Glyph runs out of order (and run not forced)");

        // Don't append a run if the font is already the one we want
        if (lastGlyphRun->mFont == aFont &&
            lastGlyphRun->mMatchType == aMatchType)
        {
            return NS_OK;
        }

        // If the offset has not changed, avoid leaving a zero-length run
        // by overwriting the last entry instead of appending...
        if (lastGlyphRun->mCharacterOffset == aUTF16Offset) {

            // ...except that if the run before the last entry had the same
            // font as the new one wants, merge with it instead of creating
            // adjacent runs with the same font
            if (numGlyphRuns > 1 &&
                mGlyphRuns[numGlyphRuns - 2].mFont == aFont &&
                mGlyphRuns[numGlyphRuns - 2].mMatchType == aMatchType)
            {
                mGlyphRuns.TruncateLength(numGlyphRuns - 1);
                return NS_OK;
            }

            lastGlyphRun->mFont = aFont;
            lastGlyphRun->mMatchType = aMatchType;
            return NS_OK;
        }
    }

    NS_ASSERTION(aForceNewRun || numGlyphRuns > 0 || aUTF16Offset == 0,
                 "First run doesn't cover the first character (and run not forced)?");

    GlyphRun *glyphRun = mGlyphRuns.AppendElement();
    if (!glyphRun)
        return NS_ERROR_OUT_OF_MEMORY;
    glyphRun->mFont = aFont;
    glyphRun->mCharacterOffset = aUTF16Offset;
    glyphRun->mMatchType = aMatchType;
    return NS_OK;
}

void
gfxTextRun::SortGlyphRuns()
{
    if (mGlyphRuns.Length() <= 1)
        return;

    nsTArray<GlyphRun> runs(mGlyphRuns);
    GlyphRunOffsetComparator comp;
    runs.Sort(comp);

    // Now copy back, coalescing adjacent glyph runs that have the same font
    mGlyphRuns.Clear();
    uint32_t i, count = runs.Length();
    for (i = 0; i < count; ++i) {
        // a GlyphRun with the same font as the previous GlyphRun can just
        // be skipped; the last GlyphRun will cover its character range.
        if (i == 0 || runs[i].mFont != runs[i - 1].mFont) {
            mGlyphRuns.AppendElement(runs[i]);
            // If two fonts have the same character offset, Sort() will have
            // randomized the order.
            NS_ASSERTION(i == 0 ||
                         runs[i].mCharacterOffset !=
                         runs[i - 1].mCharacterOffset,
                         "Two fonts for the same run, glyph indices may not match the font");
        }
    }
}

// Note that SanitizeGlyphRuns scans all glyph runs in the textrun;
// therefore we only call it once, at the end of textrun construction,
// NOT incrementally as each glyph run is added (bug 680402).
void
gfxTextRun::SanitizeGlyphRuns()
{
    if (mGlyphRuns.Length() <= 1)
        return;

    // If any glyph run starts with ligature-continuation characters, we need to advance it
    // to the first "real" character to avoid drawing partial ligature glyphs from wrong font
    // (seen with U+FEFF in reftest 474417-1, as Core Text eliminates the glyph, which makes
    // it appear as if a ligature has been formed)
    int32_t i, lastRunIndex = mGlyphRuns.Length() - 1;
    const CompressedGlyph *charGlyphs = mCharacterGlyphs;
    for (i = lastRunIndex; i >= 0; --i) {
        GlyphRun& run = mGlyphRuns[i];
        while (charGlyphs[run.mCharacterOffset].IsLigatureContinuation() &&
               run.mCharacterOffset < mCharacterCount) {
            run.mCharacterOffset++;
        }
        // if the run has become empty, eliminate it
        if ((i < lastRunIndex &&
             run.mCharacterOffset >= mGlyphRuns[i+1].mCharacterOffset) ||
            (i == lastRunIndex && run.mCharacterOffset == mCharacterCount)) {
            mGlyphRuns.RemoveElementAt(i);
            --lastRunIndex;
        }
    }
}

uint32_t
gfxTextRun::CountMissingGlyphs()
{
    uint32_t i;
    uint32_t count = 0;
    for (i = 0; i < mCharacterCount; ++i) {
        if (mCharacterGlyphs[i].IsMissing()) {
            ++count;
        }
    }
    return count;
}

gfxTextRun::DetailedGlyph *
gfxTextRun::AllocateDetailedGlyphs(uint32_t aIndex, uint32_t aCount)
{
    NS_ASSERTION(aIndex < mCharacterCount, "Index out of range");

    if (!mDetailedGlyphs) {
        mDetailedGlyphs = new DetailedGlyphStore();
    }

    DetailedGlyph *details = mDetailedGlyphs->Allocate(aIndex, aCount);
    if (!details) {
        mCharacterGlyphs[aIndex].SetMissing(0);
        return nullptr;
    }

    return details;
}

void
gfxTextRun::SetGlyphs(uint32_t aIndex, CompressedGlyph aGlyph,
                      const DetailedGlyph *aGlyphs)
{
    NS_ASSERTION(!aGlyph.IsSimpleGlyph(), "Simple glyphs not handled here");
    NS_ASSERTION(aIndex > 0 || aGlyph.IsLigatureGroupStart(),
                 "First character can't be a ligature continuation!");

    uint32_t glyphCount = aGlyph.GetGlyphCount();
    if (glyphCount > 0) {
        DetailedGlyph *details = AllocateDetailedGlyphs(aIndex, glyphCount);
        if (!details)
            return;
        memcpy(details, aGlyphs, sizeof(DetailedGlyph)*glyphCount);
    }
    mCharacterGlyphs[aIndex] = aGlyph;
}

void
gfxTextRun::SetMissingGlyph(uint32_t aIndex, uint32_t aChar)
{
    uint8_t category = GetGeneralCategory(aChar);
    if (category >= HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK &&
        category <= HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK)
    {
        mCharacterGlyphs[aIndex].SetComplex(false, true, 0);
    }

    DetailedGlyph *details = AllocateDetailedGlyphs(aIndex, 1);
    if (!details)
        return;

    details->mGlyphID = aChar;
    GlyphRun *glyphRun = &mGlyphRuns[FindFirstGlyphRunContaining(aIndex)];
    if (IsDefaultIgnorable(aChar)) {
        // Setting advance width to zero will prevent drawing the hexbox
        details->mAdvance = 0;
    } else {
        gfxFloat width = NS_MAX(glyphRun->mFont->GetMetrics().aveCharWidth,
                                gfxFontMissingGlyphs::GetDesiredMinWidth(aChar));
        details->mAdvance = uint32_t(width*GetAppUnitsPerDevUnit());
    }
    details->mXOffset = 0;
    details->mYOffset = 0;
    mCharacterGlyphs[aIndex].SetMissing(1);
}

void
gfxTextRun::CopyGlyphDataFrom(const gfxShapedWord *aShapedWord, uint32_t aOffset)
{
    uint32_t wordLen = aShapedWord->Length();
    NS_ASSERTION(aOffset + wordLen <= GetLength(),
                 "word overruns end of textrun!");

    const CompressedGlyph *wordGlyphs = aShapedWord->GetCharacterGlyphs();
    if (aShapedWord->HasDetailedGlyphs()) {
        for (uint32_t i = 0; i < wordLen; ++i, ++aOffset) {
            const CompressedGlyph& g = wordGlyphs[i];
            if (g.IsSimpleGlyph()) {
                SetSimpleGlyph(aOffset, g);
            } else {
                const DetailedGlyph *details =
                    g.GetGlyphCount() > 0 ?
                        aShapedWord->GetDetailedGlyphs(i) : nullptr;
                SetGlyphs(aOffset, g, details);
            }
        }
    } else {
        memcpy(GetCharacterGlyphs() + aOffset, wordGlyphs,
               wordLen * sizeof(CompressedGlyph));
    }
}

void
gfxTextRun::CopyGlyphDataFrom(gfxTextRun *aSource, uint32_t aStart,
                              uint32_t aLength, uint32_t aDest)
{
    NS_ASSERTION(aStart + aLength <= aSource->GetLength(),
                 "Source substring out of range");
    NS_ASSERTION(aDest + aLength <= GetLength(),
                 "Destination substring out of range");

    if (aSource->mSkipDrawing) {
        mSkipDrawing = true;
    }

    // Copy base glyph data, and DetailedGlyph data where present
    const CompressedGlyph *srcGlyphs = aSource->mCharacterGlyphs + aStart;
    CompressedGlyph *dstGlyphs = mCharacterGlyphs + aDest;
    for (uint32_t i = 0; i < aLength; ++i) {
        CompressedGlyph g = srcGlyphs[i];
        g.SetCanBreakBefore(!g.IsClusterStart() ?
            CompressedGlyph::FLAG_BREAK_TYPE_NONE :
            dstGlyphs[i].CanBreakBefore());
        if (!g.IsSimpleGlyph()) {
            uint32_t count = g.GetGlyphCount();
            if (count > 0) {
                DetailedGlyph *dst = AllocateDetailedGlyphs(i + aDest, count);
                if (dst) {
                    DetailedGlyph *src = aSource->GetDetailedGlyphs(i + aStart);
                    if (src) {
                        ::memcpy(dst, src, count * sizeof(DetailedGlyph));
                    } else {
                        g.SetMissing(0);
                    }
                } else {
                    g.SetMissing(0);
                }
            }
        }
        dstGlyphs[i] = g;
    }

    // Copy glyph runs
    GlyphRunIterator iter(aSource, aStart, aLength);
#ifdef DEBUG
    gfxFont *lastFont = nullptr;
#endif
    while (iter.NextRun()) {
        gfxFont *font = iter.GetGlyphRun()->mFont;
        NS_ASSERTION(font != lastFont, "Glyphruns not coalesced?");
#ifdef DEBUG
        lastFont = font;
        uint32_t end = iter.GetStringEnd();
#endif
        uint32_t start = iter.GetStringStart();

        // These used to be NS_ASSERTION()s, but WARNING is more appropriate.
        // Although it's unusual (and not desirable), it's possible for us to assign
        // different fonts to a base character and a following diacritic.
        // Example on OSX 10.5/10.6 with default fonts installed:
        //     data:text/html,<p style="font-family:helvetica, arial, sans-serif;">
        //                    &%23x043E;&%23x0486;&%23x20;&%23x043E;&%23x0486;
        // This means the rendering of the cluster will probably not be very good,
        // but it's the best we can do for now if the specified font only covered the
        // initial base character and not its applied marks.
        NS_WARN_IF_FALSE(aSource->IsClusterStart(start),
                         "Started font run in the middle of a cluster");
        NS_WARN_IF_FALSE(end == aSource->GetLength() || aSource->IsClusterStart(end),
                         "Ended font run in the middle of a cluster");

        nsresult rv = AddGlyphRun(font, iter.GetGlyphRun()->mMatchType,
                                  start - aStart + aDest, false);
        if (NS_FAILED(rv))
            return;
    }
}

void
gfxTextRun::SetSpaceGlyph(gfxFont *aFont, gfxContext *aContext,
                          uint32_t aCharIndex)
{
    if (SetSpaceGlyphIfSimple(aFont, aContext, aCharIndex, ' ')) {
        return;
    }

    aFont->InitWordCache();
    static const uint8_t space = ' ';
    gfxShapedWord *sw = aFont->GetShapedWord(aContext,
                                             &space, 1,
                                             HashMix(0, ' '), 
                                             MOZ_SCRIPT_LATIN,
                                             mAppUnitsPerDevUnit,
                                             gfxTextRunFactory::TEXT_IS_8BIT |
                                             gfxTextRunFactory::TEXT_IS_ASCII |
                                             gfxTextRunFactory::TEXT_IS_PERSISTENT);
    if (sw) {
        AddGlyphRun(aFont, gfxTextRange::kFontGroup, aCharIndex, false);
        CopyGlyphDataFrom(sw, aCharIndex);
    }
}

bool
gfxTextRun::SetSpaceGlyphIfSimple(gfxFont *aFont, gfxContext *aContext,
                                  uint32_t aCharIndex, PRUnichar aSpaceChar)
{
    uint32_t spaceGlyph = aFont->GetSpaceGlyph();
    if (!spaceGlyph || !CompressedGlyph::IsSimpleGlyphID(spaceGlyph)) {
        return false;
    }

    uint32_t spaceWidthAppUnits =
        NS_lroundf(aFont->GetMetrics().spaceWidth * mAppUnitsPerDevUnit);
    if (!CompressedGlyph::IsSimpleAdvance(spaceWidthAppUnits)) {
        return false;
    }

    AddGlyphRun(aFont, gfxTextRange::kFontGroup, aCharIndex, false);
    CompressedGlyph g;
    g.SetSimpleGlyph(spaceWidthAppUnits, spaceGlyph);
    if (aSpaceChar == ' ') {
        g.SetIsSpace();
    }
    SetSimpleGlyph(aCharIndex, g);
    return true;
}

void
gfxTextRun::FetchGlyphExtents(gfxContext *aRefContext)
{
    bool needsGlyphExtents = NeedsGlyphExtents(this);
    if (!needsGlyphExtents && !mDetailedGlyphs)
        return;

    uint32_t i, runCount = mGlyphRuns.Length();
    CompressedGlyph *charGlyphs = mCharacterGlyphs;
    for (i = 0; i < runCount; ++i) {
        const GlyphRun& run = mGlyphRuns[i];
        gfxFont *font = run.mFont;
        uint32_t start = run.mCharacterOffset;
        uint32_t end = i + 1 < runCount ?
            mGlyphRuns[i + 1].mCharacterOffset : GetLength();
        bool fontIsSetup = false;
        uint32_t j;
        gfxGlyphExtents *extents = font->GetOrCreateGlyphExtents(mAppUnitsPerDevUnit);
  
        for (j = start; j < end; ++j) {
            const gfxTextRun::CompressedGlyph *glyphData = &charGlyphs[j];
            if (glyphData->IsSimpleGlyph()) {
                // If we're in speed mode, don't set up glyph extents here; we'll
                // just return "optimistic" glyph bounds later
                if (needsGlyphExtents) {
                    uint32_t glyphIndex = glyphData->GetSimpleGlyph();
                    if (!extents->IsGlyphKnown(glyphIndex)) {
                        if (!fontIsSetup) {
                            if (!font->SetupCairoFont(aRefContext)) {
                                NS_WARNING("failed to set up font for glyph extents");
                                break;
                            }
                            fontIsSetup = true;
                        }
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
                        ++gGlyphExtentsSetupEagerSimple;
#endif
                        font->SetupGlyphExtents(aRefContext, glyphIndex, false, extents);
                    }
                }
            } else if (!glyphData->IsMissing()) {
                uint32_t glyphCount = glyphData->GetGlyphCount();
                if (glyphCount == 0) {
                    continue;
                }
                const gfxTextRun::DetailedGlyph *details = GetDetailedGlyphs(j);
                if (!details) {
                    continue;
                }
                for (uint32_t k = 0; k < glyphCount; ++k, ++details) {
                    uint32_t glyphIndex = details->mGlyphID;
                    if (!extents->IsGlyphKnownWithTightExtents(glyphIndex)) {
                        if (!fontIsSetup) {
                            if (!font->SetupCairoFont(aRefContext)) {
                                NS_WARNING("failed to set up font for glyph extents");
                                break;
                            }
                            fontIsSetup = true;
                        }
#ifdef DEBUG_TEXT_RUN_STORAGE_METRICS
                        ++gGlyphExtentsSetupEagerTight;
#endif
                        font->SetupGlyphExtents(aRefContext, glyphIndex, true, extents);
                    }
                }
            }
        }
    }
}


gfxTextRun::ClusterIterator::ClusterIterator(gfxTextRun *aTextRun)
    : mTextRun(aTextRun), mCurrentChar(uint32_t(-1))
{
}

void
gfxTextRun::ClusterIterator::Reset()
{
    mCurrentChar = uint32_t(-1);
}

bool
gfxTextRun::ClusterIterator::NextCluster()
{
    uint32_t len = mTextRun->GetLength();
    while (++mCurrentChar < len) {
        if (mTextRun->IsClusterStart(mCurrentChar)) {
            return true;
        }
    }

    mCurrentChar = uint32_t(-1);
    return false;
}

uint32_t
gfxTextRun::ClusterIterator::ClusterLength() const
{
    if (mCurrentChar == uint32_t(-1)) {
        return 0;
    }

    uint32_t i = mCurrentChar,
             len = mTextRun->GetLength();
    while (++i < len) {
        if (mTextRun->IsClusterStart(i)) {
            break;
        }
    }

    return i - mCurrentChar;
}

gfxFloat
gfxTextRun::ClusterIterator::ClusterAdvance(PropertyProvider *aProvider) const
{
    if (mCurrentChar == uint32_t(-1)) {
        return 0;
    }

    return mTextRun->GetAdvanceWidth(mCurrentChar, ClusterLength(), aProvider);
}

size_t
gfxTextRun::SizeOfExcludingThis(nsMallocSizeOfFun aMallocSizeOf)
{
    // The second arg is how much gfxTextRun::AllocateStorage would have
    // allocated.
    size_t total = mGlyphRuns.SizeOfExcludingThis(aMallocSizeOf);

    if (mDetailedGlyphs) {
        total += mDetailedGlyphs->SizeOfIncludingThis(aMallocSizeOf);
    }

    return total;
}

size_t
gfxTextRun::SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf)
{
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}


#ifdef DEBUG
void
gfxTextRun::Dump(FILE* aOutput) {
    if (!aOutput) {
        aOutput = stdout;
    }

    uint32_t i;
    fputc('[', aOutput);
    for (i = 0; i < mGlyphRuns.Length(); ++i) {
        if (i > 0) {
            fputc(',', aOutput);
        }
        gfxFont* font = mGlyphRuns[i].mFont;
        const gfxFontStyle* style = font->GetStyle();
        NS_ConvertUTF16toUTF8 fontName(font->GetName());
        nsAutoCString lang;
        style->language->ToUTF8String(lang);
        fprintf(aOutput, "%d: %s %f/%d/%d/%s", mGlyphRuns[i].mCharacterOffset,
                fontName.get(), style->size,
                style->weight, style->style, lang.get());
    }
    fputc(']', aOutput);
}
#endif
