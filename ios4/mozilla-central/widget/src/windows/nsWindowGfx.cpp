/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
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

/*
 * nsWindowGfx - Painting and aceleration.
 */

// XXX Future: this should really be a stand alone class stored as
// a member of nsWindow with getters and setters for things like render
// mode and methods for handling paint.

/**************************************************************
 **************************************************************
 **
 ** BLOCK: Includes
 **
 ** Include headers.
 **
 **************************************************************
 **************************************************************/

#ifdef MOZ_IPC
#include "mozilla/plugins/PluginInstanceParent.h"
using mozilla::plugins::PluginInstanceParent;
#endif

#include "nsWindowGfx.h"
#include <windows.h>
#include "nsIRegion.h"
#include "gfxImageSurface.h"
#include "gfxWindowsSurface.h"
#include "gfxWindowsPlatform.h"
#include "nsGfxCIID.h"
#include "gfxContext.h"
#include "nsIRenderingContext.h"
#include "nsIDeviceContext.h"
#include "prmem.h"

#include "LayerManagerOGL.h"
#include "BasicLayers.h"
#ifdef MOZ_ENABLE_D3D9_LAYER
#include "LayerManagerD3D9.h"
#endif

#ifndef WINCE
#include "nsUXThemeData.h"
#include "nsUXThemeConstants.h"
#endif

extern "C" {
#include "pixman.h"
}

using namespace mozilla::layers;

/**************************************************************
 **************************************************************
 **
 ** BLOCK: Variables
 **
 ** nsWindow Class static initializations and global variables.
 **
 **************************************************************
 **************************************************************/

/**************************************************************
 *
 * SECTION: nsWindow statics
 * 
 **************************************************************/

static nsAutoPtr<PRUint8>  sSharedSurfaceData;
static gfxIntSize          sSharedSurfaceSize;

/**************************************************************
 *
 * SECTION: global variables.
 *
 **************************************************************/

#ifdef CAIRO_HAS_DDRAW_SURFACE
// XXX Still need to handle clean-up!!
static LPDIRECTDRAW glpDD                         = NULL;
static LPDIRECTDRAWSURFACE glpDDPrimary           = NULL;
static LPDIRECTDRAWCLIPPER glpDDClipper           = NULL;
static LPDIRECTDRAWSURFACE glpDDSecondary         = NULL;
static nsAutoPtr<gfxDDrawSurface> gpDDSurf        = NULL;
static DDSURFACEDESC gDDSDSecondary;
#endif

static NS_DEFINE_CID(kRegionCID,                  NS_REGION_CID);
static NS_DEFINE_IID(kRenderingContextCID,        NS_RENDERING_CONTEXT_CID);

/**************************************************************
 **************************************************************
 **
 ** BLOCK: nsWindowGfx impl.
 **
 ** Misc. graphics related utilities.
 **
 **************************************************************
 **************************************************************/

static PRBool
IsRenderMode(gfxWindowsPlatform::RenderMode rmode)
{
  return gfxWindowsPlatform::GetPlatform()->GetRenderMode() == rmode;
}

nsIntRegion
nsWindowGfx::ConvertHRGNToRegion(HRGN aRgn)
{
  NS_ASSERTION(aRgn, "Don't pass NULL region here");

  nsIntRegion rgn;

  DWORD size = ::GetRegionData(aRgn, 0, NULL);
  nsAutoTArray<PRUint8,100> buffer;
  if (!buffer.SetLength(size))
    return rgn;

  RGNDATA* data = reinterpret_cast<RGNDATA*>(buffer.Elements());
  if (!::GetRegionData(aRgn, size, data))
    return rgn;

  if (data->rdh.nCount > MAX_RECTS_IN_REGION) {
    rgn = ToIntRect(data->rdh.rcBound);
    return rgn;
  }

  RECT* rects = reinterpret_cast<RECT*>(data->Buffer);
  for (PRUint32 i = 0; i < data->rdh.nCount; ++i) {
    RECT* r = rects + i;
    rgn.Or(rgn, ToIntRect(*r));
  }

  return rgn;
}

