/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
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
 * The Original Code is Mozilla Corporation code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Robert O'Callahan <robert@ocallahan.org>
 *   Chris Jones <jones.chris.g@gmail.com>
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

#ifdef MOZ_IPC
#  include "gfxSharedImageSurface.h"

#  include "mozilla/layers/PLayerChild.h"
#  include "mozilla/layers/PLayersChild.h"
#  include "mozilla/layers/PLayersParent.h"
#  include "ipc/ShadowLayerChild.h"
#endif

#include "BasicLayers.h"
#include "ImageLayers.h"

#include "nsTArray.h"
#include "nsGUIEvent.h"
#include "nsIRenderingContext.h"
#include "gfxContext.h"
#include "gfxImageSurface.h"
#include "gfxPattern.h"
#include "gfxPlatform.h"
#include "gfxUtils.h"
#include "ThebesLayerBuffer.h"
#include "nsIWidget.h"

#include "GLContext.h"

namespace mozilla {
namespace layers {

class BasicContainerLayer;
class ShadowableLayer;

/**
 * This is the ImplData for all Basic layers. It also exposes methods
 * private to the Basic implementation that are common to all Basic layer types.
 * In particular, there is an internal Paint() method that we can use
 * to paint the contents of non-Thebes layers.
 *
 * The class hierarchy for Basic layers is like this:
 *                                 BasicImplData
 * Layer                            |   |   |
 *  |                               |   |   |
 *  +-> ContainerLayer              |   |   |
 *  |    |                          |   |   |
 *  |    +-> BasicContainerLayer <--+   |   |
 *  |                                   |   |
 *  +-> ThebesLayer                     |   |
 *  |    |                              |   |
 *  |    +-> BasicThebesLayer <---------+   |
 *  |                                       |
 *  +-> ImageLayer                          |
 *       |                                  |
 *       +-> BasicImageLayer <--------------+
 */
class BasicImplData {
public:
  BasicImplData()
  {
    MOZ_COUNT_CTOR(BasicImplData);
  }
  virtual ~BasicImplData()
  {
    MOZ_COUNT_DTOR(BasicImplData);
  }

  /**
   * Layers that paint themselves, such as ImageLayers, should paint
   * in response to this method call. aContext will already have been
   * set up to account for all the properties of the layer (transform,
   * opacity, etc).
   */
  virtual void Paint(gfxContext* aContext,
                     LayerManager::DrawThebesLayerCallback aCallback,
                     void* aCallbackData,
                     float aOpacity) {}

  virtual ShadowableLayer* AsShadowableLayer() { return nsnull; }

  /**
   * Layers will get this call when their layer manager is destroyed, this
   * indicates they should clear resources they don't really need after their
   * LayerManager ceases to exist.
   */
  virtual void ClearCachedResources() {}
};

static BasicImplData*
ToData(Layer* aLayer)
{
  return static_cast<BasicImplData*>(aLayer->ImplData());
}

class BasicContainerLayer : public ContainerLayer, BasicImplData {
public:
  BasicContainerLayer(BasicLayerManager* aManager) :
    ContainerLayer(aManager, static_cast<BasicImplData*>(this))
  {
    MOZ_COUNT_CTOR(BasicContainerLayer);
  }
  virtual ~BasicContainerLayer();

  virtual void SetVisibleRegion(const nsIntRegion& aRegion)
  {
    NS_ASSERTION(BasicManager()->InConstruction(),
                 "Can only set properties in construction phase");
    ContainerLayer::SetVisibleRegion(aRegion);
  }
  virtual void InsertAfter(Layer* aChild, Layer* aAfter);
  virtual void RemoveChild(Layer* aChild);

protected:
  void RemoveChildInternal(Layer* aChild);

  BasicLayerManager* BasicManager()
  {
    return static_cast<BasicLayerManager*>(mManager);
  }
};

BasicContainerLayer::~BasicContainerLayer()
{
  while (mFirstChild) {
    RemoveChildInternal(mFirstChild);
  }

  MOZ_COUNT_DTOR(BasicContainerLayer);
}

void
BasicContainerLayer::InsertAfter(Layer* aChild, Layer* aAfter)
{
  NS_ASSERTION(BasicManager()->InConstruction(),
               "Can only set properties in construction phase");
  NS_ASSERTION(aChild->Manager() == Manager(),
               "Child has wrong manager");
  NS_ASSERTION(!aChild->GetParent(),
               "aChild already in the tree");
  NS_ASSERTION(!aChild->GetNextSibling() && !aChild->GetPrevSibling(),
               "aChild already has siblings?");
  NS_ASSERTION(!aAfter ||
               (aAfter->Manager() == Manager() &&
                aAfter->GetParent() == this),
               "aAfter is not our child");

  NS_ADDREF(aChild);

  aChild->SetParent(this);
  if (!aAfter) {
    aChild->SetNextSibling(mFirstChild);
    if (mFirstChild) {
      mFirstChild->SetPrevSibling(aChild);
    }
    mFirstChild = aChild;
    return;
  }

  Layer* next = aAfter->GetNextSibling();
  aChild->SetNextSibling(next);
  aChild->SetPrevSibling(aAfter);
  if (next) {
    next->SetPrevSibling(aChild);
  }
  aAfter->SetNextSibling(aChild);
}

void
BasicContainerLayer::RemoveChild(Layer* aChild)
{
  NS_ASSERTION(BasicManager()->InConstruction(),
               "Can only set properties in construction phase");
  RemoveChildInternal(aChild);
}

void
BasicContainerLayer::RemoveChildInternal(Layer* aChild)
{
  NS_ASSERTION(aChild->Manager() == Manager(),
               "Child has wrong manager");
  NS_ASSERTION(aChild->GetParent() == this,
               "aChild not our child");

  Layer* prev = aChild->GetPrevSibling();
  Layer* next = aChild->GetNextSibling();
  if (prev) {
    prev->SetNextSibling(next);
  } else {
    mFirstChild = next;
  }
  if (next) {
    next->SetPrevSibling(prev);
  }

  aChild->SetNextSibling(nsnull);
  aChild->SetPrevSibling(nsnull);
  aChild->SetParent(nsnull);

  NS_RELEASE(aChild);
}

class BasicThebesLayer;
class BasicThebesLayerBuffer : public ThebesLayerBuffer {
  typedef ThebesLayerBuffer Base;

public:
  BasicThebesLayerBuffer(BasicThebesLayer* aLayer)
    : Base(ContainsVisibleBounds)
    , mLayer(aLayer)
  {}

  virtual ~BasicThebesLayerBuffer()
  {}

  using Base::BufferRect;
  using Base::BufferRotation;

  /**
   * Complete the drawing operation. The region to draw must have been
   * drawn before this is called. The contents of the buffer are drawn
   * to aTarget.
   */
  void DrawTo(ThebesLayer* aLayer, PRBool aIsOpaqueContent,
              gfxContext* aTarget, float aOpacity);

  virtual already_AddRefed<gfxASurface>
  CreateBuffer(ContentType aType, const nsIntSize& aSize);

private:
  BasicThebesLayer* mLayer;
};

class BasicThebesLayer : public ThebesLayer, BasicImplData {
public:
  typedef BasicThebesLayerBuffer Buffer;

  BasicThebesLayer(BasicLayerManager* aLayerManager) :
    ThebesLayer(aLayerManager, static_cast<BasicImplData*>(this)),
    mBuffer(this)
  {
    MOZ_COUNT_CTOR(BasicThebesLayer);
  }
  virtual ~BasicThebesLayer()
  {
    MOZ_COUNT_DTOR(BasicThebesLayer);
  }

  virtual void SetVisibleRegion(const nsIntRegion& aRegion)
  {
    NS_ASSERTION(BasicManager()->InConstruction(),
                 "Can only set properties in construction phase");
    ThebesLayer::SetVisibleRegion(aRegion);
  }
  virtual void InvalidateRegion(const nsIntRegion& aRegion)
  {
    NS_ASSERTION(BasicManager()->InConstruction(),
                 "Can only set properties in construction phase");
    mValidRegion.Sub(mValidRegion, aRegion);
  }

