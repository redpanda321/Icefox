/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
// vim:set ts=2 sts=2 sw=2 et cin:
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
 * The Original Code is Mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   bgirard <b56girard@gmail.com>
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

#ifndef nsCoreAnimationSupport_h__
#define nsCoreAnimationSupport_h__
#ifdef XP_MACOSX

#import "ApplicationServices/ApplicationServices.h"
#include "nscore.h"
#include "gfxTypes.h"

// Get the system color space.
CGColorSpaceRef THEBES_API CreateSystemColorSpace();

// Manages a CARenderer
struct _CGLPBufferObject;
struct _CGLContextObject;
class nsIOSurface;

class THEBES_API nsCARenderer {
public:
  nsCARenderer() : mCARenderer(nsnull), mPixelBuffer(nsnull), mOpenGLContext(nsnull),
                   mCGImage(nsnull), mCGData(nsnull), mIOSurface(nsnull), mFBO(nsnull),
                   mIOTexture(nsnull) {}
  ~nsCARenderer();
  nsresult SetupRenderer(void* aCALayer, int aWidth, int aHeight);
  nsresult Render(int aWidth, int aHeight, CGImageRef *aOutCAImage);
  bool isInit() { return mCARenderer != nsnull; }
  /*
   * Render the CALayer to an IOSurface. If no IOSurface
   * is attached then an internal pixel buffer will be
   * used.
   */ 
  void AttachIOSurface(nsIOSurface *aSurface);
  static nsresult DrawSurfaceToCGContext(CGContextRef aContext, 
                                         nsIOSurface *surf, 
                                         CGColorSpaceRef aColorSpace, 
                                         int aX, int aY,
                                         int aWidth, int aHeight);
private:
  void Destroy();

  void *mCARenderer;
  _CGLPBufferObject *mPixelBuffer;
  _CGLContextObject *mOpenGLContext;
  CGImageRef         mCGImage;
  void              *mCGData;
  nsIOSurface       *mIOSurface;
  uint32_t           mFBO;
  uint32_t           mIOTexture;
};

typedef uint32_t IOSurfaceID;

class THEBES_API nsIOSurface {
public:
  static nsIOSurface *CreateIOSurface(int aWidth, int aHeight); 
  static void ReleaseIOSurface(nsIOSurface *aIOSurface); 
  static nsIOSurface *LookupSurface(IOSurfaceID aSurfaceID);

  nsIOSurface(CFTypeRef aIOSurfacePtr) : mIOSurfacePtr(aIOSurfacePtr) {}
  ~nsIOSurface() { CFRelease(mIOSurfacePtr); }
  IOSurfaceID GetIOSurfaceID();
  void *GetBaseAddress();
  size_t GetWidth();
  size_t GetHeight();
  size_t GetBytesPerRow();
  void Lock();
  void Unlock();
private:
  friend class nsCARenderer;
  CFTypeRef mIOSurfacePtr;
};

#endif // XP_MACOSX
#endif // nsCoreAnimationSupport_h__

