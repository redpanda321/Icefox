/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LayerManagerOGL.h"

#include "mozilla/layers/PLayers.h"

/* This must occur *after* layers/PLayers.h to avoid typedefs conflicts. */
#include "mozilla/Util.h"

#include "Composer2D.h"
#include "ThebesLayerOGL.h"
#include "ContainerLayerOGL.h"
#include "ImageLayerOGL.h"
#include "ColorLayerOGL.h"
#include "CanvasLayerOGL.h"
#include "TiledThebesLayerOGL.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Preferences.h"
#include "TexturePoolOGL.h"

#include "gfxContext.h"
#include "gfxUtils.h"
#include "gfxPlatform.h"
#include "nsIWidget.h"

#include "GLContext.h"
#include "GLContextProvider.h"

#include "nsIServiceManager.h"
#include "nsIConsoleService.h"

#include "gfxCrashReporterUtils.h"

#include "sampler.h"

#ifdef MOZ_WIDGET_ANDROID
#include <android/log.h>
#endif
#ifdef XP_MACOSX
#include "gfxPlatformMac.h"
#endif

namespace mozilla {
namespace layers {

using namespace mozilla::gfx;
using namespace mozilla::gl;

#ifdef CHECK_CURRENT_PROGRAM
int ShaderProgramOGL::sCurrentProgramKey = 0;
#endif

bool
LayerManagerOGL::Initialize(bool force)
{
  return Initialize(CreateContext(), force);
}

int32_t
LayerManagerOGL::GetMaxTextureSize() const
{
  int32_t maxSize;
  mGLContext->fGetIntegerv(LOCAL_GL_MAX_TEXTURE_SIZE, &maxSize);
  return maxSize;
}

void
LayerManagerOGL::MakeCurrent(bool aForce)
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return;
  }
  mGLContext->MakeCurrent(aForce);
}

void*
LayerManagerOGL::GetNSOpenGLContext() const
{
  return gl()->GetNativeData(GLContext::NativeGLContext);
}


void
LayerManagerOGL::BindQuadVBO() {
  mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, mQuadVBO);
}

void
LayerManagerOGL::QuadVBOVerticesAttrib(GLuint aAttribIndex) {
  mGLContext->fVertexAttribPointer(aAttribIndex, 2,
                                   LOCAL_GL_FLOAT, LOCAL_GL_FALSE, 0,
                                   (GLvoid*) QuadVBOVertexOffset());
}

void
LayerManagerOGL::QuadVBOTexCoordsAttrib(GLuint aAttribIndex) {
  mGLContext->fVertexAttribPointer(aAttribIndex, 2,
                                   LOCAL_GL_FLOAT, LOCAL_GL_FALSE, 0,
                                   (GLvoid*) QuadVBOTexCoordOffset());
}

void
LayerManagerOGL::QuadVBOFlippedTexCoordsAttrib(GLuint aAttribIndex) {
  mGLContext->fVertexAttribPointer(aAttribIndex, 2,
                                   LOCAL_GL_FLOAT, LOCAL_GL_FALSE, 0,
                                   (GLvoid*) QuadVBOFlippedTexCoordOffset());
}

// Super common

void
LayerManagerOGL::BindAndDrawQuad(GLuint aVertAttribIndex,
                     GLuint aTexCoordAttribIndex,
                     bool aFlipped)
{
  BindQuadVBO();
  QuadVBOVerticesAttrib(aVertAttribIndex);

  if (aTexCoordAttribIndex != GLuint(-1)) {
    if (aFlipped)
      QuadVBOFlippedTexCoordsAttrib(aTexCoordAttribIndex);
    else
      QuadVBOTexCoordsAttrib(aTexCoordAttribIndex);

    mGLContext->fEnableVertexAttribArray(aTexCoordAttribIndex);
  }

  mGLContext->fEnableVertexAttribArray(aVertAttribIndex);

  mGLContext->fDrawArrays(LOCAL_GL_TRIANGLE_STRIP, 0, 4);

  mGLContext->fDisableVertexAttribArray(aVertAttribIndex);

  if (aTexCoordAttribIndex != GLuint(-1)) {
    mGLContext->fDisableVertexAttribArray(aTexCoordAttribIndex);
  }
}

static const double kFpsWindowMs = 250.0;
static const size_t kNumFrameTimeStamps = 16;
struct FPSCounter {
  FPSCounter() : mCurrentFrameIndex(0) {}

  // We keep a circular buffer of the time points at which the last K
  // frames were drawn.  To estimate FPS, we count the number of
  // frames we've drawn within the last kFPSWindowMs milliseconds and
  // divide by the amount time since the first of those frames.
  TimeStamp mFrames[kNumFrameTimeStamps];
  size_t mCurrentFrameIndex;

  void AddFrame(TimeStamp aNewFrame) {
    mFrames[mCurrentFrameIndex] = aNewFrame;
    mCurrentFrameIndex = (mCurrentFrameIndex + 1) % kNumFrameTimeStamps;
  }

  double AddFrameAndGetFps(TimeStamp aCurrentFrame) {
    AddFrame(aCurrentFrame);
    return EstimateFps(aCurrentFrame);
  }

  double GetFpsAt(TimeStamp aNow) {
    return EstimateFps(aNow);
  }

private:
  double EstimateFps(TimeStamp aNow) {
    TimeStamp beginningOfWindow =
      (aNow - TimeDuration::FromMilliseconds(kFpsWindowMs));
    TimeStamp earliestFrameInWindow = aNow;
    size_t numFramesDrawnInWindow = 0;
    for (size_t i = 0; i < kNumFrameTimeStamps; ++i) {
      const TimeStamp& frame = mFrames[i];
      if (!frame.IsNull() && frame > beginningOfWindow) {
        ++numFramesDrawnInWindow;
        earliestFrameInWindow = NS_MIN(earliestFrameInWindow, frame);
      }
    }
    double realWindowSecs = (aNow - earliestFrameInWindow).ToSeconds();
    if (realWindowSecs == 0.0 || numFramesDrawnInWindow == 1) {
      return 0.0;
    }
    return double(numFramesDrawnInWindow - 1) / realWindowSecs;
  }
};

struct FPSState {
  GLuint mTexture;
  FPSCounter mCompositionFps;
  FPSCounter mTransactionFps;

  FPSState() : mTexture(0) { }

  void DrawFPS(TimeStamp, GLContext*, ShaderProgramOGL*);

  static void DrawFrameCounter(GLContext* context);

  void NotifyShadowTreeTransaction() {
    mTransactionFps.AddFrame(TimeStamp::Now());
  }
};


