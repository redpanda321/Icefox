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
 *   Benoit Girard <b56girard@gmail.com>
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

#include "nsCoreAnimationSupport.h"
#include "nsDebug.h"

#import <QuartzCore/QuartzCore.h>
#include <dlfcn.h>

#define IOSURFACE_FRAMEWORK_PATH \
  "/System/Library/Frameworks/IOSurface.framework/IOSurface"
#define OPENGL_FRAMEWORK_PATH \
  "/System/Library/Frameworks/OpenGL.framework/OpenGL"


// IOSurface signatures
typedef CFTypeRef IOSurfacePtr;
typedef IOSurfacePtr (*IOSurfaceCreateFunc) (CFDictionaryRef properties);
typedef IOSurfacePtr (*IOSurfaceLookupFunc) (uint32_t io_surface_id);
typedef IOSurfaceID (*IOSurfaceGetIDFunc) (CFTypeRef io_surface);
typedef IOReturn (*IOSurfaceLockFunc) (CFTypeRef io_surface, 
                                       uint32_t options, 
                                       uint32_t *seed);
typedef IOReturn (*IOSurfaceUnlockFunc) (CFTypeRef io_surface, 
                                         uint32_t options, 
                                         uint32_t *seed);
typedef void* (*IOSurfaceGetBaseAddressFunc) (CFTypeRef io_surface);
typedef size_t (*IOSurfaceGetWidthFunc) (IOSurfacePtr io_surface);
typedef size_t (*IOSurfaceGetHeightFunc) (IOSurfacePtr io_surface);
typedef size_t (*IOSurfaceGetBytesPerRowFunc) (IOSurfacePtr io_surface);
typedef CGLError (*CGLTexImageIOSurface2DFunc) (CGLContextObj ctxt,
                             GLenum target, GLenum internalFormat,
                             GLsizei width, GLsizei height,
                             GLenum format, GLenum type,
                             IOSurfacePtr ioSurface, GLuint plane);

#define GET_CONST(const_name) \
  ((CFStringRef*) dlsym(sIOSurfaceFramework, const_name))
#define GET_IOSYM(dest,sym_name) \
  (typeof(dest)) dlsym(sIOSurfaceFramework, sym_name)
#define GET_CGLSYM(dest,sym_name) \
  (typeof(dest)) dlsym(sOpenGLFramework, sym_name)

class nsIOSurfaceLib: public nsIOSurface {
public:
  static void                        *sIOSurfaceFramework;
  static void                        *sOpenGLFramework;
  static bool                         isLoaded;
  static IOSurfaceCreateFunc          sCreate;
  static IOSurfaceGetIDFunc           sGetID;
  static IOSurfaceLookupFunc          sLookup;
  static IOSurfaceGetBaseAddressFunc  sGetBaseAddress;
  static IOSurfaceLockFunc            sLock;
  static IOSurfaceUnlockFunc          sUnlock;
  static IOSurfaceGetWidthFunc        sWidth;
  static IOSurfaceGetHeightFunc       sHeight;
  static IOSurfaceGetBytesPerRowFunc  sBytesPerRow;
  static CGLTexImageIOSurface2DFunc   sTexImage;
  static CFStringRef                  kPropWidth;
  static CFStringRef                  kPropHeight;
  static CFStringRef                  kPropBytesPerElem;
  static CFStringRef                  kPropBytesPerRow;
  static CFStringRef                  kPropIsGlobal;

  static bool isInit();
  static CFStringRef GetIOConst(const char* symbole);
  static IOSurfacePtr IOSurfaceCreate(CFDictionaryRef properties);
  static IOSurfacePtr IOSurfaceLookup(IOSurfaceID aIOSurfaceID);
  static IOSurfaceID  IOSurfaceGetID(IOSurfacePtr aIOSurfacePtr);
  static void        *IOSurfaceGetBaseAddress(IOSurfacePtr aIOSurfacePtr);
  static size_t       IOSurfaceGetWidth(IOSurfacePtr aIOSurfacePtr);
  static size_t       IOSurfaceGetHeight(IOSurfacePtr aIOSurfacePtr);
  static size_t       IOSurfaceGetBytesPerRow(IOSurfacePtr aIOSurfacePtr);
  static IOReturn     IOSurfaceLock(IOSurfacePtr aIOSurfacePtr, 
                                    uint32_t options, uint32_t *seed);
  static IOReturn     IOSurfaceUnlock(IOSurfacePtr aIOSurfacePtr, 
                                      uint32_t options, uint32_t *seed);
  static CGLError     CGLTexImageIOSurface2D(CGLContextObj ctxt,
                             GLenum target, GLenum internalFormat,
                             GLsizei width, GLsizei height,
                             GLenum format, GLenum type,
                             IOSurfacePtr ioSurface, GLuint plane);
  static void LoadLibrary();
  static void CloseLibrary();

