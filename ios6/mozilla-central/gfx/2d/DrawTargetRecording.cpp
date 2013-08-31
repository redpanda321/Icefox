/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DrawTargetRecording.h"
#include "PathRecording.h"
#include <stdio.h>

#include "Logging.h"
#include "Tools.h"

namespace mozilla {
namespace gfx {

class SourceSurfaceRecording : public SourceSurface
{
public:
  SourceSurfaceRecording(SourceSurface *aFinalSurface, DrawEventRecorderPrivate *aRecorder)
    : mFinalSurface(aFinalSurface), mRecorder(aRecorder)
  {
  }

  ~SourceSurfaceRecording()
  {
    mRecorder->RecordEvent(RecordedSourceSurfaceDestruction(this));
  }

  virtual SurfaceType GetType() const { return SURFACE_RECORDING; }
  virtual IntSize GetSize() const { return mFinalSurface->GetSize(); }
  virtual SurfaceFormat GetFormat() const { return mFinalSurface->GetFormat(); }
  virtual TemporaryRef<DataSourceSurface> GetDataSurface() { return mFinalSurface->GetDataSurface(); }

  RefPtr<SourceSurface> mFinalSurface;
  RefPtr<DrawEventRecorderPrivate> mRecorder;
};

class GradientStopsRecording : public GradientStops
{
public:
  GradientStopsRecording(GradientStops *aFinalGradientStops, DrawEventRecorderPrivate *aRecorder)
    : mFinalGradientStops(aFinalGradientStops), mRecorder(aRecorder)
  {
  }

  ~GradientStopsRecording()
  {
    mRecorder->RecordEvent(RecordedGradientStopsDestruction(this));
  }

  virtual BackendType GetBackendType() const { return BACKEND_RECORDING; }

  RefPtr<GradientStops> mFinalGradientStops;
  RefPtr<DrawEventRecorderPrivate> mRecorder;
};

static SourceSurface *
GetSourceSurface(SourceSurface *aSurface)
{
  if (aSurface->GetType() != SURFACE_RECORDING) {
    return aSurface;
  }

  return static_cast<SourceSurfaceRecording*>(aSurface)->mFinalSurface;
}

static GradientStops *
GetGradientStops(GradientStops *aStops)
{
  if (aStops->GetBackendType() != BACKEND_RECORDING) {
    return aStops;
  }

  return static_cast<GradientStopsRecording*>(aStops)->mFinalGradientStops;
}

struct AdjustedPattern
{
  AdjustedPattern(const Pattern &aPattern)
    : mPattern(NULL)
  {
    mOrigPattern = const_cast<Pattern*>(&aPattern);
  }

  ~AdjustedPattern() {
    if (mPattern) {
      mPattern->~Pattern();
    }
  }

  operator Pattern*()
  {
    switch(mOrigPattern->GetType()) {
    case PATTERN_COLOR:
      return mOrigPattern;
    case PATTERN_SURFACE:
      {
        SurfacePattern *surfPat = static_cast<SurfacePattern*>(mOrigPattern);
        mPattern =
          new (mSurfPat) SurfacePattern(GetSourceSurface(surfPat->mSurface),
                                        surfPat->mExtendMode, surfPat->mMatrix,
                                        surfPat->mFilter);
        return mPattern;
      }
    case PATTERN_LINEAR_GRADIENT:
      {
        LinearGradientPattern *linGradPat = static_cast<LinearGradientPattern*>(mOrigPattern);
        mPattern =
          new (mLinGradPat) LinearGradientPattern(linGradPat->mBegin, linGradPat->mEnd,
                                                  GetGradientStops(linGradPat->mStops),
                                                  linGradPat->mMatrix);
        return mPattern;
      }
    case PATTERN_RADIAL_GRADIENT:
      {
        RadialGradientPattern *radGradPat = static_cast<RadialGradientPattern*>(mOrigPattern);
        mPattern =
          new (mRadGradPat) RadialGradientPattern(radGradPat->mCenter1, radGradPat->mCenter2,
                                                  radGradPat->mRadius1, radGradPat->mRadius2,
                                                  GetGradientStops(radGradPat->mStops),
                                                  radGradPat->mMatrix);
        return mPattern;
      }
    default:
      return new (mColPat) ColorPattern(Color());
    }

    return mPattern;
  }

  union {
    char mColPat[sizeof(ColorPattern)];
    char mLinGradPat[sizeof(LinearGradientPattern)];
    char mRadGradPat[sizeof(RadialGradientPattern)];
    char mSurfPat[sizeof(SurfacePattern)];
  };

