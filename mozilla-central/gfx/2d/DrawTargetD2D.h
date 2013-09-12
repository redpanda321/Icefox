/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_DRAWTARGETD2D_H_
#define MOZILLA_GFX_DRAWTARGETD2D_H_

#include "2D.h"
#include "PathD2D.h"
#include <d3d10_1.h>
#include "HelpersD2D.h"

#include <vector>
#include <sstream>

#ifdef _MSC_VER
#include <hash_set>
#else
#include <unordered_set>
#endif

struct IDWriteFactory;

namespace mozilla {
namespace gfx {

class SourceSurfaceD2DTarget;
class SourceSurfaceD2D;
class GradientStopsD2D;
class ScaledFontDWrite;

const int32_t kLayerCacheSize = 5;

struct PrivateD3D10DataD2D
{
  RefPtr<ID3D10Effect> mEffect;
  RefPtr<ID3D10InputLayout> mInputLayout;
  RefPtr<ID3D10Buffer> mVB;
  RefPtr<ID3D10BlendState> mBlendStates[OP_COUNT];
};

class DrawTargetD2D : public DrawTarget
{
public:
  DrawTargetD2D();
  virtual ~DrawTargetD2D();

  virtual BackendType GetType() const { return BACKEND_DIRECT2D; }
  virtual TemporaryRef<SourceSurface> Snapshot();
  virtual IntSize GetSize() { return mSize; }

  virtual void Flush();
  virtual void DrawSurface(SourceSurface *aSurface,
                           const Rect &aDest,
                           const Rect &aSource,
                           const DrawSurfaceOptions &aSurfOptions = DrawSurfaceOptions(),
                           const DrawOptions &aOptions = DrawOptions());
  virtual void DrawSurfaceWithShadow(SourceSurface *aSurface,
                                     const Point &aDest,
                                     const Color &aColor,
                                     const Point &aOffset,
                                     Float aSigma,
                                     CompositionOp aOperator);
  virtual void ClearRect(const Rect &aRect);

  virtual void CopySurface(SourceSurface *aSurface,
                           const IntRect &aSourceRect,
                           const IntPoint &aDestination);

  virtual void FillRect(const Rect &aRect,
                        const Pattern &aPattern,
                        const DrawOptions &aOptions = DrawOptions());
  virtual void StrokeRect(const Rect &aRect,
                          const Pattern &aPattern,
                          const StrokeOptions &aStrokeOptions = StrokeOptions(),
                          const DrawOptions &aOptions = DrawOptions());
  virtual void StrokeLine(const Point &aStart,
                          const Point &aEnd,
                          const Pattern &aPattern,
                          const StrokeOptions &aStrokeOptions = StrokeOptions(),
                          const DrawOptions &aOptions = DrawOptions());
  virtual void Stroke(const Path *aPath,
                      const Pattern &aPattern,
                      const StrokeOptions &aStrokeOptions = StrokeOptions(),
                      const DrawOptions &aOptions = DrawOptions());
  virtual void Fill(const Path *aPath,
                    const Pattern &aPattern,
                    const DrawOptions &aOptions = DrawOptions());
  virtual void FillGlyphs(ScaledFont *aFont,
                          const GlyphBuffer &aBuffer,
                          const Pattern &aPattern,
                          const DrawOptions &aOptions = DrawOptions(),
                          const GlyphRenderingOptions *aRenderingOptions = nullptr);
  virtual void Mask(const Pattern &aSource,
                    const Pattern &aMask,
                    const DrawOptions &aOptions = DrawOptions());
  virtual void PushClip(const Path *aPath);
  virtual void PushClipRect(const Rect &aRect);
  virtual void PopClip();

  virtual TemporaryRef<SourceSurface> CreateSourceSurfaceFromData(unsigned char *aData,
                                                            const IntSize &aSize,
                                                            int32_t aStride,
                                                            SurfaceFormat aFormat) const;
  virtual TemporaryRef<SourceSurface> OptimizeSourceSurface(SourceSurface *aSurface) const;

  virtual TemporaryRef<SourceSurface>
    CreateSourceSurfaceFromNativeSurface(const NativeSurface &aSurface) const;
  
