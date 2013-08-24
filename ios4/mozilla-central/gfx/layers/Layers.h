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

#ifndef GFX_LAYERS_H
#define GFX_LAYERS_H

#include "gfxTypes.h"
#include "gfxASurface.h"
#include "nsRegion.h"
#include "nsPoint.h"
#include "nsRect.h"
#include "nsISupportsImpl.h"
#include "nsAutoPtr.h"
#include "gfx3DMatrix.h"
#include "gfxColor.h"
#include "gfxPattern.h"

#if defined(DEBUG) || defined(PR_LOGGING)
#  include <stdio.h>            // FILE
#  include "prlog.h"
#  define MOZ_LAYERS_HAVE_LOG
#  define MOZ_LAYERS_LOG(_args)                             \
  PR_LOG(LayerManager::GetLog(), PR_LOG_DEBUG, _args)
#else
struct PRLogModuleInfo;
#  define MOZ_LAYERS_LOG(_args)
#endif  // if defined(DEBUG) || defined(PR_LOGGING)

class gfxContext;
class nsPaintEvent;

namespace mozilla {
namespace gl {
class GLContext;
}

namespace layers {

class Layer;
class ThebesLayer;
class ContainerLayer;
class ImageLayer;
class ColorLayer;
class ImageContainer;
class CanvasLayer;
class SpecificLayerAttributes;

#define MOZ_LAYER_DECL_NAME(n, e)                           \
  virtual const char* Name() const { return n; }            \
  virtual LayerType GetType() const { return e; }

/*
 * Motivation: For truly smooth animation and video playback, we need to
 * be able to compose frames and render them on a dedicated thread (i.e.
 * off the main thread where DOM manipulation, script execution and layout
 * induce difficult-to-bound latency). This requires Gecko to construct
 * some kind of persistent scene structure (graph or tree) that can be
 * safely transmitted across threads. We have other scenarios (e.g. mobile 
 * browsing) where retaining some rendered data between paints is desired
 * for performance, so again we need a retained scene structure.
 * 
 * Our retained scene structure is a layer tree. Each layer represents
 * content which can be composited onto a destination surface; the root
 * layer is usually composited into a window, and non-root layers are
 * composited into their parent layers. Layers have attributes (e.g.
 * opacity and clipping) that influence their compositing.
 * 
 * We want to support a variety of layer implementations, including
 * a simple "immediate mode" implementation that doesn't retain any
 * rendered data between paints (i.e. uses cairo in just the way that
 * Gecko used it before layers were introduced). But we also don't want
 * to have bifurcated "layers"/"non-layers" rendering paths in Gecko.
 * Therefore the layers API is carefully designed to permit maximally
 * efficient implementation in an "immediate mode" style. See the
 * BasicLayerManager for such an implementation.
 */

/**
 * A LayerManager controls a tree of layers. All layers in the tree
 * must use the same LayerManager.
 * 
 * All modifications to a layer tree must happen inside a transaction.
 * Only the state of the layer tree at the end of a transaction is
 * rendered. Transactions cannot be nested
 * 
 * Each transaction has two phases:
 * 1) Construction: layers are created, inserted, removed and have
 * properties set on them in this phase.
 * BeginTransaction and BeginTransactionWithTarget start a transaction in
 * the Construction phase. When the client has finished constructing the layer
 * tree, it should call EndConstruction() to enter the drawing phase.
 * 2) Drawing: ThebesLayers are rendered into in this phase, in tree
 * order. When the client has finished drawing into the ThebesLayers, it should
 * call EndTransaction to complete the transaction.
 * 
 * All layer API calls happen on the main thread.
 * 
 * Layers are refcounted. The layer manager holds a reference to the
 * root layer, and each container layer holds a reference to its children.
 */
class THEBES_API LayerManager {
  NS_INLINE_DECL_REFCOUNTING(LayerManager)

public:
  enum LayersBackend {
    LAYERS_BASIC = 0,
    LAYERS_OPENGL,
    LAYERS_D3D9
  };

