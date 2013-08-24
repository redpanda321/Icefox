/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=8 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <vector>

#include "AutoOpenSurface.h"
#include "CompositorParent.h"
#include "gfxSharedImageSurface.h"
#include "ImageLayers.h"
#include "mozilla/layout/RenderFrameParent.h"
#include "mozilla/unused.h"
#include "RenderTrace.h"
#include "ShadowLayerParent.h"
#include "ShadowLayersParent.h"
#include "ShadowLayers.h"
#include "ShadowLayerUtils.h"
#include "TiledLayerBuffer.h"
#include "imgITools.h"
#include "imgIContainer.h"
#include "nsStringStream.h"

typedef std::vector<mozilla::layers::EditReply> EditReplyVector;

using mozilla::layout::RenderFrameParent;

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
ShadowLayersParent::ShadowLayersParent(ShadowLayerManager* aManager,
                                       ShadowLayersManager* aLayersManager)
  : mLayerManager(aManager), mShadowLayersManager(aLayersManager), mDestroyed(false)
{
  MOZ_COUNT_CTOR(ShadowLayersParent);
}

ShadowLayersParent::~ShadowLayersParent()
{
  MOZ_COUNT_DTOR(ShadowLayersParent);
}

void
ShadowLayersParent::Destroy()
{
  mDestroyed = true;
  for (size_t i = 0; i < ManagedPLayerParent().Length(); ++i) {
    ShadowLayerParent* slp =
      static_cast<ShadowLayerParent*>(ManagedPLayerParent()[i]);
    slp->Destroy();
  }
}

/* virtual */
bool
ShadowLayersParent::RecvUpdateNoSwap(const InfallibleTArray<Edit>& cset,
                 const bool& isFirstPaint)
{
  InfallibleTArray<EditReply> noReplies;
  bool success = RecvUpdate(cset, isFirstPaint, &noReplies);
  NS_ABORT_IF_FALSE(noReplies.Length() == 0, "RecvUpdateNoSwap requires a sync Update to carry Edits");
  return success;
}

