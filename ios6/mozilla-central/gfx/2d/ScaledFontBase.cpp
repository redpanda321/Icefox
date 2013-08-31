/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScaledFontBase.h"

#ifdef USE_SKIA
#include "PathSkia.h"
#include "skia/SkPaint.h"
#include "skia/SkPath.h"
#endif

#ifdef USE_CAIRO
#include "PathCairo.h"
#endif

#include <vector>
#include <cmath>

using namespace std;

namespace mozilla {
namespace gfx {

ScaledFontBase::~ScaledFontBase()
{
#ifdef USE_SKIA
  SkSafeUnref(mTypeface);
#endif
#ifdef USE_CAIRO
  cairo_scaled_font_destroy(mScaledFont);
#endif
}

ScaledFontBase::ScaledFontBase(Float aSize)
  : mSize(aSize)
{
#ifdef USE_SKIA
  mTypeface = nullptr;
#endif
#ifdef USE_CAIRO
  mScaledFont = nullptr;
#endif
}

TemporaryRef<Path>
ScaledFontBase::GetPathForGlyphs(const GlyphBuffer &aBuffer, const DrawTarget *aTarget)
{
#ifdef USE_SKIA
  if (aTarget->GetType() == BACKEND_SKIA) {
    SkPaint paint;
    paint.setTypeface(GetSkTypeface());
    paint.setTextEncoding(SkPaint::kGlyphID_TextEncoding);
    paint.setTextSize(SkFloatToScalar(mSize));

    std::vector<uint16_t> indices;
    std::vector<SkPoint> offsets;
    indices.resize(aBuffer.mNumGlyphs);
    offsets.resize(aBuffer.mNumGlyphs);

    for (unsigned int i = 0; i < aBuffer.mNumGlyphs; i++) {
      indices[i] = aBuffer.mGlyphs[i].mIndex;
      offsets[i].fX = SkFloatToScalar(aBuffer.mGlyphs[i].mPosition.x);
      offsets[i].fY = SkFloatToScalar(aBuffer.mGlyphs[i].mPosition.y);
    }

    SkPath path;
    paint.getPosTextPath(&indices.front(), aBuffer.mNumGlyphs*2, &offsets.front(), &path);
    return new PathSkia(path, FILL_WINDING);
  }
#endif
#ifdef USE_CAIRO
  if (aTarget->GetType() == BACKEND_CAIRO) {
    MOZ_ASSERT(mScaledFont);

    RefPtr<PathBuilder> builder_iface = aTarget->CreatePathBuilder();
    PathBuilderCairo* builder = static_cast<PathBuilderCairo*>(builder_iface.get());

    // Manually build the path for the PathBuilder.
    RefPtr<CairoPathContext> context = builder->GetPathContext();

    cairo_set_scaled_font(*context, mScaledFont);

    // Convert our GlyphBuffer into an array of Cairo glyphs.
    std::vector<cairo_glyph_t> glyphs(aBuffer.mNumGlyphs);
    for (uint32_t i = 0; i < aBuffer.mNumGlyphs; ++i) {
      glyphs[i].index = aBuffer.mGlyphs[i].mIndex;
      glyphs[i].x = aBuffer.mGlyphs[i].mPosition.x;
      glyphs[i].y = aBuffer.mGlyphs[i].mPosition.y;
    }

    cairo_glyph_path(*context, &glyphs[0], aBuffer.mNumGlyphs);

    return builder->Finish();
  }
#endif
  return nullptr;
}

void
ScaledFontBase::CopyGlyphsToBuilder(const GlyphBuffer &aBuffer, PathBuilder *aBuilder)
{
  // XXX - implement me
  MOZ_ASSERT(false);
  return;
}

#ifdef USE_CAIRO
void
ScaledFontBase::SetCairoScaledFont(cairo_scaled_font_t* font)
{
  MOZ_ASSERT(!mScaledFont);

  mScaledFont = font;
  cairo_scaled_font_reference(mScaledFont);
}
#endif

}
}