  LayerManager() : mUserData(nsnull), mDestroyed(PR_FALSE)
  {
    InitLog();
  }
  virtual ~LayerManager() {}

  /**
   * Release layers and resources held by this layer manager, and mark
   * it as destroyed.  Should do any cleanup necessary in preparation
   * for its widget going away.  After this call, only user data calls
   * are valid on the layer manager.
   */
  virtual void Destroy() { mDestroyed = PR_TRUE; }
  PRBool IsDestroyed() { return mDestroyed; }

  /**
   * Start a new transaction. Nested transactions are not allowed so
   * there must be no transaction currently in progress.
   * This transaction will update the state of the window from which
   * this LayerManager was obtained.
   */
  virtual void BeginTransaction() = 0;
  /**
   * Start a new transaction. Nested transactions are not allowed so
   * there must be no transaction currently in progress. 
   * This transaction will render the contents of the layer tree to
   * the given target context. The rendering will be complete when
   * EndTransaction returns.
   */
  virtual void BeginTransactionWithTarget(gfxContext* aTarget) = 0;
  /**
   * Function called to draw the contents of each ThebesLayer.
   * aRegionToDraw contains the region that needs to be drawn.
   * This would normally be a subregion of the visible region.
   * The callee must draw all of aRegionToDraw. Drawing outside
   * aRegionToDraw will be clipped out or ignored.
   * The callee must draw all of aRegionToDraw.
   * This region is relative to 0,0 in the ThebesLayer.
   * 
   * aRegionToInvalidate contains a region whose contents have been
   * changed by the layer manager and which must therefore be invalidated.
   * For example, this could be non-empty if a retained layer internally
   * switches from RGBA to RGB or back ... we might want to repaint it to
   * consistently use subpixel-AA or not.
   * This region is relative to 0,0 in the ThebesLayer.
   * aRegionToInvalidate may contain areas that are outside
   * aRegionToDraw; the callee must ensure that these areas are repainted
   * in the current layer manager transaction or in a later layer
   * manager transaction.
   * 
   * aContext must not be used after the call has returned.
   * We guarantee that buffered contents in the visible
   * region are valid once drawing is complete.
   * 
   * The origin of aContext is 0,0 in the ThebesLayer.
   */
  typedef void (* DrawThebesLayerCallback)(ThebesLayer* aLayer,
                                           gfxContext* aContext,
                                           const nsIntRegion& aRegionToDraw,
                                           const nsIntRegion& aRegionToInvalidate,
                                           void* aCallbackData);
  /**
   * Finish the construction phase of the transaction, perform the
   * drawing phase, and end the transaction.
   * During the drawing phase, all ThebesLayers in the tree are
   * drawn in tree order, exactly once each, except for those layers
   * where it is known that the visible region is empty.
   */
  virtual void EndTransaction(DrawThebesLayerCallback aCallback,
                              void* aCallbackData) = 0;

  /**
   * CONSTRUCTION PHASE ONLY
   * Set the root layer.
   */
  virtual void SetRoot(Layer* aLayer) = 0;
  /**
   * Can be called anytime
   */
  Layer* GetRoot() { return mRoot; }

  /**
   * CONSTRUCTION PHASE ONLY
   * Called when a managee has mutated.
   */
  virtual void Mutated(Layer* aLayer) { }

  /**
   * CONSTRUCTION PHASE ONLY
   * Create a ThebesLayer for this manager's layer tree.
   */
  virtual already_AddRefed<ThebesLayer> CreateThebesLayer() = 0;
  /**
   * CONSTRUCTION PHASE ONLY
   * Create a ContainerLayer for this manager's layer tree.
   */
  virtual already_AddRefed<ContainerLayer> CreateContainerLayer() = 0;
  /**
   * CONSTRUCTION PHASE ONLY
   * Create an ImageLayer for this manager's layer tree.
   */
  virtual already_AddRefed<ImageLayer> CreateImageLayer() = 0;
  /**
   * CONSTRUCTION PHASE ONLY
   * Create a ColorLayer for this manager's layer tree.
   */
  virtual already_AddRefed<ColorLayer> CreateColorLayer() = 0;
  /**
   * CONSTRUCTION PHASE ONLY
   * Create a CanvasLayer for this manager's layer tree.
   */
  virtual already_AddRefed<CanvasLayer> CreateCanvasLayer() = 0;