bool
ShadowLayersParent::RecvUpdate(const InfallibleTArray<Edit>& cset,
                               const bool& isFirstPaint,
                               InfallibleTArray<EditReply>* reply)
{
#ifdef COMPOSITOR_PERFORMANCE_WARNING
  TimeStamp updateStart = TimeStamp::Now();
#endif

  MOZ_LAYERS_LOG(("[ParentSide] received txn with %d edits", cset.Length()));

  if (mDestroyed || layer_manager()->IsDestroyed()) {
    return true;
  }

  EditReplyVector replyv;

  layer_manager()->BeginTransactionWithTarget(NULL);

  for (EditArray::index_type i = 0; i < cset.Length(); ++i) {
    const Edit& edit = cset[i];

    switch (edit.type()) {
    
    case Edit::TOpDestroySurface: {
        const OpDestroySurface& op = edit.get_OpDestroySurface();
        SurfaceDescriptorNetworked sd = op.surface();
        MOZ_LAYERS_LOG(("need to destroy buffer id = %d", sd.id()));
    
	break;
    }
    
      // Create* ops
    case Edit::TOpCreateThebesLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateThebesLayer"));

      nsRefPtr<ShadowThebesLayer> layer =
        layer_manager()->CreateShadowThebesLayer();
      layer->SetAllocator(this);
      AsShadowLayer(edit.get_OpCreateThebesLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateContainerLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateContainerLayer"));

      nsRefPtr<ContainerLayer> layer = layer_manager()->CreateShadowContainerLayer();
      AsShadowLayer(edit.get_OpCreateContainerLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateImageLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateImageLayer"));

      nsRefPtr<ShadowImageLayer> layer =
        layer_manager()->CreateShadowImageLayer();
      AsShadowLayer(edit.get_OpCreateImageLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateColorLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateColorLayer"));

      nsRefPtr<ShadowColorLayer> layer = layer_manager()->CreateShadowColorLayer();
      AsShadowLayer(edit.get_OpCreateColorLayer())->Bind(layer);
      break;
    }
    case Edit::TOpCreateCanvasLayer: {
      MOZ_LAYERS_LOG(("[ParentSide] CreateCanvasLayer"));

      nsRefPtr<ShadowCanvasLayer> layer = 
        layer_manager()->CreateShadowCanvasLayer();
      layer->SetAllocator(this);
      AsShadowLayer(edit.get_OpCreateCanvasLayer())->Bind(layer);
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
      layer->SetContentFlags(common.contentFlags());
      layer->SetOpacity(common.opacity());
      layer->SetClipRect(common.useClipRect() ? &common.clipRect() : NULL);
      layer->SetTransform(common.transform());
      static bool fixedPositionLayersEnabled = getenv("MOZ_ENABLE_FIXED_POSITION_LAYERS") != 0;
      if (fixedPositionLayersEnabled) {
        layer->SetIsFixedPosition(common.isFixedPosition());
        layer->SetFixedPositionAnchor(common.fixedPositionAnchor());
      }
      if (PLayerParent* maskLayer = common.maskLayerParent()) {
        layer->SetMaskLayer(cast(maskLayer)->AsLayer());
      } else {
        layer->SetMaskLayer(NULL);
      }

      typedef SpecificLayerAttributes Specific;
      const SpecificLayerAttributes& specific = attrs.specific();
      switch (specific.type()) {
      case Specific::Tnull_t:
        break;

      case Specific::TThebesLayerAttributes: {
        MOZ_LAYERS_LOG(("[ParentSide]   thebes layer"));

        ShadowThebesLayer* thebesLayer =
          static_cast<ShadowThebesLayer*>(layer);
        const ThebesLayerAttributes& attrs =
          specific.get_ThebesLayerAttributes();

        thebesLayer->SetValidRegion(attrs.validRegion());

        break;
      }
      case Specific::TContainerLayerAttributes:
        MOZ_LAYERS_LOG(("[ParentSide]   container layer"));

        static_cast<ContainerLayer*>(layer)->SetFrameMetrics(
          specific.get_ContainerLayerAttributes().metrics());
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

      case Specific::TImageLayerAttributes: {
        MOZ_LAYERS_LOG(("[ParentSide]   image layer"));

        ImageLayer* imageLayer = static_cast<ImageLayer*>(layer);
        const ImageLayerAttributes& attrs = specific.get_ImageLayerAttributes();
        imageLayer->SetFilter(attrs.filter());
        imageLayer->SetForceSingleTile(attrs.forceSingleTile());
        break;
      }
      default:
        NS_RUNTIMEABORT("not reached");
      }
      break;
    }

      // Tree ops
    case Edit::TOpSetRoot: {
      MOZ_LAYERS_LOG(("[ParentSide] SetRoot"));

      mRoot = AsShadowLayer(edit.get_OpSetRoot())->AsContainer();
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

    case Edit::TOpPaintTiledLayerBuffer: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint TiledLayerBuffer"));
      const OpPaintTiledLayerBuffer& op = edit.get_OpPaintTiledLayerBuffer();
      ShadowLayerParent* shadow = AsShadowLayer(op);

      ShadowThebesLayer* shadowLayer = static_cast<ShadowThebesLayer*>(shadow->AsLayer());
      TiledLayerComposer* tileComposer = shadowLayer->AsTiledLayerComposer();

      NS_ASSERTION(tileComposer, "shadowLayer is not a tile composer");

      BasicTiledLayerBuffer* p = (BasicTiledLayerBuffer*)op.tiledLayerBuffer();
      tileComposer->PaintedTiledLayerBuffer(p);
      break;
    }
    case Edit::TOpPaintThebesBufferNet: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint ThebesLayerNet"));
      const OpPaintThebesBufferNet& op = edit.get_OpPaintThebesBufferNet();
      ShadowLayerParent* shadow = AsShadowLayer(op);
      ShadowThebesLayer* thebes =
      static_cast<ShadowThebesLayer*>(shadow->AsLayer());
      const ThebesBuffer& newFront = op.newFrontBuffer();

      unsigned int images = op.numregions();
      const InfallibleTArray<unsigned int> sizes = op.sizes();
      const InfallibleTArray<uint8> data = op.data();
      static int update=0;

      MOZ_LAYERS_LOG(("we got serial = %d", update));
      OptionalThebesBuffer newBack;
      nsIntRegion newValidRegion;
      OptionalThebesBuffer readonlyFront;
      nsIntRegion frontUpdatedRegion;
      nsIntPoint rot = newFront.rotation();

      thebes->Swap(newFront, op.updatedRegion(),
                   &newBack, &newValidRegion,
                   &readonlyFront, &frontUpdatedRegion);

      if (thebes->_currentFrontSurface) {
        MOZ_LAYERS_LOG(("[%d] we have a valid surface", update));
        nsRefPtr<gfxContext> target_ctx = new gfxContext(thebes->_currentFrontSurface);

        nsIntRegionRectIterator *iter = new nsIntRegionRectIterator(op.updatedRegion());
        const nsIntRect *cur = NULL;
        const unsigned char *dptr = data.Elements();
        int i = 0;

        static nsIntRect prevBufferRect(0,0,0,0);
        MOZ_LAYERS_LOG(("[%d] we have %d regions, beginning iteraction, buffer rect = %d %d %d %d", update, images, newFront.rect().x, newFront.rect().y, newFront.rect().width, newFront.rect().height));

        nsIntRect rect_real(newFront.rect().x, newFront.rect().y, newFront.rect().width, newFront.rect().height - newFront.rotation().y);
        nsIntRect rect_rot(0, newFront.rect().y  + newFront.rect().height - newFront.rotation().y, newFront.rect().width, newFront.rotation().y);
        printf("real region: %d %d %d %d\n", rect_real.x, rect_real.y, rect_real.width, rect_real.height);
        printf("rotated region: %d %d %d %d\n", rect_rot.x, rect_rot.y, rect_rot.width, rect_rot.height);

        int hasrotation = (newFront.rotation() != nsIntPoint(0,0));
        nsIntPoint movedby = prevBufferRect.TopLeft() - newFront.rect().TopLeft();

        if (op.didSelfCopy()) {
          printf("moving surface by %d %d\n", movedby.x, movedby.y);
          // spooky, and plan wrong
          thebes->_currentFrontSurface->MovePixels(nsIntRect(0, 0, thebes->_currentFrontSurface->GetSize().width,  thebes->_currentFrontSurface->GetSize().height), movedby);
        }

        for (cur = iter->Next(); cur; cur=iter->Next()) {
          nsCOMPtr<imgITools> imgtool = do_CreateInstance("@mozilla.org/image/tools;1");
          nsCOMPtr<nsIInputStream> stream;
          nsresult rv = NS_NewByteInputStream(getter_AddRefs(stream),
                                              reinterpret_cast<const char*>(dptr), sizes[i],
                                              NS_ASSIGNMENT_DEPEND);
          NS_ENSURE_SUCCESS(rv, rv);
          // decode imag
          nsCOMPtr<imgIContainer> container;
          rv = imgtool->DecodeImageData(stream, NS_LITERAL_CSTRING("image/jpeg"), getter_AddRefs(container));
          NS_ENSURE_SUCCESS(rv, rv);

          nsRefPtr<gfxImageSurface> region_surf;
          rv = container->CopyFrame(imgIContainer::FRAME_CURRENT, true,
                                    getter_AddRefs(region_surf));
          NS_ENSURE_SUCCESS(rv, rv);

          printf("[%d] buffer has a region: %d %d %d %d, len=%d\n", update, cur->x, cur->y, cur->width, cur->height, sizes[i]);
          gfxMatrix m(target_ctx->CurrentMatrix());
          int direction = 0;

          if (movedby.y > 0) {
            direction = 1;
          } else if (movedby.y < 0) {
          }

          nsIntPoint rotationpoint;
          printf("direction = %d, as delta is %d\n", direction, movedby.y);

          if(rect_real.Contains(*cur) && rect_rot.Contains(*cur)) {
            printf("unhandled buffer contains in both real and rot sects\n");
            abort();
          }
          if (hasrotation) {
            if (!direction) {
              if(rect_real.Contains(*cur)) {
                rotationpoint = -rect_real.TopLeft();
                printf("real rect\n");
              } else if (rect_rot.Contains(*cur)) {
                rotationpoint = -rect_rot.TopLeft();
                printf("rot rect\n");
              }
            } else {
              if(rect_real.Contains(*cur)) {
                rotationpoint = -rect_rot.TopLeft();
                printf("rot rect\n");
              } else if (rect_rot.Contains(*cur)) {
                rotationpoint = -rect_real.TopLeft();
                printf("real rect\n");
              }
            }
          } else {
            rotationpoint = -newFront.rect().TopLeft();
          }

          target_ctx->Translate(rotationpoint);
          target_ctx->Clip(*cur);
          target_ctx->SetSource(region_surf, gfxPoint(cur->x, cur->y));
          target_ctx->SetOperator(gfxContext::OPERATOR_SOURCE);
          target_ctx->Paint();
          printf("painting, translation is: %f\n", target_ctx->CurrentMatrix().y0);
#ifdef MOZ_DUMP_PAINTING
          if (PR_GetEnv("MOZ_LAYERS_DUMP_NETWORK_SURFACES")) {
            char buff[128];
            snprintf(buff,128,"/tmp/recv-surface-%04d-%04d.png", update, i);
            thebes->_currentFrontSurface->WriteAsPNG(buff);
            snprintf(buff,128,"/tmp/recv-region-%04d-%04d.png", update, i);
            region_surf->WriteAsPNG(buff);
          }
#endif
          target_ctx->SetMatrix(m);
          target_ctx->ResetClip();

          dptr+=sizes[i];
          i++;
        }

        // ugly, ugly ugly FIXME
        prevBufferRect = newFront.rect();
      }
      update++;

      replyv.push_back(
        OpThebesBufferSwap(
          shadow, NULL,
          newBack, newValidRegion,
          readonlyFront, frontUpdatedRegion));

      RenderTraceInvalidateEnd(thebes, "FF00FF");
      break;
    }
    case Edit::TOpPaintThebesBuffer: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint ThebesLayer"));

      const OpPaintThebesBuffer& op = edit.get_OpPaintThebesBuffer();
      ShadowLayerParent* shadow = AsShadowLayer(op);
      ShadowThebesLayer* thebes =
        static_cast<ShadowThebesLayer*>(shadow->AsLayer());
      const ThebesBuffer& newFront = op.newFrontBuffer();

      RenderTraceInvalidateStart(thebes, "FF00FF", op.updatedRegion().GetBounds());
      OptionalThebesBuffer newBack;
      nsIntRegion newValidRegion;
      OptionalThebesBuffer readonlyFront;
      nsIntRegion frontUpdatedRegion;
      thebes->Swap(newFront, op.updatedRegion(),
                   &newBack, &newValidRegion,
                   &readonlyFront, &frontUpdatedRegion);
      replyv.push_back(
        OpThebesBufferSwap(
          shadow, NULL,
          newBack, newValidRegion,
          readonlyFront, frontUpdatedRegion));

      RenderTraceInvalidateEnd(thebes, "FF00FF");
      break;
    }
    case Edit::TOpPaintCanvas: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint CanvasLayer"));

      const OpPaintCanvas& op = edit.get_OpPaintCanvas();
      ShadowLayerParent* shadow = AsShadowLayer(op);
      ShadowCanvasLayer* canvas =
        static_cast<ShadowCanvasLayer*>(shadow->AsLayer());

      RenderTraceInvalidateStart(canvas, "FF00FF", canvas->GetVisibleRegion().GetBounds());

      canvas->SetAllocator(this);
      CanvasSurface newBack;
      canvas->Swap(op.newFrontBuffer(), op.needYFlip(), &newBack);
      canvas->Updated();
      replyv.push_back(OpBufferSwap(shadow, NULL,
                                    newBack));

      RenderTraceInvalidateEnd(canvas, "FF00FF");
      break;
    }
    case Edit::TOpPaintImage: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint ImageLayer"));

      const OpPaintImage& op = edit.get_OpPaintImage();
      ShadowLayerParent* shadow = AsShadowLayer(op);
      ShadowImageLayer* image =
        static_cast<ShadowImageLayer*>(shadow->AsLayer());

      RenderTraceInvalidateStart(image, "FF00FF", image->GetVisibleRegion().GetBounds());

      image->SetAllocator(this);
      SharedImage newBack;
      image->Swap(op.newFrontBuffer(), &newBack);
      replyv.push_back(OpImageSwap(shadow, NULL,
                                   newBack));

      RenderTraceInvalidateEnd(image, "FF00FF");
      break;
    }

    default:
      NS_RUNTIMEABORT("not reached");
    }
  }

  layer_manager()->EndTransaction(NULL, NULL, LayerManager::END_NO_IMMEDIATE_REDRAW);

  reply->SetCapacity(replyv.size());
  if (replyv.size() > 0) {
    reply->AppendElements(&replyv.front(), replyv.size());
  }

  // Ensure that any pending operations involving back and front
  // buffers have completed, so that neither process stomps on the
  // other's buffer contents.
  ShadowLayerManager::PlatformSyncBeforeReplyUpdate();

  mShadowLayersManager->ShadowLayersUpdated(isFirstPaint);

