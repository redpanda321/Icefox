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
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Bas Schouten <bschouten@mozilla.com>
 *   Vladimir Vukicevic <vladimir@pobox.com>
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

#include "GLContextProvider.h"
#include "GLContext.h"
#include "nsDebug.h"
#include "nsIWidget.h"
#include "WGLLibrary.h"
#include "gfxASurface.h"
#include "gfxImageSurface.h"
#include "gfxPlatform.h"
#include "gfxWindowsSurface.h"

namespace mozilla {
namespace gl {

WGLLibrary sWGLLibrary;

static HWND gSharedWindow = 0;
static HDC gSharedWindowDC = 0;
static HGLRC gSharedWindowGLContext = 0;
static int gSharedWindowPixelFormat = 0;

static HWND
CreateDummyWindow(HDC *aWindowDC = nsnull)
{
    WNDCLASSW wc;
    if (!GetClassInfoW(GetModuleHandle(NULL), L"GLContextWGLClass", &wc)) {
        ZeroMemory(&wc, sizeof(WNDCLASSW));
        wc.style = CS_OWNDC;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpfnWndProc = DefWindowProc;
        wc.lpszClassName = L"GLContextWGLClass";
        if (!RegisterClassW(&wc)) {
            NS_WARNING("Failed to register GLContextWGLClass?!");
            // er. failed to register our class?
            return NULL;
        }
    }

    HWND win = CreateWindowW(L"GLContextWGLClass", L"GLContextWGL", 0,
                             0, 0, 16, 16,
                             NULL, NULL, GetModuleHandle(NULL), NULL);
    NS_ENSURE_TRUE(win, NULL);

    HDC dc = GetDC(win);
    NS_ENSURE_TRUE(dc, NULL);

    if (gSharedWindowPixelFormat == 0) {
        PIXELFORMATDESCRIPTOR pfd;
        ZeroMemory(&pfd, sizeof(PIXELFORMATDESCRIPTOR));
        pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 24;
        pfd.cRedBits = 8;
        pfd.cGreenBits = 8;
        pfd.cBlueBits = 8;
        pfd.cAlphaBits = 8;
        pfd.cDepthBits = 0;
        pfd.iLayerType = PFD_MAIN_PLANE;

        gSharedWindowPixelFormat = ChoosePixelFormat(dc, &pfd);
    }

    if (!SetPixelFormat(dc, gSharedWindowPixelFormat, NULL)) {
        NS_WARNING("SetPixelFormat failed!");
        DestroyWindow(win);
        return NULL;
    }

    if (aWindowDC) {
        *aWindowDC = dc;
    }

    return win;
}

PRBool
WGLLibrary::EnsureInitialized()
{
    if (mInitialized)
        return PR_TRUE;

    if (!mOGLLibrary) {
        mOGLLibrary = PR_LoadLibrary("Opengl32.dll");
        if (!mOGLLibrary) {
            NS_WARNING("Couldn't load OpenGL DLL.");
            return PR_FALSE;
        }
    }

    LibrarySymbolLoader::SymLoadStruct earlySymbols[] = {
        { (PRFuncPtr*) &fCreateContext, { "wglCreateContext", NULL } },
        { (PRFuncPtr*) &fMakeCurrent, { "wglMakeCurrent", NULL } },
        { (PRFuncPtr*) &fGetProcAddress, { "wglGetProcAddress", NULL } },
        { (PRFuncPtr*) &fDeleteContext, { "wglDeleteContext", NULL } },
        { (PRFuncPtr*) &fGetCurrentContext, { "wglGetCurrentContext", NULL } },
        { (PRFuncPtr*) &fGetCurrentDC, { "wglGetCurrentDC", NULL } },
        { (PRFuncPtr*) &fShareLists, { "wglShareLists", NULL } },
        { NULL, { NULL } }
    };

    if (!LibrarySymbolLoader::LoadSymbols(mOGLLibrary, &earlySymbols[0])) {
        NS_WARNING("Couldn't find required entry points in OpenGL DLL (early init)");
        return PR_FALSE;
    }

    // This is ridiculous -- we have to actually create a context to
    // get the OpenGL ICD to load.
    gSharedWindow = CreateDummyWindow(&gSharedWindowDC);
    NS_ENSURE_TRUE(gSharedWindow, PR_FALSE);

    // create rendering context
    gSharedWindowGLContext = fCreateContext(gSharedWindowDC);
    NS_ENSURE_TRUE(gSharedWindowGLContext, PR_FALSE);

    HGLRC curCtx = fGetCurrentContext();
    HDC curDC = fGetCurrentDC();

    if (!fMakeCurrent((HDC)gSharedWindowDC, (HGLRC)gSharedWindowGLContext)) {
        NS_WARNING("wglMakeCurrent failed");
        return PR_FALSE;
    }

    // Now we can grab all the other symbols that we couldn't without having
    // a context current.

    LibrarySymbolLoader::SymLoadStruct pbufferSymbols[] = {
        { (PRFuncPtr*) &fCreatePbuffer, { "wglCreatePbufferARB", "wglCreatePbufferEXT", NULL } },
        { (PRFuncPtr*) &fDestroyPbuffer, { "wglDestroyPbufferARB", "wglDestroyPbufferEXT", NULL } },
        { (PRFuncPtr*) &fGetPbufferDC, { "wglGetPbufferDCARB", "wglGetPbufferDCEXT", NULL } },
        { (PRFuncPtr*) &fBindTexImage, { "wglBindTexImageARB", "wglBindTexImageEXT", NULL } },
        { (PRFuncPtr*) &fReleaseTexImage, { "wglReleaseTexImageARB", "wglReleaseTexImageEXT", NULL } },
        { NULL, { NULL } }
    };

    LibrarySymbolLoader::SymLoadStruct pixFmtSymbols[] = {
        { (PRFuncPtr*) &fChoosePixelFormat, { "wglChoosePixelFormatARB", "wglChoosePixelFormatEXT", NULL } },
        { (PRFuncPtr*) &fGetPixelFormatAttribiv, { "wglGetPixelFormatAttribivARB", "wglGetPixelFormatAttribivEXT", NULL } },
        { NULL, { NULL } }
    };

    if (!LibrarySymbolLoader::LoadSymbols(mOGLLibrary, &pbufferSymbols[0],
         (LibrarySymbolLoader::PlatformLookupFunction)fGetProcAddress))
    {
        // this isn't an error, just means that pbuffers aren't supported
        fCreatePbuffer = nsnull;
    }

    if (!LibrarySymbolLoader::LoadSymbols(mOGLLibrary, &pixFmtSymbols[0],
         (LibrarySymbolLoader::PlatformLookupFunction)fGetProcAddress))
    {
        // this isn't an error, just means that we don't have the pixel format extension
        fChoosePixelFormat = nsnull;
    }

    // reset back to the previous context, just in case
    fMakeCurrent(curDC, curCtx);

    mInitialized = PR_TRUE;

    // Call this to create the global GLContext instance,
    // and to check for errors.  Note that this must happen /after/
    // setting mInitialized to TRUE, or an infinite loop results.
    if (GLContextProviderWGL::GetGlobalContext() == nsnull) {
        mInitialized = PR_FALSE;
        return PR_FALSE;
    }

    return PR_TRUE;
}

class GLContextWGL : public GLContext
{
public:
    GLContextWGL(const ContextFormat& aFormat,
                 GLContext *aSharedContext,
                 HDC aDC,
                 HGLRC aContext,
                 HWND aWindow = nsnull,
                 PRBool aIsOffscreen = PR_FALSE)
        : GLContext(aFormat, aIsOffscreen, aSharedContext),
          mDC(aDC),
          mContext(aContext),
          mWnd(aWindow),
          mPBuffer(NULL),
          mPixelFormat(0)
    {
    }