  /**
   * Can be called anytime
   */
  virtual already_AddRefed<ImageContainer> CreateImageContainer() = 0;

  /**
   * Type of layer manager his is. This is to be used sparsely in order to
   * avoid a lot of Layers backend specific code. It should be used only when
   * Layers backend specific functionality is necessary.
   */
  virtual LayersBackend GetBackendType() = 0;

  // This setter and getter can be used anytime. The user data is initially
  // null.
  void SetUserData(void* aData) { mUserData = aData; }
  void* GetUserData() { return mUserData; }

  // We always declare the following logging symbols, because it's
  // extremely tricky to conditionally declare them.  However, for
  // ifndef MOZ_LAYERS_HAVE_LOG builds, they only have trivial
  // definitions in Layers.cpp.
  virtual const char* Name() const { return "???"; }

  /**
   * Dump information about this layer manager and its managed tree to
   * aFile, which defaults to stderr.
   */
  void Dump(FILE* aFile=NULL, const char* aPrefix="");
  /**
   * Dump information about just this layer manager itself to aFile,
   * which defaults to stderr.
   */
  void DumpSelf(FILE* aFile=NULL, const char* aPrefix="");

  /**
   * Log information about this layer manager and its managed tree to
   * the NSPR log (if enabled for "Layers").
   */
  void Log(const char* aPrefix="");
  /**
   * Log information about just this layer manager itself to the NSPR
   * log (if enabled for "Layers").
   */
  void LogSelf(const char* aPrefix="");

  static bool IsLogEnabled();
  static PRLogModuleInfo* GetLog() { return sLog; }

protected:
  nsRefPtr<Layer> mRoot;
  void* mUserData;
  PRPackedBool mDestroyed;

  // Print interesting information about this into aTo.  Internally
  // used to implement Dump*() and Log*().
  virtual nsACString& PrintInfo(nsACString& aTo, const char* aPrefix);

  static void InitLog();
  static PRLogModuleInfo* sLog;
};

class ThebesLayer;

/**
 * A Layer represents anything that can be rendered onto a destination
 * surface.
 */
class THEBES_API Layer {
  NS_INLINE_DECL_REFCOUNTING(Layer)  

public:
  enum LayerType {
    TYPE_THEBES,
    TYPE_CONTAINER,
    TYPE_IMAGE,
    TYPE_COLOR,
    TYPE_CANVAS,
    TYPE_SHADOW
  };

  virtual ~Layer() {}

  /**
   * Returns the LayerManager this Layer belongs to. Note that the layer
   * manager might be in a destroyed state, at which point it's only
   * valid to set/get user data from it.
   */
  LayerManager* Manager() { return mManager; }

  /**
   * CONSTRUCTION PHASE ONLY
   * If this is called with aOpaque set to true, the caller is promising
   * that by the end of this transaction the entire visible region
   * (as specified by SetVisibleRegion) will be filled with opaque
   * content. This enables some internal quality and performance
   * optimizations.
   */
  void SetIsOpaqueContent(PRBool aOpaque)
  {
    mIsOpaqueContent = aOpaque;
    Mutated();
  }
  /**
   * CONSTRUCTION PHASE ONLY
   * Tell this layer which region will be visible. It is the responsibility
   * of the caller to ensure that content outside this region does not
   * contribute to the final visible window. This can be an
   * overapproximation to the true visible region.
   */
  virtual void SetVisibleRegion(const nsIntRegion& aRegion)
  {
    mVisibleRegion = aRegion;
    Mutated();
  }

