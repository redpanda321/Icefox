/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * The Original Code is Android port
 *
 * The Initial Developer of the Original Code is mozilla.org.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@pobox.com>
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

#include "nsIDeviceContext.h"
#include "nsIRenderingContext.h"

#include "nsSystemFontsAndroid.h"

#include "gfxPlatform.h"

#define DEFAULT_FONT  "Droid Sans"

nsSystemFontsAndroid::nsSystemFontsAndroid()
  : mDefaultFontName(NS_LITERAL_STRING(DEFAULT_FONT))
  , mButtonFontName(NS_LITERAL_STRING(DEFAULT_FONT))
  , mFieldFontName(NS_LITERAL_STRING(DEFAULT_FONT))
  , mMenuFontName(NS_LITERAL_STRING(DEFAULT_FONT))
{
}

nsSystemFontsAndroid::~nsSystemFontsAndroid()
{
}

nsresult
nsSystemFontsAndroid::GetSystemFontInfo(const char *aClassName, nsString *aFontName,
                                        gfxFontStyle *aFontStyle) const
{
    aFontStyle->style = FONT_STYLE_NORMAL;
    aFontStyle->systemFont = PR_TRUE;
    *aFontName = NS_LITERAL_STRING("Droid Sans");
    aFontStyle->weight = 400;
    aFontStyle->stretch = NS_FONT_STRETCH_NORMAL;
    aFontStyle->size = 9.0 * 96.0f / 72.0f;
    return NS_OK;
}


nsresult
nsSystemFontsAndroid::GetSystemFont(nsSystemFontID anID, nsString *aFontName,
                                    gfxFontStyle *aFontStyle) const
{
    switch (anID) {
    case eSystemFont_Menu:         // css2
    case eSystemFont_PullDownMenu: // css3
        *aFontName = mMenuFontName;
        *aFontStyle = mMenuFontStyle;
        break;

    case eSystemFont_Field:        // css3
    case eSystemFont_List:         // css3
        *aFontName = mFieldFontName;
        *aFontStyle = mFieldFontStyle;
        break;

    case eSystemFont_Button:       // css3
        *aFontName = mButtonFontName;
        *aFontStyle = mButtonFontStyle;
        break;

    case eSystemFont_Caption:      // css2
    case eSystemFont_Icon:         // css2
    case eSystemFont_MessageBox:   // css2
    case eSystemFont_SmallCaption: // css2
    case eSystemFont_StatusBar:    // css2
    case eSystemFont_Window:       // css3
    case eSystemFont_Document:     // css3
    case eSystemFont_Workspace:    // css3
    case eSystemFont_Desktop:      // css3
    case eSystemFont_Info:         // css3
    case eSystemFont_Dialog:       // css3
    case eSystemFont_Tooltips:     // moz
    case eSystemFont_Widget:       // moz
        *aFontName = mDefaultFontName;
        *aFontStyle = mDefaultFontStyle;
        break;
    }

    return NS_OK;
}

