/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=8 et :
 */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at:
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Code.
 *
 * The Initial Developer of the Original Code is
 *   The Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#ifndef mozilla_layers_ShadowLayers_h
#define mozilla_layers_ShadowLayers_h 1

#include "gfxASurface.h"

#include "ImageLayers.h"
#include "Layers.h"

class gfxSharedImageSurface;

namespace mozilla {
namespace layers {

struct Edit;
struct EditReply;
class PLayerChild;
class PLayersChild;
class PLayersParent;
class ShadowableLayer;
class ShadowThebesLayer;
class ShadowImageLayer;
class ShadowCanvasLayer;
class Transaction;

/**
 * We want to share layer trees across thread contexts and address
 * spaces for several reasons; chief among them
 *
 *  - a parent process can paint a child process's layer tree while
 *    the child process is blocked, say on content script.  This is
 *    important on mobile devices where UI responsiveness is key.
 *
 *  - a dedicated "compositor" process can asynchronously (wrt the
 *    browser process) composite and animate layer trees, allowing a
 *    form of pipeline parallelism between compositor/browser/content
 *
 *  - a dedicated "compositor" process can take all responsibility for
 *    accessing the GPU, which is desirable on systems with
 *    buggy/leaky drivers because the compositor process can die while
 *    browser and content live on (and failover mechanisms can be
 *    installed to quickly bring up a replacement compositor)
 *
 * The Layers model has a crisply defined API, which makes it easy to
 * safely "share" layer trees.  The ShadowLayers API extends Layers to
 * allow a remote, parent process to access a child process's layer
 * tree.
 *
 * ShadowLayerForwarder publishes a child context's layer tree to a
 * parent context.  This comprises recording layer-tree modifications
 * into atomic transactions and pushing them over IPC.
 *
 * ShadowLayerManager grafts layer subtrees published by child-context
 * ShadowLayerForwarder(s) into a parent-context layer tree.
 *
 * (Advanced note: because our process tree may have a height >2, a
 * non-leaf subprocess may both receive updates from child processes
 * and publish them to parent processes.  Put another way,
 * LayerManagers may be both ShadowLayerManagers and
 * ShadowLayerForwarders.)
 *
 * There are only shadow types for layers that have different shadow
 * vs. not-shadow behavior.  ColorLayers and ContainerLayers behave
 * the same way in both regimes (so far).
 */

class ShadowLayerForwarder
{
public:
  virtual ~ShadowLayerForwarder();

  /**
   * Begin recording a transaction to be forwarded atomically to a
   * ShadowLayerManager.
   */
  void BeginTransaction();

  /**
   * The following methods may only be called after BeginTransaction()
   * but before EndTransaction().  They mirror the LayerManager
   * interface in Layers.h.
   */

  /**
   * Notify the shadow manager that a new, "real" layer has been
   * created, and a corresponding shadow layer should be created in
   * the compositing process.
   */
  void CreatedThebesLayer(ShadowableLayer* aThebes);
  void CreatedContainerLayer(ShadowableLayer* aContainer);
  void CreatedImageLayer(ShadowableLayer* aImage);
  void CreatedColorLayer(ShadowableLayer* aColor);
  void CreatedCanvasLayer(ShadowableLayer* aCanvas);

  /**
   * Notify the shadow manager that a buffer has been created for the
   * specificed layer.  |aInitialFrontSurface| is one of the newly
   * created, transparent black buffers for the layer; the "real"
   * layer holds on to the other as its back buffer.  We send it
   * across on buffer creation to avoid special cases in the buffer
   * swapping logic for Painted*() operations.
   *
   * It is expected that Created*Buffer() will be followed by a
   * Painted*Buffer() in the same transaction, so that
   * |aInitialFrontBuffer| is never actually drawn to screen.
   */
  /**
   * |aBufferRect| is the screen rect covered by |aInitialFrontBuffer|.
   */
  void CreatedThebesBuffer(ShadowableLayer* aThebes,
                           nsIntRect aBufferRect,
                           gfxSharedImageSurface* aInitialFrontBuffer);
  /**
   * For the next two methods, |aSize| is the size of
   * |aInitialFrontSurface|.
   */
  void CreatedImageBuffer(ShadowableLayer* aImage,
                          nsIntSize aSize,
                          gfxSharedImageSurface* aInitialFrontSurface);
  void CreatedCanvasBuffer(ShadowableLayer* aCanvas,
                           nsIntSize aSize,
                           gfxSharedImageSurface* aInitialFrontSurface);

