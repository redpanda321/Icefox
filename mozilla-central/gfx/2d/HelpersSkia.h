/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_HELPERSSKIA_H_
#define MOZILLA_GFX_HELPERSSKIA_H_

#include "2D.h"
#include "skia/SkCanvas.h"
#include "skia/SkDashPathEffect.h"
#include "skia/SkShader.h"
#include "mozilla/Assertions.h"
#include <vector>

namespace mozilla {
namespace gfx {

static inline SkBitmap::Config
GfxFormatToSkiaConfig(SurfaceFormat format)
{
  switch (format)
  {
    case FORMAT_B8G8R8A8:
      return SkBitmap::kARGB_8888_Config;
    case FORMAT_B8G8R8X8:
      // We probably need to do something here.
      return SkBitmap::kARGB_8888_Config;
    case FORMAT_R5G6B5:
      return SkBitmap::kRGB_565_Config;
    case FORMAT_A8:
      return SkBitmap::kA8_Config;

  }

  return SkBitmap::kARGB_8888_Config;
}

static inline void
GfxMatrixToSkiaMatrix(const Matrix& mat, SkMatrix& retval)
{
    retval.setAll(SkFloatToScalar(mat._11), SkFloatToScalar(mat._21), SkFloatToScalar(mat._31),
                  SkFloatToScalar(mat._12), SkFloatToScalar(mat._22), SkFloatToScalar(mat._32),
                  0, 0, SK_Scalar1);
}

static inline SkPaint::Cap
CapStyleToSkiaCap(CapStyle aCap)
{
  switch (aCap)
  {
    case CAP_BUTT:
      return SkPaint::kButt_Cap;
    case CAP_ROUND:
      return SkPaint::kRound_Cap;
    case CAP_SQUARE:
      return SkPaint::kSquare_Cap;
  }
  return SkPaint::kDefault_Cap;
}

static inline SkPaint::Join
JoinStyleToSkiaJoin(JoinStyle aJoin)
{
  switch (aJoin)
  {
    case JOIN_BEVEL:
      return SkPaint::kBevel_Join;
    case JOIN_ROUND:
      return SkPaint::kRound_Join;
    case JOIN_MITER:
    case JOIN_MITER_OR_BEVEL:
      return SkPaint::kMiter_Join;
  }
  return SkPaint::kDefault_Join;
}

static inline bool
StrokeOptionsToPaint(SkPaint& aPaint, const StrokeOptions &aOptions)
{
  // Skia renders 0 width strokes with a width of 1 (and in black),
  // so we should just skip the draw call entirely.
  if (!aOptions.mLineWidth) {
    return false;
  }
  aPaint.setStrokeWidth(SkFloatToScalar(aOptions.mLineWidth));
  aPaint.setStrokeMiter(SkFloatToScalar(aOptions.mMiterLimit));
  aPaint.setStrokeCap(CapStyleToSkiaCap(aOptions.mLineCap));
  aPaint.setStrokeJoin(JoinStyleToSkiaJoin(aOptions.mLineJoin));

  if (aOptions.mDashLength > 0) {
    // Skia only supports dash arrays that are multiples of 2.
    uint32_t dashCount;

    if (aOptions.mDashLength % 2 == 0) {
      dashCount = aOptions.mDashLength;
    } else {
      dashCount = aOptions.mDashLength * 2;
    }

    std::vector<SkScalar> pattern;
    pattern.resize(dashCount);

    for (uint32_t i = 0; i < dashCount; i++) {
      pattern[i] = SkFloatToScalar(aOptions.mDashPattern[i % aOptions.mDashLength]);
    }

    SkDashPathEffect* dash = new SkDashPathEffect(&pattern.front(),
                                                  dashCount, 
                                                  SkFloatToScalar(aOptions.mDashOffset));
    SkSafeUnref(aPaint.setPathEffect(dash));
  }

  aPaint.setStyle(SkPaint::kStroke_Style);
  return true;
}

static inline void
ConvertBGRXToBGRA(unsigned char* aData, const IntSize &aSize, int32_t aStride)
{
    uint32_t* pixel = reinterpret_cast<uint32_t*>(aData);

    for (int row = 0; row < aSize.height; ++row) {
        for (int column = 0; column < aSize.width; ++column) {
            pixel[column] |= 0xFF000000;
        }
        pixel += (aStride/4);
    }
}

static inline SkXfermode::Mode
GfxOpToSkiaOp(CompositionOp op)
{
  switch (op)
  {
    case OP_OVER:
      return SkXfermode::kSrcOver_Mode;
    case OP_ADD:
      return SkXfermode::kPlus_Mode;
    case OP_ATOP:
      return SkXfermode::kSrcATop_Mode;
    case OP_OUT:
      return SkXfermode::kSrcOut_Mode;
    case OP_IN:
      return SkXfermode::kSrcIn_Mode;
    case OP_SOURCE:
      return SkXfermode::kSrc_Mode;
    case OP_DEST_IN:
      return SkXfermode::kDstIn_Mode;
    case OP_DEST_OUT:
      return SkXfermode::kDstOut_Mode;
    case OP_DEST_OVER:
      return SkXfermode::kDstOver_Mode;
    case OP_DEST_ATOP:
      return SkXfermode::kDstATop_Mode;
    case OP_XOR:
      return SkXfermode::kXor_Mode;
    case OP_COUNT:
      return SkXfermode::kSrcOver_Mode;
  }
  return SkXfermode::kSrcOver_Mode;
}

static inline SkColor ColorToSkColor(const Color &color, Float aAlpha)
{
  //XXX: do a better job converting to int
  return SkColorSetARGB(U8CPU(color.a*aAlpha*255.0), U8CPU(color.r*255.0),
                        U8CPU(color.g*255.0), U8CPU(color.b*255.0));
}

static inline SkRect
RectToSkRect(const Rect& aRect)
{
  return SkRect::MakeXYWH(SkFloatToScalar(aRect.x), SkFloatToScalar(aRect.y), 
                          SkFloatToScalar(aRect.width), SkFloatToScalar(aRect.height));
}

static inline SkRect
IntRectToSkRect(const IntRect& aRect)
{
  return SkRect::MakeXYWH(SkIntToScalar(aRect.x), SkIntToScalar(aRect.y), 
                          SkIntToScalar(aRect.width), SkIntToScalar(aRect.height));
}

static inline SkIRect
RectToSkIRect(const Rect& aRect)
{
  return SkIRect::MakeXYWH(int32_t(aRect.x), int32_t(aRect.y),
                           int32_t(aRect.width), int32_t(aRect.height));
}

static inline SkIRect
IntRectToSkIRect(const IntRect& aRect)
{
  return SkIRect::MakeXYWH(aRect.x, aRect.y, aRect.width, aRect.height);
}

static inline SkShader::TileMode
ExtendModeToTileMode(ExtendMode aMode)
{
  switch (aMode)
  {
    case EXTEND_CLAMP:
      return SkShader::kClamp_TileMode;
    case EXTEND_REPEAT:
      return SkShader::kRepeat_TileMode;
    case EXTEND_REFLECT:
      return SkShader::kMirror_TileMode;
  }
  return SkShader::kClamp_TileMode;
}

}
}

#endif /* MOZILLA_GFX_HELPERSSKIA_H_ */
