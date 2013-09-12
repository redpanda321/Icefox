/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// for strtod()
#include <stdlib.h>

#include "nsLookAndFeel.h"

#include <gtk/gtk.h>
#include <gdk/gdk.h>

#ifdef MOZ_PANGO
#include <pango/pango.h>
#include <pango/pango-fontmap.h>
#endif

#include <fontconfig/fontconfig.h>
#include "gfxPlatformGtk.h"

#include "gtkdrawing.h"
#include "nsStyleConsts.h"

#ifdef MOZ_PLATFORM_MAEMO
#include "nsIServiceManager.h"
#include "nsIPropertyBag2.h"
#include "nsLiteralString.h"
#endif

using mozilla::LookAndFeel;

#define GDK_COLOR_TO_NS_RGB(c) \
    ((nscolor) NS_RGB(c.red>>8, c.green>>8, c.blue>>8))

nscolor   nsLookAndFeel::sInfoText = 0;
nscolor   nsLookAndFeel::sInfoBackground = 0;
nscolor   nsLookAndFeel::sMenuBarText = 0;
nscolor   nsLookAndFeel::sMenuBarHoverText = 0;
nscolor   nsLookAndFeel::sMenuText = 0;
nscolor   nsLookAndFeel::sMenuHover = 0;
nscolor   nsLookAndFeel::sMenuHoverText = 0;
nscolor   nsLookAndFeel::sMenuBackground = 0;
nscolor   nsLookAndFeel::sButtonBackground = 0;
nscolor   nsLookAndFeel::sButtonText = 0;
nscolor   nsLookAndFeel::sButtonOuterLightBorder = 0;
nscolor   nsLookAndFeel::sButtonInnerDarkBorder = 0;
nscolor   nsLookAndFeel::sOddCellBackground = 0;
nscolor   nsLookAndFeel::sNativeHyperLinkText = 0;
nscolor   nsLookAndFeel::sComboBoxText = 0;
nscolor   nsLookAndFeel::sComboBoxBackground = 0;
PRUnichar nsLookAndFeel::sInvisibleCharacter = PRUnichar('*');
float     nsLookAndFeel::sCaretRatio = 0;
bool      nsLookAndFeel::sMenuSupportsDrag = false;

nsLookAndFeel::nsLookAndFeel()
    : nsXPLookAndFeel(),
      mStyle(nullptr),
      mDefaultFontCached(false), mButtonFontCached(false),
      mFieldFontCached(false), mMenuFontCached(false)
{
    InitWidget();

    static bool sInitialized = false;

    if (!sInitialized) {
        sInitialized = true;
        InitLookAndFeel();
    }
}

nsLookAndFeel::~nsLookAndFeel()
{
    g_object_unref(mStyle);
}

