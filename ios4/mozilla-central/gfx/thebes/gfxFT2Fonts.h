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
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@mozilla.com>
 *   Masayuki Nakano <masayuki@d-toybox.com>
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

#ifndef GFX_FT2FONTS_H
#define GFX_FT2FONTS_H

#include "cairo.h"
#include "gfxTypes.h"
#include "gfxFont.h"
#include "gfxFT2FontBase.h"
#include "gfxContext.h"
#include "gfxFontUtils.h"
#include "gfxUserFontSet.h"

typedef struct FT_FaceRec_* FT_Face;

/**
 * FontFamily is a class that describes one of the fonts on the users system.  It holds
 * each FontEntry (maps more directly to a font face) which holds font type, charset info
 * and character map info.
 */
class FontEntry;
class FontFamily : public gfxFontFamily
{
public:
    FontFamily(const nsAString& aName) :
        gfxFontFamily(aName) { }

    FontEntry *FindFontEntry(const gfxFontStyle& aFontStyle);

protected:
    virtual PRBool FindWeightsForStyle(gfxFontEntry* aFontsForWeights[],
                                       PRBool anItalic, PRInt16 aStretch);
};

class FontEntry : public gfxFontEntry
{
public:
    FontEntry(const nsAString& aFaceName) :
        gfxFontEntry(aFaceName)
    {
        mFTFace = nsnull;
        mFontFace = nsnull;
        mFTFontIndex = 0;
    }

    ~FontEntry();

    const nsString& GetName() const {
        return Name();
    }

    static FontEntry* 
    CreateFontEntry(const gfxProxyFontEntry &aProxyEntry,
                    const PRUint8 *aFontData, PRUint32 aLength);

    static FontEntry* 
    CreateFontEntryFromFace(FT_Face aFace, const PRUint8 *aFontData = nsnull);
        // aFontData is NS_Malloc'ed data that aFace depends on, to be freed
        // after the face is destroyed; null if there is no such buffer

    virtual gfxFont *CreateFontInstance(const gfxFontStyle *aFontStyle, PRBool aNeedsBold);

    cairo_font_face_t *CairoFontFace();
    nsresult ReadCMAP();

    FT_Face mFTFace;
    cairo_font_face_t *mFontFace;

    nsCString mFilename;
    PRUint8 mFTFontIndex;
};


class gfxFT2Font : public gfxFT2FontBase {
public: // new functions
    gfxFT2Font(cairo_scaled_font_t *aCairoFont,
               FontEntry *aFontEntry,
               const gfxFontStyle *aFontStyle);
    virtual ~gfxFT2Font ();

    cairo_font_face_t *CairoFontFace();

    FontEntry *GetFontEntry();

    static already_AddRefed<gfxFT2Font>
    GetOrMakeFont(const nsAString& aName, const gfxFontStyle *aStyle);
    static already_AddRefed<gfxFT2Font>
    GetOrMakeFont(FontEntry *aFontEntry, const gfxFontStyle *aStyle);

    struct CachedGlyphData {
        CachedGlyphData()
            : glyphIndex(0xffffffffU) { }

        CachedGlyphData(PRUint32 gid)
            : glyphIndex(gid) { }

        PRUint32 glyphIndex;
        PRInt32 lsbDelta;
        PRInt32 rsbDelta;
        PRInt32 xAdvance;
    };

    const CachedGlyphData* GetGlyphDataForChar(PRUint32 ch) {
        CharGlyphMapEntryType *entry = mCharGlyphCache.PutEntry(ch);

        if (!entry)
            return nsnull;

        if (entry->mData.glyphIndex == 0xffffffffU) {
            // this is a new entry, fill it
            FillGlyphDataForChar(ch, &entry->mData);
        }

        return &entry->mData;
    }

protected:
    void FillGlyphDataForChar(PRUint32 ch, CachedGlyphData *gd);

    typedef nsBaseHashtableET<nsUint32HashKey, CachedGlyphData> CharGlyphMapEntryType;
    typedef nsTHashtable<CharGlyphMapEntryType> CharGlyphMap;
    CharGlyphMap mCharGlyphCache;
};

class THEBES_API gfxFT2FontGroup : public gfxFontGroup {
public: // new functions
    gfxFT2FontGroup (const nsAString& families,
                    const gfxFontStyle *aStyle,
                    gfxUserFontSet *aUserFontSet);
    virtual ~gfxFT2FontGroup ();

protected: // from gfxFontGroup
    virtual gfxTextRun *MakeTextRun(const PRUnichar *aString, 
                                    PRUint32 aLength,
                                    const Parameters *aParams, 
                                    PRUint32 aFlags);

    virtual gfxTextRun *MakeTextRun(const PRUint8 *aString, 
                                    PRUint32 aLength,
                                    const Parameters *aParams, 
                                    PRUint32 aFlags);

    virtual gfxFontGroup *Copy(const gfxFontStyle *aStyle);


protected: // new functions
    void InitTextRun(gfxTextRun *aTextRun);

    void CreateGlyphRunsFT(gfxTextRun *aTextRun);
    void AddRange(gfxTextRun *aTextRun, gfxFT2Font *font, const PRUnichar *str, PRUint32 offset, PRUint32 len);

    static PRBool FontCallback (const nsAString & fontName, 
                                const nsACString & genericName, 
                                void *closure);
    PRBool mEnableKerning;

    void GetPrefFonts(nsIAtom *aLangGroup,
                      nsTArray<nsRefPtr<gfxFontEntry> >& aFontEntryList);
    void GetCJKPrefFonts(nsTArray<nsRefPtr<gfxFontEntry> >& aFontEntryList);
    void FamilyListToArrayList(const nsString& aFamilies,
                               nsIAtom *aLangGroup,
                               nsTArray<nsRefPtr<gfxFontEntry> > *aFontEntryList);
    already_AddRefed<gfxFT2Font> WhichFontSupportsChar(const nsTArray<nsRefPtr<gfxFontEntry> >& aFontEntryList,
                                                       PRUint32 aCh);
    already_AddRefed<gfxFont> WhichPrefFontSupportsChar(PRUint32 aCh);
    already_AddRefed<gfxFont> WhichSystemFontSupportsChar(PRUint32 aCh);

    nsTArray<gfxTextRange> mRanges;
    nsString mString;
};

#endif /* GFX_FT2FONTS_H */

