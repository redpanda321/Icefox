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

#import <Foundation/Foundation.h>
#import <UIKit/UIFont.h>

#include "gfxPlatformMac.h"
#include "gfxUIKitPlatformFontList.h"
#include "gfxUIKitFont.h"
#include "gfxUserFontSet.h"

#include "nsServiceManagerUtils.h"
#include "nsTArray.h"

#include "nsDirectoryServiceUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsISimpleEnumerator.h"

#include <unistd.h>
#include <time.h>

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
static PRLogModuleInfo *gFontInfoLog = PR_NewLogModule("fontInfoLog");
#endif /* PR_LOGGING */

#define LOG(args) PR_LOG(gFontInfoLog, PR_LOG_DEBUG, args)
#define LOG_ENABLED() PR_LOG_TEST(gFontInfoLog, PR_LOG_DEBUG)

/* UIKitFontEntry */
#pragma mark-

UIKitFontEntry::UIKitFontEntry(const nsAString& aPostscriptName,
                               PRInt32 aWeight,
                               gfxFontFamily *aFamily,
                               PRBool aIsStandardFace)
    : gfxFontEntry(aPostscriptName, aFamily, aIsStandardFace),
      mPostscriptName((CFStringRef)GetNSStringForString(aPostscriptName))
{
    mWeight = aWeight;
    ::CFRetain(mPostscriptName);
}

UIKitFontEntry::UIKitFontEntry(const nsAString& aPostscriptName,
                               PRUint16 aWeight, PRUint16 aStretch, PRUint32 aItalicStyle,
                               gfxUserFontData *aUserFontData)
    : gfxFontEntry(aPostscriptName),
      mPostscriptName((CFStringRef)GetNSStringForString(aPostscriptName))
{
    ::CFRetain(mPostscriptName);
    // xxx - stretch is basically ignored for now

    mUserFontData = aUserFontData;
    mWeight = aWeight;
    mStretch = aStretch;
    mFixedPitch = PR_FALSE; // xxx - do we need this for downloaded fonts?
    mItalic = (aItalicStyle & (NS_FONT_STYLE_ITALIC | NS_FONT_STYLE_OBLIQUE)) != 0;
    mIsUserFont = aUserFontData != nullptr;
}

// ATSUI requires AAT-enabled fonts to render complex scripts correctly.
// For now, simple clear out the cmap codepoints for fonts that have
// codepoints for complex scripts. (Bug 361986)
// Core Text is similar, but can render Arabic using OpenType fonts as well.

enum eComplexScript {
    eComplexScriptArabic,
    eComplexScriptIndic,
    eComplexScriptTibetan
};

struct ScriptRange {
    eComplexScript   script;
    PRUint32         rangeStart;
    PRUint32         rangeEnd;
};

const ScriptRange gScriptsThatRequireShaping[] = {
    { eComplexScriptArabic, 0x0600, 0x077F },   // Basic Arabic and Arabic Supplement
    { eComplexScriptIndic, 0x0900, 0x0D7F },     // Indic scripts - Devanagari, Bengali, ..., Malayalam
    { eComplexScriptTibetan, 0x0F00, 0x0FFF }     // Tibetan
    // Thai seems to be "renderable" without AAT morphing tables
    // xxx - Lao, Khmer?
};

nsresult
UIKitFontEntry::ReadCMAP()
{
    // attempt this once, if errors occur leave a blank cmap
    if (mCharacterMap)
        return NS_OK;

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

    mHasCmapTable = NS_SUCCEEDED(rv);
    if (mHasCmapTable) {
        gfxPlatformFontList *pfl = gfxPlatformFontList::PlatformFontList();
        mCharacterMap = pfl->FindCharMap(charmap);
    } else {
        mCharacterMap = new gfxCharacterMap();
    }

    PR_LOG(gFontInfoLog, PR_LOG_DEBUG, ("(fontinit-cmap) psname: %s\n",
                                        NS_ConvertUTF16toUTF8(mName).get()));

    return rv;
}

