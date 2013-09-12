/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZ_WIDGET_GTK
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#define GET_NATIVE_WINDOW(aWidget) GDK_WINDOW_XID((GdkWindow *) aWidget->GetNativeData(NS_NATIVE_WINDOW))
#elif defined(MOZ_WIDGET_QT)
#include <QWidget>
#define GET_NATIVE_WINDOW(aWidget) static_cast<QWidget*>(aWidget->GetNativeData(NS_NATIVE_SHELLWIDGET))->winId()
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "mozilla/X11Util.h"

#include "prenv.h"
#include "GLContextProvider.h"
#include "GLLibraryLoader.h"
#include "nsDebug.h"
#include "nsIWidget.h"
#include "GLXLibrary.h"
#include "gfxXlibSurface.h"
#include "gfxContext.h"
#include "gfxImageSurface.h"
#include "gfxPlatform.h"
#include "GLContext.h"
#include "gfxUtils.h"

#include "gfxCrashReporterUtils.h"

#ifdef MOZ_WIDGET_GTK
#include "gfxPlatformGtk.h"
#endif

namespace mozilla {
namespace gl {

GLXLibrary sGLXLibrary[GLXLibrary::LIBS_MAX];
GLXLibrary& sDefGLXLib = sGLXLibrary[GLXLibrary::OPENGL_LIB];

typedef GLXLibrary::LibraryType LibType;

static LibType gCurrLib = GLXLibrary::OPENGL_LIB;

LibType
GLXLibrary::SelectLibrary(const GLContext::ContextFlags& aFlags)
{
  return (aFlags & GLContext::ContextFlagsMesaLLVMPipe)
          ? GLXLibrary::MESA_LLVMPIPE_LIB
          : GLXLibrary::OPENGL_LIB;
}

// Check that we have at least version aMajor.aMinor .
bool 
GLXLibrary::GLXVersionCheck(int aMajor, int aMinor)
{
    return aMajor < mGLXMajorVersion ||
           (aMajor == mGLXMajorVersion && aMinor <= mGLXMinorVersion);
}

static inline bool
HasExtension(const char* aExtensions, const char* aRequiredExtension)
{
    return GLContext::ListHasExtension(
        reinterpret_cast<const GLubyte*>(aExtensions), aRequiredExtension);
}

bool
GLXLibrary::EnsureInitialized(bool aUseMesaLLVMPipe)
{
    if (mInitialized) {
        return true;
    }

    // Don't repeatedly try to initialize.
    if (mTriedInitializing) {
        return false;
    }
    mTriedInitializing = true;

    // Force enabling s3 texture compression (http://dri.freedesktop.org/wiki/S3TC)
    PR_SetEnv("force_s3tc_enable=true");

    if (!mOGLLibrary) {
        // see e.g. bug 608526: it is intrinsically interesting to know whether we have dynamically linked to libGL.so.1
        // because at least the NVIDIA implementation requires an executable stack, which causes mprotect calls,
        // which trigger glibc bug http://sourceware.org/bugzilla/show_bug.cgi?id=12225
#ifdef __OpenBSD__
        const char *libGLfilename = aUseMesaLLVMPipe? "mesallvmpipe.so" : "libGL.so";
#else
        const char *libGLfilename = aUseMesaLLVMPipe? "mesallvmpipe.so" : "libGL.so.1";
#endif
        ScopedGfxFeatureReporter reporter(libGLfilename, aUseMesaLLVMPipe);
        mOGLLibrary = PR_LoadLibrary(libGLfilename);
        if (!mOGLLibrary) {
            NS_WARNING("Couldn't load OpenGL shared library.");
            return false;
        }
        reporter.SetSuccessful();
    }

    if (PR_GetEnv("MOZ_GLX_DEBUG")) {
        mDebug = true;
    }

    GLLibraryLoader::SymLoadStruct symbols[] = {
        /* functions that were in GLX 1.0 */
        { (PRFuncPtr*) &xDestroyContextInternal, { "glXDestroyContext", NULL } },
        { (PRFuncPtr*) &xMakeCurrentInternal, { "glXMakeCurrent", NULL } },
        { (PRFuncPtr*) &xSwapBuffersInternal, { "glXSwapBuffers", NULL } },
        { (PRFuncPtr*) &xQueryVersionInternal, { "glXQueryVersion", NULL } },
        { (PRFuncPtr*) &xGetCurrentContextInternal, { "glXGetCurrentContext", NULL } },
        { (PRFuncPtr*) &xWaitGLInternal, { "glXWaitGL", NULL } },
        { (PRFuncPtr*) &xWaitXInternal, { "glXWaitX", NULL } },
        /* functions introduced in GLX 1.1 */
        { (PRFuncPtr*) &xQueryExtensionsStringInternal, { "glXQueryExtensionsString", NULL } },
        { (PRFuncPtr*) &xGetClientStringInternal, { "glXGetClientString", NULL } },
        { (PRFuncPtr*) &xQueryServerStringInternal, { "glXQueryServerString", NULL } },
        { NULL, { NULL } }
    };

    GLLibraryLoader::SymLoadStruct symbols13[] = {
        /* functions introduced in GLX 1.3 */
        { (PRFuncPtr*) &xChooseFBConfigInternal, { "glXChooseFBConfig", NULL } },
        { (PRFuncPtr*) &xGetFBConfigAttribInternal, { "glXGetFBConfigAttrib", NULL } },
        // WARNING: xGetFBConfigs not set in symbols13_ext
        { (PRFuncPtr*) &xGetFBConfigsInternal, { "glXGetFBConfigs", NULL } },
        // WARNING: symbols13_ext sets xCreateGLXPixmapWithConfig instead
        { (PRFuncPtr*) &xCreatePixmapInternal, { "glXCreatePixmap", NULL } },
        { (PRFuncPtr*) &xDestroyPixmapInternal, { "glXDestroyPixmap", NULL } },
        { (PRFuncPtr*) &xCreateNewContextInternal, { "glXCreateNewContext", NULL } },
        { NULL, { NULL } }
    };

    GLLibraryLoader::SymLoadStruct symbols13_ext[] = {
        /* extension equivalents for functions introduced in GLX 1.3 */
        // GLX_SGIX_fbconfig extension
        { (PRFuncPtr*) &xChooseFBConfigInternal, { "glXChooseFBConfigSGIX", NULL } },
        { (PRFuncPtr*) &xGetFBConfigAttribInternal, { "glXGetFBConfigAttribSGIX", NULL } },
        // WARNING: no xGetFBConfigs equivalent in extensions
        // WARNING: different from symbols13:
        { (PRFuncPtr*) &xCreateGLXPixmapWithConfigInternal, { "glXCreateGLXPixmapWithConfigSGIX", NULL } },
        { (PRFuncPtr*) &xDestroyPixmapInternal, { "glXDestroyGLXPixmap", NULL } }, // not from ext
        { (PRFuncPtr*) &xCreateNewContextInternal, { "glXCreateContextWithConfigSGIX", NULL } },
        { NULL, { NULL } }
    };

    GLLibraryLoader::SymLoadStruct symbols14[] = {
        /* functions introduced in GLX 1.4 */
        { (PRFuncPtr*) &xGetProcAddressInternal, { "glXGetProcAddress", NULL } },
        { NULL, { NULL } }
    };

    GLLibraryLoader::SymLoadStruct symbols14_ext[] = {
        /* extension equivalents for functions introduced in GLX 1.4 */
        // GLX_ARB_get_proc_address extension
        { (PRFuncPtr*) &xGetProcAddressInternal, { "glXGetProcAddressARB", NULL } },
        { NULL, { NULL } }
    };

    GLLibraryLoader::SymLoadStruct symbols_texturefrompixmap[] = {
        { (PRFuncPtr*) &xBindTexImageInternal, { "glXBindTexImageEXT", NULL } },
        { (PRFuncPtr*) &xReleaseTexImageInternal, { "glXReleaseTexImageEXT", NULL } },
        { NULL, { NULL } }
    };

    GLLibraryLoader::SymLoadStruct symbols_robustness[] = {
        { (PRFuncPtr*) &xCreateContextAttribsInternal, { "glXCreateContextAttribsARB", NULL } },
        { NULL, { NULL } }
    };

    if (!GLLibraryLoader::LoadSymbols(mOGLLibrary, &symbols[0])) {
        NS_WARNING("Couldn't find required entry point in OpenGL shared library");
        return false;
    }

    Display *display = DefaultXDisplay();
    int screen = DefaultScreen(display);

    if (!xQueryVersion(display, &mGLXMajorVersion, &mGLXMinorVersion)) {
        mGLXMajorVersion = 0;
        mGLXMinorVersion = 0;
        return false;
    }

    if (!GLXVersionCheck(1, 1))
        // Not possible to query for extensions.
        return false;

    const char *clientVendor = xGetClientString(display, GLX_VENDOR);
    const char *serverVendor = xQueryServerString(display, screen, GLX_VENDOR);
    const char *extensionsStr = xQueryExtensionsString(display, screen);

    GLLibraryLoader::SymLoadStruct *sym13;
    if (!GLXVersionCheck(1, 3)) {
        // Even if we don't have 1.3, we might have equivalent extensions
        // (as on the Intel X server).
        if (!HasExtension(extensionsStr, "GLX_SGIX_fbconfig")) {
            return false;
        }
        sym13 = symbols13_ext;
    } else {
        sym13 = symbols13;
    }
    if (!GLLibraryLoader::LoadSymbols(mOGLLibrary, sym13)) {
        NS_WARNING("Couldn't find required entry point in OpenGL shared library");
        return false;
    }

    GLLibraryLoader::SymLoadStruct *sym14;
    if (!GLXVersionCheck(1, 4)) {
        // Even if we don't have 1.4, we might have equivalent extensions
        // (as on the Intel X server).
        if (!HasExtension(extensionsStr, "GLX_ARB_get_proc_address")) {
            return false;
        }
        sym14 = symbols14_ext;
    } else {
        sym14 = symbols14;
    }
    if (!GLLibraryLoader::LoadSymbols(mOGLLibrary, sym14)) {
        NS_WARNING("Couldn't find required entry point in OpenGL shared library");
        return false;
    }

    if (HasExtension(extensionsStr, "GLX_EXT_texture_from_pixmap") &&
        GLLibraryLoader::LoadSymbols(mOGLLibrary, symbols_texturefrompixmap, 
                                         (GLLibraryLoader::PlatformLookupFunction)&xGetProcAddress))
    {
#ifdef MOZ_WIDGET_GTK
        mUseTextureFromPixmap = gfxPlatformGtk::GetPlatform()->UseXRender();
#else
        mUseTextureFromPixmap = true;
#endif
    } else {
        mUseTextureFromPixmap = false;
        NS_WARNING("Texture from pixmap disabled");
    }

    if (HasExtension(extensionsStr, "GLX_ARB_create_context_robustness") &&
        GLLibraryLoader::LoadSymbols(mOGLLibrary, symbols_robustness)) {
        mHasRobustness = true;
    }

    mIsATI = serverVendor && DoesStringMatch(serverVendor, "ATI");
    mClientIsMesa = clientVendor && DoesStringMatch(clientVendor, "Mesa");

    mInitialized = true;
    if(aUseMesaLLVMPipe)
      mLibType = GLXLibrary::MESA_LLVMPIPE_LIB;

    return true;
}

bool
GLXLibrary::SupportsTextureFromPixmap(gfxASurface* aSurface)
{
    if (!EnsureInitialized(mLibType == MESA_LLVMPIPE_LIB)) {
        return false;
    }
    
    if (aSurface->GetType() != gfxASurface::SurfaceTypeXlib || !mUseTextureFromPixmap) {
        return false;
    }

    return true;
}

GLXPixmap 
GLXLibrary::CreatePixmap(gfxASurface* aSurface)
{
    if (!SupportsTextureFromPixmap(aSurface)) {
        return None;
    }

    gfxXlibSurface *xs = static_cast<gfxXlibSurface*>(aSurface);
    const XRenderPictFormat *format = xs->XRenderFormat();
    if (!format || format->type != PictTypeDirect) {
        return None;
    }
    const XRenderDirectFormat& direct = format->direct;
    int alphaSize;
    PR_FLOOR_LOG2(alphaSize, direct.alphaMask + 1);
    NS_ASSERTION((1 << alphaSize) - 1 == direct.alphaMask,
                 "Unexpected render format with non-adjacent alpha bits");

    int attribs[] = { GLX_DOUBLEBUFFER, False,
                      GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
                      GLX_ALPHA_SIZE, alphaSize,
                      (alphaSize ? GLX_BIND_TO_TEXTURE_RGBA_EXT
                       : GLX_BIND_TO_TEXTURE_RGB_EXT), True,
                      GLX_RENDER_TYPE, GLX_RGBA_BIT,
                      None };

    int numConfigs = 0;
    Display *display = xs->XDisplay();
    int xscreen = DefaultScreen(display);

    ScopedXFree<GLXFBConfig> cfgs(xChooseFBConfig(display,
                                                  xscreen,
                                                  attribs,
                                                  &numConfigs));

    // Find an fbconfig that matches the pixel format used on the Pixmap. 
    int matchIndex = -1;
    unsigned long redMask =
        static_cast<unsigned long>(direct.redMask) << direct.red;
    unsigned long greenMask =
        static_cast<unsigned long>(direct.greenMask) << direct.green;
    unsigned long blueMask =
        static_cast<unsigned long>(direct.blueMask) << direct.blue;
    // This is true if the Pixmap has bits for alpha or unused bits.
    bool haveNonColorBits =
        ~(redMask | greenMask | blueMask) != -1UL << format->depth;

    for (int i = 0; i < numConfigs; i++) {
        int id = None;
        sGLXLibrary[mLibType].xGetFBConfigAttrib(display, cfgs[i], GLX_VISUAL_ID, &id);
        Visual *visual;
        int depth;
        FindVisualAndDepth(display, id, &visual, &depth);
        if (!visual ||
            visual->c_class != TrueColor ||
            visual->red_mask != redMask ||
            visual->green_mask != greenMask ||
            visual->blue_mask != blueMask ) {
            continue;
        }

        // Historically Xlib Visuals did not try to represent an alpha channel
        // and there was no means to use an alpha channel on a Pixmap.  The
        // Xlib Visual from the fbconfig was not intended to have any
        // information about alpha bits.
        //
        // Since then, RENDER has added formats for 32 bit depth Pixmaps.
        // Some of these formats have bits for alpha and some have unused
        // bits.
        //
        // Then the Composite extension added a 32 bit depth Visual intended
        // for Windows with an alpha channel, so bits not in the visual color
        // masks were expected to be treated as alpha bits.
        //
        // Usually GLX counts only color bits in the Visual depth, but the
        // depth of Composite's ARGB Visual includes alpha bits.  However,
        // bits not in the color masks are not necessarily alpha bits because
        // sometimes (NVIDIA) 32 bit Visuals are added for fbconfigs with 32
        // bit BUFFER_SIZE but zero alpha bits and 24 color bits (NVIDIA
        // again).
        //
        // This checks that the depth matches in one of the two ways.
        if (depth != format->depth && depth != format->depth - alphaSize) {
            continue;
        }

        // If all bits of the Pixmap are color bits and the Pixmap depth
        // matches the depth of the fbconfig visual, then we can assume that
        // the driver will do whatever is necessary to ensure that any
        // GLXPixmap alpha bits are treated as set.  We can skip the
        // ALPHA_SIZE check in this situation.  We need to skip this check for
        // situations (ATI) where there are no fbconfigs without alpha bits.
        //
        // glXChooseFBConfig should prefer configs with smaller
        // GLX_BUFFER_SIZE, so we should still get zero alpha bits if
        // available, except perhaps with NVIDIA drivers where buffer size is
        // not the specified sum of the component sizes.
        if (haveNonColorBits) {
            // There are bits in the Pixmap format that haven't been matched
            // against the fbconfig visual.  These bits could either represent
            // alpha or be unused, so just check that the number of alpha bits
            // matches.
            int size = 0;
            sGLXLibrary[mLibType].xGetFBConfigAttrib(display, cfgs[i],
                                                     GLX_ALPHA_SIZE, &size);
            if (size != alphaSize) {
                continue;
            }
        }

        matchIndex = i;
        break;
    }
    if (matchIndex == -1) {
        NS_WARNING("[GLX] Couldn't find a FBConfig matching Pixmap format");
        return None;
    }

    int pixmapAttribs[] = { GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
                            GLX_TEXTURE_FORMAT_EXT,
                            (alphaSize ? GLX_TEXTURE_FORMAT_RGBA_EXT
                             : GLX_TEXTURE_FORMAT_RGB_EXT),
                            None};

    GLXPixmap glxpixmap = xCreatePixmap(display,
                                        cfgs[matchIndex],
                                        xs->XDrawable(),
                                        pixmapAttribs);

    return glxpixmap;
}

void
GLXLibrary::DestroyPixmap(GLXPixmap aPixmap)
{
    if (!mUseTextureFromPixmap) {
        return;
    }

    Display *display = DefaultXDisplay();
    xDestroyPixmap(display, aPixmap);
}

void
GLXLibrary::BindTexImage(GLXPixmap aPixmap)
{    
    if (!mUseTextureFromPixmap) {
        return;
    }

    Display *display = DefaultXDisplay();
    // Make sure all X drawing to the surface has finished before binding to a texture.
    if (mClientIsMesa) {
        // Using XSync instead of Mesa's glXWaitX, because its glxWaitX is a
        // noop when direct rendering unless the current drawable is a
        // single-buffer window.
        FinishX(display);
    } else {
        xWaitX();
    }
    xBindTexImage(display, aPixmap, GLX_FRONT_LEFT_EXT, NULL);
}

void
GLXLibrary::ReleaseTexImage(GLXPixmap aPixmap)
{
    if (!mUseTextureFromPixmap) {
        return;
    }

    Display *display = DefaultXDisplay();
    xReleaseTexImage(display, aPixmap, GLX_FRONT_LEFT_EXT);
}

#ifdef DEBUG

static int (*sOldErrorHandler)(Display *, XErrorEvent *);
ScopedXErrorHandler::ErrorEvent sErrorEvent;
static int GLXErrorHandler(Display *display, XErrorEvent *ev)
{
    if (!sErrorEvent.mError.error_code) {
        sErrorEvent.mError = *ev;
    }
    return 0;
}

void
GLXLibrary::BeforeGLXCall()
{
    if (mDebug) {
        sOldErrorHandler = XSetErrorHandler(GLXErrorHandler);
    }
}

void
GLXLibrary::AfterGLXCall()
{
    if (mDebug) {
        FinishX(DefaultXDisplay());
        if (sErrorEvent.mError.error_code) {
            char buffer[2048];
            XGetErrorText(DefaultXDisplay(), sErrorEvent.mError.error_code, buffer, sizeof(buffer));
            printf_stderr("X ERROR: %s (%i) - Request: %i.%i, Serial: %i",
                          buffer,
                          sErrorEvent.mError.error_code,
                          sErrorEvent.mError.request_code,
                          sErrorEvent.mError.minor_code,
                          sErrorEvent.mError.serial);
            NS_ABORT();
        }
        XSetErrorHandler(sOldErrorHandler);
    }
}

#define BEFORE_GLX_CALL do {                     \
    sGLXLibrary[gCurrLib].BeforeGLXCall();       \
} while (0)
    
#define AFTER_GLX_CALL do {                      \
    sGLXLibrary[gCurrLib].AfterGLXCall();        \
} while (0)

