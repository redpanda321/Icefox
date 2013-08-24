/* -*- Mode: ObjC; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * ***** BEGIN LICENSE BLOCK *****
 * Version: BSD
 *
 * Copyright (C) 2006-2009 Mozilla Corporation.  All rights reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@pobox.com>
 *   Masayuki Nakano <masayuki@d-toybox.com>
 *   John Daggett <jdaggett@mozilla.com>
 *   Jonathan Kew <jfkthame@gmail.com>
 *
 * Copyright (C) 2006 Apple Computer, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ***** END LICENSE BLOCK ***** */

#ifdef MOZ_LOGGING
#define FORCE_PR_LOG /* Allow logging in the release build */
#endif
#include "prlog.h"

#include <Carbon/Carbon.h>

#import <AppKit/AppKit.h>

#include "gfxPlatformMac.h"
#include "gfxMacPlatformFontList.h"
#include "gfxMacFont.h"
#include "gfxUserFontSet.h"
#include "harfbuzz/hb.h"
#include "harfbuzz/hb-ot.h"

#include "nsServiceManagerUtils.h"
#include "nsTArray.h"

#include "nsDirectoryServiceUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsISimpleEnumerator.h"
#include "nsCharTraits.h"

#include "mozilla/Telemetry.h"

#include <unistd.h>
#include <time.h>

using namespace mozilla;

class nsAutoreleasePool {
public:
    nsAutoreleasePool()
    {
        mLocalPool = [[NSAutoreleasePool alloc] init];
    }
    ~nsAutoreleasePool()
    {
        [mLocalPool release];
    }
private:
    NSAutoreleasePool *mLocalPool;
};

// font info loader constants
static const PRUint32 kDelayBeforeLoadingCmaps = 8 * 1000; // 8secs
static const PRUint32 kIntervalBetweenLoadingCmaps = 150; // 150ms
static const PRUint32 kNumFontsPerSlice = 10; // read in info 10 fonts at a time

// indexes into the NSArray objects that the Cocoa font manager returns
// as the available members of a family
#define INDEX_FONT_POSTSCRIPT_NAME 0
#define INDEX_FONT_FACE_NAME 1
#define INDEX_FONT_WEIGHT 2
#define INDEX_FONT_TRAITS 3

static const int kAppleMaxWeight = 14;
static const int kAppleExtraLightWeight = 3;
static const int kAppleUltraLightWeight = 2;

static const int gAppleWeightToCSSWeight[] = {
    0,
    1, // 1.
    1, // 2.  W1, ultralight
    2, // 3.  W2, extralight
    3, // 4.  W3, light
    4, // 5.  W4, semilight
    5, // 6.  W5, medium
    6, // 7.
    6, // 8.  W6, semibold
    7, // 9.  W7, bold
    8, // 10. W8, extrabold
    8, // 11.
    9, // 12. W9, ultrabold
    9, // 13
    9  // 14
};

// cache Cocoa's "shared font manager" for performance
static NSFontManager *sFontManager;

static void GetStringForNSString(const NSString *aSrc, nsAString& aDist)
{
    aDist.SetLength([aSrc length]);
    [aSrc getCharacters:aDist.BeginWriting()];
}

static NSString* GetNSStringForString(const nsAString& aSrc)
{
    return [NSString stringWithCharacters:aSrc.BeginReading()
                     length:aSrc.Length()];
}

#ifdef PR_LOGGING

#define LOG_FONTLIST(args) PR_LOG(gfxPlatform::GetLog(eGfxLog_fontlist), \
                               PR_LOG_DEBUG, args)
#define LOG_FONTLIST_ENABLED() PR_LOG_TEST( \
                                   gfxPlatform::GetLog(eGfxLog_fontlist), \
                                   PR_LOG_DEBUG)
#define LOG_CMAPDATA_ENABLED() PR_LOG_TEST( \
                                   gfxPlatform::GetLog(eGfxLog_cmapdata), \
                                   PR_LOG_DEBUG)

#endif // PR_LOGGING

/* MacOSFontEntry - abstract superclass for ATS and CG font entries */
#pragma mark-

MacOSFontEntry::MacOSFontEntry(const nsAString& aPostscriptName,
                               PRInt32 aWeight,
                               gfxFontFamily *aFamily,
                               bool aIsStandardFace)
    : gfxFontEntry(aPostscriptName, aFamily, aIsStandardFace),
      mFontRef(NULL),
      mFontRefInitialized(false),
      mRequiresAAT(false),
      mIsCFF(false),
      mIsCFFInitialized(false)
{
    mWeight = aWeight;
}

// Complex scripts will not render correctly unless appropriate AAT or OT
// layout tables are present.
// For OpenType, we also check that the GSUB table supports the relevant
// script tag, to avoid using things like Arial Unicode MS for Lao (it has
// the characters, but lacks OpenType support).

// TODO: consider whether we should move this to gfxFontEntry and do similar
// cmap-masking on other platforms to avoid using fonts that won't shape
// properly.

struct ScriptRange {
    PRUint32         rangeStart;
    PRUint32         rangeEnd;
    PRUint32         minVersion; // minimum OS X version where OT shaping is
                                 // supported for this range
    hb_tag_t         tags[3]; // one or two OpenType script tags to check,
                              // plus a NULL terminator
};

// shorthands for the minVersion field
#define _ANY   0 // Arabic/Syriac works on any version because we use harfbuzz
#define _10_7  MAC_OS_X_VERSION_10_7_HEX   // has Indic support in CoreText
#define _NONE  MAC_OS_X_MAJOR_VERSION_MASK // currently not supported by
                                           // any known version