  virtual void Paint(gfxContext* aContext,
                     LayerManager::DrawThebesLayerCallback aCallback,
                     void* aCallbackData,
                     float aOpacity);

  virtual void ClearCachedResources() { mBuffer.Clear(); mValidRegion.SetEmpty(); }
  
  virtual already_AddRefed<gfxASurface>
  CreateBuffer(Buffer::ContentType aType, const nsIntSize& aSize)
  {
    nsRefPtr<gfxASurface> referenceSurface = mBuffer.GetBuffer();
    if (!referenceSurface) {
      gfxContext* defaultTarget = BasicManager()->GetDefaultTarget();
      if (defaultTarget) {
        referenceSurface = defaultTarget->CurrentSurface();
      } else {
        nsIWidget* widget = BasicManager()->GetRetainerWidget();
        if (widget) {
          referenceSurface = widget->GetThebesSurface();
        } else {
          referenceSurface = BasicManager()->GetTarget()->CurrentSurface();
        }
      }
    }
    return referenceSurface->CreateSimilarSurface(
      aType, gfxIntSize(aSize.width, aSize.height));
  }

protected:
  BasicLayerManager* BasicManager()
  {
    return static_cast<BasicLayerManager*>(mManager);
  }

  virtual void
  PaintBuffer(gfxContext* aContext,
              const nsIntRegion& aRegionToDraw,
              const nsIntRegion& aRegionToInvalidate,
              LayerManager::DrawThebesLayerCallback aCallback,
              void* aCallbackData)
  {
    aCallback(this, aContext, aRegionToDraw, aRegionToInvalidate,
              aCallbackData);
    mValidRegion.Or(mValidRegion, aRegionToDraw);
  }

  Buffer mBuffer;
};

static void
ClipToContain(gfxContext* aContext, const nsIntRect& aRect)
{
  gfxRect deviceRect =
    aContext->UserToDevice(gfxRect(aRect.x, aRect.y, aRect.width, aRect.height));
  deviceRect.RoundOut();

  gfxMatrix currentMatrix = aContext->CurrentMatrix();
  aContext->IdentityMatrix();
  aContext->NewPath();
  aContext->Rectangle(deviceRect);
  aContext->Clip();
  aContext->SetMatrix(currentMatrix);
}

static void
InheritContextFlags(gfxContext* aSource, gfxContext* aDest)
{
  if (aSource->GetFlags() & gfxContext::FLAG_DESTINED_FOR_SCREEN) {
    aDest->SetFlag(gfxContext::FLAG_DESTINED_FOR_SCREEN);
  } else {
    aDest->ClearFlag(gfxContext::FLAG_DESTINED_FOR_SCREEN);
  }
}

void
BasicThebesLayer::Paint(gfxContext* aContext,
                        LayerManager::DrawThebesLayerCallback aCallback,
                        void* aCallbackData,
                        float aOpacity)
{
  NS_ASSERTION(BasicManager()->InDrawing(),
               "Can only draw in drawing phase");
  gfxContext* target = BasicManager()->GetTarget();
  NS_ASSERTION(target, "We shouldn't be called if there's no target");

  if (!BasicManager()->IsRetained()) {
    if (aOpacity != 1.0) {
      target->Save();
      ClipToContain(target, mVisibleRegion.GetBounds());
      target->PushGroup(gfxASurface::CONTENT_COLOR_ALPHA);
    }
    mValidRegion.SetEmpty();
    mBuffer.Clear();
    aCallback(this, target, mVisibleRegion, nsIntRegion(), aCallbackData);
    if (aOpacity != 1.0) {
      target->PopGroupToSource();
      target->Paint(aOpacity);
      target->Restore();
    }
    return;
  }

  nsRefPtr<gfxASurface> targetSurface = aContext->CurrentSurface();
  PRBool isOpaqueContent =
    (targetSurface->AreSimilarSurfacesSensitiveToContentType() &&
     aOpacity == 1.0 &&
     CanUseOpaqueSurface());
  {
    Buffer::ContentType contentType =
      isOpaqueContent ? gfxASurface::CONTENT_COLOR :
                        gfxASurface::CONTENT_COLOR_ALPHA;
    Buffer::PaintState state = mBuffer.BeginPaint(this, contentType);
    mValidRegion.Sub(mValidRegion, state.mRegionToInvalidate);

    if (state.mContext) {
      // The area that became invalid and is visible needs to be repainted
      // (this could be the whole visible area if our buffer switched
      // from RGB to RGBA, because we might need to repaint with
      // subpixel AA)
      state.mRegionToInvalidate.And(state.mRegionToInvalidate, mVisibleRegion);
      InheritContextFlags(target, state.mContext);
      PaintBuffer(state.mContext,
                  state.mRegionToDraw, state.mRegionToInvalidate,
                  aCallback, aCallbackData);
    } else {
      // It's possible that state.mRegionToInvalidate is nonempty here,
      // if we are shrinking the valid region to nothing.
      NS_ASSERTION(state.mRegionToDraw.IsEmpty(),
                   "If we need to draw, we should have a context");
    }
  }

  mBuffer.DrawTo(this, isOpaqueContent, target, aOpacity);
}

void
BasicThebesLayerBuffer::DrawTo(ThebesLayer* aLayer,
                               PRBool aIsOpaqueContent,
                               gfxContext* aTarget,
                               float aOpacity)
{
  aTarget->Save();
  ClipToRegion(aTarget, aLayer->GetVisibleRegion());
  if (aIsOpaqueContent) {
    aTarget->SetOperator(gfxContext::OPERATOR_SOURCE);
  }
  DrawBufferWithRotation(aTarget, aOpacity);
  aTarget->Restore();
}

already_AddRefed<gfxASurface>
BasicThebesLayerBuffer::CreateBuffer(ContentType aType, 
                                     const nsIntSize& aSize)
{
  return mLayer->CreateBuffer(aType, aSize);
}

class BasicImageLayer : public ImageLayer, BasicImplData {
public:
  BasicImageLayer(BasicLayerManager* aLayerManager) :
    ImageLayer(aLayerManager, static_cast<BasicImplData*>(this))
  {
    MOZ_COUNT_CTOR(BasicImageLayer);
  }
  virtual ~BasicImageLayer()
  {
    MOZ_COUNT_DTOR(BasicImageLayer);
  }

  virtual void SetVisibleRegion(const nsIntRegion& aRegion)
  {
    NS_ASSERTION(BasicManager()->InConstruction(),
                 "Can only set properties in construction phase");
    ImageLayer::SetVisibleRegion(aRegion);
  }

  virtual void Paint(gfxContext* aContext,
                     LayerManager::DrawThebesLayerCallback aCallback,
                     void* aCallbackData,
                     float aOpacity);

  static void PaintContext(gfxPattern* aPattern,
                           const gfxIntSize& aSize,
                           float aOpacity,
                           gfxContext* aContext);

protected:
  BasicLayerManager* BasicManager()
  {
    return static_cast<BasicLayerManager*>(mManager);
  }

  already_AddRefed<gfxPattern>
  GetAndPaintCurrentImage(gfxContext* aContext,
                          float aOpacity);