  // Static deconstructor
  static class LibraryUnloader {
  public:
    ~LibraryUnloader() {
      CloseLibrary();
    }
  } sLibraryUnloader;
};

nsIOSurfaceLib::LibraryUnloader nsIOSurfaceLib::sLibraryUnloader;
bool                          nsIOSurfaceLib::isLoaded = false;
void*                         nsIOSurfaceLib::sIOSurfaceFramework;
void*                         nsIOSurfaceLib::sOpenGLFramework;
IOSurfaceCreateFunc           nsIOSurfaceLib::sCreate;
IOSurfaceGetIDFunc            nsIOSurfaceLib::sGetID;
IOSurfaceLookupFunc           nsIOSurfaceLib::sLookup;
IOSurfaceGetBaseAddressFunc   nsIOSurfaceLib::sGetBaseAddress;
IOSurfaceGetHeightFunc        nsIOSurfaceLib::sWidth;
IOSurfaceGetWidthFunc         nsIOSurfaceLib::sHeight;
IOSurfaceGetBytesPerRowFunc   nsIOSurfaceLib::sBytesPerRow;
IOSurfaceLockFunc             nsIOSurfaceLib::sLock;
IOSurfaceUnlockFunc           nsIOSurfaceLib::sUnlock;
CGLTexImageIOSurface2DFunc    nsIOSurfaceLib::sTexImage;
CFStringRef                   nsIOSurfaceLib::kPropWidth;
CFStringRef                   nsIOSurfaceLib::kPropHeight;
CFStringRef                   nsIOSurfaceLib::kPropBytesPerElem;
CFStringRef                   nsIOSurfaceLib::kPropBytesPerRow;
CFStringRef                   nsIOSurfaceLib::kPropIsGlobal;

bool nsIOSurfaceLib::isInit() {
  // Guard against trying to reload the library
  // if it is not available.
  if (!isLoaded)
    LoadLibrary();
  if (!sIOSurfaceFramework) {
    NS_ERROR("nsIOSurfaceLib failed to initialize");
  }
  return sIOSurfaceFramework;
}

IOSurfacePtr nsIOSurfaceLib::IOSurfaceCreate(CFDictionaryRef properties) {
  return sCreate(properties);
}

IOSurfacePtr nsIOSurfaceLib::IOSurfaceLookup(IOSurfaceID aIOSurfaceID) {
  return sLookup(aIOSurfaceID);
}

IOSurfaceID nsIOSurfaceLib::IOSurfaceGetID(IOSurfacePtr aIOSurfacePtr) {
  return sGetID(aIOSurfacePtr);
}

void* nsIOSurfaceLib::IOSurfaceGetBaseAddress(IOSurfacePtr aIOSurfacePtr) {
  return sGetBaseAddress(aIOSurfacePtr);
}

size_t nsIOSurfaceLib::IOSurfaceGetWidth(IOSurfacePtr aIOSurfacePtr) {
  return sWidth(aIOSurfacePtr);
}

size_t nsIOSurfaceLib::IOSurfaceGetHeight(IOSurfacePtr aIOSurfacePtr) {
  return sHeight(aIOSurfacePtr);
}

size_t nsIOSurfaceLib::IOSurfaceGetBytesPerRow(IOSurfacePtr aIOSurfacePtr) {
  return sBytesPerRow(aIOSurfacePtr);
}