static const ScriptRange sComplexScripts[] = {
    // Actually, now that harfbuzz supports presentation-forms shaping for
    // Arabic, we can render it without layout tables. So maybe we don't
    // want to mask the basic Arabic block here?
    // This affects the arabic-fallback-*.html reftests, which rely on
    // loading a font that *doesn't* have any GSUB table.
    { 0x0600, 0x06FF, _ANY,  { TRUETYPE_TAG('a','r','a','b'), 0, 0 } },
    { 0x0700, 0x074F, _ANY,  { TRUETYPE_TAG('s','y','r','c'), 0, 0 } },
    { 0x0750, 0x077F, _ANY,  { TRUETYPE_TAG('a','r','a','b'), 0, 0 } },
    { 0x08A0, 0x08FF, _ANY,  { TRUETYPE_TAG('a','r','a','b'), 0, 0 } },
    { 0x0900, 0x097F, _10_7, { TRUETYPE_TAG('d','e','v','2'),
                               TRUETYPE_TAG('d','e','v','a'), 0 } },
    { 0x0980, 0x09FF, _10_7, { TRUETYPE_TAG('b','n','g','2'),
                               TRUETYPE_TAG('b','e','n','g'), 0 } },
    { 0x0A00, 0x0A7F, _10_7, { TRUETYPE_TAG('g','u','r','2'),
                               TRUETYPE_TAG('g','u','r','u'), 0 } },
    { 0x0A80, 0x0AFF, _10_7, { TRUETYPE_TAG('g','j','r','2'),
                               TRUETYPE_TAG('g','u','j','r'), 0 } },
    { 0x0B00, 0x0B7F, _10_7, { TRUETYPE_TAG('o','r','y','2'),
                               TRUETYPE_TAG('o','r','y','a'), 0 } },
    { 0x0B80, 0x0BFF, _10_7, { TRUETYPE_TAG('t','m','l','2'),
                               TRUETYPE_TAG('t','a','m','l'), 0 } },
    { 0x0C00, 0x0C7F, _10_7, { TRUETYPE_TAG('t','e','l','2'),
                               TRUETYPE_TAG('t','e','l','u'), 0 } },
    { 0x0C80, 0x0CFF, _10_7, { TRUETYPE_TAG('k','n','d','2'),
                               TRUETYPE_TAG('k','n','d','a'), 0 } },
    { 0x0D00, 0x0D7F, _10_7, { TRUETYPE_TAG('m','l','m','2'),
                               TRUETYPE_TAG('m','l','y','m'), 0 } },
    { 0x0D80, 0x0DFF, _NONE, { TRUETYPE_TAG('s','i','n','h'), 0, 0 } },
    { 0x0E80, 0x0EFF, _10_7, { TRUETYPE_TAG('l','a','o',' '), 0, 0 } },
    { 0x0F00, 0x0FFF, _10_7, { TRUETYPE_TAG('t','i','b','t'), 0, 0 } },
    // Thai seems to be "renderable" without AAT morphing tables
    // xxx - Khmer?
};

#undef _ANY
#undef _10_7
#undef _NONE

static void
DestroyBlobFunc(void* aUserData)
{
    FallibleTArray<PRUint8>* data = static_cast<FallibleTArray<PRUint8>*>(aUserData);
    delete data;
}

// This is only used via MacOSFontEntry::ReadCMAP when checking for layout
// support; it does not respect the mIgnore* flags on font entries, as those
// are not relevant here at present.
static hb_blob_t *
GetTableForHarfBuzz(hb_face_t *aFace, hb_tag_t aTag, void *aUserData)
{
    gfxFontEntry *fe = static_cast<gfxFontEntry*>(aUserData);
    FallibleTArray<PRUint8>* table = new FallibleTArray<PRUint8>;
    nsresult rv = fe->GetFontTable(aTag, *table);
    if (NS_SUCCEEDED(rv)) {
        return hb_blob_create((const char*)table->Elements(), table->Length(),
                              HB_MEMORY_MODE_READONLY, table, DestroyBlobFunc);
    }
    delete table;
    return hb_blob_get_empty();
}

static bool
SupportsScriptInGSUB(gfxFontEntry* aFontEntry, const hb_tag_t* aScriptTags)
{
    hb_face_t *face = hb_face_create_for_tables(GetTableForHarfBuzz,
                                                aFontEntry, nsnull);
    unsigned int index;
    hb_tag_t     chosenScript;
    bool found =
        hb_ot_layout_table_choose_script(face, TRUETYPE_TAG('G','S','U','B'),
                                         aScriptTags, &index, &chosenScript);
    hb_face_destroy(face);
    return found && chosenScript != TRUETYPE_TAG('D','F','L','T');
}

nsresult
MacOSFontEntry::ReadCMAP()
{
    // attempt this once, if errors occur leave a blank cmap
    if (mCharacterMap) {
        return NS_OK;
    }

    nsRefPtr<gfxCharacterMap> charmap = new gfxCharacterMap();

    PRUint32 kCMAP = TRUETYPE_TAG('c','m','a','p');
    nsresult rv;

    AutoFallibleTArray<PRUint8,16384> cmap;
    rv = GetFontTable(kCMAP, cmap);

    bool unicodeFont = false, symbolFont = false; // currently ignored

    if (NS_SUCCEEDED(rv)) {
        rv = gfxFontUtils::ReadCMAP(cmap.Elements(), cmap.Length(),
                                    *charmap, mUVSOffset,
                                    unicodeFont, symbolFont);
    }
  
    if (NS_SUCCEEDED(rv)) {
        // for layout support, check for the presence of mort/morx and/or
        // opentype layout tables
        bool hasAATLayout = HasFontTable(TRUETYPE_TAG('m','o','r','x')) ||
                            HasFontTable(TRUETYPE_TAG('m','o','r','t'));
        bool hasGSUB = HasFontTable(TRUETYPE_TAG('G','S','U','B'));
        bool hasGPOS = HasFontTable(TRUETYPE_TAG('G','P','O','S'));

        PRUint32 osxVersion = gfxPlatformMac::GetPlatform()->OSXVersion();

        if (hasAATLayout && !(hasGSUB || hasGPOS)) {
            mRequiresAAT = true; // prefer CoreText if font has no OTL tables
        }

        PRUint32 numScripts = ArrayLength(sComplexScripts);

        for (PRUint32 s = 0; s < numScripts; s++) {
            // check to see if the cmap includes complex script codepoints
            const ScriptRange& sr = sComplexScripts[s];
            if (charmap->TestRange(sr.rangeStart, sr.rangeEnd)) {
                if (hasAATLayout) {
                    // prefer CoreText for Apple's complex-script fonts,
                    // even if they also have some OpenType tables
                    // (e.g. Geeza Pro Bold on 10.6; see bug 614903)
                    mRequiresAAT = true;
                    // and don't mask off complex-script ranges, we assume
                    // the AAT tables will provide the necessary shaping
                    continue;
                }

                // Check whether the OS version is sufficient to support
                // OpenType shaping for this range (provided GSUB available)
                if (osxVersion >= sr.minVersion) {
                    // We check for GSUB here, as GPOS alone would not be ok.
                    if (hasGSUB && SupportsScriptInGSUB(this, sr.tags)) {
                        continue;
                    }
                }

                charmap->ClearRange(sr.rangeStart, sr.rangeEnd);
            }
        }
    }

    mHasCmapTable = NS_SUCCEEDED(rv);
    if (mHasCmapTable) {
        gfxPlatformFontList *pfl = gfxPlatformFontList::PlatformFontList();
        mCharacterMap = pfl->FindCharMap(charmap);
    } else {
        // if error occurred, initialize to null cmap
        mCharacterMap = new gfxCharacterMap();
    }

#ifdef PR_LOGGING
    LOG_FONTLIST(("(fontlist-cmap) name: %s, size: %d hash: %8.8x%s\n",
                  NS_ConvertUTF16toUTF8(mName).get(),
                  charmap->SizeOfIncludingThis(moz_malloc_size_of),
                  charmap->mHash, mCharacterMap == charmap ? " new" : ""));
    if (LOG_CMAPDATA_ENABLED()) {
        char prefix[256];
        sprintf(prefix, "(cmapdata) name: %.220s",
                NS_ConvertUTF16toUTF8(mName).get());
        charmap->Dump(prefix, eGfxLog_cmapdata);
    }
#endif

    return rv;
}

