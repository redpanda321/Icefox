/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_LAYERMANAGEROGL_H
#define GFX_LAYERMANAGEROGL_H

#include "LayerManagerOGLProgram.h"

#include "mozilla/layers/ShadowLayers.h"
#include "mozilla/TimeStamp.h"

#ifdef XP_WIN
#include <windows.h>
#endif

#define BUFFER_OFFSET(i) ((char *)NULL + (i))

#include "gfxContext.h"
#include "gfx3DMatrix.h"
#include "nsIWidget.h"
#include "GLContextTypes.h"

namespace mozilla {
namespace gl {
class GLContext;
}
namespace layers {

class Composer2D;
class LayerOGL;
class ShadowThebesLayer;
class ShadowContainerLayer;
class ShadowImageLayer;
class ShadowCanvasLayer;
class ShadowColorLayer;
struct FPSState;

/**
 * This is the LayerManager used for OpenGL 2.1 and OpenGL ES 2.0.
 * This can be used either on the main thread or the compositor.
 */
class THEBES_API LayerManagerOGL :
    public ShadowLayerManager
{
  typedef mozilla::gl::GLContext GLContext;
  typedef mozilla::gl::ShaderProgramType ProgramType;

public:
  LayerManagerOGL(nsIWidget *aWidget, int aSurfaceWidth = -1, int aSurfaceHeight = -1,
                  bool aIsRenderingToEGLSurface = false);
  virtual ~LayerManagerOGL();

  void Destroy();


  /**
   * Initializes the layer manager with a given GLContext. If aContext is null
   * then the layer manager will try to create one for the associated widget.
   *
   * \param aContext an existing GL context to use. Can be created with CreateContext()
   *
   * \return True is initialization was succesful, false when it was not.
   */
  bool Initialize(bool force = false);

  bool Initialize(nsRefPtr<GLContext> aContext, bool force = false);

  /**
   * Sets the clipping region for this layer manager. This is important on 
   * windows because using OGL we no longer have GDI's native clipping. Therefor
   * widget must tell us what part of the screen is being invalidated,
   * and we should clip to this.
   *
   * \param aClippingRegion Region to clip to. Setting an empty region
   * will disable clipping.
   */
  void SetClippingRegion(const nsIntRegion& aClippingRegion);

  /**
   * LayerManager implementation.
   */
  virtual ShadowLayerManager* AsShadowManager()
  {
    return this;
  }

  void BeginTransaction();

  void BeginTransactionWithTarget(gfxContext* aTarget);

  void EndConstruction();

  virtual bool EndEmptyTransaction(EndTransactionFlags aFlags = END_DEFAULT);
  virtual void NotifyShadowTreeTransaction();
  virtual void EndTransaction(DrawThebesLayerCallback aCallback,
                              void* aCallbackData,
                              EndTransactionFlags aFlags = END_DEFAULT);

  virtual void SetRoot(Layer* aLayer) { mRoot = aLayer; }

  virtual bool CanUseCanvasLayerForSize(const gfxIntSize &aSize) {
    if (!mGLContext)
      return false;
    int32_t maxSize = GetMaxTextureSize();
    return aSize <= gfxIntSize(maxSize, maxSize);
  }

  virtual int32_t GetMaxTextureSize() const;

  virtual already_AddRefed<ThebesLayer> CreateThebesLayer();

  virtual already_AddRefed<ContainerLayer> CreateContainerLayer();

  virtual already_AddRefed<ImageLayer> CreateImageLayer();

  virtual already_AddRefed<ColorLayer> CreateColorLayer();

  virtual already_AddRefed<CanvasLayer> CreateCanvasLayer();

  virtual already_AddRefed<ShadowThebesLayer> CreateShadowThebesLayer();
  virtual already_AddRefed<ShadowContainerLayer> CreateShadowContainerLayer();
  virtual already_AddRefed<ShadowImageLayer> CreateShadowImageLayer();
  virtual already_AddRefed<ShadowColorLayer> CreateShadowColorLayer();
  virtual already_AddRefed<ShadowCanvasLayer> CreateShadowCanvasLayer();
  virtual already_AddRefed<ShadowRefLayer> CreateShadowRefLayer();

  virtual LayersBackend GetBackendType() { return LAYERS_OPENGL; }
  virtual void GetBackendName(nsAString& name) { name.AssignLiteral("OpenGL"); }

  virtual already_AddRefed<gfxASurface>
    CreateOptimalMaskSurface(const gfxIntSize &aSize);

  virtual void ClearCachedResources(Layer* aSubtree = nullptr) MOZ_OVERRIDE;

  /**
   * Helper methods.
   */
  void MakeCurrent(bool aForce = false);

  ShaderProgramOGL* GetBasicLayerProgram(bool aOpaque, bool aIsRGB,
                                         MaskType aMask = MaskNone)
  {
    gl::ShaderProgramType format = gl::BGRALayerProgramType;
    if (aIsRGB) {
      if (aOpaque) {
        format = gl::RGBXLayerProgramType;
      } else {
        format = gl::RGBALayerProgramType;
      }
    } else {
      if (aOpaque) {
        format = gl::BGRXLayerProgramType;
      }
    }
    return GetProgram(format, aMask);
  }

  ShaderProgramOGL* GetProgram(gl::ShaderProgramType aType,
                               Layer* aMaskLayer) {
    if (aMaskLayer)
      return GetProgram(aType, Mask2d);
    return GetProgram(aType, MaskNone);
  }

  ShaderProgramOGL* GetProgram(gl::ShaderProgramType aType,
                               MaskType aMask = MaskNone) {
    NS_ASSERTION(ProgramProfileOGL::ProgramExists(aType, aMask),
                 "Invalid program type.");
    return mPrograms[aType].mVariations[aMask];
  }

  ShaderProgramOGL* GetFBOLayerProgram(MaskType aMask = MaskNone) {
    return GetProgram(GetFBOLayerProgramType(), aMask);
  }

  gl::ShaderProgramType GetFBOLayerProgramType() {
    if (mFBOTextureTarget == LOCAL_GL_TEXTURE_RECTANGLE_ARB)
      return gl::RGBARectLayerProgramType;
    return gl::RGBALayerProgramType;
  }

  GLContext* gl() const { return mGLContext; }

  // |NSOpenGLContext*|:
  void* GetNSOpenGLContext() const;

  DrawThebesLayerCallback GetThebesLayerCallback() const
  { return mThebesLayerCallback; }

  void* GetThebesLayerCallbackData() const
  { return mThebesLayerCallbackData; }

  /*
   * Helper functions for our layers
   */
  void CallThebesLayerDrawCallback(ThebesLayer* aLayer,
                                   gfxContext* aContext,
                                   const nsIntRegion& aRegionToDraw)
  {
    NS_ASSERTION(mThebesLayerCallback,
                 "CallThebesLayerDrawCallback without callback!");
    mThebesLayerCallback(aLayer, aContext,
                         aRegionToDraw, nsIntRegion(),
                         mThebesLayerCallbackData);
  }

  GLenum FBOTextureTarget() { return mFBOTextureTarget; }

  /**
   * Controls how to initialize the texture / FBO created by
   * CreateFBOWithTexture.
   *  - InitModeNone: No initialization, contents are undefined.
   *  - InitModeClear: Clears the FBO.
   *  - InitModeCopy: Copies the contents of the current glReadBuffer into the
   *    texture.
   */
  enum InitMode {
    InitModeNone,
    InitModeClear,
    InitModeCopy
  };

  /* Create a FBO backed by a texture; will leave the FBO
   * bound.  Note that the texture target type will be
   * of the type returned by FBOTextureTarget; different
   * shaders are required to sample from the different
   * texture types.
   */
  void CreateFBOWithTexture(const nsIntRect& aRect, InitMode aInit,
                            GLuint aCurrentFrameBuffer,
                            GLuint *aFBO, GLuint *aTexture);

  GLuint QuadVBO() { return mQuadVBO; }
  GLintptr QuadVBOVertexOffset() { return 0; }
  GLintptr QuadVBOTexCoordOffset() { return sizeof(float)*4*2; }
  GLintptr QuadVBOFlippedTexCoordOffset() { return sizeof(float)*8*2; }

  void BindQuadVBO();
  void QuadVBOVerticesAttrib(GLuint aAttribIndex);
  void QuadVBOTexCoordsAttrib(GLuint aAttribIndex);
  void QuadVBOFlippedTexCoordsAttrib(GLuint aAttribIndex);

  // Super common
  void BindAndDrawQuad(GLuint aVertAttribIndex,
                       GLuint aTexCoordAttribIndex,
                       bool aFlipped = false);

  void BindAndDrawQuad(ShaderProgramOGL *aProg,
                       bool aFlipped = false)
  {
    NS_ASSERTION(aProg->HasInitialized(), "Shader program not correctly initialized");
    BindAndDrawQuad(aProg->AttribLocation(ShaderProgramOGL::VertexCoordAttrib),
                    aProg->AttribLocation(ShaderProgramOGL::TexCoordAttrib),
                    aFlipped);
  }

  void BindAndDrawQuadWithTextureRect(ShaderProgramOGL *aProg,
                                      const nsIntRect& aTexCoordRect,
                                      const nsIntSize& aTexSize,
                                      GLenum aWrapMode = LOCAL_GL_REPEAT,
                                      bool aFlipped = false);

#ifdef MOZ_LAYERS_HAVE_LOG
  virtual const char* Name() const { return "OGL"; }
#endif // MOZ_LAYERS_HAVE_LOG

  const nsIntSize& GetWidgetSize() {
    return mWidgetSize;
  }

  enum WorldTransforPolicy {
    ApplyWorldTransform,
    DontApplyWorldTransform
  };

  /**
   * Setup the viewport and projection matrix for rendering
   * to a window of the given dimensions.
   */
  void SetupPipeline(int aWidth, int aHeight, WorldTransforPolicy aTransformPolicy);

  /**
   * Setup World transform matrix.
   * Transform will be ignored if it is not PreservesAxisAlignedRectangles
   * or has non integer scale
   */
  void SetWorldTransform(const gfxMatrix& aMatrix);
  gfxMatrix& GetWorldTransform(void);
  void WorldTransformRect(nsIntRect& aRect);

  void UpdateRenderBounds(const nsIntRect& aRect);

  /**
   * Set the size of the surface we're rendering to.
   */
  void SetSurfaceSize(int width, int height);

  bool CompositingDisabled() { return mCompositingDisabled; }
  void SetCompositingDisabled(bool aCompositingDisabled) { mCompositingDisabled = aCompositingDisabled; }

  /**
   * Creates a DrawTarget which is optimized for inter-operating with this
   * layermanager.
   */
  virtual TemporaryRef<mozilla::gfx::DrawTarget>
    CreateDrawTarget(const mozilla::gfx::IntSize &aSize,
                     mozilla::gfx::SurfaceFormat aFormat);

  /**
   * Calculates the 'completeness' of the rendering that intersected with the
   * screen on the last render. This is only useful when progressive tile
   * drawing is enabled, otherwise this will always return 1.0.
   * This function's expense scales with the size of the layer tree and the
   * complexity of individual layers' valid regions.
   */
  float ComputeRenderIntegrity();

private:
  /** Widget associated with this layer manager */
  nsIWidget *mWidget;
  nsIntSize mWidgetSize;

  /** The size of the surface we are rendering to */
  nsIntSize mSurfaceSize;

  /** 
   * Context target, NULL when drawing directly to our swap chain.
   */
  nsRefPtr<gfxContext> mTarget;

  nsRefPtr<GLContext> mGLContext;

  /** Our more efficient but less powerful alter ego, if one is available. */
  nsRefPtr<Composer2D> mComposer2D;

  already_AddRefed<mozilla::gl::GLContext> CreateContext();

  /** Backbuffer */
  GLuint mBackBufferFBO;
  GLuint mBackBufferTexture;
  nsIntSize mBackBufferSize;

  /** Shader Programs */
  struct ShaderProgramVariations {
    ShaderProgramOGL* mVariations[NumMaskTypes];
  };
  nsTArray<ShaderProgramVariations> mPrograms;

  /** Texture target to use for FBOs */
  GLenum mFBOTextureTarget;

  /** VBO that has some basics in it for a textured quad,
   *  including vertex coords and texcoords for both
   *  flipped and unflipped textures */
  GLuint mQuadVBO;

  /** Region we're clipping our current drawing to. */
  nsIntRegion mClippingRegion;

  /** Misc */
  bool mHasBGRA;
  bool mCompositingDisabled;

  /**
   * When rendering to an EGL surface (e.g. on Android), we rely on being told
   * about size changes (via SetSurfaceSize) rather than pulling this information
   * from the widget, since the widget's information can lag behind.
   */
  bool mIsRenderingToEGLSurface;

  /** Current root layer. */
  LayerOGL *RootLayer() const;

  /**
   * Render the current layer tree to the active target.
   */
  void Render();

  /**
   * Setup a backbuffer of the given dimensions.
   */
  void SetupBackBuffer(int aWidth, int aHeight);

  /**
   * Copies the content of our backbuffer to the set transaction target.
   */
  void CopyToTarget(gfxContext *aTarget);

  /**
   * Updates all layer programs with a new projection matrix.
   */
  void SetLayerProgramProjectionMatrix(const gfx3DMatrix& aMatrix);

  /**
   * Helper method for Initialize, creates all valid variations of a program
   * and adds them to mPrograms
   */
  void AddPrograms(gl::ShaderProgramType aType);

  /**
   * Recursive helper method for use by ComputeRenderIntegrity. Subtracts
   * any incomplete rendering on aLayer from aScreenRegion. Any low-precision
   * rendering is included in aLowPrecisionScreenRegion. aTransform is the
   * accumulated transform of intermediate surfaces beneath aLayer.
   */
  static void ComputeRenderIntegrityInternal(Layer* aLayer,
                                             nsIntRegion& aScreenRegion,
                                             nsIntRegion& aLowPrecisionScreenRegion,
                                             const gfx3DMatrix& aTransform);

  /* Thebes layer callbacks; valid at the end of a transaciton,
   * while rendering */
  DrawThebesLayerCallback mThebesLayerCallback;
  void *mThebesLayerCallbackData;
  gfxMatrix mWorldMatrix;
  nsAutoPtr<FPSState> mFPS;
  nsIntRect mRenderBounds;
#ifdef DEBUG
  // NB: only interesting when this is a purely compositing layer
  // manager.  True after possibly onscreen layers have had their
  // cached resources cleared outside of a transaction, and before the
  // next forwarded transaction that re-validates their buffers.
  bool mMaybeInvalidTree;
#endif

  static bool sDrawFPS;
  static bool sFrameCounter;
};

enum LayerRenderStateFlags {
  LAYER_RENDER_STATE_Y_FLIPPED = 1 << 0,
  LAYER_RENDER_STATE_BUFFER_ROTATION = 1 << 1
};

struct LayerRenderState {
  LayerRenderState() : mSurface(nullptr), mFlags(0)
  {}