IOReturn nsIOSurfaceLib::IOSurfaceLock(IOSurfacePtr aIOSurfacePtr, 
                                       uint32_t options, uint32_t *seed) {
  return sLock(aIOSurfacePtr, options, seed);
}

IOReturn nsIOSurfaceLib::IOSurfaceUnlock(IOSurfacePtr aIOSurfacePtr, 
                                         uint32_t options, uint32_t *seed) {
  return sUnlock(aIOSurfacePtr, options, seed);
}

CGLError nsIOSurfaceLib::CGLTexImageIOSurface2D(CGLContextObj ctxt,
                             GLenum target, GLenum internalFormat,
                             GLsizei width, GLsizei height,
                             GLenum format, GLenum type,
                             IOSurfacePtr ioSurface, GLuint plane) {
  return sTexImage(ctxt, target, internalFormat, width, height, 
                   format, type, ioSurface, plane);
}

CFStringRef nsIOSurfaceLib::GetIOConst(const char* symbole) {
  CFStringRef *address = (CFStringRef*)dlsym(sIOSurfaceFramework, symbole);
  if (!address)
    return nsnull;

  return *address;
}

void nsIOSurfaceLib::LoadLibrary() {
  if (isLoaded) {
    return;
  } 
  isLoaded = true;
  sIOSurfaceFramework = dlopen(IOSURFACE_FRAMEWORK_PATH, 
                            RTLD_LAZY | RTLD_LOCAL);
  sOpenGLFramework = dlopen(OPENGL_FRAMEWORK_PATH, 
                            RTLD_LAZY | RTLD_LOCAL);
  if (!sIOSurfaceFramework) {
    return;
  }
  if (!sOpenGLFramework) {
    dlclose(sIOSurfaceFramework);
    sIOSurfaceFramework = nsnull;
    return;
  }

  kPropWidth = GetIOConst("kIOSurfaceWidth");
  kPropHeight = GetIOConst("kIOSurfaceHeight");
  kPropBytesPerElem = GetIOConst("kIOSurfaceBytesPerElement");
  kPropBytesPerRow = GetIOConst("kIOSurfaceBytesPerRow");
  kPropIsGlobal = GetIOConst("kIOSurfaceIsGlobal");
  sCreate = GET_IOSYM(sCreate, "IOSurfaceCreate");
  sGetID  = GET_IOSYM(sGetID,  "IOSurfaceGetID");
  sWidth = GET_IOSYM(sWidth, "IOSurfaceGetWidth");
  sHeight = GET_IOSYM(sHeight, "IOSurfaceGetHeight");
  sBytesPerRow = GET_IOSYM(sBytesPerRow, "IOSurfaceGetBytesPerRow");
  sLookup = GET_IOSYM(sLookup, "IOSurfaceLookup");
  sLock = GET_IOSYM(sLock, "IOSurfaceLock");
  sUnlock = GET_IOSYM(sUnlock, "IOSurfaceUnlock");
  sGetBaseAddress = GET_IOSYM(sGetBaseAddress, "IOSurfaceGetBaseAddress");
  sTexImage = GET_CGLSYM(sTexImage, "CGLTexImageIOSurface2D");

  if (!sCreate || !sGetID || !sLookup || !sTexImage || !sGetBaseAddress ||
      !kPropWidth || !kPropHeight || !kPropBytesPerElem || !kPropIsGlobal ||
      !sLock || !sUnlock || !sWidth || !sHeight || !kPropBytesPerRow ||
      !sBytesPerRow) {
    CloseLibrary();
  }
}

void nsIOSurfaceLib::CloseLibrary() {
  if (sIOSurfaceFramework) {
    dlclose(sIOSurfaceFramework);
  }
  if (sOpenGLFramework) {
    dlclose(sOpenGLFramework);
  }
  sIOSurfaceFramework = nsnull;
  sOpenGLFramework = nsnull;
}

