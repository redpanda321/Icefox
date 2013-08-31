/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef THEBESLAYERBUFFER_H_
#define THEBESLAYERBUFFER_H_

#include "gfxContext.h"
#include "gfxASurface.h"
#include "nsRegion.h"

namespace mozilla {
namespace layers {

class AutoOpenSurface;
class ThebesLayer;

/**
 * This class encapsulates the buffer used to retain ThebesLayer contents,
 * i.e., the contents of the layer's GetVisibleRegion().
 * 
 * This is a cairo/Thebes surface, but with a literal twist. Scrolling
 * causes the layer's visible region to move. We want to keep
 * reusing the same surface if the region size hasn't changed, but we don't
 * want to keep moving the contents of the surface around in memory. So
 * we use a trick.
 * Consider just the vertical case, and suppose the buffer is H pixels
 * high and we're scrolling down by N pixels. Instead of copying the
 * buffer contents up by N pixels, we leave the buffer contents in place,
 * and paint content rows H to H+N-1 into rows 0 to N-1 of the buffer.
 * Then we can refresh the screen by painting rows N to H-1 of the buffer
 * at row 0 on the screen, and then painting rows 0 to N-1 of the buffer
 * at row H-N on the screen.
 * mBufferRotation.y would be N in this example.
 */
class ThebesLayerBuffer {
public:
  typedef gfxASurface::gfxContentType ContentType;

  /**
   * Controls the size of the backing buffer of this.
   * - SizedToVisibleBounds: the backing buffer is exactly the same
   *   size as the bounds of ThebesLayer's visible region
   * - ContainsVisibleBounds: the backing buffer is large enough to
   *   fit visible bounds.  May be larger.
   */
  enum BufferSizePolicy {
    SizedToVisibleBounds,
    ContainsVisibleBounds
  };

  ThebesLayerBuffer(BufferSizePolicy aBufferSizePolicy)
    : mBufferProvider(nullptr)
    , mBufferRotation(0,0)
    , mBufferSizePolicy(aBufferSizePolicy)
  {
    MOZ_COUNT_CTOR(ThebesLayerBuffer);
  }
  virtual ~ThebesLayerBuffer()
  {
    MOZ_COUNT_DTOR(ThebesLayerBuffer);
  }

  /**
   * Wipe out all retained contents. Call this when the entire
   * buffer becomes invalid.
   */
  void Clear()
  {
    mBuffer = nullptr;
    mBufferProvider = nullptr;
    mBufferRect.SetEmpty();
  }

  /**
   * This is returned by BeginPaint. The caller should draw into mContext.
   * mRegionToDraw must be drawn. mRegionToInvalidate has been invalidated
   * by ThebesLayerBuffer and must be redrawn on the screen.
   * mRegionToInvalidate is set when the buffer has changed from
   * opaque to transparent or vice versa, since the details of rendering can
   * depend on the buffer type.  mDidSelfCopy is true if we kept our buffer
   * but used MovePixels() to shift its content.
   */
  struct PaintState {
    PaintState()
      : mDidSelfCopy(false)
    {}

    nsRefPtr<gfxContext> mContext;
    nsIntRegion mRegionToDraw;
    nsIntRegion mRegionToInvalidate;
    bool mDidSelfCopy;
  };

  enum {
    PAINT_WILL_RESAMPLE = 0x01,
    PAINT_NO_ROTATION = 0x02
  };
  /**
   * Start a drawing operation. This returns a PaintState describing what
   * needs to be drawn to bring the buffer up to date in the visible region.
   * This queries aLayer to get the currently valid and visible regions.
   * The returned mContext may be null if mRegionToDraw is empty.
   * Otherwise it must not be null.
   * mRegionToInvalidate will contain mRegionToDraw.
   * @param aFlags when PAINT_WILL_RESAMPLE is passed, this indicates that
   * buffer will be resampled when rendering (i.e the effective transform
   * combined with the scale for the resolution is not just an integer
   * translation). This will disable buffer rotation (since we don't want
   * to resample across the rotation boundary) and will ensure that we
   * make the entire buffer contents valid (since we don't want to sample
   * invalid pixels outside the visible region, if the visible region doesn't
   * fill the buffer bounds).
   */
  PaintState BeginPaint(ThebesLayer* aLayer, ContentType aContentType,
                        uint32_t aFlags);

  enum {
    ALLOW_REPEAT = 0x01
  };
  /**
   * Return a new surface of |aSize| and |aType|.
   * @param aFlags if ALLOW_REPEAT is set, then the buffer should be configured
   * to allow repeat-mode, otherwise it should be in pad (clamp) mode
   */
  virtual already_AddRefed<gfxASurface>
  CreateBuffer(ContentType aType, const nsIntSize& aSize, uint32_t aFlags) = 0;