nsresult
nsLookAndFeel::NativeGetColor(ColorID aID, nscolor& aColor)
{
    nsresult res = NS_OK;

    switch (aID) {
        // These colors don't seem to be used for anything anymore in Mozilla
        // (except here at least TextSelectBackground and TextSelectForeground)
        // The CSS2 colors below are used.
    case eColorID_WindowBackground:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->base[GTK_STATE_NORMAL]);
        break;
    case eColorID_WindowForeground:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->text[GTK_STATE_NORMAL]);
        break;
    case eColorID_WidgetBackground:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->bg[GTK_STATE_NORMAL]);
        break;
    case eColorID_WidgetForeground:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->fg[GTK_STATE_NORMAL]);
        break;
    case eColorID_WidgetSelectBackground:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->bg[GTK_STATE_SELECTED]);
        break;
    case eColorID_WidgetSelectForeground:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->fg[GTK_STATE_SELECTED]);
        break;
    case eColorID_Widget3DHighlight:
        aColor = NS_RGB(0xa0,0xa0,0xa0);
        break;
    case eColorID_Widget3DShadow:
        aColor = NS_RGB(0x40,0x40,0x40);
        break;
    case eColorID_TextBackground:
        // not used?
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->base[GTK_STATE_NORMAL]);
        break;
    case eColorID_TextForeground: 
        // not used?
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->text[GTK_STATE_NORMAL]);
        break;
    case eColorID_TextSelectBackground:
    case eColorID_IMESelectedRawTextBackground:
    case eColorID_IMESelectedConvertedTextBackground:
        // still used
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->base[GTK_STATE_SELECTED]);
        break;
    case eColorID_TextSelectForeground:
    case eColorID_IMESelectedRawTextForeground:
    case eColorID_IMESelectedConvertedTextForeground:
        // still used
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->text[GTK_STATE_SELECTED]);
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

        // css2  http://www.w3.org/TR/REC-CSS2/ui.html#system-colors
    case eColorID_activeborder:
        // active window border
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->bg[GTK_STATE_NORMAL]);
        break;
    case eColorID_activecaption:
        // active window caption background
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->bg[GTK_STATE_NORMAL]);
        break;
    case eColorID_appworkspace:
        // MDI background color
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->bg[GTK_STATE_NORMAL]);
        break;
    case eColorID_background:
        // desktop background
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->bg[GTK_STATE_NORMAL]);
        break;
    case eColorID_captiontext:
        // text in active window caption, size box, and scrollbar arrow box (!)
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->fg[GTK_STATE_NORMAL]);
        break;
    case eColorID_graytext:
        // disabled text in windows, menus, etc.
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->fg[GTK_STATE_INSENSITIVE]);
        break;
    case eColorID_highlight:
        // background of selected item
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->base[GTK_STATE_SELECTED]);
        break;
    case eColorID_highlighttext:
        // text of selected item
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->text[GTK_STATE_SELECTED]);
        break;
    case eColorID_inactiveborder:
        // inactive window border
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->bg[GTK_STATE_NORMAL]);
        break;
    case eColorID_inactivecaption:
        // inactive window caption
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->bg[GTK_STATE_INSENSITIVE]);
        break;
    case eColorID_inactivecaptiontext:
        // text in inactive window caption
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->fg[GTK_STATE_INSENSITIVE]);
        break;
    case eColorID_infobackground:
        // tooltip background color
        aColor = sInfoBackground;
        break;
    case eColorID_infotext:
        // tooltip text color
        aColor = sInfoText;
        break;
    case eColorID_menu:
        // menu background
        aColor = sMenuBackground;
        break;
    case eColorID_menutext:
        // menu text
        aColor = sMenuText;
        break;
    case eColorID_scrollbar:
        // scrollbar gray area
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->bg[GTK_STATE_ACTIVE]);
        break;

    case eColorID_threedface:
    case eColorID_buttonface:
        // 3-D face color
        aColor = sButtonBackground;
        break;

    case eColorID_buttontext:
        // text on push buttons
        aColor = sButtonText;
        break;

    case eColorID_buttonhighlight:
        // 3-D highlighted edge color
    case eColorID_threedhighlight:
        // 3-D highlighted outer edge color
        aColor = sButtonOuterLightBorder;
        break;

    case eColorID_threedlightshadow:
        // 3-D highlighted inner edge color
        aColor = sButtonBackground; // always same as background in GTK code
        break;

    case eColorID_buttonshadow:
        // 3-D shadow edge color
    case eColorID_threedshadow:
        // 3-D shadow inner edge color
        aColor = sButtonInnerDarkBorder;
        break;

    case eColorID_threeddarkshadow:
        // 3-D shadow outer edge color
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->black);
        break;

    case eColorID_window:
    case eColorID_windowframe:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->bg[GTK_STATE_NORMAL]);
        break;

    case eColorID_windowtext:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->fg[GTK_STATE_NORMAL]);
        break;

    case eColorID__moz_eventreerow:
    case eColorID__moz_field:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->base[GTK_STATE_NORMAL]);
        break;
    case eColorID__moz_fieldtext:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->text[GTK_STATE_NORMAL]);
        break;
    case eColorID__moz_dialog:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->bg[GTK_STATE_NORMAL]);
        break;
    case eColorID__moz_dialogtext:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->fg[GTK_STATE_NORMAL]);
        break;
    case eColorID__moz_dragtargetzone:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->bg[GTK_STATE_SELECTED]);
        break; 
    case eColorID__moz_buttondefault:
        // default button border color
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->black);
        break;
    case eColorID__moz_buttonhoverface:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->bg[GTK_STATE_PRELIGHT]);
        break;
    case eColorID__moz_buttonhovertext:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->fg[GTK_STATE_PRELIGHT]);
        break;
    case eColorID__moz_cellhighlight:
    case eColorID__moz_html_cellhighlight:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->base[GTK_STATE_ACTIVE]);
        break;
    case eColorID__moz_cellhighlighttext:
    case eColorID__moz_html_cellhighlighttext:
        aColor = GDK_COLOR_TO_NS_RGB(mStyle->text[GTK_STATE_ACTIVE]);
        break;
    case eColorID__moz_menuhover:
        aColor = sMenuHover;
        break;
    case eColorID__moz_menuhovertext:
        aColor = sMenuHoverText;
        break;
    case eColorID__moz_oddtreerow:
        aColor = sOddCellBackground;
        break;
    case eColorID__moz_nativehyperlinktext:
        aColor = sNativeHyperLinkText;
        break;
    case eColorID__moz_comboboxtext:
        aColor = sComboBoxText;
        break;
    case eColorID__moz_combobox:
        aColor = sComboBoxBackground;
        break;
    case eColorID__moz_menubartext:
        aColor = sMenuBarText;
        break;
    case eColorID__moz_menubarhovertext:
        aColor = sMenuBarHoverText;
        break;
    default:
        /* default color is BLACK */
        aColor = 0;
        res    = NS_ERROR_FAILURE;
        break;
    }

    return res;
}

