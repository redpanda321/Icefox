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
 * The Original Code is Mozilla Japan code.
 *
 * The Initial Developer of the Original Code is Mozilla Japan.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Masayuki Nakano <masayuki@d-toybox.com>
 *   Vladimir Vukicevic <vladimir@pobox.com>
 *   Karl Tomlinson <karlt+@karlt.net>, Mozilla Corporation
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

#include "gfxFontconfigUtils.h"
#include "gfxFont.h"

#include <locale.h>
#include <fontconfig/fontconfig.h>

#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsServiceManagerUtils.h"
#include "nsILanguageAtomService.h"
#include "nsTArray.h"

#include "nsIAtom.h"
#include "nsCRT.h"

/* static */ gfxFontconfigUtils* gfxFontconfigUtils::sUtils = nsnull;
static nsILanguageAtomService* gLangService = nsnull;

/* static */ void
gfxFontconfigUtils::Shutdown() {
    if (sUtils) {
        delete sUtils;
        sUtils = nsnull;
    }
    NS_IF_RELEASE(gLangService);
}

/* static */ PRUint8
gfxFontconfigUtils::FcSlantToThebesStyle(int aFcSlant)
{
    switch (aFcSlant) {
        case FC_SLANT_ITALIC:
            return FONT_STYLE_ITALIC;
        case FC_SLANT_OBLIQUE:
            return FONT_STYLE_OBLIQUE;
        default:
            return FONT_STYLE_NORMAL;
    }
}

/* static */ PRUint8
gfxFontconfigUtils::GetThebesStyle(FcPattern *aPattern)
{
    int slant;
    if (FcPatternGetInteger(aPattern, FC_SLANT, 0, &slant) != FcResultMatch) {
        return FONT_STYLE_NORMAL;
    }

    return FcSlantToThebesStyle(slant);
}

/* static */ int
gfxFontconfigUtils::GetFcSlant(const gfxFontStyle& aFontStyle)
{
    if (aFontStyle.style == FONT_STYLE_ITALIC)
        return FC_SLANT_ITALIC;
    if (aFontStyle.style == FONT_STYLE_OBLIQUE)
        return FC_SLANT_OBLIQUE;

    return FC_SLANT_ROMAN;
}

// OS/2 weight classes were introduced in fontconfig-2.1.93 (2003).
#ifndef FC_WEIGHT_THIN 
#define FC_WEIGHT_THIN              0 // 2.1.93
#define FC_WEIGHT_EXTRALIGHT        40 // 2.1.93
#define FC_WEIGHT_REGULAR           80 // 2.1.93
#define FC_WEIGHT_EXTRABOLD         205 // 2.1.93
#endif
// book was introduced in fontconfig-2.2.90 (and so fontconfig-2.3.0 in 2005)
#ifndef FC_WEIGHT_BOOK
#define FC_WEIGHT_BOOK              75
#endif
// extra black was introduced in fontconfig-2.4.91 (2007)
#ifndef FC_WEIGHT_EXTRABLACK
#define FC_WEIGHT_EXTRABLACK        215
#endif

/* static */ PRUint16
gfxFontconfigUtils::GetThebesWeight(FcPattern *aPattern)
{
    int weight;
    if (FcPatternGetInteger(aPattern, FC_WEIGHT, 0, &weight) != FcResultMatch)
        return FONT_WEIGHT_NORMAL;

    if (weight <= (FC_WEIGHT_THIN + FC_WEIGHT_EXTRALIGHT) / 2)
        return 100;
    if (weight <= (FC_WEIGHT_EXTRALIGHT + FC_WEIGHT_LIGHT) / 2)
        return 200;
    if (weight <= (FC_WEIGHT_LIGHT + FC_WEIGHT_BOOK) / 2)
        return 300;
    if (weight <= (FC_WEIGHT_REGULAR + FC_WEIGHT_MEDIUM) / 2)
        // This includes FC_WEIGHT_BOOK
        return 400;
    if (weight <= (FC_WEIGHT_MEDIUM + FC_WEIGHT_DEMIBOLD) / 2)
        return 500;
    if (weight <= (FC_WEIGHT_DEMIBOLD + FC_WEIGHT_BOLD) / 2)
        return 600;
    if (weight <= (FC_WEIGHT_BOLD + FC_WEIGHT_EXTRABOLD) / 2)
        return 700;
    if (weight <= (FC_WEIGHT_EXTRABOLD + FC_WEIGHT_BLACK) / 2)
        return 800;
    if (weight <= FC_WEIGHT_BLACK)
        return 900;

    // including FC_WEIGHT_EXTRABLACK
    return 901;
}

