/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
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
 * The Initial Developer of the Original Code is Christopher Blizzard
 * <blizzard@mozilla.org>.  Portions created by the Initial Developer
 * are Copyright (C) 2001 the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#include "mozilla/ModuleUtils.h"
#include "nsWidgetsCID.h"
#include "nsAppShell.h"
#include "nsAppShellSingleton.h"
#include "nsBaseWidget.h"
#include "nsLookAndFeel.h"
#include "nsWindow.h"
#include "nsTransferable.h"
#include "nsHTMLFormatConverter.h"
#ifdef MOZ_X11
#include "nsClipboardHelper.h"
#include "nsClipboard.h"
#include "nsDragService.h"
#endif
#include "nsFilePicker.h"
#include "nsSound.h"
#include "nsBidiKeyboard.h"
#include "nsNativeKeyBindings.h"
#include "nsScreenManagerGtk.h"

#ifdef NS_PRINTING
#include "nsPrintOptionsGTK.h"
#include "nsPrintSession.h"
#include "nsDeviceContextSpecG.h"
#endif

#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsImageToPixbuf.h"
#include "nsPrintDialogGTK.h"

#if defined(MOZ_X11)
#include "nsIdleServiceGTK.h"
#endif

#ifdef NATIVE_THEME_SUPPORT
#include "nsNativeThemeGTK.h"
#endif

#include "nsIComponentRegistrar.h"
#include "nsComponentManagerUtils.h"
#include "nsAutoPtr.h"
#include <gtk/gtk.h>

/* from nsFilePicker.js */
#define XULFILEPICKER_CID \
  { 0x54ae32f8, 0x1dd2, 0x11b2, \
    { 0xa2, 0x09, 0xdf, 0x7c, 0x50, 0x53, 0x70, 0xf8} }
static NS_DEFINE_CID(kXULFilePickerCID, XULFILEPICKER_CID);
static NS_DEFINE_CID(kNativeFilePickerCID, NS_FILEPICKER_CID);

NS_GENERIC_FACTORY_CONSTRUCTOR(nsWindow)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsChildWindow)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsLookAndFeel)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsTransferable)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsBidiKeyboard)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsHTMLFormatConverter)
#ifdef MOZ_X11
NS_GENERIC_FACTORY_CONSTRUCTOR(nsClipboardHelper)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsClipboard, Init)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsDragService)
#endif
NS_GENERIC_FACTORY_CONSTRUCTOR(nsSound)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsScreenManagerGtk)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsImageToPixbuf)


#ifdef NATIVE_THEME_SUPPORT
// from nsWindow.cpp
extern PRBool gDisableNativeTheme;

static nsresult
nsNativeThemeGTKConstructor(nsISupports *aOuter, REFNSIID aIID,
                            void **aResult)
{
    nsresult rv;
    nsNativeThemeGTK * inst;

    if (gDisableNativeTheme)
        return NS_ERROR_NO_INTERFACE;

    *aResult = NULL;
    if (NULL != aOuter) {
        rv = NS_ERROR_NO_AGGREGATION;
        return rv;
    }

    inst = new nsNativeThemeGTK();
    if (NULL == inst) {
        rv = NS_ERROR_OUT_OF_MEMORY;
        return rv;
    }
    NS_ADDREF(inst);
    rv = inst->QueryInterface(aIID, aResult);
    NS_RELEASE(inst);

    return rv;
}
#endif

#if defined(MOZ_X11)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsIdleServiceGTK)
#endif

#ifdef NS_PRINTING
NS_GENERIC_FACTORY_CONSTRUCTOR(nsDeviceContextSpecGTK)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsPrintOptionsGTK, Init)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsPrinterEnumeratorGTK)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsPrintSession, Init)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsPrintDialogServiceGTK, Init)
#endif

static nsresult
nsFilePickerConstructor(nsISupports *aOuter, REFNSIID aIID,
                        void **aResult)
{
  *aResult = nsnull;
  if (aOuter != nsnull) {
    return NS_ERROR_NO_AGGREGATION;
  }

  PRBool allowPlatformPicker = PR_TRUE;
  nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
  if (prefs) {
    PRBool prefAllow;
    nsresult rv = prefs->GetBoolPref("ui.allow_platform_file_picker",
                                     &prefAllow);
    if (NS_SUCCEEDED(rv)) {
        allowPlatformPicker = prefAllow;
    }
  }
  
  nsCOMPtr<nsIFilePicker> picker;
  if (allowPlatformPicker && gtk_check_version(2,6,3) == NULL) {
      picker = new nsFilePicker;
  } else {
    picker = do_CreateInstance(kXULFilePickerCID);
  }

  if (!picker) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return picker->QueryInterface(aIID, aResult);
}