static void darken_gdk_color(GdkColor *src, GdkColor *dest)
{
    gdouble red;
    gdouble green;
    gdouble blue;

    red = (gdouble) src->red / 65535.0;
    green = (gdouble) src->green / 65535.0;
    blue = (gdouble) src->blue / 65535.0;

    red *= 0.93;
    green *= 0.93;
    blue *= 0.93;

    dest->red = red * 65535.0;
    dest->green = green * 65535.0;
    dest->blue = blue * 65535.0;
}

static int32_t CheckWidgetStyle(GtkWidget* aWidget, const char* aStyle, int32_t aResult) {
    gboolean value = FALSE;
    gtk_widget_style_get(aWidget, aStyle, &value, NULL);
    return value ? aResult : 0;
}

static int32_t ConvertGTKStepperStyleToMozillaScrollArrowStyle(GtkWidget* aWidget)
{
    if (!aWidget)
        return mozilla::LookAndFeel::eScrollArrowStyle_Single;
  
    return
        CheckWidgetStyle(aWidget, "has-backward-stepper",
                         mozilla::LookAndFeel::eScrollArrow_StartBackward) |
        CheckWidgetStyle(aWidget, "has-forward-stepper",
                         mozilla::LookAndFeel::eScrollArrow_EndForward) |
        CheckWidgetStyle(aWidget, "has-secondary-backward-stepper",
                         mozilla::LookAndFeel::eScrollArrow_EndBackward) |
        CheckWidgetStyle(aWidget, "has-secondary-forward-stepper",
                         mozilla::LookAndFeel::eScrollArrow_StartForward);
}