gfxFont*
MacOSFontEntry::CreateFontInstance(const gfxFontStyle *aFontStyle, bool aNeedsBold)
{
    return new gfxMacFont(this, aFontStyle, aNeedsBold);
}

bool
MacOSFontEntry::IsCFF()
{
    if (!mIsCFFInitialized) {
        mIsCFFInitialized = true;
        mIsCFF = HasFontTable(TRUETYPE_TAG('C','F','F',' '));
    }

    return mIsCFF;
}

/* ATSFontEntry - used on Mac OS X 10.5.x */
#pragma mark-

ATSFontEntry::ATSFontEntry(const nsAString& aPostscriptName,
                           PRInt32 aWeight,
                           gfxFontFamily *aFamily,
                           bool aIsStandardFace)
    : MacOSFontEntry(aPostscriptName, aWeight, aFamily, aIsStandardFace),
      mATSFontRef(kInvalidFont),
      mATSFontRefInitialized(false)
{
}

ATSFontEntry::ATSFontEntry(const nsAString& aPostscriptName,
                           ATSFontRef aFontRef,
                           PRUint16 aWeight, PRUint16 aStretch,
                           PRUint32 aItalicStyle,
                           gfxUserFontData *aUserFontData,
                           bool aIsLocal)
    : MacOSFontEntry(aPostscriptName, aWeight, nsnull, false)
{
    mATSFontRef = aFontRef;
    mATSFontRefInitialized = true;

    mWeight = aWeight;
    mStretch = aStretch;
    mFixedPitch = false; // xxx - do we need this for downloaded fonts?
    mItalic = (aItalicStyle & (NS_FONT_STYLE_ITALIC | NS_FONT_STYLE_OBLIQUE)) != 0;
    mUserFontData = aUserFontData;
    mIsUserFont = (aUserFontData != nsnull) || aIsLocal;
    mIsLocalUserFont = aIsLocal;
}

ATSFontRef
ATSFontEntry::GetATSFontRef()
{
    if (!mATSFontRefInitialized) {
        mATSFontRefInitialized = true;
        NSString *psname = GetNSStringForString(mName);
        mATSFontRef = ::ATSFontFindFromPostScriptName(CFStringRef(psname),
                                                      kATSOptionFlagsDefault);
    }
    return mATSFontRef;
}

CGFontRef
ATSFontEntry::GetFontRef()
{
    if (mFontRefInitialized) {
        return mFontRef;
    }

    // GetATSFontRef will initialize mATSFontRef
    if (GetATSFontRef() == kInvalidFont) {
        return nsnull;
    }
    
    mFontRef = ::CGFontCreateWithPlatformFont(&mATSFontRef);
    mFontRefInitialized = true;

    return mFontRef;
}

nsresult
ATSFontEntry::GetFontTable(PRUint32 aTableTag, FallibleTArray<PRUint8>& aBuffer)
{
    nsAutoreleasePool localPool;

    ATSFontRef fontRef = GetATSFontRef();
    if (fontRef == kInvalidFont) {
        return NS_ERROR_FAILURE;
    }

    ByteCount dataLength;
    OSStatus status = ::ATSFontGetTable(fontRef, aTableTag, 0, 0, 0, &dataLength);
    if (status != noErr) {
        return NS_ERROR_FAILURE;
    }

    if (!aBuffer.SetLength(dataLength)) {
        return NS_ERROR_OUT_OF_MEMORY;
    }
    PRUint8 *dataPtr = aBuffer.Elements();

    status = ::ATSFontGetTable(fontRef, aTableTag, 0, dataLength, dataPtr, &dataLength);
    NS_ENSURE_TRUE(status == noErr, NS_ERROR_FAILURE);

    return NS_OK;
}
 
bool
ATSFontEntry::HasFontTable(PRUint32 aTableTag)
{
    ATSFontRef fontRef = GetATSFontRef();
    ByteCount size;
    return fontRef != kInvalidFont &&
        (::ATSFontGetTable(fontRef, aTableTag, 0, 0, 0, &size) == noErr);
}

void
ATSFontEntry::SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf,
                                  FontListSizes*    aSizes) const
{
    aSizes->mFontListSize += aMallocSizeOf(this);
    SizeOfExcludingThis(aMallocSizeOf, aSizes);
}

/* CGFontEntry - used on Mac OS X 10.6+ */
#pragma mark-

CGFontEntry::CGFontEntry(const nsAString& aPostscriptName,
                         PRInt32 aWeight,
                         gfxFontFamily *aFamily,
                         bool aIsStandardFace)
    : MacOSFontEntry(aPostscriptName, aWeight, aFamily, aIsStandardFace)
{
}

