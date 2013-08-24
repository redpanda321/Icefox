/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ted Mielczarek <ted.mielczarek@gmail.com>
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

#import <UIKit/UIColor.h>
#import <UIKit/UIInterface.h>

#include "nsLookAndFeel.h"

nsLookAndFeel::nsLookAndFeel()
    : nsXPLookAndFeel()
{
}

nsLookAndFeel::~nsLookAndFeel()
{
}

static nscolor GetColorFromUIColor(UIColor* aColor)
{
    CGColorRef cgColor = [aColor CGColor];
    CGColorSpaceModel model = CGColorSpaceGetModel(CGColorGetColorSpace(cgColor));
    const CGFloat* components = CGColorGetComponents(cgColor);
    if (model == kCGColorSpaceModelRGB) {
        return NS_RGB((unsigned int)(components[0] * 255.0),
                      (unsigned int)(components[1] * 255.0),
                      (unsigned int)(components[2] * 255.0));
    }
    else if (model == kCGColorSpaceModelMonochrome) {
        unsigned int val = (unsigned int)(components[0] * 255.0);
        return NS_RGBA(val, val, val,
                       (unsigned int)(components[1] * 255.0));
    }
    NS_NOTREACHED("Unhandled color space!");
    return 0;
}

nsresult
nsLookAndFeel::NativeGetColor(const nsColorID aID, nscolor &aColor)
{
  nsresult res = NS_OK;
  
  switch (aID) {
    case eColor_WindowBackground:
      aColor = NS_RGB(0xff,0xff,0xff);
      break;
    case eColor_WindowForeground:
      aColor = NS_RGB(0x00,0x00,0x00);        
      break;
    case eColor_WidgetBackground:
      aColor = NS_RGB(0xdd,0xdd,0xdd);
      break;
    case eColor_WidgetForeground:
      aColor = NS_RGB(0x00,0x00,0x00);        
      break;
    case eColor_WidgetSelectBackground:
      aColor = NS_RGB(0x80,0x80,0x80);
      break;
    case eColor_WidgetSelectForeground:
      aColor = NS_RGB(0x00,0x00,0x80);
      break;
    case eColor_Widget3DHighlight:
      aColor = NS_RGB(0xa0,0xa0,0xa0);
      break;
    case eColor_Widget3DShadow:
      aColor = NS_RGB(0x40,0x40,0x40);
      break;
    case eColor_TextBackground:
      aColor = NS_RGB(0xff,0xff,0xff);
      break;
    case eColor_TextForeground:
      aColor = NS_RGB(0x00,0x00,0x00);
      break;
    case eColor_TextSelectBackground:
    case eColor_highlight: // CSS2 color
      aColor = NS_RGB(0xaa,0xaa,0xaa);
      break;
    case eColor__moz_menuhover:
      aColor = NS_RGB(0xee,0xee,0xee);
      break;      
    case eColor_TextSelectForeground:
    case eColor_highlighttext:  // CSS2 color
    case eColor__moz_menuhovertext:
      GetColor(eColor_TextSelectBackground, aColor);
      if (aColor == 0x000000)
        aColor = NS_RGB(0xff,0xff,0xff);
      else
        aColor = NS_DONT_CHANGE_COLOR;
      break;
    case eColor_IMESelectedRawTextBackground:
    case eColor_IMESelectedConvertedTextBackground:
    case eColor_IMERawInputBackground:
    case eColor_IMEConvertedTextBackground:
      aColor = NS_TRANSPARENT;
      break;
    case eColor_IMESelectedRawTextForeground:
    case eColor_IMESelectedConvertedTextForeground:
    case eColor_IMERawInputForeground:
    case eColor_IMEConvertedTextForeground:
      aColor = NS_SAME_AS_FOREGROUND_COLOR;
      break;
    case eColor_IMERawInputUnderline:
    case eColor_IMEConvertedTextUnderline:
      aColor = NS_40PERCENT_FOREGROUND_COLOR;
      break;
    case eColor_IMESelectedRawTextUnderline:
    case eColor_IMESelectedConvertedTextUnderline:
      aColor = NS_SAME_AS_FOREGROUND_COLOR;
      break;
    case eColor_SpellCheckerUnderline:
      aColor = NS_RGB(0xff, 0, 0);
      break;

    //
    // css2 system colors http://www.w3.org/TR/REC-CSS2/ui.html#system-colors
    //
    case eColor_buttontext:
    case eColor__moz_buttonhovertext:
    case eColor_captiontext:
    case eColor_menutext:
    case eColor_infotext:
    case eColor__moz_menubartext:
    case eColor_windowtext:
      aColor = GetColorFromUIColor([UIColor darkTextColor]);
      break;
    case eColor_activecaption:
      aColor = NS_RGB(0xff,0xff,0xff);
      break;
    case eColor_activeborder:
      aColor = NS_RGB(0x00,0x00,0x00);
      break;
     case eColor_appworkspace:
      aColor = NS_RGB(0xFF,0xFF,0xFF);
      break;
    case eColor_background:
      aColor = NS_RGB(0x63,0x63,0xCE);
      break;
    case eColor_buttonface:
    case eColor__moz_buttonhoverface:
      aColor = NS_RGB(0xF0,0xF0,0xF0);
      break;
    case eColor_buttonhighlight:
      aColor = NS_RGB(0xFF,0xFF,0xFF);
      break;
    case eColor_buttonshadow:
      aColor = NS_RGB(0xDC,0xDC,0xDC);
      break;
    case eColor_graytext:
      aColor = NS_RGB(0x44,0x44,0x44);
      break;
    case eColor_inactiveborder:
      aColor = NS_RGB(0xff,0xff,0xff);
      break;
    case eColor_inactivecaption:
      aColor = NS_RGB(0xaa,0xaa,0xaa);
      break;
    case eColor_inactivecaptiontext:
      aColor = NS_RGB(0x45,0x45,0x45);
      break;
    case eColor_scrollbar:
      aColor = NS_RGB(0,0,0); //XXX
      break;
    case eColor_threeddarkshadow:
      aColor = NS_RGB(0xDC,0xDC,0xDC);
      break;
    case eColor_threedshadow:
      aColor = NS_RGB(0xE0,0xE0,0xE0);
      break;
    case eColor_threedface:
      aColor = NS_RGB(0xF0,0xF0,0xF0);
      break;
    case eColor_threedhighlight:
      aColor = NS_RGB(0xff,0xff,0xff);
      break;
    case eColor_threedlightshadow:
      aColor = NS_RGB(0xDA,0xDA,0xDA);
      break;
    case eColor_menu:
      aColor = NS_RGB(0xff,0xff,0xff);
      break;
    case eColor_infobackground:
      aColor = NS_RGB(0xFF,0xFF,0xC7);
      break;
    case eColor_windowframe:
      aColor = NS_RGB(0xaa,0xaa,0xaa);
      break;
    case eColor_window:
    case eColor__moz_field:
    case eColor__moz_combobox:
      aColor = NS_RGB(0xff,0xff,0xff);
      break;
    case eColor__moz_fieldtext:
    case eColor__moz_comboboxtext:
      aColor = GetColorFromUIColor([UIColor darkTextColor]);
      break;
    case eColor__moz_dialog:
      aColor = NS_RGB(0xaa,0xaa,0xaa);
      break;
    case eColor__moz_dialogtext:
    case eColor__moz_cellhighlighttext:
    case eColor__moz_html_cellhighlighttext:
      aColor = GetColorFromUIColor([UIColor darkTextColor]);
      break;
    case eColor__moz_dragtargetzone:
    case eColor__moz_mac_chrome_active:
    case eColor__moz_mac_chrome_inactive:
      aColor = NS_RGB(0xaa,0xaa,0xaa);
      break;
    case eColor__moz_mac_focusring:
      aColor = NS_RGB(0x3F,0x98,0xDD);
      break;
    case eColor__moz_mac_menushadow:
      aColor = NS_RGB(0xA3,0xA3,0xA3);
      break;          
    case eColor__moz_mac_menutextdisable:
      aColor = NS_RGB(0x88,0x88,0x88);
      break;      
    case eColor__moz_mac_menutextselect:
      aColor = NS_RGB(0xaa,0xaa,0xaa);
      break;      
    case eColor__moz_mac_disabledtoolbartext:
      aColor = NS_RGB(0x3F,0x3F,0x3F);
      break;
    case eColor__moz_mac_menuselect:
      aColor = NS_RGB(0xaa,0xaa,0xaa);
      break;
    case eColor__moz_buttondefault:
      aColor = NS_RGB(0xDC,0xDC,0xDC);
      break;
    case eColor__moz_mac_alternateprimaryhighlight:
      aColor = NS_RGB(0xaa,0xaa,0xaa);
      break;
    case eColor__moz_cellhighlight:
    case eColor__moz_html_cellhighlight:
    case eColor__moz_mac_secondaryhighlight:
      // For inactive list selection
      aColor = NS_RGB(0xaa,0xaa,0xaa);
      break;
    case eColor__moz_eventreerow:
      // Background color of even list rows.
      aColor = NS_RGB(0xff,0xff,0xff);
      break;
    case eColor__moz_oddtreerow:
      // Background color of odd list rows.
      aColor = NS_TRANSPARENT;
      break;
    case eColor__moz_nativehyperlinktext:
      // There appears to be no available system defined color. HARDCODING to the appropriate color.
      aColor = NS_RGB(0x14,0x4F,0xAE);
      break;
    default:
      NS_WARNING("Someone asked nsILookAndFeel for a color I don't know about");
      aColor = NS_RGB(0xff,0xff,0xff);
      res = NS_ERROR_FAILURE;
      break;
    }
  
  return res;
}