nsresult
nsLookAndFeel::GetIntImpl(IntID aID, int32_t &aResult)
{
    nsresult res = NS_OK;

    // Set these before they can get overrided in the nsXPLookAndFeel. 
    switch (aID) {
    case eIntID_ScrollButtonLeftMouseButtonAction:
        aResult = 0;
        return NS_OK;
    case eIntID_ScrollButtonMiddleMouseButtonAction:
        aResult = 1;
        return NS_OK;
    case eIntID_ScrollButtonRightMouseButtonAction:
        aResult = 2;
        return NS_OK;
    default:
        break;
    }

    res = nsXPLookAndFeel::GetIntImpl(aID, aResult);
    if (NS_SUCCEEDED(res))
        return res;
    res = NS_OK;

    switch (aID) {
    case eIntID_CaretBlinkTime:
        {
            GtkSettings *settings;
            gint blink_time;
            gboolean blink;

            settings = gtk_settings_get_default ();
            g_object_get (settings,
                          "gtk-cursor-blink-time", &blink_time,
                          "gtk-cursor-blink", &blink,
                          NULL);
 
            if (blink)
                aResult = (int32_t) blink_time;
            else
                aResult = 0;
            break;
        }
    case eIntID_CaretWidth:
        aResult = 1;
        break;
    case eIntID_ShowCaretDuringSelection:
        aResult = 0;
        break;
    case eIntID_SelectTextfieldsOnKeyFocus:
        {
            GtkWidget *entry;
            GtkSettings *settings;
            gboolean select_on_focus;

            entry = gtk_entry_new();
            g_object_ref_sink(entry);
            settings = gtk_widget_get_settings(entry);
            g_object_get(settings, 
                         "gtk-entry-select-on-focus",
                         &select_on_focus,
                         NULL);
            
            if(select_on_focus)
                aResult = 1;
            else
                aResult = 0;

            gtk_widget_destroy(entry);
            g_object_unref(entry);
        }
        break;
    case eIntID_SubmenuDelay:
        {
            GtkSettings *settings;
            gint delay;

            settings = gtk_settings_get_default ();
            g_object_get (settings, "gtk-menu-popup-delay", &delay, NULL);
            aResult = (int32_t) delay;
            break;
        }
    case eIntID_TooltipDelay:
        {
            aResult = 500;
            break;
        }
    case eIntID_MenusCanOverlapOSBar:
        // we want XUL popups to be able to overlap the task bar.
        aResult = 1;
        break;
    case eIntID_SkipNavigatingDisabledMenuItem:
        aResult = 1;
        break;
    case eIntID_DragThresholdX:
    case eIntID_DragThresholdY:
        {
            GtkWidget* box = gtk_hbox_new(FALSE, 5);
            gint threshold = 0;
            g_object_get(gtk_widget_get_settings(box),
                         "gtk-dnd-drag-threshold", &threshold,
                         NULL);
            g_object_ref_sink(box);
            
            aResult = threshold;
        }
        break;
    case eIntID_ScrollArrowStyle:
        moz_gtk_init();
        aResult =
            ConvertGTKStepperStyleToMozillaScrollArrowStyle(moz_gtk_get_scrollbar_widget());
        break;
    case eIntID_ScrollSliderStyle:
        aResult = eScrollThumbStyle_Proportional;
        break;
    case eIntID_TreeOpenDelay:
        aResult = 1000;
        break;
    case eIntID_TreeCloseDelay:
        aResult = 1000;
        break;
    case eIntID_TreeLazyScrollDelay:
        aResult = 150;
        break;
    case eIntID_TreeScrollDelay:
        aResult = 100;
        break;
    case eIntID_TreeScrollLinesMax:
        aResult = 3;
        break;
    case eIntID_DWMCompositor:
    case eIntID_WindowsClassic:
    case eIntID_WindowsDefaultTheme:
    case eIntID_WindowsThemeIdentifier:
        aResult = 0;
        res = NS_ERROR_NOT_IMPLEMENTED;
        break;
    case eIntID_TouchEnabled:
#ifdef MOZ_PLATFORM_MAEMO
        // All Hildon devices are touch-enabled
        aResult = 1;
#else
        aResult = 0;
        res = NS_ERROR_NOT_IMPLEMENTED;
#endif
        break;
    case eIntID_MaemoClassic:
#ifdef MOZ_PLATFORM_MAEMO
        {
            aResult = 0;
            nsCOMPtr<nsIPropertyBag2> infoService(do_GetService("@mozilla.org/system-info;1"));
            if (infoService) {
                nsCString deviceType;
                nsresult rv = infoService->GetPropertyAsACString(NS_LITERAL_STRING("device"),
                                                                 deviceType);
                if (NS_SUCCEEDED(rv)) {
                    if (deviceType.EqualsLiteral("Nokia N8xx"))
                        aResult = 1;
                }
            }
        }
#else
        aResult = 0;
        res = NS_ERROR_NOT_IMPLEMENTED;
#endif
        break;
    case eIntID_MacGraphiteTheme:
    case eIntID_MacLionTheme:
        aResult = 0;
        res = NS_ERROR_NOT_IMPLEMENTED;
        break;
    case eIntID_IMERawInputUnderlineStyle:
    case eIntID_IMEConvertedTextUnderlineStyle:
        aResult = NS_STYLE_TEXT_DECORATION_STYLE_SOLID;
        break;
    case eIntID_IMESelectedRawTextUnderlineStyle:
    case eIntID_IMESelectedConvertedTextUnderline:
        aResult = NS_STYLE_TEXT_DECORATION_STYLE_NONE;
        break;
    case eIntID_SpellCheckerUnderlineStyle:
        aResult = NS_STYLE_TEXT_DECORATION_STYLE_WAVY;
        break;
    case eIntID_ImagesInMenus:
        aResult = moz_gtk_images_in_menus();
        break;
    case eIntID_ImagesInButtons:
        aResult = moz_gtk_images_in_buttons();
        break;
    case eIntID_MenuBarDrag:
        aResult = sMenuSupportsDrag;
        break;
    case eIntID_ScrollbarButtonAutoRepeatBehavior:
        aResult = 1;
        break;
    default:
        aResult = 0;
        res     = NS_ERROR_FAILURE;
    }

    return res;
}