  gfxIntSize mSize;
};

void
BasicImageLayer::Paint(gfxContext* aContext,
                       LayerManager::DrawThebesLayerCallback aCallback,
                       void* aCallbackData,
                       float aOpacity)
{
  nsRefPtr<gfxPattern> dontcare = GetAndPaintCurrentImage(aContext, aOpacity);
}

already_AddRefed<gfxPattern>
BasicImageLayer::GetAndPaintCurrentImage(gfxContext* aContext,
                                         float aOpacity)
{
  if (!mContainer)
    return nsnull;

  nsRefPtr<gfxASurface> surface = mContainer->GetCurrentAsSurface(&mSize);
  if (!surface) {
    return nsnull;
  }

  nsRefPtr<gfxPattern> pat = new gfxPattern(surface);
  if (!pat) {
    return nsnull;
  }

  pat->SetFilter(mFilter);

  PaintContext(pat, mSize, aOpacity, aContext); 
  return pat.forget();
}

/*static*/ void
BasicImageLayer::PaintContext(gfxPattern* aPattern,
                              const gfxIntSize& aSize,
                              float aOpacity,
                              gfxContext* aContext)
{
  // Set PAD mode so that when the video is being scaled, we do not sample
  // outside the bounds of the video image.
  gfxPattern::GraphicsExtend extend = gfxPattern::EXTEND_PAD;

  // PAD is slow with X11 and Quartz surfaces, so prefer speed over correctness
  // and use NONE.
  nsRefPtr<gfxASurface> target = aContext->CurrentSurface();
  gfxASurface::gfxSurfaceType type = target->GetType();
  if (type == gfxASurface::SurfaceTypeXlib ||
      type == gfxASurface::SurfaceTypeXcb ||
      type == gfxASurface::SurfaceTypeQuartz) {
    extend = gfxPattern::EXTEND_NONE;
  }

  aPattern->SetExtend(extend);

  /* Draw RGB surface onto frame */
  aContext->NewPath();
  aContext->PixelSnappedRectangleAndSetPattern(
      gfxRect(0, 0, aSize.width, aSize.height), aPattern);
  if (aOpacity != 1.0) {
    aContext->Save();
    aContext->Clip();
    aContext->Paint(aOpacity);
    aContext->Restore();
  } else {
    aContext->Fill();
  }
}

class BasicColorLayer : public ColorLayer, BasicImplData {
public:
  BasicColorLayer(BasicLayerManager* aLayerManager) :
    ColorLayer(aLayerManager, static_cast<BasicImplData*>(this))
  {
    MOZ_COUNT_CTOR(BasicColorLayer);
  }
  virtual ~BasicColorLayer()
  {
    MOZ_COUNT_DTOR(BasicColorLayer);
  }

  virtual void SetVisibleRegion(const nsIntRegion& aRegion)
  {
    NS_ASSERTION(BasicManager()->InConstruction(),
                 "Can only set properties in construction phase");
    ColorLayer::SetVisibleRegion(aRegion);
  }

  virtual void Paint(gfxContext* aContext,
                     LayerManager::DrawThebesLayerCallback aCallback,
                     void* aCallbackData,
                     float aOpacity);

protected:
  BasicLayerManager* BasicManager()
  {
    return static_cast<BasicLayerManager*>(mManager);
  }
};

void
BasicColorLayer::Paint(gfxContext* aContext,
                       LayerManager::DrawThebesLayerCallback aCallback,
                       void* aCallbackData,
                       float aOpacity)
{
  aContext->SetColor(mColor);
  aContext->Paint(aOpacity);
}

class BasicCanvasLayer : public CanvasLayer,
                         BasicImplData
{
public:
  BasicCanvasLayer(BasicLayerManager* aLayerManager) :
    CanvasLayer(aLayerManager, static_cast<BasicImplData*>(this))
  {
    MOZ_COUNT_CTOR(BasicCanvasLayer);
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
  virtual void Updated(const nsIntRect& aRect);
  virtual void Paint(gfxContext* aContext,
                     LayerManager::DrawThebesLayerCallback aCallback,
                     void* aCallbackData,
                     float aOpacity);

protected:
  BasicLayerManager* BasicManager()
  {
    return static_cast<BasicLayerManager*>(mManager);
  }

  nsRefPtr<gfxASurface> mSurface;
  nsRefPtr<mozilla::gl::GLContext> mGLContext;
  PRUint32 mCanvasFramebuffer;

  nsIntRect mBounds;
  nsIntRect mUpdatedRect;

  PRPackedBool mGLBufferIsPremultiplied;
  PRPackedBool mNeedsYFlip;
};

void
BasicCanvasLayer::Initialize(const Data& aData)
{
  NS_ASSERTION(mSurface == nsnull, "BasicCanvasLayer::Initialize called twice!");

  mUpdatedRect.Empty();

  if (aData.mSurface) {
    mSurface = aData.mSurface;
    NS_ASSERTION(aData.mGLContext == nsnull,
                 "CanvasLayer can't have both surface and GLContext");
    mNeedsYFlip = PR_FALSE;
  } else if (aData.mGLContext) {
    NS_ASSERTION(aData.mGLContext->IsOffscreen(), "canvas gl context isn't offscreen");
    mGLContext = aData.mGLContext;
    mGLBufferIsPremultiplied = aData.mGLBufferIsPremultiplied;
    mCanvasFramebuffer = mGLContext->GetOffscreenFBO();
    mNeedsYFlip = PR_TRUE;
  } else {
    NS_ERROR("CanvasLayer created without mSurface or mGLContext?");
  }

  mBounds.SetRect(0, 0, aData.mSize.width, aData.mSize.height);
}

void
BasicCanvasLayer::Updated(const nsIntRect& aRect)
{
  NS_ASSERTION(mUpdatedRect.IsEmpty(),
               "CanvasLayer::Updated called more than once in a transaction!");

  mUpdatedRect.UnionRect(mUpdatedRect, aRect);

  if (mGLContext) {
    nsRefPtr<gfxImageSurface> isurf =
      new gfxImageSurface(gfxIntSize(mBounds.width, mBounds.height),
                          IsOpaqueContent()
                            ? gfxASurface::ImageFormatRGB24
                            : gfxASurface::ImageFormatARGB32);
    if (!isurf || isurf->CairoStatus() != 0) {
      return;
    }

    NS_ASSERTION(isurf->Stride() == mBounds.width * 4, "gfxImageSurface stride isn't what we expect!");

    // We need to read from the GLContext
    mGLContext->MakeCurrent();

    // We have to flush to ensure that any buffered GL operations are
    // in the framebuffer before we read.
    mGLContext->fFlush();

    PRUint32 currentFramebuffer = 0;

    mGLContext->fGetIntegerv(LOCAL_GL_FRAMEBUFFER_BINDING, (GLint*)&currentFramebuffer);

    // Make sure that we read pixels from the correct framebuffer, regardless
    // of what's currently bound.
    if (currentFramebuffer != mCanvasFramebuffer)
      mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mCanvasFramebuffer);

    // For simplicity, we read the entire framebuffer for now -- in
    // the future we should use mUpdatedRect, though with WebGL we don't
    // have an easy way to generate one.
    mGLContext->ReadPixelsIntoImageSurface(0, 0,
                                           mBounds.width, mBounds.height,
                                           isurf);

    // Put back the previous framebuffer binding.
    if (currentFramebuffer != mCanvasFramebuffer)
      mGLContext->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, currentFramebuffer);

    // If the underlying GLContext doesn't have a framebuffer into which
    // premultiplied values were written, we have to do this ourselves here.
    // Note that this is a WebGL attribute; GL itself has no knowledge of
    // premultiplied or unpremultiplied alpha.
    if (!mGLBufferIsPremultiplied)
      gfxUtils::PremultiplyImageSurface(isurf);

    // stick our surface into mSurface, so that the Paint() path is the same
    mSurface = isurf;
  }

