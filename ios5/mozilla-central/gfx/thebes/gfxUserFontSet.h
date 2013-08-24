/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_USER_FONT_SET_H
#define GFX_USER_FONT_SET_H

#include "gfxTypes.h"
#include "gfxFont.h"
#include "gfxFontUtils.h"
#include "nsRefPtrHashtable.h"
#include "nsAutoPtr.h"
#include "nsCOMPtr.h"
#include "nsIURI.h"
#include "nsIFile.h"
#include "nsISupportsImpl.h"
#include "nsIScriptError.h"

class nsIURI;
class gfxMixedFontFamily;
class nsFontFaceLoader;

// parsed CSS @font-face rule information
// lifetime: from when @font-face rule processed until font is loaded
struct gfxFontFaceSrc {
    bool                   mIsLocal;       // url or local

    // if url, whether to use the origin principal or not
    bool                   mUseOriginPrincipal;

    // format hint flags, union of all possible formats
    // (e.g. TrueType, EOT, SVG, etc.)
    // see FLAG_FORMAT_* enum values below
    PRUint32               mFormatFlags;

    nsString               mLocalName;     // full font name if local
    nsCOMPtr<nsIURI>       mURI;           // uri if url 
    nsCOMPtr<nsIURI>       mReferrer;      // referrer url if url
    nsCOMPtr<nsISupports>  mOriginPrincipal; // principal if url 
    
};

// Subclassed to store platform-specific code cleaned out when font entry is
// deleted.
// Lifetime: from when platform font is created until it is deactivated.
// If the platform does not need to add any platform-specific code/data here,
// then the gfxUserFontSet will allocate a base gfxUserFontData and attach
// to the entry to track the basic user font info fields here.
class gfxUserFontData {
public:
    gfxUserFontData()
        : mSrcIndex(0), mFormat(0), mMetaOrigLen(0)
    { }
    virtual ~gfxUserFontData() { }

    nsTArray<PRUint8> mMetadata;  // woff metadata block (compressed), if any
    nsCOMPtr<nsIURI>  mURI;       // URI of the source, if it was url()
    nsString          mLocalName; // font name used for the source, if local()
    nsString          mRealName;  // original fullname from the font resource
    PRUint32          mSrcIndex;  // index in the rule's source list
    PRUint32          mFormat;    // format hint for the source used, if any
    PRUint32          mMetaOrigLen; // length needed to decompress metadata
};

// initially contains a set of proxy font entry objects, replaced with
// platform/user fonts as downloaded

class gfxMixedFontFamily : public gfxFontFamily {
public:
    friend class gfxUserFontSet;

    gfxMixedFontFamily(const nsAString& aName)
        : gfxFontFamily(aName) { }

    virtual ~gfxMixedFontFamily() { }

    void AddFontEntry(gfxFontEntry *aFontEntry) {
        nsRefPtr<gfxFontEntry> fe = aFontEntry;
        mAvailableFonts.AppendElement(fe);
        aFontEntry->SetFamily(this);
        ResetCharacterMap();
    }

    void ReplaceFontEntry(gfxFontEntry *aOldFontEntry,
                          gfxFontEntry *aNewFontEntry) {
        PRUint32 numFonts = mAvailableFonts.Length();
        for (PRUint32 i = 0; i < numFonts; i++) {
            gfxFontEntry *fe = mAvailableFonts[i];
            if (fe == aOldFontEntry) {
                aOldFontEntry->SetFamily(nsnull);
                // note that this may delete aOldFontEntry, if there's no
                // other reference to it except from its family
                mAvailableFonts[i] = aNewFontEntry;
                aNewFontEntry->SetFamily(this);
                break;
            }
        }
        ResetCharacterMap();
    }

    void RemoveFontEntry(gfxFontEntry *aFontEntry) {
        PRUint32 numFonts = mAvailableFonts.Length();
        for (PRUint32 i = 0; i < numFonts; i++) {
            gfxFontEntry *fe = mAvailableFonts[i];
            if (fe == aFontEntry) {
                aFontEntry->SetFamily(nsnull);
                mAvailableFonts.RemoveElementAt(i);
                break;
            }
        }
        ResetCharacterMap();
    }