    GLContextWGL(const ContextFormat& aFormat,
                 GLContext *aSharedContext,
                 HANDLE aPbuffer,
                 HDC aDC,
                 HGLRC aContext,
                 int aPixelFormat)
        : GLContext(aFormat, PR_TRUE, aSharedContext),
          mDC(aDC),
          mContext(aContext),
          mWnd(NULL),
          mPBuffer(aPbuffer),
          mPixelFormat(aPixelFormat)
    {
    }

    ~GLContextWGL()
    {
        if (mOffscreenFBO) {
            MakeCurrent();
            DeleteOffscreenFBO();
        }

        sWGLLibrary.fDeleteContext(mContext);

        if (mPBuffer)
            sWGLLibrary.fDestroyPbuffer(mPBuffer);
        if (mWnd)
            DestroyWindow(mWnd);
    }

    GLContextType GetContextType() {
        return ContextTypeWGL;
    }

    PRBool Init()
    {
        if (!mDC || !mContext)
            return PR_FALSE;

        MakeCurrent();
        SetupLookupFunction();
        return InitWithPrefix("gl", PR_TRUE);
    }

    PRBool MakeCurrent()
    {
        BOOL succeeded = PR_TRUE;

        // wglGetCurrentContext seems to just pull the HGLRC out
        // of its TLS slot, so no need to do our own tls slot.
        // You would think that wglMakeCurrent would avoid doing
        // work if mContext was already current, but not so much..
        if (sWGLLibrary.fGetCurrentContext() != mContext) {
            succeeded = sWGLLibrary.fMakeCurrent(mDC, mContext);
            NS_ASSERTION(succeeded, "Failed to make GL context current!");
        }

        return succeeded;
    }