void
FPSState::DrawFPS(TimeStamp aNow,
                  GLContext* context, ShaderProgramOGL* copyprog)
{
  int fps = int(mCompositionFps.AddFrameAndGetFps(aNow));
  int txnFps = int(mTransactionFps.GetFpsAt(aNow));

  GLint viewport[4];
  context->fGetIntegerv(LOCAL_GL_VIEWPORT, viewport);

  if (!mTexture) {
    // Bind the number of textures we need, in this case one.
    context->fGenTextures(1, &mTexture);
    context->fBindTexture(LOCAL_GL_TEXTURE_2D, mTexture);
    context->fTexParameteri(LOCAL_GL_TEXTURE_2D,LOCAL_GL_TEXTURE_MIN_FILTER,LOCAL_GL_NEAREST);
    context->fTexParameteri(LOCAL_GL_TEXTURE_2D,LOCAL_GL_TEXTURE_MAG_FILTER,LOCAL_GL_NEAREST);

    unsigned char text[] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0, 255, 255, 255,   0, 255, 255,   0,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255,   0, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0,
      0, 255,   0, 255,   0,   0, 255,   0,   0,   0,   0, 255,   0,   0,   0, 255,   0, 255,   0, 255,   0, 255,   0,   0,   0, 255,   0,   0,   0,   0,   0, 255,   0, 255,   0, 255,   0, 255,   0, 255,   0,
      0, 255,   0, 255,   0,   0, 255,   0,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0,   0,   0, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0,
      0, 255,   0, 255,   0,   0, 255,   0,   0, 255,   0,   0,   0,   0,   0, 255,   0,   0,   0, 255,   0,   0,   0, 255,   0, 255,   0, 255,   0,   0,   0, 255,   0, 255,   0, 255,   0,   0,   0, 255,   0,
      0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0,   0,   0, 255,   0, 255, 255, 255,   0, 255, 255, 255,   0,   0,   0, 255,   0, 255, 255, 255,   0,   0,   0, 255,   0,
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    };

    // convert from 8 bit to 32 bit so that don't have to write the text above out in 32 bit format
    // we rely on int being 32 bits
    unsigned int* buf = (unsigned int*)malloc(64 * 8 * 4);
    for (int i = 0; i < 7; i++) {
      for (int j = 0; j < 41; j++) {
        unsigned int purple = 0xfff000ff;
        unsigned int white  = 0xffffffff;
        buf[i * 64 + j] = (text[i * 41 + j] == 0) ? purple : white;
      }
    }
    context->fTexImage2D(LOCAL_GL_TEXTURE_2D, 0, LOCAL_GL_RGBA, 64, 8, 0, LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE, buf);
    free(buf);
  }

  struct Vertex2D {
    float x,y;
  };
  const Vertex2D vertices[] = {
    { -1.0f, 1.0f - 42.f / viewport[3] },
    { -1.0f, 1.0f},
    { -1.0f + 22.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 22.f / viewport[2], 1.0f },

    {  -1.0f + 22.f / viewport[2], 1.0f - 42.f / viewport[3] },
    {  -1.0f + 22.f / viewport[2], 1.0f },
    {  -1.0f + 44.f / viewport[2], 1.0f - 42.f / viewport[3] },
    {  -1.0f + 44.f / viewport[2], 1.0f },

    { -1.0f + 44.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 44.f / viewport[2], 1.0f },
    { -1.0f + 66.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 66.f / viewport[2], 1.0f }
  };

  const Vertex2D vertices2[] = {
    { -1.0f + 80.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 80.f / viewport[2], 1.0f },
    { -1.0f + 102.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 102.f / viewport[2], 1.0f },
    
    { -1.0f + 102.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 102.f / viewport[2], 1.0f },
    { -1.0f + 124.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 124.f / viewport[2], 1.0f },
    
    { -1.0f + 124.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 124.f / viewport[2], 1.0f },
    { -1.0f + 146.f / viewport[2], 1.0f - 42.f / viewport[3] },
    { -1.0f + 146.f / viewport[2], 1.0f },
  };

  int v1   = fps % 10;
  int v10  = (fps % 100) / 10;
  int v100 = (fps % 1000) / 100;

  int txn1 = txnFps % 10;
  int txn10  = (txnFps % 100) / 10;
  int txn100 = (txnFps % 1000) / 100;

  // Feel free to comment these texture coordinates out and use one
  // of the ones below instead, or play around with your own values.
  const GLfloat texCoords[] = {
    (v100 * 4.f) / 64, 7.f / 8,
    (v100 * 4.f) / 64, 0.0f,
    (v100 * 4.f + 4) / 64, 7.f / 8,
    (v100 * 4.f + 4) / 64, 0.0f,

    (v10 * 4.f) / 64, 7.f / 8,
    (v10 * 4.f) / 64, 0.0f,
    (v10 * 4.f + 4) / 64, 7.f / 8,
    (v10 * 4.f + 4) / 64, 0.0f,

    (v1 * 4.f) / 64, 7.f / 8,
    (v1 * 4.f) / 64, 0.0f,
    (v1 * 4.f + 4) / 64, 7.f / 8,
    (v1 * 4.f + 4) / 64, 0.0f,
  };

  const GLfloat texCoords2[] = {
    (txn100 * 4.f) / 64, 7.f / 8,
    (txn100 * 4.f) / 64, 0.0f,
    (txn100 * 4.f + 4) / 64, 7.f / 8,
    (txn100 * 4.f + 4) / 64, 0.0f,

    (txn10 * 4.f) / 64, 7.f / 8,
    (txn10 * 4.f) / 64, 0.0f,
    (txn10 * 4.f + 4) / 64, 7.f / 8,
    (txn10 * 4.f + 4) / 64, 0.0f,

    (txn1 * 4.f) / 64, 7.f / 8,
    (txn1 * 4.f) / 64, 0.0f,
    (txn1 * 4.f + 4) / 64, 7.f / 8,
    (txn1 * 4.f + 4) / 64, 0.0f,
  };

  // Turn necessary features on
  context->fEnable(LOCAL_GL_BLEND);
  context->fBlendFunc(LOCAL_GL_ONE, LOCAL_GL_SRC_COLOR);

  context->fActiveTexture(LOCAL_GL_TEXTURE0);
  context->fBindTexture(LOCAL_GL_TEXTURE_2D, mTexture);

  copyprog->Activate();
  copyprog->SetTextureUnit(0);

  // we're going to use client-side vertex arrays for this.
  context->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);

  // "COPY"
  context->fBlendFuncSeparate(LOCAL_GL_ONE, LOCAL_GL_ZERO,
                              LOCAL_GL_ONE, LOCAL_GL_ZERO);

  // enable our vertex attribs; we'll call glVertexPointer below
  // to fill with the correct data.
  GLint vcattr = copyprog->AttribLocation(ShaderProgramOGL::VertexCoordAttrib);
  GLint tcattr = copyprog->AttribLocation(ShaderProgramOGL::TexCoordAttrib);

  context->fEnableVertexAttribArray(vcattr);
  context->fEnableVertexAttribArray(tcattr);

  context->fVertexAttribPointer(vcattr,
                                2, LOCAL_GL_FLOAT,
                                LOCAL_GL_FALSE,
                                0, vertices);

  context->fVertexAttribPointer(tcattr,
                                2, LOCAL_GL_FLOAT,
                                LOCAL_GL_FALSE,
                                0, texCoords);

  context->fDrawArrays(LOCAL_GL_TRIANGLE_STRIP, 0, 12);

  context->fVertexAttribPointer(vcattr,
                                2, LOCAL_GL_FLOAT,
                                LOCAL_GL_FALSE,
                                0, vertices2);

  context->fVertexAttribPointer(tcattr,
                                2, LOCAL_GL_FLOAT,
                                LOCAL_GL_FALSE,
                                0, texCoords2);

  context->fDrawArrays(LOCAL_GL_TRIANGLE_STRIP, 0, 12);
}

/**
 * LayerManagerOGL
 */
LayerManagerOGL::LayerManagerOGL(nsIWidget *aWidget, int aSurfaceWidth, int aSurfaceHeight,
                                 bool aIsRenderingToEGLSurface)
  : mWidget(aWidget)
  , mWidgetSize(-1, -1)
  , mSurfaceSize(aSurfaceWidth, aSurfaceHeight)
  , mBackBufferFBO(0)
  , mBackBufferTexture(0)
  , mBackBufferSize(-1, -1)
  , mHasBGRA(0)
  , mIsRenderingToEGLSurface(aIsRenderingToEGLSurface)
#ifdef DEBUG
  , mMaybeInvalidTree(false)
#endif
{
}

LayerManagerOGL::~LayerManagerOGL()
{
  Destroy();
}

void
LayerManagerOGL::Destroy()
{
  if (mDestroyed)
    return;

  if (mRoot) {
    RootLayer()->Destroy();
    mRoot = nullptr;
  }

  if (!mGLContext)
    return;

  nsRefPtr<GLContext> ctx = mGLContext->GetSharedContext();
  if (!ctx) {
    ctx = mGLContext;
  }

  ctx->MakeCurrent();

  for (uint32_t i = 0; i < mPrograms.Length(); ++i) {
    for (uint32_t type = MaskNone; type < NumMaskTypes; ++type) {
      delete mPrograms[i].mVariations[type];
    }
  }
  mPrograms.Clear();

  ctx->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, 0);

  if (mBackBufferFBO) {
    ctx->fDeleteFramebuffers(1, &mBackBufferFBO);
    mBackBufferFBO = 0;
  }

  if (mBackBufferTexture) {
    ctx->fDeleteTextures(1, &mBackBufferTexture);
    mBackBufferTexture = 0;
  }

  if (mQuadVBO) {
    ctx->fDeleteBuffers(1, &mQuadVBO);
    mQuadVBO = 0;
  }

  mGLContext = nullptr;

  mDestroyed = true;
}

