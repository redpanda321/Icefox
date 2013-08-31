/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Qt headers must be included before anything that might pull in our
// malloc wrappers.
#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyle>

#undef NS_LOOKANDFEEL_DEBUG
#ifdef NS_LOOKANDFEEL_DEBUG
#include <QDebug>
#endif

#include "nsLookAndFeel.h"
#include "nsStyleConsts.h"

#include <qglobal.h>

#define QCOLOR_TO_NS_RGB(c)                     \
  ((nscolor)NS_RGB(c.red(),c.green(),c.blue()))

nsLookAndFeel::nsLookAndFeel()
  : nsXPLookAndFeel(),
    mDefaultFontCached(false), mButtonFontCached(false),
    mFieldFontCached(false), mMenuFontCached(false)
{
}

nsLookAndFeel::~nsLookAndFeel()
{
}

nsresult
nsLookAndFeel::NativeGetColor(ColorID aID, nscolor &aColor)
{
  nsresult res = NS_OK;

  if (!qApp)
    return NS_ERROR_FAILURE;

  QPalette palette = qApp->palette();

  switch (aID) {
    case eColorID_WindowBackground:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
      break;

    case eColorID_WindowForeground:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
      break;

    case eColorID_WidgetBackground:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
      break;

    case eColorID_WidgetForeground:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::WindowText));
      break;

    case eColorID_WidgetSelectBackground:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
      break;

    case eColorID_WidgetSelectForeground:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::WindowText));
      break;

    case eColorID_Widget3DHighlight:
      aColor = NS_RGB(0xa0,0xa0,0xa0);
      break;

    case eColorID_Widget3DShadow:
      aColor = NS_RGB(0x40,0x40,0x40);
      break;

    case eColorID_TextBackground:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
      break;

    case eColorID_TextForeground:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::WindowText));
      break;

    case eColorID_TextSelectBackground:
    case eColorID_IMESelectedRawTextBackground:
    case eColorID_IMESelectedConvertedTextBackground:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Highlight));
      break;

    case eColorID_TextSelectForeground:
    case eColorID_IMESelectedRawTextForeground:
    case eColorID_IMESelectedConvertedTextForeground:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::HighlightedText));
      break;

    case eColorID_IMERawInputBackground:
    case eColorID_IMEConvertedTextBackground:
      aColor = NS_TRANSPARENT;
      break;

    case eColorID_IMERawInputForeground:
    case eColorID_IMEConvertedTextForeground:
      aColor = NS_SAME_AS_FOREGROUND_COLOR;
      break;

    case eColorID_IMERawInputUnderline:
    case eColorID_IMEConvertedTextUnderline:
      aColor = NS_SAME_AS_FOREGROUND_COLOR;
      break;

    case eColorID_IMESelectedRawTextUnderline:
    case eColorID_IMESelectedConvertedTextUnderline:
      aColor = NS_TRANSPARENT;
      break;

    case eColorID_SpellCheckerUnderline:
      aColor = NS_RGB(0xff, 0, 0);
      break;

    case eColorID_activeborder:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
      break;

    case eColorID_activecaption:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
      break;

    case eColorID_appworkspace:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
      break;

    case eColorID_background:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
      break;

    case eColorID_captiontext:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Text));
      break;

    case eColorID_graytext:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Disabled, QPalette::Text));
      break;

    case eColorID_highlight:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Highlight));
      break;

    case eColorID_highlighttext:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::HighlightedText));
      break;

    case eColorID_inactiveborder:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Disabled, QPalette::Window));
      break;

    case eColorID_inactivecaption:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Disabled, QPalette::Window));
      break;

    case eColorID_inactivecaptiontext:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Disabled, QPalette::Text));
      break;

    case eColorID_infobackground:
#if (QT_VERSION >= QT_VERSION_CHECK(4, 4, 0))
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::ToolTipBase));
#else
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Base));
#endif
      break;

    case eColorID_infotext:
#if (QT_VERSION >= QT_VERSION_CHECK(4, 4, 0))
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::ToolTipText));
#else
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Text));
#endif
      break;

    case eColorID_menu:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
      break;

    case eColorID_menutext:
    case eColorID__moz_menubartext:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Text));
      break;

    case eColorID_scrollbar:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Mid));
      break;

    case eColorID_threedface:
    case eColorID_buttonface:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Button));
      break;

    case eColorID_buttonhighlight:
    case eColorID_threedhighlight:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Dark));
      break;

    case eColorID_buttontext:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::ButtonText));
      break;

    case eColorID_buttonshadow:
    case eColorID_threedshadow:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Dark));
      break;

    case eColorID_threeddarkshadow:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Shadow));
      break;

    case eColorID_threedlightshadow:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Light));
      break;

    case eColorID_window:
    case eColorID_windowframe:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
      break;

    case eColorID_windowtext:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Text));
      break;

      // from the CSS3 working draft (not yet finalized)
      // http://www.w3.org/tr/2000/wd-css3-userint-20000216.html#color

    case eColorID__moz_buttondefault:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Button));
      break;

    case eColorID__moz_field:
    case eColorID__moz_combobox:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Base));
      break;

    case eColorID__moz_fieldtext:
    case eColorID__moz_comboboxtext:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Text));
      break;

    case eColorID__moz_dialog:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
      break;

    case eColorID__moz_dialogtext:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::WindowText));
      break;

    case eColorID__moz_dragtargetzone:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Window));
      break;

    case eColorID__moz_buttonhovertext:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::ButtonText));
      break;

    case eColorID__moz_menuhovertext:
    case eColorID__moz_menubarhovertext:
      aColor = QCOLOR_TO_NS_RGB(palette.color(QPalette::Normal, QPalette::Text));
      break;

    default:
      aColor = 0;
      res    = NS_ERROR_FAILURE;
      break;
  }
  return res;
}

#ifdef NS_LOOKANDFEEL_DEBUG
static const char *metricToString[] = {
  "eIntID_CaretBlinkTime",
  "eIntID_CaretWidth",
  "eIntID_ShowCaretDuringSelection",
  "eIntID_SelectTextfieldsOnKeyFocus",
  "eIntID_SubmenuDelay",
  "eIntID_MenusCanOverlapOSBar",
  "eIntID_SkipNavigatingDisabledMenuItem",
  "eIntID_DragThresholdX",
  "eIntID_DragThresholdY",
  "eIntID_UseAccessibilityTheme",
  "eIntID_ScrollArrowStyle",
  "eIntID_ScrollSliderStyle",
  "eIntID_ScrollButtonLeftMouseButtonAction",
  "eIntID_ScrollButtonMiddleMouseButtonAction",
  "eIntID_ScrollButtonRightMouseButtonAction",
  "eIntID_TreeOpenDelay",
  "eIntID_TreeCloseDelay",
  "eIntID_TreeLazyScrollDelay",
  "eIntID_TreeScrollDelay",
  "eIntID_TreeScrollLinesMax",
  "eIntID_TabFocusModel",
  "eIntID_WindowsDefaultTheme",
  "eIntID_AlertNotificationOrigin",
  "eIntID_ScrollToClick",
  "eIntID_IMERawInputUnderlineStyle",
  "eIntID_IMESelectedRawTextUnderlineStyle",
  "eIntID_IMEConvertedTextUnderlineStyle",
  "eIntID_IMESelectedConvertedTextUnderline",
  "eIntID_ImagesInMenus"
};
#endif

nsresult
nsLookAndFeel::GetIntImpl(IntID aID, int32_t &aResult)
{
#ifdef NS_LOOKANDFEEL_DEBUG
  qDebug("nsLookAndFeel::GetIntImpl aID = %s", metricToString[aID]);
#endif

  nsresult res = nsXPLookAndFeel::GetIntImpl(aID, aResult);
  if (NS_SUCCEEDED(res))
    return res;

  res = NS_OK;

  switch (aID) {
    case eIntID_CaretBlinkTime:
      aResult = 500;
      break;

    case eIntID_CaretWidth:
      aResult = 1;
      break;

    case eIntID_ShowCaretDuringSelection:
      aResult = 0;
      break;

    case eIntID_SelectTextfieldsOnKeyFocus:
      // Select textfield content when focused by kbd
      // used by nsEventStateManager::sTextfieldSelectModel
      aResult = 1;
      break;

    case eIntID_SubmenuDelay:
      aResult = 200;
      break;

    case eIntID_TooltipDelay:
      aResult = 500;
      break;

    case eIntID_MenusCanOverlapOSBar:
      // we want XUL popups to be able to overlap the task bar.
      aResult = 1;
      break;

    case eIntID_ScrollArrowStyle:
      aResult = eScrollArrowStyle_Single;
      break;

    case eIntID_ScrollSliderStyle:
      aResult = eScrollThumbStyle_Proportional;
      break;

    case eIntID_TouchEnabled:
#ifdef MOZ_PLATFORM_MAEMO
      // All known Maemo devices are touch enabled.
      aResult = 1;
#else
      aResult = 0;
      res = NS_ERROR_NOT_IMPLEMENTED;
#endif
      break;

    case eIntID_WindowsDefaultTheme:
    case eIntID_MaemoClassic:
      aResult = 0;
      res = NS_ERROR_NOT_IMPLEMENTED;
      break;

    case eIntID_SpellCheckerUnderlineStyle:
      aResult = NS_STYLE_TEXT_DECORATION_STYLE_WAVY;
      break;

    case eIntID_ScrollbarButtonAutoRepeatBehavior:
      aResult = 1;
      break;

    default:
      aResult = 0;
      res = NS_ERROR_FAILURE;
  }
  return res;
}