nsIOSurface* nsIOSurface::CreateIOSurface(int aWidth, int aHeight) { 
  if (!nsIOSurfaceLib::isInit())
    return nsnull;

  CFMutableDictionaryRef props = ::CFDictionaryCreateMutable(
                      kCFAllocatorDefault, 4,
                      &kCFTypeDictionaryKeyCallBacks,
                      &kCFTypeDictionaryValueCallBacks);
  if (!props)
    return nsnull;

  int32_t bytesPerElem = 4;
  CFNumberRef cfWidth = ::CFNumberCreate(NULL, kCFNumberSInt32Type, &aWidth);
  CFNumberRef cfHeight = ::CFNumberCreate(NULL, kCFNumberSInt32Type, &aHeight);
  CFNumberRef cfBytesPerElem = ::CFNumberCreate(NULL, kCFNumberSInt32Type, &bytesPerElem);
  ::CFDictionaryAddValue(props, nsIOSurfaceLib::kPropWidth,
                                cfWidth);
  ::CFRelease(cfWidth);
  ::CFDictionaryAddValue(props, nsIOSurfaceLib::kPropHeight,
                                cfHeight);
  ::CFRelease(cfHeight);
  ::CFDictionaryAddValue(props, nsIOSurfaceLib::kPropBytesPerElem, 
                                cfBytesPerElem);
  ::CFRelease(cfBytesPerElem);
  ::CFDictionaryAddValue(props, nsIOSurfaceLib::kPropIsGlobal, 
                                kCFBooleanTrue);

  IOSurfacePtr surfaceRef = nsIOSurfaceLib::IOSurfaceCreate(props);
  ::CFRelease(props);

  if (!surfaceRef)
    return nsnull;

  nsIOSurface* ioSurface = new nsIOSurface(surfaceRef);
  if (!ioSurface) {
    ::CFRelease(surfaceRef);
    return nsnull;
  }

  return ioSurface;
}

nsIOSurface* nsIOSurface::LookupSurface(IOSurfaceID aIOSurfaceID) { 
  if (!nsIOSurfaceLib::isInit())
    return nsnull;

  IOSurfacePtr surfaceRef = nsIOSurfaceLib::IOSurfaceLookup(aIOSurfaceID);
  if (!surfaceRef)
    return nsnull;
  // IOSurfaceLookup does not retain the object for us,
  // we want IOSurfacePtr to remain for the lifetime of
  // nsIOSurface.
  CFRetain(surfaceRef);

  nsIOSurface* ioSurface = new nsIOSurface(surfaceRef);
  if (!ioSurface) {
    ::CFRelease(ioSurface);
    return nsnull;
  }
  return ioSurface;
}

IOSurfaceID nsIOSurface::GetIOSurfaceID() { 
  return nsIOSurfaceLib::IOSurfaceGetID(mIOSurfacePtr);
}

void* nsIOSurface::GetBaseAddress() { 
  return nsIOSurfaceLib::IOSurfaceGetBaseAddress(mIOSurfacePtr);
}

size_t nsIOSurface::GetWidth() { 
  return nsIOSurfaceLib::IOSurfaceGetWidth(mIOSurfacePtr);
}

size_t nsIOSurface::GetHeight() { 
  return nsIOSurfaceLib::IOSurfaceGetHeight(mIOSurfacePtr);
}

size_t nsIOSurface::GetBytesPerRow() { 
  return nsIOSurfaceLib::IOSurfaceGetBytesPerRow(mIOSurfacePtr);
}

#define READ_ONLY 0x1
void nsIOSurface::Lock() {
  nsIOSurfaceLib::IOSurfaceLock(mIOSurfacePtr, READ_ONLY, NULL);
}

void nsIOSurface::Unlock() {
  nsIOSurfaceLib::IOSurfaceUnlock(mIOSurfacePtr, READ_ONLY, NULL);
}

nsCARenderer::~nsCARenderer() {
  Destroy();
}

CGColorSpaceRef CreateSystemColorSpace() {
    CMProfileRef system_profile = nsnull;
    CGColorSpaceRef cspace = nsnull;

    if (::CMGetSystemProfile(&system_profile) == noErr) {
      // Create a colorspace with the systems profile
      cspace = ::CGColorSpaceCreateWithPlatformColorSpace(system_profile);
      ::CMCloseProfile(system_profile);
    } else {
      // Default to generic
      cspace = ::CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
    }

    return cspace;
}

void cgdata_release_callback(void *aCGData, const void *data, size_t size) {
  if (aCGData) {
    free(aCGData);
  }
}