already_AddRefed<mozilla::gl::GLContext>
LayerManagerOGL::CreateContext()
{
  nsRefPtr<GLContext> context;

#ifdef XP_WIN
  if (PR_GetEnv("MOZ_LAYERS_PREFER_EGL")) {
    printf_stderr("Trying GL layers...\n");
    context = gl::GLContextProviderEGL::CreateForWindow(mWidget);
  }
#endif

  if (!context)
    context = gl::GLContextProvider::CreateForWindow(mWidget);

  if (!context) {
    NS_WARNING("Failed to create LayerManagerOGL context");
  }
  return context.forget();
}

void
LayerManagerOGL::AddPrograms(ShaderProgramType aType)
{
  for (uint32_t maskType = MaskNone; maskType < NumMaskTypes; ++maskType) {
    if (ProgramProfileOGL::ProgramExists(aType, static_cast<MaskType>(maskType))) {
      mPrograms[aType].mVariations[maskType] = new ShaderProgramOGL(this->gl(),
        ProgramProfileOGL::GetProfileFor(aType, static_cast<MaskType>(maskType)));
    } else {
      mPrograms[aType].mVariations[maskType] = nullptr;
    }
  }
}

bool
LayerManagerOGL::Initialize(nsRefPtr<GLContext> aContext, bool force)
{
  ScopedGfxFeatureReporter reporter("GL Layers", force);

  // Do not allow double initialization
  NS_ABORT_IF_FALSE(mGLContext == nullptr, "Don't reinitialize layer managers");

#ifdef MOZ_WIDGET_ANDROID
  if (!aContext)
    NS_RUNTIMEABORT("We need a context on Android");
#endif

  if (!aContext)
    return false;

  mGLContext = aContext;
  mGLContext->SetFlipped(true);

  MakeCurrent();

  mHasBGRA =
    mGLContext->IsExtensionSupported(gl::GLContext::EXT_texture_format_BGRA8888) ||
    mGLContext->IsExtensionSupported(gl::GLContext::EXT_bgra);

  mGLContext->fBlendFuncSeparate(LOCAL_GL_ONE, LOCAL_GL_ONE_MINUS_SRC_ALPHA,
                                 LOCAL_GL_ONE, LOCAL_GL_ONE);
  mGLContext->fEnable(LOCAL_GL_BLEND);

  mPrograms.AppendElements(NumProgramTypes);
  for (int type = 0; type < NumProgramTypes; ++type) {
    AddPrograms(static_cast<ShaderProgramType>(type));
  }

  // initialise a common shader to check that we can actually compile a shader
  if (!mPrograms[gl::RGBALayerProgramType].mVariations[MaskNone]->Initialize()) {
    return false;
  }


  mGLContext->fGenFramebuffers(1, &mBackBufferFBO);

  if (mGLContext->WorkAroundDriverBugs()) {

    /**
    * We'll test the ability here to bind NPOT textures to a framebuffer, if
    * this fails we'll try ARB_texture_rectangle.
    */

    GLenum textureTargets[] = {
      LOCAL_GL_TEXTURE_2D,
      LOCAL_GL_NONE
    };

    if (mGLContext->IsGLES2()) {
        textureTargets[1] = LOCAL_GL_TEXTURE_RECTANGLE_ARB;
    }

    mFBOTextureTarget = LOCAL_GL_NONE;

    for (uint32_t i = 0; i < ArrayLength(textureTargets); i++) {
      GLenum target = textureTargets[i];
      if (!target)
          continue;

      mGLContext->fGenTextures(1, &mBackBufferTexture);
      mGLContext->fBindTexture(target, mBackBufferTexture);
      mGLContext->fTexParameteri(target,
                                LOCAL_GL_TEXTURE_MIN_FILTER,
                                LOCAL_GL_NEAREST);
      mGLContext->fTexParameteri(target,
                                LOCAL_GL_TEXTURE_MAG_FILTER,
                                LOCAL_GL_NEAREST);
      mGLContext->fTexImage2D(target,
                              0,
                              LOCAL_GL_RGBA,
                              5, 3, /* sufficiently NPOT */
                              0,
                              LOCAL_GL_RGBA,
                              LOCAL_GL_UNSIGNED_BYTE,
                              NULL);

      // unbind this texture, in preparation for binding it to the FBO
      mGLContext->fBindTexture(target, 0);

      mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mBackBufferFBO);
      mGLContext->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER,
                                        LOCAL_GL_COLOR_ATTACHMENT0,
                                        target,
                                        mBackBufferTexture,
                                        0);

      if (mGLContext->fCheckFramebufferStatus(LOCAL_GL_FRAMEBUFFER) ==
          LOCAL_GL_FRAMEBUFFER_COMPLETE)
      {
        mFBOTextureTarget = target;
        break;
      }

      // We weren't succesful with this texture, so we don't need it
      // any more.
      mGLContext->fDeleteTextures(1, &mBackBufferTexture);
    }

    if (mFBOTextureTarget == LOCAL_GL_NONE) {
      /* Unable to find a texture target that works with FBOs and NPOT textures */
      return false;
    }
  } else {
    // not trying to work around driver bugs, so TEXTURE_2D should just work
    mFBOTextureTarget = LOCAL_GL_TEXTURE_2D;
  }

  // back to default framebuffer, to avoid confusion
  mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, 0);

  if (mFBOTextureTarget == LOCAL_GL_TEXTURE_RECTANGLE_ARB) {
    /* If we're using TEXTURE_RECTANGLE, then we must have the ARB
     * extension -- the EXT variant does not provide support for
     * texture rectangle access inside GLSL (sampler2DRect,
     * texture2DRect).
     */
    if (!mGLContext->IsExtensionSupported(gl::GLContext::ARB_texture_rectangle))
      return false;
  }

  // If we're double-buffered, we don't need this fbo anymore.
  if (mGLContext->IsDoubleBuffered()) {
    mGLContext->fDeleteFramebuffers(1, &mBackBufferFBO);
    mBackBufferFBO = 0;
  }

  /* Create a simple quad VBO */

  mGLContext->fGenBuffers(1, &mQuadVBO);
  mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, mQuadVBO);

  GLfloat vertices[] = {
    /* First quad vertices */
    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
    /* Then quad texcoords */
    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
    /* Then flipped quad texcoords */
    0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
  };
  mGLContext->fBufferData(LOCAL_GL_ARRAY_BUFFER, sizeof(vertices), vertices, LOCAL_GL_STATIC_DRAW);
  mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);

  nsCOMPtr<nsIConsoleService>
    console(do_GetService(NS_CONSOLESERVICE_CONTRACTID));

  if (console) {
    nsString msg;
    msg +=
      NS_LITERAL_STRING("OpenGL LayerManager Initialized Succesfully.\nVersion: ");
    msg += NS_ConvertUTF8toUTF16(
      nsDependentCString((const char*)mGLContext->fGetString(LOCAL_GL_VERSION)));
    msg += NS_LITERAL_STRING("\nVendor: ");
    msg += NS_ConvertUTF8toUTF16(
      nsDependentCString((const char*)mGLContext->fGetString(LOCAL_GL_VENDOR)));
    msg += NS_LITERAL_STRING("\nRenderer: ");
    msg += NS_ConvertUTF8toUTF16(
      nsDependentCString((const char*)mGLContext->fGetString(LOCAL_GL_RENDERER)));
    msg += NS_LITERAL_STRING("\nFBO Texture Target: ");
    if (mFBOTextureTarget == LOCAL_GL_TEXTURE_2D)
      msg += NS_LITERAL_STRING("TEXTURE_2D");
    else
      msg += NS_LITERAL_STRING("TEXTURE_RECTANGLE");
    console->LogStringMessage(msg.get());
  }

  if (NS_IsMainThread()) {
    Preferences::AddBoolVarCache(&sDrawFPS, "layers.acceleration.draw-fps");
    Preferences::AddBoolVarCache(&sFrameCounter, "layers.acceleration.frame-counter");
  } else {
    // We have to dispatch an event to the main thread to read the pref.
    class ReadDrawFPSPref : public nsRunnable {
    public:
      NS_IMETHOD Run()
      {
        Preferences::AddBoolVarCache(&sDrawFPS, "layers.acceleration.draw-fps");
        Preferences::AddBoolVarCache(&sFrameCounter, "layers.acceleration.frame-counter");
        return NS_OK;
      }
    };
    NS_DispatchToMainThread(new ReadDrawFPSPref());
  }

  mComposer2D = mWidget->GetComposer2D();

  reporter.SetSuccessful();
  return true;
}

