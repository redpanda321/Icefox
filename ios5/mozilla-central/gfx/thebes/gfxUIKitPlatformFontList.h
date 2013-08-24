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
 * The Original Code is Mozilla Corporation code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2006-2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Ted Mielczarek <ted.mielczarek@gmail.com>
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

#ifndef gfxUIKitPlatformFontList_H_
#define gfxUIKitPlatformFontList_H_

#include "nsDataHashtable.h"
#include "nsRefPtrHashtable.h"

#include "gfxPlatformFontList.h"
#include "gfxPlatform.h"

#include "nsUnicharUtils.h"
#include "nsTArray.h"

#include <CoreText/CoreText.h>

class gfxUIKitPlatformFontList;

// a single member of a font family (i.e. a single face, such as Times Italic)
class UIKitFontEntry : public gfxFontEntry
{
public:
    friend class gfxUIKitPlatformFontList;

    UIKitFontEntry(const nsAString& aPostscriptName, PRInt32 aWeight,
                   gfxFontFamily *aFamily, PRBool aIsStandardFace = PR_FALSE);
    ~UIKitFontEntry() {
        ::CFRelease(mPostscriptName);
    }

    nsresult ReadCMAP();

    CFStringRef GetPostscriptName() { return mPostscriptName; }

protected:
    // for use with data fonts
    UIKitFontEntry(const nsAString& aPostscriptName,
                   PRUint16 aWeight, PRUint16 aStretch, PRUint32 aItalicStyle,
                   gfxUserFontData *aUserFontData);

    virtual nsresult GetFontTable(PRUint32 aTableTag, FallibleTArray<PRUint8>& aBuffer);

    virtual gfxFont* CreateFontInstance(const gfxFontStyle *aFontStyle, bool aNeedsBold);

    CFStringRef mPostscriptName;
};

class gfxUIKitPlatformFontList : public gfxPlatformFontList {
public:
    static gfxUIKitPlatformFontList* PlatformFontList() {
        return static_cast<gfxUIKitPlatformFontList*>(sPlatformFontList);
    }

    static PRInt32 AppleWeightToCSSWeight(PRInt32 aAppleWeight);

    virtual gfxFontEntry* GetDefaultFont(const gfxFontStyle* aStyle, bool& aNeedsBold);

    virtual bool GetStandardFamilyName(const nsAString& aFontName, nsAString& aFamilyName);

    virtual gfxFontEntry* LookupLocalFont(const gfxProxyFontEntry *aProxyEntry,
                                          const nsAString& aFontName);

    virtual gfxFontEntry* MakePlatformFont(const gfxProxyFontEntry *aProxyEntry,
                                           const PRUint8 *aFontData, PRUint32 aLength);

    void ClearPrefFonts() { mPrefFonts.Clear(); }

private:
    friend class gfxPlatformMac;

    gfxUIKitPlatformFontList();

    // initialize font lists
    virtual nsresult InitFontList();

    // special case font faces treated as font families (set via prefs)
    void InitSingleFaceList();

    // eliminate faces which have the same ATS font reference
    void EliminateDuplicateFaces(const nsAString& aFamilyName);
};

#endif /* gfxUIKitPlatformFontList_H_ */