#else

#define BEFORE_GLX_CALL do { } while(0)
#define AFTER_GLX_CALL do { } while(0)

#endif
    
void 
GLXLibrary::xDestroyContext(Display* display, GLXContext context)
{
    BEFORE_GLX_CALL;
    xDestroyContextInternal(display, context);
    AFTER_GLX_CALL;
}

Bool 
GLXLibrary::xMakeCurrent(Display* display, 
                         GLXDrawable drawable, 
                         GLXContext context)
{
    BEFORE_GLX_CALL;
    Bool result = xMakeCurrentInternal(display, drawable, context);
    AFTER_GLX_CALL;
    return result;
}

GLXContext 
GLXLibrary::xGetCurrentContext()
{
    BEFORE_GLX_CALL;
    GLXContext result = xGetCurrentContextInternal();
    AFTER_GLX_CALL;
    return result;
}

/* static */ void* 
GLXLibrary::xGetProcAddress(const char *procName)
{
    BEFORE_GLX_CALL;
    void* result = sGLXLibrary[gCurrLib].xGetProcAddressInternal(procName);
    AFTER_GLX_CALL;
    return result;
}

GLXFBConfig*
GLXLibrary::xChooseFBConfig(Display* display, 
                            int screen, 
                            const int *attrib_list, 
                            int *nelements)
{
    BEFORE_GLX_CALL;
    GLXFBConfig* result = xChooseFBConfigInternal(display, screen, attrib_list, nelements);
    AFTER_GLX_CALL;
    return result;
}

