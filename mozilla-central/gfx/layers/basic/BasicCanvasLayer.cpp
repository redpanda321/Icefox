/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/PLayersParent.h"
#include "gfxImageSurface.h"
#include "GLContext.h"
#include "gfxUtils.h"
#include "gfxPlatform.h"
#include "mozilla/Preferences.h"

#include "BasicLayersImpl.h"
#include "nsXULAppAPI.h"

using namespace mozilla::gfx;
using namespace mozilla::gl;

namespace mozilla {
namespace layers {

class BasicCanvasLayer : public CanvasLayer,
                         public BasicImplData
{
public:
  BasicCanvasLayer(BasicLayerManager* aLayerManager) :
    CanvasLayer(aLayerManager, static_cast<BasicImplData*>(this))
  {
    MOZ_COUNT_CTOR(BasicCanvasLayer);
    mForceReadback = Preferences::GetBool("webgl.force-layers-readback", false);
  }
  virtual ~BasicCanvasLayer()
  {
    MOZ_COUNT_DTOR(BasicCanvasLayer);
  }

  virtual void SetVisibleRegion(const nsIntRegion& aRegion)
  {
    NS_ASSERTION(BasicManager()->InConstruction(),
                 "Can only set properties in construction phase");
    CanvasLayer::SetVisibleRegion(aRegion);
  }

  virtual void Initialize(const Data& aData);
  virtual void Paint(gfxContext* aContext, Layer* aMaskLayer);

  virtual void PaintWithOpacity(gfxContext* aContext,
                                float aOpacity,
                                Layer* aMaskLayer);

protected:
  BasicLayerManager* BasicManager()
  {
    return static_cast<BasicLayerManager*>(mManager);
  }
  void UpdateSurface(gfxASurface* aDestSurface = nullptr, Layer* aMaskLayer = nullptr);

  nsRefPtr<gfxASurface> mSurface;
  nsRefPtr<mozilla::gl::GLContext> mGLContext;
  mozilla::RefPtr<mozilla::gfx::DrawTarget> mDrawTarget;
  
  uint32_t mCanvasFramebuffer;

  bool mGLBufferIsPremultiplied;
  bool mNeedsYFlip;
  bool mForceReadback;

  nsRefPtr<gfxImageSurface> mCachedTempSurface;
  gfxIntSize mCachedSize;
  gfxImageFormat mCachedFormat;

  gfxImageSurface* GetTempSurface(const gfxIntSize& aSize, const gfxImageFormat aFormat)
  {
    if (!mCachedTempSurface ||
        aSize.width != mCachedSize.width ||
        aSize.height != mCachedSize.height ||
        aFormat != mCachedFormat)
    {
      mCachedTempSurface = new gfxImageSurface(aSize, aFormat);
      mCachedSize = aSize;
      mCachedFormat = aFormat;
    }

    MOZ_ASSERT(mCachedTempSurface->Stride() == mCachedTempSurface->Width() * 4);
    return mCachedTempSurface;
  }

  void DiscardTempSurface()
  {
    mCachedTempSurface = nullptr;
  }
};

void
BasicCanvasLayer::Initialize(const Data& aData)
{
  NS_ASSERTION(mSurface == nullptr, "BasicCanvasLayer::Initialize called twice!");

  if (aData.mSurface) {
    mSurface = aData.mSurface;
    NS_ASSERTION(aData.mGLContext == nullptr,
                 "CanvasLayer can't have both surface and GLContext");
    mNeedsYFlip = false;
  } else if (aData.mGLContext) {
    NS_ASSERTION(aData.mGLContext->IsOffscreen(), "canvas gl context isn't offscreen");
    mGLContext = aData.mGLContext;
    mGLBufferIsPremultiplied = aData.mGLBufferIsPremultiplied;
    mCanvasFramebuffer = mGLContext->GetOffscreenFBO();
    mNeedsYFlip = true;
  } else if (aData.mDrawTarget) {
    mDrawTarget = aData.mDrawTarget;
    mSurface = gfxPlatform::GetPlatform()->CreateThebesSurfaceAliasForDrawTarget_hack(mDrawTarget);
    mNeedsYFlip = false;
  } else {
    NS_ERROR("CanvasLayer created without mSurface, mDrawTarget or mGLContext?");
  }

  mBounds.SetRect(0, 0, aData.mSize.width, aData.mSize.height);
}

void
BasicCanvasLayer::UpdateSurface(gfxASurface* aDestSurface, Layer* aMaskLayer)
{
  if (!IsDirty())
    return;
  Painted();

  if (mDrawTarget) {
    mDrawTarget->Flush();
    if (mDrawTarget->GetType() == BACKEND_COREGRAPHICS_ACCELERATED) {
      // We have an accelerated CG context which has changed, unlike a bitmap surface
      // where we can alias the bits on initializing the mDrawTarget, we need to readback
      // and copy the accelerated surface each frame. We want to support this for quick
      // thumbnail but if we're going to be doing this every frame it likely is better
      // to use a non accelerated (bitmap) canvas.
      mSurface = gfxPlatform::GetPlatform()->GetThebesSurfaceForDrawTarget(mDrawTarget);
    }
  }

  if (!mGLContext && aDestSurface) {
    nsRefPtr<gfxContext> tmpCtx = new gfxContext(aDestSurface);
    tmpCtx->SetOperator(gfxContext::OPERATOR_SOURCE);
    BasicCanvasLayer::PaintWithOpacity(tmpCtx, 1.0f, aMaskLayer);
    return;
  }

  if (mGLContext) {
    if (aDestSurface && aDestSurface->GetType() != gfxASurface::SurfaceTypeImage) {
      NS_ASSERTION(aDestSurface->GetType() == gfxASurface::SurfaceTypeImage,
                   "Destination surface must be ImageSurface type");
      return;
    }

    // We need to read from the GLContext
    mGLContext->MakeCurrent();

    gfxIntSize readSize(mBounds.width, mBounds.height);
    gfxImageFormat format = (GetContentFlags() & CONTENT_OPAQUE)
                              ? gfxASurface::ImageFormatRGB24
                              : gfxASurface::ImageFormatARGB32;

    nsRefPtr<gfxImageSurface> readSurf;
    nsRefPtr<gfxImageSurface> resultSurf;

    bool usingTempSurface = false;

    if (aDestSurface) {
      resultSurf = static_cast<gfxImageSurface*>(aDestSurface);

      if (resultSurf->GetSize() != readSize ||
          resultSurf->Stride() != resultSurf->Width() * 4)
      {
        readSurf = GetTempSurface(readSize, format);
        usingTempSurface = true;
      }
    } else {
      resultSurf = GetTempSurface(readSize, format);
      usingTempSurface = true;
    }

    if (!usingTempSurface)
      DiscardTempSurface();

    if (!readSurf)
      readSurf = resultSurf;

    if (!resultSurf || resultSurf->CairoStatus() != 0)
      return;

    MOZ_ASSERT(readSurf);
    MOZ_ASSERT(readSurf->Stride() == mBounds.width * 4, "gfxImageSurface stride isn't what we expect!");

    // We need to Flush() the surface before modifying it outside of cairo.
    readSurf->Flush();
    mGLContext->ReadScreenIntoImageSurface(readSurf);
    readSurf->MarkDirty();

    // If the underlying GLContext doesn't have a framebuffer into which
    // premultiplied values were written, we have to do this ourselves here.
    // Note that this is a WebGL attribute; GL itself has no knowledge of
    // premultiplied or unpremultiplied alpha.
    if (!mGLBufferIsPremultiplied)
      gfxUtils::PremultiplyImageSurface(readSurf);

    if (readSurf != resultSurf) {
      MOZ_ASSERT(resultSurf->Width() >= readSurf->Width());
      MOZ_ASSERT(resultSurf->Height() >= readSurf->Height());

      resultSurf->Flush();
      resultSurf->CopyFrom(readSurf);
      resultSurf->MarkDirty();
    }

    // stick our surface into mSurface, so that the Paint() path is the same
    if (!aDestSurface) {
      mSurface = resultSurf;
    }
  }
}

void
BasicCanvasLayer::Paint(gfxContext* aContext, Layer* aMaskLayer)
{
  if (IsHidden())
    return;
  UpdateSurface();
  FireDidTransactionCallback();
  PaintWithOpacity(aContext, GetEffectiveOpacity(), aMaskLayer);
}

void
BasicCanvasLayer::PaintWithOpacity(gfxContext* aContext,
                                   float aOpacity,
                                   Layer* aMaskLayer)
{
  NS_ASSERTION(BasicManager()->InDrawing(),
               "Can only draw in drawing phase");

  if (!mSurface) {
    NS_WARNING("No valid surface to draw!");
    return;
  }

  nsRefPtr<gfxPattern> pat = new gfxPattern(mSurface);

  pat->SetFilter(mFilter);
  pat->SetExtend(gfxPattern::EXTEND_PAD);

  gfxMatrix m;
  if (mNeedsYFlip) {
    m = aContext->CurrentMatrix();
    aContext->Translate(gfxPoint(0.0, mBounds.height));
    aContext->Scale(1.0, -1.0);
  }

  // If content opaque, then save off current operator and set to source.
  // This ensures that alpha is not applied even if the source surface
  // has an alpha channel
  gfxContext::GraphicsOperator savedOp;
  if (GetContentFlags() & CONTENT_OPAQUE) {
    savedOp = aContext->CurrentOperator();
    aContext->SetOperator(gfxContext::OPERATOR_SOURCE);
  }

  AutoSetOperator setOperator(aContext, GetOperator());
  aContext->NewPath();
  // No need to snap here; our transform is already set up to snap our rect
  aContext->Rectangle(gfxRect(0, 0, mBounds.width, mBounds.height));
  aContext->SetPattern(pat);

  FillWithMask(aContext, aOpacity, aMaskLayer);

  // Restore surface operator
  if (GetContentFlags() & CONTENT_OPAQUE) {
    aContext->SetOperator(savedOp);
  }  

  if (mNeedsYFlip) {
    aContext->SetMatrix(m);
  }
}

class BasicShadowableCanvasLayer : public BasicCanvasLayer,
                                   public BasicShadowableLayer
{
public:
  BasicShadowableCanvasLayer(BasicShadowLayerManager* aManager) :
    BasicCanvasLayer(aManager),
    mBufferIsOpaque(false)
  {
    MOZ_COUNT_CTOR(BasicShadowableCanvasLayer);
  }
  virtual ~BasicShadowableCanvasLayer()
  {
    DestroyBackBuffer();
    MOZ_COUNT_DTOR(BasicShadowableCanvasLayer);
  }

  virtual void Initialize(const Data& aData);
  virtual void Paint(gfxContext* aContext, Layer* aMaskLayer);

  virtual void ClearCachedResources() MOZ_OVERRIDE
  {
    DestroyBackBuffer();
  }

  virtual void FillSpecificAttributes(SpecificLayerAttributes& aAttrs)
  {
    aAttrs = CanvasLayerAttributes(mFilter);
  }

  virtual Layer* AsLayer() { return this; }
  virtual ShadowableLayer* AsShadowableLayer() { return this; }

  virtual void SetBackBuffer(const SurfaceDescriptor& aBuffer)
  {
    mBackBuffer = aBuffer;
  }

  virtual void Disconnect()
  {
    mBackBuffer = SurfaceDescriptor();
    BasicShadowableLayer::Disconnect();
  }

  void DestroyBackBuffer()
  {
    if (mBackBuffer.type() == SurfaceDescriptor::TSharedTextureDescriptor) {
      SharedTextureDescriptor handle = mBackBuffer.get_SharedTextureDescriptor();
      if (mGLContext && handle.handle()) {
        mGLContext->ReleaseSharedHandle(handle.shareType(), handle.handle());
        mBackBuffer = SurfaceDescriptor();
      }
    } else if (IsSurfaceDescriptorValid(mBackBuffer)) {
      BasicManager()->ShadowLayerForwarder::DestroySharedSurface(&mBackBuffer);
      mBackBuffer = SurfaceDescriptor();
    }
  }

private:
  typedef mozilla::gl::SharedTextureHandle SharedTextureHandle;
  typedef mozilla::gl::TextureImage TextureImage;
  SharedTextureHandle GetSharedBackBufferHandle()
  {
    if (mBackBuffer.type() == SurfaceDescriptor::TSharedTextureDescriptor)
      return mBackBuffer.get_SharedTextureDescriptor().handle();
    return 0;
  }

  BasicShadowLayerManager* BasicManager()
  {
    return static_cast<BasicShadowLayerManager*>(mManager);
  }

  SurfaceDescriptor mBackBuffer;
  bool mBufferIsOpaque;
};

void
BasicShadowableCanvasLayer::Initialize(const Data& aData)
{
  BasicCanvasLayer::Initialize(aData);
  if (!HasShadow())
      return;

  // XXX won't get here currently; need to figure out what to do on
  // canvas resizes

  if (IsSurfaceDescriptorValid(mBackBuffer)) {
    AutoOpenSurface backSurface(OPEN_READ_ONLY, mBackBuffer);
    if (gfxIntSize(mBounds.width, mBounds.height) != backSurface.Size()) {
      DestroyBackBuffer();
    }
  }
}

void
BasicShadowableCanvasLayer::Paint(gfxContext* aContext, Layer* aMaskLayer)
{
  if (!HasShadow()) {
    BasicCanvasLayer::Paint(aContext, aMaskLayer);
    return;
  }

  if (!IsDirty())
    return;

  if (mGLContext &&
      !mForceReadback &&
      BasicManager()->GetParentBackendType() == mozilla::layers::LAYERS_OPENGL)
  {
    GLContext::SharedTextureShareType shareType;
    // if process type is default, then it is single-process (non-e10s)
    if (XRE_GetProcessType() == GeckoProcessType_Default)
      shareType = GLContext::SameProcess;
    else
      shareType = GLContext::CrossProcess;

    SharedTextureHandle handle = GetSharedBackBufferHandle();
    if (!handle) {
      handle = mGLContext->CreateSharedHandle(shareType);
      if (handle) {
        mBackBuffer = SharedTextureDescriptor(shareType, handle, mBounds.Size(), false);
      }
    }
    if (handle) {
      mGLContext->MakeCurrent();
      mGLContext->UpdateSharedHandle(shareType, handle);
      // call Painted() to reset our dirty 'bit'
      Painted();
      FireDidTransactionCallback();
      BasicManager()->PaintedCanvas(BasicManager()->Hold(this),
                                    mNeedsYFlip,
                                    mBackBuffer);
      // Move SharedTextureHandle ownership to ShadowLayer
      mBackBuffer = SurfaceDescriptor();
      return;
    }
  }

  bool isOpaque = (GetContentFlags() & CONTENT_OPAQUE);
  if (!IsSurfaceDescriptorValid(mBackBuffer) ||
      isOpaque != mBufferIsOpaque) {
    DestroyBackBuffer();
    mBufferIsOpaque = isOpaque;

    gfxIntSize size(mBounds.width, mBounds.height);
    gfxASurface::gfxContentType type = isOpaque ?
        gfxASurface::CONTENT_COLOR : gfxASurface::CONTENT_COLOR_ALPHA;

    if (!BasicManager()->AllocBuffer(size, type, &mBackBuffer)) {
      NS_RUNTIMEABORT("creating CanvasLayer back buffer failed!");
    }
  }

  AutoOpenSurface autoBackSurface(OPEN_READ_WRITE, mBackBuffer);

  if (aMaskLayer) {
    static_cast<BasicImplData*>(aMaskLayer->ImplData())
      ->Paint(aContext, nullptr);
  }
  UpdateSurface(autoBackSurface.Get(), nullptr);
  FireDidTransactionCallback();

  BasicManager()->PaintedCanvas(BasicManager()->Hold(this),
                                mNeedsYFlip, mBackBuffer);
}

class BasicShadowCanvasLayer : public ShadowCanvasLayer,
                               public BasicImplData
{
public:
  BasicShadowCanvasLayer(BasicShadowLayerManager* aLayerManager) :
    ShadowCanvasLayer(aLayerManager, static_cast<BasicImplData*>(this))
  {
    MOZ_COUNT_CTOR(BasicShadowCanvasLayer);
  }
  virtual ~BasicShadowCanvasLayer()
  {
    MOZ_COUNT_DTOR(BasicShadowCanvasLayer);
  }

  virtual void Disconnect()
  {
    DestroyFrontBuffer();
    ShadowCanvasLayer::Disconnect();
  }

  virtual void Initialize(const Data& aData);
  void Swap(const CanvasSurface& aNewFront, bool needYFlip, CanvasSurface* aNewBack);

  virtual void DestroyFrontBuffer()
  {
    if (IsSurfaceDescriptorValid(mFrontSurface)) {
      mAllocator->DestroySharedSurface(&mFrontSurface);
    }
  }

  virtual void Paint(gfxContext* aContext, Layer* aMaskLayer);

private:
  BasicShadowLayerManager* BasicManager()
  {
    return static_cast<BasicShadowLayerManager*>(mManager);
  }

  SurfaceDescriptor mFrontSurface;
  bool mNeedsYFlip;
};


void
BasicShadowCanvasLayer::Initialize(const Data& aData)
{
  NS_RUNTIMEABORT("Incompatibe surface type");
}

void
BasicShadowCanvasLayer::Swap(const CanvasSurface& aNewFront, bool needYFlip,
                             CanvasSurface* aNewBack)
{
  AutoOpenSurface autoSurface(OPEN_READ_ONLY, aNewFront);
  // Destroy mFrontBuffer if size different
  gfxIntSize sz = autoSurface.Size();
  bool surfaceConfigChanged = sz != gfxIntSize(mBounds.width, mBounds.height);
  if (IsSurfaceDescriptorValid(mFrontSurface)) {
    AutoOpenSurface autoFront(OPEN_READ_ONLY, mFrontSurface);
    surfaceConfigChanged = surfaceConfigChanged ||
                           autoSurface.ContentType() != autoFront.ContentType();
  }
  if (surfaceConfigChanged) {
    DestroyFrontBuffer();
    mBounds.SetRect(0, 0, sz.width, sz.height);
  }

  mNeedsYFlip = needYFlip;
  // If mFrontBuffer
  if (IsSurfaceDescriptorValid(mFrontSurface)) {
    *aNewBack = mFrontSurface;
  } else {
    *aNewBack = null_t();
  }
  mFrontSurface = aNewFront;
}

void
BasicShadowCanvasLayer::Paint(gfxContext* aContext, Layer* aMaskLayer)
{
  NS_ASSERTION(BasicManager()->InDrawing(),
               "Can only draw in drawing phase");

  if (!IsSurfaceDescriptorValid(mFrontSurface)) {
    return;
  }

  AutoOpenSurface autoSurface(OPEN_READ_ONLY, mFrontSurface);
  nsRefPtr<gfxPattern> pat = new gfxPattern(autoSurface.Get());

  pat->SetFilter(mFilter);
  pat->SetExtend(gfxPattern::EXTEND_PAD);

  gfxRect r(0, 0, mBounds.width, mBounds.height);

  gfxMatrix m;
  if (mNeedsYFlip) {
    m = aContext->CurrentMatrix();
    aContext->Translate(gfxPoint(0.0, mBounds.height));
    aContext->Scale(1.0, -1.0);
  }

  AutoSetOperator setOperator(aContext, GetOperator());
  aContext->NewPath();
  // No need to snap here; our transform has already taken care of it
  aContext->Rectangle(r);
  aContext->SetPattern(pat);
  FillWithMask(aContext, GetEffectiveOpacity(), aMaskLayer);

  if (mNeedsYFlip) {
    aContext->SetMatrix(m);
  }
}

already_AddRefed<CanvasLayer>
BasicLayerManager::CreateCanvasLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<CanvasLayer> layer = new BasicCanvasLayer(this);
  return layer.forget();
}

already_AddRefed<CanvasLayer>
BasicShadowLayerManager::CreateCanvasLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<BasicShadowableCanvasLayer> layer =
    new BasicShadowableCanvasLayer(this);
  MAYBE_CREATE_SHADOW(Canvas);
  return layer.forget();
}

already_AddRefed<ShadowCanvasLayer>
BasicShadowLayerManager::CreateShadowCanvasLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<ShadowCanvasLayer> layer = new BasicShadowCanvasLayer(this);
  return layer.forget();
}

}
}