#ifdef COMPOSITOR_PERFORMANCE_WARNING
  int compositeTime = (int)(mozilla::TimeStamp::Now() - updateStart).ToMilliseconds();
  if (compositeTime > 15) {
    printf_stderr("Compositor: Layers update took %i ms (blocking gecko).\n", compositeTime);
  }
#endif

  return true;
}

bool
ShadowLayersParent::RecvDrawToSurface(const SurfaceDescriptor& surfaceIn,
                                      SurfaceDescriptor* surfaceOut)
{
  *surfaceOut = surfaceIn;
  if (mDestroyed || layer_manager()->IsDestroyed()) {
    return true;
  }

  AutoOpenSurface sharedSurface(OPEN_READ_WRITE, surfaceIn);

  nsRefPtr<gfxASurface> localSurface =
    gfxPlatform::GetPlatform()->CreateOffscreenSurface(sharedSurface.Size(),
                                                       sharedSurface.ContentType());
  nsRefPtr<gfxContext> context = new gfxContext(localSurface);

  layer_manager()->BeginTransactionWithTarget(context);
  layer_manager()->EndTransaction(NULL, NULL);
  nsRefPtr<gfxContext> contextForCopy = new gfxContext(sharedSurface.Get());
  contextForCopy->SetOperator(gfxContext::OPERATOR_SOURCE);
  contextForCopy->DrawSurface(localSurface, localSurface->GetSize());
  return true;
}