#ifdef CAIRO_HAS_DDRAW_SURFACE
PRBool
nsWindowGfx::InitDDraw()
{
  HRESULT hr;

  hr = DirectDrawCreate(NULL, &glpDD, NULL);
  NS_ENSURE_SUCCESS(hr, PR_FALSE);

  hr = glpDD->SetCooperativeLevel(NULL, DDSCL_NORMAL);
  NS_ENSURE_SUCCESS(hr, PR_FALSE);

  DDSURFACEDESC ddsd;
  memset(&ddsd, 0, sizeof(ddsd));
  ddsd.dwSize = sizeof(ddsd);
  ddsd.dwFlags = DDSD_CAPS;
  ddsd.ddpfPixelFormat.dwSize = sizeof(ddsd.ddpfPixelFormat);
  ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

  hr = glpDD->CreateSurface(&ddsd, &glpDDPrimary, NULL);
  NS_ENSURE_SUCCESS(hr, PR_FALSE);

  hr = glpDD->CreateClipper(0, &glpDDClipper, NULL);
  NS_ENSURE_SUCCESS(hr, PR_FALSE);

  hr = glpDDPrimary->SetClipper(glpDDClipper);
  NS_ENSURE_SUCCESS(hr, PR_FALSE);

  // We do not use the cairo ddraw surface for IMAGE_DDRAW16.  Instead, we
  // use an 24bpp image surface, convert that to 565, then blit using ddraw.
  if (!IsRenderMode(gfxWindowsPlatform::RENDER_IMAGE_DDRAW16)) {
    gfxIntSize screen_size(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    gpDDSurf = new gfxDDrawSurface(glpDD, screen_size, gfxASurface::ImageFormatRGB24);
    if (!gpDDSurf) {
      /*XXX*/
      fprintf(stderr, "couldn't create ddsurf\n");
      return PR_FALSE;
    }
  }

  return PR_TRUE;
}
#endif

/**************************************************************
 **************************************************************
 **
 ** BLOCK: nsWindow impl.
 **
 ** Paint related nsWindow methods.
 **
 **************************************************************
 **************************************************************/

void nsWindowGfx::OnSettingsChangeGfx(WPARAM wParam)
{
#if defined(WINCE_WINDOWS_MOBILE)
  if (wParam == SETTINGCHANGE_RESET) {
    if (glpDDSecondary) {
      glpDDSecondary->Release();
      glpDDSecondary = NULL;
    }

    if(glpDD)
      glpDD->RestoreAllSurfaces();
  }
#endif
}

// GetRegionToPaint returns the invalidated region that needs to be painted
// it's abstracted out because Windows XP/Vista/7 handles this for us, but
// we need to keep track of it our selves for Windows CE and Windows Mobile

nsIntRegion nsWindow::GetRegionToPaint(PRBool aForceFullRepaint,
                                       PAINTSTRUCT ps, HDC aDC)
{
  if (aForceFullRepaint) {
    RECT paintRect;
    ::GetClientRect(mWnd, &paintRect);
    nsIntRegion region(nsWindowGfx::ToIntRect(paintRect));
#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
    region.Sub(region, mCaptionButtonsRoundedRegion);
#endif
    return region;
  }

#if defined(WINCE_WINDOWS_MOBILE) || !defined(WINCE)
  HRGN paintRgn = ::CreateRectRgn(0, 0, 0, 0);
  if (paintRgn != NULL) {
# ifdef WINCE
    int result = GetUpdateRgn(mWnd, paintRgn, FALSE);
# else
    int result = GetRandomRgn(aDC, paintRgn, SYSRGN);
# endif
    if (result == 1) {
      POINT pt = {0,0};
      ::MapWindowPoints(NULL, mWnd, &pt, 1);
      ::OffsetRgn(paintRgn, pt.x, pt.y);
    }
    nsIntRegion rgn(nsWindowGfx::ConvertHRGNToRegion(paintRgn));
    ::DeleteObject(paintRgn);
# ifdef WINCE
    if (!rgn.IsEmpty())
      return rgn;
# elif MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
    rgn.Sub(rgn, mCaptionButtonsRoundedRegion);
    return rgn;
# else
    return rgn;
# endif
  }
#endif

  nsIntRegion region(nsWindowGfx::ToIntRect(ps.rcPaint));
#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
  region.Sub(region, mCaptionButtonsRoundedRegion);
#endif
  return region;
}

#define WORDSSIZE(x) ((x).width * (x).height)
static PRBool
EnsureSharedSurfaceSize(gfxIntSize size)
{
  gfxIntSize screenSize;
  screenSize.height = GetSystemMetrics(SM_CYSCREEN);
  screenSize.width = GetSystemMetrics(SM_CXSCREEN);

  if (WORDSSIZE(screenSize) > WORDSSIZE(size))
    size = screenSize;

  if (WORDSSIZE(screenSize) < WORDSSIZE(size))
    NS_WARNING("Trying to create a shared surface larger than the screen");

  if (!sSharedSurfaceData || (WORDSSIZE(size) > WORDSSIZE(sSharedSurfaceSize))) {
    sSharedSurfaceSize = size;
    sSharedSurfaceData = nsnull;
    sSharedSurfaceData = (PRUint8 *)malloc(WORDSSIZE(sSharedSurfaceSize) * 4);
  }

  return (sSharedSurfaceData != nsnull);
}

PRBool nsWindow::OnPaint(HDC aDC, PRUint32 aNestingLevel)
{
#ifdef MOZ_IPC
  // We never have reentrant paint events, except when we're running our RPC
  // windows event spin loop. If we don't trap for this, we'll try to paint,
  // but view manager will refuse to paint the surface, resulting is black
  // flashes on the plugin rendering surface.
  if (mozilla::ipc::RPCChannel::IsSpinLoopActive() && mPainting)
    return PR_FALSE;

  if (mWindowType == eWindowType_plugin) {

    /**
     * After we CallUpdateWindow to the child, occasionally a WM_PAINT message
     * is posted to the parent event loop with an empty update rect. Do a
     * dummy paint so that Windows stops dispatching WM_PAINT in an inifinite
     * loop. See bug 543788.
     */
    RECT updateRect;
    if (!GetUpdateRect(mWnd, &updateRect, FALSE) ||
        (updateRect.left == updateRect.right &&
         updateRect.top == updateRect.bottom)) {
      PAINTSTRUCT ps;
      BeginPaint(mWnd, &ps);
      EndPaint(mWnd, &ps);
      return PR_TRUE;
    }

    PluginInstanceParent* instance = reinterpret_cast<PluginInstanceParent*>(
      ::GetPropW(mWnd, L"PluginInstanceParentProperty"));
    if (instance) {
      instance->CallUpdateWindow();
      ValidateRect(mWnd, NULL);
      return PR_TRUE;
    }
  }
#endif

#ifdef MOZ_IPC
  // We never have reentrant paint events, except when we're running our RPC
  // windows event spin loop. If we don't trap for this, we'll try to paint,
  // but view manager will refuse to paint the surface, resulting is black
  // flashes on the plugin rendering surface.
  if (mozilla::ipc::RPCChannel::IsSpinLoopActive() && mPainting)
    return PR_FALSE;
#endif

  nsPaintEvent willPaintEvent(PR_TRUE, NS_WILL_PAINT, this);
  willPaintEvent.willSendDidPaint = PR_TRUE;
  DispatchWindowEvent(&willPaintEvent);

#ifdef CAIRO_HAS_DDRAW_SURFACE
  if (IsRenderMode(gfxWindowsPlatform::RENDER_IMAGE_DDRAW16)) {
    return OnPaintImageDDraw16();
  }
#endif

  PRBool result = PR_TRUE;
  PAINTSTRUCT ps;
  nsEventStatus eventStatus = nsEventStatus_eIgnore;

#ifdef MOZ_XUL
  if (!aDC && (eTransparencyTransparent == mTransparencyMode))
  {
    // For layered translucent windows all drawing should go to memory DC and no
    // WM_PAINT messages are normally generated. To support asynchronous painting
    // we force generation of WM_PAINT messages by invalidating window areas with
    // RedrawWindow, InvalidateRect or InvalidateRgn function calls.
    // BeginPaint/EndPaint must be called to make Windows think that invalid area
    // is painted. Otherwise it will continue sending the same message endlessly.
    ::BeginPaint(mWnd, &ps);
    ::EndPaint(mWnd, &ps);

    aDC = mMemoryDC;
  }
#endif

  mPainting = PR_TRUE;

#ifdef WIDGET_DEBUG_OUTPUT
  HRGN debugPaintFlashRegion = NULL;
  HDC debugPaintFlashDC = NULL;

  if (debug_WantPaintFlashing())
  {
    debugPaintFlashRegion = ::CreateRectRgn(0, 0, 0, 0);
    ::GetUpdateRgn(mWnd, debugPaintFlashRegion, TRUE);
    debugPaintFlashDC = ::GetDC(mWnd);
  }
#endif // WIDGET_DEBUG_OUTPUT

  HDC hDC = aDC ? aDC : (::BeginPaint(mWnd, &ps));
  if (!IsRenderMode(gfxWindowsPlatform::RENDER_DIRECT2D)) {
    mPaintDC = hDC;
  }

  // generate the event and call the event callback
  nsPaintEvent event(PR_TRUE, NS_PAINT, this);
  InitEvent(event);

#ifdef MOZ_XUL
  PRBool forceRepaint = aDC || (eTransparencyTransparent == mTransparencyMode);
#else
  PRBool forceRepaint = NULL != aDC;
#endif
  event.region = GetRegionToPaint(forceRepaint, ps, hDC);
  event.willSendDidPaint = PR_TRUE;

  if (!event.region.IsEmpty() && mEventCallback)
  {
    // Should probably pass in a real region here, using GetRandomRgn
    // http://msdn.microsoft.com/library/default.asp?url=/library/en-us/gdi/clipping_4q0e.asp

#ifdef WIDGET_DEBUG_OUTPUT
    debug_DumpPaintEvent(stdout,
                         this,
                         &event,
                         nsCAutoString("noname"),
                         (PRInt32) mWnd);
#endif // WIDGET_DEBUG_OUTPUT

    switch (GetLayerManager()->GetBackendType()) {
      case LayerManager::LAYERS_BASIC:
        {
          nsRefPtr<gfxASurface> targetSurface;

#if defined(MOZ_XUL)
          // don't support transparency for non-GDI rendering, for now
          if ((IsRenderMode(gfxWindowsPlatform::RENDER_GDI) ||
               IsRenderMode(gfxWindowsPlatform::RENDER_DIRECT2D)) &&
              eTransparencyTransparent == mTransparencyMode) {
            if (mTransparentSurface == nsnull)
              SetupTranslucentWindowMemoryBitmap(mTransparencyMode);
            targetSurface = mTransparentSurface;
          }
#endif

          nsRefPtr<gfxWindowsSurface> targetSurfaceWin;
          if (!targetSurface &&
              IsRenderMode(gfxWindowsPlatform::RENDER_GDI))
          {
            PRUint32 flags = (mTransparencyMode == eTransparencyOpaque) ? 0 :
                gfxWindowsSurface::FLAG_IS_TRANSPARENT;
            targetSurfaceWin = new gfxWindowsSurface(hDC, flags);
            targetSurface = targetSurfaceWin;
          }
#ifdef CAIRO_HAS_D2D_SURFACE
          if (!targetSurface &&
              IsRenderMode(gfxWindowsPlatform::RENDER_DIRECT2D))
          {
            if (!mD2DWindowSurface) {
              gfxASurface::gfxContentType content = gfxASurface::CONTENT_COLOR;
#if defined(MOZ_XUL)
              if (mTransparencyMode != eTransparencyOpaque) {
                content = gfxASurface::CONTENT_COLOR_ALPHA;
              }
#endif
              mD2DWindowSurface = new gfxD2DSurface(mWnd, content);
            }
            targetSurface = mD2DWindowSurface;
          }
#endif
#ifdef CAIRO_HAS_DDRAW_SURFACE
          nsRefPtr<gfxDDrawSurface> targetSurfaceDDraw;
          if (!targetSurface &&
              (IsRenderMode(gfxWindowsPlatform::RENDER_DDRAW) ||
               IsRenderMode(gfxWindowsPlatform::RENDER_DDRAW_GL)))
          {
            if (!glpDD) {
              if (!nsWindowGfx::InitDDraw()) {
                NS_WARNING("DirectDraw init failed; falling back to RENDER_IMAGE_STRETCH24");
                gfxWindowsPlatform::GetPlatform()->SetRenderMode(gfxWindowsPlatform::RENDER_IMAGE_STRETCH24);
                goto DDRAW_FAILED;
              }
            }

            // create a rect that maps the window in screen space
            // create a new sub-surface that aliases this one
            RECT winrect;
            GetClientRect(mWnd, &winrect);
            MapWindowPoints(mWnd, NULL, (LPPOINT)&winrect, 2);

            targetSurfaceDDraw = new gfxDDrawSurface(gpDDSurf.get(), winrect);
            targetSurface = targetSurfaceDDraw;
          }

DDRAW_FAILED:
#endif
          nsRefPtr<gfxImageSurface> targetSurfaceImage;
          if (!targetSurface &&
              (IsRenderMode(gfxWindowsPlatform::RENDER_IMAGE_STRETCH32) ||
               IsRenderMode(gfxWindowsPlatform::RENDER_IMAGE_STRETCH24)))
          {
            gfxIntSize surfaceSize(ps.rcPaint.right - ps.rcPaint.left,
                                   ps.rcPaint.bottom - ps.rcPaint.top);

            if (!EnsureSharedSurfaceSize(surfaceSize)) {
              NS_ERROR("Couldn't allocate a shared image surface!");
              return NS_ERROR_FAILURE;
            }

            // don't use the shared surface directly; instead, create a new one
            // that just reuses its buffer.
            targetSurfaceImage = new gfxImageSurface(sSharedSurfaceData.get(),
                                                     surfaceSize,
                                                     surfaceSize.width * 4,
                                                     gfxASurface::ImageFormatRGB24);

            if (targetSurfaceImage && !targetSurfaceImage->CairoStatus()) {
              targetSurfaceImage->SetDeviceOffset(gfxPoint(-ps.rcPaint.left, -ps.rcPaint.top));
              targetSurface = targetSurfaceImage;
            }
          }

          if (!targetSurface) {
            NS_ERROR("Invalid RenderMode!");
            return NS_ERROR_FAILURE;
          }

          nsRefPtr<gfxContext> thebesContext = new gfxContext(targetSurface);
          thebesContext->SetFlag(gfxContext::FLAG_DESTINED_FOR_SCREEN);
          if (IsRenderMode(gfxWindowsPlatform::RENDER_DIRECT2D)) {
            const nsIntRect* r;
            for (nsIntRegionRectIterator iter(event.region);
                 (r = iter.Next()) != nsnull;) {
              thebesContext->Rectangle(gfxRect(r->x, r->y, r->width, r->height), PR_TRUE);
            }
            thebesContext->Clip();
            thebesContext->SetOperator(gfxContext::OPERATOR_CLEAR);
            thebesContext->Paint();
            thebesContext->SetOperator(gfxContext::OPERATOR_OVER);
          }
#ifdef WINCE
          thebesContext->SetFlag(gfxContext::FLAG_SIMPLIFY_OPERATORS);
#endif

          // don't need to double buffer with anything but GDI
          BasicLayerManager::BufferMode doubleBuffering =
            BasicLayerManager::BUFFER_NONE;
          if (IsRenderMode(gfxWindowsPlatform::RENDER_GDI)) {
# if defined(MOZ_XUL) && !defined(WINCE)
            switch (mTransparencyMode) {
              case eTransparencyGlass:
              case eTransparencyBorderlessGlass:
              default:
                // If we're not doing translucency, then double buffer
                doubleBuffering = BasicLayerManager::BUFFER_BUFFERED;
                break;
              case eTransparencyTransparent:
                // If we're rendering with translucency, we're going to be
                // rendering the whole window; make sure we clear it first
                thebesContext->SetOperator(gfxContext::OPERATOR_CLEAR);
                thebesContext->Paint();
                thebesContext->SetOperator(gfxContext::OPERATOR_OVER);
                break;
            }
#else
            doubleBuffering = BasicLayerManager::BUFFER_BUFFERED;
#endif
          }

#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_LONGHORN
          if (IsRenderMode(gfxWindowsPlatform::RENDER_GDI) &&
              mTransparencyMode != eTransparencyTransparent &&
              !mCaptionButtons.IsEmpty()) {
            // The area behind the caption buttons need to have a
            // black background first to make the clipping work.
            RECT rect;
            rect.top = mCaptionButtons.y;
            rect.left = mCaptionButtons.x;
            rect.right = mCaptionButtons.x + mCaptionButtons.width;
            rect.bottom = mCaptionButtons.y + mCaptionButtons.height;
            FillRect(hDC, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));

            const nsIntRect* r;
            for (nsIntRegionRectIterator iter(event.region);
                 (r = iter.Next()) != nsnull;) {
              thebesContext->Rectangle(gfxRect(r->x, r->y, r->width, r->height), PR_TRUE);
            }
            thebesContext->Clip();
          }
#endif

          {
            AutoLayerManagerSetup
                setupLayerManager(this, thebesContext, doubleBuffering);
            result = DispatchWindowEvent(&event, eventStatus);
          }

#ifdef MOZ_XUL
          if ((IsRenderMode(gfxWindowsPlatform::RENDER_GDI) ||
               IsRenderMode(gfxWindowsPlatform::RENDER_DIRECT2D))&&
              eTransparencyTransparent == mTransparencyMode) {
            // Data from offscreen drawing surface was copied to memory bitmap of transparent
            // bitmap. Now it can be read from memory bitmap to apply alpha channel and after
            // that displayed on the screen.
            UpdateTranslucentWindow();
          } else
#endif
#ifdef CAIRO_HAS_D2D_SURFACE
          if (result) {
            if (mD2DWindowSurface) {
              mD2DWindowSurface->Present();
            }
          }
#endif
          if (result) {
            if (IsRenderMode(gfxWindowsPlatform::RENDER_DDRAW) ||
                       IsRenderMode(gfxWindowsPlatform::RENDER_DDRAW_GL))
            {
#ifdef CAIRO_HAS_DDRAW_SURFACE
              // blit with direct draw
              HRESULT hr = glpDDClipper->SetHWnd(0, mWnd);

#ifdef DEBUG
              if (FAILED(hr))
                DDError("SetHWnd", hr);
#endif

              // blt from the affected area from the window back-buffer to the
              // screen-relative coordinates of the window paint area
              RECT dst_rect = ps.rcPaint;
              MapWindowPoints(mWnd, NULL, (LPPOINT)&dst_rect, 2);
              hr = glpDDPrimary->Blt(&dst_rect,
                                     gpDDSurf->GetDDSurface(),
                                     &dst_rect,
                                     DDBLT_WAITNOTBUSY,
                                     NULL);
#ifdef DEBUG
              if (FAILED(hr))
                DDError("SetHWnd", hr);
#endif
#endif
            } else if (IsRenderMode(gfxWindowsPlatform::RENDER_IMAGE_STRETCH24) ||
                       IsRenderMode(gfxWindowsPlatform::RENDER_IMAGE_STRETCH32)) 
            {
              gfxIntSize surfaceSize = targetSurfaceImage->GetSize();

              // Just blit this directly
              BITMAPINFOHEADER bi;
              memset(&bi, 0, sizeof(BITMAPINFOHEADER));
              bi.biSize = sizeof(BITMAPINFOHEADER);
              bi.biWidth = surfaceSize.width;
              bi.biHeight = - surfaceSize.height;
              bi.biPlanes = 1;
              bi.biBitCount = 32;
              bi.biCompression = BI_RGB;

              if (IsRenderMode(gfxWindowsPlatform::RENDER_IMAGE_STRETCH24)) {
                // On Windows CE/Windows Mobile, 24bpp packed-pixel sources
                // seem to be far faster to blit than 32bpp (see bug 484864).
                // So, convert the bits to 24bpp by stripping out the unused
                // alpha byte.  24bpp DIBs also have scanlines that are 4-byte
                // aligned though, so that must be taken into account.
                int srcstride = surfaceSize.width*4;
                int dststride = surfaceSize.width*3;
                dststride = (dststride + 3) & ~3;

                // Convert in place
                for (int j = 0; j < surfaceSize.height; ++j) {
                  unsigned int *src = (unsigned int*) (targetSurfaceImage->Data() + j*srcstride);
                  unsigned int *dst = (unsigned int*) (targetSurfaceImage->Data() + j*dststride);

                  // go 4 pixels at a time, since each 4 pixels
                  // turns into 3 DWORDs when converted into BGR:
                  // BGRx BGRx BGRx BGRx -> BGRB GRBG RBGR
                  //
                  // However, since we're dealing with little-endian ints, this is actually:
                  // xRGB xrgb xRGB xrgb -> bRGB GBrg rgbR
                  int width_left = surfaceSize.width;
                  while (width_left >= 4) {
                    unsigned int a = *src++;
                    unsigned int b = *src++;
                    unsigned int c = *src++;
                    unsigned int d = *src++;

                    *dst++ =  (a & 0x00ffffff)        | (b << 24);
                    *dst++ = ((b & 0x00ffff00) >> 8)  | (c << 16);
                    *dst++ = ((c & 0x00ff0000) >> 16) | (d << 8);

                    width_left -= 4;
                  }

                  // then finish up whatever number of pixels are left,
                  // using bytes.
                  unsigned char *bsrc = (unsigned char*) src;
                  unsigned char *bdst = (unsigned char*) dst;
                  switch (width_left) {
                    case 3:
                      *bdst++ = *bsrc++;
                      *bdst++ = *bsrc++;
                      *bdst++ = *bsrc++;
                      bsrc++;
                    case 2:
                      *bdst++ = *bsrc++;
                      *bdst++ = *bsrc++;
                      *bdst++ = *bsrc++;
                      bsrc++;
                    case 1:
                      *bdst++ = *bsrc++;
                      *bdst++ = *bsrc++;
                      *bdst++ = *bsrc++;
                      bsrc++;
                    case 0:
                      break;
                  }
                }

                bi.biBitCount = 24;
              }

              StretchDIBits(hDC,
                            ps.rcPaint.left, ps.rcPaint.top,
                            surfaceSize.width, surfaceSize.height,
                            0, 0,
                            surfaceSize.width, surfaceSize.height,
                            targetSurfaceImage->Data(),
                            (BITMAPINFO*) &bi,
                            DIB_RGB_COLORS,
                            SRCCOPY);
            }
          }
        }
        break;
      case LayerManager::LAYERS_OPENGL:
        static_cast<mozilla::layers::LayerManagerOGL*>(GetLayerManager())->
          SetClippingRegion(event.region);
        result = DispatchWindowEvent(&event, eventStatus);
        break;
#ifdef MOZ_ENABLE_D3D9_LAYER
      case LayerManager::LAYERS_D3D9:
        static_cast<mozilla::layers::LayerManagerD3D9*>(GetLayerManager())->
          SetClippingRegion(event.region);
        result = DispatchWindowEvent(&event, eventStatus);
        break;
#endif
      default:
        NS_ERROR("Unknown layers backend used!");
        break;
    }
  }

  if (!aDC) {
    ::EndPaint(mWnd, &ps);
  }

  mPaintDC = nsnull;

#if defined(WIDGET_DEBUG_OUTPUT) && !defined(WINCE)
  if (debug_WantPaintFlashing())
  {
    // Only flash paint events which have not ignored the paint message.
    // Those that ignore the paint message aren't painting anything so there
    // is only the overhead of the dispatching the paint event.
    if (nsEventStatus_eIgnore != eventStatus) {
      ::InvertRgn(debugPaintFlashDC, debugPaintFlashRegion);
      PR_Sleep(PR_MillisecondsToInterval(30));
      ::InvertRgn(debugPaintFlashDC, debugPaintFlashRegion);
      PR_Sleep(PR_MillisecondsToInterval(30));
    }
    ::ReleaseDC(mWnd, debugPaintFlashDC);
    ::DeleteObject(debugPaintFlashRegion);
  }
#endif // WIDGET_DEBUG_OUTPUT && !WINCE

  mPainting = PR_FALSE;

  nsPaintEvent didPaintEvent(PR_TRUE, NS_DID_PAINT, this);
  DispatchWindowEvent(&didPaintEvent);

  if (aNestingLevel == 0 && ::GetUpdateRect(mWnd, NULL, PR_FALSE)) {
    OnPaint(aDC, 1);
  }

  return result;
}