GLXFBConfig* 
GLXLibrary::xGetFBConfigs(Display* display, 
                          int screen, 
                          int *nelements)
{
    BEFORE_GLX_CALL;
    GLXFBConfig* result = xGetFBConfigsInternal(display, screen, nelements);
    AFTER_GLX_CALL;
    return result;
}
    
GLXContext
GLXLibrary::xCreateNewContext(Display* display, 
                              GLXFBConfig config, 
                              int render_type, 
                              GLXContext share_list, 
                              Bool direct)
{
    BEFORE_GLX_CALL;
    GLXContext result = xCreateNewContextInternal(display, config, 
	                                              render_type,
	                                              share_list, direct);
    AFTER_GLX_CALL;
    return result;
}

int
GLXLibrary::xGetFBConfigAttrib(Display *display,
                               GLXFBConfig config,
                               int attribute,
                               int *value)
{
    BEFORE_GLX_CALL;
    int result = xGetFBConfigAttribInternal(display, config,
                                            attribute, value);
    AFTER_GLX_CALL;
    return result;
}

void
GLXLibrary::xSwapBuffers(Display *display, GLXDrawable drawable)
{
    BEFORE_GLX_CALL;
    xSwapBuffersInternal(display, drawable);
    AFTER_GLX_CALL;
}