CGFontEntry::CGFontEntry(const nsAString& aPostscriptName,
                         CGFontRef aFontRef,
                         PRUint16 aWeight, PRUint16 aStretch,
                         PRUint32 aItalicStyle,
                         bool aIsUserFont, bool aIsLocal)
    : MacOSFontEntry(aPostscriptName, aWeight, nsnull, false)
{
    mFontRef = aFontRef;
    mFontRefInitialized = true;
    ::CFRetain(mFontRef);

    mWeight = aWeight;
    mStretch = aStretch;
    mFixedPitch = false; // xxx - do we need this for downloaded fonts?
    mItalic = (aItalicStyle & (NS_FONT_STYLE_ITALIC | NS_FONT_STYLE_OBLIQUE)) != 0;
    mIsUserFont = aIsUserFont;
    mIsLocalUserFont = aIsLocal;
}

CGFontRef
CGFontEntry::GetFontRef()
{
    if (!mFontRefInitialized) {
        mFontRefInitialized = true;
        NSString *psname = GetNSStringForString(mName);
        mFontRef = ::CGFontCreateWithFontName(CFStringRef(psname));
    }
    return mFontRef;
}

nsresult
CGFontEntry::GetFontTable(PRUint32 aTableTag, FallibleTArray<PRUint8>& aBuffer)
{
    nsAutoreleasePool localPool;

    CGFontRef fontRef = GetFontRef();
    if (!fontRef) {
        return NS_ERROR_FAILURE;
    }

    CFDataRef tableData = ::CGFontCopyTableForTag(fontRef, aTableTag);
    if (!tableData) {
        return NS_ERROR_FAILURE;
    }

    nsresult rval = NS_OK;
    CFIndex dataLength = ::CFDataGetLength(tableData);
    if (aBuffer.AppendElements(dataLength)) {
        ::CFDataGetBytes(tableData, ::CFRangeMake(0, dataLength),
                         aBuffer.Elements());
    } else {
        rval = NS_ERROR_OUT_OF_MEMORY;
    }
    ::CFRelease(tableData);

    return rval;
}

bool
CGFontEntry::HasFontTable(PRUint32 aTableTag)
{
    nsAutoreleasePool localPool;

    CGFontRef fontRef = GetFontRef();
    if (!fontRef) {
        return false;
    }

    CFDataRef tableData = ::CGFontCopyTableForTag(fontRef, aTableTag);
    if (!tableData) {
        return false;
    }

    ::CFRelease(tableData);
    return true;
}

void
CGFontEntry::SizeOfIncludingThis(nsMallocSizeOfFun aMallocSizeOf,
                                 FontListSizes*    aSizes) const
{
    aSizes->mFontListSize += aMallocSizeOf(this);
    SizeOfExcludingThis(aMallocSizeOf, aSizes);
}

/* gfxMacFontFamily */
#pragma mark-

class gfxMacFontFamily : public gfxFontFamily
{
public:
    gfxMacFontFamily(nsAString& aName) :
        gfxFontFamily(aName)
    {}

    virtual ~gfxMacFontFamily() {}

    virtual void LocalizedName(nsAString& aLocalizedName);

    virtual void FindStyleVariations();
};

void
gfxMacFontFamily::LocalizedName(nsAString& aLocalizedName)
{
    nsAutoreleasePool localPool;

    if (!HasOtherFamilyNames()) {
        aLocalizedName = mName;
        return;
    }

    NSString *family = GetNSStringForString(mName);
    NSString *localized = [sFontManager
                           localizedNameForFamily:family
                                             face:nil];

    if (localized) {
        GetStringForNSString(localized, aLocalizedName);
        return;
    }

    // failed to get localized name, just use the canonical one
    aLocalizedName = mName;
}

void
gfxMacFontFamily::FindStyleVariations()
{
    if (mHasStyles)
        return;

    nsAutoreleasePool localPool;

    NSString *family = GetNSStringForString(mName);

    // create a font entry for each face
    NSArray *fontfaces = [sFontManager
                          availableMembersOfFontFamily:family];  // returns an array of [psname, style name, weight, traits] elements, goofy api
    int faceCount = [fontfaces count];
    int faceIndex;

    // Bug 420981 - under 10.5, UltraLight and Light have the same weight value
    bool needToCheckLightFaces =
        (gfxPlatformMac::GetPlatform()->OSXVersion() >= MAC_OS_X_VERSION_10_5_HEX);

    for (faceIndex = 0; faceIndex < faceCount; faceIndex++) {
        NSArray *face = [fontfaces objectAtIndex:faceIndex];
        NSString *psname = [face objectAtIndex:INDEX_FONT_POSTSCRIPT_NAME];
        PRInt32 appKitWeight = [[face objectAtIndex:INDEX_FONT_WEIGHT] unsignedIntValue];
        PRUint32 macTraits = [[face objectAtIndex:INDEX_FONT_TRAITS] unsignedIntValue];
        NSString *facename = [face objectAtIndex:INDEX_FONT_FACE_NAME];
        bool isStandardFace = false;

        if (needToCheckLightFaces && appKitWeight == kAppleExtraLightWeight) {
            // if the facename contains UltraLight, set the weight to the ultralight weight value
            NSRange range = [facename rangeOfString:@"ultralight" options:NSCaseInsensitiveSearch];
            if (range.location != NSNotFound) {
                appKitWeight = kAppleUltraLightWeight;
            }
        }

        PRInt32 cssWeight = gfxMacPlatformFontList::AppleWeightToCSSWeight(appKitWeight) * 100;

        // make a nsString
        nsAutoString postscriptFontName;
        GetStringForNSString(psname, postscriptFontName);

        if ([facename isEqualToString:@"Regular"] ||
            [facename isEqualToString:@"Bold"] ||
            [facename isEqualToString:@"Italic"] ||
            [facename isEqualToString:@"Oblique"] ||
            [facename isEqualToString:@"Bold Italic"] ||
            [facename isEqualToString:@"Bold Oblique"])
        {
            isStandardFace = true;
        }

        // create a font entry
        MacOSFontEntry *fontEntry;
        if (gfxMacPlatformFontList::UseATSFontEntry()) {
            fontEntry = new ATSFontEntry(postscriptFontName,
                                         cssWeight, this, isStandardFace);
        } else {
            fontEntry = new CGFontEntry(postscriptFontName,
                                        cssWeight, this, isStandardFace);
        }
        if (!fontEntry) {
            break;
        }

        // set additional properties based on the traits reported by Cocoa
        if (macTraits & (NSCondensedFontMask | NSNarrowFontMask | NSCompressedFontMask)) {
            fontEntry->mStretch = NS_FONT_STRETCH_CONDENSED;
        } else if (macTraits & NSExpandedFontMask) {
            fontEntry->mStretch = NS_FONT_STRETCH_EXPANDED;
        }
        // Cocoa fails to set the Italic traits bit for HelveticaLightItalic,
        // at least (see bug 611855), so check for style name endings as well
        if ((macTraits & NSItalicFontMask) ||
            [facename hasSuffix:@"Italic"] ||
            [facename hasSuffix:@"Oblique"])
        {
            fontEntry->mItalic = true;
        }
        if (macTraits & NSFixedPitchFontMask) {
            fontEntry->mFixedPitch = true;
        }

#ifdef PR_LOGGING
        if (LOG_FONTLIST_ENABLED()) {
            LOG_FONTLIST(("(fontlist) added (%s) to family (%s)"
                 " with style: %s weight: %d stretch: %d"
                 " (apple-weight: %d macTraits: %8.8x)",
                 NS_ConvertUTF16toUTF8(fontEntry->Name()).get(), 
                 NS_ConvertUTF16toUTF8(Name()).get(), 
                 fontEntry->IsItalic() ? "italic" : "normal",
                 cssWeight, fontEntry->Stretch(),
                 appKitWeight, macTraits));
        }
#endif

        // insert into font entry array of family
        AddFontEntry(fontEntry);
    }

    SortAvailableFonts();
    SetHasStyles(true);

    if (mIsBadUnderlineFamily) {
        SetBadUnderlineFonts();
    }
}


