/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BasicThebesLayer.h"

#include "nsIWidget.h"
#include "RenderTrace.h"
#include "sampler.h"
#include "gfxUtils.h"

#include "prprf.h"

#include "imgIEncoder.h"
#include "prmem.h"

using namespace mozilla::gfx;

namespace mozilla {
namespace layers {

static uint32_t crc32_tab[] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
	0xe963a535, 0x9e6495a3,	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
	0xf3b97148, 0x84be41de,	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,	0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5,	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,	0x35b5a8fa, 0x42b2986c,
	0xdbbbc9d6, 0xacbcf940,	0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
	0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,	0x76dc4190, 0x01db7106,
	0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
	0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
	0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
	0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
	0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
	0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
	0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
	0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
	0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
	0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
	0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
	0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
	0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

uint32_t
crc32(uint32_t crc, const uint8_t *buf, size_t size)
{
	const uint8_t *p;

	p = (uint8_t*) buf;
	crc = crc ^ ~0U;

	while (size--)
		crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);

	return crc ^ ~0U;
}

uint32_t crc32_surface(uint8_t *data, uint32_t stride, uint32 width, uint32 height)
{

	uint32_t crc = 0;
	int i = 0;

	while (height--) {
		crc = crc32(crc, data, width);
		data += stride;
	}

	return crc;
}

uint32_t crc32_surface_rect(gfxImageSurface *surf,  const nsIntRect *rect)
{

	uint32_t crc = 0;
	int i = 0;
	uint32_t height = rect->height;

	const uint8_t *data = (uint8_t *) surf->Data() + rect->y * surf->Stride() + (rect->x * 4);

	while (height--) {
		crc = crc32(crc, data , rect->width);
		data += surf->Stride();
	}

	return crc;
}

already_AddRefed<gfxASurface>
BasicThebesLayer::CreateBuffer(Buffer::ContentType aType, const nsIntSize& aSize)
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

static nsIntRegion
IntersectWithClip(const nsIntRegion& aRegion, gfxContext* aContext)
{
  gfxRect clip = aContext->GetClipExtents();
  clip.RoundOut();
  nsIntRect r(clip.X(), clip.Y(), clip.Width(), clip.Height());
  nsIntRegion result;
  result.And(aRegion, r);
  return result;
}

static void
SetAntialiasingFlags(Layer* aLayer, gfxContext* aTarget)
{
  if (!aTarget->IsCairo()) {
    RefPtr<DrawTarget> dt = aTarget->GetDrawTarget();

    if (dt->GetFormat() != FORMAT_B8G8R8A8) {
      return;
    }

    const nsIntRect& bounds = aLayer->GetVisibleRegion().GetBounds();
    Rect transformedBounds = dt->GetTransform().TransformBounds(Rect(Float(bounds.x), Float(bounds.y),
                                                                     Float(bounds.width), Float(bounds.height)));
    transformedBounds.RoundOut();
    IntRect intTransformedBounds;
    transformedBounds.ToIntRect(&intTransformedBounds);
    dt->SetPermitSubpixelAA(!(aLayer->GetContentFlags() & Layer::CONTENT_COMPONENT_ALPHA) ||
                            dt->GetOpaqueRect().Contains(intTransformedBounds));
  } else {
    nsRefPtr<gfxASurface> surface = aTarget->CurrentSurface();
    if (surface->GetContentType() != gfxASurface::CONTENT_COLOR_ALPHA) {
      // Destination doesn't have alpha channel; no need to set any special flags
      return;
    }

    const nsIntRect& bounds = aLayer->GetVisibleRegion().GetBounds();
    surface->SetSubpixelAntialiasingEnabled(
        !(aLayer->GetContentFlags() & Layer::CONTENT_COMPONENT_ALPHA) ||
        surface->GetOpaqueRect().Contains(
          aTarget->UserToDevice(gfxRect(bounds.x, bounds.y, bounds.width, bounds.height))));
  }
}

void
BasicThebesLayer::PaintThebes(gfxContext* aContext,
                              Layer* aMaskLayer,
                              LayerManager::DrawThebesLayerCallback aCallback,
                              void* aCallbackData,
                              ReadbackProcessor* aReadback)
{
  SAMPLE_LABEL("BasicThebesLayer", "PaintThebes");
  NS_ASSERTION(BasicManager()->InDrawing(),
               "Can only draw in drawing phase");
  nsRefPtr<gfxASurface> targetSurface = aContext->CurrentSurface();

  nsTArray<ReadbackProcessor::Update> readbackUpdates;
  if (aReadback && UsedForReadback()) {
    aReadback->GetThebesLayerUpdates(this, &readbackUpdates);
  }
  SyncFrontBufferToBackBuffer();

  bool canUseOpaqueSurface = CanUseOpaqueSurface();
  Buffer::ContentType contentType =
    canUseOpaqueSurface ? gfxASurface::CONTENT_COLOR :
                          gfxASurface::CONTENT_COLOR_ALPHA;
  float opacity = GetEffectiveOpacity();
  
  if (!BasicManager()->IsRetained() ||
      (!canUseOpaqueSurface &&
       (mContentFlags & CONTENT_COMPONENT_ALPHA) &&
       !MustRetainContent())) {
    NS_ASSERTION(readbackUpdates.IsEmpty(), "Can't do readback for non-retained layer");

    mValidRegion.SetEmpty();
    mBuffer.Clear();

    nsIntRegion toDraw = IntersectWithClip(GetEffectiveVisibleRegion(), aContext);

    RenderTraceInvalidateStart(this, "FFFF00", toDraw.GetBounds());

    if (!toDraw.IsEmpty() && !IsHidden()) {
      if (!aCallback) {
        BasicManager()->SetTransactionIncomplete();
        return;
      }

      aContext->Save();

      bool needsClipToVisibleRegion = GetClipToVisibleRegion();
      bool needsGroup =
          opacity != 1.0 || GetOperator() != gfxContext::OPERATOR_OVER || aMaskLayer;
      nsRefPtr<gfxContext> groupContext;
      if (needsGroup) {
        groupContext =
          BasicManager()->PushGroupForLayer(aContext, this, toDraw,
                                            &needsClipToVisibleRegion);
        if (GetOperator() != gfxContext::OPERATOR_OVER) {
          needsClipToVisibleRegion = true;
        }
      } else {
        groupContext = aContext;
      }
      SetAntialiasingFlags(this, groupContext);
      aCallback(this, groupContext, toDraw, nsIntRegion(), aCallbackData);
      if (needsGroup) {
        BasicManager()->PopGroupToSourceWithCachedSurface(aContext, groupContext);
        if (needsClipToVisibleRegion) {
          gfxUtils::ClipToRegion(aContext, toDraw);
        }
        AutoSetOperator setOperator(aContext, GetOperator());
        PaintWithMask(aContext, opacity, aMaskLayer);
      }

      aContext->Restore();
    }

    RenderTraceInvalidateEnd(this, "FFFF00");
    return;
  }

  {
    PRUint32 flags = 0;
#ifndef MOZ_GFX_OPTIMIZE_MOBILE
    gfxMatrix transform;
    if (!GetEffectiveTransform().CanDraw2D(&transform) ||
        transform.HasNonIntegerTranslation()) {
      flags |= ThebesLayerBuffer::PAINT_WILL_RESAMPLE;
    }
#endif
    if (mDrawAtomically) {
      flags |= ThebesLayerBuffer::PAINT_NO_ROTATION;
    }
    Buffer::PaintState state =
      mBuffer.BeginPaint(this, contentType, flags);
    mValidRegion.Sub(mValidRegion, state.mRegionToInvalidate);

    if (state.mContext) {
      // The area that became invalid and is visible needs to be repainted
      // (this could be the whole visible area if our buffer switched
      // from RGB to RGBA, because we might need to repaint with
      // subpixel AA)
      state.mRegionToInvalidate.And(state.mRegionToInvalidate,
                                    GetEffectiveVisibleRegion());
      nsIntRegion extendedDrawRegion = state.mRegionToDraw;
      SetAntialiasingFlags(this, state.mContext);

      RenderTraceInvalidateStart(this, "FFFF00", state.mRegionToDraw.GetBounds());

      PaintBuffer(state.mContext,
                  state.mRegionToDraw, extendedDrawRegion, state.mRegionToInvalidate,
                  state.mDidSelfCopy,
                  aCallback, aCallbackData);
      Mutated();

      RenderTraceInvalidateEnd(this, "FFFF00");
    } else {
      // It's possible that state.mRegionToInvalidate is nonempty here,
      // if we are shrinking the valid region to nothing. So use mRegionToDraw
      // instead.
      NS_WARN_IF_FALSE(state.mRegionToDraw.IsEmpty(),
                       "No context when we have something to draw; resource exhaustion?");
    }
  }

  if (BasicManager()->IsTransactionIncomplete())
    return;

  gfxRect clipExtents;
  clipExtents = aContext->GetClipExtents();
  if (!IsHidden() && !clipExtents.IsEmpty()) {
    AutoSetOperator setOperator(aContext, GetOperator());
    mBuffer.DrawTo(this, aContext, opacity, aMaskLayer);
  }

  for (PRUint32 i = 0; i < readbackUpdates.Length(); ++i) {
    ReadbackProcessor::Update& update = readbackUpdates[i];
    nsIntPoint offset = update.mLayer->GetBackgroundLayerOffset();
    nsRefPtr<gfxContext> ctx =
      update.mLayer->GetSink()->BeginUpdate(update.mUpdateRect + offset,
                                            update.mSequenceCounter);
    if (ctx) {
      NS_ASSERTION(opacity == 1.0, "Should only read back opaque layers");
      ctx->Translate(gfxPoint(offset.x, offset.y));
      mBuffer.DrawTo(this, ctx, 1.0, aMaskLayer);
      update.mLayer->GetSink()->EndUpdate(ctx, update.mUpdateRect + offset);
    }
  }
}

/**
 * AutoOpenBuffer is a helper that builds on top of AutoOpenSurface,
 * which we need to get a gfxASurface from a SurfaceDescriptor.  For
 * other layer types, simple lexical scoping of AutoOpenSurface is
 * easy.  For ThebesLayers, the lifetime of buffer mappings doesn't
 * exactly match simple lexical scopes, so naively putting
 * AutoOpenSurfaces on the stack doesn't always work.  We use this
 * helper to track openings instead.
 *
 * Any surface that's opened while painting this ThebesLayer will
 * notify this helper and register itself for unmapping.
 *
 * We ignore buffer destruction here because the shadow layers
 * protocol already ensures that destroyed buffers stay alive until
 * end-of-transaction.
 */
struct NS_STACK_CLASS AutoBufferTracker {
  AutoBufferTracker(BasicShadowableThebesLayer* aLayer)
    : mLayer(aLayer)
  {
    MOZ_ASSERT(!mLayer->mBufferTracker);

    mLayer->mBufferTracker = this;
    if (IsSurfaceDescriptorValid(mLayer->mBackBuffer)) {
      mInitialBuffer.construct(OPEN_READ_WRITE, mLayer->mBackBuffer);
      mLayer->mBuffer.MapBuffer(mInitialBuffer.ref().Get());
    }
  }