  /**
   * At least one attribute of |aMutant| has changed, and |aMutant|
   * needs to sync to its shadow layer.  This initial implementation
   * forwards all attributes when any is mutated.
   */
  void Mutated(ShadowableLayer* aMutant);

  void SetRoot(ShadowableLayer* aRoot);
  /**
   * Insert |aChild| after |aAfter| in |aContainer|.  |aAfter| can be
   * NULL to indicated that |aChild| should be appended to the end of
   * |aContainer|'s child list.
   */
  void InsertAfter(ShadowableLayer* aContainer,
                   ShadowableLayer* aChild,
                   ShadowableLayer* aAfter=NULL);
  void RemoveChild(ShadowableLayer* aContainer,
                   ShadowableLayer* aChild);

  /**
   * Notify the shadow manager that the specified layer's back buffer
   * has new pixels and should become the new front buffer, and be
   * re-rendered, in the compositing process.  The former front buffer
   * is swapped for |aNewFrontBuffer| and becomes the new back buffer
   * for the "real" layer.
   */
  /**
   * |aBufferRect| is the screen rect covered as a whole by the
   * possibly-toroidally-rotated |aNewFrontBuffer|.  |aBufferRotation|
   * is buffer's rotation, if any.
   */
  void PaintedThebesBuffer(ShadowableLayer* aThebes,
                           nsIntRect aBufferRect,
                           nsIntPoint aBufferRotation,
                           gfxSharedImageSurface* aNewFrontBuffer);
  /**
   * NB: this initial implementation only forwards RGBA data for
   * ImageLayers.  This is slow, and will be optimized.
   */
  void PaintedImage(ShadowableLayer* aImage,
                    gfxSharedImageSurface* aNewFrontSurface);
  void PaintedCanvas(ShadowableLayer* aCanvas,
                     gfxSharedImageSurface* aNewFrontSurface);

  /**
   * End the current transaction and forward it to ShadowLayerManager.
   * |aReplies| are directions from the ShadowLayerManager to the
   * caller of EndTransaction().
   */
  PRBool EndTransaction(nsTArray<EditReply>* aReplies);

  /**
   * True if this is forwarding to a ShadowLayerManager.
   */
  PRBool HasShadowManager() { return !!mShadowManager; }

  PRBool AllocDoubleBuffer(const gfxIntSize& aSize,
                           gfxASurface::gfxImageFormat aFormat,
                           gfxSharedImageSurface** aFrontBuffer,
                           gfxSharedImageSurface** aBackBuffer);

  void DestroySharedSurface(gfxSharedImageSurface* aSurface);

  /**
   * Construct a shadow of |aLayer| on the "other side", at the
   * ShadowLayerManager.
   */
  PLayerChild* ConstructShadowFor(ShadowableLayer* aLayer);

protected:
  ShadowLayerForwarder();

  PLayersChild* mShadowManager;

private:
  Transaction* mTxn;
};


class ShadowLayerManager : public LayerManager
{
public:
  virtual ~ShadowLayerManager() {}

  PRBool HasForwarder() { return !!mForwarder; }

  void SetForwarder(PLayersParent* aForwarder)
  {
    NS_ASSERTION(!HasForwarder(), "setting forwarder twice?");
    mForwarder = aForwarder;
  }

  void DestroySharedSurface(gfxSharedImageSurface* aSurface);

  /** CONSTRUCTION PHASE ONLY */
  virtual already_AddRefed<ShadowThebesLayer> CreateShadowThebesLayer() = 0;
  /** CONSTRUCTION PHASE ONLY */
  virtual already_AddRefed<ShadowImageLayer> CreateShadowImageLayer() = 0;
  /** CONSTRUCTION PHASE ONLY */
  virtual already_AddRefed<ShadowCanvasLayer> CreateShadowCanvasLayer() = 0;

protected:
  ShadowLayerManager() : mForwarder(NULL) {}