nsresult
nsLookAndFeel::GetFloatImpl(FloatID aID, float &aResult)
{
    nsresult res = NS_OK;
    res = nsXPLookAndFeel::GetFloatImpl(aID, aResult);
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
    case eFloatID_CaretAspectRatio:
        aResult = sCaretRatio;
        break;
    default:
        aResult = -1.0;
        res = NS_ERROR_FAILURE;
    }
    return res;
}

#ifdef MOZ_PANGO
static void
GetSystemFontInfo(GtkWidget *aWidget,
                  nsString *aFontName,
                  gfxFontStyle *aFontStyle)
{
    GtkSettings *settings = gtk_widget_get_settings(aWidget);

    aFontStyle->style       = NS_FONT_STYLE_NORMAL;

    gchar *fontname;
    g_object_get(settings, "gtk-font-name", &fontname, NULL);

    PangoFontDescription *desc;
    desc = pango_font_description_from_string(fontname);

    aFontStyle->systemFont = true;

    g_free(fontname);

    NS_NAMED_LITERAL_STRING(quote, "\"");
    NS_ConvertUTF8toUTF16 family(pango_font_description_get_family(desc));
    *aFontName = quote + family + quote;

    aFontStyle->weight = pango_font_description_get_weight(desc);

    // FIXME: Set aFontStyle->stretch correctly!
    aFontStyle->stretch = NS_FONT_STRETCH_NORMAL;

    float size = float(pango_font_description_get_size(desc)) / PANGO_SCALE;

    // |size| is now either pixels or pango-points (not Mozilla-points!)

    if (!pango_font_description_get_size_is_absolute(desc)) {
        // |size| is in pango-points, so convert to pixels.
        size *= float(gfxPlatformGtk::GetDPI()) / POINTS_PER_INCH_FLOAT;
    }

    // |size| is now pixels

    aFontStyle->size = size;

    pango_font_description_free(desc);
}

static void
GetSystemFontInfo(LookAndFeel::FontID aID,
                  nsString *aFontName,
                  gfxFontStyle *aFontStyle)
{
    if (aID == LookAndFeel::eFont_Widget) {
        GtkWidget *label = gtk_label_new("M");
        GtkWidget *parent = gtk_fixed_new();
        GtkWidget *window = gtk_window_new(GTK_WINDOW_POPUP);

        gtk_container_add(GTK_CONTAINER(parent), label);
        gtk_container_add(GTK_CONTAINER(window), parent);

        gtk_widget_ensure_style(label);
        GetSystemFontInfo(label, aFontName, aFontStyle);
        gtk_widget_destroy(window);  // no unref, windows are different

    } else if (aID == LookAndFeel::eFont_Button) {
        GtkWidget *label = gtk_label_new("M");
        GtkWidget *parent = gtk_fixed_new();
        GtkWidget *button = gtk_button_new();
        GtkWidget *window = gtk_window_new(GTK_WINDOW_POPUP);

        gtk_container_add(GTK_CONTAINER(button), label);
        gtk_container_add(GTK_CONTAINER(parent), button);
        gtk_container_add(GTK_CONTAINER(window), parent);

        gtk_widget_ensure_style(label);
        GetSystemFontInfo(label, aFontName, aFontStyle);
        gtk_widget_destroy(window);  // no unref, windows are different

    } else if (aID == LookAndFeel::eFont_Field) {
        GtkWidget *entry = gtk_entry_new();
        GtkWidget *parent = gtk_fixed_new();
        GtkWidget *window = gtk_window_new(GTK_WINDOW_POPUP);

        gtk_container_add(GTK_CONTAINER(parent), entry);
        gtk_container_add(GTK_CONTAINER(window), parent);

        gtk_widget_ensure_style(entry);
        GetSystemFontInfo(entry, aFontName, aFontStyle);
        gtk_widget_destroy(window);  // no unref, windows are different

    } else {
        NS_ABORT_IF_FALSE(aID == LookAndFeel::eFont_Menu, "unexpected font ID");
        GtkWidget *accel_label = gtk_accel_label_new("M");
        GtkWidget *menuitem = gtk_menu_item_new();
        GtkWidget *menu = gtk_menu_new();
        g_object_ref_sink(GTK_OBJECT(menu));

        gtk_container_add(GTK_CONTAINER(menuitem), accel_label);
        gtk_menu_shell_append((GtkMenuShell *)GTK_MENU(menu), menuitem);

        gtk_widget_ensure_style(accel_label);
        GetSystemFontInfo(accel_label, aFontName, aFontStyle);
        g_object_unref(menu);
    }
}

