/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_IMAGELAYER_H
#define GFX_IMAGELAYER_H

#include "Layers.h"

#include "ImageTypes.h"
#include "nsISupportsImpl.h"
#include "gfxPattern.h"

namespace mozilla {
namespace layers {

class ImageContainer;

/**
 * A Layer which renders an Image.
 */
class THEBES_API ImageLayer : public Layer {
public:
  enum ScaleMode {
    SCALE_NONE,
    SCALE_STRETCH
  // Unimplemented - SCALE_PRESERVE_ASPECT_RATIO_CONTAIN
  };

  /**
   * CONSTRUCTION PHASE ONLY
   * Set the ImageContainer. aContainer must have the same layer manager
   * as this layer.
   */
  void SetContainer(ImageContainer* aContainer);

  /**
   * CONSTRUCTION PHASE ONLY
   * Set the filter used to resample this image if necessary.
   */
  void SetFilter(gfxPattern::GraphicsFilter aFilter) { mFilter = aFilter; }

  /**
   * CONSTRUCTION PHASE ONLY
   * Set the size to scale the image to and the mode at which to scale.
   */
  void SetScaleToSize(const gfxIntSize &aSize, ScaleMode aMode)
  {
    mScaleToSize = aSize;
    mScaleMode = aMode;
  }


  ImageContainer* GetContainer() { return mContainer; }
  gfxPattern::GraphicsFilter GetFilter() { return mFilter; }
  const gfxIntSize& GetScaleToSize() { return mScaleToSize; }
  ScaleMode GetScaleMode() { return mScaleMode; }

  MOZ_LAYER_DECL_NAME("ImageLayer", TYPE_IMAGE)

  virtual void ComputeEffectiveTransforms(const gfx3DMatrix& aTransformToSurface);

  /**
   * if true, the image will only be backed by a single tile texture
   */
  void SetForceSingleTile(bool aForceSingleTile)
  {
    mForceSingleTile = aForceSingleTile;
    Mutated();
  }

protected:
  ImageLayer(LayerManager* aManager, void* aImplData);
  ~ImageLayer();
  virtual nsACString& PrintInfo(nsACString& aTo, const char* aPrefix);


  nsRefPtr<ImageContainer> mContainer;
  gfxPattern::GraphicsFilter mFilter;
  gfxIntSize mScaleToSize;
  ScaleMode mScaleMode;
  bool mForceSingleTile;
};

}
}

#endif /* GFX_IMAGELAYER_H */