  ~AutoBufferTracker() {
    mLayer->mBufferTracker = nsnull;
    mLayer->mBuffer.UnmapBuffer();
    // mInitialBuffer and mNewBuffer will clean up after themselves if
    // they were constructed.
  }

  gfxASurface*
  CreatedBuffer(const SurfaceDescriptor& aDescriptor) {
    Maybe<AutoOpenSurface>* surface = mNewBuffers.AppendElement();
    surface->construct(OPEN_READ_WRITE, aDescriptor);
    return surface->ref().Get();
  }

  Maybe<AutoOpenSurface> mInitialBuffer;
  nsAutoTArray<Maybe<AutoOpenSurface>, 2> mNewBuffers;
  BasicShadowableThebesLayer* mLayer;

private:
  AutoBufferTracker(const AutoBufferTracker&) MOZ_DELETE;
  AutoBufferTracker& operator=(const AutoBufferTracker&) MOZ_DELETE;
};

void
BasicShadowableThebesLayer::PaintThebes(gfxContext* aContext,
                                        Layer* aMaskLayer,
                                        LayerManager::DrawThebesLayerCallback aCallback,
                                        void* aCallbackData,
                                        ReadbackProcessor* aReadback)
{
  if (!HasShadow()) {
    BasicThebesLayer::PaintThebes(aContext, aMaskLayer, aCallback, aCallbackData, aReadback);
    return;
  }

  AutoBufferTracker tracker(this);

  BasicThebesLayer::PaintThebes(aContext, nsnull, aCallback, aCallbackData, aReadback);
  if (aMaskLayer) {
    static_cast<BasicImplData*>(aMaskLayer->ImplData())
      ->Paint(aContext, nsnull);
  }
}


void
BasicShadowableThebesLayer::SetBackBufferAndAttrs(const OptionalThebesBuffer& aBuffer,
                                                  const nsIntRegion& aValidRegion,
                                                  const OptionalThebesBuffer& aReadOnlyFrontBuffer,
                                                  const nsIntRegion& aFrontUpdatedRegion)
{
  if (OptionalThebesBuffer::Tnull_t == aBuffer.type()) {
    mBackBuffer = SurfaceDescriptor();
  } else {
    mBackBuffer = aBuffer.get_ThebesBuffer().buffer();
    mBackBufferRect = aBuffer.get_ThebesBuffer().rect();
    mBackBufferRectRotation = aBuffer.get_ThebesBuffer().rotation();
  }
  mFrontAndBackBufferDiffer = true;
  mROFrontBuffer = aReadOnlyFrontBuffer;
  mFrontUpdatedRegion = aFrontUpdatedRegion;
  mFrontValidRegion = aValidRegion;
  if (OptionalThebesBuffer::Tnull_t == mROFrontBuffer.type()) {
    // For null readonly front, we have single buffer mode
    // so we can do sync right now, because it does not create new buffer and
    // don't do any graphic operations
    SyncFrontBufferToBackBuffer();
  }
}

void
BasicShadowableThebesLayer::SyncFrontBufferToBackBuffer()
{
  if (!mFrontAndBackBufferDiffer) {
    return;
  }

  gfxASurface* backBuffer = mBuffer.GetBuffer();
  if (!IsSurfaceDescriptorValid(mBackBuffer)) {
    MOZ_ASSERT(!backBuffer);
    MOZ_ASSERT(mROFrontBuffer.type() == OptionalThebesBuffer::TThebesBuffer);
    const ThebesBuffer roFront = mROFrontBuffer.get_ThebesBuffer();
    AutoOpenSurface roFrontBuffer(OPEN_READ_ONLY, roFront.buffer());
    AllocBackBuffer(roFrontBuffer.ContentType(), roFrontBuffer.Size());
  }
  mFrontAndBackBufferDiffer = false;

  Maybe<AutoOpenSurface> autoBackBuffer;
  if (!backBuffer) {
    autoBackBuffer.construct(OPEN_READ_WRITE, mBackBuffer);
    backBuffer = autoBackBuffer.ref().Get();
  }

  if (OptionalThebesBuffer::Tnull_t == mROFrontBuffer.type()) {
    // We didn't get back a read-only ref to our old back buffer (the
    // parent's new front buffer).  If the parent is pushing updates
    // to a texture it owns, then we probably got back the same buffer
    // we pushed in the update and all is well.  If not, ...
    mValidRegion = mFrontValidRegion;
    mBuffer.SetBackingBuffer(backBuffer, mBackBufferRect, mBackBufferRectRotation);
    return;
  }

  MOZ_LAYERS_LOG(("BasicShadowableThebes(%p): reading back <x=%d,y=%d,w=%d,h=%d>",
                  this,
                  mFrontUpdatedRegion.GetBounds().x,
                  mFrontUpdatedRegion.GetBounds().y,
                  mFrontUpdatedRegion.GetBounds().width,
                  mFrontUpdatedRegion.GetBounds().height));

  const ThebesBuffer roFront = mROFrontBuffer.get_ThebesBuffer();
  AutoOpenSurface autoROFront(OPEN_READ_ONLY, roFront.buffer());
  mBuffer.SetBackingBufferAndUpdateFrom(
    backBuffer,
    autoROFront.Get(), roFront.rect(), roFront.rotation(),
    mFrontUpdatedRegion);
  mIsNewBuffer = false;
  // Now the new back buffer has the same (interesting) pixels as the
  // new front buffer, and mValidRegion et al. are correct wrt the new
  // back buffer (i.e. as they were for the old back buffer)
}

static void 
PNGEncodeBuffer (gfxImageSurface *surf,  nsIntRect *rect,
                 char **buffer, unsigned long *size)
{
  nsCOMPtr<imgIEncoder> encoder =
  do_CreateInstance("@mozilla.org/image/encoder;2?type=image/jpeg");

  nsresult rv = encoder->InitFromData(surf->Data() + rect->y * surf->Stride() + (rect->x * 4),
                                      0,
                                      rect->width,
                                      rect->height,
                                      surf->Stride(),
                                      imgIEncoder::INPUT_FORMAT_HOSTARGB,
                                      NS_LITERAL_STRING("quality=30"));
  if (NS_FAILED(rv))
    return;

  nsCOMPtr<nsIInputStream> imgStream;
  CallQueryInterface(encoder.get(), getter_AddRefs(imgStream));
  if (!imgStream)
    return;

  PRUint32 bufSize;
  rv = imgStream->Available(&bufSize);
  if (NS_FAILED(rv))
    return;

  // ...leave a little extra room so we can call read again and make sure we
  // got everything. 16 bytes for better padding (maybe)
  bufSize += 16;
  PRUint32 imgSize = 0;
  char* imgData = (char*)PR_Malloc(bufSize);
  if (!imgData)
    return;

  PRUint32 numReadThisTime = 0;
  while ((rv = imgStream->Read(&imgData[imgSize],
                               bufSize - imgSize,
                               &numReadThisTime)) == NS_OK && numReadThisTime > 0)
  {
    imgSize += numReadThisTime;
    if (imgSize == bufSize) {
      // need a bigger buffer, just double
      bufSize *= 2;
      char* newImgData = (char*)PR_Realloc(imgData, bufSize);
      if (!newImgData) {
        PR_Free(imgData);
        return;
      }
      imgData = newImgData;
    }
  }

  *buffer = imgData;
  *size = imgSize;
  return;

}

void
BasicShadowableThebesLayer::PaintBuffer(gfxContext* aContext,
                                        const nsIntRegion& aRegionToDraw,
                                        const nsIntRegion& aExtendedRegionToDraw,
                                        const nsIntRegion& aRegionToInvalidate,
                                        bool aDidSelfCopy,
                                        LayerManager::DrawThebesLayerCallback aCallback,
                                        void* aCallbackData)
{
  nsTArray<uint32_t> sums(50);
  nsIntRegion trimmed_region;

  if (XRE_GetProcessType() == GeckoProcessType_Content)
  {
    nsRefPtr<gfxASurface> surf = getter_AddRefs(aContext->CurrentSurface());
    nsRefPtr<gfxImageSurface> imgsurf = static_cast<gfxImageSurface*>(surf.get());
    int i = 0;

    nsIntRegionRectIterator *iter = new nsIntRegionRectIterator(aRegionToDraw);
    const nsIntRect *cur = NULL;
    for (cur = iter->Next(); cur; cur=iter->Next()) {
      printf("drew region: %4d %4d %4d %4d \n", cur->x, cur->y, cur->width, cur->height);
      nsIntRect tmprect(cur->x + (int) aContext->CurrentMatrix().x0, cur->y + (int) aContext->CurrentMatrix().y0, cur->width, cur->height);
      uint32_t crc = crc32_surface_rect(imgsurf, &tmprect);
      sums.AppendElement(crc);
    }
  }

  Base::PaintBuffer(aContext,
                    aRegionToDraw, aExtendedRegionToDraw, aRegionToInvalidate,
                    aDidSelfCopy,
                    aCallback, aCallbackData);
  if (!HasShadow() || BasicManager()->IsTransactionIncomplete()) {
    return;
  }

  if (XRE_GetProcessType() == GeckoProcessType_Content)
  {
    nsRefPtr<gfxASurface> surf = getter_AddRefs(aContext->CurrentSurface());
    nsRefPtr<gfxImageSurface> imgsurf = static_cast<gfxImageSurface*>(surf.get());
    int i = 0;

    nsIntRegionRectIterator iter(aRegionToDraw);

    while (const nsIntRect* cur = iter.Next()) {
      nsIntRect tmprect(cur->x + (int) aContext->CurrentMatrix().x0, cur->y + (int) aContext->CurrentMatrix().y0, cur->width, cur->height);
      uint32_t crc = crc32_surface_rect(imgsurf, &tmprect);
      if (crc == sums[i]) {
      } else {
        trimmed_region.Or(trimmed_region, *cur);
      }
      i++;
    }
  }

  nsIntRegion updatedRegion;
  if (mIsNewBuffer || aDidSelfCopy) {
    // A buffer reallocation clears both buffers. The front buffer has all the
    // content by now, but the back buffer is still clear. Here, in effect, we
    // are saying to copy all of the pixels of the front buffer to the back.
    // Also when we self-copied in the buffer, the buffer space
    // changes and some changed buffer content isn't reflected in the
    // draw or invalidate region (on purpose!).  When this happens, we
    // need to read back the entire buffer too.
    updatedRegion = mVisibleRegion;
    mIsNewBuffer = false;
  } else {
    updatedRegion = aRegionToDraw;
  }

  if (PR_GetEnv("MOZ_LAYERS_FORCE_NETWORK_SURFACES")) {
    const nsIntRegion &workingReg = trimmed_region;

    nsIntPoint rotation = mBuffer.BufferRotation();
    nsIntRect bufferrect = mBuffer.BufferRect();

    static int update = 0;

    nsRefPtr<gfxASurface> surf = getter_AddRefs(aContext->CurrentSurface());
    nsRefPtr<gfxImageSurface> imgsurf = static_cast<gfxImageSurface*>(surf.get());

    unsigned int numregions = workingReg.GetNumRects();

    InfallibleTArray<PRUint32> sizes;
    InfallibleTArray<uint8> data;

    nsIntRegionRectIterator *iter = new nsIntRegionRectIterator(workingReg);
    const nsIntRect *cur = NULL;

    int serial = 0;
    for (cur = iter->Next(); cur; cur=iter->Next()) {
      printf("[%d %d] saving region: %d %d %d %d\n", update, serial, cur->x, cur->y, cur->width, cur->height);
      MOZ_LAYERS_LOG(("matrix translation: %f ", aContext->CurrentMatrix().y0));

      char *__buf = NULL;
      unsigned long __size = 0;

      nsIntRect tmprect(cur->x + (int) aContext->CurrentMatrix().x0, cur->y + (int) aContext->CurrentMatrix().y0, cur->width, cur->height);
      PNGEncodeBuffer(imgsurf, &tmprect, &__buf, &__size);
#ifdef MOZ_DUMP_PAINTING
      if (PR_GetEnv("MOZ_LAYERS_DUMP_NETWORK_SURFACES")) {
        char buff[128];
        snprintf(buff,128,"/tmp/to-be-sent-surface-%04d-%04d.jpg", update, serial);
        FILE *f = fopen(buff, "wb");
        fwrite(__buf, __size, 1, f);
        fclose(f);
        snprintf(buff,128,"/tmp/shm-surface-%04d-%04d.png", update, serial);
        imgsurf->WriteAsPNG(buff);
      }
#endif
      sizes.AppendElement(__size);
      data.AppendElements(__buf, __size);

      PR_Free(__buf);
      serial++;
    }

    update++;

    unsigned long total = 0;
    for (int i = 0; i<numregions; i++) {
      total += sizes[i];
    }
    MOZ_LAYERS_LOG(("about to send %d over the network", total));
    printf("about to send %6d k over the network, new buffer=%d, didselfcopy=%d, bufferRect=%d %d %d %d, rot= %d %d\n", total / 1024, mIsNewBuffer, aDidSelfCopy, mBuffer.BufferRect().x,mBuffer.BufferRect().y, mBuffer.BufferRect().width, mBuffer.BufferRect().height, mBuffer.BufferRotation().x, mBuffer.BufferRotation().y );

    BasicManager()->PaintedThebesBufferNet(BasicManager()->Hold(this),
                                           workingReg,
                                           mBuffer.BufferRect(),
                                           mBuffer.BufferRotation(),
                                           mBackBuffer, numregions, sizes, data, aDidSelfCopy);

    return;
  }

  NS_ASSERTION(mBuffer.BufferRect().Contains(aRegionToDraw.GetBounds()),
               "Update outside of buffer rect!");
  NS_ABORT_IF_FALSE(IsSurfaceDescriptorValid(mBackBuffer),
                    "should have a back buffer by now");
  BasicManager()->PaintedThebesBuffer(BasicManager()->Hold(this),
                                      updatedRegion,
                                      mBuffer.BufferRect(),
                                      mBuffer.BufferRotation(),
                                      mBackBuffer);
}

void
BasicShadowableThebesLayer::AllocBackBuffer(Buffer::ContentType aType,
                                            const nsIntSize& aSize)
{
  // This function may *not* open the buffer it allocates.
  if (!BasicManager()->AllocBuffer(gfxIntSize(aSize.width, aSize.height),
                                   aType,
                                   &mBackBuffer)) {
    enum { buflen = 256 };
    char buf[buflen];
    PR_snprintf(buf, buflen,
                "creating ThebesLayer 'back buffer' failed! width=%d, height=%d, type=%x",
                aSize.width, aSize.height, int(aType));
    NS_RUNTIMEABORT(buf);
  }
}

already_AddRefed<gfxASurface>
BasicShadowableThebesLayer::CreateBuffer(Buffer::ContentType aType,
                                         const nsIntSize& aSize)
{
  if (!HasShadow()) {
    return BasicThebesLayer::CreateBuffer(aType, aSize);
  }

  MOZ_LAYERS_LOG(("BasicShadowableThebes(%p): creating %d x %d buffer(x2)",
                  this,
                  aSize.width, aSize.height));

  if (IsSurfaceDescriptorValid(mBackBuffer)) {
    BasicManager()->DestroyedThebesBuffer(BasicManager()->Hold(this),
                                          mBackBuffer);
    mBackBuffer = SurfaceDescriptor();
  }

  AllocBackBuffer(aType, aSize);

  NS_ABORT_IF_FALSE(!mIsNewBuffer,
                    "Bad! Did we create a buffer twice without painting?");

  mIsNewBuffer = true;

  nsRefPtr<gfxASurface> buffer = mBufferTracker->CreatedBuffer(mBackBuffer);
  return buffer.forget();
}

void
BasicShadowableThebesLayer::Disconnect()
{
  mBackBuffer = SurfaceDescriptor();
  BasicShadowableLayer::Disconnect();
}


class BasicShadowThebesLayer : public ShadowThebesLayer, public BasicImplData {
public:
  BasicShadowThebesLayer(BasicShadowLayerManager* aLayerManager)
    : ShadowThebesLayer(aLayerManager, static_cast<BasicImplData*>(this))
  {
    MOZ_COUNT_CTOR(BasicShadowThebesLayer);
  }
  virtual ~BasicShadowThebesLayer()
  {
    // If Disconnect() wasn't called on us, then we assume that the
    // remote side shut down and IPC is disconnected, so we let IPDL
    // clean up our front surface Shmem.
    MOZ_COUNT_DTOR(BasicShadowThebesLayer);
  }