    // clear family pointer for all entries and remove them from the family;
    // we need to do this explicitly before inserting the entries into a new
    // family, in case the old one is not actually deleted until later
    void DetachFontEntries() {
        PRUint32 i = mAvailableFonts.Length();
        while (i--) {
            gfxFontEntry *fe = mAvailableFonts[i];
            if (fe) {
                fe->SetFamily(nsnull);
            }
        }
        mAvailableFonts.Clear();
    }

    // temp method to determine if all proxies are loaded
    bool AllLoaded() 
    {
        PRUint32 numFonts = mAvailableFonts.Length();
        for (PRUint32 i = 0; i < numFonts; i++) {
            gfxFontEntry *fe = mAvailableFonts[i];
            if (fe->mIsProxy)
                return false;
        }
        return true;
    }
};

class gfxProxyFontEntry;

class THEBES_API gfxUserFontSet {

public:

    NS_INLINE_DECL_REFCOUNTING(gfxUserFontSet)

    gfxUserFontSet();
    virtual ~gfxUserFontSet();

    enum {
        // no flags ==> no hint set
        // unknown ==> unknown format hint set
        FLAG_FORMAT_UNKNOWN        = 1,
        FLAG_FORMAT_OPENTYPE       = 1 << 1,
        FLAG_FORMAT_TRUETYPE       = 1 << 2,
        FLAG_FORMAT_TRUETYPE_AAT   = 1 << 3,
        FLAG_FORMAT_EOT            = 1 << 4,
        FLAG_FORMAT_SVG            = 1 << 5,
        FLAG_FORMAT_WOFF           = 1 << 6,

        // mask of all unused bits, update when adding new formats
        FLAG_FORMAT_NOT_USED       = ~((1 << 7)-1)
    };

    enum LoadStatus {
        STATUS_LOADING = 0,
        STATUS_LOADED,
        STATUS_FORMAT_NOT_SUPPORTED,
        STATUS_ERROR,
        STATUS_END_OF_LIST
    };


    // add in a font face
    // weight, stretch - 0 == unknown, [1, 9] otherwise
    // italic style = constants in gfxFontConstants.h, e.g. NS_FONT_STYLE_NORMAL
    // TODO: support for unicode ranges not yet implemented
    gfxFontEntry *AddFontFace(const nsAString& aFamilyName,
                              const nsTArray<gfxFontFaceSrc>& aFontFaceSrcList,
                              PRUint32 aWeight,
                              PRUint32 aStretch,
                              PRUint32 aItalicStyle,
                              const nsTArray<gfxFontFeature>& aFeatureSettings,
                              const nsString& aLanguageOverride,
                              gfxSparseBitSet *aUnicodeRanges = nsnull);

    // add in a font face for which we have the gfxFontEntry already
    void AddFontFace(const nsAString& aFamilyName, gfxFontEntry* aFontEntry);

    // Whether there is a face with this family name
    bool HasFamily(const nsAString& aFamilyName) const
    {
        return GetFamily(aFamilyName) != nsnull;
    }

    // lookup a font entry for a given style, returns null if not loaded
    gfxFontEntry *FindFontEntry(const nsAString& aName,
                                const gfxFontStyle& aFontStyle,
                                bool& aFoundFamily,
                                bool& aNeedsBold,
                                bool& aWaitForUserFont);
                                
    // initialize the process that loads external font data, which upon 
    // completion will call OnLoadComplete method
    virtual nsresult StartLoad(gfxProxyFontEntry *aProxy, 
                               const gfxFontFaceSrc *aFontFaceSrc) = 0;

    // when download has been completed, pass back data here
    // aDownloadStatus == NS_OK ==> download succeeded, error otherwise
    // returns true if platform font creation sucessful (or local()
    // reference was next in line)
    // Ownership of aFontData is passed in here; the font set must
    // ensure that it is eventually deleted with NS_Free().
    bool OnLoadComplete(gfxProxyFontEntry *aProxy,
                          const PRUint8 *aFontData, PRUint32 aLength,
                          nsresult aDownloadStatus);