/* gfxSingleFaceMacFontFamily */
#pragma mark-

class gfxSingleFaceMacFontFamily : public gfxFontFamily
{
public:
    gfxSingleFaceMacFontFamily(nsAString& aName) :
        gfxFontFamily(aName)
    {}

    virtual ~gfxSingleFaceMacFontFamily() {}

    virtual void LocalizedName(nsAString& aLocalizedName);

    virtual void ReadOtherFamilyNames(gfxPlatformFontList *aPlatformFontList);
};

void
gfxSingleFaceMacFontFamily::LocalizedName(nsAString& aLocalizedName)
{
    nsAutoreleasePool localPool;

    if (!HasOtherFamilyNames()) {
        aLocalizedName = mName;
        return;
    }

    gfxFontEntry *fe = mAvailableFonts[0];
    NSFont *font = [NSFont fontWithName:GetNSStringForString(fe->Name())
                                   size:0.0];
    if (font) {
        NSString *localized = [font displayName];
        if (localized) {
            GetStringForNSString(localized, aLocalizedName);
            return;
        }
    }

    // failed to get localized name, just use the canonical one
    aLocalizedName = mName;
}

void
gfxSingleFaceMacFontFamily::ReadOtherFamilyNames(gfxPlatformFontList *aPlatformFontList)
{
    if (mOtherFamilyNamesInitialized)
        return;

    gfxFontEntry *fe = mAvailableFonts[0];
    if (!fe)
        return;

    const PRUint32 kNAME = TRUETYPE_TAG('n','a','m','e');
    AutoFallibleTArray<PRUint8,8192> buffer;

    if (fe->GetFontTable(kNAME, buffer) != NS_OK)
        return;

    mHasOtherFamilyNames = ReadOtherFamilyNamesForFace(aPlatformFontList,
                                                       buffer,
                                                       true);
    mOtherFamilyNamesInitialized = true;
}


/* gfxMacPlatformFontList */
#pragma mark-

gfxMacPlatformFontList::gfxMacPlatformFontList() :
    gfxPlatformFontList(false), mATSGeneration(PRUint32(kATSGenerationInitial)),
    mDefaultFont(nsnull)
{
    ::ATSFontNotificationSubscribe(ATSNotification,
                                   kATSFontNotifyOptionDefault,
                                   (void*)this, nsnull);

    // this should always be available (though we won't actually fail if it's missing,
    // we'll just end up doing a search and then caching the new result instead)
    mReplacementCharFallbackFamily = NS_LITERAL_STRING("Lucida Grande");

    // cache this in a static variable so that MacOSFontFamily objects
    // don't have to repeatedly look it up
    sFontManager = [NSFontManager sharedFontManager];
}

gfxMacPlatformFontList::~gfxMacPlatformFontList()
{
    if (mDefaultFont) {
        ::CFRelease(mDefaultFont);
    }
}

nsresult
gfxMacPlatformFontList::InitFontList()
{
    nsAutoreleasePool localPool;

    ATSGeneration currentGeneration = ::ATSGetGeneration();

    // need to ignore notifications after adding each font
    if (mATSGeneration == currentGeneration)
        return NS_OK;

    Telemetry::AutoTimer<Telemetry::MAC_INITFONTLIST_TOTAL> timer;

    mATSGeneration = currentGeneration;
#ifdef PR_LOGGING
    LOG_FONTLIST(("(fontlist) updating to generation: %d", mATSGeneration));
#endif

    // reset font lists
    gfxPlatformFontList::InitFontList();
    
    // iterate over available families
    NSEnumerator *families = [[sFontManager availableFontFamilies]
                              objectEnumerator];  // returns "canonical", non-localized family name

    nsAutoString availableFamilyName;

    NSString *availableFamily = nil;
    while ((availableFamily = [families nextObject])) {

        // make a nsString
        GetStringForNSString(availableFamily, availableFamilyName);

        // create a family entry
        gfxFontFamily *familyEntry = new gfxMacFontFamily(availableFamilyName);
        if (!familyEntry) break;

        // add the family entry to the hash table
        ToLowerCase(availableFamilyName);
        mFontFamilies.Put(availableFamilyName, familyEntry);

        // check the bad underline blacklist
        if (mBadUnderlineFamilyNames.Contains(availableFamilyName))
            familyEntry->SetBadUnderlineFamily();
    }

    InitSingleFaceList();

    // to avoid full search of font name tables, seed the other names table with localized names from
    // some of the prefs fonts which are accessed via their localized names.  changes in the pref fonts will only cause
    // a font lookup miss earlier. this is a simple optimization, it's not required for correctness
    PreloadNamesList();

    // start the delayed cmap loader
    StartLoader(kDelayBeforeLoadingCmaps, kIntervalBetweenLoadingCmaps);

	return NS_OK;
}