/* static */ int
gfxFontconfigUtils::FcWeightForBaseWeight(PRInt8 aBaseWeight)
{
    NS_PRECONDITION(aBaseWeight >= 0 && aBaseWeight <= 10,
                    "base weight out of range");

    switch (aBaseWeight) {
        case 2:
            return FC_WEIGHT_EXTRALIGHT;
        case 3:
            return FC_WEIGHT_LIGHT;
        case 4:
            return FC_WEIGHT_REGULAR;
        case 5:
            return FC_WEIGHT_MEDIUM;
        case 6:
            return FC_WEIGHT_DEMIBOLD;
        case 7:
            return FC_WEIGHT_BOLD;
        case 8:
            return FC_WEIGHT_EXTRABOLD;
        case 9:
            return FC_WEIGHT_BLACK;
    }

    // extremes
    return aBaseWeight < 2 ? FC_WEIGHT_THIN : FC_WEIGHT_EXTRABLACK;
}

// This makes a guess at an FC_WEIGHT corresponding to a base weight and
// offset (without any knowledge of which weights are available).

/* static */ int
GuessFcWeight(const gfxFontStyle& aFontStyle)
{
    /*
     * weights come in two parts crammed into one
     * integer -- the "base" weight is weight / 100,
     * the rest of the value is the "offset" from that
     * weight -- the number of steps to move to adjust
     * the weight in the list of supported font weights,
     * this value can be negative or positive.
     */
    PRInt8 weight;
    PRInt8 offset;
    aFontStyle.ComputeWeightAndOffset(&weight, &offset);

    // ComputeWeightAndOffset trimmed the range of weights for us
    NS_ASSERTION(weight >= 0 && weight <= 10,
                 "base weight out of range");

    // Most font families do not support every weight.  The tables here are
    // chosen such that a normal (4) base weight and an offset of +1 will
    // guess bold.

    // Mapping from weight to a guess of the nearest available lighter weight
    static const int lighterGuess[11] =
        { 0, 0, 1, 1, 2, 3, 4, 4, 6, 7, 8 };
    // Mapping from weight to a guess of the nearest available bolder weight
    static const int bolderGuess[11] =
        { 2, 3, 4, 6, 7, 7, 8, 9, 10, 10, 10 };

    while (offset < 0) {
        weight = lighterGuess[weight];
        offset++;
    }
    while (offset > 0) {
        weight = bolderGuess[weight];
        offset--;
    }

    return gfxFontconfigUtils::FcWeightForBaseWeight(weight);
}

static void
AddString(FcPattern *aPattern, const char *object, const char *aString)
{
    FcPatternAddString(aPattern, object,
                       gfxFontconfigUtils::ToFcChar8(aString));
}

static void
AddLangGroup(FcPattern *aPattern, const nsACString& aLangGroup)
{
    // Translate from mozilla's internal mapping into fontconfig's
    nsCAutoString lang;
    gfxFontconfigUtils::GetSampleLangForGroup(aLangGroup, &lang);

    if (!lang.IsEmpty()) {
        AddString(aPattern, FC_LANG, lang.get());
    }
}


nsReturnRef<FcPattern>
gfxFontconfigUtils::NewPattern(const nsTArray<nsString>& aFamilies,
                               const gfxFontStyle& aFontStyle,
                               const char *aLang)
{
    nsAutoRef<FcPattern> pattern(FcPatternCreate());
    if (!pattern)
        return nsReturnRef<FcPattern>();

    FcPatternAddDouble(pattern, FC_PIXEL_SIZE, aFontStyle.size);
    FcPatternAddInteger(pattern, FC_SLANT, GetFcSlant(aFontStyle));
    FcPatternAddInteger(pattern, FC_WEIGHT, GuessFcWeight(aFontStyle));

    if (aLang) {
        AddString(pattern, FC_LANG, aLang);
    }

    for (PRUint32 i = 0; i < aFamilies.Length(); ++i) {
        NS_ConvertUTF16toUTF8 family(aFamilies[i]);
        AddString(pattern, FC_FAMILY, family.get());
    }

    return pattern.out();
}