nsresult nsWindowGfx::CreateIcon(imgIContainer *aContainer,
                                  PRBool aIsCursor,
                                  PRUint32 aHotspotX,
                                  PRUint32 aHotspotY,
                                  HICON *aIcon) {

  // Get the image data
  nsRefPtr<gfxImageSurface> frame;
  aContainer->CopyFrame(imgIContainer::FRAME_CURRENT,
                        imgIContainer::FLAG_SYNC_DECODE,
                        getter_AddRefs(frame));
  if (!frame)
    return NS_ERROR_NOT_AVAILABLE;

  PRUint8 *data = frame->Data();

  PRInt32 width = frame->Width();
  PRInt32 height = frame->Height();

  HBITMAP bmp = DataToBitmap(data, width, -height, 32);
  PRUint8* a1data = Data32BitTo1Bit(data, width, height);
  if (!a1data) {
    return NS_ERROR_FAILURE;
  }

  HBITMAP mbmp = DataToBitmap(a1data, width, -height, 1);
  PR_Free(a1data);

  ICONINFO info = {0};
  info.fIcon = !aIsCursor;
  info.xHotspot = aHotspotX;
  info.yHotspot = aHotspotY;
  info.hbmMask = mbmp;
  info.hbmColor = bmp;

  HCURSOR icon = ::CreateIconIndirect(&info);
  ::DeleteObject(mbmp);
  ::DeleteObject(bmp);
  if (!icon)
    return NS_ERROR_FAILURE;
  *aIcon = icon;
  return NS_OK;
}

