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
 *   Vladimir Vukicevic <vladimir@pobox.com>
 *   Jonathan Kew <jfkthame@gmail.com>
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

#import <UIKit/UIFont.h>
#import <UIKit/UIInterface.h>

#include "nsSystemFontsUIKit.h"


nsSystemFontsUIKit::nsSystemFontsUIKit()
{
}

// copied from gfxQuartzFontCache.mm, maybe should go in a Cocoa utils file somewhere
static void GetStringForNSString(const NSString *aSrc, nsAString& aDest)
{
    aDest.SetLength([aSrc length]);
    [aSrc getCharacters:aDest.BeginWriting()];
}

nsresult
nsSystemFontsUIKit::GetSystemFont(nsSystemFontID aID, nsString *aFontName,
                                gfxFontStyle *aFontStyle) const
{
    // hack for now
    if (aID == eSystemFont_Window ||
        aID == eSystemFont_Document)
    {
        aFontStyle->style       = FONT_STYLE_NORMAL;
        aFontStyle->weight      = FONT_WEIGHT_NORMAL;
        aFontStyle->stretch     = NS_FONT_STRETCH_NORMAL;

        aFontName->AssignLiteral("sans-serif");
        aFontStyle->size = 14;
        aFontStyle->systemFont = PR_TRUE;

        return NS_OK;
    }

    UIFont *font = nsnull;
    switch (aID) {
        // css2
        case eSystemFont_Caption:
            font = [UIFont systemFontOfSize:0.0];
            break;
        case eSystemFont_Icon: // used in urlbar; tried labelFont, but too small
            font = [UIFont systemFontOfSize:0.0];
            break;
        case eSystemFont_Menu:
            font = [UIFont systemFontOfSize:0.0];
            break;
        case eSystemFont_MessageBox:
            font = [UIFont systemFontOfSize:[UIFont smallSystemFontSize]];
            break;
        case eSystemFont_SmallCaption:
            font = [UIFont boldSystemFontOfSize:[UIFont smallSystemFontSize]];
            break;
        case eSystemFont_StatusBar:
            font = [UIFont systemFontOfSize:[UIFont smallSystemFontSize]];
            break;
        // css3
        //case eSystemFont_Window:     = 'sans-serif'
        //case eSystemFont_Document:   = 'sans-serif'
        case eSystemFont_Workspace:
            font = [UIFont systemFontOfSize:0.0];
            break;
        case eSystemFont_Desktop:
            font = [UIFont systemFontOfSize:0.0];
            break;
        case eSystemFont_Info:
            font = [UIFont systemFontOfSize:0.0];
            break;
        case eSystemFont_Dialog:
            font = [UIFont systemFontOfSize:0.0];
            break;
        case eSystemFont_Button:
            font = [UIFont systemFontOfSize:[UIFont buttonFontSize]];
            break;
        case eSystemFont_PullDownMenu:
            font = [UIFont systemFontOfSize:0.0];
            break;
        case eSystemFont_List:
            font = [UIFont systemFontOfSize:[UIFont smallSystemFontSize]];
            break;
        case eSystemFont_Field:
            font = [UIFont systemFontOfSize:[UIFont smallSystemFontSize]];
            break;
        // moz
        case eSystemFont_Tooltips:
            font = [UIFont systemFontOfSize:0.0];
            break;
        case eSystemFont_Widget:
            font = [UIFont systemFontOfSize:[UIFont smallSystemFontSize]];
            break;
        default:
            // should never hit this
            return NS_ERROR_FAILURE;
    }

    NS_ASSERTION(font != nsnull, "failed to find a system font!");

    GetStringForNSString([font familyName], *aFontName);
    aFontStyle->size = [font pointSize];
    /*
    UIFontSymbolicTraits traits = [[font fontDescriptor] symbolicTraits];
    if (traits & UIFontBoldTrait)
        aFontStyle->weight = FONT_WEIGHT_BOLD;
    if (traits & UIFontItalicTrait)
        aFontStyle->style = FONT_STYLE_ITALIC;
    aFontStyle->stretch =
        (traits & UIFontExpandedTrait) ?
            NS_FONT_STRETCH_EXPANDED : (traits & UIFontCondensedTrait) ?
                NS_FONT_STRETCH_CONDENSED : NS_FONT_STRETCH_NORMAL;
    */
    aFontStyle->systemFont = PR_TRUE;

    return NS_OK;
}