gfxFontconfigUtils::gfxFontconfigUtils()
    : mLastConfig(NULL)
{
    mFontsByFamily.Init(50);
    mFontsByFullname.Init(50);
    mLangSupportTable.Init(20);
    UpdateFontListInternal();
}

nsresult
gfxFontconfigUtils::GetFontList(nsIAtom *aLangGroup,
                                const nsACString& aGenericFamily,
                                nsTArray<nsString>& aListOfFonts)
{
    aListOfFonts.Clear();

    nsTArray<nsCString> fonts;
    nsCAutoString langGroupStr;
    if (aLangGroup) {
        aLangGroup->ToUTF8String(langGroupStr);
    }
    nsresult rv = GetFontListInternal(fonts, langGroupStr);
    if (NS_FAILED(rv))
        return rv;

    for (PRUint32 i = 0; i < fonts.Length(); ++i) {
        aListOfFonts.AppendElement(NS_ConvertUTF8toUTF16(fonts[i]));
    }

    aListOfFonts.Sort();

    PRInt32 serif = 0, sansSerif = 0, monospace = 0;

    // Fontconfig supports 3 generic fonts, "serif", "sans-serif", and
    // "monospace", slightly different from CSS's 5.
    if (aGenericFamily.IsEmpty())
        serif = sansSerif = monospace = 1;
    else if (aGenericFamily.LowerCaseEqualsLiteral("serif"))
        serif = 1;
    else if (aGenericFamily.LowerCaseEqualsLiteral("sans-serif"))
        sansSerif = 1;
    else if (aGenericFamily.LowerCaseEqualsLiteral("monospace"))
        monospace = 1;
    else if (aGenericFamily.LowerCaseEqualsLiteral("cursive") ||
             aGenericFamily.LowerCaseEqualsLiteral("fantasy"))
        serif = sansSerif = 1;
    else
        NS_NOTREACHED("unexpected CSS generic font family");

    // The first in the list becomes the default in
    // gFontsDialog.readFontSelection() if the preference-selected font is not
    // available, so put system configured defaults first.
    if (monospace)
        aListOfFonts.InsertElementAt(0, NS_LITERAL_STRING("monospace"));
    if (sansSerif)
        aListOfFonts.InsertElementAt(0, NS_LITERAL_STRING("sans-serif"));
    if (serif)
        aListOfFonts.InsertElementAt(0, NS_LITERAL_STRING("serif"));

    return NS_OK;
}

struct MozLangGroupData {
    const char *mozLangGroup;
    const char *defaultLang;
};

const MozLangGroupData MozLangGroups[] = {
    { "x-western",      "en" },
    { "x-central-euro", "pl" },
    { "x-cyrillic",     "ru" },
    { "x-baltic",       "lv" },
    { "x-devanagari",   "hi" },
    { "x-tamil",        "ta" },
    { "x-armn",         "hy" },
    { "x-beng",         "bn" },
    { "x-cans",         "iu" },
    { "x-ethi",         "am" },
    { "x-geor",         "ka" },
    { "x-gujr",         "gu" },
    { "x-guru",         "pa" },
    { "x-khmr",         "km" },
    { "x-knda",         "kn" },
    { "x-mlym",         "ml" },
    { "x-orya",         "or" },
    { "x-sinh",         "si" },
    { "x-telu",         "te" },
    { "x-tibt",         "bo" },
    { "x-unicode",      0    },
    { "x-user-def",     0    }
};

static PRBool
TryLangForGroup(const nsACString& aOSLang, nsIAtom *aLangGroup,
                nsACString *aFcLang)
{
    // Truncate at '.' or '@' from aOSLang, and convert '_' to '-'.
    // aOSLang is in the form "language[_territory][.codeset][@modifier]".
    // fontconfig takes languages in the form "language-territory".
    // nsILanguageAtomService takes languages in the form language-subtag,
    // where subtag may be a territory.  fontconfig and nsILanguageAtomService
    // handle case-conversion for us.
    const char *pos, *end;
    aOSLang.BeginReading(pos);
    aOSLang.EndReading(end);
    aFcLang->Truncate();
    while (pos < end) {
        switch (*pos) {
            case '.':
            case '@':
                end = pos;
                break;
            case '_':
                aFcLang->Append('-');
                break;
            default:
                aFcLang->Append(*pos);
        }
        ++pos;
    }

    nsIAtom *atom =
        gLangService->LookupLanguage(NS_ConvertUTF8toUTF16(*aFcLang));

    return atom == aLangGroup;
}

