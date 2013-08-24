/* vim: se cin sw=2 ts=2 et : */
/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
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
 * The Original Code is the Mozilla browser.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Rob Arnold <robarnold@mozilla.com> (Original Author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "nsUXThemeData.h"
#include "nsDebug.h"
// For GetWindowsVersion
#include "nsWindow.h"
#include "nsUXThemeConstants.h"

const PRUnichar
nsUXThemeData::kThemeLibraryName[] = L"uxtheme.dll";
#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
const PRUnichar
nsUXThemeData::kDwmLibraryName[] = L"dwmapi.dll";
#endif

HANDLE
nsUXThemeData::sThemes[eUXNumClasses];

HMODULE
nsUXThemeData::sThemeDLL = NULL;
#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
HMODULE
nsUXThemeData::sDwmDLL = NULL;
#endif

BOOL
nsUXThemeData::sFlatMenus = FALSE;
PRPackedBool
nsUXThemeData::sIsXPOrLater = PR_FALSE;
PRPackedBool
nsUXThemeData::sIsVistaOrLater = PR_FALSE;
PRPackedBool
nsUXThemeData::sHaveCompositor = PR_FALSE;

PRBool nsUXThemeData::sTitlebarInfoPopulated = PR_FALSE;
SIZE nsUXThemeData::sCommandButtons[3];

nsUXThemeData::OpenThemeDataPtr nsUXThemeData::openTheme = NULL;
nsUXThemeData::CloseThemeDataPtr nsUXThemeData::closeTheme = NULL;
nsUXThemeData::DrawThemeBackgroundPtr nsUXThemeData::drawThemeBG = NULL;
nsUXThemeData::DrawThemeEdgePtr nsUXThemeData::drawThemeEdge = NULL;
nsUXThemeData::GetThemeContentRectPtr nsUXThemeData::getThemeContentRect = NULL;
nsUXThemeData::GetThemeBackgroundRegionPtr nsUXThemeData::getThemeBackgroundRegion = NULL;
nsUXThemeData::GetThemePartSizePtr nsUXThemeData::getThemePartSize = NULL;
nsUXThemeData::GetThemeSysFontPtr nsUXThemeData::getThemeSysFont = NULL;
nsUXThemeData::GetThemeColorPtr nsUXThemeData::getThemeColor = NULL;
nsUXThemeData::GetThemeMarginsPtr nsUXThemeData::getThemeMargins = NULL;
nsUXThemeData::IsAppThemedPtr nsUXThemeData::isAppThemed = NULL;
nsUXThemeData::GetCurrentThemeNamePtr nsUXThemeData::getCurrentThemeName = NULL;
nsUXThemeData::GetThemeSysColorPtr nsUXThemeData::getThemeSysColor = NULL;
nsUXThemeData::IsThemeBackgroundPartiallyTransparentPtr nsUXThemeData::isThemeBackgroundPartiallyTransparent = NULL;

#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
nsUXThemeData::DwmExtendFrameIntoClientAreaProc nsUXThemeData::dwmExtendFrameIntoClientAreaPtr = NULL;
nsUXThemeData::DwmIsCompositionEnabledProc nsUXThemeData::dwmIsCompositionEnabledPtr = NULL;
nsUXThemeData::DwmSetIconicThumbnailProc nsUXThemeData::dwmSetIconicThumbnailPtr = NULL;
nsUXThemeData::DwmSetIconicLivePreviewBitmapProc nsUXThemeData::dwmSetIconicLivePreviewBitmapPtr = NULL;
nsUXThemeData::DwmGetWindowAttributeProc nsUXThemeData::dwmGetWindowAttributePtr = NULL;
nsUXThemeData::DwmSetWindowAttributeProc nsUXThemeData::dwmSetWindowAttributePtr = NULL;
nsUXThemeData::DwmInvalidateIconicBitmapsProc nsUXThemeData::dwmInvalidateIconicBitmapsPtr = NULL;
nsUXThemeData::DwmDefWindowProcProc nsUXThemeData::dwmDwmDefWindowProcPtr = NULL;
#endif

void
nsUXThemeData::Teardown() {
  Invalidate();
  if(sThemeDLL)
    FreeLibrary(sThemeDLL);
#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
  if(sDwmDLL)
    FreeLibrary(sDwmDLL);
#endif
}

void
nsUXThemeData::Initialize()
{
  ::ZeroMemory(sThemes, sizeof(sThemes));
  NS_ASSERTION(!sThemeDLL, "nsUXThemeData being initialized twice!");

  PRInt32 version = nsWindow::GetWindowsVersion();
  sIsXPOrLater = version >= WINXP_VERSION;
  sIsVistaOrLater = version >= VISTA_VERSION;

  if (GetThemeDLL()) {
    openTheme = (OpenThemeDataPtr)GetProcAddress(sThemeDLL, "OpenThemeData");
    closeTheme = (CloseThemeDataPtr)GetProcAddress(sThemeDLL, "CloseThemeData");
    drawThemeBG = (DrawThemeBackgroundPtr)GetProcAddress(sThemeDLL, "DrawThemeBackground");
    drawThemeEdge = (DrawThemeEdgePtr)GetProcAddress(sThemeDLL, "DrawThemeEdge");
    getThemeContentRect = (GetThemeContentRectPtr)GetProcAddress(sThemeDLL, "GetThemeBackgroundContentRect");
    getThemeBackgroundRegion = (GetThemeBackgroundRegionPtr)GetProcAddress(sThemeDLL, "GetThemeBackgroundRegion");
    getThemePartSize = (GetThemePartSizePtr)GetProcAddress(sThemeDLL, "GetThemePartSize");
    getThemeSysFont = (GetThemeSysFontPtr)GetProcAddress(sThemeDLL, "GetThemeSysFont");
    getThemeColor = (GetThemeColorPtr)GetProcAddress(sThemeDLL, "GetThemeColor");
    getThemeMargins = (GetThemeMarginsPtr)GetProcAddress(sThemeDLL, "GetThemeMargins");
    isAppThemed = (IsAppThemedPtr)GetProcAddress(sThemeDLL, "IsAppThemed");
    getCurrentThemeName = (GetCurrentThemeNamePtr)GetProcAddress(sThemeDLL, "GetCurrentThemeName");
    getThemeSysColor = (GetThemeSysColorPtr)GetProcAddress(sThemeDLL, "GetThemeSysColor");
    isThemeBackgroundPartiallyTransparent = (IsThemeBackgroundPartiallyTransparentPtr)GetProcAddress(sThemeDLL, "IsThemeBackgroundPartiallyTransparent");
  }
#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
  if (GetDwmDLL()) {
    dwmExtendFrameIntoClientAreaPtr = (DwmExtendFrameIntoClientAreaProc)::GetProcAddress(sDwmDLL, "DwmExtendFrameIntoClientArea");
    dwmIsCompositionEnabledPtr = (DwmIsCompositionEnabledProc)::GetProcAddress(sDwmDLL, "DwmIsCompositionEnabled");
    dwmSetIconicThumbnailPtr = (DwmSetIconicThumbnailProc)::GetProcAddress(sDwmDLL, "DwmSetIconicThumbnail");
    dwmSetIconicLivePreviewBitmapPtr = (DwmSetIconicLivePreviewBitmapProc)::GetProcAddress(sDwmDLL, "DwmSetIconicLivePreviewBitmap");
    dwmGetWindowAttributePtr = (DwmGetWindowAttributeProc)::GetProcAddress(sDwmDLL, "DwmGetWindowAttribute");
    dwmSetWindowAttributePtr = (DwmSetWindowAttributeProc)::GetProcAddress(sDwmDLL, "DwmSetWindowAttribute");
    dwmInvalidateIconicBitmapsPtr = (DwmInvalidateIconicBitmapsProc)::GetProcAddress(sDwmDLL, "DwmInvalidateIconicBitmaps");
    dwmDwmDefWindowProcPtr = (DwmDefWindowProcProc)::GetProcAddress(sDwmDLL, "DwmDefWindowProc");
    CheckForCompositor();
  }
#endif

  Invalidate();
}

void
nsUXThemeData::Invalidate() {
  for(int i = 0; i < eUXNumClasses; i++) {
    if(sThemes[i]) {
      closeTheme(sThemes[i]);
      sThemes[i] = NULL;
    }
  }
  if (sIsXPOrLater) {
    BOOL useFlat = PR_FALSE;
    sFlatMenus = ::SystemParametersInfo(SPI_GETFLATMENU, 0, &useFlat, 0) ?
                     useFlat : PR_FALSE;
  } else {
    // Contrary to Microsoft's documentation, SPI_GETFLATMENU will not fail
    // on Windows 2000, and it is also possible (though unlikely) for WIN2K
    // to be misconfigured in such a way that it would return true, so we
    // shall give WIN2K special treatment
    sFlatMenus = PR_FALSE;
  }
  // Refresh titlebar button info
  sTitlebarInfoPopulated = PR_FALSE;
}

HANDLE
nsUXThemeData::GetTheme(nsUXThemeClass cls) {
  NS_ASSERTION(cls < eUXNumClasses, "Invalid theme class!");
  if(!sThemeDLL)
    return NULL;
  if(!sThemes[cls])
  {
    sThemes[cls] = openTheme(NULL, GetClassName(cls));
  }
  return sThemes[cls];
}

HMODULE
nsUXThemeData::GetThemeDLL() {
  if (!sThemeDLL && sIsXPOrLater)
    sThemeDLL = ::LoadLibraryW(kThemeLibraryName);
  return sThemeDLL;
}

#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
HMODULE
nsUXThemeData::GetDwmDLL() {
  if (!sDwmDLL && sIsVistaOrLater)
    sDwmDLL = ::LoadLibraryW(kDwmLibraryName);
  return sDwmDLL;
}
#endif

const wchar_t *nsUXThemeData::GetClassName(nsUXThemeClass cls) {
  switch(cls) {
    case eUXButton:
      return L"Button";
    case eUXEdit:
      return L"Edit";
    case eUXTooltip:
      return L"Tooltip";
    case eUXRebar:
      return L"Rebar";
    case eUXMediaRebar:
      return L"Media::Rebar";
    case eUXCommunicationsRebar:
      return L"Communications::Rebar";
    case eUXBrowserTabBarRebar:
      return L"BrowserTabBar::Rebar";
    case eUXToolbar:
      return L"Toolbar";
    case eUXMediaToolbar:
      return L"Media::Toolbar";
    case eUXCommunicationsToolbar:
      return L"Communications::Toolbar";
    case eUXProgress:
      return L"Progress";
    case eUXTab:
      return L"Tab";
    case eUXScrollbar:
      return L"Scrollbar";
    case eUXTrackbar:
      return L"Trackbar";
    case eUXSpin:
      return L"Spin";
    case eUXStatus:
      return L"Status";
    case eUXCombobox:
      return L"Combobox";
    case eUXHeader:
      return L"Header";
    case eUXListview:
      return L"Listview";
    case eUXMenu:
      return L"Menu";
    case eUXWindowFrame:
      return L"Window";
    default:
      NS_NOTREACHED("unknown uxtheme class");
      return L"";
  }
}

// static
void
nsUXThemeData::InitTitlebarInfo()
{
  // Pre-populate with generic metrics. These likley will not match
  // the current theme, but they insure the buttons at least show up.
  sCommandButtons[0].cx = GetSystemMetrics(SM_CXSIZE);
  sCommandButtons[0].cy = GetSystemMetrics(SM_CYSIZE);
  sCommandButtons[1].cx = sCommandButtons[2].cx = sCommandButtons[0].cx;
  sCommandButtons[1].cy = sCommandButtons[2].cy = sCommandButtons[0].cy;

  // Use system metrics for pre-vista
  if (nsWindow::GetWindowsVersion() < VISTA_VERSION)
    sTitlebarInfoPopulated = PR_TRUE;
}

// static
void
nsUXThemeData::UpdateTitlebarInfo(HWND aWnd)
{
  if (sTitlebarInfoPopulated || !aWnd)
    return;

  // Compositor enabled, we won't use these.
#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
  if (nsUXThemeData::CheckForCompositor()) {
    sTitlebarInfoPopulated = PR_TRUE;
    return;
  }
#endif

  // Query a temporary, visible window with command buttons to get
  // the right metrics. 
  WNDCLASSW wc;
  wc.style         = 0;
  wc.lpfnWndProc   = ::DefWindowProcW;
  wc.cbClsExtra    = 0;
  wc.cbWndExtra    = 0;
  wc.hInstance     = nsToolkit::mDllInstance;
  wc.hIcon         = NULL;
  wc.hCursor       = NULL;
  wc.hbrBackground = NULL;
  wc.lpszMenuName  = NULL;
  wc.lpszClassName = kClassNameTemp;
  ::RegisterClassW(&wc);

  // Create a transparent, descendent of the window passed in. This
  // keeps the window from showing up on the desktop or the taskbar.
  // Note the parent (browser) window is usually still hidden, we
  // don't want to display it, so we can't query it directly.
  HWND hWnd = CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_LAYERED,
                              kClassNameTemp, L"",
                              WS_OVERLAPPEDWINDOW,
                              0, 0, 0, 0, aWnd, NULL,
                              nsToolkit::mDllInstance, NULL);
  NS_ASSERTION(hWnd, "UpdateTitlebarInfo window creation failed.");

  ShowWindow(hWnd, SW_SHOW);
  TITLEBARINFOEX info = {0};
  info.cbSize = sizeof(TITLEBARINFOEX);
  SendMessage(hWnd, WM_GETTITLEBARINFOEX, 0, (LPARAM)&info); 
  DestroyWindow(hWnd);

  // Only set if we have valid data for all three buttons we use.
  if ((info.rgrect[2].right - info.rgrect[2].left) == 0 ||
      (info.rgrect[3].right - info.rgrect[3].left) == 0 ||
      (info.rgrect[5].right - info.rgrect[5].left) == 0) {
    NS_WARNING("WM_GETTITLEBARINFOEX query failed to find usable metrics.");
    return;
  }
  // minimize
  sCommandButtons[0].cx = info.rgrect[2].right - info.rgrect[2].left;
  sCommandButtons[0].cy = info.rgrect[2].bottom - info.rgrect[2].top;
  // maximize/restore
  sCommandButtons[1].cx = info.rgrect[3].right - info.rgrect[3].left;
  sCommandButtons[1].cy = info.rgrect[3].bottom - info.rgrect[3].top;
  // close
  sCommandButtons[2].cx = info.rgrect[5].right - info.rgrect[5].left;
  sCommandButtons[2].cy = info.rgrect[5].bottom - info.rgrect[5].top;

  sTitlebarInfoPopulated = PR_TRUE;
}
