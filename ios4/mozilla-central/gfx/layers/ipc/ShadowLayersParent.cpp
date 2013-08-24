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

#include <vector>

#include "ShadowLayersParent.h"
#include "ShadowLayerParent.h"
#include "ShadowLayers.h"

#include "mozilla/unused.h"

#include "gfxSharedImageSurface.h"

#include "ImageLayers.h"

typedef std::vector<mozilla::layers::EditReply> EditReplyVector;

namespace mozilla {
namespace layers {

//--------------------------------------------------
// Convenience accessors
static ShadowLayerParent*
cast(const PLayerParent* in)
{ 
  return const_cast<ShadowLayerParent*>(
    static_cast<const ShadowLayerParent*>(in));
}

template<class OpCreateT>
static ShadowLayerParent*
AsShadowLayer(const OpCreateT& op)
{
  return cast(op.layerParent());
}

static ShadowLayerParent*
AsShadowLayer(const OpSetRoot& op)
{
  return cast(op.rootParent());
}

static ShadowLayerParent*
ShadowContainer(const OpInsertAfter& op)
{
  return cast(op.containerParent());
}
static ShadowLayerParent*
ShadowChild(const OpInsertAfter& op)
{
  return cast(op.childLayerParent());
}
static ShadowLayerParent*
ShadowAfter(const OpInsertAfter& op)
{
  return cast(op.afterParent());
}

static ShadowLayerParent*
ShadowContainer(const OpAppendChild& op)
{
  return cast(op.containerParent());
}
static ShadowLayerParent*
ShadowChild(const OpAppendChild& op)
{
  return cast(op.childLayerParent());
}

static ShadowLayerParent*
ShadowContainer(const OpRemoveChild& op)
{
  return cast(op.containerParent());
}
static ShadowLayerParent*
ShadowChild(const OpRemoveChild& op)
{
  return cast(op.childLayerParent());
}

//--------------------------------------------------
// ShadowLayersParent
ShadowLayersParent::ShadowLayersParent(ShadowLayerManager* aManager)
{
  MOZ_COUNT_CTOR(ShadowLayersParent);
  mLayerManager = aManager;
}

ShadowLayersParent::~ShadowLayersParent()
{
  MOZ_COUNT_DTOR(ShadowLayersParent);
}

bool
ShadowLayersParent::RecvUpdate(const nsTArray<Edit>& cset,
                               nsTArray<EditReply>* reply)
{
  MOZ_LAYERS_LOG(("[ParentSide] recieved txn with %d edits", cset.Length()));

  EditReplyVector replyv;

  layer_manager()->BeginTransactionWithTarget(NULL);

  for (EditArray::index_type i = 0; i < cset.Length(); ++i) {
    const Edit& edit = cset[i];

    switch (edit.type()) {
      // Create* ops
    case Edit::TOpCreateThebesLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateThebesLayer"));

      nsRefPtr<ThebesLayer> layer = layer_manager()->CreateShadowThebesLayer();
      AsShadowLayer(edit.get_OpCreateThebesLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateContainerLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateContainerLayer"));

      nsRefPtr<ContainerLayer> layer = layer_manager()->CreateContainerLayer();
      AsShadowLayer(edit.get_OpCreateContainerLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateImageLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateImageLayer"));

      AsShadowLayer(edit.get_OpCreateImageLayer())->Bind(
        layer_manager()->CreateShadowImageLayer().get());
      break;
    }
    case Edit::TOpCreateColorLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateColorLayer"));

      nsRefPtr<ColorLayer> layer = layer_manager()->CreateColorLayer();
      AsShadowLayer(edit.get_OpCreateColorLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateCanvasLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateCanvasLayer"));

      nsRefPtr<CanvasLayer> layer = layer_manager()->CreateShadowCanvasLayer();
      AsShadowLayer(edit.get_OpCreateCanvasLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateThebesBuffer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateThebesBuffer"));

      const OpCreateThebesBuffer& otb = edit.get_OpCreateThebesBuffer();
      ShadowThebesLayer* thebes = static_cast<ShadowThebesLayer*>(
        AsShadowLayer(otb)->AsLayer());

      unused << thebes->Swap(new gfxSharedImageSurface(otb.initialFront()),
                             otb.bufferRect(),
                             nsIntPoint(0, 0));

      break;
    }
    case Edit::TOpCreateCanvasBuffer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateCanvasBuffer"));

      const OpCreateCanvasBuffer& ocb = edit.get_OpCreateCanvasBuffer();
      ShadowCanvasLayer* canvas = static_cast<ShadowCanvasLayer*>(
        AsShadowLayer(ocb)->AsLayer());
      nsRefPtr<gfxSharedImageSurface> front =
        new gfxSharedImageSurface(ocb.initialFront());
      CanvasLayer::Data data;
      data.mSurface = front;
      data.mSize = ocb.size();

      canvas->Initialize(data);

      break;
    }
    case Edit::TOpCreateImageBuffer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateImageBuffer"));

      const OpCreateImageBuffer ocb = edit.get_OpCreateImageBuffer();
      ShadowImageLayer* image = static_cast<ShadowImageLayer*>(
        AsShadowLayer(ocb)->AsLayer());

      image->Init(new gfxSharedImageSurface(ocb.initialFront()), ocb.size());

      break;
    }

      // Attributes
    case Edit::TOpSetLayerAttributes: {
      MOZ_LAYERS_LOG(("[ParentSide] SetLayerAttributes"));

      const OpSetLayerAttributes& osla = edit.get_OpSetLayerAttributes();
      Layer* layer = AsShadowLayer(osla)->AsLayer();
      const LayerAttributes& attrs = osla.attrs();

      const CommonLayerAttributes& common = attrs.common();
      layer->SetVisibleRegion(common.visibleRegion());
      layer->SetIsOpaqueContent(common.isOpaqueContent());
      layer->SetOpacity(common.opacity());
      layer->SetClipRect(common.useClipRect() ? &common.clipRect() : NULL);
      layer->SetTransform(common.transform());

      typedef SpecificLayerAttributes Specific;
      const SpecificLayerAttributes& specific = attrs.specific();
      switch (specific.type()) {
      case Specific::Tnull_t:
        break;

      case Specific::TThebesLayerAttributes:
        MOZ_LAYERS_LOG(("[ParentSide]   thebes layer"));

        static_cast<ShadowThebesLayer*>(layer)->SetValidRegion(
          specific.get_ThebesLayerAttributes().validRegion());
        break;

      case Specific::TColorLayerAttributes:
        MOZ_LAYERS_LOG(("[ParentSide]   color layer"));

        static_cast<ColorLayer*>(layer)->SetColor(
          specific.get_ColorLayerAttributes().color());
        break;

      case Specific::TCanvasLayerAttributes:
        MOZ_LAYERS_LOG(("[ParentSide]   canvas layer"));

        static_cast<CanvasLayer*>(layer)->SetFilter(
          specific.get_CanvasLayerAttributes().filter());
        break;

      case Specific::TImageLayerAttributes:
        MOZ_LAYERS_LOG(("[ParentSide]   image layer"));

        static_cast<ImageLayer*>(layer)->SetFilter(
          specific.get_ImageLayerAttributes().filter());
        break;

      default:
        NS_RUNTIMEABORT("not reached");
      }
      break;
    }

      // Tree ops
    case Edit::TOpSetRoot: {
      MOZ_LAYERS_LOG(("[ParentSide] SetRoot"));

      layer_manager()->SetRoot(AsShadowLayer(edit.get_OpSetRoot())->AsLayer());
      break;
    }
    case Edit::TOpInsertAfter: {
      MOZ_LAYERS_LOG(("[ParentSide] InsertAfter"));

      const OpInsertAfter& oia = edit.get_OpInsertAfter();
      ShadowContainer(oia)->AsContainer()->InsertAfter(
        ShadowChild(oia)->AsLayer(), ShadowAfter(oia)->AsLayer());
      break;
    }
    case Edit::TOpAppendChild: {
      MOZ_LAYERS_LOG(("[ParentSide] AppendChild"));

      const OpAppendChild& oac = edit.get_OpAppendChild();
      ShadowContainer(oac)->AsContainer()->InsertAfter(
        ShadowChild(oac)->AsLayer(), NULL);
      break;
    }
    case Edit::TOpRemoveChild: {
      MOZ_LAYERS_LOG(("[ParentSide] RemoveChild"));

      const OpRemoveChild& orc = edit.get_OpRemoveChild();
      Layer* childLayer = ShadowChild(orc)->AsLayer();
      ShadowContainer(orc)->AsContainer()->RemoveChild(childLayer);
      break;
    }

    case Edit::TOpPaintThebesBuffer: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint ThebesLayer"));

      const OpPaintThebesBuffer& op = edit.get_OpPaintThebesBuffer();
      ShadowLayerParent* shadow = AsShadowLayer(op);
      ShadowThebesLayer* thebes =
        static_cast<ShadowThebesLayer*>(shadow->AsLayer());
      const ThebesBuffer& newFront = op.newFrontBuffer();

      nsRefPtr<gfxSharedImageSurface> newBack =
        thebes->Swap(new gfxSharedImageSurface(newFront.buffer()),
                     newFront.rect(),
                     newFront.rotation());

      // XXX figure me out
      replyv.push_back(OpBufferSwap(shadow, NULL,
                                    newBack->GetShmem()));
      break;
    }
    case Edit::TOpPaintCanvas: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint CanvasLayer"));

      const OpPaintCanvas& op = edit.get_OpPaintCanvas();
      ShadowLayerParent* shadow = AsShadowLayer(op);
      ShadowCanvasLayer* canvas =
        static_cast<ShadowCanvasLayer*>(shadow->AsLayer());

      nsRefPtr<gfxSharedImageSurface> newBack =
        canvas->Swap(new gfxSharedImageSurface(op.newFrontBuffer()));
      canvas->Updated(op.updated());

      replyv.push_back(OpBufferSwap(shadow, NULL,
                                    newBack->GetShmem()));

      break;
    }
    case Edit::TOpPaintImage: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint ImageLayer"));

      const OpPaintImage& op = edit.get_OpPaintImage();
      ShadowLayerParent* shadow = AsShadowLayer(op);
      ShadowImageLayer* image =
        static_cast<ShadowImageLayer*>(shadow->AsLayer());

      nsRefPtr<gfxSharedImageSurface> newBack =
        image->Swap(new gfxSharedImageSurface(op.newFrontBuffer()));

      replyv.push_back(OpBufferSwap(shadow, NULL,
                                    newBack->GetShmem()));

      break;
    }

    default:
      NS_RUNTIMEABORT("not reached");
    }
  }

  layer_manager()->EndTransaction(NULL, NULL);

  reply->SetCapacity(replyv.size());
  if (replyv.size() > 0) {
    reply->AppendElements(&replyv.front(), replyv.size());
  }

  return true;
}

} // namespace layers
} // namespace mozilla