// Adjust cursor image data
PRUint8* nsWindowGfx::Data32BitTo1Bit(PRUint8* aImageData,
                                      PRUint32 aWidth, PRUint32 aHeight)
{
  // We need (aWidth + 7) / 8 bytes plus zero-padding up to a multiple of
  // 4 bytes for each row (HBITMAP requirement). Bug 353553.
  PRUint32 outBpr = ((aWidth + 31) / 8) & ~3;

  // Allocate and clear mask buffer
  PRUint8* outData = (PRUint8*)PR_Calloc(outBpr, aHeight);
  if (!outData)
    return NULL;

  PRInt32 *imageRow = (PRInt32*)aImageData;
  for (PRUint32 curRow = 0; curRow < aHeight; curRow++) {
    PRUint8 *outRow = outData + curRow * outBpr;
    PRUint8 mask = 0x80;
    for (PRUint32 curCol = 0; curCol < aWidth; curCol++) {
      // Use sign bit to test for transparency, as alpha byte is highest byte
      if (*imageRow++ < 0)
        *outRow |= mask;

      mask >>= 1;
      if (!mask) {
        outRow ++;
        mask = 0x80;
      }
    }
  }

  return outData;
}

PRBool nsWindowGfx::IsCursorTranslucencySupported()
{
#ifdef WINCE
  return PR_FALSE;
#else
  static PRBool didCheck = PR_FALSE;
  static PRBool isSupported = PR_FALSE;
  if (!didCheck) {
    didCheck = PR_TRUE;
    // Cursor translucency is supported on Windows XP and newer
    isSupported = nsWindow::GetWindowsVersion() >= 0x501;
  }

  return isSupported;
#endif
}