#ifdef NS_LOOKANDFEEL_DEBUG
static const char *floatMetricToString[] = {
  "eFloatID_IMEUnderlineRelativeSize"
};
#endif

nsresult
nsLookAndFeel::GetFloatImpl(FloatID aID, float &aResult)
{
#ifdef NS_LOOKANDFEEL_DEBUG
  qDebug("nsLookAndFeel::GetFloatImpl aID = %s", floatMetricToString[aID]);
#endif

  nsresult res = nsXPLookAndFeel::GetFloatImpl(aID, aResult);
  if (NS_SUCCEEDED(res))
    return res;
  res = NS_OK;

  switch (aID) {
    case eFloatID_IMEUnderlineRelativeSize:
      aResult = 1.0f;
      break;

    case eFloatID_SpellCheckerUnderlineRelativeSize:
      aResult = 1.0f;
      break;

    default:
      aResult = -1.0;
      res = NS_ERROR_FAILURE;
      break;
  }
  return res;
}

static void
GetSystemFontInfo(const char *aClassName, nsString *aFontName,
                  gfxFontStyle *aFontStyle)
{
  QFont qFont = QApplication::font(aClassName);

  NS_NAMED_LITERAL_STRING(quote, "\"");
  nsString family((PRUnichar*)qFont.family().data());
  *aFontName = quote + family + quote;

  aFontStyle->systemFont = true;
  aFontStyle->style = NS_FONT_STYLE_NORMAL;
  aFontStyle->weight = qFont.weight();
  // FIXME: Set aFontStyle->stretch correctly!
  aFontStyle->stretch = NS_FONT_STRETCH_NORMAL;
  // use pixel size directly if it is set, otherwise compute from point size
  if (qFont.pixelSize() != -1) {
    aFontStyle->size = qFont.pixelSize();
  } else {
    aFontStyle->size = qFont.pointSizeF() * 96.0f / 72.0f;
  }
}

bool
nsLookAndFeel::GetFontImpl(FontID aID, nsString& aFontName,
                           gfxFontStyle& aFontStyle,
                           float aDevPixPerCSSPixel)
{
  const char *className = NULL;
  nsString *cachedFontName = NULL;
  gfxFontStyle *cachedFontStyle = NULL;
  bool *isCached = NULL;

  switch (aID) {
    case eFont_Menu:         // css2
    case eFont_PullDownMenu: // css3
      cachedFontName = &mMenuFontName;
      cachedFontStyle = &mMenuFontStyle;
      isCached = &mMenuFontCached;
      className = "QAction";
      break;

    case eFont_Field:        // css3
    case eFont_List:         // css3
      cachedFontName = &mFieldFontName;
      cachedFontStyle = &mFieldFontStyle;
      isCached = &mFieldFontCached;
      className = "QlineEdit";
      break;

    case eFont_Button:       // css3
      cachedFontName = &mButtonFontName;
      cachedFontStyle = &mButtonFontStyle;
      isCached = &mButtonFontCached;
      className = "QPushButton";
      break;

    case eFont_Caption:      // css2
    case eFont_Icon:         // css2
    case eFont_MessageBox:   // css2
    case eFont_SmallCaption: // css2
    case eFont_StatusBar:    // css2
    case eFont_Window:       // css3
    case eFont_Document:     // css3
    case eFont_Workspace:    // css3
    case eFont_Desktop:      // css3
    case eFont_Info:         // css3
    case eFont_Dialog:       // css3
    case eFont_Tooltips:     // moz
    case eFont_Widget:       // moz
      cachedFontName = &mDefaultFontName;
      cachedFontStyle = &mDefaultFontStyle;
      isCached = &mDefaultFontCached;
      className = "Qlabel";
      break;
  }

  if (!*isCached) {
    GetSystemFontInfo(className, cachedFontName, cachedFontStyle);
    *isCached = true;
  }

  aFontName = *cachedFontName;
  aFontStyle = *cachedFontStyle;
  return true;
}

void
nsLookAndFeel::RefreshImpl()
{
  nsXPLookAndFeel::RefreshImpl();
  mDefaultFontCached = false;
  mButtonFontCached = false;
  mFieldFontCached = false;
  mMenuFontCached = false;
}