const char *
GLXLibrary::xQueryExtensionsString(Display *display,
                                   int screen)
{
    BEFORE_GLX_CALL;
    const char *result = xQueryExtensionsStringInternal(display, screen);
    AFTER_GLX_CALL;
    return result;
}

const char *
GLXLibrary::xGetClientString(Display *display,
                             int screen)
{
    BEFORE_GLX_CALL;
    const char *result = xGetClientStringInternal(display, screen);
    AFTER_GLX_CALL;
    return result;
}

const char *
GLXLibrary::xQueryServerString(Display *display,
                               int screen, int name)
{
    BEFORE_GLX_CALL;
    const char *result = xQueryServerStringInternal(display, screen, name);
    AFTER_GLX_CALL;
    return result;
}

GLXPixmap
GLXLibrary::xCreatePixmap(Display *display, 
                          GLXFBConfig config,
                          Pixmap pixmap,
                          const int *attrib_list)
{
    BEFORE_GLX_CALL;
    GLXPixmap result = xCreatePixmapInternal(display, config,
                                             pixmap, attrib_list);
    AFTER_GLX_CALL;
    return result;
}

GLXPixmap
GLXLibrary::xCreateGLXPixmapWithConfig(Display *display,
                                       GLXFBConfig config,
                                       Pixmap pixmap)
{
    BEFORE_GLX_CALL;
    GLXPixmap result = xCreateGLXPixmapWithConfigInternal(display, config, pixmap);
    AFTER_GLX_CALL;
    return result;
}