    PRBool SetupLookupFunction()
    {
        mLookupFunc = (PlatformLookupFunction)sWGLLibrary.fGetProcAddress;
        return PR_TRUE;
    }

    void *GetNativeData(NativeDataType aType)
    {
        switch (aType) {
        case NativeGLContext:
            return mContext;

        default:
            return nsnull;
        }
    }

    PRBool BindTex2DOffscreen(GLContext *aOffscreen);
    void UnbindTex2DOffscreen(GLContext *aOffscreen);
    PRBool ResizeOffscreen(const gfxIntSize& aNewSize);

    HGLRC Context() { return mContext; }

    virtual already_AddRefed<TextureImage>
    CreateBasicTextureImage(GLuint aTexture,
                            const nsIntSize& aSize,
                            TextureImage::ContentType aContentType,
                            GLContext* aContext);

protected:
    friend class GLContextProviderWGL;

    HDC mDC;
    HGLRC mContext;
    HWND mWnd;
    HANDLE mPBuffer;
    int mPixelFormat;
};

PRBool
GLContextWGL::BindTex2DOffscreen(GLContext *aOffscreen)
{
    if (aOffscreen->GetContextType() != ContextTypeWGL) {
        NS_WARNING("non-WGL context");
        return PR_FALSE;
    }

    if (!aOffscreen->IsOffscreen()) {
        NS_WARNING("non-offscreen context");
        return PR_FALSE;
    }

    GLContextWGL *offs = static_cast<GLContextWGL*>(aOffscreen);

    if (offs->mPBuffer) {
        BOOL ok = sWGLLibrary.fBindTexImage(offs->mPBuffer,
                                            LOCAL_WGL_FRONT_LEFT_ARB);
        if (!ok) {
            NS_WARNING("CanvasLayerOGL::Updated wglBindTexImageARB failed");
            return PR_FALSE;
        }
    } else if (offs->mOffscreenTexture) {
        if (offs->GetSharedContext() != GLContextProviderWGL::GetGlobalContext())
        {
            NS_WARNING("offscreen FBO context can only be bound with context sharing!");
            return PR_FALSE;
        }

        fBindTexture(LOCAL_GL_TEXTURE_2D, offs->mOffscreenTexture);
    } else {
        NS_WARNING("don't know how to bind this!");
        return PR_FALSE;
    }

    return PR_TRUE;
}

void
GLContextWGL::UnbindTex2DOffscreen(GLContext *aOffscreen)
{
    NS_ASSERTION(aOffscreen->GetContextType() == ContextTypeWGL, "wrong type");

    GLContextWGL *offs = static_cast<GLContextWGL*>(aOffscreen);
    if (offs->mPBuffer) {
        // XXX so, according to the extension, ReleaseTexImage is not required to
        // preserve color buffer contents.  This sucks, but everywhere that I've
        // tried it the color buffer is preserved.  So let's cross our fingers..
        sWGLLibrary.fReleaseTexImage(offs->mPBuffer, LOCAL_WGL_FRONT_LEFT_ARB);
    }
}

PRBool
GLContextWGL::ResizeOffscreen(const gfxIntSize& aNewSize)
{
    if (mPBuffer) {
        int pbattrs[] = {
            LOCAL_WGL_TEXTURE_FORMAT_ARB,
              mCreationFormat.alpha > 0 ? LOCAL_WGL_TEXTURE_RGBA_ARB
                                        : LOCAL_WGL_TEXTURE_RGB_ARB,
            LOCAL_WGL_TEXTURE_TARGET_ARB, LOCAL_WGL_TEXTURE_2D_ARB,
            0
        };

        HANDLE newbuf = sWGLLibrary.fCreatePbuffer(gSharedWindowDC, mPixelFormat,
                                                   aNewSize.width, aNewSize.height,
                                                   pbattrs);
        if (!newbuf)
            return PR_FALSE;

        bool isCurrent = false;
        if (sWGLLibrary.fGetCurrentContext() == mContext) {
            sWGLLibrary.fMakeCurrent(NULL, NULL);
            isCurrent = true;
        }

        sWGLLibrary.fDestroyPbuffer(mPBuffer);

        mPBuffer = newbuf;
        mDC = sWGLLibrary.fGetPbufferDC(mPBuffer);

        mOffscreenSize = aNewSize;
        mOffscreenActualSize = aNewSize;

        MakeCurrent();
        ClearSafely();

        return PR_TRUE;
    }

    return ResizeOffscreenFBO(aNewSize);
}

static GLContextWGL *
GetGlobalContextWGL()
{
    return static_cast<GLContextWGL*>(GLContextProviderWGL::GetGlobalContext());
}

class TextureImageWGL : public BasicTextureImage
{
    friend already_AddRefed<TextureImage>
    GLContextWGL::CreateBasicTextureImage(GLuint,
                                          const nsIntSize&,
                                          TextureImage::ContentType,
                                          GLContext*);

protected:
    virtual already_AddRefed<gfxASurface>
    CreateUpdateSurface(const gfxIntSize& aSize, ImageFormat aFmt)
    {
        return gfxPlatform::GetPlatform()->CreateOffscreenSurface(aSize, aFmt);
    }