void nsCARenderer::Destroy() {
  if (mCARenderer) {
    CARenderer* caRenderer = (CARenderer*)mCARenderer;
    // Bug 556453:
    // Explicitly remove the layer from the renderer
    // otherwise it does not always happen right away.
    caRenderer.layer = nsnull;
    [caRenderer release];
  }
  if (mPixelBuffer) {
    ::CGLDestroyPBuffer((CGLPBufferObj)mPixelBuffer);
  }
  if (mOpenGLContext) {
    if (mFBO || mIOTexture) {
      // Release these resources with the context that allocated them
      CGLContextObj oldContext = ::CGLGetCurrentContext();
      ::CGLSetCurrentContext(mOpenGLContext);

      if (mIOTexture) {
        ::glDeleteTextures(1, &mIOTexture);
      }
      if (mFBO) {
        ::glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
        ::glDeleteFramebuffersEXT(1, &mFBO);
      }

      if (oldContext)
        ::CGLSetCurrentContext(oldContext);
    }
    ::CGLDestroyContext((CGLContextObj)mOpenGLContext);
  }
  if (mCGImage) {
    ::CGImageRelease(mCGImage);
  }
  if (mIOSurface) {
    delete mIOSurface;
  }
  // mCGData is deallocated by cgdata_release_callback

  mCARenderer = nil;
  mPixelBuffer = nsnull;
  mOpenGLContext = nsnull;
  mCGImage = nsnull;
  mIOSurface = nsnull;
  mFBO = nsnull;
  mIOTexture = nsnull;
}