/* static */ void
gfxFontconfigUtils::GetSampleLangForGroup(const nsACString& aLangGroup,
                                          nsACString *aFcLang)
{
    NS_PRECONDITION(aFcLang != nsnull, "aFcLang must not be NULL");

    const MozLangGroupData *langGroup = nsnull;

    for (unsigned int i = 0; i < NS_ARRAY_LENGTH(MozLangGroups); ++i) {
        if (aLangGroup.Equals(MozLangGroups[i].mozLangGroup,
                              nsCaseInsensitiveCStringComparator())) {
            langGroup = &MozLangGroups[i];
            break;
        }
    }

    if (!langGroup) {
        // Not a special mozilla language group.
        // Use aLangGroup as a language code.
        aFcLang->Assign(aLangGroup);
        return;
    }

    // Check the environment for the users preferred language that corresponds
    // to this langGroup.
    if (!gLangService) {
        CallGetService(NS_LANGUAGEATOMSERVICE_CONTRACTID, &gLangService);
    }

    if (gLangService) {
        nsRefPtr<nsIAtom> langGroupAtom = do_GetAtom(langGroup->mozLangGroup);

        const char *languages = getenv("LANGUAGE");
        if (languages) {
            const char separator = ':';

            for (const char *pos = languages; PR_TRUE; ++pos) {
                if (*pos == '\0' || *pos == separator) {
                    if (languages < pos &&
                        TryLangForGroup(Substring(languages, pos),
                                        langGroupAtom, aFcLang))
                        return;

                    if (*pos == '\0')
                        break;

                    languages = pos + 1;
                }
            }
        }
        const char *ctype = setlocale(LC_CTYPE, NULL);
        if (ctype &&
            TryLangForGroup(nsDependentCString(ctype), langGroupAtom, aFcLang))
            return;
    }

    if (langGroup->defaultLang) {
        aFcLang->Assign(langGroup->defaultLang);
    } else {
        aFcLang->Truncate();
    }
}

nsresult
gfxFontconfigUtils::GetFontListInternal(nsTArray<nsCString>& aListOfFonts,
                                        const nsACString& aLangGroup)
{
    FcPattern *pat = NULL;
    FcObjectSet *os = NULL;
    FcFontSet *fs = NULL;
    nsresult rv = NS_ERROR_FAILURE;

    aListOfFonts.Clear();

    pat = FcPatternCreate();
    if (!pat)
        goto end;

    os = FcObjectSetBuild(FC_FAMILY, NULL);
    if (!os)
        goto end;

    // take the pattern and add the lang group to it
    if (!aLangGroup.IsEmpty()) {
        AddLangGroup(pat, aLangGroup);
    }

    fs = FcFontList(NULL, pat, os);
    if (!fs)
        goto end;

    for (int i = 0; i < fs->nfont; i++) {
        char *family;

        if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0,
                               (FcChar8 **) &family) != FcResultMatch)
        {
            continue;
        }

        // Remove duplicates...
        nsCAutoString strFamily(family);
        if (aListOfFonts.Contains(strFamily))
            continue;

        aListOfFonts.AppendElement(strFamily);
    }

    rv = NS_OK;

  end:
    if (NS_FAILED(rv))
        aListOfFonts.Clear();

    if (pat)
        FcPatternDestroy(pat);
    if (os)
        FcObjectSetDestroy(os);
    if (fs)
        FcFontSetDestroy(fs);

    return rv;
}

nsresult
gfxFontconfigUtils::UpdateFontList()
{
    return UpdateFontListInternal(PR_TRUE);
}

