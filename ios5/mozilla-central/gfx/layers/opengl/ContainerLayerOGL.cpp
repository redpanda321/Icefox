/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ContainerLayerOGL.h"
#include "gfxUtils.h"

namespace mozilla {
namespace layers {

template<class Container>
static void
ContainerInsertAfter(Container* aContainer, Layer* aChild, Layer* aAfter)
{
  aChild->SetParent(aContainer);
  if (!aAfter) {
    Layer *oldFirstChild = aContainer->GetFirstChild();
    aContainer->mFirstChild = aChild;
    aChild->SetNextSibling(oldFirstChild);
    aChild->SetPrevSibling(nsnull);
    if (oldFirstChild) {
      oldFirstChild->SetPrevSibling(aChild);
    } else {
      aContainer->mLastChild = aChild;
    }
    NS_ADDREF(aChild);
    aContainer->DidInsertChild(aChild);
    return;
  }
  for (Layer *child = aContainer->GetFirstChild(); 
       child; child = child->GetNextSibling()) {
    if (aAfter == child) {
      Layer *oldNextSibling = child->GetNextSibling();
      child->SetNextSibling(aChild);
      aChild->SetNextSibling(oldNextSibling);
      if (oldNextSibling) {
        oldNextSibling->SetPrevSibling(aChild);
      } else {
        aContainer->mLastChild = aChild;
      }
      aChild->SetPrevSibling(child);
      NS_ADDREF(aChild);
      aContainer->DidInsertChild(aChild);
      return;
    }
  }
  NS_WARNING("Failed to find aAfter layer!");
}

template<class Container>
static void
ContainerRemoveChild(Container* aContainer, Layer* aChild)
{
  if (aContainer->GetFirstChild() == aChild) {
    aContainer->mFirstChild = aContainer->GetFirstChild()->GetNextSibling();
    if (aContainer->mFirstChild) {
      aContainer->mFirstChild->SetPrevSibling(nsnull);
    } else {
      aContainer->mLastChild = nsnull;
    }
    aChild->SetNextSibling(nsnull);
    aChild->SetPrevSibling(nsnull);
    aChild->SetParent(nsnull);
    aContainer->DidRemoveChild(aChild);
    NS_RELEASE(aChild);
    return;
  }
  Layer *lastChild = nsnull;
  for (Layer *child = aContainer->GetFirstChild(); child; 
       child = child->GetNextSibling()) {
    if (child == aChild) {
      // We're sure this is not our first child. So lastChild != NULL.
      lastChild->SetNextSibling(child->GetNextSibling());
      if (child->GetNextSibling()) {
        child->GetNextSibling()->SetPrevSibling(lastChild);
      } else {
        aContainer->mLastChild = lastChild;
      }
      child->SetNextSibling(nsnull);
      child->SetPrevSibling(nsnull);
      child->SetParent(nsnull);
      aContainer->DidRemoveChild(aChild);
      NS_RELEASE(aChild);
      return;
    }
    lastChild = child;
  }
}

template<class Container>
static void
ContainerDestroy(Container* aContainer)
 {
  if (!aContainer->mDestroyed) {
    while (aContainer->mFirstChild) {
      aContainer->GetFirstChildOGL()->Destroy();
      aContainer->RemoveChild(aContainer->mFirstChild);
    }
    aContainer->mDestroyed = true;
  }
}

template<class Container>
static void
ContainerCleanupResources(Container* aContainer)
{
  for (Layer* l = aContainer->GetFirstChild(); l; l = l->GetNextSibling()) {
    LayerOGL* layerToRender = static_cast<LayerOGL*>(l->ImplData());
    layerToRender->CleanupResources();
  }
}

static inline LayerOGL*
GetNextSibling(LayerOGL* aLayer)
{
   Layer* layer = aLayer->GetLayer()->GetNextSibling();
   return layer ? static_cast<LayerOGL*>(layer->
                                         ImplData())
                 : nsnull;
}

static bool
HasOpaqueAncestorLayer(Layer* aLayer)
{
  for (Layer* l = aLayer->GetParent(); l; l = l->GetParent()) {
    if (l->GetContentFlags() & Layer::CONTENT_OPAQUE)
      return true;
  }
  return false;
}

template<class Container>
static void
ContainerRender(Container* aContainer,
                int aPreviousFrameBuffer,
                const nsIntPoint& aOffset,
                LayerManagerOGL* aManager)
{
  /**
   * Setup our temporary texture for rendering the contents of this container.
   */
  GLuint containerSurface;
  GLuint frameBuffer;

  nsIntPoint childOffset(aOffset);
  nsIntRect visibleRect = aContainer->GetEffectiveVisibleRegion().GetBounds();

  nsIntRect cachedScissor = aContainer->gl()->ScissorRect();
  aContainer->gl()->PushScissorRect();
  aContainer->mSupportsComponentAlphaChildren = false;

  float opacity = aContainer->GetEffectiveOpacity();
  const gfx3DMatrix& transform = aContainer->GetEffectiveTransform();
  bool needsFramebuffer = aContainer->UseIntermediateSurface();
  if (needsFramebuffer) {
    LayerManagerOGL::InitMode mode = LayerManagerOGL::InitModeClear;
    nsIntRect framebufferRect = visibleRect;
    if (aContainer->GetEffectiveVisibleRegion().GetNumRects() == 1 && 
        (aContainer->GetContentFlags() & Layer::CONTENT_OPAQUE))
    {
      // don't need a background, we're going to paint all opaque stuff
      aContainer->mSupportsComponentAlphaChildren = true;
      mode = LayerManagerOGL::InitModeNone;
    } else {
      const gfx3DMatrix& transform3D = aContainer->GetEffectiveTransform();
      gfxMatrix transform;
      // If we have an opaque ancestor layer, then we can be sure that
      // all the pixels we draw into are either opaque already or will be
      // covered by something opaque. Otherwise copying up the background is
      // not safe.
      if (HasOpaqueAncestorLayer(aContainer) &&
          transform3D.Is2D(&transform) && !transform.HasNonIntegerTranslation()) {
        mode = LayerManagerOGL::InitModeCopy;
        framebufferRect.x += transform.x0;
        framebufferRect.y += transform.y0;
        aContainer->mSupportsComponentAlphaChildren = true;
      }
    }

    aContainer->gl()->PushViewportRect();
    framebufferRect -= childOffset; 
    aManager->CreateFBOWithTexture(framebufferRect,
                                   mode,
                                   aPreviousFrameBuffer,
                                   &frameBuffer,
                                   &containerSurface);
    childOffset.x = visibleRect.x;
    childOffset.y = visibleRect.y;
  } else {
    frameBuffer = aPreviousFrameBuffer;
    aContainer->mSupportsComponentAlphaChildren = (aContainer->GetContentFlags() & Layer::CONTENT_OPAQUE) ||
      (aContainer->GetParent() && aContainer->GetParent()->SupportsComponentAlphaChildren());
  }

  nsAutoTArray<Layer*, 12> children;
  aContainer->SortChildrenBy3DZOrder(children);

  /**
   * Render this container's contents.
   */
  for (PRUint32 i = 0; i < children.Length(); i++) {
    LayerOGL* layerToRender = static_cast<LayerOGL*>(children.ElementAt(i)->ImplData());

    if (layerToRender->GetLayer()->GetEffectiveVisibleRegion().IsEmpty()) {
      continue;
    }

    nsIntRect scissorRect = layerToRender->GetLayer()->
        CalculateScissorRect(cachedScissor, &aManager->GetWorldTransform());
    if (scissorRect.IsEmpty()) {
      continue;
    }

    aContainer->gl()->fScissor(scissorRect.x, 
                               scissorRect.y, 
                               scissorRect.width, 
                               scissorRect.height);

    layerToRender->RenderLayer(frameBuffer, childOffset);
    aContainer->gl()->MakeCurrent();
  }


  if (needsFramebuffer) {
    // Unbind the current framebuffer and rebind the previous one.
#ifdef MOZ_DUMP_PAINTING
    if (gfxUtils::sDumpPainting) {
      nsRefPtr<gfxImageSurface> surf = 
        aContainer->gl()->GetTexImage(containerSurface, true, aManager->GetFBOLayerProgramType());

      WriteSnapshotToDumpFile(aContainer, surf);
    }
#endif
    
    // Restore the viewport
    aContainer->gl()->PopViewportRect();
    nsIntRect viewport = aContainer->gl()->ViewportRect();
    aManager->SetupPipeline(viewport.width, viewport.height,
                            LayerManagerOGL::ApplyWorldTransform);
    aContainer->gl()->PopScissorRect();

    aContainer->gl()->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, aPreviousFrameBuffer);
    aContainer->gl()->fDeleteFramebuffers(1, &frameBuffer);

    aContainer->gl()->fActiveTexture(LOCAL_GL_TEXTURE0);

    aContainer->gl()->fBindTexture(aManager->FBOTextureTarget(), containerSurface);

    MaskType maskType = MaskNone;
    if (aContainer->GetMaskLayer()) {
      if (!aContainer->GetTransform().CanDraw2D()) {
        maskType = Mask3d;
      } else {
        maskType = Mask2d;
      }
    }
    ShaderProgramOGL *rgb =
      aManager->GetFBOLayerProgram(maskType);

    rgb->Activate();
    rgb->SetLayerQuadRect(visibleRect);
    rgb->SetLayerTransform(transform);
    rgb->SetLayerOpacity(opacity);
    rgb->SetRenderOffset(aOffset);
    rgb->SetTextureUnit(0);
    rgb->LoadMask(aContainer->GetMaskLayer());

    if (rgb->GetTexCoordMultiplierUniformLocation() != -1) {
      // 2DRect case, get the multiplier right for a sampler2DRect
      rgb->SetTexCoordMultiplier(visibleRect.width, visibleRect.height);
    }

    // Drawing is always flipped, but when copying between surfaces we want to avoid
    // this. Pass true for the flip parameter to introduce a second flip
    // that cancels the other one out.
    aManager->BindAndDrawQuad(rgb, true);

    // Clean up resources.  This also unbinds the texture.
    aContainer->gl()->fDeleteTextures(1, &containerSurface);
  } else {
    aContainer->gl()->PopScissorRect();
  }
}

ContainerLayerOGL::ContainerLayerOGL(LayerManagerOGL *aManager)
  : ContainerLayer(aManager, NULL)
  , LayerOGL(aManager)
{
  mImplData = static_cast<LayerOGL*>(this);
}

ContainerLayerOGL::~ContainerLayerOGL()
{
  Destroy();
}

void
ContainerLayerOGL::InsertAfter(Layer* aChild, Layer* aAfter)
{
  ContainerInsertAfter(this, aChild, aAfter);
}

void
ContainerLayerOGL::RemoveChild(Layer *aChild)
{
  ContainerRemoveChild(this, aChild);
}

void
ContainerLayerOGL::Destroy()
{
  ContainerDestroy(this);
}

LayerOGL*
ContainerLayerOGL::GetFirstChildOGL()
{
  if (!mFirstChild) {
    return nsnull;
  }
  return static_cast<LayerOGL*>(mFirstChild->ImplData());
}

void
ContainerLayerOGL::RenderLayer(int aPreviousFrameBuffer,
                               const nsIntPoint& aOffset)
{
  ContainerRender(this, aPreviousFrameBuffer, aOffset, mOGLManager);
}

void
ContainerLayerOGL::CleanupResources()
{
  ContainerCleanupResources(this);
}

ShadowContainerLayerOGL::ShadowContainerLayerOGL(LayerManagerOGL *aManager)
  : ShadowContainerLayer(aManager, NULL)
  , LayerOGL(aManager)
{
  mImplData = static_cast<LayerOGL*>(this);
}
 
ShadowContainerLayerOGL::~ShadowContainerLayerOGL()
{
  Destroy();
}

void
ShadowContainerLayerOGL::InsertAfter(Layer* aChild, Layer* aAfter)
{
  ContainerInsertAfter(this, aChild, aAfter);
}

void
ShadowContainerLayerOGL::RemoveChild(Layer *aChild)
{
  ContainerRemoveChild(this, aChild);
}

void
ShadowContainerLayerOGL::Destroy()
{
  ContainerDestroy(this);
}

LayerOGL*
ShadowContainerLayerOGL::GetFirstChildOGL()
{
  if (!mFirstChild) {
    return nsnull;
   }
  return static_cast<LayerOGL*>(mFirstChild->ImplData());
}
 
void
ShadowContainerLayerOGL::RenderLayer(int aPreviousFrameBuffer,
                                     const nsIntPoint& aOffset)
{
  ContainerRender(this, aPreviousFrameBuffer, aOffset, mOGLManager);
}

void
ShadowContainerLayerOGL::CleanupResources()
{
  ContainerCleanupResources(this);
}

} /* layers */
} /* mozilla */