  virtual void SetValidRegion(const nsIntRegion& aRegion)
  {
    mOldValidRegion = mValidRegion;
    ShadowThebesLayer::SetValidRegion(aRegion);
  }

  virtual void Disconnect()
  {
    DestroyFrontBuffer();
    ShadowThebesLayer::Disconnect();
  }

  virtual void
  Swap(const ThebesBuffer& aNewFront, const nsIntRegion& aUpdatedRegion,
       OptionalThebesBuffer* aNewBack, nsIntRegion* aNewBackValidRegion,
       OptionalThebesBuffer* aReadOnlyFront, nsIntRegion* aFrontUpdatedRegion);

  virtual void DestroyFrontBuffer()
  {
    mFrontBuffer.Clear();
    mValidRegion.SetEmpty();
    mOldValidRegion.SetEmpty();

    if (IsSurfaceDescriptorValid(mFrontBufferDescriptor)) {
      mAllocator->DestroySharedSurface(&mFrontBufferDescriptor);
    }
  }

  virtual void PaintThebes(gfxContext* aContext,
                           Layer* aMaskLayer,
                           LayerManager::DrawThebesLayerCallback aCallback,
                           void* aCallbackData,
                           ReadbackProcessor* aReadback);

private:
  BasicShadowLayerManager* BasicManager()
  {
    return static_cast<BasicShadowLayerManager*>(mManager);
  }