nsresult
gfxFontconfigUtils::UpdateFontListInternal(PRBool aForce)
{
    if (!aForce) {
        // This checks periodically according to fontconfig's configured
        // <rescan> interval.
        FcInitBringUptoDate();
    } else if (!FcConfigUptoDate(NULL)) { // check now with aForce
        mLastConfig = NULL;
        FcInitReinitialize();
    }

    // FcInitReinitialize() (used by FcInitBringUptoDate) creates a new config
    // before destroying the old config, so the only way that we'd miss an
    // update is if fontconfig did more than one update and the memory for the
    // most recent config happened to be at the same location as the original
    // config.
    FcConfig *currentConfig = FcConfigGetCurrent();
    if (currentConfig == mLastConfig)
        return NS_OK;

    // This FcFontSet is owned by fontconfig
    FcFontSet *fontSet = FcConfigGetFonts(currentConfig, FcSetSystem);

    mFontsByFamily.Clear();
    mFontsByFullname.Clear();
    mLangSupportTable.Clear();
    mAliasForMultiFonts.Clear();

    // Record the existing font families
    for (int f = 0; f < fontSet->nfont; ++f) {
        FcPattern *font = fontSet->fonts[f];

        FcChar8 *family;
        for (int v = 0;
             FcPatternGetString(font, FC_FAMILY, v, &family) == FcResultMatch;
             ++v) {
            FontsByFcStrEntry *entry = mFontsByFamily.PutEntry(family);
            if (entry) {
                PRBool added = entry->AddFont(font);

                if (!entry->mKey) {
                    // The reference to the font pattern keeps the pointer to
                    // string for the key valid.  If adding the font failed
                    // then the entry must be removed.
                    if (added) {
                        entry->mKey = family;
                    } else {
                        mFontsByFamily.RawRemoveEntry(entry);
                    }
                }
            }
        }
    }

    // XXX we don't support all alias names.
    // Because if we don't check whether the given font name is alias name,
    // fontconfig converts the non existing font to sans-serif.
    // This is not good if the web page specifies font-family
    // that has Windows font name in the first.
    nsCOMPtr<nsIPrefService> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
    if (!prefs)
        return NS_ERROR_FAILURE;

    nsCOMPtr<nsIPrefBranch> prefBranch;
    prefs->GetBranch(0, getter_AddRefs(prefBranch));
    if (!prefBranch)
        return NS_ERROR_FAILURE;

    nsXPIDLCString list;
    prefBranch->GetCharPref("font.alias-list", getter_Copies(list));

    if (!list.IsEmpty()) {
        const char kComma = ',';
        const char *p, *p_end;
        list.BeginReading(p);
        list.EndReading(p_end);
        while (p < p_end) {
            while (nsCRT::IsAsciiSpace(*p)) {
                if (++p == p_end)
                    break;
            }
            if (p == p_end)
                break;
            const char *start = p;
            while (++p != p_end && *p != kComma)
                /* nothing */ ;
            nsCAutoString name(Substring(start, p));
            name.CompressWhitespace(PR_FALSE, PR_TRUE);
            mAliasForMultiFonts.AppendElement(name);
            p++;
        }
    }

    mLastConfig = currentConfig;
    return NS_OK;
}