void
GLXLibrary::xDestroyPixmap(Display *display, GLXPixmap pixmap)
{
    BEFORE_GLX_CALL;
    xDestroyPixmapInternal(display, pixmap);
    AFTER_GLX_CALL;
}

Bool
GLXLibrary::xQueryVersion(Display *display,
                          int *major,
                          int *minor)
{
    BEFORE_GLX_CALL;
    Bool result = xQueryVersionInternal(display, major, minor);
    AFTER_GLX_CALL;
    return result;
}

void
GLXLibrary::xBindTexImage(Display *display,
                          GLXDrawable drawable,
                          int buffer,
                          const int *attrib_list)
{
    BEFORE_GLX_CALL;
    xBindTexImageInternal(display, drawable, buffer, attrib_list);
    AFTER_GLX_CALL;
}

void
GLXLibrary::xReleaseTexImage(Display *display,
                             GLXDrawable drawable,
                             int buffer)
{
    BEFORE_GLX_CALL;
    xReleaseTexImageInternal(display, drawable, buffer);
    AFTER_GLX_CALL;
}

void 
GLXLibrary::xWaitGL()
{
    BEFORE_GLX_CALL;
    xWaitGLInternal();
    AFTER_GLX_CALL;
}

void
GLXLibrary::xWaitX()
{
    BEFORE_GLX_CALL;
    xWaitXInternal();
    AFTER_GLX_CALL;
}

GLXContext
GLXLibrary::xCreateContextAttribs(Display* display, 
                                  GLXFBConfig config, 
                                  GLXContext share_list, 
                                  Bool direct,
                                  const int* attrib_list)
{
    BEFORE_GLX_CALL;
    GLXContext result = xCreateContextAttribsInternal(display, 
                                                      config, 
                                                      share_list, 
                                                      direct,
                                                      attrib_list);
    AFTER_GLX_CALL;
    return result;
}

class GLContextGLX : public GLContext
{
public:
    static already_AddRefed<GLContextGLX>
    CreateGLContext(const ContextFormat& format,
                    Display *display,
                    GLXDrawable drawable,
                    GLXFBConfig cfg,
                    GLContextGLX *shareContext,
                    bool deleteDrawable,
                    LibType lib = GLXLibrary::OPENGL_LIB,
                    gfxXlibSurface *pixmap = nullptr)
    {
        int db = 0, err;
        err = sGLXLibrary[lib].xGetFBConfigAttrib(display, cfg,
                                             GLX_DOUBLEBUFFER, &db);
        if (GLX_BAD_ATTRIBUTE != err) {
#ifdef DEBUG
            if (DebugMode()) {
                printf("[GLX] FBConfig is %sdouble-buffered\n", db ? "" : "not ");
            }
#endif
        }

        GLXContext context;
        nsRefPtr<GLContextGLX> glContext;
        bool error;

        ScopedXErrorHandler xErrorHandler;

TRY_AGAIN_NO_SHARING:

        error = false;

        if (sGLXLibrary[lib].HasRobustness()) {
            int attrib_list[] = {
                LOCAL_GL_CONTEXT_FLAGS_ARB, LOCAL_GL_CONTEXT_ROBUST_ACCESS_BIT_ARB,
                LOCAL_GL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB, LOCAL_GL_LOSE_CONTEXT_ON_RESET_ARB,
                0,
            };

            context = sGLXLibrary[lib].xCreateContextAttribs(display,
                                                        cfg,
                                                        shareContext ? shareContext->mContext : NULL,
                                                        True,
                                                        attrib_list);
        } else {
            context = sGLXLibrary[lib].xCreateNewContext(display,
                                                    cfg,
                                                    GLX_RGBA_TYPE,
                                                    shareContext ? shareContext->mContext : NULL,
                                                    True);
        }

        if (context) {
            glContext = new GLContextGLX(format,
                                        shareContext,
                                        display,
                                        drawable,
                                        context,
                                        deleteDrawable,
                                        db,
                                        pixmap,
                                        lib);
            if (!glContext->Init())
                error = true;
        } else {
            error = true;
        }

        error |= xErrorHandler.SyncAndGetError(display);

        if (error) {
            if (shareContext) {
                shareContext = nullptr;
                goto TRY_AGAIN_NO_SHARING;
            }

            NS_WARNING("Failed to create GLXContext!");
            glContext = nullptr; // note: this must be done while the graceful X error handler is set,
                                // because glxMakeCurrent can give a GLXBadDrawable error
        }

        return glContext.forget();
    }

    ~GLContextGLX()
    {
        MarkDestroyed();

        // see bug 659842 comment 76
#ifdef DEBUG
        bool success =
#endif
        sGLXLib.xMakeCurrent(mDisplay, None, nullptr);
        NS_ABORT_IF_FALSE(success,
            "glXMakeCurrent failed to release GL context before we call glXDestroyContext!");

        sGLXLib.xDestroyContext(mDisplay, mContext);

        if (mDeleteDrawable) {
            sGLXLib.xDestroyPixmap(mDisplay, mDrawable);
        }
    }

    GLContextType GetContextType() {
        return ContextTypeGLX;
    }

    bool Init()
    {
        MakeCurrent();
        SetupLookupFunction();
        if (!InitWithPrefix("gl", true)) {
            return false;
        }

        if (!IsExtensionSupported(EXT_framebuffer_object))
            return false;

        InitFramebuffers();

        return true;
    }

    bool MakeCurrentImpl(bool aForce = false)
    {
        bool succeeded = true;

        // With the ATI FGLRX driver, glxMakeCurrent is very slow even when the context doesn't change.
        // (This is not the case with other drivers such as NVIDIA).
        // So avoid calling it more than necessary. Since GLX documentation says that:
        //     "glXGetCurrentContext returns client-side information.
        //      It does not make a round trip to the server."
        // I assume that it's not worth using our own TLS slot here.
        if (aForce || sGLXLib.xGetCurrentContext() != mContext) {
            succeeded = sGLXLib.xMakeCurrent(mDisplay, mDrawable, mContext);
            NS_ASSERTION(succeeded, "Failed to make GL context current!");
        }

        return succeeded;
    }