  /**
   * CONSTRUCTION PHASE ONLY
   * Set the opacity which will be applied to this layer as it
   * is composited to the destination.
   */
  void SetOpacity(float aOpacity)
  {
    mOpacity = aOpacity;
    Mutated();
  }

  /**
   * CONSTRUCTION PHASE ONLY
   * Set a clip rect which will be applied to this layer as it is
   * composited to the destination. The coordinates are relative to
   * the parent layer (i.e. the contents of this layer
   * are transformed before this clip rect is applied).
   * For the root layer, the coordinates are relative to the widget,
   * in device pixels.
   * If aRect is null no clipping will be performed. 
   */
  void SetClipRect(const nsIntRect* aRect)
  {
    mUseClipRect = aRect != nsnull;
    if (aRect) {
      mClipRect = *aRect;
    }
    Mutated();
  }

  /**
   * CONSTRUCTION PHASE ONLY
   * Set a clip rect which will be applied to this layer as it is
   * composited to the destination. The coordinates are relative to
   * the parent layer (i.e. the contents of this layer
   * are transformed before this clip rect is applied).
   * For the root layer, the coordinates are relative to the widget,
   * in device pixels.
   * The provided rect is intersected with any existing clip rect.
   */
  void IntersectClipRect(const nsIntRect& aRect)
  {
    if (mUseClipRect) {
      mClipRect.IntersectRect(mClipRect, aRect);
    } else {
      mUseClipRect = PR_TRUE;
      mClipRect = aRect;
    }
    Mutated();
  }

  /**
   * CONSTRUCTION PHASE ONLY
   * Tell this layer what its transform should be. The transformation
   * is applied when compositing the layer into its parent container.
   * XXX Currently only transformations corresponding to 2D affine transforms
   * are supported.
   */
  void SetTransform(const gfx3DMatrix& aMatrix)
  {
    mTransform = aMatrix;
    Mutated();
  }

  // These getters can be used anytime.
  float GetOpacity() { return mOpacity; }
  const nsIntRect* GetClipRect() { return mUseClipRect ? &mClipRect : nsnull; }
  PRBool IsOpaqueContent() { return mIsOpaqueContent; }
  const nsIntRegion& GetVisibleRegion() { return mVisibleRegion; }
  ContainerLayer* GetParent() { return mParent; }
  Layer* GetNextSibling() { return mNextSibling; }
  Layer* GetPrevSibling() { return mPrevSibling; }
  virtual Layer* GetFirstChild() { return nsnull; }
  const gfx3DMatrix& GetTransform() { return mTransform; }

  /**
   * DRAWING PHASE ONLY
   *
   * Write layer-subtype-specific attributes into aAttrs.  Used to
   * synchronize layer attributes to their shadows'.
   */
  virtual void FillSpecificAttributes(SpecificLayerAttributes& aAttrs) { }

  // Returns true if it's OK to save the contents of aLayer in an
  // opaque surface (a surface without an alpha channel).
  // If we can use a surface without an alpha channel, we should, because
  // it will often make painting of antialiased text faster and higher
  // quality.
  PRBool CanUseOpaqueSurface();

  // This setter and getter can be used anytime. The user data is initially
  // null.
  void SetUserData(void* aData) { mUserData = aData; }
  void* GetUserData() { return mUserData; }

  /**
   * Dynamic downcast to a Thebes layer. Returns null if this is not
   * a ThebesLayer.
   */
  virtual ThebesLayer* AsThebesLayer() { return nsnull; }

  virtual const char* Name() const =0;
  virtual LayerType GetType() const =0;

  /**
   * Only the implementation should call this. This is per-implementation
   * private data. Normally, all layers with a given layer manager
   * use the same type of ImplData.
   */
  void* ImplData() { return mImplData; }

  /**
   * Only the implementation should use these methods.
   */
  void SetParent(ContainerLayer* aParent) { mParent = aParent; }
  void SetNextSibling(Layer* aSibling) { mNextSibling = aSibling; }
  void SetPrevSibling(Layer* aSibling) { mPrevSibling = aSibling; }