  virtual TemporaryRef<DrawTarget>
    CreateSimilarDrawTarget(const IntSize &aSize, SurfaceFormat aFormat) const;

  virtual TemporaryRef<PathBuilder> CreatePathBuilder(FillRule aFillRule = FILL_WINDING) const;

  virtual TemporaryRef<GradientStops>
    CreateGradientStops(GradientStop *aStops,
                        uint32_t aNumStops,
                        ExtendMode aExtendMode = EXTEND_CLAMP) const;

  virtual void *GetNativeSurface(NativeSurfaceType aType);

  bool Init(const IntSize &aSize, SurfaceFormat aFormat);
  bool Init(ID3D10Texture2D *aTexture, SurfaceFormat aFormat);
  bool InitD3D10Data();
  uint32_t GetByteSize() const;
  TemporaryRef<ID2D1Layer> GetCachedLayer();
  void PopCachedLayer(ID2D1RenderTarget *aRT);

  static ID2D1Factory *factory();
  static void CleanupD2D();
  static TemporaryRef<ID2D1StrokeStyle> CreateStrokeStyleForOptions(const StrokeOptions &aStrokeOptions);
  static IDWriteFactory *GetDWriteFactory();

  operator std::string() const {
    std::stringstream stream;
    stream << "DrawTargetD2D(" << this << ")";
    return stream.str();
  }

  static uint64_t mVRAMUsageDT;
  static uint64_t mVRAMUsageSS;

private:
  friend class AutoSaveRestoreClippedOut;
  friend class SourceSurfaceD2DTarget;

#ifdef _MSC_VER
  typedef stdext::hash_set<DrawTargetD2D*> TargetSet;
#else
  typedef std::unordered_set<DrawTargetD2D*> TargetSet;
#endif

  bool InitD2DRenderTarget();
  void PrepareForDrawing(ID2D1RenderTarget *aRT);

  // This function will mark the surface as changing, and make sure any
  // copy-on-write snapshots are notified.
  void MarkChanged();
  void FlushTransformToRT() {
    if (mTransformDirty) {
      mRT->SetTransform(D2DMatrix(mTransform));
      mTransformDirty = false;
    }
  }
  void AddDependencyOnSource(SourceSurfaceD2DTarget* aSource);

  ID3D10BlendState *GetBlendStateForOperator(CompositionOp aOperator);
  ID2D1RenderTarget *GetRTForOperation(CompositionOp aOperator, const Pattern &aPattern);
  void FinalizeRTForOperation(CompositionOp aOperator, const Pattern &aPattern, const Rect &aBounds);  void EnsureViews();
  void PopAllClips();
  void PushClipsToRT(ID2D1RenderTarget *aRT);
  void PopClipsFromRT(ID2D1RenderTarget *aRT);

  // This function ensures mCurrentClipMaskTexture contains a texture containing
  // a mask corresponding with the current DrawTarget clip. See
  // GetClippedGeometry for a description of aClipBounds.
  void EnsureClipMaskTexture(IntRect *aClipBounds);

  bool FillGlyphsManual(ScaledFontDWrite *aFont,
                        const GlyphBuffer &aBuffer,
                        const Color &aColor,
                        IDWriteRenderingParams *aParams,
                        const DrawOptions &aOptions = DrawOptions());

  TemporaryRef<ID2D1RenderTarget> CreateRTForTexture(ID3D10Texture2D *aTexture, SurfaceFormat aFormat);
  TemporaryRef<ID2D1Geometry> ConvertRectToGeometry(const D2D1_RECT_F& aRect);
  TemporaryRef<ID2D1Geometry> GetTransformedGeometry(ID2D1Geometry *aGeometry, const D2D1_MATRIX_3X2_F &aTransform);
  TemporaryRef<ID2D1Geometry> Intersect(ID2D1Geometry *aGeometryA, ID2D1Geometry *aGeometryB);

  // This returns the clipped geometry, in addition it returns aClipBounds which
  // represents the intersection of all pixel-aligned rectangular clips that
  // are currently set. The returned clipped geometry must be clipped by these
  // bounds to correctly reflect the total clip. This is in device space.
  TemporaryRef<ID2D1Geometry> GetClippedGeometry(IntRect *aClipBounds);