nsresult
gfxFontconfigUtils::GetStandardFamilyName(const nsAString& aFontName, nsAString& aFamilyName)
{
    aFamilyName.Truncate();

    // The fontconfig has generic family names in the font list.
    if (aFontName.EqualsLiteral("serif") ||
        aFontName.EqualsLiteral("sans-serif") ||
        aFontName.EqualsLiteral("monospace")) {
        aFamilyName.Assign(aFontName);
        return NS_OK;
    }

    nsresult rv = UpdateFontListInternal();
    if (NS_FAILED(rv))
        return rv;

    NS_ConvertUTF16toUTF8 fontname(aFontName);

    // return empty string if no such family exists
    if (!IsExistingFamily(fontname))
        return NS_OK;

    FcPattern *pat = NULL;
    FcObjectSet *os = NULL;
    FcFontSet *givenFS = NULL;
    nsTArray<nsCString> candidates;
    FcFontSet *candidateFS = NULL;
    rv = NS_ERROR_FAILURE;

    pat = FcPatternCreate();
    if (!pat)
        goto end;

    FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)fontname.get());

    os = FcObjectSetBuild(FC_FAMILY, FC_FILE, FC_INDEX, NULL);
    if (!os)
        goto end;

    givenFS = FcFontList(NULL, pat, os);
    if (!givenFS)
        goto end;

    // The first value associated with a FC_FAMILY property is the family
    // returned by GetFontList(), so use this value if appropriate.

    // See if there is a font face with first family equal to the given family.
    for (int i = 0; i < givenFS->nfont; ++i) {
        char *firstFamily;
        if (FcPatternGetString(givenFS->fonts[i], FC_FAMILY, 0,
                               (FcChar8 **) &firstFamily) != FcResultMatch)
            continue;

        nsDependentCString first(firstFamily);
        if (!candidates.Contains(first)) {
            candidates.AppendElement(first);

            if (fontname.Equals(first)) {
                aFamilyName.Assign(aFontName);
                rv = NS_OK;
                goto end;
            }
        }
    }

    // See if any of the first family names represent the same set of font
    // faces as the given family.
    for (PRUint32 j = 0; j < candidates.Length(); ++j) {
        FcPatternDel(pat, FC_FAMILY);
        FcPatternAddString(pat, FC_FAMILY, (FcChar8 *)candidates[j].get());

        candidateFS = FcFontList(NULL, pat, os);
        if (!candidateFS)
            goto end;

        if (candidateFS->nfont != givenFS->nfont)
            continue;

        PRBool equal = PR_TRUE;
        for (int i = 0; i < givenFS->nfont; ++i) {
            if (!FcPatternEqual(candidateFS->fonts[i], givenFS->fonts[i])) {
                equal = PR_FALSE;
                break;
            }
        }
        if (equal) {
            AppendUTF8toUTF16(candidates[j], aFamilyName);
            rv = NS_OK;
            goto end;
        }
    }

    // No match found; return empty string.
    rv = NS_OK;

  end:
    if (pat)
        FcPatternDestroy(pat);
    if (os)
        FcObjectSetDestroy(os);
    if (givenFS)
        FcFontSetDestroy(givenFS);
    if (candidateFS)
        FcFontSetDestroy(candidateFS);

    return rv;
}

nsresult
gfxFontconfigUtils::ResolveFontName(const nsAString& aFontName,
                                    gfxPlatform::FontResolverCallback aCallback,
                                    void *aClosure,
                                    PRBool& aAborted)
{
    aAborted = PR_FALSE;

    nsresult rv = UpdateFontListInternal();
    if (NS_FAILED(rv))
        return rv;

    NS_ConvertUTF16toUTF8 fontname(aFontName);
    // Sometimes, the font has two or more names (e.g., "Sazanami Gothic" has
    // Japanese localized name).  We should not resolve to a single name
    // because different names sometimes have different behavior. e.g., with
    // the default settings of "Sazanami" on Fedora Core 5, the non-localized
    // name uses anti-alias, but the localized name uses it.  So, we should
    // check just whether the font is existing, without resolving to regular
    // name.
    //
    // The family names in mAliasForMultiFonts are names understood by
    // fontconfig.  The actual font to which they resolve depends on the
    // entire match pattern.  That info is not available here, but there
    // will be a font so leave the resolving to the gfxFontGroup.
    if (IsExistingFamily(fontname) ||
        mAliasForMultiFonts.Contains(fontname, gfxIgnoreCaseCStringComparator()))
        aAborted = !(*aCallback)(aFontName, aClosure);

    return NS_OK;
}

PRBool
gfxFontconfigUtils::IsExistingFamily(const nsCString& aFamilyName)
{
    return mFontsByFamily.GetEntry(ToFcChar8(aFamilyName)) != nsnull;
}

const nsTArray< nsCountedRef<FcPattern> >&
gfxFontconfigUtils::GetFontsForFamily(const FcChar8 *aFamilyName)
{
    FontsByFcStrEntry *entry = mFontsByFamily.GetEntry(aFamilyName);

    if (!entry)
        return mEmptyPatternArray;

    return entry->GetFonts();
}

// Fontconfig only provides a fullname property for fonts in formats with SFNT
// wrappers.  For other font formats (including PCF and PS Type 1), a fullname
// must be generated from the family and style properties.  Only the first
// family and style is checked, but that should be OK, as I don't expect
// non-SFNT fonts to have multiple families or styles.
PRBool
gfxFontconfigUtils::GetFullnameFromFamilyAndStyle(FcPattern *aFont,
                                                  nsACString *aFullname)
{
    FcChar8 *family;
    if (FcPatternGetString(aFont, FC_FAMILY, 0, &family) != FcResultMatch)
        return PR_FALSE;

    aFullname->Truncate();
    aFullname->Append(ToCString(family));

    FcChar8 *style;
    if (FcPatternGetString(aFont, FC_STYLE, 0, &style) == FcResultMatch &&
        strcmp(ToCString(style), "Regular") != 0) {
        aFullname->Append(' ');
        aFullname->Append(ToCString(style));
    }

    return PR_TRUE;
}