  /**
   * Dump information about this layer manager and its managed tree to
   * aFile, which defaults to stderr.
   */
  void Dump(FILE* aFile=NULL, const char* aPrefix="");
  /**
   * Dump information about just this layer manager itself to aFile,
   * which defaults to stderr.
   */
  void DumpSelf(FILE* aFile=NULL, const char* aPrefix="");

  /**
   * Log information about this layer manager and its managed tree to
   * the NSPR log (if enabled for "Layers").
   */
  void Log(const char* aPrefix="");
  /**
   * Log information about just this layer manager itself to the NSPR
   * log (if enabled for "Layers").
   */
  void LogSelf(const char* aPrefix="");

  static bool IsLogEnabled() { return LayerManager::IsLogEnabled(); }

protected:
  Layer(LayerManager* aManager, void* aImplData) :
    mManager(aManager),
    mParent(nsnull),
    mNextSibling(nsnull),
    mPrevSibling(nsnull),
    mImplData(aImplData),
    mUserData(nsnull),
    mOpacity(1.0),
    mUseClipRect(PR_FALSE),
    mIsOpaqueContent(PR_FALSE)
    {}

  void Mutated() { mManager->Mutated(this); }

  // Print interesting information about this into aTo.  Internally
  // used to implement Dump*() and Log*().  If subclasses have
  // additional interesting properties, they should override this with
  // an implementation that first calls the base implementation then
  // appends additional info to aTo.
  virtual nsACString& PrintInfo(nsACString& aTo, const char* aPrefix);

  LayerManager* mManager;
  ContainerLayer* mParent;
  Layer* mNextSibling;
  Layer* mPrevSibling;
  void* mImplData;
  void* mUserData;
  nsIntRegion mVisibleRegion;
  gfx3DMatrix mTransform;
  float mOpacity;
  nsIntRect mClipRect;
  PRPackedBool mUseClipRect;
  PRPackedBool mIsOpaqueContent;
};

/**
 * A Layer which we can draw into using Thebes. It is a conceptually
 * infinite surface, but each ThebesLayer has an associated "valid region"
 * of contents that it is currently storing, which is finite. ThebesLayer
 * implementations can store content between paints.
 * 
 * ThebesLayers are rendered into during the drawing phase of a transaction.
 *
 * Currently the contents of a ThebesLayer are in the device output color
 * space.
 */
class THEBES_API ThebesLayer : public Layer {
public:
  /**
   * CONSTRUCTION PHASE ONLY
   * Tell this layer that the content in some region has changed and
   * will need to be repainted. This area is removed from the valid
   * region.
   */
  virtual void InvalidateRegion(const nsIntRegion& aRegion) = 0;

  /**
   * Can be used anytime
   */
  const nsIntRegion& GetValidRegion() { return mValidRegion; }

  virtual ThebesLayer* AsThebesLayer() { return this; }

  MOZ_LAYER_DECL_NAME("ThebesLayer", TYPE_THEBES)

protected:
  ThebesLayer(LayerManager* aManager, void* aImplData)
    : Layer(aManager, aImplData) {}

  virtual nsACString& PrintInfo(nsACString& aTo, const char* aPrefix);

  nsIntRegion mValidRegion;
};

/**
 * A Layer which other layers render into. It holds references to its
 * children.
 */
class THEBES_API ContainerLayer : public Layer {
public:
  /**
   * CONSTRUCTION PHASE ONLY
   * Insert aChild into the child list of this container. aChild must
   * not be currently in any child list or the root for the layer manager.
   * If aAfter is non-null, it must be a child of this container and
   * we insert after that layer. If it's null we insert at the start.
   */
  virtual void InsertAfter(Layer* aChild, Layer* aAfter) = 0;
  /**
   * CONSTRUCTION PHASE ONLY
   * Remove aChild from the child list of this container. aChild must
   * be a child of this container.
   */
  virtual void RemoveChild(Layer* aChild) = 0;