    // Replace a proxy with a real fontEntry; this is implemented in
    // nsUserFontSet in order to keep track of the entry corresponding
    // to each @font-face rule.
    virtual void ReplaceFontEntry(gfxProxyFontEntry *aProxy,
                                  gfxFontEntry *aFontEntry) = 0;

    // generation - each time a face is loaded, generation is
    // incremented so that the change can be recognized 
    PRUint64 GetGeneration() { return mGeneration; }

    // increment the generation on font load
    void IncrementGeneration();

protected:
    // for a given proxy font entry, attempt to load the next resource
    // in the src list
    LoadStatus LoadNext(gfxProxyFontEntry *aProxyEntry);

    // helper method for creating a platform font
    // returns font entry if platform font creation successful
    // Ownership of aFontData is passed in here; the font set must
    // ensure that it is eventually deleted with NS_Free().
    gfxFontEntry* LoadFont(gfxProxyFontEntry *aProxy,
                           const PRUint8 *aFontData, PRUint32 &aLength);

    // parse data for a data URL
    virtual nsresult SyncLoadFontData(gfxProxyFontEntry *aFontToLoad,
                                      const gfxFontFaceSrc *aFontFaceSrc,
                                      PRUint8* &aBuffer,
                                      PRUint32 &aBufferLength)
    {
        // implemented in nsUserFontSet
        return NS_ERROR_NOT_IMPLEMENTED;
    }

    gfxMixedFontFamily *GetFamily(const nsAString& aName) const;

    // report a problem of some kind (implemented in nsUserFontSet)
    virtual nsresult LogMessage(gfxProxyFontEntry *aProxy,
                                const char *aMessage,
                                PRUint32 aFlags = nsIScriptError::errorFlag,
                                nsresult aStatus = 0) = 0;

    const PRUint8* SanitizeOpenTypeData(gfxProxyFontEntry *aProxy,
                                        const PRUint8* aData,
                                        PRUint32 aLength,
                                        PRUint32& aSaneLength,
                                        bool aIsCompressed);

#ifdef MOZ_OTS_REPORT_ERRORS
    static bool OTSMessage(void *aUserData, const char *format, ...);
#endif

    // font families defined by @font-face rules
    nsRefPtrHashtable<nsStringHashKey, gfxMixedFontFamily> mFontFamilies;

    PRUint64        mGeneration;

    static PRLogModuleInfo *sUserFontsLog;

private:
    static void CopyWOFFMetadata(const PRUint8* aFontData,
                                 PRUint32 aLength,
                                 nsTArray<PRUint8>* aMetadata,
                                 PRUint32* aMetaOrigLen);
};

// acts a placeholder until the real font is downloaded

class gfxProxyFontEntry : public gfxFontEntry {
    friend class gfxUserFontSet;

public:
    gfxProxyFontEntry(const nsTArray<gfxFontFaceSrc>& aFontFaceSrcList,
                      gfxMixedFontFamily *aFamily,
                      PRUint32 aWeight,
                      PRUint32 aStretch,
                      PRUint32 aItalicStyle,
                      const nsTArray<gfxFontFeature>& aFeatureSettings,
                      PRUint32 aLanguageOverride,
                      gfxSparseBitSet *aUnicodeRanges);

    virtual ~gfxProxyFontEntry();

    virtual gfxFont *CreateFontInstance(const gfxFontStyle *aFontStyle, bool aNeedsBold);

    // note that code depends on the ordering of these values!
    enum LoadingState {
        NOT_LOADING = 0,     // not started to load any font resources yet
        LOADING_STARTED,     // loading has started; hide fallback font
        LOADING_ALMOST_DONE, // timeout happened but we're nearly done,
                             // so keep hiding fallback font
        LOADING_SLOWLY,      // timeout happened and we're not nearly done,
                             // so use the fallback font
        LOADING_FAILED       // failed to load any source: use fallback
    };
    LoadingState             mLoadingState;
    bool                     mUnsupportedFormat;

    nsTArray<gfxFontFaceSrc> mSrcList;
    PRUint32                 mSrcIndex; // index of loading src item
    nsFontFaceLoader        *mLoader; // current loader for this entry, if any
};


#endif /* GFX_USER_FONT_SET_H */