/**
 * Convert the given image data to a HBITMAP. If the requested depth is
 * 32 bit and the OS supports translucency, a bitmap with an alpha channel
 * will be returned.
 *
 * @param aImageData The image data to convert. Must use the format accepted
 *                   by CreateDIBitmap.
 * @param aWidth     With of the bitmap, in pixels.
 * @param aHeight    Height of the image, in pixels.
 * @param aDepth     Image depth, in bits. Should be one of 1, 24 and 32.
 *
 * @return The HBITMAP representing the image. Caller should call
 *         DeleteObject when done with the bitmap.
 *         On failure, NULL will be returned.
 */
HBITMAP nsWindowGfx::DataToBitmap(PRUint8* aImageData,
                                  PRUint32 aWidth,
                                  PRUint32 aHeight,
                                  PRUint32 aDepth)
{
#ifndef WINCE
  HDC dc = ::GetDC(NULL);

  if (aDepth == 32 && IsCursorTranslucencySupported()) {
    // Alpha channel. We need the new header.
    BITMAPV4HEADER head = { 0 };
    head.bV4Size = sizeof(head);
    head.bV4Width = aWidth;
    head.bV4Height = aHeight;
    head.bV4Planes = 1;
    head.bV4BitCount = aDepth;
    head.bV4V4Compression = BI_BITFIELDS;
    head.bV4SizeImage = 0; // Uncompressed
    head.bV4XPelsPerMeter = 0;
    head.bV4YPelsPerMeter = 0;
    head.bV4ClrUsed = 0;
    head.bV4ClrImportant = 0;

    head.bV4RedMask   = 0x00FF0000;
    head.bV4GreenMask = 0x0000FF00;
    head.bV4BlueMask  = 0x000000FF;
    head.bV4AlphaMask = 0xFF000000;

    HBITMAP bmp = ::CreateDIBitmap(dc,
                                   reinterpret_cast<CONST BITMAPINFOHEADER*>(&head),
                                   CBM_INIT,
                                   aImageData,
                                   reinterpret_cast<CONST BITMAPINFO*>(&head),
                                   DIB_RGB_COLORS);
    ::ReleaseDC(NULL, dc);
    return bmp;
  }

  char reserved_space[sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * 2];
  BITMAPINFOHEADER& head = *(BITMAPINFOHEADER*)reserved_space;

  head.biSize = sizeof(BITMAPINFOHEADER);
  head.biWidth = aWidth;
  head.biHeight = aHeight;
  head.biPlanes = 1;
  head.biBitCount = (WORD)aDepth;
  head.biCompression = BI_RGB;
  head.biSizeImage = 0; // Uncompressed
  head.biXPelsPerMeter = 0;
  head.biYPelsPerMeter = 0;
  head.biClrUsed = 0;
  head.biClrImportant = 0;
  
  BITMAPINFO& bi = *(BITMAPINFO*)reserved_space;

  if (aDepth == 1) {
    RGBQUAD black = { 0, 0, 0, 0 };
    RGBQUAD white = { 255, 255, 255, 0 };

    bi.bmiColors[0] = white;
    bi.bmiColors[1] = black;
  }

  HBITMAP bmp = ::CreateDIBitmap(dc, &head, CBM_INIT, aImageData, &bi, DIB_RGB_COLORS);
  ::ReleaseDC(NULL, dc);
  return bmp;
#else
  return nsnull;
#endif
}