  // This getter can be used anytime.
  virtual Layer* GetFirstChild() { return mFirstChild; }

  MOZ_LAYER_DECL_NAME("ContainerLayer", TYPE_CONTAINER)

protected:
  ContainerLayer(LayerManager* aManager, void* aImplData)
    : Layer(aManager, aImplData),
      mFirstChild(nsnull)
  {}

  Layer* mFirstChild;
};

/**
 * A Layer which just renders a solid color in its visible region. It actually
 * can fill any area that contains the visible region, so if you need to
 * restrict the area filled, set a clip region on this layer.
 */
class THEBES_API ColorLayer : public Layer {
public:
  /**
   * CONSTRUCTION PHASE ONLY
   * Set the color of the layer.
   */
  virtual void SetColor(const gfxRGBA& aColor)
  {
    mColor = aColor;
  }

  // This getter can be used anytime.
  virtual const gfxRGBA& GetColor() { return mColor; }

  MOZ_LAYER_DECL_NAME("ColorLayer", TYPE_COLOR)

protected:
  ColorLayer(LayerManager* aManager, void* aImplData)
    : Layer(aManager, aImplData),
      mColor(0.0, 0.0, 0.0, 0.0)
  {}

  virtual nsACString& PrintInfo(nsACString& aTo, const char* aPrefix);

  gfxRGBA mColor;
};

/**
 * A Layer for HTML Canvas elements.  It's backed by either a
 * gfxASurface or a GLContext (for WebGL layers), and has some control
 * for intelligent updating from the source if necessary (for example,
 * if hardware compositing is not available, for reading from the GL
 * buffer into an image surface that we can layer composite.)
 *
 * After Initialize is called, the underlying canvas Surface/GLContext
 * must not be modified during a layer transaction.
 */
class THEBES_API CanvasLayer : public Layer {
public:
  struct Data {
    Data()
      : mSurface(nsnull), mGLContext(nsnull),
        mGLBufferIsPremultiplied(PR_FALSE)
    { }

    /* One of these two must be specified, but never both */
    gfxASurface* mSurface;  // a gfx Surface for the canvas contents
    mozilla::gl::GLContext* mGLContext; // a GL PBuffer Context

    /* The size of the canvas content */
    nsIntSize mSize;

    /* Whether the GLContext contains premultiplied alpha
     * values in the framebuffer or not.  Defaults to FALSE.
     */
    PRPackedBool mGLBufferIsPremultiplied;
  };

  /**
   * CONSTRUCTION PHASE ONLY
   * Initialize this CanvasLayer with the given data.  The data must
   * have either mSurface or mGLContext initialized (but not both), as
   * well as mSize.
   *
   * This must only be called once.
   */
  virtual void Initialize(const Data& aData) = 0;

  /**
   * CONSTRUCTION PHASE ONLY
   * Notify this CanvasLayer that the rectangle given by aRect
   * has been updated, and any work that needs to be done
   * to bring the contents from the Surface/GLContext to the
   * Layer in preparation for compositing should be performed.
   */
  virtual void Updated(const nsIntRect& aRect) = 0;

  /**
   * CONSTRUCTION PHASE ONLY
   * Set the filter used to resample this image (if necessary).
   */
  void SetFilter(gfxPattern::GraphicsFilter aFilter) { mFilter = aFilter; }
  gfxPattern::GraphicsFilter GetFilter() const { return mFilter; }

  MOZ_LAYER_DECL_NAME("CanvasLayer", TYPE_CANVAS)

protected:
  CanvasLayer(LayerManager* aManager, void* aImplData)
    : Layer(aManager, aImplData), mFilter(gfxPattern::FILTER_GOOD) {}

  virtual nsACString& PrintInfo(nsACString& aTo, const char* aPrefix);

  gfxPattern::GraphicsFilter mFilter;
};

}
}

#endif /* GFX_LAYERS_H */