#else // not MOZ_PANGO

static void
GetSystemFontInfo(LookAndFeel::FontID /*unused */,
                  nsString *aFontName,
                  gfxFontStyle *aFontStyle)
{
    /* FIXME: DFB FT2 Hardcoding the system font info for now. */
    aFontStyle->style      = NS_FONT_STYLE_NORMAL;
    aFontStyle->weight     = NS_FONT_WEIGHT_NORMAL;
    aFontStyle->size       = 40/3;
    aFontStyle->stretch    = NS_FONT_STRETCH_NORMAL;
    aFontStyle->systemFont = true;
    aFontName->AssignLiteral("\"Sans\"");
}

#endif // not MOZ_PANGO

bool
nsLookAndFeel::GetFontImpl(FontID aID, nsString& aFontName,
                           gfxFontStyle& aFontStyle,
                           float aDevPixPerCSSPixel)
{
  nsString *cachedFontName = NULL;
  gfxFontStyle *cachedFontStyle = NULL;
  bool *isCached = NULL;

  switch (aID) {
    case eFont_Menu:         // css2
    case eFont_PullDownMenu: // css3
      cachedFontName = &mMenuFontName;
      cachedFontStyle = &mMenuFontStyle;
      isCached = &mMenuFontCached;
      aID = eFont_Menu;
      break;

    case eFont_Field:        // css3
    case eFont_List:         // css3
      cachedFontName = &mFieldFontName;
      cachedFontStyle = &mFieldFontStyle;
      isCached = &mFieldFontCached;
      aID = eFont_Field;
      break;

    case eFont_Button:       // css3
      cachedFontName = &mButtonFontName;
      cachedFontStyle = &mButtonFontStyle;
      isCached = &mButtonFontCached;
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
      aID = eFont_Widget;
      break;
  }

  if (!*isCached) {
    GetSystemFontInfo(aID, cachedFontName, cachedFontStyle);
    *isCached = true;
  }

  aFontName = *cachedFontName;
  aFontStyle = *cachedFontStyle;
  return true;
}