PRBool
gfxFontconfigUtils::FontsByFullnameEntry::KeyEquals(KeyTypePointer aKey) const
{
    const FcChar8 *key = mKey;
    // If mKey is NULL, key comes from the style and family of the first font.
    nsCAutoString fullname;
    if (!key) {
        NS_ASSERTION(mFonts.Length(), "No font in FontsByFullnameEntry!");
        GetFullnameFromFamilyAndStyle(mFonts[0], &fullname);

        key = ToFcChar8(fullname);
    }

    return FcStrCmpIgnoreCase(aKey, key) == 0;
}

void
gfxFontconfigUtils::AddFullnameEntries()
{
    // This FcFontSet is owned by fontconfig
    FcFontSet *fontSet = FcConfigGetFonts(NULL, FcSetSystem);

    // Record the existing font families
    for (int f = 0; f < fontSet->nfont; ++f) {
        FcPattern *font = fontSet->fonts[f];

        int v = 0;
        FcChar8 *fullname;
        while (FcPatternGetString(font,
                                  FC_FULLNAME, v, &fullname) == FcResultMatch) {
            FontsByFullnameEntry *entry = mFontsByFullname.PutEntry(fullname);
            if (entry) {
                // entry always has space for one font, so the first AddFont
                // will always succeed, and so the entry will always have a
                // font from which to obtain the key.
                PRBool added = entry->AddFont(font);
                // The key may be NULL either if this is the first font, or if
                // the first font does not have a fullname property, and so
                // the key is obtained from the font.  Set the key in both
                // cases.  The check that AddFont succeeded is required for
                // the second case.
                if (!entry->mKey && added) {
                    entry->mKey = fullname;
                }
            }

            ++v;
        }

        // Fontconfig does not provide a fullname property for all fonts.
        if (v == 0) {
            nsCAutoString name;
            if (!GetFullnameFromFamilyAndStyle(font, &name))
                continue;

            FontsByFullnameEntry *entry =
                mFontsByFullname.PutEntry(ToFcChar8(name));
            if (entry) {
                entry->AddFont(font);
                // Either entry->mKey has been set for a previous font or it
                // remains NULL to indicate that the key is obtained from the
                // first font.
            }
        }
    }
}

const nsTArray< nsCountedRef<FcPattern> >&
gfxFontconfigUtils::GetFontsForFullname(const FcChar8 *aFullname)
{
    if (mFontsByFullname.Count() == 0) {
        AddFullnameEntries();
    }

    FontsByFullnameEntry *entry = mFontsByFullname.GetEntry(aFullname);

    if (!entry)
        return mEmptyPatternArray;

    return entry->GetFonts();
}

static FcLangResult
CompareLangString(const FcChar8 *aLangA, const FcChar8 *aLangB) {
    FcLangResult result = FcLangDifferentLang;
    for (PRUint32 i = 0; ; ++i) {
        FcChar8 a = FcToLower(aLangA[i]);
        FcChar8 b = FcToLower(aLangB[i]);

        if (a != b) {
            if ((a == '\0' && b == '-') || (a == '-' && b == '\0'))
                return FcLangDifferentCountry;

            return result;
        }
        if (a == '\0')
            return FcLangEqual;

        if (a == '-') {
            result = FcLangDifferentCountry;
        }
    }
}