nsresult nsCARenderer::SetupRenderer(void *aCALayer, int aWidth, int aHeight) {
  if (aWidth == 0 || aHeight == 0)
    return NS_ERROR_FAILURE;

  CALayer* layer = (CALayer*)aCALayer;
  CARenderer* caRenderer = nsnull;

  CGLPixelFormatAttribute attributes[] = {
    kCGLPFANoRecovery,
    kCGLPFAAccelerated,
    kCGLPFAPBuffer,
    kCGLPFADepthSize, (CGLPixelFormatAttribute)24,
    (CGLPixelFormatAttribute)0
  };

  if (!mIOSurface) {
    CGLError result = ::CGLCreatePBuffer(aWidth, aHeight,
                         GL_TEXTURE_2D, GL_RGBA, 0, &mPixelBuffer);
    if (result != kCGLNoError) {
      Destroy();
      return NS_ERROR_FAILURE;
    }
  }

  GLint screen;
  CGLPixelFormatObj format;
  if (::CGLChoosePixelFormat(attributes, &format, &screen) != kCGLNoError) {
    Destroy();
    return NS_ERROR_FAILURE;
  }

  if (::CGLCreateContext(format, nsnull, &mOpenGLContext) != kCGLNoError) {
    Destroy();
    return NS_ERROR_FAILURE;
  }
  ::CGLDestroyPixelFormat(format);

  caRenderer = [[CARenderer rendererWithCGLContext:mOpenGLContext 
                            options:nil] retain];
  mCARenderer = caRenderer;
  if (caRenderer == nil) {
    Destroy();
    return NS_ERROR_FAILURE;
  }
  [layer setBounds:CGRectMake(0, 0, aWidth, aHeight)];
  [layer setPosition:CGPointMake(aWidth/2.0, aHeight/2.0)];
  caRenderer.layer = layer;
  caRenderer.bounds = CGRectMake(0, 0, aWidth, aHeight);

  // We either target rendering to a CGImage or IOSurface.
  if (!mIOSurface) {
    mCGData = malloc(aWidth*aHeight*4);
    if (!mCGData) {
      Destroy();
    }
    memset(mCGData, 0, aWidth*aHeight*4);

    CGDataProviderRef dataProvider = nsnull;
    dataProvider = ::CGDataProviderCreateWithData(mCGData,
                                        mCGData, aHeight*aWidth*4, 
                                        cgdata_release_callback);
    if (!dataProvider) {
      cgdata_release_callback(mCGData, mCGData, aHeight*aWidth*4);
      Destroy();
      return NS_ERROR_FAILURE;
    }

    CGColorSpaceRef colorSpace = CreateSystemColorSpace();

    mCGImage = ::CGImageCreate(aWidth, aHeight, 8, 32, aWidth * 4, colorSpace, 
                kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host,
                dataProvider, NULL, true, kCGRenderingIntentDefault);

    ::CGDataProviderRelease(dataProvider);
    if (colorSpace) {
      ::CGColorSpaceRelease(colorSpace);
    }
    if (!mCGImage) {
      Destroy();
      return NS_ERROR_FAILURE;
    }
  } else {
    CGLContextObj oldContext = ::CGLGetCurrentContext();
    ::CGLSetCurrentContext(mOpenGLContext);

    // Create the IOSurface mapped texture.
    ::glGenTextures(1, &mIOTexture);
    ::glBindTexture(GL_TEXTURE_RECTANGLE_ARB, mIOTexture);
    ::glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ::glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    nsIOSurfaceLib::CGLTexImageIOSurface2D(mOpenGLContext, GL_TEXTURE_RECTANGLE_ARB,
                                           GL_RGBA, aWidth, aHeight,
                                           GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 
                                           mIOSurface->mIOSurfacePtr, 0);
    ::glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);

    // Create the fbo
    ::glGenFramebuffersEXT(1, &mFBO);
    ::glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, mFBO);
    ::glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
                                GL_TEXTURE_RECTANGLE_ARB, mIOTexture, 0);

    // Make sure that the Framebuffer configuration is supported on the client machine
    GLenum fboStatus;
    fboStatus = ::glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
    if (fboStatus != GL_FRAMEBUFFER_COMPLETE_EXT) {
      NS_ERROR("FBO not supported");
      if (oldContext)
        ::CGLSetCurrentContext(oldContext);
      Destroy();
      return NS_ERROR_FAILURE; 
    }

    if (oldContext)
      ::CGLSetCurrentContext(oldContext);
  }

  CGLContextObj oldContext = ::CGLGetCurrentContext();
  ::CGLSetCurrentContext(mOpenGLContext);

  ::glViewport(0.0, 0.0, aWidth, aHeight);
  ::glMatrixMode(GL_PROJECTION);
  ::glLoadIdentity();
  ::glOrtho (0.0, aWidth, 0.0, aHeight, -1, 1);

  // Render upside down to speed up CGContextDrawImage
  ::glTranslatef(0.0f, aHeight, 0.0);
  ::glScalef(1.0, -1.0, 1.0);

  GLenum result = ::glGetError();
  if (result != GL_NO_ERROR) {
    NS_ERROR("Unexpected OpenGL Error");
    Destroy();
    if (oldContext)
      ::CGLSetCurrentContext(oldContext);
    return NS_ERROR_FAILURE;
  }

  if (oldContext)
    ::CGLSetCurrentContext(oldContext);

  return NS_OK;
}

void nsCARenderer::AttachIOSurface(nsIOSurface *aSurface) {
  if (mIOSurface && 
      aSurface->GetIOSurfaceID() == mIOSurface->GetIOSurfaceID()) {
    delete aSurface; 
    return;
  }
  if (mCARenderer) {
    // We are attaching a larger IOSurface, we need to
    // resize our elements.
    Destroy(); 
  }
  if (mIOSurface)
    delete mIOSurface;

  mIOSurface = aSurface;
}