void
nsLookAndFeel::InitLookAndFeel()
{
    GtkStyle *style;

    // tooltip foreground and background
    style = gtk_rc_get_style_by_paths(gtk_settings_get_default(),
                                      "gtk-tooltips", "GtkWindow",
                                      GTK_TYPE_WINDOW);
    if (style) {
        sInfoBackground = GDK_COLOR_TO_NS_RGB(style->bg[GTK_STATE_NORMAL]);
        sInfoText = GDK_COLOR_TO_NS_RGB(style->fg[GTK_STATE_NORMAL]);
    }

    // menu foreground & menu background
    GtkWidget *accel_label = gtk_accel_label_new("M");
    GtkWidget *menuitem = gtk_menu_item_new();
    GtkWidget *menu = gtk_menu_new();

    g_object_ref_sink(menu);

    gtk_container_add(GTK_CONTAINER(menuitem), accel_label);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

    gtk_widget_set_style(accel_label, NULL);
    gtk_widget_set_style(menu, NULL);
    gtk_widget_realize(menu);
    gtk_widget_realize(accel_label);

    style = gtk_widget_get_style(accel_label);
    if (style) {
        sMenuText = GDK_COLOR_TO_NS_RGB(style->fg[GTK_STATE_NORMAL]);
    }

    style = gtk_widget_get_style(menu);
    if (style) {
        sMenuBackground = GDK_COLOR_TO_NS_RGB(style->bg[GTK_STATE_NORMAL]);
    }
    
    style = gtk_widget_get_style(menuitem);
    if (style) {
        sMenuHover = GDK_COLOR_TO_NS_RGB(style->bg[GTK_STATE_PRELIGHT]);
        sMenuHoverText = GDK_COLOR_TO_NS_RGB(style->fg[GTK_STATE_PRELIGHT]);
    }

    g_object_unref(menu);


    // button styles
    GtkWidget *parent = gtk_fixed_new();
    GtkWidget *button = gtk_button_new();
    GtkWidget *label = gtk_label_new("M");
    GtkWidget *combobox = gtk_combo_box_new();
    GtkWidget *comboboxLabel = gtk_label_new("M");
    GtkWidget *window = gtk_window_new(GTK_WINDOW_POPUP);
    GtkWidget *treeView = gtk_tree_view_new();
    GtkWidget *linkButton = gtk_link_button_new("http://example.com/");
    GtkWidget *menuBar = gtk_menu_bar_new();
    GtkWidget *entry = gtk_entry_new();

    gtk_container_add(GTK_CONTAINER(button), label);
    gtk_container_add(GTK_CONTAINER(combobox), comboboxLabel);
    gtk_container_add(GTK_CONTAINER(parent), button);
    gtk_container_add(GTK_CONTAINER(parent), treeView);
    gtk_container_add(GTK_CONTAINER(parent), linkButton);
    gtk_container_add(GTK_CONTAINER(parent), combobox);
    gtk_container_add(GTK_CONTAINER(parent), menuBar);
    gtk_container_add(GTK_CONTAINER(window), parent);
    gtk_container_add(GTK_CONTAINER(parent), entry);

    gtk_widget_set_style(button, NULL);
    gtk_widget_set_style(label, NULL);
    gtk_widget_set_style(treeView, NULL);
    gtk_widget_set_style(linkButton, NULL);
    gtk_widget_set_style(combobox, NULL);
    gtk_widget_set_style(comboboxLabel, NULL);
    gtk_widget_set_style(menuBar, NULL);
    gtk_widget_set_style(entry, NULL);

    gtk_widget_realize(button);
    gtk_widget_realize(label);
    gtk_widget_realize(treeView);
    gtk_widget_realize(linkButton);
    gtk_widget_realize(combobox);
    gtk_widget_realize(comboboxLabel);
    gtk_widget_realize(menuBar);
    gtk_widget_realize(entry);

    style = gtk_widget_get_style(label);
    if (style) {
        sButtonText = GDK_COLOR_TO_NS_RGB(style->fg[GTK_STATE_NORMAL]);
    }

    style = gtk_widget_get_style(comboboxLabel);
    if (style) {
        sComboBoxText = GDK_COLOR_TO_NS_RGB(style->fg[GTK_STATE_NORMAL]);
    }
    style = gtk_widget_get_style(combobox);
    if (style) {
        sComboBoxBackground = GDK_COLOR_TO_NS_RGB(style->bg[GTK_STATE_NORMAL]);
    }

    style = gtk_widget_get_style(menuBar);
    if (style) {
        sMenuBarText = GDK_COLOR_TO_NS_RGB(style->fg[GTK_STATE_NORMAL]);
        sMenuBarHoverText = GDK_COLOR_TO_NS_RGB(style->fg[GTK_STATE_SELECTED]);
    }

    // Some themes have a unified menu bar, and support window dragging on it
    gboolean supports_menubar_drag = FALSE;
    GParamSpec *param_spec =
        gtk_widget_class_find_style_property(GTK_WIDGET_GET_CLASS(menuBar),
                                             "window-dragging");
    if (param_spec) {
        if (g_type_is_a(G_PARAM_SPEC_VALUE_TYPE(param_spec), G_TYPE_BOOLEAN)) {
            gtk_widget_style_get(menuBar,
                                 "window-dragging", &supports_menubar_drag,
                                 NULL);
        }
    }
    sMenuSupportsDrag = supports_menubar_drag;

    // GTK's guide to fancy odd row background colors:
    // 1) Check if a theme explicitly defines an odd row color
    // 2) If not, check if it defines an even row color, and darken it
    //    slightly by a hardcoded value (gtkstyle.c)
    // 3) If neither are defined, take the base background color and
    //    darken that by a hardcoded value
    GdkColor colorValue;
    GdkColor *colorValuePtr = NULL;
    gtk_widget_style_get(treeView,
                         "odd-row-color", &colorValuePtr,
                         NULL);

    if (colorValuePtr) {
        colorValue = *colorValuePtr;
    } else {
        gtk_widget_style_get(treeView,
                             "even-row-color", &colorValuePtr,
                             NULL);
        if (colorValuePtr)
            darken_gdk_color(colorValuePtr, &colorValue);
        else
            darken_gdk_color(&treeView->style->base[GTK_STATE_NORMAL], &colorValue);
    }

    sOddCellBackground = GDK_COLOR_TO_NS_RGB(colorValue);
    if (colorValuePtr)
        gdk_color_free(colorValuePtr);

    style = gtk_widget_get_style(button);
    if (style) {
        sButtonBackground = GDK_COLOR_TO_NS_RGB(style->bg[GTK_STATE_NORMAL]);
        sButtonOuterLightBorder =
            GDK_COLOR_TO_NS_RGB(style->light[GTK_STATE_NORMAL]);
        sButtonInnerDarkBorder =
            GDK_COLOR_TO_NS_RGB(style->dark[GTK_STATE_NORMAL]);
    }

    colorValuePtr = NULL;
    gtk_widget_style_get(linkButton, "link-color", &colorValuePtr, NULL);
    if (colorValuePtr) {
        colorValue = *colorValuePtr; // we can't pass deref pointers to GDK_COLOR_TO_NS_RGB
        sNativeHyperLinkText = GDK_COLOR_TO_NS_RGB(colorValue);
        gdk_color_free(colorValuePtr);
    } else {
        sNativeHyperLinkText = NS_RGB(0x00,0x00,0xEE);
    }

    // invisible character styles
    guint value;
    g_object_get (entry, "invisible-char", &value, NULL);
    sInvisibleCharacter = PRUnichar(value);

    // caret styles
    gtk_widget_style_get(entry,
                         "cursor-aspect-ratio", &sCaretRatio,
                         NULL);

    gtk_widget_destroy(window);
}

