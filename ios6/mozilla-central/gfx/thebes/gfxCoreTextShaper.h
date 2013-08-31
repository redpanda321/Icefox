/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_CORETEXTSHAPER_H
#define GFX_CORETEXTSHAPER_H

#include "gfxTypes.h"
#include "gfxFont.h"
#include "gfxFontUtils.h"
#include "gfxPlatform.h"

#ifdef MOZ_WIDGET_COCOA
#include "gfxMacPlatformFontList.h"
#include <Carbon/Carbon.h>

class gfxMacFont;
typedef gfxMacFont gfxCTFont;
#else
#include "gfxUIKitPlatformFontList.h"
#include <CoreText/CoreText.h>

class gfxUIKitFont;
typedef gfxUIKitFont gfxCTFont;
#endif

class gfxCoreTextShaper : public gfxFontShaper {
public:
    gfxCoreTextShaper(gfxUIKitFont *aFont);

    virtual ~gfxCoreTextShaper();

    virtual bool ShapeWord(gfxContext *aContext,
                           gfxShapedWord *aShapedWord,
                           const PRUnichar *aText);

    // clean up static objects that may have been cached
    static void Shutdown();

protected:
    CTFontRef mCTFont;
    CFDictionaryRef mAttributesDict;

    nsresult SetGlyphsFromRun(gfxShapedWord *aShapedWord,
                              CTRunRef aCTRun,
                              int32_t aStringOffset);

#ifdef MOZ_WIDGET_COCOA
    CTFontRef CreateCTFontWithDisabledLigatures(CGFloat aSize);
#endif

    static void CreateDefaultFeaturesDescriptor();

    static CTFontDescriptorRef GetDefaultFeaturesDescriptor() {
        if (sDefaultFeaturesDescriptor == NULL) {
            CreateDefaultFeaturesDescriptor();
        }
        return sDefaultFeaturesDescriptor;
    }

    // cached font descriptor, created the first time it's needed
    static CTFontDescriptorRef    sDefaultFeaturesDescriptor;

    // cached descriptor for adding disable-ligatures setting to a font
    static CTFontDescriptorRef    sDisableLigaturesDescriptor;
};

#endif /* GFX_CORETEXTSHAPER_H */