void
LayerManagerOGL::SetClippingRegion(const nsIntRegion& aClippingRegion)
{
  mClippingRegion = aClippingRegion;
}

void
LayerManagerOGL::BeginTransaction()
{
  mInTransaction = true;
#ifdef DEBUG
  mMaybeInvalidTree = false;
#endif
}

void
LayerManagerOGL::BeginTransactionWithTarget(gfxContext *aTarget)
{
  mInTransaction = true;
#ifdef DEBUG
  mMaybeInvalidTree = false;
#endif

#ifdef MOZ_LAYERS_HAVE_LOG
  MOZ_LAYERS_LOG(("[----- BeginTransaction"));
  Log();
#endif

  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return;
  }

  mTarget = aTarget;
}

bool
LayerManagerOGL::EndEmptyTransaction(EndTransactionFlags aFlags)
{
  // NB: this makes the somewhat bogus assumption that pure
  // compositing txns don't call BeginTransaction(), because that's
  // the behavior of CompositorParent.
  MOZ_ASSERT(!mMaybeInvalidTree);
  mInTransaction = false;

  if (!mRoot)
    return false;

  EndTransaction(nullptr, nullptr, aFlags);
  return true;
}

void
LayerManagerOGL::EndTransaction(DrawThebesLayerCallback aCallback,
                                void* aCallbackData,
                                EndTransactionFlags aFlags)
{
  mInTransaction = false;

#ifdef MOZ_LAYERS_HAVE_LOG
  MOZ_LAYERS_LOG(("  ----- (beginning paint)"));
  Log();
#endif

  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return;
  }

  if (mRoot && !(aFlags & END_NO_IMMEDIATE_REDRAW)) {
    if (aFlags & END_NO_COMPOSITE) {
      // Apply pending tree updates before recomputing effective
      // properties.
      mRoot->ApplyPendingUpdatesToSubtree();
    }

    // The results of our drawing always go directly into a pixel buffer,
    // so we don't need to pass any global transform here.
    mRoot->ComputeEffectiveTransforms(gfx3DMatrix());

    mThebesLayerCallback = aCallback;
    mThebesLayerCallbackData = aCallbackData;
    SetCompositingDisabled(aFlags & END_NO_COMPOSITE);

    bool needGLRender = true;
    if (mComposer2D && mComposer2D->TryRender(mRoot, mWorldMatrix)) {
      needGLRender = false;

      if (sDrawFPS) {
        if (!mFPS) {
          mFPS = new FPSState();
        }
        double fps = mFPS->mCompositionFps.AddFrameAndGetFps(TimeStamp::Now());
        printf_stderr("HWComposer: FPS is %g\n", fps);
      }

      // This lets us reftest and screenshot content rendered by the
      // 2d composer.
      if (mTarget) {
        MakeCurrent();
        CopyToTarget(mTarget);
        mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);
      }
      MOZ_ASSERT(!needGLRender);
    }

    if (needGLRender) {
      Render();
    }

    mThebesLayerCallback = nullptr;
    mThebesLayerCallbackData = nullptr;
  }

  mTarget = NULL;

#ifdef MOZ_LAYERS_HAVE_LOG
  Log();
  MOZ_LAYERS_LOG(("]----- EndTransaction"));
#endif
}

already_AddRefed<gfxASurface>
LayerManagerOGL::CreateOptimalMaskSurface(const gfxIntSize &aSize)
{
  return gfxPlatform::GetPlatform()->
    CreateOffscreenImageSurface(aSize, gfxASurface::CONTENT_ALPHA);
}

already_AddRefed<ThebesLayer>
LayerManagerOGL::CreateThebesLayer()
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }

  nsRefPtr<ThebesLayer> layer = new ThebesLayerOGL(this);
  return layer.forget();
}

already_AddRefed<ContainerLayer>
LayerManagerOGL::CreateContainerLayer()
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }

  nsRefPtr<ContainerLayer> layer = new ContainerLayerOGL(this);
  return layer.forget();
}

already_AddRefed<ImageLayer>
LayerManagerOGL::CreateImageLayer()
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }

  nsRefPtr<ImageLayer> layer = new ImageLayerOGL(this);
  return layer.forget();
}

already_AddRefed<ColorLayer>
LayerManagerOGL::CreateColorLayer()
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }

  nsRefPtr<ColorLayer> layer = new ColorLayerOGL(this);
  return layer.forget();
}

already_AddRefed<CanvasLayer>
LayerManagerOGL::CreateCanvasLayer()
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }

  nsRefPtr<CanvasLayer> layer = new CanvasLayerOGL(this);
  return layer.forget();
}

static LayerOGL*
ToLayerOGL(Layer* aLayer)
{
  return static_cast<LayerOGL*>(aLayer->ImplData());
}

static void ClearSubtree(Layer* aLayer)
{
  ToLayerOGL(aLayer)->CleanupResources();
  for (Layer* child = aLayer->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    ClearSubtree(child);
  }
}

void
LayerManagerOGL::ClearCachedResources(Layer* aSubtree)
{
  MOZ_ASSERT(!aSubtree || aSubtree->Manager() == this);
  Layer* subtree = aSubtree ? aSubtree : mRoot.get();
  if (!subtree) {
    return;
  }

  ClearSubtree(subtree);
#ifdef DEBUG
  // If this subtree is reachable from the root layer, then it's
  // possibly onscreen, and the resource clear means that composites
  // until the next received transaction may draw garbage to the
  // framebuffer.
  for(; subtree && subtree != mRoot; subtree = subtree->GetParent());
  mMaybeInvalidTree = (subtree == mRoot);
#endif  // DEBUG
}

LayerOGL*
LayerManagerOGL::RootLayer() const
{
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }

  return ToLayerOGL(mRoot);
}

bool LayerManagerOGL::sDrawFPS = false;
bool LayerManagerOGL::sFrameCounter = false;

static uint16_t sFrameCount = 0;
void
FPSState::DrawFrameCounter(GLContext* context)
{
  SAMPLER_FRAME_NUMBER(sFrameCount);

  context->fEnable(LOCAL_GL_SCISSOR_TEST);

  uint16_t frameNumber = sFrameCount;
  for (size_t i = 0; i < 16; i++) {
    context->fScissor(3*i, 0, 3, 3);

    // We should do this using a single draw call
    // instead of 16 glClear()
    if ((frameNumber >> i) & 0x1) {
      context->fClearColor(0.0, 0.0, 0.0, 0.0);
    } else {
      context->fClearColor(1.0, 1.0, 1.0, 0.0);
    }
    context->fClear(LOCAL_GL_COLOR_BUFFER_BIT);
  }
  // We intentionally overflow at 2^16.
  sFrameCount++;
}

// |aTexCoordRect| is the rectangle from the texture that we want to
// draw using the given program.  The program already has a necessary
// offset and scale, so the geometry that needs to be drawn is a unit
// square from 0,0 to 1,1.
//
// |aTexSize| is the actual size of the texture, as it can be larger
// than the rectangle given by |aTexCoordRect|.
void 
LayerManagerOGL::BindAndDrawQuadWithTextureRect(ShaderProgramOGL *aProg,
                                                const nsIntRect& aTexCoordRect,
                                                const nsIntSize& aTexSize,
                                                GLenum aWrapMode /* = LOCAL_GL_REPEAT */,
                                                bool aFlipped /* = false */)
{
  NS_ASSERTION(aProg->HasInitialized(), "Shader program not correctly initialized");
  GLuint vertAttribIndex =
    aProg->AttribLocation(ShaderProgramOGL::VertexCoordAttrib);
  GLuint texCoordAttribIndex =
    aProg->AttribLocation(ShaderProgramOGL::TexCoordAttrib);
  NS_ASSERTION(texCoordAttribIndex != GLuint(-1), "no texture coords?");

  // clear any bound VBO so that glVertexAttribPointer() goes back to
  // "pointer mode"
  mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);

  // Given what we know about these textures and coordinates, we can
  // compute fmod(t, 1.0f) to get the same texture coordinate out.  If
  // the texCoordRect dimension is < 0 or > width/height, then we have
  // wraparound that we need to deal with by drawing multiple quads,
  // because we can't rely on full non-power-of-two texture support
  // (which is required for the REPEAT wrap mode).

  GLContext::RectTriangles rects;

  nsIntSize realTexSize = aTexSize;
  if (!mGLContext->CanUploadNonPowerOfTwo()) {
    realTexSize = nsIntSize(NextPowerOfTwo(aTexSize.width),
                            NextPowerOfTwo(aTexSize.height));
  }

  if (aWrapMode == LOCAL_GL_REPEAT) {
    rects.addRect(/* dest rectangle */
                  0.0f, 0.0f, 1.0f, 1.0f,
                  /* tex coords */
                  aTexCoordRect.x / GLfloat(realTexSize.width),
                  aTexCoordRect.y / GLfloat(realTexSize.height),
                  aTexCoordRect.XMost() / GLfloat(realTexSize.width),
                  aTexCoordRect.YMost() / GLfloat(realTexSize.height),
                  aFlipped);
  } else {
    GLContext::DecomposeIntoNoRepeatTriangles(aTexCoordRect, realTexSize,
                                              rects, aFlipped);
  }

  mGLContext->fVertexAttribPointer(vertAttribIndex, 2,
                                   LOCAL_GL_FLOAT, LOCAL_GL_FALSE, 0,
                                   rects.vertexPointer());

  mGLContext->fVertexAttribPointer(texCoordAttribIndex, 2,
                                   LOCAL_GL_FLOAT, LOCAL_GL_FALSE, 0,
                                   rects.texCoordPointer());

  {
    mGLContext->fEnableVertexAttribArray(texCoordAttribIndex);
    {
      mGLContext->fEnableVertexAttribArray(vertAttribIndex);

      mGLContext->fDrawArrays(LOCAL_GL_TRIANGLES, 0, rects.elements());

      mGLContext->fDisableVertexAttribArray(vertAttribIndex);
    }
    mGLContext->fDisableVertexAttribArray(texCoordAttribIndex);
  }
}