void
gfxMacPlatformFontList::InitSingleFaceList()
{
    nsAutoTArray<nsString, 10> singleFaceFonts;
    gfxFontUtils::GetPrefsFontList("font.single-face-list", singleFaceFonts);

    PRUint32 numFonts = singleFaceFonts.Length();
    for (PRUint32 i = 0; i < numFonts; i++) {
#ifdef PR_LOGGING
        LOG_FONTLIST(("(fontlist-singleface) face name: %s\n",
                      NS_ConvertUTF16toUTF8(singleFaceFonts[i]).get()));
#endif
        gfxFontEntry *fontEntry = LookupLocalFont(nsnull, singleFaceFonts[i]);
        if (fontEntry) {
            nsAutoString familyName, key;
            familyName = singleFaceFonts[i];
            GenerateFontListKey(familyName, key);
#ifdef PR_LOGGING
            LOG_FONTLIST(("(fontlist-singleface) family name: %s, key: %s\n",
                          NS_ConvertUTF16toUTF8(familyName).get(),
                          NS_ConvertUTF16toUTF8(key).get()));
#endif

            // add only if doesn't exist already
            if (!mFontFamilies.GetWeak(key)) {
                gfxFontFamily *familyEntry =
                    new gfxSingleFaceMacFontFamily(familyName);
                familyEntry->AddFontEntry(fontEntry);
                familyEntry->SetHasStyles(true);
                mFontFamilies.Put(key, familyEntry);
                fontEntry->mFamily = familyEntry;
#ifdef PR_LOGGING
                LOG_FONTLIST(("(fontlist-singleface) added new family\n",
                              NS_ConvertUTF16toUTF8(familyName).get(),
                              NS_ConvertUTF16toUTF8(key).get()));
#endif
            }
        }
    }
}

bool
gfxMacPlatformFontList::GetStandardFamilyName(const nsAString& aFontName, nsAString& aFamilyName)
{
    gfxFontFamily *family = FindFamily(aFontName);
    if (family) {
        family->LocalizedName(aFamilyName);
        return true;
    }

    // Gecko 1.8 used Quickdraw font api's which produce a slightly different set of "family"
    // names.  Try to resolve based on these names, in case this is stored in an old profile
    // 1.8: "Futura", "Futura Condensed" ==> 1.9: "Futura"

    // convert the name to a Pascal-style Str255 to try as Quickdraw name
    Str255 qdname;
    NS_ConvertUTF16toUTF8 utf8name(aFontName);
    qdname[0] = NS_MAX<size_t>(255, strlen(utf8name.get()));
    memcpy(&qdname[1], utf8name.get(), qdname[0]);

    // look up the Quickdraw name
    ATSFontFamilyRef atsFamily = ::ATSFontFamilyFindFromQuickDrawName(qdname);
    if (atsFamily == (ATSFontFamilyRef)kInvalidFontFamily) {
        return false;
    }

    // if we found a family, get its ATS name
    CFStringRef cfName;
    OSStatus status = ::ATSFontFamilyGetName(atsFamily, kATSOptionFlagsDefault, &cfName);
    if (status != noErr) {
        return false;
    }

    // then use this to locate the family entry and retrieve its localized name
    nsAutoString familyName;
    GetStringForNSString((const NSString*)cfName, familyName);
    ::CFRelease(cfName);

    family = FindFamily(familyName);
    if (family) {
        family->LocalizedName(aFamilyName);
        return true;
    }

    return false;
}

void
gfxMacPlatformFontList::ATSNotification(ATSFontNotificationInfoRef aInfo,
                                    void* aUserArg)
{
    // xxx - should be carefully pruning the list of fonts, not rebuilding it from scratch
    gfxMacPlatformFontList *qfc = (gfxMacPlatformFontList*)aUserArg;
    qfc->UpdateFontList();
}

gfxFontEntry*
gfxMacPlatformFontList::GlobalFontFallback(const PRUint32 aCh,
                                           PRInt32 aRunScript,
                                           const gfxFontStyle* aMatchStyle,
                                           PRUint32& aCmapCount)
{
    bool useCmaps = gfxPlatform::GetPlatform()->UseCmapsDuringSystemFallback();

    if (useCmaps) {
        return gfxPlatformFontList::GlobalFontFallback(aCh,
                                                       aRunScript,
                                                       aMatchStyle,
                                                       aCmapCount);
    }

    CFStringRef str;
    UniChar ch[2];
    CFIndex len = 1;

    if (IS_IN_BMP(aCh)) {
        ch[0] = aCh;
        str = ::CFStringCreateWithCharactersNoCopy(kCFAllocatorDefault, ch, 1,
                                                   kCFAllocatorNull);
    } else {
        ch[0] = H_SURROGATE(aCh);
        ch[1] = L_SURROGATE(aCh);
        str = ::CFStringCreateWithCharactersNoCopy(kCFAllocatorDefault, ch, 2,
                                                   kCFAllocatorNull);
        if (!str) {
            return nsnull;
        }
        len = 2;
    }

    // use CoreText to find the fallback family

    gfxFontEntry *fontEntry = nsnull;
    CTFontRef fallback;
    bool cantUseFallbackFont = false;

    if (!mDefaultFont) {
        mDefaultFont = ::CTFontCreateWithName(CFSTR("Lucida Grande"), 12.f,
                                              NULL);
    }

    fallback = ::CTFontCreateForString(mDefaultFont, str,
                                       ::CFRangeMake(0, len));

    if (fallback) {
        CFStringRef familyName = ::CTFontCopyFamilyName(fallback);
        ::CFRelease(fallback);

        if (familyName &&
            ::CFStringCompare(familyName, CFSTR("LastResort"),
                              kCFCompareCaseInsensitive) != kCFCompareEqualTo)
        {
            nsAutoTArray<UniChar, 1024> buffer;
            CFIndex len = ::CFStringGetLength(familyName);
            buffer.SetLength(len+1);
            ::CFStringGetCharacters(familyName, ::CFRangeMake(0, len),
                                    buffer.Elements());
            buffer[len] = 0;
            nsDependentString family(buffer.Elements(), len);

            bool needsBold;  // ignored in the system fallback case

            fontEntry = FindFontForFamily(family, aMatchStyle, needsBold);
            if (fontEntry && !fontEntry->TestCharacterMap(aCh)) {
                fontEntry = nsnull;
                cantUseFallbackFont = true;
            }
        }

        if (familyName) {
            ::CFRelease(familyName);
        }
    }

    if (cantUseFallbackFont) {
        Telemetry::Accumulate(Telemetry::BAD_FALLBACK_FONT, cantUseFallbackFont);
    }

    ::CFRelease(str);

    return fontEntry;
}