NS_IMETHODIMP
nsLookAndFeel::GetMetric(const nsMetricID aID, PRInt32 &aMetric)
{
  nsresult res = nsXPLookAndFeel::GetMetric(aID, aMetric);
  if (NS_SUCCEEDED(res))
    return res;
  res = NS_OK;
  
  switch (aID) {
    case eMetric_WindowTitleHeight:
      aMetric = 0;
      break;
    case eMetric_WindowBorderWidth:
      aMetric = 4;
      break;
    case eMetric_WindowBorderHeight:
      aMetric = 4;
      break;
    case eMetric_Widget3DBorder:
      aMetric = 4;
      break;
    case eMetric_TextFieldHeight:
      aMetric = 16;
      break;
    case eMetric_TextFieldBorder:
      aMetric = 2;
      break;
    case eMetric_ButtonHorizontalInsidePaddingNavQuirks:
      aMetric = 20;
      break;
    case eMetric_ButtonHorizontalInsidePaddingOffsetNavQuirks:
      aMetric = 0;
      break;
    case eMetric_CheckboxSize:
      aMetric = 14;
      break;
    case eMetric_RadioboxSize:
      aMetric = 14;
      break;
    case eMetric_TextHorizontalInsideMinimumPadding:
      aMetric = 4;
      break;
    case eMetric_TextVerticalInsidePadding:
      aMetric = 4;
      break;
    case eMetric_TextShouldUseVerticalInsidePadding:
      aMetric = 1;
      break;
    case eMetric_TextShouldUseHorizontalInsideMinimumPadding:
      aMetric = 1;
      break;
    case eMetric_ListShouldUseHorizontalInsideMinimumPadding:
      aMetric = 0;
      break;
    case eMetric_ListHorizontalInsideMinimumPadding:
      aMetric = 4;
      break;
    case eMetric_ListShouldUseVerticalInsidePadding:
      aMetric = 1;
      break;
    case eMetric_ListVerticalInsidePadding:
      aMetric = 3;
      break;
    case eMetric_CaretBlinkTime:
      aMetric = 567;
      break;
    case eMetric_CaretWidth:
      aMetric = 1;
      break;
    case eMetric_ShowCaretDuringSelection:
      aMetric = 0;
      break;
    case eMetric_SelectTextfieldsOnKeyFocus:
      // Select textfield content when focused by kbd
      // used by nsEventStateManager::sTextfieldSelectModel
      aMetric = 1;
      break;
    case eMetric_SubmenuDelay:
      aMetric = 200;
      break;
    case eMetric_MenusCanOverlapOSBar:
      // xul popups are not allowed to overlap the menubar.
      aMetric = 0;
      break;
    case eMetric_SkipNavigatingDisabledMenuItem:
      aMetric = 1;
      break;
    case eMetric_DragThresholdX:
    case eMetric_DragThresholdY:
      aMetric = 4;
      break;
    case eMetric_ScrollArrowStyle:
      aMetric = eMetric_ScrollArrowStyleSingle;
      break;
    case eMetric_ScrollSliderStyle:
      aMetric = eMetric_ScrollThumbStyleProportional;
      break;
    case eMetric_TreeOpenDelay:
      aMetric = 1000;
      break;
    case eMetric_TreeCloseDelay:
      aMetric = 1000;
      break;
    case eMetric_TreeLazyScrollDelay:
      aMetric = 150;
      break;
    case eMetric_TreeScrollDelay:
      aMetric = 100;
      break;
    case eMetric_TreeScrollLinesMax:
      aMetric = 3;
      break;
    case eMetric_DWMCompositor:
    case eMetric_WindowsClassic:
    case eMetric_WindowsDefaultTheme:
    case eMetric_TouchEnabled:
    case eMetric_MaemoClassic:
      aMetric = 0;
      res = NS_ERROR_NOT_IMPLEMENTED;
      break;
    case eMetric_MacGraphiteTheme:
      aMetric = 0;
      break;
    case eMetric_TabFocusModel:
      aMetric = 1;    // default to just textboxes
      break;
    case eMetric_ScrollToClick:
      aMetric = 0;
      break;
    case eMetric_ChosenMenuItemsShouldBlink:
      aMetric = 1;
      break;
    case eMetric_IMERawInputUnderlineStyle:
    case eMetric_IMEConvertedTextUnderlineStyle:
    case eMetric_IMESelectedRawTextUnderlineStyle:
    case eMetric_IMESelectedConvertedTextUnderline:
      aMetric = NS_UNDERLINE_STYLE_SOLID;
      break;
    case eMetric_SpellCheckerUnderlineStyle:
      aMetric = NS_UNDERLINE_STYLE_DOTTED;
      break;
    default:
      aMetric = 0;
      res = NS_ERROR_FAILURE;
  }
  return res;
}