static nsresult
nsNativeKeyBindingsConstructor(nsISupports *aOuter, REFNSIID aIID,
                               void **aResult,
                               NativeKeyBindingsType aKeyBindingsType)
{
    nsresult rv;

    nsNativeKeyBindings *inst;

    *aResult = NULL;
    if (NULL != aOuter) {
        rv = NS_ERROR_NO_AGGREGATION;
        return rv;
    }

    inst = new nsNativeKeyBindings();
    if (NULL == inst) {
        rv = NS_ERROR_OUT_OF_MEMORY;
        return rv;
    }
    NS_ADDREF(inst);
    inst->Init(aKeyBindingsType);
    rv = inst->QueryInterface(aIID, aResult);
    NS_RELEASE(inst);

    return rv;
}

static nsresult
nsNativeKeyBindingsInputConstructor(nsISupports *aOuter, REFNSIID aIID,
                                    void **aResult)
{
    return nsNativeKeyBindingsConstructor(aOuter, aIID, aResult,
                                          eKeyBindings_Input);
}

static nsresult
nsNativeKeyBindingsTextAreaConstructor(nsISupports *aOuter, REFNSIID aIID,
                                       void **aResult)
{
    return nsNativeKeyBindingsConstructor(aOuter, aIID, aResult,
                                          eKeyBindings_TextArea);
}

NS_DEFINE_NAMED_CID(NS_WINDOW_CID);
NS_DEFINE_NAMED_CID(NS_CHILD_CID);
NS_DEFINE_NAMED_CID(NS_APPSHELL_CID);
NS_DEFINE_NAMED_CID(NS_LOOKANDFEEL_CID);
NS_DEFINE_NAMED_CID(NS_FILEPICKER_CID);
NS_DEFINE_NAMED_CID(NS_SOUND_CID);
NS_DEFINE_NAMED_CID(NS_TRANSFERABLE_CID);
#ifdef MOZ_X11
NS_DEFINE_NAMED_CID(NS_CLIPBOARD_CID);
NS_DEFINE_NAMED_CID(NS_CLIPBOARDHELPER_CID);
NS_DEFINE_NAMED_CID(NS_DRAGSERVICE_CID);
#endif
NS_DEFINE_NAMED_CID(NS_HTMLFORMATCONVERTER_CID);
NS_DEFINE_NAMED_CID(NS_BIDIKEYBOARD_CID);
NS_DEFINE_NAMED_CID(NS_NATIVEKEYBINDINGSINPUT_CID);
NS_DEFINE_NAMED_CID(NS_NATIVEKEYBINDINGSTEXTAREA_CID);
NS_DEFINE_NAMED_CID(NS_NATIVEKEYBINDINGSEDITOR_CID);
NS_DEFINE_NAMED_CID(NS_SCREENMANAGER_CID);
#ifdef NATIVE_THEME_SUPPORT
NS_DEFINE_NAMED_CID(NS_THEMERENDERER_CID);
#endif
#ifdef NS_PRINTING
NS_DEFINE_NAMED_CID(NS_PRINTSETTINGSSERVICE_CID);
NS_DEFINE_NAMED_CID(NS_PRINTER_ENUMERATOR_CID);
NS_DEFINE_NAMED_CID(NS_PRINTSESSION_CID);
NS_DEFINE_NAMED_CID(NS_DEVICE_CONTEXT_SPEC_CID);
NS_DEFINE_NAMED_CID(NS_PRINTDIALOGSERVICE_CID);
#endif 
NS_DEFINE_NAMED_CID(NS_IMAGE_TO_PIXBUF_CID);
#if defined(MOZ_X11)
NS_DEFINE_NAMED_CID(NS_IDLE_SERVICE_CID);
#endif


static const mozilla::Module::CIDEntry kWidgetCIDs[] = {
    { &kNS_WINDOW_CID, false, NULL, nsWindowConstructor },
    { &kNS_CHILD_CID, false, NULL, nsChildWindowConstructor },
    { &kNS_APPSHELL_CID, false, NULL, nsAppShellConstructor },
    { &kNS_LOOKANDFEEL_CID, false, NULL, nsLookAndFeelConstructor },
    { &kNS_FILEPICKER_CID, false, NULL, nsFilePickerConstructor },
    { &kNS_SOUND_CID, false, NULL, nsSoundConstructor },
    { &kNS_TRANSFERABLE_CID, false, NULL, nsTransferableConstructor },
#ifdef MOZ_X11
    { &kNS_CLIPBOARD_CID, false, NULL, nsClipboardConstructor },
    { &kNS_CLIPBOARDHELPER_CID, false, NULL, nsClipboardHelperConstructor },
    { &kNS_DRAGSERVICE_CID, false, NULL, nsDragServiceConstructor },
#endif
    { &kNS_HTMLFORMATCONVERTER_CID, false, NULL, nsHTMLFormatConverterConstructor },
    { &kNS_BIDIKEYBOARD_CID, false, NULL, nsBidiKeyboardConstructor },
    { &kNS_NATIVEKEYBINDINGSINPUT_CID, false, NULL, nsNativeKeyBindingsInputConstructor },
    { &kNS_NATIVEKEYBINDINGSTEXTAREA_CID, false, NULL, nsNativeKeyBindingsTextAreaConstructor },
    { &kNS_NATIVEKEYBINDINGSEDITOR_CID, false, NULL, nsNativeKeyBindingsTextAreaConstructor },
    { &kNS_SCREENMANAGER_CID, false, NULL, nsScreenManagerGtkConstructor },
#ifdef NATIVE_THEME_SUPPORT
    { &kNS_THEMERENDERER_CID, false, NULL, nsNativeThemeGTKConstructor },
#endif
#ifdef NS_PRINTING
    { &kNS_PRINTSETTINGSSERVICE_CID, false, NULL, nsPrintOptionsGTKConstructor },
    { &kNS_PRINTER_ENUMERATOR_CID, false, NULL, nsPrinterEnumeratorGTKConstructor },
    { &kNS_PRINTSESSION_CID, false, NULL, nsPrintSessionConstructor },
    { &kNS_DEVICE_CONTEXT_SPEC_CID, false, NULL, nsDeviceContextSpecGTKConstructor },
    { &kNS_PRINTDIALOGSERVICE_CID, false, NULL, nsPrintDialogServiceGTKConstructor },
#endif 
    { &kNS_IMAGE_TO_PIXBUF_CID, false, NULL, nsImageToPixbufConstructor },
#if defined(MOZ_X11)
    { &kNS_IDLE_SERVICE_CID, false, NULL, nsIdleServiceGTKConstructor },
#endif
    { NULL }
};

