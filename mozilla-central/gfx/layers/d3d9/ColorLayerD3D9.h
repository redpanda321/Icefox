/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_COLORLAYERD3D9_H
#define GFX_COLORLAYERD3D9_H

#include "LayerManagerD3D9.h"

namespace mozilla {
namespace layers {

class THEBES_API ColorLayerD3D9 : public ColorLayer,
                                 public LayerD3D9
{
public:
  ColorLayerD3D9(LayerManagerD3D9 *aManager)
    : ColorLayer(aManager, NULL)
    , LayerD3D9(aManager)
  {
    mImplData = static_cast<LayerD3D9*>(this);
  }

  // LayerD3D9 Implementation
  virtual Layer* GetLayer();

  virtual void RenderLayer();
};

class ShadowColorLayerD3D9 : public ShadowColorLayer,
                            public LayerD3D9
{
public:
  ShadowColorLayerD3D9(LayerManagerD3D9 *aManager)
    : ShadowColorLayer(aManager, NULL)
    , LayerD3D9(aManager)
  { 
    mImplData = static_cast<LayerD3D9*>(this);
  }
  ~ShadowColorLayerD3D9() { Destroy(); }

  // LayerOGL Implementation
  virtual Layer* GetLayer() { return this; }

  virtual void Destroy() { }

  virtual void RenderLayer();
};

} /* layers */
} /* mozilla */

#endif /* GFX_COLORLAYERD3D9_H */