  // sanity
  NS_ASSERTION(mUpdatedRect.IsEmpty() || mBounds.Contains(mUpdatedRect),
               "CanvasLayer: Updated rect bigger than bounds!");
}

void
BasicCanvasLayer::Paint(gfxContext* aContext,
                        LayerManager::DrawThebesLayerCallback aCallback,
                        void* aCallbackData,
                        float aOpacity)
{
  NS_ASSERTION(BasicManager()->InDrawing(),
               "Can only draw in drawing phase");

  nsRefPtr<gfxPattern> pat = new gfxPattern(mSurface);

  pat->SetFilter(mFilter);
  pat->SetExtend(gfxPattern::EXTEND_PAD);

  gfxRect r(0, 0, mBounds.width, mBounds.height);
  gfxMatrix m;
  if (mNeedsYFlip) {
    m = aContext->CurrentMatrix();
    aContext->Translate(gfxPoint(0.0, mBounds.height));
    aContext->Scale(1.0, -1.0);
  }

  aContext->NewPath();
  aContext->PixelSnappedRectangleAndSetPattern(r, pat);
  if (aOpacity != 1.0) {
    aContext->Save();
    aContext->Clip();
    aContext->Paint(aOpacity);
    aContext->Restore();
  } else {
    aContext->Fill();
  }

  if (mNeedsYFlip) {
    aContext->SetMatrix(m);
  }

  mUpdatedRect.Empty();
}

static nsIntRect
ToOutsideIntRect(const gfxRect &aRect)
{
  gfxRect r = aRect;
  r.RoundOut();
  return nsIntRect(r.pos.x, r.pos.y, r.size.width, r.size.height);
}

/**
 * Returns false if there is at most one leaf layer overlapping aBounds
 * and that layer is opaque.
 * aDirtyVisibleRegionInContainer is filled in only if we return false.
 * It contains the union of the visible regions of leaf layers under aLayer.
 */
static PRBool
MayHaveOverlappingOrTransparentLayers(Layer* aLayer,
                                      const nsIntRect& aBounds,
                                      nsIntRegion* aDirtyVisibleRegionInContainer)
{
  if (!aLayer->IsOpaqueContent()) {
    return PR_TRUE;
  }

  gfxMatrix matrix;
  if (!aLayer->GetTransform().Is2D(&matrix) ||
      matrix.HasNonIntegerTranslation()) {
    return PR_TRUE;
  }

  nsIntPoint translation = nsIntPoint(PRInt32(matrix.x0), PRInt32(matrix.y0));
  nsIntRect bounds = aBounds - translation;

  nsIntRect clippedDirtyRect = bounds;
  const nsIntRect* clipRect = aLayer->GetClipRect();
  if (clipRect) {
    clippedDirtyRect.IntersectRect(clippedDirtyRect, *clipRect - translation);
  }
  aDirtyVisibleRegionInContainer->And(aLayer->GetVisibleRegion(), clippedDirtyRect);
  aDirtyVisibleRegionInContainer->MoveBy(translation);

  /* Ignore layers outside the clip rect */
  if (aDirtyVisibleRegionInContainer->IsEmpty()) {
    return PR_FALSE;
  }

  nsIntRegion region;

  for (Layer* child = aLayer->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    nsIntRegion childRegion;
    if (MayHaveOverlappingOrTransparentLayers(child, bounds, &childRegion)) {
      return PR_TRUE;
    }

    nsIntRegion tmp;
    tmp.And(region, childRegion);
    if (!tmp.IsEmpty()) {
      return PR_TRUE;
    }

    region.Or(region, childRegion);
  }

  return PR_FALSE;
}

BasicLayerManager::BasicLayerManager(nsIWidget* aWidget) :
#ifdef DEBUG
  mPhase(PHASE_NONE),
#endif
  mWidget(aWidget)
  , mDoubleBuffering(BUFFER_NONE), mUsingDefaultTarget(PR_FALSE)
{
  MOZ_COUNT_CTOR(BasicLayerManager);
  NS_ASSERTION(aWidget, "Must provide a widget");
}

BasicLayerManager::BasicLayerManager() :
#ifdef DEBUG
  mPhase(PHASE_NONE),
#endif
  mWidget(nsnull)
  , mDoubleBuffering(BUFFER_NONE), mUsingDefaultTarget(PR_FALSE)
{
  MOZ_COUNT_CTOR(BasicLayerManager);
}

BasicLayerManager::~BasicLayerManager()
{
  NS_ASSERTION(!InTransaction(), "Died during transaction?");

  ClearCachedResources();

  mRoot = nsnull;

  MOZ_COUNT_DTOR(BasicLayerManager);
}

void
BasicLayerManager::SetDefaultTarget(gfxContext* aContext,
                                    BufferMode aDoubleBuffering)
{
  NS_ASSERTION(!InTransaction(),
               "Must set default target outside transaction");
  mDefaultTarget = aContext;
  mDoubleBuffering = aDoubleBuffering;
}

void
BasicLayerManager::BeginTransaction()
{
  mUsingDefaultTarget = PR_TRUE;
  BeginTransactionWithTarget(mDefaultTarget);
}

already_AddRefed<gfxContext>
BasicLayerManager::PushGroupWithCachedSurface(gfxContext *aTarget,
                                              gfxASurface::gfxContentType aContent,
                                              gfxPoint *aSavedOffset)
{
  gfxContextMatrixAutoSaveRestore saveMatrix(aTarget);
  aTarget->IdentityMatrix();

  nsRefPtr<gfxASurface> currentSurf = aTarget->CurrentSurface();
  gfxRect clip = aTarget->GetClipExtents();
  clip.RoundOut();

  nsRefPtr<gfxContext> ctx =
    mCachedSurface.Get(aContent,
                       gfxIntSize(clip.size.width, clip.size.height),
                       currentSurf);
  InheritContextFlags(aTarget, ctx);
  /* Align our buffer for the original surface */
  ctx->Translate(-clip.pos);
  *aSavedOffset = clip.pos;
  ctx->Multiply(saveMatrix.Matrix());
  return ctx.forget();
}

void
BasicLayerManager::PopGroupWithCachedSurface(gfxContext *aTarget,
                                             const gfxPoint& aSavedOffset)
{
  if (!mTarget)
    return;

  gfxContextMatrixAutoSaveRestore saveMatrix(aTarget);
  aTarget->IdentityMatrix();

  aTarget->SetSource(mTarget->OriginalSurface(), aSavedOffset);
  aTarget->Paint();
}

void
BasicLayerManager::BeginTransactionWithTarget(gfxContext* aTarget)
{
#ifdef MOZ_LAYERS_HAVE_LOG
  MOZ_LAYERS_LOG(("[----- BeginTransaction"));
  Log();
#endif

  NS_ASSERTION(!InTransaction(), "Nested transactions not allowed");
#ifdef DEBUG
  mPhase = PHASE_CONSTRUCTION;
#endif
  mTarget = aTarget;
}

void
BasicLayerManager::EndTransaction(DrawThebesLayerCallback aCallback,
                                  void* aCallbackData)
{
#ifdef MOZ_LAYERS_HAVE_LOG
  MOZ_LAYERS_LOG(("  ----- (beginning paint)"));
  Log();
#endif

  NS_ASSERTION(InConstruction(), "Should be in construction phase");
#ifdef DEBUG
  mPhase = PHASE_DRAWING;
#endif

  if (mTarget) {
    NS_ASSERTION(mRoot, "Root not set");

    nsRefPtr<gfxContext> finalTarget = mTarget;
    gfxPoint cachedSurfaceOffset;

    nsIntRegion rootRegion;
    PRBool useDoubleBuffering = mUsingDefaultTarget &&
      mDoubleBuffering != BUFFER_NONE &&
      MayHaveOverlappingOrTransparentLayers(mRoot,
                                            ToOutsideIntRect(mTarget->GetClipExtents()),
                                            &rootRegion);
    if (useDoubleBuffering) {
      nsRefPtr<gfxASurface> targetSurface = mTarget->CurrentSurface();
      mTarget = PushGroupWithCachedSurface(mTarget, targetSurface->GetContentType(),
                                           &cachedSurfaceOffset);
    }

    PaintLayer(mRoot, aCallback, aCallbackData, mRoot->GetOpacity());
    
    if (useDoubleBuffering) {
      finalTarget->SetOperator(gfxContext::OPERATOR_SOURCE);
      PopGroupWithCachedSurface(finalTarget, cachedSurfaceOffset);
    }

    mTarget = nsnull;
  }

#ifdef MOZ_LAYERS_HAVE_LOG
  Log();
  MOZ_LAYERS_LOG(("]----- EndTransaction"));
#endif

#ifdef DEBUG
  mPhase = PHASE_NONE;
#endif
  mUsingDefaultTarget = PR_FALSE;
}

void
BasicLayerManager::SetRoot(Layer* aLayer)
{
  NS_ASSERTION(aLayer, "Root can't be null");
  NS_ASSERTION(aLayer->Manager() == this, "Wrong manager");
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  mRoot = aLayer;
}

// Returns true if we need to save the state of the gfxContext when
// we start painting aLayer (and restore the state when we've finished
// painting aLayer)
static PRBool
NeedsState(Layer* aLayer)
{
  return aLayer->GetClipRect() != nsnull ||
         !aLayer->GetTransform().IsIdentity();
}

static inline int
GetChildCount(Layer *aLayer)
{
  int count = 0;
  for (Layer* child = aLayer->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    count++;
  }
  return count;
}

void
BasicLayerManager::PaintLayer(Layer* aLayer,
                              DrawThebesLayerCallback aCallback,
                              void* aCallbackData,
                              float aOpacity)
{
  PRBool needsGroup = aOpacity != 1.0;
  PRBool needsSaveRestore = needsGroup || NeedsState(aLayer);
  int children = GetChildCount(aLayer);

 if (needsSaveRestore) {
    mTarget->Save();

    if (aLayer->GetClipRect()) {
      const nsIntRect& r = *aLayer->GetClipRect();
      mTarget->NewPath();
      mTarget->Rectangle(gfxRect(r.x, r.y, r.width, r.height), PR_TRUE);
      mTarget->Clip();
    }

    gfxMatrix transform;
    // XXX we need to add some kind of 3D transform support, possibly
    // using pixman?
    NS_ASSERTION(aLayer->GetTransform().Is2D(),
                 "Only 2D transforms supported currently");
    aLayer->GetTransform().Is2D(&transform);
    mTarget->Multiply(transform);

    if (needsGroup && children > 1) {
      // If we need to call PushGroup, we should clip to the smallest possible
      // area first to minimize the size of the temporary surface.
      ClipToContain(mTarget, aLayer->GetVisibleRegion().GetBounds());

      gfxASurface::gfxContentType type = aLayer->CanUseOpaqueSurface()
          ? gfxASurface::CONTENT_COLOR : gfxASurface::CONTENT_COLOR_ALPHA;
      mTarget->PushGroup(type);
    }
  }

  /* Only paint ourself, or our children - This optimization relies on this! */
  if (!children) {
    ToData(aLayer)->Paint(mTarget, aCallback, aCallbackData, aOpacity);
  } else {
    for (Layer* child = aLayer->GetFirstChild(); child;
         child = child->GetNextSibling()) {
      /* If we have a single child, we can pass the aOpacity down, otherwise we will have double buffered */
      if (needsGroup && children == 1) {
        PaintLayer(child, aCallback, aCallbackData, child->GetOpacity() * aOpacity);
      } else {
        PaintLayer(child, aCallback, aCallbackData, child->GetOpacity());
      }
    }
  }

  if (needsSaveRestore) {
    if (needsGroup && children > 1) {
      mTarget->PopGroupToSource();
      mTarget->Paint(aOpacity);
    }

    mTarget->Restore();
  }
}

void
BasicLayerManager::ClearCachedResources()
{
  if (mRoot) {
    ClearLayer(mRoot);
  }

  mCachedSurface.Expire();
}
void
BasicLayerManager::ClearLayer(Layer* aLayer)
{
  ToData(aLayer)->ClearCachedResources();
  for (Layer* child = aLayer->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    ClearLayer(child);
  }
}

already_AddRefed<ThebesLayer>
BasicLayerManager::CreateThebesLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<ThebesLayer> layer = new BasicThebesLayer(this);
  return layer.forget();
}