nsresult
UIKitFontEntry::GetFontTable(PRUint32 aTableTag, FallibleTArray<PRUint8>& aBuffer)
{
    nsAutoreleasePool localPool;

    CTFontRef ctFont = ::CTFontCreateWithName(mPostscriptName, 10, NULL);
    CFDataRef table  = ::CTFontCopyTable(ctFont, aTableTag, kCTFontTableOptionNoOptions);
    ::CFRelease(ctFont);
    if (!table)
        return NS_ERROR_FAILURE;

    if (!aBuffer.AppendElements(CFDataGetLength(table))) {
        ::CFRelease(table);
        return NS_ERROR_OUT_OF_MEMORY;
    }

    CFDataGetBytes(table,
                   CFRangeMake(0, CFDataGetLength(table)),
                   aBuffer.Elements());
    ::CFRelease(table);

    return NS_OK;
}

gfxFont*
UIKitFontEntry::CreateFontInstance(const gfxFontStyle *aFontStyle, bool aNeedsBold)
{
    return new gfxUIKitFont(this, aFontStyle, aNeedsBold);
}


/* gfxUIKitFontFamily */
#pragma mark-

class gfxUIKitFontFamily : public gfxFontFamily
{
public:
    gfxUIKitFontFamily(nsAString& aName) :
        gfxFontFamily(aName)
    {}

    virtual ~gfxUIKitFontFamily() {}

    virtual void LocalizedName(nsAString& aLocalizedName);

    virtual void FindStyleVariations();
};

void
gfxUIKitFontFamily::LocalizedName(nsAString& aLocalizedName)
{
    aLocalizedName = mName;
}

void
gfxUIKitFontFamily::FindStyleVariations()
{
    if (mHasStyles)
        return;

    nsAutoreleasePool localPool;

    NSString *family = GetNSStringForString(mName);
    for (NSString* psname in [UIFont fontNamesForFamilyName:family]) {
        PRBool isStandardFace = PR_FALSE;
        // make a nsString
        nsAutoString postscriptFontName;
        GetStringForNSString(psname, postscriptFontName);

        CTFontRef ctFont = ::CTFontCreateWithName((CFStringRef)psname, 10, NULL);
        CFDictionaryRef traits = ::CTFontCopyTraits(ctFont);
        NSString* facename = (NSString*)CTFontCopyName(ctFont, kCTFontStyleNameKey);
        if ([facename isEqualToString:@"Regular"] ||
            [facename isEqualToString:@"Bold"] ||
            [facename isEqualToString:@"Italic"] ||
            [facename isEqualToString:@"Oblique"] ||
            [facename isEqualToString:@"Bold Italic"] ||
            [facename isEqualToString:@"Bold Oblique"])
        {
            isStandardFace = PR_TRUE;
        }
        ::CFRelease(ctFont);

        CFNumberRef weight = (CFNumberRef)::CFDictionaryGetValue(traits, kCTFontWeightTrait);
        double ctWeight;
        CFNumberGetValue(weight, kCFNumberDoubleType, &ctWeight);
        ::CFRelease(traits);
        ::CFRelease(weight);

        PRInt32 cssWeight = PRInt32(round(((ctWeight + 1.0) / 2.0) * 800.0 + 100.0));
        //printf("FindStyleVariations: %s: ctWeight: %lf, cssWeight: %d\n",
        //       [psname UTF8String], ctWeight, cssWeight);
        // create a font entry
        UIKitFontEntry *fontEntry = new UIKitFontEntry(postscriptFontName,
                                                       cssWeight, this, isStandardFace);
        if (!fontEntry) break;

        // insert into font entry array of family
        AddFontEntry(fontEntry);
    }

    SortAvailableFonts();
    SetHasStyles(PR_TRUE);

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
                                                       PR_TRUE);
    mOtherFamilyNamesInitialized = PR_TRUE;
}


/* gfxUIKitPlatformFontList */
#pragma mark-

gfxUIKitPlatformFontList::gfxUIKitPlatformFontList() :
    gfxPlatformFontList(PR_FALSE)
{
}