    virtual bool IsCurrent() {
        return sGLXLib.xGetCurrentContext() == mContext;
    }

    bool SetupLookupFunction()
    {
        mLookupFunc = (PlatformLookupFunction)&GLXLibrary::xGetProcAddress;
        return true;
    }

    void *GetNativeData(NativeDataType aType)
    {
        switch(aType) {
        case NativeGLContext:
            return mContext;
 
        case NativeThebesSurface:
            return mPixmap;

        default:
            return nullptr;
        }
    }

    bool IsDoubleBuffered()
    {
        return mDoubleBuffered;
    }

    bool SupportsRobustness()
    {
        return sGLXLib.HasRobustness();
    }

    bool SwapBuffers()
    {
        if (!mDoubleBuffered)
            return false;
        sGLXLib.xSwapBuffers(mDisplay, mDrawable);
        sGLXLib.xWaitGL();
        return true;
    }

    bool TextureImageSupportsGetBackingSurface()
    {
        return sGLXLib.UseTextureFromPixmap();
    }

    virtual already_AddRefed<TextureImage>
    CreateTextureImage(const nsIntSize& aSize,
                       TextureImage::ContentType aContentType,
                       GLenum aWrapMode,
                       TextureImage::Flags aFlags = TextureImage::NoFlags);

private:
    friend class GLContextProviderGLX;

    GLContextGLX(const ContextFormat& aFormat,
                 GLContext *aShareContext,
                 Display *aDisplay,
                 GLXDrawable aDrawable,
                 GLXContext aContext,
                 bool aDeleteDrawable,
                 bool aDoubleBuffered,
                 gfxXlibSurface *aPixmap,
                 LibType aLibType)
        : GLContext(aFormat, aDeleteDrawable ? true : false, aShareContext),
          mContext(aContext),
          mDisplay(aDisplay),
          mDrawable(aDrawable),
          mDeleteDrawable(aDeleteDrawable),
          mDoubleBuffered(aDoubleBuffered),
          mLibType(aLibType),
          mPixmap(aPixmap),
          sGLXLib(sGLXLibrary[aLibType])
    {
    }

    GLXContext mContext;
    Display *mDisplay;
    GLXDrawable mDrawable;
    bool mDeleteDrawable;
    bool mDoubleBuffered;
    LibType mLibType;

    nsRefPtr<gfxXlibSurface> mPixmap;
    GLXLibrary& sGLXLib;
};

class TextureImageGLX : public TextureImage
{
    friend already_AddRefed<TextureImage>
    GLContextGLX::CreateTextureImage(const nsIntSize&,
                                     ContentType,
                                     GLenum,
                                     TextureImage::Flags);

public:
    virtual ~TextureImageGLX()
    {
        mGLContext->MakeCurrent();
        mGLContext->fDeleteTextures(1, &mTexture);
        sGLXLib.DestroyPixmap(mPixmap);
    }

    virtual gfxASurface* BeginUpdate(nsIntRegion& aRegion)
    {
        mInUpdate = true;
        return mUpdateSurface;
    }

    virtual void EndUpdate()
    {
        mInUpdate = false;
    }


    virtual bool DirectUpdate(gfxASurface* aSurface, const nsIntRegion& aRegion, const nsIntPoint& aFrom)
    {
        nsRefPtr<gfxContext> ctx = new gfxContext(mUpdateSurface);
        gfxUtils::ClipToRegion(ctx, aRegion);
        ctx->SetSource(aSurface, aFrom);
        ctx->SetOperator(gfxContext::OPERATOR_SOURCE);
        ctx->Paint();
        return true;
    }

    virtual void BindTexture(GLenum aTextureUnit)
    {
        mGLContext->fActiveTexture(aTextureUnit);
        mGLContext->fBindTexture(LOCAL_GL_TEXTURE_2D, mTexture);
        sGLXLib.BindTexImage(mPixmap);
        mGLContext->fActiveTexture(LOCAL_GL_TEXTURE0);
    }

    virtual void ReleaseTexture()
    {
        sGLXLib.ReleaseTexImage(mPixmap);
    }

    virtual already_AddRefed<gfxASurface> GetBackingSurface()
    {
        nsRefPtr<gfxASurface> copy = mUpdateSurface;
        return copy.forget();
    }

    virtual bool InUpdate() const { return mInUpdate; }

    virtual GLuint GetTextureID() {
        return mTexture;
    }

private:
   TextureImageGLX(GLuint aTexture,
                   const nsIntSize& aSize,
                   GLenum aWrapMode,
                   ContentType aContentType,
                   GLContext* aContext,
                   gfxASurface* aSurface,
                   GLXPixmap aPixmap,
                   TextureImage::Flags aFlags,
                   LibType aLibType)
        : TextureImage(aSize, aWrapMode, aContentType, aFlags)
        , mGLContext(aContext)
        , mUpdateSurface(aSurface)
        , mPixmap(aPixmap)
        , mInUpdate(false)
        , mTexture(aTexture)
        , sGLXLib(sGLXLibrary[aLibType])
    {
        if (aSurface->GetContentType() == gfxASurface::CONTENT_COLOR_ALPHA) {
            mShaderType = gl::RGBALayerProgramType;
        } else {
            mShaderType = gl::RGBXLayerProgramType;
        }
    }

    GLContext* mGLContext;
    nsRefPtr<gfxASurface> mUpdateSurface;
    GLXPixmap mPixmap;
    bool mInUpdate;
    GLuint mTexture;
    GLXLibrary& sGLXLib;

    virtual void ApplyFilter()
    {
        mGLContext->ApplyFilterToBoundTexture(mFilter);
    }
};