already_AddRefed<ContainerLayer>
BasicLayerManager::CreateContainerLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<ContainerLayer> layer = new BasicContainerLayer(this);
  return layer.forget();
}

already_AddRefed<ImageLayer>
BasicLayerManager::CreateImageLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<ImageLayer> layer = new BasicImageLayer(this);
  return layer.forget();
}

already_AddRefed<ColorLayer>
BasicLayerManager::CreateColorLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<ColorLayer> layer = new BasicColorLayer(this);
  return layer.forget();
}

already_AddRefed<CanvasLayer>
BasicLayerManager::CreateCanvasLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<CanvasLayer> layer = new BasicCanvasLayer(this);
  return layer.forget();
}


#ifdef MOZ_IPC

class BasicShadowableLayer : public ShadowableLayer
{
public:
  BasicShadowableLayer()
  {
    MOZ_COUNT_CTOR(BasicShadowableLayer);
  }

  ~BasicShadowableLayer()
  {
    if (HasShadow()) {
      PLayerChild::Send__delete__(GetShadow());
    }
    MOZ_COUNT_DTOR(BasicShadowableLayer);
  }

  void SetShadow(PLayerChild* aShadow)
  {
    NS_ABORT_IF_FALSE(!mShadow, "can't have two shadows (yet)");
    mShadow = aShadow;
  }

  virtual void SetBackBuffer(gfxSharedImageSurface* aBuffer)
  {
    NS_RUNTIMEABORT("if this default impl is called, |aBuffer| leaks");
  }
};

static ShadowableLayer*
ToShadowable(Layer* aLayer)
{
  return ToData(aLayer)->AsShadowableLayer();
}

template<class OpT>
static BasicShadowableLayer*
GetBasicShadowable(const OpT& op)
{
  return static_cast<BasicShadowableLayer*>(
    static_cast<const ShadowLayerChild*>(op.layerChild())->layer());
}

class BasicShadowableContainerLayer : public BasicContainerLayer,
                                      public BasicShadowableLayer {
public:
  BasicShadowableContainerLayer(BasicShadowLayerManager* aManager) :
    BasicContainerLayer(aManager)
  {
    MOZ_COUNT_CTOR(BasicShadowableContainerLayer);
  }
  virtual ~BasicShadowableContainerLayer()
  {
    MOZ_COUNT_DTOR(BasicShadowableContainerLayer);
  }

  virtual void InsertAfter(Layer* aChild, Layer* aAfter);
  virtual void RemoveChild(Layer* aChild);

  virtual Layer* AsLayer() { return this; }
  virtual ShadowableLayer* AsShadowableLayer() { return this; }

private:
  BasicShadowLayerManager* ShadowManager()
  {
    return static_cast<BasicShadowLayerManager*>(mManager);
  }
};

void
BasicShadowableContainerLayer::InsertAfter(Layer* aChild, Layer* aAfter)
{
  if (HasShadow()) {
    ShadowManager()->InsertAfter(ShadowManager()->Hold(this),
                                 ShadowManager()->Hold(aChild),
                                 aAfter ? ShadowManager()->Hold(aAfter) : nsnull);
  }
  BasicContainerLayer::InsertAfter(aChild, aAfter);
}

void
BasicShadowableContainerLayer::RemoveChild(Layer* aChild)
{
  if (HasShadow()) {
    ShadowManager()->RemoveChild(ShadowManager()->Hold(this),
                                 ShadowManager()->Hold(aChild));
  }
  BasicContainerLayer::RemoveChild(aChild);
}

class BasicShadowableThebesLayer : public BasicThebesLayer,
                                   public BasicShadowableLayer
{
  typedef BasicThebesLayer Base;

public:
  BasicShadowableThebesLayer(BasicShadowLayerManager* aManager) :
    BasicThebesLayer(aManager)
  {
    MOZ_COUNT_CTOR(BasicShadowableThebesLayer);
  }
  virtual ~BasicShadowableThebesLayer()
  {
    if (mBackBuffer)
      BasicManager()->ShadowLayerForwarder::DestroySharedSurface(mBackBuffer);
    MOZ_COUNT_DTOR(BasicShadowableThebesLayer);
  }

  virtual void FillSpecificAttributes(SpecificLayerAttributes& aAttrs)
  {
    aAttrs = ThebesLayerAttributes(GetValidRegion());
  }

  virtual Layer* AsLayer() { return this; }
  virtual ShadowableLayer* AsShadowableLayer() { return this; }

  virtual void SetBackBuffer(gfxSharedImageSurface* aBuffer)
  {
    mBackBuffer = aBuffer;
  }

private:
  BasicShadowLayerManager* BasicManager()
  {
    return static_cast<BasicShadowLayerManager*>(mManager);
  }

  NS_OVERRIDE virtual void
  PaintBuffer(gfxContext* aContext,
              const nsIntRegion& aRegionToDraw,
              const nsIntRegion& aRegionToInvalidate,
              LayerManager::DrawThebesLayerCallback aCallback,
              void* aCallbackData);

  NS_OVERRIDE virtual already_AddRefed<gfxASurface>
  CreateBuffer(Buffer::ContentType aType, const nsIntSize& aSize);

  nsRefPtr<gfxSharedImageSurface> mBackBuffer;
  nsIntSize mBufferSize;
};
 