gfxFontEntry*
gfxMacPlatformFontList::GetDefaultFont(const gfxFontStyle* aStyle, bool& aNeedsBold)
{
    nsAutoreleasePool localPool;

    NSString *defaultFamily = [[NSFont userFontOfSize:aStyle->size] familyName];
    nsAutoString familyName;

    GetStringForNSString(defaultFamily, familyName);
    return FindFontForFamily(familyName, aStyle, aNeedsBold);
}

PRInt32
gfxMacPlatformFontList::AppleWeightToCSSWeight(PRInt32 aAppleWeight)
{
    if (aAppleWeight < 1)
        aAppleWeight = 1;
    else if (aAppleWeight > kAppleMaxWeight)
        aAppleWeight = kAppleMaxWeight;
    return gAppleWeightToCSSWeight[aAppleWeight];
}

gfxFontEntry*
gfxMacPlatformFontList::LookupLocalFont(const gfxProxyFontEntry *aProxyEntry,
                                        const nsAString& aFontName)
{
    nsAutoreleasePool localPool;

    NSString *faceName = GetNSStringForString(aFontName);
    MacOSFontEntry *newFontEntry;

    if (UseATSFontEntry()) {
        // first lookup a single face based on postscript name
        ATSFontRef fontRef = ::ATSFontFindFromPostScriptName(CFStringRef(faceName),
                                                             kATSOptionFlagsDefault);

        // if not found, lookup using full font name
        if (fontRef == kInvalidFont) {
            fontRef = ::ATSFontFindFromName(CFStringRef(faceName),
                                            kATSOptionFlagsDefault);
            if (fontRef == kInvalidFont) {
                return nsnull;
            }
        }

        if (aProxyEntry) {
            PRUint16 w = aProxyEntry->mWeight;
            NS_ASSERTION(w >= 100 && w <= 900, "bogus font weight value!");
 
            newFontEntry =
                new ATSFontEntry(aFontName, fontRef,
                                 w, aProxyEntry->mStretch,
                                 aProxyEntry->mItalic ?
                                     NS_FONT_STYLE_ITALIC : NS_FONT_STYLE_NORMAL,
                                 nsnull, true);
        } else {
            newFontEntry =
                new ATSFontEntry(aFontName, fontRef,
                                 400, 0, NS_FONT_STYLE_NORMAL, nsnull, false);
        }
    } else {
        // lookup face based on postscript or full name
        CGFontRef fontRef = ::CGFontCreateWithFontName(CFStringRef(faceName));
        if (!fontRef) {
            return nsnull;
        }

        if (aProxyEntry) {
            PRUint16 w = aProxyEntry->mWeight;
            NS_ASSERTION(w >= 100 && w <= 900, "bogus font weight value!");

            newFontEntry =
                new CGFontEntry(aFontName, fontRef,
                                w, aProxyEntry->mStretch,
                                aProxyEntry->mItalic ?
                                    NS_FONT_STYLE_ITALIC : NS_FONT_STYLE_NORMAL,
                                true, true);
        } else {
            newFontEntry =
                new CGFontEntry(aFontName, fontRef,
                                400, 0, NS_FONT_STYLE_NORMAL,
                                false, false);
        }
        ::CFRelease(fontRef);
    }

    return newFontEntry;
}

gfxFontEntry*
gfxMacPlatformFontList::MakePlatformFont(const gfxProxyFontEntry *aProxyEntry,
                                         const PRUint8 *aFontData,
                                         PRUint32 aLength)
{
    return UseATSFontEntry()
        ? MakePlatformFontATS(aProxyEntry, aFontData, aLength)
        : MakePlatformFontCG(aProxyEntry, aFontData, aLength);
}

static void ReleaseData(void *info, const void *data, size_t size)
{
    NS_Free((void*)data);
}

gfxFontEntry*
gfxMacPlatformFontList::MakePlatformFontCG(const gfxProxyFontEntry *aProxyEntry,
                                           const PRUint8 *aFontData,
                                           PRUint32 aLength)
{
    NS_ASSERTION(aFontData, "MakePlatformFont called with null data");

    PRUint16 w = aProxyEntry->mWeight;
    NS_ASSERTION(w >= 100 && w <= 900, "bogus font weight value!");

    // create the font entry
    nsAutoString uniqueName;

    nsresult rv = gfxFontUtils::MakeUniqueUserFontName(uniqueName);
    if (NS_FAILED(rv)) {
        return nsnull;
    }

    CGDataProviderRef provider =
        ::CGDataProviderCreateWithData(nsnull, aFontData, aLength,
                                       &ReleaseData);
    CGFontRef fontRef = ::CGFontCreateWithDataProvider(provider);
    ::CGDataProviderRelease(provider);

    if (!fontRef) {
        return nsnull;
    }

    nsAutoPtr<CGFontEntry>
        newFontEntry(new CGFontEntry(uniqueName, fontRef, w,
                                     aProxyEntry->mStretch,
                                     aProxyEntry->mItalic ?
                                         NS_FONT_STYLE_ITALIC : NS_FONT_STYLE_NORMAL,
                                     true, false));
    ::CFRelease(fontRef);

    // if succeeded and font cmap is good, return the new font
    if (newFontEntry->mIsValid && NS_SUCCEEDED(newFontEntry->ReadCMAP())) {
        return newFontEntry.forget();
    }

    // if something is funky about this font, delete immediately
#if DEBUG
    char warnBuf[1024];
    sprintf(warnBuf, "downloaded font not loaded properly, removed face for (%s)",
            NS_ConvertUTF16toUTF8(aProxyEntry->mFamily->Name()).get());
    NS_WARNING(warnBuf);
#endif

    return nsnull;
}