  TemporaryRef<ID2D1Brush> CreateBrushForPattern(const Pattern &aPattern, Float aAlpha = 1.0f);

  TemporaryRef<ID3D10Texture2D> CreateGradientTexture(const GradientStopsD2D *aStops);
  TemporaryRef<ID3D10Texture2D> CreateTextureForAnalysis(IDWriteGlyphRunAnalysis *aAnalysis, const IntRect &aBounds);

  // This creates a (partially) uploaded bitmap for a DataSourceSurface. It
  // uploads the minimum requirement and possibly downscales. It adjusts the
  // input Matrix to compensate.
  TemporaryRef<ID2D1Bitmap> CreatePartialBitmapForSurface(DataSourceSurface *aSurface, Matrix &aMatrix,
                                                          ExtendMode aExtendMode);

  void SetupEffectForRadialGradient(const RadialGradientPattern *aPattern);
  void SetupStateForRendering();

  // Set the scissor rect to a certain IntRects, resets the scissor rect to
  // surface bounds when NULL is specified.
  void SetScissorToRect(IntRect *aRect);

  void PushD2DLayer(ID2D1RenderTarget *aRT, ID2D1Geometry *aGeometry, ID2D1Layer *aLayer, const D2D1_MATRIX_3X2_F &aTransform);

  static const uint32_t test = 4;

  IntSize mSize;

  RefPtr<ID3D10Device1> mDevice;
  RefPtr<ID3D10Texture2D> mTexture;
  RefPtr<ID3D10Texture2D> mCurrentClipMaskTexture;
  RefPtr<ID2D1Geometry> mCurrentClippedGeometry;
  // This is only valid if mCurrentClippedGeometry is non-null. And will
  // only be the intersection of all pixel-aligned retangular clips. This is in
  // device space.
  IntRect mCurrentClipBounds;
  mutable RefPtr<ID2D1RenderTarget> mRT;

  // We store this to prevent excessive SetTextRenderingParams calls.
  RefPtr<IDWriteRenderingParams> mTextRenderingParams;

  // Temporary texture and render target used for supporting alternative operators.
  RefPtr<ID3D10Texture2D> mTempTexture;
  RefPtr<ID3D10RenderTargetView> mRTView;
  RefPtr<ID3D10ShaderResourceView> mSRView;
  RefPtr<ID2D1RenderTarget> mTempRT;
  RefPtr<ID3D10RenderTargetView> mTempRTView;

  // List of pushed clips.
  struct PushedClip
  {
    RefPtr<ID2D1Layer> mLayer;
    D2D1_RECT_F mBounds;
    union {
      // If mPath is non-NULL, the mTransform member will be used, otherwise
      // the mIsPixelAligned member is valid.
      D2D1_MATRIX_3X2_F mTransform;
      bool mIsPixelAligned;
    };
    RefPtr<PathD2D> mPath;
  };
  std::vector<PushedClip> mPushedClips;

  // We cache ID2D1Layer objects as it causes D2D to keep around textures that
  // serve as the temporary surfaces for these operations. As texture creation
  // is quite expensive this considerably improved performance.
  // Careful here, RAII will not ensure destruction of the RefPtrs.
  RefPtr<ID2D1Layer> mCachedLayers[kLayerCacheSize];
  uint32_t mCurrentCachedLayer;
  
  // The latest snapshot of this surface. This needs to be told when this
  // target is modified. We keep it alive as a cache.
  RefPtr<SourceSurfaceD2DTarget> mSnapshot;
  // A list of targets we need to flush when we're modified.
  TargetSet mDependentTargets;
  // A list of targets which have this object in their mDependentTargets set
  TargetSet mDependingOnTargets;

  // True of the current clip stack is pushed to the main RT.
  bool mClipsArePushed;
  PrivateD3D10DataD2D *mPrivateData;
  static ID2D1Factory *mFactory;
  static IDWriteFactory *mDWriteFactory;
};

}
}

#endif /* MOZILLA_GFX_DRAWTARGETD2D_H_ */