  ShadowThebesLayerBuffer mFrontBuffer;
  // Describes the gfxASurface we hand out to |mFrontBuffer|.
  SurfaceDescriptor mFrontBufferDescriptor;
  // When we receive an update from our remote partner, we stow away
  // our previous parameters that described our previous front buffer.
  // Then when we Swap() back/front buffers, we can return these
  // parameters to our partner (adjusted as needed).
  nsIntRegion mOldValidRegion;
};

void
BasicShadowThebesLayer::Swap(const ThebesBuffer& aNewFront,
                             const nsIntRegion& aUpdatedRegion,
                             OptionalThebesBuffer* aNewBack,
                             nsIntRegion* aNewBackValidRegion,
                             OptionalThebesBuffer* aReadOnlyFront,
                             nsIntRegion* aFrontUpdatedRegion)
{
  if (IsSurfaceDescriptorValid(mFrontBufferDescriptor)) {
    AutoOpenSurface autoNewFrontBuffer(OPEN_READ_ONLY, aNewFront.buffer());
    AutoOpenSurface autoCurrentFront(OPEN_READ_ONLY, mFrontBufferDescriptor);

    if (PR_GetEnv("MOZ_LAYERS_FORCE_NETWORK_SURFACES")) {
        gfxImageSurface *newsurf = static_cast<gfxImageSurface*>(autoNewFrontBuffer.Get());
        gfxImageSurface *cursurf = static_cast<gfxImageSurface*>(autoCurrentFront.Get());
        newsurf->CopyFrom(cursurf);
    }

    if (autoCurrentFront.Size() != autoNewFrontBuffer.Size()) {
      // Current front buffer is obsolete
      DestroyFrontBuffer();
    }
  }
  // This code relies on Swap() arriving *after* attribute mutations.
  if (IsSurfaceDescriptorValid(mFrontBufferDescriptor)) {
    *aNewBack = ThebesBuffer();
    aNewBack->get_ThebesBuffer().buffer() = mFrontBufferDescriptor;
  } else {
    *aNewBack = null_t();
  }
  // We have to invalidate the pixels painted into the new buffer.
  // They might overlap with our old pixels.
  aNewBackValidRegion->Sub(mOldValidRegion, aUpdatedRegion);

  nsIntRect backRect;
  nsIntPoint backRotation;
  mFrontBuffer.Swap(
    aNewFront.rect(), aNewFront.rotation(),
    &backRect, &backRotation);

  if (aNewBack->type() != OptionalThebesBuffer::Tnull_t) {
    aNewBack->get_ThebesBuffer().rect() = backRect;
    aNewBack->get_ThebesBuffer().rotation() = backRotation;
  }

  mFrontBufferDescriptor = aNewFront.buffer();

  *aReadOnlyFront = aNewFront;
  *aFrontUpdatedRegion = aUpdatedRegion;

  AutoOpenSurface autoCurrentFront(OPEN_READ_ONLY, mFrontBufferDescriptor);
  _currentFrontSurface = autoCurrentFront.Get();
  nsIntPoint rot = mFrontBuffer.BufferRotation();
  MOZ_LAYERS_LOG(("Swap buffer %x, rot = %d %d, backrot = %d %d", _currentFrontSurface, rot.x, rot.y, backRotation.x, backRotation.y));
}

void
BasicShadowThebesLayer::PaintThebes(gfxContext* aContext,
                                    Layer* aMaskLayer,
                                    LayerManager::DrawThebesLayerCallback aCallback,
                                    void* aCallbackData,
                                    ReadbackProcessor* aReadback)
{
  NS_ASSERTION(BasicManager()->InDrawing(),
               "Can only draw in drawing phase");
  NS_ASSERTION(BasicManager()->IsRetained(),
               "ShadowThebesLayer makes no sense without retained mode");

  if (!IsSurfaceDescriptorValid(mFrontBufferDescriptor)) {
    return;
  }

  AutoOpenSurface autoFrontBuffer(OPEN_READ_ONLY, mFrontBufferDescriptor);
  mFrontBuffer.MapBuffer(autoFrontBuffer.Get());

  mFrontBuffer.DrawTo(this, aContext, GetEffectiveOpacity(), aMaskLayer);

  mFrontBuffer.UnmapBuffer();
}

already_AddRefed<ThebesLayer>
BasicLayerManager::CreateThebesLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<ThebesLayer> layer = new BasicThebesLayer(this);
  return layer.forget();
}

already_AddRefed<ShadowThebesLayer>
BasicShadowLayerManager::CreateShadowThebesLayer()
{
  NS_ASSERTION(InConstruction(), "Only allowed in construction phase");
  nsRefPtr<ShadowThebesLayer> layer = new BasicShadowThebesLayer(this);
  return layer.forget();
}

}
}