void
LayerManagerOGL::NotifyShadowTreeTransaction()
{
  if (mFPS) {
    mFPS->NotifyShadowTreeTransaction();
  }
}

void
LayerManagerOGL::Render()
{
  SAMPLE_LABEL("LayerManagerOGL", "Render");
  if (mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return;
  }

  nsIntRect rect;
  if (mIsRenderingToEGLSurface) {
    rect = nsIntRect(0, 0, mSurfaceSize.width, mSurfaceSize.height);
  } else {
    rect = mRenderBounds;
    // If render bounds is not updated explicitly, try to infer it from widget
    if (rect.width == 0 || rect.height == 0) {
      // FIXME/bug XXXXXX this races with rotation changes on the main
      // thread, and undoes all the care we take with layers txns being
      // sent atomically with rotation changes
      mWidget->GetClientBounds(rect);
    }
  }
  WorldTransformRect(rect);

  GLint width = rect.width;
  GLint height = rect.height;

  // We can't draw anything to something with no area
  // so just return
  if (width == 0 || height == 0)
    return;

  // If the widget size changed, we have to force a MakeCurrent
  // to make sure that GL sees the updated widget size.
  if (mWidgetSize.width != width ||
      mWidgetSize.height != height)
  {
    MakeCurrent(true);

    mWidgetSize.width = width;
    mWidgetSize.height = height;
  } else {
    MakeCurrent();
  }

#if MOZ_WIDGET_ANDROID
  TexturePoolOGL::Fill(gl());
#endif

  SetupBackBuffer(width, height);
  SetupPipeline(width, height, ApplyWorldTransform);

  // Default blend function implements "OVER"
  mGLContext->fBlendFuncSeparate(LOCAL_GL_ONE, LOCAL_GL_ONE_MINUS_SRC_ALPHA,
                                 LOCAL_GL_ONE, LOCAL_GL_ONE);
  mGLContext->fEnable(LOCAL_GL_BLEND);

  const nsIntRect *clipRect = mRoot->GetClipRect();

  if (clipRect) {
    nsIntRect r = *clipRect;
    WorldTransformRect(r);
    mGLContext->fScissor(r.x, r.y, r.width, r.height);
  } else {
    mGLContext->fScissor(0, 0, width, height);
  }

  if (CompositingDisabled()) {
    RootLayer()->RenderLayer(mGLContext->IsDoubleBuffered() ? 0 : mBackBufferFBO,
                             nsIntPoint(0, 0));
    mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);
    return;
  }

  mGLContext->fEnable(LOCAL_GL_SCISSOR_TEST);

  // If the Android compositor is being used, this clear will be done in
  // DrawWindowUnderlay. Make sure the bits used here match up with those used
  // in mobile/android/base/gfx/LayerRenderer.java
#ifndef MOZ_ANDROID_OMTC
  mGLContext->fClearColor(0.0, 0.0, 0.0, 0.0);
  mGLContext->fClear(LOCAL_GL_COLOR_BUFFER_BIT | LOCAL_GL_DEPTH_BUFFER_BIT);
#endif

  // Allow widget to render a custom background.
  mWidget->DrawWindowUnderlay(this, rect);

  // Reset some state that might of been clobbered by the underlay.
  mGLContext->fBlendFuncSeparate(LOCAL_GL_ONE, LOCAL_GL_ONE_MINUS_SRC_ALPHA,
                                 LOCAL_GL_ONE, LOCAL_GL_ONE);

  // Render our layers.
  RootLayer()->RenderLayer(mGLContext->IsDoubleBuffered() ? 0 : mBackBufferFBO,
                           nsIntPoint(0, 0));

  // Allow widget to render a custom foreground too.
  mWidget->DrawWindowOverlay(this, rect);

#ifdef MOZ_DUMP_PAINTING
  if (gfxUtils::sDumpPainting) {
    nsIntRect rect;
    if (mIsRenderingToEGLSurface) {
      rect = nsIntRect(0, 0, mSurfaceSize.width, mSurfaceSize.height);
    } else {
      mWidget->GetClientBounds(rect);
    }
    nsRefPtr<gfxASurface> surf = gfxPlatform::GetPlatform()->CreateOffscreenSurface(rect.Size(), gfxASurface::CONTENT_COLOR_ALPHA);
    nsRefPtr<gfxContext> ctx = new gfxContext(surf);
    CopyToTarget(ctx);

    WriteSnapshotToDumpFile(this, surf);
  }
#endif

  if (mTarget) {
    CopyToTarget(mTarget);
    mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);
    return;
  }

  if (sDrawFPS && !mFPS) {
    mFPS = new FPSState();
  } else if (!sDrawFPS && mFPS) {
    mFPS = nullptr;
  }

  if (mFPS) {
    mFPS->DrawFPS(TimeStamp::Now(), mGLContext, GetProgram(Copy2DProgramType));
  } else if (sFrameCounter) {
    FPSState::DrawFrameCounter(mGLContext);
  }

  if (mGLContext->IsDoubleBuffered()) {
    mGLContext->SwapBuffers();
    LayerManager::PostPresent();
    mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);
    return;
  }

  mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, 0);

  mGLContext->fActiveTexture(LOCAL_GL_TEXTURE0);

  ShaderProgramOGL *copyprog = GetProgram(Copy2DProgramType);

  if (mFBOTextureTarget == LOCAL_GL_TEXTURE_RECTANGLE_ARB) {
    copyprog = GetProgram(Copy2DRectProgramType);
  }

  mGLContext->fBindTexture(mFBOTextureTarget, mBackBufferTexture);

  copyprog->Activate();
  copyprog->SetTextureUnit(0);

  if (copyprog->GetTexCoordMultiplierUniformLocation() != -1) {
    copyprog->SetTexCoordMultiplier(width, height);
  }

  // we're going to use client-side vertex arrays for this.
  mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);

  // "COPY"
  mGLContext->fBlendFuncSeparate(LOCAL_GL_ONE, LOCAL_GL_ZERO,
                                 LOCAL_GL_ONE, LOCAL_GL_ZERO);

  // enable our vertex attribs; we'll call glVertexPointer below
  // to fill with the correct data.
  GLint vcattr = copyprog->AttribLocation(ShaderProgramOGL::VertexCoordAttrib);
  GLint tcattr = copyprog->AttribLocation(ShaderProgramOGL::TexCoordAttrib);

  mGLContext->fEnableVertexAttribArray(vcattr);
  mGLContext->fEnableVertexAttribArray(tcattr);

  const nsIntRect *r;
  nsIntRegionRectIterator iter(mClippingRegion);

  while ((r = iter.Next()) != nullptr) {
    nsIntRect cRect = *r; r = &cRect;
    WorldTransformRect(cRect);
    float left = (GLfloat)r->x / width;
    float right = (GLfloat)r->XMost() / width;
    float top = (GLfloat)r->y / height;
    float bottom = (GLfloat)r->YMost() / height;

    float vertices[] = { left * 2.0f - 1.0f,
                         -(top * 2.0f - 1.0f),
                         right * 2.0f - 1.0f,
                         -(top * 2.0f - 1.0f),
                         left * 2.0f - 1.0f,
                         -(bottom * 2.0f - 1.0f),
                         right * 2.0f - 1.0f,
                         -(bottom * 2.0f - 1.0f) };

    // Use inverted texture coordinates since our projection matrix also has a
    // flip and we need to cancel that out.
    float coords[] = { left, 1 - top,
                       right, 1 - top,
                       left, 1 - bottom,
                       right, 1 - bottom };

    mGLContext->fVertexAttribPointer(vcattr,
                                     2, LOCAL_GL_FLOAT,
                                     LOCAL_GL_FALSE,
                                     0, vertices);

    mGLContext->fVertexAttribPointer(tcattr,
                                     2, LOCAL_GL_FLOAT,
                                     LOCAL_GL_FALSE,
                                     0, coords);

    mGLContext->fDrawArrays(LOCAL_GL_TRIANGLE_STRIP, 0, 4);
  }

  mGLContext->fDisableVertexAttribArray(vcattr);
  mGLContext->fDisableVertexAttribArray(tcattr);

  mGLContext->fFlush();
  mGLContext->fBindBuffer(LOCAL_GL_ARRAY_BUFFER, 0);
}