NS_IMETHODIMP
nsLookAndFeel::GetMetric(const nsMetricFloatID aID,
                         float &aMetric)
{
  nsresult res = nsXPLookAndFeel::GetMetric(aID, aMetric);
  if (NS_SUCCEEDED(res))
    return res;
  res = NS_OK;
  
  switch (aID) {
    case eMetricFloat_TextFieldVerticalInsidePadding:
      aMetric = 0.25f;
      break;
    case eMetricFloat_TextFieldHorizontalInsidePadding:
      aMetric = 0.95f;
      break;
    case eMetricFloat_TextAreaVerticalInsidePadding:
      aMetric = 0.40f;
      break;
    case eMetricFloat_TextAreaHorizontalInsidePadding:
      aMetric = 0.40f;
      break;
    case eMetricFloat_ListVerticalInsidePadding:
      aMetric = 0.08f;
      break;
    case eMetricFloat_ListHorizontalInsidePadding:
      aMetric = 0.40f;
      break;
    case eMetricFloat_ButtonVerticalInsidePadding:
      aMetric = 0.5f;
      break;
    case eMetricFloat_ButtonHorizontalInsidePadding:
      aMetric = 0.5f;
      break;
    case eMetricFloat_IMEUnderlineRelativeSize:
      aMetric = 2.0f;
      break;
    case eMetricFloat_SpellCheckerUnderlineRelativeSize:
      aMetric = 2.0f;
      break;
    default:
      aMetric = -1.0;
      res = NS_ERROR_FAILURE;
  }

  return res;
}