already_AddRefed<TextureImage>
GLContextGLX::CreateTextureImage(const nsIntSize& aSize,
                                 TextureImage::ContentType aContentType,
                                 GLenum aWrapMode,
                                 TextureImage::Flags aFlags)
{
    if (!TextureImageSupportsGetBackingSurface()) {
        return GLContext::CreateTextureImage(aSize, 
                                             aContentType, 
                                             aWrapMode, 
                                             aFlags);
    }

    Display *display = DefaultXDisplay();
    int xscreen = DefaultScreen(display);
    gfxASurface::gfxImageFormat imageFormat = gfxPlatform::GetPlatform()->OptimalFormatForContent(aContentType);

    XRenderPictFormat* xrenderFormat =
        gfxXlibSurface::FindRenderFormat(display, imageFormat);
    NS_ASSERTION(xrenderFormat, "Could not find a render format for our display!");


    nsRefPtr<gfxXlibSurface> surface =
        gfxXlibSurface::Create(ScreenOfDisplay(display, xscreen),
                               xrenderFormat,
                               gfxIntSize(aSize.width, aSize.height));
    NS_ASSERTION(surface, "Failed to create xlib surface!");

    if (aContentType == gfxASurface::CONTENT_COLOR_ALPHA) {
        nsRefPtr<gfxContext> ctx = new gfxContext(surface);
        ctx->SetOperator(gfxContext::OPERATOR_CLEAR);
        ctx->Paint();
    }

    MakeCurrent();
    GLXPixmap pixmap = sGLXLib.CreatePixmap(surface);
    NS_ASSERTION(pixmap, "Failed to create pixmap!");

    GLuint texture;
    fGenTextures(1, &texture);

    fActiveTexture(LOCAL_GL_TEXTURE0);
    fBindTexture(LOCAL_GL_TEXTURE_2D, texture);

    nsRefPtr<TextureImageGLX> teximage =
        new TextureImageGLX(texture, aSize, aWrapMode, aContentType, 
                            this, surface, pixmap, aFlags, mLibType);

    GLint texfilter = aFlags & TextureImage::UseNearestFilter ? LOCAL_GL_NEAREST : LOCAL_GL_LINEAR;
    fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MIN_FILTER, texfilter);
    fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_MAG_FILTER, texfilter);
    fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_S, aWrapMode);
    fTexParameteri(LOCAL_GL_TEXTURE_2D, LOCAL_GL_TEXTURE_WRAP_T, aWrapMode);

    return teximage.forget();
}

static GLContextGLX *
GetGlobalContextGLX(const GLContext::ContextFlags aFlags = GLContext::ContextFlagsNone)
{
    return static_cast<GLContextGLX*>(GLContextProviderGLX::GetGlobalContext(aFlags));
}

static bool
AreCompatibleVisuals(Visual *one, Visual *two)
{
    if (one->c_class != two->c_class) {
        return false;
    }

    if (one->red_mask != two->red_mask ||
        one->green_mask != two->green_mask ||
        one->blue_mask != two->blue_mask) {
        return false;
    }

    if (one->bits_per_rgb != two->bits_per_rgb) {
        return false;
    }

    return true;
}

already_AddRefed<GLContext>
GLContextProviderGLX::CreateForWindow(nsIWidget *aWidget)
{
    if (!sDefGLXLib.EnsureInitialized(false)) {
        return nullptr;
    }

    // Currently, we take whatever Visual the window already has, and
    // try to create an fbconfig for that visual.  This isn't
    // necessarily what we want in the long run; an fbconfig may not
    // be available for the existing visual, or if it is, the GL
    // performance might be suboptimal.  But using the existing visual
    // is a relatively safe intermediate step.

    Display *display = (Display*)aWidget->GetNativeData(NS_NATIVE_DISPLAY); 
    int xscreen = DefaultScreen(display);
    Window window = GET_NATIVE_WINDOW(aWidget);

    int numConfigs;
    ScopedXFree<GLXFBConfig> cfgs;
    if (sDefGLXLib.IsATI() || 
        !sDefGLXLib.GLXVersionCheck(1, 3)) {
        const int attribs[] = {
            GLX_DOUBLEBUFFER, False,
            0
        };
        cfgs = sDefGLXLib.xChooseFBConfig(display,
                                       xscreen,
                                       attribs,
                                       &numConfigs);
    } else {
        cfgs = sDefGLXLib.xGetFBConfigs(display,
                                     xscreen,
                                     &numConfigs);
    }

    if (!cfgs) {
        NS_WARNING("[GLX] glXGetFBConfigs() failed");
        return nullptr;
    }
    NS_ASSERTION(numConfigs > 0, "No FBConfigs found!");

    // XXX the visual ID is almost certainly the GLX_FBCONFIG_ID, so
    // we could probably do this first and replace the glXGetFBConfigs
    // with glXChooseConfigs.  Docs are sparklingly clear as always.
    XWindowAttributes widgetAttrs;
    if (!XGetWindowAttributes(display, window, &widgetAttrs)) {
        NS_WARNING("[GLX] XGetWindowAttributes() failed");
        return nullptr;
    }
    const VisualID widgetVisualID = XVisualIDFromVisual(widgetAttrs.visual);
#ifdef DEBUG
    printf("[GLX] widget has VisualID 0x%lx\n", widgetVisualID);
#endif

    int matchIndex = -1;

    for (int i = 0; i < numConfigs; i++) {
        int visid = None;
        sDefGLXLib.xGetFBConfigAttrib(display, cfgs[i], GLX_VISUAL_ID, &visid);
        if (!visid) {
            continue;
        }
        if (sDefGLXLib.IsATI()) {
            int depth;
            Visual *visual;
            FindVisualAndDepth(display, visid, &visual, &depth);
            if (depth == widgetAttrs.depth &&
                AreCompatibleVisuals(widgetAttrs.visual, visual)) {
                matchIndex = i;
                break;
            }
        } else {
            if (widgetVisualID == static_cast<VisualID>(visid)) {
                matchIndex = i;
                break;
            }
        }
    }

    if (matchIndex == -1) {
        NS_WARNING("[GLX] Couldn't find a FBConfig matching widget visual");
        return nullptr;
    }

    GLContextGLX *shareContext = GetGlobalContextGLX();

    nsRefPtr<GLContextGLX> glContext = GLContextGLX::CreateGLContext(ContextFormat(ContextFormat::BasicRGB24),
                                                                     display,
                                                                     window,
                                                                     cfgs[matchIndex],
                                                                     shareContext,
                                                                     false,
                                                                     GLXLibrary::OPENGL_LIB);

    return glContext.forget();
}