void
LayerManagerOGL::SetWorldTransform(const gfxMatrix& aMatrix)
{
  NS_ASSERTION(aMatrix.PreservesAxisAlignedRectangles(),
               "SetWorldTransform only accepts matrices that satisfy PreservesAxisAlignedRectangles");
  NS_ASSERTION(!aMatrix.HasNonIntegerScale(),
               "SetWorldTransform only accepts matrices with integer scale");

  mWorldMatrix = aMatrix;
}

gfxMatrix&
LayerManagerOGL::GetWorldTransform(void)
{
  return mWorldMatrix;
}

void
LayerManagerOGL::WorldTransformRect(nsIntRect& aRect)
{
  gfxRect grect(aRect.x, aRect.y, aRect.width, aRect.height);
  grect = mWorldMatrix.TransformBounds(grect);
  aRect.SetRect(grect.X(), grect.Y(), grect.Width(), grect.Height());
}

void
LayerManagerOGL::UpdateRenderBounds(const nsIntRect& aRect)
{
  mRenderBounds = aRect;
}

void
LayerManagerOGL::SetSurfaceSize(int width, int height)
{
  mSurfaceSize.width = width;
  mSurfaceSize.height = height;
}

void
LayerManagerOGL::SetupPipeline(int aWidth, int aHeight, WorldTransforPolicy aTransformPolicy)
{
  // Set the viewport correctly. 
  mGLContext->fViewport(0, 0, aWidth, aHeight);

  // We flip the view matrix around so that everything is right-side up; we're
  // drawing directly into the window's back buffer, so this keeps things
  // looking correct.
  //
  // XXX: We keep track of whether the window size changed, so we could skip
  // this update if it hadn't changed since the last call. We will need to
  // track changes to aTransformPolicy and mWorldMatrix for this to work
  // though.

  // Matrix to transform (0, 0, aWidth, aHeight) to viewport space (-1.0, 1.0,
  // 2, 2) and flip the contents.
  gfxMatrix viewMatrix; 
  viewMatrix.Translate(-gfxPoint(1.0, -1.0));
  viewMatrix.Scale(2.0f / float(aWidth), 2.0f / float(aHeight));
  viewMatrix.Scale(1.0f, -1.0f);

  if (aTransformPolicy == ApplyWorldTransform) {
    viewMatrix = mWorldMatrix * viewMatrix;
  }

  gfx3DMatrix matrix3d = gfx3DMatrix::From2D(viewMatrix);
  matrix3d._33 = 0.0f;

  SetLayerProgramProjectionMatrix(matrix3d);
}

void
LayerManagerOGL::SetupBackBuffer(int aWidth, int aHeight)
{
  if (mGLContext->IsDoubleBuffered()) {
    mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, 0);
    return;
  }

  // Do we have a FBO of the right size already?
  if (mBackBufferSize.width == aWidth &&
      mBackBufferSize.height == aHeight)
  {
    mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mBackBufferFBO);
    return;
  }

  // we already have a FBO, but we need to resize its texture.
  mGLContext->fActiveTexture(LOCAL_GL_TEXTURE0);
  mGLContext->fBindTexture(mFBOTextureTarget, mBackBufferTexture);
  mGLContext->fTexImage2D(mFBOTextureTarget,
                          0,
                          LOCAL_GL_RGBA,
                          aWidth, aHeight,
                          0,
                          LOCAL_GL_RGBA,
                          LOCAL_GL_UNSIGNED_BYTE,
                          NULL);
  mGLContext->fBindTexture(mFBOTextureTarget, 0);

  mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mBackBufferFBO);
  mGLContext->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER,
                                    LOCAL_GL_COLOR_ATTACHMENT0,
                                    mFBOTextureTarget,
                                    mBackBufferTexture,
                                    0);

  GLenum result = mGLContext->fCheckFramebufferStatus(LOCAL_GL_FRAMEBUFFER);
  if (result != LOCAL_GL_FRAMEBUFFER_COMPLETE) {
    nsAutoCString msg;
    msg.Append("Framebuffer not complete -- error 0x");
    msg.AppendInt(result, 16);
    NS_RUNTIMEABORT(msg.get());
  }

  mBackBufferSize.width = aWidth;
  mBackBufferSize.height = aHeight;
}

void
LayerManagerOGL::CopyToTarget(gfxContext *aTarget)
{
  nsIntRect rect;
  if (mIsRenderingToEGLSurface) {
    rect = nsIntRect(0, 0, mSurfaceSize.width, mSurfaceSize.height);
  } else {
    rect = nsIntRect(0, 0, mWidgetSize.width, mWidgetSize.height);
  }
  GLint width = rect.width;
  GLint height = rect.height;

  if ((int64_t(width) * int64_t(height) * int64_t(4)) > INT32_MAX) {
    NS_ERROR("Widget size too big - integer overflow!");
    return;
  }

  nsRefPtr<gfxImageSurface> imageSurface =
    new gfxImageSurface(gfxIntSize(width, height),
                        gfxASurface::ImageFormatARGB32);

  mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER,
                               mGLContext->IsDoubleBuffered() ? 0 : mBackBufferFBO);

  if (!mGLContext->IsGLES2()) {
    // GLES2 promises that binding to any custom FBO will attach
    // to GL_COLOR_ATTACHMENT0 attachment point.
    if (mGLContext->IsDoubleBuffered()) {
      mGLContext->fReadBuffer(LOCAL_GL_BACK);
    }
    else {
      mGLContext->fReadBuffer(LOCAL_GL_COLOR_ATTACHMENT0);
    }
  }

  NS_ASSERTION(imageSurface->Stride() == width * 4,
               "Image Surfaces being created with weird stride!");

  mGLContext->ReadPixelsIntoImageSurface(imageSurface);

  // Map from GL space to Cairo space and reverse the world transform.
  gfxMatrix glToCairoTransform = mWorldMatrix;
  glToCairoTransform.Invert();
  glToCairoTransform.Scale(1.0, -1.0);
  glToCairoTransform.Translate(-gfxPoint(0.0, height));

  gfxContextAutoSaveRestore restore(aTarget);
  aTarget->SetOperator(gfxContext::OPERATOR_SOURCE);
  aTarget->SetMatrix(glToCairoTransform);
  aTarget->SetSource(imageSurface);
  aTarget->Paint();
}

void
LayerManagerOGL::SetLayerProgramProjectionMatrix(const gfx3DMatrix& aMatrix)
{
  for (unsigned int i = 0; i < mPrograms.Length(); ++i) {
    for (uint32_t mask = MaskNone; mask < NumMaskTypes; ++mask) {
      if (mPrograms[i].mVariations[mask]) {
        mPrograms[i].mVariations[mask]->CheckAndSetProjectionMatrix(aMatrix);
      }
    }
  }
}