  Pattern *mOrigPattern;
  Pattern *mPattern;
};

DrawTargetRecording::DrawTargetRecording(DrawEventRecorder *aRecorder, DrawTarget *aDT)
  : mRecorder(static_cast<DrawEventRecorderPrivate*>(aRecorder))
  , mFinalDT(aDT)
{
  mRecorder->RecordEvent(RecordedDrawTargetCreation(this, mFinalDT->GetType(), mFinalDT->GetSize(), mFinalDT->GetFormat()));
  mFormat = mFinalDT->GetFormat();
}

DrawTargetRecording::~DrawTargetRecording()
{
  mRecorder->RecordEvent(RecordedDrawTargetDestruction(this));
}

void
DrawTargetRecording::FillRect(const Rect &aRect,
                              const Pattern &aPattern,
                              const DrawOptions &aOptions)
{
  mRecorder->RecordEvent(RecordedFillRect(this, aRect, aPattern, aOptions));
  mFinalDT->FillRect(aRect, *AdjustedPattern(aPattern), aOptions);
}

void
DrawTargetRecording::StrokeRect(const Rect &aRect,
                                const Pattern &aPattern,
                                const StrokeOptions &aStrokeOptions,
                                const DrawOptions &aOptions)
{
  mRecorder->RecordEvent(RecordedStrokeRect(this, aRect, aPattern, aStrokeOptions, aOptions));
  mFinalDT->StrokeRect(aRect, *AdjustedPattern(aPattern), aStrokeOptions, aOptions);
}

void
DrawTargetRecording::StrokeLine(const Point &aBegin,
                                const Point &aEnd,
                                const Pattern &aPattern,
                                const StrokeOptions &aStrokeOptions,
                                const DrawOptions &aOptions)
{
  mRecorder->RecordEvent(RecordedStrokeLine(this, aBegin, aEnd, aPattern, aStrokeOptions, aOptions));
  mFinalDT->StrokeLine(aBegin, aEnd, *AdjustedPattern(aPattern), aStrokeOptions, aOptions);
}

Path*
DrawTargetRecording::GetPathForPathRecording(const Path *aPath) const
{
  if (aPath->GetBackendType() != BACKEND_RECORDING) {
    return NULL;
  }

  return static_cast<const PathRecording*>(aPath)->mPath;
}

void
DrawTargetRecording::Fill(const Path *aPath,
                          const Pattern &aPattern,
                          const DrawOptions &aOptions)
{
  EnsureStored(aPath);

  mRecorder->RecordEvent(RecordedFill(this, const_cast<Path*>(aPath), aPattern, aOptions));
  mFinalDT->Fill(GetPathForPathRecording(aPath), *AdjustedPattern(aPattern), aOptions);
}

struct RecordingFontUserData
{
  void *refPtr;
  RefPtr<DrawEventRecorderPrivate> recorder;
};

void RecordingFontUserDataDestroyFunc(void *aUserData)
{
  RecordingFontUserData *userData =
    static_cast<RecordingFontUserData*>(aUserData);

  userData->recorder->RecordEvent(RecordedScaledFontDestruction(userData->refPtr));

  delete userData;
}

void
DrawTargetRecording::FillGlyphs(ScaledFont *aFont,
                                const GlyphBuffer &aBuffer,
                                const Pattern &aPattern,
                                const DrawOptions &aOptions,
                                const GlyphRenderingOptions *aRenderingOptions)
{
  if (!aFont->GetUserData(reinterpret_cast<UserDataKey*>(mRecorder.get()))) {
    mRecorder->RecordEvent(RecordedScaledFontCreation(aFont, aFont));
    RecordingFontUserData *userData = new RecordingFontUserData;
    userData->refPtr = aFont;
    userData->recorder = mRecorder;
    aFont->AddUserData(reinterpret_cast<UserDataKey*>(mRecorder.get()), userData, 
                       &RecordingFontUserDataDestroyFunc);
  }

  mRecorder->RecordEvent(RecordedFillGlyphs(this, aFont, aPattern, aOptions, aBuffer.mGlyphs, aBuffer.mNumGlyphs));
  mFinalDT->FillGlyphs(aFont, aBuffer, aPattern, aOptions, aRenderingOptions);
}

void
DrawTargetRecording::Mask(const Pattern &aSource,
                          const Pattern &aMask,
                          const DrawOptions &aOptions)
{
  mRecorder->RecordEvent(RecordedMask(this, aSource, aMask, aOptions));
  mFinalDT->Mask(*AdjustedPattern(aSource), *AdjustedPattern(aMask), aOptions);
}

void
DrawTargetRecording::Stroke(const Path *aPath,
                            const Pattern &aPattern,
                            const StrokeOptions &aStrokeOptions,
                            const DrawOptions &aOptions)
{
  EnsureStored(aPath);

  mRecorder->RecordEvent(RecordedStroke(this, const_cast<Path*>(aPath), aPattern, aStrokeOptions, aOptions));
  mFinalDT->Stroke(GetPathForPathRecording(aPath), *AdjustedPattern(aPattern), aStrokeOptions, aOptions);
}

TemporaryRef<SourceSurface>
DrawTargetRecording::Snapshot()
{
  RefPtr<SourceSurface> surf = mFinalDT->Snapshot();

  RefPtr<SourceSurface> retSurf = new SourceSurfaceRecording(surf, mRecorder);

  mRecorder->RecordEvent(RecordedSnapshot(retSurf, this));

  return retSurf;
}

void
DrawTargetRecording::DrawSurface(SourceSurface *aSurface,
                                 const Rect &aDest,
                                 const Rect &aSource,
                                 const DrawSurfaceOptions &aSurfOptions,
                                 const DrawOptions &aOptions)
{
  mRecorder->RecordEvent(RecordedDrawSurface(this, aSurface, aDest, aSource, aSurfOptions, aOptions));
  mFinalDT->DrawSurface(GetSourceSurface(aSurface), aDest, aSource, aSurfOptions, aOptions);
}

void
DrawTargetRecording::DrawSurfaceWithShadow(SourceSurface *aSurface,
                                           const Point &aDest,
                                           const Color &aColor,
                                           const Point &aOffset,
                                           Float aSigma,
                                           CompositionOp aOp)
{
  mRecorder->RecordEvent(RecordedDrawSurfaceWithShadow(this, aSurface, aDest, aColor, aOffset, aSigma, aOp));
  mFinalDT->DrawSurfaceWithShadow(GetSourceSurface(aSurface), aDest, aColor, aOffset, aSigma, aOp);
}

void
DrawTargetRecording::ClearRect(const Rect &aRect)
{
  mRecorder->RecordEvent(RecordedClearRect(this, aRect));
  mFinalDT->ClearRect(aRect);
}

void
DrawTargetRecording::CopySurface(SourceSurface *aSurface,
                                 const IntRect &aSourceRect,
                                 const IntPoint &aDestination)
{
  mRecorder->RecordEvent(RecordedCopySurface(this, aSurface, aSourceRect, aDestination));
  mFinalDT->CopySurface(GetSourceSurface(aSurface), aSourceRect, aDestination);
}

void
DrawTargetRecording::PushClip(const Path *aPath)
{
  EnsureStored(aPath);

  mRecorder->RecordEvent(RecordedPushClip(this, const_cast<Path*>(aPath)));
  mFinalDT->PushClip(GetPathForPathRecording(aPath));
}

void
DrawTargetRecording::PushClipRect(const Rect &aRect)
{
  mRecorder->RecordEvent(RecordedPushClipRect(this, aRect));
  mFinalDT->PushClipRect(aRect);
}

void
DrawTargetRecording::PopClip()
{
  mRecorder->RecordEvent(RecordedPopClip(this));
  mFinalDT->PopClip();
}

TemporaryRef<SourceSurface>
DrawTargetRecording::CreateSourceSurfaceFromData(unsigned char *aData,
                                                 const IntSize &aSize,
                                                 int32_t aStride,
                                                 SurfaceFormat aFormat) const
{
  RefPtr<SourceSurface> surf = mFinalDT->CreateSourceSurfaceFromData(aData, aSize, aStride, aFormat);

  RefPtr<SourceSurface> retSurf = new SourceSurfaceRecording(surf, mRecorder);

  mRecorder->RecordEvent(RecordedSourceSurfaceCreation(retSurf, aData, aStride, aSize, aFormat));

  return retSurf;
}

TemporaryRef<SourceSurface>
DrawTargetRecording::OptimizeSourceSurface(SourceSurface *aSurface) const
{
  RefPtr<SourceSurface> surf = mFinalDT->OptimizeSourceSurface(aSurface);

  RefPtr<SourceSurface> retSurf = new SourceSurfaceRecording(surf, mRecorder);

  RefPtr<DataSourceSurface> dataSurf = surf->GetDataSurface();

  if (!dataSurf) {
    // Let's try get it off the original surface.
    dataSurf = aSurface->GetDataSurface();
  }

  if (!dataSurf) {
    gfxWarning() << "Recording failed to record SourceSurface created from OptimizeSourceSurface";
    // Insert a bogus source surface.
    uint8_t *sourceData = new uint8_t[surf->GetSize().width * surf->GetSize().height * BytesPerPixel(surf->GetFormat())];
    memset(sourceData, 0, surf->GetSize().width * surf->GetSize().height * BytesPerPixel(surf->GetFormat()));
    mRecorder->RecordEvent(
      RecordedSourceSurfaceCreation(retSurf, sourceData,
                                    surf->GetSize().width * BytesPerPixel(surf->GetFormat()),
                                    surf->GetSize(), surf->GetFormat()));
    delete [] sourceData;
  } else {
    mRecorder->RecordEvent(
      RecordedSourceSurfaceCreation(retSurf, dataSurf->GetData(), dataSurf->Stride(),
                                    dataSurf->GetSize(), dataSurf->GetFormat()));
  }

  return retSurf;
}

TemporaryRef<SourceSurface>
DrawTargetRecording::CreateSourceSurfaceFromNativeSurface(const NativeSurface &aSurface) const
{
  RefPtr<SourceSurface> surf = mFinalDT->CreateSourceSurfaceFromNativeSurface(aSurface);

  RefPtr<SourceSurface> retSurf = new SourceSurfaceRecording(surf, mRecorder);

  RefPtr<DataSourceSurface> dataSurf = surf->GetDataSurface();

  if (!dataSurf) {
    gfxWarning() << "Recording failed to record SourceSurface created from OptimizeSourceSurface";
    // Insert a bogus source surface.
    uint8_t *sourceData = new uint8_t[surf->GetSize().width * surf->GetSize().height * BytesPerPixel(surf->GetFormat())];
    memset(sourceData, 0, surf->GetSize().width * surf->GetSize().height * BytesPerPixel(surf->GetFormat()));
    mRecorder->RecordEvent(
      RecordedSourceSurfaceCreation(retSurf, sourceData,
                                    surf->GetSize().width * BytesPerPixel(surf->GetFormat()),
                                    surf->GetSize(), surf->GetFormat()));
    delete [] sourceData;
  } else {
    mRecorder->RecordEvent(
      RecordedSourceSurfaceCreation(retSurf, dataSurf->GetData(), dataSurf->Stride(),
                                    dataSurf->GetSize(), dataSurf->GetFormat()));
  }

  return retSurf;
}

TemporaryRef<DrawTarget>
DrawTargetRecording::CreateSimilarDrawTarget(const IntSize &aSize, SurfaceFormat aFormat) const
{
  RefPtr<DrawTarget> dt = mFinalDT->CreateSimilarDrawTarget(aSize, aFormat);

  RefPtr<DrawTarget> retDT = new DrawTargetRecording(mRecorder.get(), dt);

  return retDT;
}

TemporaryRef<PathBuilder>
DrawTargetRecording::CreatePathBuilder(FillRule aFillRule) const
{
  RefPtr<PathBuilder> builder = mFinalDT->CreatePathBuilder(aFillRule);
  return new PathBuilderRecording(builder, aFillRule);
}

TemporaryRef<GradientStops>
DrawTargetRecording::CreateGradientStops(GradientStop *aStops,
                                         uint32_t aNumStops,
                                         ExtendMode aExtendMode) const
{
  RefPtr<GradientStops> stops = mFinalDT->CreateGradientStops(aStops, aNumStops, aExtendMode);

  RefPtr<GradientStops> retStops = new GradientStopsRecording(stops, mRecorder);

  mRecorder->RecordEvent(RecordedGradientStopsCreation(retStops, aStops, aNumStops, aExtendMode));

  return retStops;
}

void
DrawTargetRecording::SetTransform(const Matrix &aTransform)
{
  mRecorder->RecordEvent(RecordedSetTransform(this, aTransform));
  DrawTarget::SetTransform(aTransform);
  mFinalDT->SetTransform(aTransform);
}

void
DrawTargetRecording::EnsureStored(const Path *aPath)
{
  if (!mRecorder->HasStoredPath(aPath)) {
    if (aPath->GetBackendType() != BACKEND_RECORDING) {
      gfxWarning() << "Cannot record this fill path properly!";
    } else {
      PathRecording *recPath = const_cast<PathRecording*>(static_cast<const PathRecording*>(aPath));
      mRecorder->RecordEvent(RecordedPathCreation(recPath));
      mRecorder->AddStoredPath(aPath);
      recPath->mStoredRecorders.push_back(mRecorder);
    }
  }
}

}
}