    virtual already_AddRefed<gfxImageSurface>
    GetImageForUpload(gfxASurface* aUpdateSurface)
    {
        NS_ASSERTION(gfxASurface::SurfaceTypeWin32 == aUpdateSurface->GetType(),
                     "unexpected surface type");
        nsRefPtr<gfxImageSurface> uploadImage(
            static_cast<gfxWindowsSurface*>(aUpdateSurface)->
            GetImageSurface());
        return uploadImage.forget();
    }

private:
    TextureImageWGL(GLuint aTexture,
                    const nsIntSize& aSize,
                    ContentType aContentType,
                    GLContext* aContext)
        : BasicTextureImage(aTexture, aSize, aContentType, aContext)
    {}
};

already_AddRefed<TextureImage>
GLContextWGL::CreateBasicTextureImage(GLuint aTexture,
                                      const nsIntSize& aSize,
                                      TextureImage::ContentType aContentType,
                                      GLContext* aContext)
{
    nsRefPtr<TextureImageWGL> teximage(
        new TextureImageWGL(aTexture, aSize, aContentType, aContext));
    return teximage.forget();
}

already_AddRefed<GLContext>
GLContextProviderWGL::CreateForWindow(nsIWidget *aWidget)
{
    if (!sWGLLibrary.EnsureInitialized()) {
        return nsnull;
    }

    /**
       * We need to make sure we call SetPixelFormat -after- calling 
       * EnsureInitialized, otherwise it can load/unload the dll and 
       * wglCreateContext will fail.
       */

    HDC dc = (HDC)aWidget->GetNativeData(NS_NATIVE_GRAPHIC);

    SetPixelFormat(dc, gSharedWindowPixelFormat, NULL);
    HGLRC context = sWGLLibrary.fCreateContext(dc);
    if (!context) {
        return nsnull;
    }

    GLContextWGL *shareContext = GetGlobalContextWGL();
    if (shareContext &&
        !sWGLLibrary.fShareLists(shareContext->Context(), context))
    {
        shareContext = nsnull;
    }

    nsRefPtr<GLContextWGL> glContext = new GLContextWGL(ContextFormat(ContextFormat::BasicRGB24),
                                                        shareContext, dc, context);
    if (!glContext->Init()) {
        return nsnull;
    }

    return glContext.forget();
}

static already_AddRefed<GLContextWGL>
CreatePBufferOffscreenContext(const gfxIntSize& aSize,
                              const ContextFormat& aFormat)
{
#define A1(_a,_x)  do { _a.AppendElement(_x); } while(0)
#define A2(_a,_x,_y)  do { _a.AppendElement(_x); _a.AppendElement(_y); } while(0)

    nsTArray<int> attrs;

    A2(attrs, LOCAL_WGL_SUPPORT_OPENGL_ARB, LOCAL_GL_TRUE);
    A2(attrs, LOCAL_WGL_DRAW_TO_PBUFFER_ARB, LOCAL_GL_TRUE);
    A2(attrs, LOCAL_WGL_DOUBLE_BUFFER_ARB, LOCAL_GL_FALSE);

    A2(attrs, LOCAL_WGL_ACCELERATION_ARB, LOCAL_WGL_FULL_ACCELERATION_ARB);

    A2(attrs, LOCAL_WGL_COLOR_BITS_ARB, aFormat.colorBits());
    A2(attrs, LOCAL_WGL_RED_BITS_ARB, aFormat.red);
    A2(attrs, LOCAL_WGL_GREEN_BITS_ARB, aFormat.green);
    A2(attrs, LOCAL_WGL_BLUE_BITS_ARB, aFormat.blue);
    A2(attrs, LOCAL_WGL_ALPHA_BITS_ARB, aFormat.alpha);

    A2(attrs, LOCAL_WGL_DEPTH_BITS_ARB, aFormat.depth);

    if (aFormat.alpha > 0) {
        A2(attrs, LOCAL_WGL_BIND_TO_TEXTURE_RGBA_ARB, LOCAL_GL_TRUE);
    } else {
        A2(attrs, LOCAL_WGL_BIND_TO_TEXTURE_RGB_ARB, LOCAL_GL_TRUE);
    }

    A2(attrs, LOCAL_WGL_DOUBLE_BUFFER_ARB, LOCAL_GL_FALSE);
    A2(attrs, LOCAL_WGL_STEREO_ARB, LOCAL_GL_FALSE);

    A1(attrs, 0);

    nsTArray<int> pbattrs;
    A2(pbattrs, LOCAL_WGL_TEXTURE_TARGET_ARB, LOCAL_WGL_TEXTURE_2D_ARB);

    if (aFormat.alpha > 0) {
        A2(pbattrs, LOCAL_WGL_TEXTURE_FORMAT_ARB, LOCAL_WGL_TEXTURE_RGBA_ARB);
    } else {
        A2(pbattrs, LOCAL_WGL_TEXTURE_FORMAT_ARB, LOCAL_WGL_TEXTURE_RGB_ARB);
    }
    A1(pbattrs, 0);

    UINT numFormats = 256;
    int formats[256];

    if (!sWGLLibrary.fChoosePixelFormat(gSharedWindowDC,
                                        attrs.Elements(), NULL,
                                        numFormats, formats, &numFormats)
        || numFormats == 0)
    {
        return nsnull;
    }

    // XXX add back the priority choosing code here
    int chosenFormat = formats[0];

    HANDLE pbuffer = sWGLLibrary.fCreatePbuffer(gSharedWindowDC, chosenFormat,
                                                aSize.width, aSize.height,
                                                pbattrs.Elements());
    if (!pbuffer) {
        return nsnull;
    }

    HDC pbdc = sWGLLibrary.fGetPbufferDC(pbuffer);
    NS_ASSERTION(pbdc, "expected a dc");

    HGLRC context = sWGLLibrary.fCreateContext(pbdc);
    if (!context) {
        sWGLLibrary.fDestroyPbuffer(pbuffer);
        return PR_FALSE;
    }

    nsRefPtr<GLContextWGL> glContext = new GLContextWGL(aFormat,
                                                        nsnull,
                                                        pbuffer,
                                                        pbdc,
                                                        context,
                                                        chosenFormat);

    return glContext.forget();
}

static already_AddRefed<GLContextWGL>
CreateWindowOffscreenContext(const gfxIntSize& aSize,
                             const ContextFormat& aFormat)
{
    // CreateWindowOffscreenContext must return a global-shared context
    GLContextWGL *shareContext = GetGlobalContextWGL();
    if (!shareContext) {
        return nsnull;
    }
    
    HDC dc;
    HWND win = CreateDummyWindow(&dc);
    if (!win) {
        return nsnull;
    }
    
    HGLRC context = sWGLLibrary.fCreateContext(dc);
    if (!context) {
        return nsnull;
    }

    if (!sWGLLibrary.fShareLists(shareContext->Context(), context)) {
        NS_WARNING("wglShareLists failed!");

        sWGLLibrary.fDeleteContext(context);
        DestroyWindow(win);
        return nsnull;
    }

    nsRefPtr<GLContextWGL> glContext = new GLContextWGL(aFormat, shareContext,
                                                        dc, context, win, PR_TRUE);

    return glContext.forget();
}

already_AddRefed<GLContext>
GLContextProviderWGL::CreateOffscreen(const gfxIntSize& aSize,
                                      const ContextFormat& aFormat)
{
    if (!sWGLLibrary.EnsureInitialized()) {
        return nsnull;
    }

    nsRefPtr<GLContextWGL> glContext;

    // Always try to create a pbuffer context first, because we
    // want the context isolation.
    if (sWGLLibrary.fCreatePbuffer &&
        sWGLLibrary.fChoosePixelFormat)
    {
        glContext = CreatePBufferOffscreenContext(aSize, aFormat);
    }

    // If it failed, then create a window context and use a FBO.
    if (!glContext) {
        glContext = CreateWindowOffscreenContext(aSize, aFormat);
    }

    if (!glContext ||
        !glContext->Init())
    {
        return nsnull;
    }

    glContext->mOffscreenSize = aSize;
    glContext->mOffscreenActualSize = aSize;

    if (!glContext->mPBuffer &&
        !glContext->ResizeOffscreenFBO(aSize))
    {
        return nsnull;
    }

    return glContext.forget();
}

already_AddRefed<GLContext>
GLContextProviderWGL::CreateForNativePixmapSurface(gfxASurface *aSurface)
{
    return nsnull;
}

static nsRefPtr<GLContextWGL> gGlobalContext;

GLContext *
GLContextProviderWGL::GetGlobalContext()
{
    if (!sWGLLibrary.EnsureInitialized()) {
        return nsnull;
    }

    static bool triedToCreateContext = false;

    if (!triedToCreateContext && !gGlobalContext) {
        triedToCreateContext = true;

        // conveniently, we already have what we need...
        gGlobalContext = new GLContextWGL(ContextFormat(ContextFormat::BasicRGB24), nsnull,
                                          gSharedWindowDC, gSharedWindowGLContext);
        if (!gGlobalContext->Init()) {
            NS_WARNING("Global context GLContext initialization failed?");
            gGlobalContext = nsnull;
            return PR_FALSE;
        }

        gGlobalContext->SetIsGlobalSharedContext(PR_TRUE);
    }

    return static_cast<GLContext*>(gGlobalContext);
}

void
GLContextProviderWGL::Shutdown()
{
}

} /* namespace gl */
} /* namespace mozilla */