  LayerRenderState(SurfaceDescriptor* aSurface, uint32_t aFlags = 0)
    : mSurface(aSurface)
    , mFlags(aFlags)
  {}

  bool YFlipped() const
  { return mFlags & LAYER_RENDER_STATE_Y_FLIPPED; }

  bool BufferRotated() const
  { return mFlags & LAYER_RENDER_STATE_BUFFER_ROTATION; }

  SurfaceDescriptor* mSurface;
  uint32_t mFlags;
};

/**
 * General information and tree management for OGL layers.
 */
class LayerOGL
{
public:
  LayerOGL(LayerManagerOGL *aManager)
    : mOGLManager(aManager), mDestroyed(false)
  { }

  virtual ~LayerOGL() { }

  virtual LayerOGL *GetFirstChildOGL() {
    return nullptr;
  }

  /* Do NOT call this from the generic LayerOGL destructor.  Only from the
   * concrete class destructor
   */
  virtual void Destroy() = 0;

  virtual Layer* GetLayer() = 0;

  virtual LayerRenderState GetRenderState() { return LayerRenderState(); }

  virtual void RenderLayer(int aPreviousFrameBuffer,
                           const nsIntPoint& aOffset) = 0;

  typedef mozilla::gl::GLContext GLContext;

  LayerManagerOGL* OGLManager() const { return mOGLManager; }
  GLContext *gl() const { return mOGLManager->gl(); }
  virtual void CleanupResources() = 0;

  /*
   * Loads the result of rendering the layer as an OpenGL texture in aTextureUnit.
   * Will try to use an existing texture if possible, or a temporary
   * one if not. It is the callee's responsibility to release the texture.
   * Will return true if a texture could be constructed and loaded, false otherwise.
   * The texture will not be transformed, i.e., it will be in the same coord
   * space as this.
   * Any layer that can be used as a mask layer should override this method.
   * aSize will contain the size of the image.
   */
  virtual bool LoadAsTexture(GLuint aTextureUnit, gfxIntSize* aSize)
  {
    NS_WARNING("LoadAsTexture called without being overriden");
    return false;
  }

protected:
  LayerManagerOGL *mOGLManager;
  bool mDestroyed;
};

} /* layers */
} /* mozilla */

#endif /* GFX_LAYERMANAGEROGL_H */