nsresult nsCARenderer::Render(int aWidth, int aHeight, 
                              CGImageRef *aOutCGImage) {
  if (!aOutCGImage && !mIOSurface) {
    NS_ERROR("No target destination for rendering");
  } else if (aOutCGImage) {
    // We are expected to return a CGImageRef, we will set
    // it to NULL in case we fail before the image is ready.
    *aOutCGImage = NULL;
  }

  if (aWidth == 0 || aHeight == 0)
    return NS_OK;

  if (!mCARenderer) {
    return NS_ERROR_FAILURE;
  }

  CARenderer* caRenderer = (CARenderer*)mCARenderer;
  int renderer_width = caRenderer.bounds.size.width;
  int renderer_height = caRenderer.bounds.size.height;

  if (renderer_width != aWidth || renderer_height != aHeight) {
    // XXX: This should be optimized to not rescale the buffer
    //      if we are resizing down.
    CALayer* caLayer = [caRenderer layer];
    Destroy();
    if (SetupRenderer(caLayer, aWidth, aHeight) != NS_OK) {
      return NS_ERROR_FAILURE;
    }
    caRenderer = (CARenderer*)mCARenderer;
  }

  CGLContextObj oldContext = ::CGLGetCurrentContext();
  ::CGLSetCurrentContext(mOpenGLContext);
  if (!mIOSurface) {
    ::CGLSetPBuffer(mOpenGLContext, mPixelBuffer, 0, 0, 0);
  }

  GLenum result = ::glGetError();
  if (result != GL_NO_ERROR) {
    NS_ERROR("Unexpected OpenGL Error");
    Destroy();
    if (oldContext)
      ::CGLSetCurrentContext(oldContext);
    return NS_ERROR_FAILURE;
  }

  ::glClearColor(0.0, 0.0, 0.0, 0.0);
  ::glClear(GL_COLOR_BUFFER_BIT);

  double caTime = ::CACurrentMediaTime();
  [caRenderer beginFrameAtTime:caTime timeStamp:NULL];
  [caRenderer addUpdateRect:CGRectMake(0,0, aWidth, aHeight)];
  [caRenderer render];
  [caRenderer endFrame];

  // Read the data back either to the IOSurface or mCGImage.
  if (mIOSurface) {
    ::glFlush();
  } else {
    ::glPixelStorei(GL_PACK_ALIGNMENT, 4);
    ::glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    ::glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    ::glPixelStorei(GL_PACK_SKIP_PIXELS, 0);

    ::glReadPixels(0.0f, 0.0f, aWidth, aHeight,
                        GL_BGRA, GL_UNSIGNED_BYTE,
                        mCGData);

    *aOutCGImage = mCGImage;
  }

  if (oldContext) {
    ::CGLSetCurrentContext(oldContext);
  }

  return NS_OK;
}

nsresult nsCARenderer::DrawSurfaceToCGContext(CGContextRef aContext, 
                                              nsIOSurface *surf, 
                                              CGColorSpaceRef aColorSpace,
                                              int aX, int aY,
                                              int aWidth, int aHeight) {
  surf->Lock();
  size_t bytesPerRow = surf->GetBytesPerRow();
  size_t ioWidth = surf->GetWidth();
  size_t ioHeight = surf->GetHeight();
  void* ioData = surf->GetBaseAddress();
  CGDataProviderRef dataProvider = ::CGDataProviderCreateWithData(ioData,
                                      ioData, ioHeight*(bytesPerRow)*4, 
                                      NULL); //No release callback 
  if (!dataProvider) {
    surf->Unlock();
    return NS_ERROR_FAILURE;
  }

  // We get rendering glitches if we use a width/height that falls
  // outside of the IOSurface.
  if (aWidth > ioWidth - aX) 
    aWidth = ioWidth - aX;
  if (aHeight > ioHeight - aY) 
    aHeight = ioHeight - aY;

  CGImageRef cgImage = ::CGImageCreate(ioWidth, ioHeight, 8, 32, bytesPerRow,
              aColorSpace, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host,
              dataProvider, NULL, true, kCGRenderingIntentDefault);
  ::CGDataProviderRelease(dataProvider);
  if (!cgImage) {
    surf->Unlock();
    return NS_ERROR_FAILURE;
  }
  CGImageRef subImage = ::CGImageCreateWithImageInRect(cgImage,
                                       ::CGRectMake(aX, aY, aWidth, aHeight));
  if (!subImage) {
    ::CGImageRelease(cgImage);
    surf->Unlock();
    return NS_ERROR_FAILURE;
  }

  ::CGContextScaleCTM(aContext, 1.0f, -1.0f);
  ::CGContextDrawImage(aContext, CGRectMake(aX, -aY-aHeight, aWidth, aHeight), subImage);

  ::CGImageRelease(subImage);
  ::CGImageRelease(cgImage);
  surf->Unlock();
  return NS_OK;
}