// grumble, another non-publised Apple API dependency (found in Webkit code)
// activated with this value, font will not be found via system lookup routines
// it can only be used via the created ATSFontRef
// needed to prevent one doc from finding a font used in a separate doc

enum {
    kPrivateATSFontContextPrivate = 3
};

class ATSUserFontData : public gfxUserFontData {
public:
    ATSUserFontData(ATSFontContainerRef aContainerRef)
        : mContainerRef(aContainerRef)
    { }

    virtual ~ATSUserFontData()
    {
        // deactivate font
        if (mContainerRef) {
            ::ATSFontDeactivate(mContainerRef, NULL, kATSOptionFlagsDefault);
        }
    }

    ATSFontContainerRef     mContainerRef;
};

gfxFontEntry*
gfxMacPlatformFontList::MakePlatformFontATS(const gfxProxyFontEntry *aProxyEntry,
                                            const PRUint8 *aFontData,
                                            PRUint32 aLength)
{
    OSStatus err;

    NS_ASSERTION(aFontData, "MakePlatformFont called with null data");

    // MakePlatformFont is responsible for deleting the font data with NS_Free
    // so we set up a stack object to ensure it is freed even if we take an
    // early exit
    struct FontDataDeleter {
        FontDataDeleter(const PRUint8 *aFontData)
            : mFontData(aFontData) { }
        ~FontDataDeleter() { NS_Free((void*)mFontData); }
        const PRUint8 *mFontData;
    };
    FontDataDeleter autoDelete(aFontData);

    ATSFontRef fontRef;
    ATSFontContainerRef containerRef;

    // we get occasional failures when multiple fonts are activated in quick succession
    // if the ATS font cache is damaged; to work around this, we can retry the activation
    const PRUint32 kMaxRetries = 3;
    PRUint32 retryCount = 0;
    while (retryCount++ < kMaxRetries) {
        err = ::ATSFontActivateFromMemory(const_cast<PRUint8*>(aFontData), aLength,
                                          kPrivateATSFontContextPrivate,
                                          kATSFontFormatUnspecified,
                                          NULL,
                                          kATSOptionFlagsDoNotNotify,
                                          &containerRef);
        mATSGeneration = ::ATSGetGeneration();

        if (err != noErr) {
#if DEBUG
            char warnBuf[1024];
            sprintf(warnBuf, "downloaded font error, ATSFontActivateFromMemory err: %d for (%s)",
                    PRInt32(err),
                    NS_ConvertUTF16toUTF8(aProxyEntry->mFamily->Name()).get());
            NS_WARNING(warnBuf);
#endif
            return nsnull;
        }

        // ignoring containers with multiple fonts, use the first face only for now
        err = ::ATSFontFindFromContainer(containerRef, kATSOptionFlagsDefault, 1,
                                         &fontRef, NULL);
        if (err != noErr) {
#if DEBUG
            char warnBuf[1024];
            sprintf(warnBuf, "downloaded font error, ATSFontFindFromContainer err: %d for (%s)",
                    PRInt32(err),
                    NS_ConvertUTF16toUTF8(aProxyEntry->mFamily->Name()).get());
            NS_WARNING(warnBuf);
#endif
            ::ATSFontDeactivate(containerRef, NULL, kATSOptionFlagsDefault);
            return nsnull;
        }

        // now lookup the Postscript name; this may fail if the font cache is bad
        OSStatus err;
        NSString *psname = NULL;
        err = ::ATSFontGetPostScriptName(fontRef, kATSOptionFlagsDefault, (CFStringRef*) (&psname));
        if (err == noErr) {
            [psname release];
        } else {
#ifdef DEBUG
            char warnBuf[1024];
            sprintf(warnBuf, "ATSFontGetPostScriptName err = %d for (%s), retries = %d", (PRInt32)err,
                    NS_ConvertUTF16toUTF8(aProxyEntry->mFamily->Name()).get(), retryCount);
            NS_WARNING(warnBuf);
#endif
            ::ATSFontDeactivate(containerRef, NULL, kATSOptionFlagsDefault);
            // retry the activation a couple of times if this fails
            // (may be a transient failure due to ATS font cache issues)
            continue;
        }

        PRUint16 w = aProxyEntry->mWeight;
        NS_ASSERTION(w >= 100 && w <= 900, "bogus font weight value!");

        nsAutoString uniqueName;
        nsresult rv = gfxFontUtils::MakeUniqueUserFontName(uniqueName);
        if (NS_FAILED(rv)) {
            return nsnull;
        }

        // font entry will own this
        ATSUserFontData *userFontData = new ATSUserFontData(containerRef);

        ATSFontEntry *newFontEntry =
            new ATSFontEntry(uniqueName,
                             fontRef,
                             w, aProxyEntry->mStretch,
                             aProxyEntry->mItalic ?
                                 NS_FONT_STYLE_ITALIC : NS_FONT_STYLE_NORMAL,
                             userFontData, false);

        // if succeeded and font cmap is good, return the new font
        if (newFontEntry->mIsValid && NS_SUCCEEDED(newFontEntry->ReadCMAP())) {
            return newFontEntry;
        }

        // if something is funky about this font, delete immediately
#if DEBUG
        char warnBuf[1024];
        sprintf(warnBuf, "downloaded font not loaded properly, removed face for (%s)",
                NS_ConvertUTF16toUTF8(aProxyEntry->mFamily->Name()).get());
        NS_WARNING(warnBuf);
#endif
        delete newFontEntry;

        // We don't retry from here; the ATS font cache issue would have caused failure earlier
        // so if we get here, there's something else bad going on within our font data structures.
        // Currently, there should be no way to reach here, as fontentry creation cannot fail
        // except by memory allocation failure.
        NS_WARNING("invalid font entry for a newly activated font");
        break;
    }

    // if we get here, the activation failed (even with possible retries); can't use this font
    return nsnull;
}