  PLayersParent* mForwarder;
};


/**
 * A ShadowableLayer is a Layer can be shared with a parent context
 * through a ShadowLayerForwarder.  A ShadowableLayer maps to a
 * Shadow*Layer in a parent context.
 *
 * Note that ShadowLayers can themselves be ShadowableLayers.
 */
class ShadowableLayer
{
public:
  virtual ~ShadowableLayer() {}

  virtual Layer* AsLayer() = 0;

  /**
   * True if this layer has a shadow in a parent process.
   */
  PRBool HasShadow() { return !!mShadow; }

  /**
   * Return the IPC handle to a Shadow*Layer referring to this if one
   * exists, NULL if not.
   */
  PLayerChild* GetShadow() { return mShadow; }

protected:
  ShadowableLayer() : mShadow(NULL) {}

  PLayerChild* mShadow;
};


class ShadowThebesLayer : public ThebesLayer
{
public:
  virtual void InvalidateRegion(const nsIntRegion& aRegion)
  {
    NS_RUNTIMEABORT("ShadowThebesLayers can't fill invalidated regions");
  }

  /**
   * CONSTRUCTION PHASE ONLY
   */
  void SetValidRegion(const nsIntRegion& aRegion)
  {
    mValidRegion = aRegion;
    Mutated();
  }

  /**
   * CONSTRUCTION PHASE ONLY
   *
   * Publish the remote layer's back ThebesLayerBuffer to this shadow,
   * swapping out the old front ThebesLayerBuffer (the new back buffer
   * for the remote layer).
   *
   * XXX should the receiving process blit updates from the new front
   * buffer to the previous front buffer (new back buffer) while it has
   * access to the new front buffer?  Or is it better to fill the
   * updates bits in anew on the new back buffer?
   *
   * Seems like memcpy()s from new-front to new-back would have to
   * always be no slower than any kind of fill from content, so one
   * would expect the former to win in terms of total throughput.
   * However, that puts the blit on the critical path of
   * publishing-process-blocking-on-receiving-process, so
   * responsiveness might suffer, pointing to the latter.  Experience
   * will tell!  (Maybe possible to choose between both depending on
   * size of blit vs. expense of re-fill?)
   */
  virtual already_AddRefed<gfxSharedImageSurface>
  Swap(gfxSharedImageSurface* aNewFront,
       const nsIntRect& aBufferRect,
       const nsIntPoint& aRotation) = 0;

  MOZ_LAYER_DECL_NAME("ShadowThebesLayer", TYPE_SHADOW)

protected:
  ShadowThebesLayer(LayerManager* aManager, void* aImplData) :
    ThebesLayer(aManager, aImplData) {}
};


class ShadowCanvasLayer : public CanvasLayer
{
public:
  /**
   * CONSTRUCTION PHASE ONLY
   *
   * Publish the remote layer's back surface to this shadow, swapping
   * out the old front surface (the new back surface for the remote
   * layer).
   */
  virtual already_AddRefed<gfxSharedImageSurface>
  Swap(gfxSharedImageSurface* aNewFront) = 0;

  MOZ_LAYER_DECL_NAME("ShadowCanvasLayer", TYPE_SHADOW)

protected:
  ShadowCanvasLayer(LayerManager* aManager, void* aImplData) :
    CanvasLayer(aManager, aImplData) {}
};


class ShadowImageLayer : public ImageLayer
{
public:
  /**
   * CONSTRUCTION PHASE ONLY
   *
   * Initialize this with a (temporary) front surface with the given
   * size.  This is expected to be followed with a Swap() in the same
   * transaction to bring in real pixels.  Init() may only be called
   * once.
   */
  virtual PRBool Init(gfxSharedImageSurface* aFront, const nsIntSize& aSize) = 0;

  /**
   * CONSTRUCTION PHASE ONLY
   * @see ShadowCanvasLayer::Swap
   */
  virtual already_AddRefed<gfxSharedImageSurface>
  Swap(gfxSharedImageSurface* newFront) = 0;

  MOZ_LAYER_DECL_NAME("ShadowImageLayer", TYPE_SHADOW)

protected:
  ShadowImageLayer(LayerManager* aManager, void* aImplData) :
    ImageLayer(aManager, aImplData) {}
};


} // namespace layers
} // namespace mozilla

#endif // ifndef mozilla_layers_ShadowLayers_h