  /**
   * Get the underlying buffer, if any. This is useful because we can pass
   * in the buffer as the default "reference surface" if there is one.
   * Don't use it for anything else!
   */
  gfxASurface* GetBuffer() { return mBuffer; }

protected:
  enum XSide {
    LEFT, RIGHT
  };
  enum YSide {
    TOP, BOTTOM
  };
  nsIntRect GetQuadrantRectangle(XSide aXSide, YSide aYSide);
  /*
   * If aMask is non-null, then it is used as an alpha mask for rendering this
   * buffer. aMaskTransform must be non-null if aMask is non-null, and is used
   * to adjust the coordinate space of the mask.
   */
  void DrawBufferQuadrant(gfxContext* aTarget, XSide aXSide, YSide aYSide,
                          float aOpacity,
                          gfxASurface* aMask,
                          const gfxMatrix* aMaskTransform);
  void DrawBufferWithRotation(gfxContext* aTarget, float aOpacity,
                              gfxASurface* aMask = nullptr,
                              const gfxMatrix* aMaskTransform = nullptr);

  /**
   * |BufferRect()| is the rect of device pixels that this
   * ThebesLayerBuffer covers.  That is what DrawBufferWithRotation()
   * will paint when it's called.
   */
  const nsIntRect& BufferRect() const { return mBufferRect; }
  const nsIntPoint& BufferRotation() const { return mBufferRotation; }

  already_AddRefed<gfxASurface>
  SetBuffer(gfxASurface* aBuffer,
            const nsIntRect& aBufferRect, const nsIntPoint& aBufferRotation)
  {
    nsRefPtr<gfxASurface> tmp = mBuffer.forget();
    mBuffer = aBuffer;
    mBufferRect = aBufferRect;
    mBufferRotation = aBufferRotation;
    return tmp.forget();
  }

  /**
   * Set the buffer provider only.  This is used with surfaces that
   * require explicit map/unmap, which |aProvider| is used to do on
   * demand in this code.
   *
   * It's the caller's responsibility to ensure |aProvider| is valid
   * for the duration of operations it requests of this
   * ThebesLayerBuffer.  It's also the caller's responsibility to
   * unset the provider when inactive, by calling
   * SetBufferProvider(nullptr).
   */
  void SetBufferProvider(AutoOpenSurface* aProvider)
  {
    mBufferProvider = aProvider;
    if (!mBufferProvider) {
      mBuffer = nullptr;
    } else {
      // Only this buffer provider can give us a buffer.  If we
      // already have one, something has gone wrong.
      MOZ_ASSERT(!mBuffer);
    }
  }

  /**
   * Get a context at the specified resolution for updating |aBounds|,
   * which must be contained within a single quadrant.
   */
  already_AddRefed<gfxContext>
  GetContextForQuadrantUpdate(const nsIntRect& aBounds);

private:
  // Buffer helpers.  Don't use mBuffer directly; instead use one of
  // these helpers.

  /**
   * Return the buffer's content type.  Requires a valid buffer or
   * buffer provider.
   */
  gfxASurface::gfxContentType BufferContentType();
  bool BufferSizeOkFor(const nsIntSize& aSize);
  /**
   * If the buffer hasn't been mapped, map it and return it.
   */
  gfxASurface* EnsureBuffer();
  /**
   * True if we have a buffer where we can get it (but not necessarily
   * mapped currently).
   */
  bool HaveBuffer();

  nsRefPtr<gfxASurface> mBuffer;
  /**
   * This member is only set transiently.  It's used to map mBuffer
   * when we're using surfaces that require explicit map/unmap.
   */
  AutoOpenSurface* mBufferProvider;
  /** The area of the ThebesLayer that is covered by the buffer as a whole */
  nsIntRect             mBufferRect;
  /**
   * The x and y rotation of the buffer. Conceptually the buffer
   * has its origin translated to mBufferRect.TopLeft() - mBufferRotation,
   * is tiled to fill the plane, and the result is clipped to mBufferRect.
   * So the pixel at mBufferRotation within the buffer is what gets painted at
   * mBufferRect.TopLeft().
   * This is "rotation" in the sense of rotating items in a linear buffer,
   * where items falling off the end of the buffer are returned to the
   * buffer at the other end, not 2D rotation!
   */
  nsIntPoint            mBufferRotation;
  BufferSizePolicy      mBufferSizePolicy;
};

}
}

#endif /* THEBESLAYERBUFFER_H_ */