void
nsLookAndFeel::InitWidget()
{
    NS_ASSERTION(!mStyle, "already initialized");
    // GtkInvisibles come with a refcount that is not floating
    // (since their initialization code calls g_object_ref_sink) and
    // their destroy code releases that reference (which means they
    // have to be explicitly destroyed, since calling unref enough
    // to cause destruction would lead to *another* unref).
    // However, this combination means that it's actually still ok
    // to use the normal pattern, which is to g_object_ref_sink
    // after construction, and then destroy *and* unref when we're
    // done.  (Though we could skip the g_object_ref_sink and the
    // corresponding g_object_unref, but that's particular to
    // GtkInvisibles and GtkWindows.)
    GtkWidget *widget = gtk_invisible_new();
    g_object_ref_sink(widget); // effectively g_object_ref (see above)

    gtk_widget_ensure_style(widget);
    mStyle = gtk_style_copy(gtk_widget_get_style(widget));

    gtk_widget_destroy(widget);
    g_object_unref(widget);
}

// virtual
PRUnichar
nsLookAndFeel::GetPasswordCharacterImpl()
{
    return sInvisibleCharacter;
}

void
nsLookAndFeel::RefreshImpl()
{
    nsXPLookAndFeel::RefreshImpl();

    mDefaultFontCached = false;
    mButtonFontCached = false;
    mFieldFontCached = false;
    mMenuFontCached = false;

    g_object_unref(mStyle);
    mStyle = nullptr;

    InitWidget();
    InitLookAndFeel();
}

bool
nsLookAndFeel::GetEchoPasswordImpl() {
#ifdef MOZ_PLATFORM_MAEMO
    return true;
#else
    return false;
#endif
}