/* static */
FcLangResult
gfxFontconfigUtils::GetLangSupport(FcPattern *aFont, const FcChar8 *aLang)
{
    // When fontconfig builds a pattern for a system font, it will set a
    // single LangSet property value for the font.  That value may be removed
    // and additional string values may be added through FcConfigSubsitute
    // with FcMatchScan.  Values that are neither LangSet nor string are
    // considered errors in fontconfig sort and match functions.
    //
    // If no string nor LangSet value is found, then either the font is a
    // system font and the LangSet has been removed through FcConfigSubsitute,
    // or the font is a web font and its language support is unknown.
    // Returning FcLangDifferentLang for these fonts ensures that this font
    // will not be assumed to satisfy the language, and so language will be
    // prioritized in sorting fallback fonts.
    FcValue value;
    FcLangResult best = FcLangDifferentLang;
    for (int v = 0;
         FcPatternGet(aFont, FC_LANG, v, &value) == FcResultMatch;
         ++v) {

        FcLangResult support;
        switch (value.type) {
            case FcTypeLangSet:
                support = FcLangSetHasLang(value.u.l, aLang);
                break;
            case FcTypeString:
                support = CompareLangString(value.u.s, aLang);
                break;
            default:
                // error. continue to see if there is a useful value.
                continue;
        }

        if (support < best) { // lower is better
            if (support == FcLangEqual)
                return support;
            best = support;
        }        
    }

    return best;
}

gfxFontconfigUtils::LangSupportEntry *
gfxFontconfigUtils::GetLangSupportEntry(const FcChar8 *aLang, PRBool aWithFonts)
{
    // Currently any unrecognized languages from documents will be converted
    // to x-unicode by nsILanguageAtomService, so there is a limit on the
    // langugages that will be added here.  Reconsider when/if document
    // languages are passed to this routine.

    LangSupportEntry *entry = mLangSupportTable.PutEntry(aLang);
    if (!entry)
        return nsnull;

    FcLangResult best = FcLangDifferentLang;

    if (!entry->IsKeyInitialized()) {
        entry->InitKey(aLang);
    } else {
        // mSupport is already initialized.
        if (!aWithFonts)
            return entry;

        best = entry->mSupport;
        // If there is support for this language, an empty font list indicates
        // that the list hasn't been initialized yet.
        if (best == FcLangDifferentLang || entry->mFonts.Length() > 0)
            return entry;
    }

    // This FcFontSet is owned by fontconfig
    FcFontSet *fontSet = FcConfigGetFonts(NULL, FcSetSystem);

    nsAutoTArray<FcPattern*,100> fonts;

    for (int f = 0; f < fontSet->nfont; ++f) {
        FcPattern *font = fontSet->fonts[f];

        FcLangResult support = GetLangSupport(font, aLang);

        if (support < best) { // lower is better
            best = support;
            if (aWithFonts) {
                fonts.Clear();
            } else if (best == FcLangEqual) {
                break;
            }
        }

        // The font list in the LangSupportEntry is expected to be used only
        // when no default fonts support the language.  There would be a large
        // number of fonts in entries for languages using Latin script but
        // these do not need to be created because default fonts already
        // support these languages.
        if (aWithFonts && support != FcLangDifferentLang && support == best) {
            fonts.AppendElement(font);
        }
    }

    entry->mSupport = best;
    if (aWithFonts) {
        if (fonts.Length() != 0) {
            entry->mFonts.AppendElements(fonts.Elements(), fonts.Length());
        } else if (best != FcLangDifferentLang) {
            // Previously there was a font that supported this language at the
            // level of entry->mSupport, but it has now disappeared.  At least
            // entry->mSupport needs to be recalculated, but this is an
            // indication that the set of installed fonts has changed, so
            // update all caches.
            mLastConfig = NULL; // invalidates caches
            UpdateFontListInternal(PR_TRUE);
            return GetLangSupportEntry(aLang, aWithFonts);
        }
    }

    return entry;
}

FcLangResult
gfxFontconfigUtils::GetBestLangSupport(const FcChar8 *aLang)
{
    UpdateFontListInternal();

    LangSupportEntry *entry = GetLangSupportEntry(aLang, PR_FALSE);
    if (!entry)
        return FcLangEqual;

    return entry->mSupport;
}

const nsTArray< nsCountedRef<FcPattern> >&
gfxFontconfigUtils::GetFontsForLang(const FcChar8 *aLang)
{
    LangSupportEntry *entry = GetLangSupportEntry(aLang, PR_TRUE);
    if (!entry)
        return mEmptyPatternArray;

    return entry->mFonts;
}

PRBool
gfxFontNameList::Exists(nsAString& aName) {
    for (PRUint32 i = 0; i < Length(); i++) {
        if (aName.Equals(ElementAt(i)))
            return PR_TRUE;
    }
    return PR_FALSE;
}