void
BasicShadowableThebesLayer::PaintBuffer(gfxContext* aContext,
                                        const nsIntRegion& aRegionToDraw,
                                        const nsIntRegion& aRegionToInvalidate,
                                        LayerManager::DrawThebesLayerCallback aCallback,
                                        void* aCallbackData)
{
  NS_ABORT_IF_FALSE(!!mBackBuffer, "should have a back buffer by now");

  Base::PaintBuffer(aContext, aRegionToDraw, aRegionToInvalidate,
                    aCallback, aCallbackData);

  nsRefPtr<gfxContext> tmpCtx = new gfxContext(mBackBuffer);
  tmpCtx->DrawSurface(aContext->OriginalSurface(),
                      gfxIntSize(mBufferSize.width, mBufferSize.height));

  BasicManager()->PaintedThebesBuffer(BasicManager()->Hold(this),
                                      mBuffer.BufferRect(),
                                      mBuffer.BufferRotation(),
                                      mBackBuffer);
}

already_AddRefed<gfxASurface>
BasicShadowableThebesLayer::CreateBuffer(Buffer::ContentType aType,
                                         const nsIntSize& aSize)
{
  nsRefPtr<gfxSharedImageSurface> tmpFront;
  // XXX error handling
  if (!BasicManager()->AllocDoubleBuffer(gfxIntSize(aSize.width, aSize.height),
                                         gfxASurface::ImageFormatARGB32,
                                         getter_AddRefs(tmpFront),
                                         getter_AddRefs(mBackBuffer)))
    NS_RUNTIMEABORT("creating ThebesLayer 'back buffer' failed!");
  mBufferSize = aSize;

  BasicManager()->CreatedThebesBuffer(BasicManager()->Hold(this),
                                      // only |aSize| really matters
                                      // here, since Painted() soon
                                      // follows
                                      nsIntRect(nsIntPoint(0, 0), aSize),
                                      tmpFront);

  return Base::CreateBuffer(aType, aSize);
}


class BasicShadowableImageLayer : public BasicImageLayer,
                                  public BasicShadowableLayer
{
public:
  BasicShadowableImageLayer(BasicShadowLayerManager* aManager) :
    BasicImageLayer(aManager)
  {
    MOZ_COUNT_CTOR(BasicShadowableImageLayer);
  }
  virtual ~BasicShadowableImageLayer()
  {
    if (mBackSurface) {
      BasicManager()->ShadowLayerForwarder::DestroySharedSurface(mBackSurface);
    }
    MOZ_COUNT_DTOR(BasicShadowableImageLayer);
  }

  virtual void Paint(gfxContext* aContext,
                     LayerManager::DrawThebesLayerCallback aCallback,
                     void* aCallbackData,
                     float aOpacity);

  virtual void FillSpecificAttributes(SpecificLayerAttributes& aAttrs)
  {
    aAttrs = ImageLayerAttributes(mFilter);
  }

  virtual Layer* AsLayer() { return this; }
  virtual ShadowableLayer* AsShadowableLayer() { return this; }

  virtual void SetBackBuffer(gfxSharedImageSurface* aBuffer)
  {
    mBackSurface = aBuffer;
  }

private:
  BasicShadowLayerManager* BasicManager()
  {
    return static_cast<BasicShadowLayerManager*>(mManager);
  }

  nsRefPtr<gfxSharedImageSurface> mBackSurface;
};
 
void
BasicShadowableImageLayer::Paint(gfxContext* aContext,
                                 LayerManager::DrawThebesLayerCallback aCallback,
                                 void* aCallbackData,
                                 float aOpacity)
{
  gfxIntSize oldSize = mSize;
  nsRefPtr<gfxPattern> pat = GetAndPaintCurrentImage(aContext, aOpacity);
  if (!pat || !HasShadow())
    return;

  if (oldSize != mSize) {
    NS_ASSERTION(oldSize == gfxIntSize(0, 0), "video changed size?");

    nsRefPtr<gfxSharedImageSurface> tmpFrontSurface;
    // XXX error handling?
    if (!BasicManager()->AllocDoubleBuffer(
          mSize, gfxASurface::ImageFormatARGB32,
          getter_AddRefs(tmpFrontSurface), getter_AddRefs(mBackSurface)))
      NS_RUNTIMEABORT("creating ImageLayer 'front buffer' failed!");

    BasicManager()->CreatedImageBuffer(BasicManager()->Hold(this),
                                       nsIntSize(mSize.width, mSize.height),
                                       tmpFrontSurface);
  }

  nsRefPtr<gfxContext> tmpCtx = new gfxContext(mBackSurface);
  PaintContext(pat, mSize, 1.0, tmpCtx);

  BasicManager()->PaintedImage(BasicManager()->Hold(this),
                               mBackSurface);
}


class BasicShadowableColorLayer : public BasicColorLayer,
                                  public BasicShadowableLayer
{
public:
  BasicShadowableColorLayer(BasicShadowLayerManager* aManager) :
    BasicColorLayer(aManager)
  {
    MOZ_COUNT_CTOR(BasicShadowableColorLayer);
  }
  virtual ~BasicShadowableColorLayer()
  {
    MOZ_COUNT_DTOR(BasicShadowableColorLayer);
  }

  virtual void FillSpecificAttributes(SpecificLayerAttributes& aAttrs)
  {
    aAttrs = ColorLayerAttributes(GetColor());
  }

  virtual Layer* AsLayer() { return this; }
  virtual ShadowableLayer* AsShadowableLayer() { return this; }
};

class BasicShadowableCanvasLayer : public BasicCanvasLayer,
                                   public BasicShadowableLayer
{
public:
  BasicShadowableCanvasLayer(BasicShadowLayerManager* aManager) :
    BasicCanvasLayer(aManager)
  {
    MOZ_COUNT_CTOR(BasicShadowableCanvasLayer);
  }
  virtual ~BasicShadowableCanvasLayer()
  {
    if (mBackBuffer) {
      BasicManager()->ShadowLayerForwarder::DestroySharedSurface(mBackBuffer);
    }
    MOZ_COUNT_DTOR(BasicShadowableCanvasLayer);
  }

  virtual void Initialize(const Data& aData);
  virtual void Paint(gfxContext* aContext,
                     LayerManager::DrawThebesLayerCallback aCallback,
                     void* aCallbackData,
                     float aOpacity);

  virtual void FillSpecificAttributes(SpecificLayerAttributes& aAttrs)
  {
    aAttrs = CanvasLayerAttributes(mFilter);
  }

  virtual Layer* AsLayer() { return this; }
  virtual ShadowableLayer* AsShadowableLayer() { return this; }

  virtual void SetBackBuffer(gfxSharedImageSurface* aBuffer)
  {
    mBackBuffer = aBuffer;
  }

private:
  BasicShadowLayerManager* BasicManager()
  {
    return static_cast<BasicShadowLayerManager*>(mManager);
  }

  nsRefPtr<gfxSharedImageSurface> mBackBuffer;
};

void
BasicShadowableCanvasLayer::Initialize(const Data& aData)
{
  BasicCanvasLayer::Initialize(aData);
  if (!HasShadow())
      return;

  nsRefPtr<gfxSharedImageSurface> tmpFrontBuffer;
  // XXX error handling?
  if (!BasicManager()->AllocDoubleBuffer(
        gfxIntSize(aData.mSize.width, aData.mSize.height),
        gfxASurface::ImageFormatARGB32,
        getter_AddRefs(tmpFrontBuffer), getter_AddRefs(mBackBuffer)))
    NS_RUNTIMEABORT("creating CanvasLayer back buffer failed!");

  BasicManager()->CreatedCanvasBuffer(BasicManager()->Hold(this),
                                      aData.mSize,
                                      tmpFrontBuffer);
}