static already_AddRefed<GLContextGLX>
CreateOffscreenPixmapContext(const gfxIntSize& aSize,
                             const ContextFormat& aFormat,
                             bool aShare, LibType aLibToUse)
{
    GLXLibrary& sGLXLib = sGLXLibrary[aLibToUse];
    if (!sGLXLib.EnsureInitialized(aLibToUse == GLXLibrary::MESA_LLVMPIPE_LIB)) {
        return nullptr;
    }

    Display *display = DefaultXDisplay();
    int xscreen = DefaultScreen(display);

    int attribs[] = {
        GLX_DOUBLEBUFFER, False,
        GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
        GLX_X_RENDERABLE, True,
        GLX_RED_SIZE, 1,
        GLX_GREEN_SIZE, 1,
        GLX_BLUE_SIZE, 1,
        GLX_ALPHA_SIZE, 0,
        GLX_DEPTH_SIZE, 0,
        0
    };
    int numConfigs = 0;

    ScopedXFree<GLXFBConfig> cfgs;
    cfgs = sGLXLib.xChooseFBConfig(display,
                                   xscreen,
                                   attribs,
                                   &numConfigs);
    if (!cfgs) {
        return nullptr;
    }

    NS_ASSERTION(numConfigs > 0,
                 "glXChooseFBConfig() failed to match our requested format and violated its spec (!)");

    int visid = None;
    int chosenIndex = 0;

    for (int i = 0; i < numConfigs; ++i) {
        int dtype;

        if (sGLXLib.xGetFBConfigAttrib(display, cfgs[i], GLX_DRAWABLE_TYPE, &dtype) != Success
            || !(dtype & GLX_PIXMAP_BIT))
        {
            continue;
        }
        if (sGLXLib.xGetFBConfigAttrib(display, cfgs[i], GLX_VISUAL_ID, &visid) != Success
            || visid == 0)
        {
            continue;
        }

        chosenIndex = i;
        break;
    }

    if (!visid) {
        NS_WARNING("glXChooseFBConfig() didn't give us any configs with visuals!");
        return nullptr;
    }

    Visual *visual;
    int depth;
    FindVisualAndDepth(display, visid, &visual, &depth);
    ScopedXErrorHandler xErrorHandler;
    GLXPixmap glxpixmap = 0;
    bool error = false;

    nsRefPtr<gfxXlibSurface> xsurface = gfxXlibSurface::Create(DefaultScreenOfDisplay(display),
                                                               visual,
                                                               gfxIntSize(16, 16));
    if (xsurface->CairoStatus() != 0) {
        error = true;
        goto DONE_CREATING_PIXMAP;
    }

    // Handle slightly different signature between glXCreatePixmap and
    // its pre-GLX-1.3 extension equivalent (though given the ABI, we
    // might not need to).
    if (sGLXLib.GLXVersionCheck(1, 3)) {
        glxpixmap = sGLXLib.xCreatePixmap(display,
                                          cfgs[chosenIndex],
                                          xsurface->XDrawable(),
                                          NULL);
    } else {
        glxpixmap = sGLXLib.xCreateGLXPixmapWithConfig(display,
                                                       cfgs[chosenIndex],
                                                       xsurface->
                                                       XDrawable());
    }
    if (glxpixmap == 0) {
        error = true;
    }

DONE_CREATING_PIXMAP:

    nsRefPtr<GLContextGLX> glContext;
    bool serverError = xErrorHandler.SyncAndGetError(display);

    if (!error && // earlier recorded error
        !serverError)
    {
      GLContext::ContextFlags flag = aLibToUse == GLXLibrary::OPENGL_LIB
                                       ? GLContext::ContextFlagsNone
                                       : GLContext::ContextFlagsMesaLLVMPipe;
        glContext = GLContextGLX::CreateGLContext(
                        aFormat,
                        display,
                        glxpixmap,
                        cfgs[chosenIndex],
                        aShare ? GetGlobalContextGLX(flag) : nullptr,
                        true,
                        aLibToUse,
                        xsurface);
    }

    return glContext.forget();
}

already_AddRefed<GLContext>
GLContextProviderGLX::CreateOffscreen(const gfxIntSize& aSize,
                                      const ContextFormat& aFormat,
                                      const ContextFlags aFlag)
{
    gCurrLib = GLXLibrary::SelectLibrary(aFlag);
    nsRefPtr<GLContextGLX> glContext =
        CreateOffscreenPixmapContext(aSize, aFormat, true, gCurrLib);

    if (!glContext) {
        return nullptr;
    }

    if (!glContext->GetSharedContext()) {
        // no point in returning anything if sharing failed, we can't
        // render from this
        return nullptr;
    }

    if (!glContext->ResizeOffscreenFBOs(aSize, true)) {
        // we weren't able to create the initial
        // offscreen FBO, so this is dead
        return nullptr;
    }

    return glContext.forget();
}

static nsRefPtr<GLContext> gGlobalContext[GLXLibrary::LIBS_MAX];

GLContext *
GLContextProviderGLX::GetGlobalContext(const ContextFlags aFlag)
{
    LibType libToUse = GLXLibrary::SelectLibrary(aFlag);
    static bool triedToCreateContext[GLXLibrary::LIBS_MAX] = {false, false};
    if (!triedToCreateContext[libToUse] && !gGlobalContext[libToUse]) {
        triedToCreateContext[libToUse] = true;
        gGlobalContext[libToUse] = CreateOffscreenPixmapContext(gfxIntSize(1, 1),
                                                      ContextFormat(ContextFormat::BasicRGB24),
                                                      false, libToUse);
        if (gGlobalContext[libToUse])
            gGlobalContext[libToUse]->SetIsGlobalSharedContext(true);
    }

    return gGlobalContext[libToUse];
}

void
GLContextProviderGLX::Shutdown()
{
    for (int i = 0; i < GLXLibrary::LIBS_MAX; ++i)
      gGlobalContext[i] = nullptr;
}

} /* namespace gl */
} /* namespace mozilla */