static const mozilla::Module::ContractIDEntry kWidgetContracts[] = {
    { "@mozilla.org/widget/window/gtk;1", &kNS_WINDOW_CID },
    { "@mozilla.org/widgets/child_window/gtk;1", &kNS_CHILD_CID },
    { "@mozilla.org/widget/appshell/gtk;1", &kNS_APPSHELL_CID },
    { "@mozilla.org/widget/lookandfeel;1", &kNS_LOOKANDFEEL_CID },
    { "@mozilla.org/filepicker;1", &kNS_FILEPICKER_CID },
    { "@mozilla.org/sound;1", &kNS_SOUND_CID },
    { "@mozilla.org/widget/transferable;1", &kNS_TRANSFERABLE_CID },
#ifdef MOZ_X11
    { "@mozilla.org/widget/clipboard;1", &kNS_CLIPBOARD_CID },
    { "@mozilla.org/widget/clipboardhelper;1", &kNS_CLIPBOARDHELPER_CID },
    { "@mozilla.org/widget/dragservice;1", &kNS_DRAGSERVICE_CID },
#endif
    { "@mozilla.org/widget/htmlformatconverter;1", &kNS_HTMLFORMATCONVERTER_CID },
    { "@mozilla.org/widget/bidikeyboard;1", &kNS_BIDIKEYBOARD_CID },
    { NS_NATIVEKEYBINDINGSINPUT_CONTRACTID, &kNS_NATIVEKEYBINDINGSINPUT_CID },
    { NS_NATIVEKEYBINDINGSTEXTAREA_CONTRACTID, &kNS_NATIVEKEYBINDINGSTEXTAREA_CID },
    { NS_NATIVEKEYBINDINGSEDITOR_CONTRACTID, &kNS_NATIVEKEYBINDINGSEDITOR_CID },
    { "@mozilla.org/gfx/screenmanager;1", &kNS_SCREENMANAGER_CID },
#ifdef NATIVE_THEME_SUPPORT
    { "@mozilla.org/chrome/chrome-native-theme;1", &kNS_THEMERENDERER_CID },
#endif
#ifdef NS_PRINTING
    { "@mozilla.org/gfx/printsettings-service;1", &kNS_PRINTSETTINGSSERVICE_CID },
    { "@mozilla.org/gfx/printerenumerator;1", &kNS_PRINTER_ENUMERATOR_CID },
    { "@mozilla.org/gfx/printsession;1", &kNS_PRINTSESSION_CID },
    { "@mozilla.org/gfx/devicecontextspec;1", &kNS_DEVICE_CONTEXT_SPEC_CID },
    { NS_PRINTDIALOGSERVICE_CONTRACTID, &kNS_PRINTDIALOGSERVICE_CID },
#endif 
    { "@mozilla.org/widget/image-to-gdk-pixbuf;1", &kNS_IMAGE_TO_PIXBUF_CID },
#if defined(MOZ_X11)
    { "@mozilla.org/widget/idleservice;1", &kNS_IDLE_SERVICE_CID },
#endif
    { NULL }
};

static void
nsWidgetGtk2ModuleDtor()
{
  nsFilePicker::Shutdown();
  nsSound::Shutdown();
  nsWindow::ReleaseGlobals();
  nsAppShellShutdown();
}

static const mozilla::Module kWidgetModule = {
    mozilla::Module::kVersion,
    kWidgetCIDs,
    kWidgetContracts,
    NULL,
    NULL,
    nsAppShellInit,
    nsWidgetGtk2ModuleDtor
};

NSMODULE_DEFN(nsWidgetGtk2Module) = &kWidgetModule;