void
BasicShadowableCanvasLayer::Paint(gfxContext* aContext,
                                  LayerManager::DrawThebesLayerCallback aCallback,
                                  void* aCallbackData,
                                  float aOpacity)
{
  BasicCanvasLayer::Paint(aContext, aCallback, aCallbackData, aOpacity);
  if (!HasShadow())
    return;

  // XXX this is yucky and slow.  It'd be nice to draw directly into
  // the shmem back buffer
  nsRefPtr<gfxContext> tmpCtx = new gfxContext(mBackBuffer);
  tmpCtx->DrawSurface(mSurface, gfxSize(mBounds.width, mBounds.height));

  BasicManager()->PaintedCanvas(BasicManager()->Hold(this),
                                mBackBuffer);
}

class ShadowThebesLayerBuffer : public BasicThebesLayerBuffer
{
  typedef BasicThebesLayerBuffer Base;

public:
  ShadowThebesLayerBuffer()
    : Base(NULL)
  {
    MOZ_COUNT_CTOR(ShadowThebesLayerBuffer);
  }

  ~ShadowThebesLayerBuffer()
  {
    MOZ_COUNT_DTOR(ShadowThebesLayerBuffer);
  }

  already_AddRefed<gfxSharedImageSurface>
  Swap(gfxSharedImageSurface* aNewFrontBuffer,
       const nsIntRect& aBufferRect,
       const nsIntPoint& aRotation=nsIntPoint(0, 0))
  {
    nsRefPtr<gfxASurface> newBackBuffer = SetBuffer(aNewFrontBuffer,
                                                    aBufferRect, aRotation);
    return static_cast<gfxSharedImageSurface*>(newBackBuffer.forget().get());
  }

protected:
  virtual already_AddRefed<gfxASurface>
  CreateBuffer(ContentType aType, const nsIntSize& aSize)
  {
    NS_RUNTIMEABORT("ShadowThebesLayer can't paint content");
    return nsnull;
  }
};

class BasicShadowThebesLayer : public ShadowThebesLayer, BasicImplData {
public:
  BasicShadowThebesLayer(BasicShadowLayerManager* aLayerManager) :
    ShadowThebesLayer(aLayerManager, static_cast<BasicImplData*>(this))
  {
    MOZ_COUNT_CTOR(BasicShadowThebesLayer);
  }
  virtual ~BasicShadowThebesLayer();

  virtual already_AddRefed<gfxSharedImageSurface>
  Swap(gfxSharedImageSurface* aNewFront,
       const nsIntRect& aBufferRect,
       const nsIntPoint& aRotation)
  {
    return mFrontBuffer.Swap(aNewFront, aBufferRect, aRotation);
  }

  virtual void Paint(gfxContext* aContext,
                     LayerManager::DrawThebesLayerCallback aCallback,
                     void* aCallbackData,
                     float aOpacity);

  MOZ_LAYER_DECL_NAME("BasicShadowThebesLayer", TYPE_SHADOW)

private:
  BasicShadowLayerManager* BasicManager()
  {
    return static_cast<BasicShadowLayerManager*>(mManager);
  }

  ShadowThebesLayerBuffer mFrontBuffer;
};

BasicShadowThebesLayer::~BasicShadowThebesLayer()
{
  nsRefPtr<gfxSharedImageSurface> frontBuffer =
    mFrontBuffer.Swap(0, nsIntRect());
  if (frontBuffer) {
    BasicManager()->ShadowLayerManager::DestroySharedSurface(frontBuffer);
  }

  MOZ_COUNT_DTOR(BasicShadowThebesLayer);
}

void
BasicShadowThebesLayer::Paint(gfxContext* aContext,
                              LayerManager::DrawThebesLayerCallback aCallback,
                              void* aCallbackData,
                              float aOpacity)
{
  NS_ASSERTION(BasicManager()->InDrawing(),
               "Can only draw in drawing phase");
  NS_ASSERTION(BasicManager()->IsRetained(),
               "ShadowThebesLayer makes no sense without retained mode");

  gfxContext* target = BasicManager()->GetTarget();
  NS_ASSERTION(target, "We shouldn't be called if there's no target");

  nsRefPtr<gfxASurface> targetSurface = aContext->CurrentSurface();
  PRBool isOpaqueContent =
    (targetSurface->AreSimilarSurfacesSensitiveToContentType() &&
     aOpacity == 1.0 &&
     CanUseOpaqueSurface());

  mFrontBuffer.DrawTo(this, isOpaqueContent, target, aOpacity);
}


class BasicShadowImageLayer : public ShadowImageLayer, BasicImplData {
public:
  BasicShadowImageLayer(BasicShadowLayerManager* aLayerManager) :
    ShadowImageLayer(aLayerManager, static_cast<BasicImplData*>(this))
  {
    MOZ_COUNT_CTOR(BasicShadowImageLayer);
  }
  virtual ~BasicShadowImageLayer()
  {
    if (mFrontSurface) {
      BasicManager()->ShadowLayerManager::DestroySharedSurface(mFrontSurface);
    }
    MOZ_COUNT_DTOR(BasicShadowImageLayer);
  }

  virtual PRBool Init(gfxSharedImageSurface* front, const nsIntSize& size);

  virtual already_AddRefed<gfxSharedImageSurface>
  Swap(gfxSharedImageSurface* newFront);

  virtual void Paint(gfxContext* aContext,
                     LayerManager::DrawThebesLayerCallback aCallback,
                     void* aCallbackData,
                     float aOpacity);

  MOZ_LAYER_DECL_NAME("BasicShadowImageLayer", TYPE_SHADOW)

protected:
  BasicShadowLayerManager* BasicManager()
  {
    return static_cast<BasicShadowLayerManager*>(mManager);
  }

  // XXX ShmemImage?
  nsRefPtr<gfxSharedImageSurface> mFrontSurface;
  gfxIntSize mSize;
};

PRBool
BasicShadowImageLayer::Init(gfxSharedImageSurface* front,
                            const nsIntSize& size)
{
  mFrontSurface = front;
  mSize = gfxIntSize(size.width, size.height);
  return PR_TRUE;
}

already_AddRefed<gfxSharedImageSurface>
BasicShadowImageLayer::Swap(gfxSharedImageSurface* newFront)
{
  already_AddRefed<gfxSharedImageSurface> tmp = mFrontSurface.forget();
  mFrontSurface = newFront;
  return tmp;
}

void
BasicShadowImageLayer::Paint(gfxContext* aContext,
                             LayerManager::DrawThebesLayerCallback aCallback,
                             void* aCallbackData,
                             float aOpacity)
{
  nsRefPtr<gfxPattern> pat = new gfxPattern(mFrontSurface);
  pat->SetFilter(mFilter);
  BasicImageLayer::PaintContext(pat, mSize, aOpacity, aContext);
}

class BasicShadowCanvasLayer : public ShadowCanvasLayer,
                               BasicImplData
{
public:
  BasicShadowCanvasLayer(BasicShadowLayerManager* aLayerManager) :
    ShadowCanvasLayer(aLayerManager, static_cast<BasicImplData*>(this))
  {
    MOZ_COUNT_CTOR(BasicShadowCanvasLayer);
  }
  virtual ~BasicShadowCanvasLayer()
  {
    if (mFrontSurface) {
      BasicManager()->ShadowLayerManager::DestroySharedSurface(mFrontSurface);
    }
    MOZ_COUNT_DTOR(BasicShadowCanvasLayer);
  }

  virtual void Initialize(const Data& aData);

  virtual void Updated(const nsIntRect& aRect)
  {}

  virtual already_AddRefed<gfxSharedImageSurface>
  Swap(gfxSharedImageSurface* newFront);

  virtual void Paint(gfxContext* aContext,
                     LayerManager::DrawThebesLayerCallback aCallback,
                     void* aCallbackData,
                     float aOpacity);

  MOZ_LAYER_DECL_NAME("BasicShadowCanvasLayer", TYPE_SHADOW)

private:
  BasicShadowLayerManager* BasicManager()
  {
    return static_cast<BasicShadowLayerManager*>(mManager);
  }

  nsRefPtr<gfxSharedImageSurface> mFrontSurface;
  nsIntRect mBounds;
};