static GLenum
GetFrameBufferInternalFormat(GLContext* gl,
                             GLuint aCurrentFrameBuffer,
                             nsIWidget* aWidget)
{
  if (aCurrentFrameBuffer == 0) { // default framebuffer
    return aWidget->GetGLFrameBufferFormat();
  }
  return LOCAL_GL_RGBA;
}

void
LayerManagerOGL::CreateFBOWithTexture(const nsIntRect& aRect, InitMode aInit,
                                      GLuint aCurrentFrameBuffer,
                                      GLuint *aFBO, GLuint *aTexture)
{
  GLuint tex, fbo;

  mGLContext->fActiveTexture(LOCAL_GL_TEXTURE0);
  mGLContext->fGenTextures(1, &tex);
  mGLContext->fBindTexture(mFBOTextureTarget, tex);

  if (aInit == InitModeCopy) {
    // We're going to create an RGBA temporary fbo.  But to
    // CopyTexImage() from the current framebuffer, the framebuffer's
    // format has to be compatible with the new texture's.  So we
    // check the format of the framebuffer here and take a slow path
    // if it's incompatible.
    GLenum format =
      GetFrameBufferInternalFormat(gl(), aCurrentFrameBuffer, mWidget);
 
    bool isFormatCompatibleWithRGBA
        = gl()->IsGLES2() ? (format == LOCAL_GL_RGBA)
                          : true;

    if (isFormatCompatibleWithRGBA) {
      mGLContext->fCopyTexImage2D(mFBOTextureTarget,
                                  0,
                                  LOCAL_GL_RGBA,
                                  aRect.x, aRect.y,
                                  aRect.width, aRect.height,
                                  0);
    } else {
      // Curses, incompatible formats.  Take a slow path.

      // RGBA
      size_t bufferSize = aRect.width * aRect.height * 4;
      nsAutoArrayPtr<uint8_t> buf(new uint8_t[bufferSize]);

      mGLContext->fReadPixels(aRect.x, aRect.y,
                              aRect.width, aRect.height,
                              LOCAL_GL_RGBA,
                              LOCAL_GL_UNSIGNED_BYTE,
                              buf);
      mGLContext->fTexImage2D(mFBOTextureTarget,
                              0,
                              LOCAL_GL_RGBA,
                              aRect.width, aRect.height,
                              0,
                              LOCAL_GL_RGBA,
                              LOCAL_GL_UNSIGNED_BYTE,
                              buf);
    }
  } else {
    mGLContext->fTexImage2D(mFBOTextureTarget,
                            0,
                            LOCAL_GL_RGBA,
                            aRect.width, aRect.height,
                            0,
                            LOCAL_GL_RGBA,
                            LOCAL_GL_UNSIGNED_BYTE,
                            NULL);
  }
  mGLContext->fTexParameteri(mFBOTextureTarget, LOCAL_GL_TEXTURE_MIN_FILTER,
                             LOCAL_GL_LINEAR);
  mGLContext->fTexParameteri(mFBOTextureTarget, LOCAL_GL_TEXTURE_MAG_FILTER,
                             LOCAL_GL_LINEAR);
  mGLContext->fTexParameteri(mFBOTextureTarget, LOCAL_GL_TEXTURE_WRAP_S, 
                             LOCAL_GL_CLAMP_TO_EDGE);
  mGLContext->fTexParameteri(mFBOTextureTarget, LOCAL_GL_TEXTURE_WRAP_T, 
                             LOCAL_GL_CLAMP_TO_EDGE);
  mGLContext->fBindTexture(mFBOTextureTarget, 0);

  mGLContext->fGenFramebuffers(1, &fbo);
  mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, fbo);
  mGLContext->fFramebufferTexture2D(LOCAL_GL_FRAMEBUFFER,
                                    LOCAL_GL_COLOR_ATTACHMENT0,
                                    mFBOTextureTarget,
                                    tex,
                                    0);

  // Making this call to fCheckFramebufferStatus prevents a crash on
  // PowerVR. See bug 695246.
  GLenum result = mGLContext->fCheckFramebufferStatus(LOCAL_GL_FRAMEBUFFER);
  if (result != LOCAL_GL_FRAMEBUFFER_COMPLETE) {
    nsAutoCString msg;
    msg.Append("Framebuffer not complete -- error 0x");
    msg.AppendInt(result, 16);
    msg.Append(", mFBOTextureTarget 0x");
    msg.AppendInt(mFBOTextureTarget, 16);
    msg.Append(", aRect.width ");
    msg.AppendInt(aRect.width);
    msg.Append(", aRect.height ");
    msg.AppendInt(aRect.height);
    NS_RUNTIMEABORT(msg.get());
  }

  SetupPipeline(aRect.width, aRect.height, DontApplyWorldTransform);
  mGLContext->fScissor(0, 0, aRect.width, aRect.height);

  if (aInit == InitModeClear) {
    mGLContext->fClearColor(0.0, 0.0, 0.0, 0.0);
    mGLContext->fClear(LOCAL_GL_COLOR_BUFFER_BIT);
  }

  *aFBO = fbo;
  *aTexture = tex;
}