nsresult
gfxUIKitPlatformFontList::InitFontList()
{
    printf("gfxUIKitPlatformFontList::InitFontList()\n");
    nsAutoreleasePool localPool;

    // reset font lists
    gfxPlatformFontList::InitFontList();

    nsAutoString availableFamilyName;
    NSString *availableFamily = nil;
    for (availableFamily in [UIFont familyNames]) {
        //printf("Font family: %s\n", [availableFamily UTF8String]);

        GetStringForNSString(availableFamily, availableFamilyName);
        // create a family entry
        gfxFontFamily *familyEntry = new gfxUIKitFontFamily(availableFamilyName);
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
gfxUIKitPlatformFontList::InitSingleFaceList()
{
    nsAutoTArray<nsString, 10> singleFaceFonts;
    gfxFontUtils::GetPrefsFontList("font.single-face-list", singleFaceFonts);

    PRUint32 numFonts = singleFaceFonts.Length();
    for (PRUint32 i = 0; i < numFonts; i++) {
        PR_LOG(gFontInfoLog, PR_LOG_DEBUG, ("(fontlist-singleface) face name: %s\n",
                                            NS_ConvertUTF16toUTF8(singleFaceFonts[i]).get()));
        gfxFontEntry *fontEntry = LookupLocalFont(nullptr, singleFaceFonts[i]);
        if (fontEntry) {
            nsAutoString familyName, key;
            familyName = singleFaceFonts[i];
            GenerateFontListKey(familyName, key);
            PR_LOG(gFontInfoLog, PR_LOG_DEBUG, ("(fontlist-singleface) family name: %s, key: %s\n",
                   NS_ConvertUTF16toUTF8(familyName).get(), NS_ConvertUTF16toUTF8(key).get()));

            // add only if doesn't exist already
            bool found;
            gfxFontFamily *familyEntry;
            if (!(familyEntry = mFontFamilies.GetWeak(key, &found))) {
                familyEntry = new gfxSingleFaceMacFontFamily(familyName);
                familyEntry->AddFontEntry(fontEntry);
                familyEntry->SetHasStyles(PR_TRUE);
                mFontFamilies.Put(key, familyEntry);
                PR_LOG(gFontInfoLog, PR_LOG_DEBUG, ("(fontlist-singleface) added new family\n",
                       NS_ConvertUTF16toUTF8(familyName).get(), NS_ConvertUTF16toUTF8(key).get()));
            }
        }
    }
}

void
gfxUIKitPlatformFontList::EliminateDuplicateFaces(const nsAString& aFamilyName)
{
}

bool
gfxUIKitPlatformFontList::GetStandardFamilyName(const nsAString& aFontName, nsAString& aFamilyName)
{
    gfxFontFamily *family = FindFamily(aFontName);
    if (family) {
        family->LocalizedName(aFamilyName);
        return true;
    }

    return false;
}

gfxFontFamily*
gfxUIKitPlatformFontList::GetDefaultFont(const gfxFontStyle* aStyle)
{
    // See https://groups.google.com/forum/?fromgroups=#!topic/rubymotion/xzzTWPRaZz0
    // for possible fix
    return 0;/*
    nsAutoreleasePool localPool;

    NSString *defaultFamily = [[UIFont systemFontOfSize:aStyle->size] familyName];
    nsAutoString familyName;

    GetStringForNSString(defaultFamily, familyName);
    return FindFamily(familyName);
    */
}

gfxFontEntry*
gfxUIKitPlatformFontList::LookupLocalFont(const gfxProxyFontEntry *aProxyEntry,
                                          const nsAString& aFontName)
{
    nsAutoreleasePool localPool;

    NSString *faceName = GetNSStringForString(aFontName);

    UIKitFontEntry *newFontEntry;
    if (aProxyEntry) {
        PRUint16 w = aProxyEntry->mWeight;
        NS_ASSERTION(w >= 100 && w <= 900, "bogus font weight value!");

        newFontEntry =
            new UIKitFontEntry(aFontName,
                               w, aProxyEntry->mStretch,
                               aProxyEntry->mItalic ?
                                   NS_FONT_STYLE_ITALIC : NS_FONT_STYLE_NORMAL,
                               nullptr);
    } else {
        newFontEntry =
            new UIKitFontEntry(aFontName,
                               400, 0, NS_FONT_STYLE_NORMAL, nullptr);
    }

    return newFontEntry;
}

gfxFontEntry*
gfxUIKitPlatformFontList::MakePlatformFont(const gfxProxyFontEntry *aProxyEntry,
                                           const PRUint8 *aFontData,
                                           PRUint32 aLength)
{
    //XXX: are there enough APIs to do this?
    return nullptr;
}

