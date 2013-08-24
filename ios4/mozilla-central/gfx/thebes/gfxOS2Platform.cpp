/* vim: set sw=4 sts=4 et cin: */
/* ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is OS/2 code in Thebes.
 *
 * The Initial Developer of the Original Code is
 * Peter Weilbacher <mozilla@Weilbacher.org>.
 * Portions created by the Initial Developer are Copyright (C) 2006-2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   author of code taken from gfxPlatformGtk:
 *     Masayuki Nakano <masayuki@d-toybox.com>
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

#include "gfxOS2Platform.h"
#include "gfxOS2Surface.h"
#include "gfxImageSurface.h"
#include "gfxOS2Fonts.h"
#include "nsTArray.h"

#include "gfxFontconfigUtils.h"
//#include <fontconfig/fontconfig.h>

/**********************************************************************
 * class gfxOS2Platform
 **********************************************************************/
gfxFontconfigUtils *gfxOS2Platform::sFontconfigUtils = nsnull;

gfxOS2Platform::gfxOS2Platform()
{
#ifdef DEBUG_thebes
    printf("gfxOS2Platform::gfxOS2Platform()\n");
#endif
    // this seems to be reasonably early in the process and only once,
    // so it's a good place to initialize OS/2 cairo stuff
    cairo_os2_init();
#ifdef DEBUG_thebes
    printf("  cairo_os2_init() was called\n");
#endif
    if (!sFontconfigUtils) {
        sFontconfigUtils = gfxFontconfigUtils::GetFontconfigUtils();
    }
}

gfxOS2Platform::~gfxOS2Platform()
{
#ifdef DEBUG_thebes
    printf("gfxOS2Platform::~gfxOS2Platform()\n");
#endif
    gfxFontconfigUtils::Shutdown();
    sFontconfigUtils = nsnull;

    // clean up OS/2 cairo stuff
    cairo_os2_fini();
#ifdef DEBUG_thebes
    printf("  cairo_os2_fini() was called\n");
#endif
}

already_AddRefed<gfxASurface>
gfxOS2Platform::CreateOffscreenSurface(const gfxIntSize& aSize,
                                       gfxASurface::gfxImageFormat aImageFormat)
{
#ifdef DEBUG_thebes_2
    printf("gfxOS2Platform::CreateOffscreenSurface(%d/%d, %d)\n",
           aSize.width, aSize.height, aImageFormat);
#endif
    gfxASurface *newSurface = nsnull;

    // we only ever seem to get aImageFormat=0 or ImageFormatARGB32 but
    // I don't really know if we need to differ between ARGB32 and RGB24 here
    if (aImageFormat == gfxASurface::ImageFormatARGB32 ||
        aImageFormat == gfxASurface::ImageFormatRGB24)
    {
        newSurface = new gfxOS2Surface(aSize, aImageFormat);
    } else if (aImageFormat == gfxASurface::ImageFormatA8 ||
               aImageFormat == gfxASurface::ImageFormatA1) {
        newSurface = new gfxImageSurface(aSize, aImageFormat);
    } else {
        return nsnull;
    }

    NS_IF_ADDREF(newSurface);
    return newSurface;
}

nsresult
gfxOS2Platform::GetFontList(nsIAtom *aLangGroup,
                            const nsACString& aGenericFamily,
                            nsTArray<nsString>& aListOfFonts)
{
#ifdef DEBUG_thebes
    const char *langgroup = "(null)";
    if (aLangGroup) {
        aLangGroup->GetUTF8String(&langgroup);
    }
    char *family = ToNewCString(aGenericFamily);
    printf("gfxOS2Platform::GetFontList(%s, %s, ..)\n",
           langgroup, family);
    free(family);
#endif
    return sFontconfigUtils->GetFontList(aLangGroup, aGenericFamily,
                                         aListOfFonts);
}

nsresult gfxOS2Platform::UpdateFontList()
{
#ifdef DEBUG_thebes
    printf("gfxOS2Platform::UpdateFontList()\n");
#endif
    mCodepointsWithNoFonts.reset();

    nsresult rv = sFontconfigUtils->UpdateFontList();

    // initialize ranges of characters for which system-wide font search should be skipped
    mCodepointsWithNoFonts.SetRange(0,0x1f);     // C0 controls
    mCodepointsWithNoFonts.SetRange(0x7f,0x9f);  // C1 controls
    return rv;
}

nsresult
gfxOS2Platform::ResolveFontName(const nsAString& aFontName,
                                FontResolverCallback aCallback,
                                void *aClosure, PRBool& aAborted)
{
#ifdef DEBUG_thebes
    char *fontname = ToNewCString(aFontName);
    printf("gfxOS2Platform::ResolveFontName(%s, ...)\n", fontname);
    free(fontname);
#endif
    return sFontconfigUtils->ResolveFontName(aFontName, aCallback, aClosure,
                                             aAborted);
}

nsresult
gfxOS2Platform::GetStandardFamilyName(const nsAString& aFontName, nsAString& aFamilyName)
{
    return sFontconfigUtils->GetStandardFamilyName(aFontName, aFamilyName);
}

gfxFontGroup *
gfxOS2Platform::CreateFontGroup(const nsAString &aFamilies,
                const gfxFontStyle *aStyle,
                gfxUserFontSet *aUserFontSet)
{
    return new gfxOS2FontGroup(aFamilies, aStyle, aUserFontSet);
}

already_AddRefed<gfxOS2Font>
gfxOS2Platform::FindFontForChar(PRUint32 aCh, gfxOS2Font *aFont)
{
#ifdef DEBUG_thebes
    printf("gfxOS2Platform::FindFontForChar(%d, ...)\n", aCh);
#endif

    // is codepoint with no matching font? return null immediately
    if (mCodepointsWithNoFonts.test(aCh)) {
        return nsnull;
    }

    // the following is not very clever but it's a quick fix to search all fonts
    // (one should instead cache the charmaps as done on Mac and Win)

    // just continue to append all fonts known to the system
    nsTArray<nsString> fontList;
    nsCAutoString generic;
    nsresult rv = GetFontList(aFont->GetStyle()->language, generic, fontList);
    if (NS_SUCCEEDED(rv)) {
        // start at 3 to skip over the generic entries
        for (PRUint32 i = 3; i < fontList.Length(); i++) {
#ifdef DEBUG_thebes
            printf("searching in entry i=%d (%s)\n",
                   i, NS_LossyConvertUTF16toASCII(fontList[i]).get());
#endif
            nsRefPtr<gfxOS2Font> font =
                gfxOS2Font::GetOrMakeFont(fontList[i], aFont->GetStyle());
            if (!font)
                continue;
            FT_Face face = cairo_ft_scaled_font_lock_face(font->CairoScaledFont());
            if (!face || !face->charmap) {
                if (face)
                    cairo_ft_scaled_font_unlock_face(font->CairoScaledFont());
                continue;
            }

            FT_UInt gid = FT_Get_Char_Index(face, aCh); // find the glyph id
            if (gid != 0) {
                // this is the font
                cairo_ft_scaled_font_unlock_face(font->CairoScaledFont());
                return font.forget();
            }
        }
    }

    // no match found, so add to the set of non-matching codepoints
    mCodepointsWithNoFonts.set(aCh);
    return nsnull;
}