already_AddRefed<ShadowThebesLayer>
LayerManagerOGL::CreateShadowThebesLayer()
{
  if (LayerManagerOGL::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
#ifdef FORCE_BASICTILEDTHEBESLAYER
  return nsRefPtr<ShadowThebesLayer>(new TiledThebesLayerOGL(this)).forget();
#else
  return nsRefPtr<ShadowThebesLayerOGL>(new ShadowThebesLayerOGL(this)).forget();
#endif
}

already_AddRefed<ShadowContainerLayer>
LayerManagerOGL::CreateShadowContainerLayer()
{
  if (LayerManagerOGL::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<ShadowContainerLayerOGL>(new ShadowContainerLayerOGL(this)).forget();
}

already_AddRefed<ShadowImageLayer>
LayerManagerOGL::CreateShadowImageLayer()
{
  if (LayerManagerOGL::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<ShadowImageLayerOGL>(new ShadowImageLayerOGL(this)).forget();
}

already_AddRefed<ShadowColorLayer>
LayerManagerOGL::CreateShadowColorLayer()
{
  if (LayerManagerOGL::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<ShadowColorLayerOGL>(new ShadowColorLayerOGL(this)).forget();
}

already_AddRefed<ShadowCanvasLayer>
LayerManagerOGL::CreateShadowCanvasLayer()
{
  if (LayerManagerOGL::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<ShadowCanvasLayerOGL>(new ShadowCanvasLayerOGL(this)).forget();
}

already_AddRefed<ShadowRefLayer>
LayerManagerOGL::CreateShadowRefLayer()
{
  if (LayerManagerOGL::mDestroyed) {
    NS_WARNING("Call on destroyed layer manager");
    return nullptr;
  }
  return nsRefPtr<ShadowRefLayerOGL>(new ShadowRefLayerOGL(this)).forget();
}

TemporaryRef<DrawTarget>
LayerManagerOGL::CreateDrawTarget(const IntSize &aSize,
                               SurfaceFormat aFormat)
{
#ifdef XP_MACOSX
  // We don't want to accelerate if the surface is too small which indicates
  // that it's likely used for an icon/static image. We also don't want to
  // accelerate anything that is above the maximum texture size of weakest gpu.
  // Safari uses 5000 area as the minimum for acceleration, we decided 64^2 is more logical.
  bool useAcceleration = aSize.width <= 4096 && aSize.height <= 4096 &&
                         aSize.width > 64 && aSize.height > 64 &&
                         gfxPlatformMac::GetPlatform()->UseAcceleratedCanvas();
  if (useAcceleration) {
    return Factory::CreateDrawTarget(BACKEND_COREGRAPHICS_ACCELERATED,
                                     aSize, aFormat);
  }
#endif
  return LayerManager::CreateDrawTarget(aSize, aFormat);
}

static void
SubtractTransformedRegion(nsIntRegion& aRegion,
                          const nsIntRegion& aRegionToSubtract,
                          const gfx3DMatrix& aTransform)
{
  if (aRegionToSubtract.IsEmpty()) {
    return;
  }

  // For each rect in the region, find out its bounds in screen space and
  // subtract it from the screen region.
  nsIntRegionRectIterator it(aRegionToSubtract);
  while (const nsIntRect* rect = it.Next()) {
    gfxRect incompleteRect = aTransform.TransformBounds(gfxRect(*rect));
    aRegion.Sub(aRegion, nsIntRect(incompleteRect.x,
                                   incompleteRect.y,
                                   incompleteRect.width,
                                   incompleteRect.height));
  }
}

/* static */ void
LayerManagerOGL::ComputeRenderIntegrityInternal(Layer* aLayer,
                                                nsIntRegion& aScreenRegion,
                                                nsIntRegion& aLowPrecisionScreenRegion,
                                                const gfx3DMatrix& aTransform)
{
  if (aLayer->GetOpacity() <= 0.f ||
      (aScreenRegion.IsEmpty() && aLowPrecisionScreenRegion.IsEmpty())) {
    return;
  }

  // If the layer's a container, recurse into all of its children
  ContainerLayer* container = aLayer->AsContainerLayer();
  if (container) {
    // Accumulate the transform of intermediate surfaces
    gfx3DMatrix transform = aTransform;
    if (container->UseIntermediateSurface()) {
      transform = aLayer->GetEffectiveTransform();
      transform.PreMultiply(aTransform);
    }
    for (Layer* child = aLayer->GetFirstChild(); child;
         child = child->GetNextSibling()) {
      ComputeRenderIntegrityInternal(child, aScreenRegion, aLowPrecisionScreenRegion, transform);
    }
    return;
  }

  // Only thebes layers can be incomplete
  ThebesLayer* thebesLayer = aLayer->AsThebesLayer();
  if (!thebesLayer) {
    return;
  }

  // See if there's any incomplete rendering
  nsIntRegion incompleteRegion = aLayer->GetEffectiveVisibleRegion();
  incompleteRegion.Sub(incompleteRegion, thebesLayer->GetValidRegion());

  if (!incompleteRegion.IsEmpty()) {
    // Calculate the transform to get between screen and layer space
    gfx3DMatrix transformToScreen = aLayer->GetEffectiveTransform();
    transformToScreen.PreMultiply(aTransform);

    SubtractTransformedRegion(aScreenRegion, incompleteRegion, transformToScreen);

    // See if there's any incomplete low-precision rendering
    TiledLayerComposer* composer = nullptr;
    ShadowLayer* shadow = aLayer->AsShadowLayer();
    if (shadow) {
      composer = shadow->AsTiledLayerComposer();
      if (composer) {
        incompleteRegion.Sub(incompleteRegion, composer->GetValidLowPrecisionRegion());
        if (!incompleteRegion.IsEmpty()) {
          SubtractTransformedRegion(aLowPrecisionScreenRegion, incompleteRegion, transformToScreen);
        }
      }
    }

    // If we can't get a valid low precision region, assume it's the same as
    // the high precision region.
    if (!composer) {
      SubtractTransformedRegion(aLowPrecisionScreenRegion, incompleteRegion, transformToScreen);
    }
  }
}

static int
GetRegionArea(const nsIntRegion& aRegion)
{
  int area = 0;
  nsIntRegionRectIterator it(aRegion);
  while (const nsIntRect* rect = it.Next()) {
    area += rect->width * rect->height;
  }
  return area;
}

#ifdef MOZ_ANDROID_OMTC
static float
GetDisplayportCoverage(const gfx::Rect& aDisplayPort,
                       const gfx3DMatrix& aTransformToScreen,
                       const nsIntRect& aScreenRect)
{
  gfxRect transformedDisplayport =
    aTransformToScreen.TransformBounds(gfxRect(aDisplayPort.x,
                                               aDisplayPort.y,
                                               aDisplayPort.width,
                                               aDisplayPort.height));
  transformedDisplayport.RoundOut();
  nsIntRect displayport = nsIntRect(transformedDisplayport.x,
                                    transformedDisplayport.y,
                                    transformedDisplayport.width,
                                    transformedDisplayport.height);
  if (!displayport.Contains(aScreenRect)) {
    nsIntRegion coveredRegion;
    coveredRegion.And(aScreenRect, displayport);
    return GetRegionArea(coveredRegion) / (float)(aScreenRect.width * aScreenRect.height);
  }

  return 1.0f;
}
#endif // MOZ_ANDROID_OMTC

float
LayerManagerOGL::ComputeRenderIntegrity()
{
  // We only ever have incomplete rendering when progressive tiles are enabled.
  Layer* root = GetRoot();
  if (!gfxPlatform::UseProgressiveTilePainting() || !root) {
    return 1.f;
  }

  const FrameMetrics& rootMetrics = root->AsContainerLayer()->GetFrameMetrics();
  nsIntRect screenRect(rootMetrics.mCompositionBounds.x,
                       rootMetrics.mCompositionBounds.y,
                       rootMetrics.mCompositionBounds.width,
                       rootMetrics.mCompositionBounds.height);

  float lowPrecisionMultiplier = 1.0f;
  float highPrecisionMultiplier = 1.0f;
#ifdef MOZ_ANDROID_OMTC
  // Use the transform on the primary scrollable layer and its FrameMetrics
  // to find out how much of the viewport the current displayport covers
  Layer* primaryScrollable = GetPrimaryScrollableLayer();
  if (primaryScrollable) {
    // This is derived from the code in
    // gfx/layers/ipc/CompositorParent.cpp::TransformShadowTree.
    const gfx3DMatrix& rootTransform = root->GetTransform();
    float devPixelRatioX = 1 / rootTransform.GetXScale();
    float devPixelRatioY = 1 / rootTransform.GetYScale();

    gfx3DMatrix transform = primaryScrollable->GetEffectiveTransform();
    transform.ScalePost(devPixelRatioX, devPixelRatioY, 1);
    const FrameMetrics& metrics = primaryScrollable->AsContainerLayer()->GetFrameMetrics();

    // Clip the screen rect to the document bounds
    gfxRect documentBounds =
      transform.TransformBounds(gfxRect(metrics.mScrollableRect.x - metrics.mScrollOffset.x,
                                        metrics.mScrollableRect.y - metrics.mScrollOffset.y,
                                        metrics.mScrollableRect.width,
                                        metrics.mScrollableRect.height));
    documentBounds.RoundOut();
    screenRect = screenRect.Intersect(nsIntRect(documentBounds.x, documentBounds.y,
                                                documentBounds.width, documentBounds.height));

    // If the screen rect is empty, the user has scrolled entirely into
    // over-scroll and so we can be considered to have full integrity.
    if (screenRect.IsEmpty()) {
      return 1.0f;
    }

    // Work out how much of the critical display-port covers the screen
    bool hasLowPrecision = false;
    if (!metrics.mCriticalDisplayPort.IsEmpty()) {
      hasLowPrecision = true;
      highPrecisionMultiplier =
        GetDisplayportCoverage(metrics.mCriticalDisplayPort, transform, screenRect);
    }

    // Work out how much of the display-port covers the screen
    if (!metrics.mDisplayPort.IsEmpty()) {
      if (hasLowPrecision) {
        lowPrecisionMultiplier =
          GetDisplayportCoverage(metrics.mDisplayPort, transform, screenRect);
      } else {
        lowPrecisionMultiplier = highPrecisionMultiplier =
          GetDisplayportCoverage(metrics.mDisplayPort, transform, screenRect);
      }
    }
  }

  // If none of the screen is covered, we have zero integrity.
  if (highPrecisionMultiplier <= 0.0f && lowPrecisionMultiplier <= 0.0f) {
    return 0.0f;
  }
#endif // MOZ_ANDROID_OMTC

  nsIntRegion screenRegion(screenRect);
  nsIntRegion lowPrecisionScreenRegion(screenRect);
  gfx3DMatrix transform;
  ComputeRenderIntegrityInternal(root, screenRegion,
                                 lowPrecisionScreenRegion, transform);

  if (!screenRegion.IsEqual(screenRect)) {
    // Calculate the area of the region. All rects in an nsRegion are
    // non-overlapping.
    float screenArea = screenRect.width * screenRect.height;
    float highPrecisionIntegrity = GetRegionArea(screenRegion) / screenArea;
    float lowPrecisionIntegrity = 1.f;
    if (!lowPrecisionScreenRegion.IsEqual(screenRect)) {
      lowPrecisionIntegrity = GetRegionArea(lowPrecisionScreenRegion) / screenArea;
    }

    return ((highPrecisionIntegrity * highPrecisionMultiplier) +
            (lowPrecisionIntegrity * lowPrecisionMultiplier)) / 2;
  }

  return 1.f;
}

} /* layers */
} /* mozilla */