PGrallocBufferParent*
ShadowLayersParent::AllocPGrallocBuffer(const gfxIntSize& aSize,
                                        const gfxContentType& aContent,
                                        MaybeMagicGrallocBufferHandle* aOutHandle)
{
#ifdef MOZ_HAVE_SURFACEDESCRIPTORGRALLOC
  return GrallocBufferActor::Create(aSize, aContent, aOutHandle);
#else
  NS_RUNTIMEABORT("No gralloc buffers for you");
  return nsnull;
#endif
}

bool
ShadowLayersParent::DeallocPGrallocBuffer(PGrallocBufferParent* actor)
{
#ifdef MOZ_HAVE_SURFACEDESCRIPTORGRALLOC
  delete actor;
  return true;
#else
  NS_RUNTIMEABORT("Um, how did we get here?");
  return false;
#endif
}

PLayerParent*
ShadowLayersParent::AllocPLayer()
{
  return new ShadowLayerParent();
}

bool
ShadowLayersParent::DeallocPLayer(PLayerParent* actor)
{
  delete actor;
  return true;
}

void
ShadowLayersParent::DestroySharedSurface(gfxSharedImageSurface* aSurface)
{
  layer_manager()->DestroySharedSurface(aSurface, this);
}

void
ShadowLayersParent::DestroySharedSurface(SurfaceDescriptor* aSurface)
{
  layer_manager()->DestroySharedSurface(aSurface, this);
}

} // namespace layers
} // namespace mozilla