void
BasicShadowCanvasLayer::Initialize(const Data& aData)
{
  NS_ASSERTION(mFrontSurface == nsnull,
               "BasicCanvasLayer::Initialize called twice!");
  NS_ASSERTION(aData.mSurface && !aData.mGLContext, "no comprende OpenGL!");

  mFrontSurface = static_cast<gfxSharedImageSurface*>(aData.mSurface);
  mBounds.SetRect(0, 0, aData.mSize.width, aData.mSize.height);
}

already_AddRefed<gfxSharedImageSurface>
BasicShadowCanvasLayer::Swap(gfxSharedImageSurface* newFront)
{
  already_AddRefed<gfxSharedImageSurface> tmp = mFrontSurface.forget();
  mFrontSurface = newFront;
  return tmp;
}

void
BasicShadowCanvasLayer::Paint(gfxContext* aContext,
                              LayerManager::DrawThebesLayerCallback aCallback,
                              void* aCallbackData,
                              float aOpacity)
{
  MOZ_LAYERS_LOG(("[ShadowLayersChild] %s()", __FUNCTION__));

  NS_ASSERTION(BasicManager()->InDrawing(),
               "Can only draw in drawing phase");

  nsRefPtr<gfxPattern> pat = new gfxPattern(mFrontSurface);

  pat->SetFilter(mFilter);
  pat->SetExtend(gfxPattern::EXTEND_PAD);

  gfxRect r(0, 0, mBounds.width, mBounds.height);
  aContext->NewPath();
  aContext->PixelSnappedRectangleAndSetPattern(r, pat);
  aContext->Fill();
}

// Create a shadow layer (PLayerChild) for aLayer, if we're forwarding
// our layer tree to a parent process.  Record the new layer creation
// in the current open transaction as a side effect.
template<typename CreatedMethod>
static void
MaybeCreateShadowFor(BasicShadowableLayer* aLayer,
                     BasicShadowLayerManager* aMgr,
                     CreatedMethod aMethod)
{
  if (!aMgr->HasShadowManager()) {
    return;
  }

  PLayerChild* shadow = aMgr->ConstructShadowFor(aLayer);
  // XXX error handling
  NS_ABORT_IF_FALSE(shadow, "failed to create shadow");

  aLayer->SetShadow(shadow);
  (aMgr->*aMethod)(aLayer);
}
#define MAYBE_CREATE_SHADOW(_type)                                      \
  MaybeCreateShadowFor(layer, this,                                     \
                       &ShadowLayerForwarder::Created ## _type ## Layer)

already_AddRefed<ThebesLayer>
BasicShadowLayerManager::CreateThebesLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<BasicShadowableThebesLayer> layer =
    new BasicShadowableThebesLayer(this);
  MAYBE_CREATE_SHADOW(Thebes);
  return layer.forget();
}

already_AddRefed<ContainerLayer>
BasicShadowLayerManager::CreateContainerLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<BasicShadowableContainerLayer> layer =
    new BasicShadowableContainerLayer(this);
  MAYBE_CREATE_SHADOW(Container);
  return layer.forget();
}

already_AddRefed<ImageLayer>
BasicShadowLayerManager::CreateImageLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<BasicShadowableImageLayer> layer =
    new BasicShadowableImageLayer(this);
  MAYBE_CREATE_SHADOW(Image);
  return layer.forget();
}

already_AddRefed<ColorLayer>
BasicShadowLayerManager::CreateColorLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<BasicShadowableColorLayer> layer =
    new BasicShadowableColorLayer(this);
  MAYBE_CREATE_SHADOW(Color);
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
already_AddRefed<ShadowThebesLayer>
BasicShadowLayerManager::CreateShadowThebesLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<ShadowThebesLayer> layer = new BasicShadowThebesLayer(this);
  return layer.forget();
}

already_AddRefed<ShadowImageLayer>
BasicShadowLayerManager::CreateShadowImageLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<ShadowImageLayer> layer = new BasicShadowImageLayer(this);
  return layer.forget();
}

already_AddRefed<ShadowCanvasLayer>
BasicShadowLayerManager::CreateShadowCanvasLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<ShadowCanvasLayer> layer = new BasicShadowCanvasLayer(this);
  return layer.forget();
}

BasicShadowLayerManager::BasicShadowLayerManager(nsIWidget* aWidget) :
  BasicLayerManager(aWidget)
{
  MOZ_COUNT_CTOR(BasicShadowLayerManager);
}

BasicShadowLayerManager::~BasicShadowLayerManager()
{
  // FIXME/bug 570294: shadow forwarders don't have __delete__ until
  // they have manager protocols
  //
  //if (HasShadowManager())
  //  PLayersChild::Send__delete__(mShadow);
  MOZ_COUNT_DTOR(BasicShadowLayerManager);
}

void
BasicShadowLayerManager::SetRoot(Layer* aLayer)
{
  if (mRoot != aLayer) {
    if (HasShadowManager()) {
      ShadowLayerForwarder::SetRoot(Hold(aLayer));
    }
    BasicLayerManager::SetRoot(aLayer);
  }
}

void
BasicShadowLayerManager::Mutated(Layer* aLayer)
{
  NS_ASSERTION(InConstruction() || InDrawing(), "wrong phase");
  if (HasShadowManager()) {
    ShadowLayerForwarder::Mutated(Hold(aLayer));
  }
}

void
BasicShadowLayerManager::BeginTransactionWithTarget(gfxContext* aTarget)
{
  NS_ABORT_IF_FALSE(mKeepAlive.IsEmpty(), "uncommitted txn?");
  if (HasShadowManager()) {
    ShadowLayerForwarder::BeginTransaction();
  }
  BasicLayerManager::BeginTransactionWithTarget(aTarget);
}

void
BasicShadowLayerManager::EndTransaction(DrawThebesLayerCallback aCallback,
                                        void* aCallbackData)
{
  BasicLayerManager::EndTransaction(aCallback, aCallbackData);
#ifdef DEBUG
  mPhase = PHASE_FORWARD;
#endif

  // forward this transaction's changeset to our ShadowLayerManager
  nsAutoTArray<EditReply, 10> replies;
  if (HasShadowManager() && ShadowLayerForwarder::EndTransaction(&replies)) {
    for (nsTArray<EditReply>::size_type i = 0; i < replies.Length(); ++i) {
      const EditReply& reply = replies[i];

      switch (reply.type()) {
      case EditReply::TOpBufferSwap: {
        MOZ_LAYERS_LOG(("[LayersForwarder] BufferSwap"));

        const OpBufferSwap& obs = reply.get_OpBufferSwap();
        GetBasicShadowable(obs)->SetBackBuffer(
          new gfxSharedImageSurface(obs.newBackBuffer()));
        break;
      }

      default:
        NS_RUNTIMEABORT("not reached");
      }
    }
  } else if (HasShadowManager()) {
    NS_WARNING("failed to forward Layers transaction");
  }

  // this may result in Layers being deleted, which results in
  // PLayer::Send__delete__() and DeallocShmem()
  mKeepAlive.Clear();

#ifdef DEBUG
  mPhase = PHASE_NONE;
#endif
}

ShadowableLayer*
BasicShadowLayerManager::Hold(Layer* aLayer)
{
  NS_ABORT_IF_FALSE(HasShadowManager(),
                    "top-level tree, no shadow tree to remote to");

  ShadowableLayer* shadowable = ToShadowable(aLayer);
  NS_ABORT_IF_FALSE(shadowable, "trying to remote an unshadowable layer");

  mKeepAlive.AppendElement(aLayer);
  return shadowable;
}
#endif  // MOZ_IPC

}
}