// Windows Mobile Special image/direct draw painting fun
#if defined(CAIRO_HAS_DDRAW_SURFACE)
PRBool nsWindow::OnPaintImageDDraw16()
{
  PRBool result = PR_FALSE;
  PAINTSTRUCT ps;
  nsPaintEvent event(PR_TRUE, NS_PAINT, this);
  gfxIntSize surfaceSize;
  nsRefPtr<gfxImageSurface> targetSurfaceImage;
  nsRefPtr<gfxContext> thebesContext;
  nsEventStatus eventStatus = nsEventStatus_eIgnore;
  gfxIntSize newSize;
  newSize.height = GetSystemMetrics(SM_CYSCREEN);
  newSize.width = GetSystemMetrics(SM_CXSCREEN);
  mPainting = PR_TRUE;

  HDC hDC = ::BeginPaint(mWnd, &ps);
  mPaintDC = hDC;
  nsIntRegion paintRgn = GetRegionToPaint(PR_FALSE, ps, hDC);

  if (paintRgn.IsEmpty() || !mEventCallback) {
    result = PR_TRUE;
    goto cleanup;
  }

  InitEvent(event);
  
  if (!glpDD) {
    if (!nsWindowGfx::InitDDraw()) {
      NS_WARNING("DirectDraw init failed.  Giving up.");
      goto cleanup;
    }
  }

  if (!glpDDSecondary) {

    memset(&gDDSDSecondary, 0, sizeof (gDDSDSecondary));
    memset(&gDDSDSecondary.ddpfPixelFormat, 0, sizeof(gDDSDSecondary.ddpfPixelFormat));
    
    gDDSDSecondary.dwSize = sizeof (gDDSDSecondary);
    gDDSDSecondary.ddpfPixelFormat.dwSize = sizeof(gDDSDSecondary.ddpfPixelFormat);
    
    gDDSDSecondary.dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;

    gDDSDSecondary.dwHeight = newSize.height;
    gDDSDSecondary.dwWidth  = newSize.width;

    gDDSDSecondary.ddpfPixelFormat.dwFlags = DDPF_RGB;
    gDDSDSecondary.ddpfPixelFormat.dwRGBBitCount = 16;
    gDDSDSecondary.ddpfPixelFormat.dwRBitMask = 0xf800;
    gDDSDSecondary.ddpfPixelFormat.dwGBitMask = 0x07e0;
    gDDSDSecondary.ddpfPixelFormat.dwBBitMask = 0x001f;
    
    HRESULT hr = glpDD->CreateSurface(&gDDSDSecondary, &glpDDSecondary, 0);
    if (FAILED(hr)) {
#ifdef DEBUG
      DDError("CreateSurface renderer", hr);
#endif
      goto cleanup;
    }
  }

  PRInt32 brx = paintRgn.GetBounds().x;
  PRInt32 bry = paintRgn.GetBounds().y;
  PRInt32 brw = paintRgn.GetBounds().width;
  PRInt32 brh = paintRgn.GetBounds().height;
  surfaceSize = gfxIntSize(brw, brh);
  
  if (!EnsureSharedSurfaceSize(surfaceSize))
    goto cleanup;

  targetSurfaceImage = new gfxImageSurface(sSharedSurfaceData.get(),
                                           surfaceSize,
                                           surfaceSize.width * 4,
                                           gfxASurface::ImageFormatRGB24);
    
  if (!targetSurfaceImage || targetSurfaceImage->CairoStatus())
    goto cleanup;
    
  targetSurfaceImage->SetDeviceOffset(gfxPoint(-brx, -bry));
  
  thebesContext = new gfxContext(targetSurfaceImage);
  thebesContext->SetFlag(gfxContext::FLAG_DESTINED_FOR_SCREEN);
  thebesContext->SetFlag(gfxContext::FLAG_SIMPLIFY_OPERATORS);
    
  {
    AutoLayerManagerSetup setupLayerManager(this, thebesContext);
    event.region = paintRgn;
    result = DispatchWindowEvent(&event, eventStatus);
  }
  
  if (!result && eventStatus  == nsEventStatus_eConsumeNoDefault)
    goto cleanup;

  HRESULT hr = glpDDSecondary->Lock(0, &gDDSDSecondary, DDLOCK_WAITNOTBUSY | DDLOCK_DISCARD, 0); 
  if (FAILED(hr))
    goto cleanup;

  pixman_image_t *srcPixmanImage = 
    pixman_image_create_bits(PIXMAN_x8r8g8b8, surfaceSize.width,
                             surfaceSize.height, 
                             (uint32_t*) sSharedSurfaceData.get(),
                             surfaceSize.width * 4);
  
  pixman_image_t *dstPixmanImage = 
    pixman_image_create_bits(PIXMAN_r5g6b5, gDDSDSecondary.dwWidth,
                             gDDSDSecondary.dwHeight,
                             (uint32_t*) gDDSDSecondary.lpSurface,
                             gDDSDSecondary.dwWidth * 2);
  

  const nsIntRect* r;
  for (nsIntRegionRectIterator iter(paintRgn);
       (r = iter.Next()) != nsnull;) {
    pixman_image_composite(PIXMAN_OP_SRC, srcPixmanImage, NULL, dstPixmanImage,
                           r->x - brx, r->y - bry,
                           0, 0,
                           r->x, r->y,
                           r->width, r->height);
  }
  
  pixman_image_unref(dstPixmanImage);
  pixman_image_unref(srcPixmanImage);

  hr = glpDDSecondary->Unlock(0);
  if (FAILED(hr))
    goto cleanup;
  
  hr = glpDDClipper->SetHWnd(0, mWnd);
  if (FAILED(hr))
    goto cleanup;
  
  for (nsIntRegionRectIterator iter(paintRgn);
       (r = iter.Next()) != nsnull;) {
    RECT wr = { r->x, r->y, r->XMost(), r->YMost() };
    RECT renderRect = wr;
    SetLastError(0); // See http://msdn.microsoft.com/en-us/library/dd145046%28VS.85%29.aspx
    MapWindowPoints(mWnd, 0, (LPPOINT)&renderRect, 2);
    hr = glpDDPrimary->Blt(&renderRect, glpDDSecondary, &wr, 0, NULL);
    if (FAILED(hr)) {
      NS_ERROR("this blt should never fail!");
      printf("#### %s blt failed: %08lx", __FUNCTION__, hr);
    }
  }
  result = PR_TRUE;

cleanup:
  NS_ASSERTION(result == PR_TRUE, "fatal drawing error");
  ::EndPaint(mWnd, &ps);
  mPaintDC = nsnull;
  mPainting = PR_FALSE;
  return result;

}
#endif // defined(CAIRO_HAS_DDRAW_SURFACE)
