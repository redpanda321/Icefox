/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DrawTargetD2D.h"
#include "SourceSurfaceD2D.h"
#include "SourceSurfaceD2DTarget.h"
#include "ShadersD2D.h"
#include "PathD2D.h"
#include "GradientStopsD2D.h"
#include "ScaledFontDWrite.h"
#include "ImageScaling.h"
#include "Logging.h"
#include "Tools.h"
#include <algorithm>

#include <dwrite.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef HRESULT (WINAPI*D2D1CreateFactoryFunc)(
    D2D1_FACTORY_TYPE factoryType,
    REFIID iid,
    CONST D2D1_FACTORY_OPTIONS *pFactoryOptions,
    void **factory
);

typedef HRESULT (WINAPI*D3D10CreateEffectFromMemoryFunc)(
    void *pData,
    SIZE_T DataLength,
    UINT FXFlags,
    ID3D10Device *pDevice,
    ID3D10EffectPool *pEffectPool,
    ID3D10Effect **ppEffect
);

typedef HRESULT (WINAPI*DWriteCreateFactoryFunc)(
  DWRITE_FACTORY_TYPE factoryType,
  REFIID iid,
  IUnknown **factory
);

using namespace std;

namespace mozilla {
namespace gfx {

struct Vertex {
  float x;
  float y;
};

ID2D1Factory *DrawTargetD2D::mFactory;
IDWriteFactory *DrawTargetD2D::mDWriteFactory;
uint64_t DrawTargetD2D::mVRAMUsageDT;
uint64_t DrawTargetD2D::mVRAMUsageSS;

// Helper class to restore surface contents that was clipped out but may have
// been altered by a drawing call.
class AutoSaveRestoreClippedOut
{
public:
  AutoSaveRestoreClippedOut(DrawTargetD2D *aDT)
    : mDT(aDT)
  {}

  void Save() {
    if (!mDT->mPushedClips.size()) {
      return;
    }

    mDT->Flush();

    RefPtr<ID3D10Texture2D> tmpTexture;
    IntSize size = mDT->mSize;
    SurfaceFormat format = mDT->mFormat;

    CD3D10_TEXTURE2D_DESC desc(DXGIFormat(format), size.width, size.height,
                               1, 1);
    desc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;

    HRESULT hr = mDT->mDevice->CreateTexture2D(&desc, NULL, byRef(tmpTexture));
    if (FAILED(hr)) {
      gfxWarning() << "Failed to create temporary texture to hold surface data.";
    }
    mDT->mDevice->CopyResource(tmpTexture, mDT->mTexture);

    D2D1_BITMAP_PROPERTIES props =
      D2D1::BitmapProperties(D2D1::PixelFormat(DXGIFormat(format),
                             AlphaMode(format)));

    RefPtr<IDXGISurface> surf;

    tmpTexture->QueryInterface((IDXGISurface**)byRef(surf));

    hr = mDT->mRT->CreateSharedBitmap(IID_IDXGISurface, surf,
                                      &props, byRef(mOldSurfBitmap));

    if (FAILED(hr)) {
      gfxWarning() << "Failed to create shared bitmap for old surface.";
    }

    mClippedArea = mDT->GetClippedGeometry();
  }

  ID2D1Factory *factory() { return mDT->factory(); }

  ~AutoSaveRestoreClippedOut()
  {
    if (!mOldSurfBitmap) {
      return;
    }

    ID2D1RenderTarget *rt = mDT->mRT;

    // Write the area that was clipped out back to the surface. This all
    // happens in device space.
    rt->SetTransform(D2D1::IdentityMatrix());
    mDT->mTransformDirty = true;

    RefPtr<ID2D1RectangleGeometry> rectGeom;
    factory()->CreateRectangleGeometry(
      D2D1::RectF(0, 0, float(mDT->mSize.width), float(mDT->mSize.height)),
      byRef(rectGeom));

    RefPtr<ID2D1PathGeometry> invClippedArea;
    factory()->CreatePathGeometry(byRef(invClippedArea));
    RefPtr<ID2D1GeometrySink> sink;
    invClippedArea->Open(byRef(sink));

    rectGeom->CombineWithGeometry(mClippedArea, D2D1_COMBINE_MODE_EXCLUDE, NULL, sink);
    sink->Close();

    RefPtr<ID2D1BitmapBrush> brush;
    rt->CreateBitmapBrush(mOldSurfBitmap, D2D1::BitmapBrushProperties(), D2D1::BrushProperties(), byRef(brush));                   

    rt->FillGeometry(invClippedArea, brush);
  }

private:

  DrawTargetD2D *mDT;  

  // If we have an operator unbound by the source, this will contain a bitmap
  // with the old dest surface data.
  RefPtr<ID2D1Bitmap> mOldSurfBitmap;
  // This contains the area drawing is clipped to.
  RefPtr<ID2D1Geometry> mClippedArea;
};

DrawTargetD2D::DrawTargetD2D()
  : mCurrentCachedLayer(0)
  , mClipsArePushed(false)
  , mPrivateData(NULL)
{
}

DrawTargetD2D::~DrawTargetD2D()
{
  if (mRT) {  
    PopAllClips();

    mRT->EndDraw();

    mVRAMUsageDT -= GetByteSize();
  }
  if (mTempRT) {
    mTempRT->EndDraw();

    mVRAMUsageDT -= GetByteSize();
  }

  if (mSnapshot) {
    // We may hold the only reference. MarkIndependent will clear mSnapshot;
    // keep the snapshot object alive so it doesn't get destroyed while
    // MarkIndependent is running.
    RefPtr<SourceSurfaceD2DTarget> deathGrip = mSnapshot;
    // mSnapshot can be treated as independent of this DrawTarget since we know
    // this DrawTarget won't change again.
    deathGrip->MarkIndependent();
    // mSnapshot will be cleared now.
  }

  for (int i = 0; i < kLayerCacheSize; i++) {
    if (mCachedLayers[i]) {
      mCachedLayers[i] = NULL;
      mVRAMUsageDT -= GetByteSize();
    }
  }

  // Targets depending on us can break that dependency, since we're obviously not going to
  // be modified in the future.
  for (TargetSet::iterator iter = mDependentTargets.begin();
       iter != mDependentTargets.end(); iter++) {
    (*iter)->mDependingOnTargets.erase(this);
  }
  // Our dependencies on other targets no longer matter.
  for (TargetSet::iterator iter = mDependingOnTargets.begin();
       iter != mDependingOnTargets.end(); iter++) {
    (*iter)->mDependentTargets.erase(this);
  }
}

/*
 * DrawTarget Implementation
 */
TemporaryRef<SourceSurface>
DrawTargetD2D::Snapshot()
{
  if (!mSnapshot) {
    mSnapshot = new SourceSurfaceD2DTarget(this, mTexture, mFormat);
    Flush();
  }

  return mSnapshot;
}

void
DrawTargetD2D::Flush()
{
  PopAllClips();

  HRESULT hr = mRT->Flush();

  if (FAILED(hr)) {
    gfxWarning() << "Error reported when trying to flush D2D rendertarget. Code: " << hr;
  }

  // We no longer depend on any target.
  for (TargetSet::iterator iter = mDependingOnTargets.begin();
       iter != mDependingOnTargets.end(); iter++) {
    (*iter)->mDependentTargets.erase(this);
  }
  mDependingOnTargets.clear();
}

void
DrawTargetD2D::AddDependencyOnSource(SourceSurfaceD2DTarget* aSource)
{
  if (aSource->mDrawTarget && !mDependingOnTargets.count(aSource->mDrawTarget)) {
    aSource->mDrawTarget->mDependentTargets.insert(this);
    mDependingOnTargets.insert(aSource->mDrawTarget);
  }
}

void
DrawTargetD2D::DrawSurface(SourceSurface *aSurface,
                           const Rect &aDest,
                           const Rect &aSource,
                           const DrawSurfaceOptions &aSurfOptions,
                           const DrawOptions &aOptions)
{
  RefPtr<ID2D1Bitmap> bitmap;

  ID2D1RenderTarget *rt = GetRTForOperation(aOptions.mCompositionOp, ColorPattern(Color()));
  
  PrepareForDrawing(rt);

  rt->SetAntialiasMode(D2DAAMode(aOptions.mAntialiasMode));

  Rect srcRect = aSource;

  switch (aSurface->GetType()) {

  case SURFACE_D2D1_BITMAP:
    {
      SourceSurfaceD2D *srcSurf = static_cast<SourceSurfaceD2D*>(aSurface);
      bitmap = srcSurf->GetBitmap();

      if (!bitmap) {
        return;
      }
    }
    break;
  case SURFACE_D2D1_DRAWTARGET:
    {
      SourceSurfaceD2DTarget *srcSurf = static_cast<SourceSurfaceD2DTarget*>(aSurface);
      bitmap = srcSurf->GetBitmap(mRT);
      AddDependencyOnSource(srcSurf);
    }
    break;
  case SURFACE_DATA:
    {
      DataSourceSurface *srcSurf = static_cast<DataSourceSurface*>(aSurface);
      if (aSource.width > rt->GetMaximumBitmapSize() ||
          aSource.height > rt->GetMaximumBitmapSize()) {
        gfxDebug() << "Bitmap source larger than texture size specified. DrawBitmap will silently fail.";
        // Don't know how to deal with this yet.
        return;
      }

      int stride = srcSurf->Stride();

      unsigned char *data = srcSurf->GetData() +
                            (uint32_t)aSource.y * stride +
                            (uint32_t)aSource.x * BytesPerPixel(srcSurf->GetFormat());

      D2D1_BITMAP_PROPERTIES props =
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGIFormat(srcSurf->GetFormat()), AlphaMode(srcSurf->GetFormat())));
      mRT->CreateBitmap(D2D1::SizeU(UINT32(aSource.width), UINT32(aSource.height)), data, stride, props, byRef(bitmap));

      srcRect.x -= (uint32_t)aSource.x;
      srcRect.y -= (uint32_t)aSource.y;
    }
    break;
  default:
    break;
  }

  rt->DrawBitmap(bitmap, D2DRect(aDest), aOptions.mAlpha, D2DFilter(aSurfOptions.mFilter), D2DRect(srcRect));

  FinalizeRTForOperation(aOptions.mCompositionOp, ColorPattern(Color()), aDest);
}

void
DrawTargetD2D::DrawSurfaceWithShadow(SourceSurface *aSurface,
                                     const Point &aDest,
                                     const Color &aColor,
                                     const Point &aOffset,
                                     Float aSigma,
                                     CompositionOp aOperator)
{
  RefPtr<ID3D10ShaderResourceView> srView = NULL;
  if (aSurface->GetType() != SURFACE_D2D1_DRAWTARGET) {
    return;
  }

  // XXX - This function is way too long, it should be split up soon to make
  // it more graspable!

  Flush();

  AutoSaveRestoreClippedOut restoreClippedOut(this);

  if (!IsOperatorBoundByMask(aOperator)) {
    restoreClippedOut.Save();
  }

  srView = static_cast<SourceSurfaceD2DTarget*>(aSurface)->GetSRView();

  EnsureViews();

  if (!mTempRTView) {
    // This view is only needed in this path.
    HRESULT hr = mDevice->CreateRenderTargetView(mTempTexture, NULL, byRef(mTempRTView));

    if (FAILED(hr)) {
      gfxWarning() << "Failure to create RenderTargetView. Code: " << hr;
      return;
    }
  }


  RefPtr<ID3D10RenderTargetView> destRTView = mRTView;
  RefPtr<ID3D10Texture2D> destTexture;
  HRESULT hr;

  RefPtr<ID3D10Texture2D> maskTexture;
  RefPtr<ID3D10ShaderResourceView> maskSRView;
  if (mPushedClips.size()) {
    EnsureClipMaskTexture();

    mDevice->CreateShaderResourceView(mCurrentClipMaskTexture, NULL, byRef(maskSRView));
  }

  IntSize srcSurfSize;
  ID3D10RenderTargetView *rtViews;
  D3D10_VIEWPORT viewport;

  UINT stride = sizeof(Vertex);
  UINT offset = 0;
  ID3D10Buffer *buff = mPrivateData->mVB;

  mDevice->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  mDevice->IASetVertexBuffers(0, 1, &buff, &stride, &offset);
  mDevice->IASetInputLayout(mPrivateData->mInputLayout);

  mPrivateData->mEffect->GetVariableByName("QuadDesc")->AsVector()->
    SetFloatVector(ShaderConstantRectD3D10(-1.0f, 1.0f, 2.0f, -2.0f));
  mPrivateData->mEffect->GetVariableByName("TexCoords")->AsVector()->
    SetFloatVector(ShaderConstantRectD3D10(0, 0, 1.0f, 1.0f));

  // If we create a downsampled source surface we need to correct aOffset for that.
  Point correctedOffset = aOffset + aDest;

  // The 'practical' scaling factors.
  Float dsFactorX = 1.0f;
  Float dsFactorY = 1.0f;

  if (aSigma > 1.7f) {
    // In this case 9 samples of our original will not cover it. Generate the
    // mip levels for the original and create a downsampled version from
    // them. We generate a version downsampled so that a kernel for a sigma
    // of 1.7 will produce the right results.
    float blurWeights[9] = { 0.234671f, 0.197389f, 0.197389f, 0.117465f, 0.117465f, 0.049456f, 0.049456f, 0.014732f, 0.014732f };
    mPrivateData->mEffect->GetVariableByName("BlurWeights")->SetRawValue(blurWeights, 0, sizeof(blurWeights));
    
    CD3D10_TEXTURE2D_DESC desc(DXGI_FORMAT_B8G8R8A8_UNORM,
                               aSurface->GetSize().width,
                               aSurface->GetSize().height);
    desc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D10_RESOURCE_MISC_GENERATE_MIPS;

    RefPtr<ID3D10Texture2D> mipTexture;
    hr = mDevice->CreateTexture2D(&desc, NULL, byRef(mipTexture));

    if (FAILED(hr)) {
      gfxWarning() << "Failure to create temporary texture. Size: " <<
        aSurface->GetSize() << " Code: " << hr;
      return;
    }

    IntSize dsSize = IntSize(int32_t(aSurface->GetSize().width * (1.7f / aSigma)),
                             int32_t(aSurface->GetSize().height * (1.7f / aSigma)));

    if (dsSize.width < 1) {
      dsSize.width = 1;
    }
    if (dsSize.height < 1) {
      dsSize.height = 1;
    }

    dsFactorX = dsSize.width / Float(aSurface->GetSize().width);
    dsFactorY = dsSize.height / Float(aSurface->GetSize().height);
    correctedOffset.x *= dsFactorX;
    correctedOffset.y *= dsFactorY;

    desc = CD3D10_TEXTURE2D_DESC(DXGI_FORMAT_B8G8R8A8_UNORM,
                                 dsSize.width,
                                 dsSize.height, 1, 1);
    desc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;
    RefPtr<ID3D10Texture2D> tmpDSTexture;
    hr = mDevice->CreateTexture2D(&desc, NULL, byRef(tmpDSTexture));

    if (FAILED(hr)) {
      gfxWarning() << "Failure to create temporary texture. Size: " << dsSize << " Code: " << hr;
      return;
    }

    D3D10_BOX box;
    box.left = box.top = box.front = 0;
    box.back = 1;
    box.right = aSurface->GetSize().width;
    box.bottom = aSurface->GetSize().height;
    mDevice->CopySubresourceRegion(mipTexture, 0, 0, 0, 0, static_cast<SourceSurfaceD2DTarget*>(aSurface)->mTexture, 0, &box);

    mDevice->CreateShaderResourceView(mipTexture, NULL,  byRef(srView));
    mDevice->GenerateMips(srView);

    RefPtr<ID3D10RenderTargetView> dsRTView;
    RefPtr<ID3D10ShaderResourceView> dsSRView;
    mDevice->CreateRenderTargetView(tmpDSTexture, NULL,  byRef(dsRTView));
    mDevice->CreateShaderResourceView(tmpDSTexture, NULL,  byRef(dsSRView));

    // We're not guaranteed the texture we created will be empty, we've
    // seen old content at least on NVidia drivers.
    float color[4] = { 0, 0, 0, 0 };
    mDevice->ClearRenderTargetView(dsRTView, color);

    rtViews = dsRTView;
    mDevice->OMSetRenderTargets(1, &rtViews, NULL);

    viewport.MaxDepth = 1;
    viewport.MinDepth = 0;
    viewport.Height = dsSize.height;
    viewport.Width = dsSize.width;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;

    mDevice->RSSetViewports(1, &viewport);
    mPrivateData->mEffect->GetVariableByName("tex")->AsShaderResource()->SetResource(srView);
    mPrivateData->mEffect->GetTechniqueByName("SampleTexture")->
      GetPassByIndex(0)->Apply(0);

    mDevice->OMSetBlendState(GetBlendStateForOperator(OP_OVER), NULL, 0xffffffff);

    mDevice->Draw(4, 0);
    
    srcSurfSize = dsSize;

    srView = dsSRView;
  } else {
    // In this case generate a kernel to draw the blur directly to the temp
    // surf in one direction and to final in the other.
    float blurWeights[9];

    float normalizeFactor = 1.0f;
    if (aSigma != 0) {
      normalizeFactor = 1.0f / Float(sqrt(2 * M_PI * pow(aSigma, 2)));
    }

    blurWeights[0] = normalizeFactor;

    // XXX - We should actually optimize for Sigma = 0 here. We could use a
    // much simpler shader and save a lot of texture lookups.
    for (int i = 1; i < 9; i += 2) {
      if (aSigma != 0) {
        blurWeights[i] = blurWeights[i + 1] = normalizeFactor *
          exp(-pow(float((i + 1) / 2), 2) / (2 * pow(aSigma, 2)));
      } else {
        blurWeights[i] = blurWeights[i + 1] = 0;
      }
    }
    
    mPrivateData->mEffect->GetVariableByName("BlurWeights")->SetRawValue(blurWeights, 0, sizeof(blurWeights));

    viewport.MaxDepth = 1;
    viewport.MinDepth = 0;
    viewport.Height = aSurface->GetSize().height;
    viewport.Width = aSurface->GetSize().width;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;

    mDevice->RSSetViewports(1, &viewport);

    srcSurfSize = aSurface->GetSize();
  }

  // We may need to draw to a different intermediate surface if our temp
  // texture isn't big enough.
  bool needBiggerTemp = srcSurfSize.width > mSize.width ||
                        srcSurfSize.height > mSize.height;

  RefPtr<ID3D10RenderTargetView> tmpRTView;
  RefPtr<ID3D10ShaderResourceView> tmpSRView;
  RefPtr<ID3D10Texture2D> tmpTexture;
  
  IntSize tmpSurfSize = mSize;

  if (!needBiggerTemp) {
    tmpRTView = mTempRTView;
    tmpSRView = mSRView;

    // There could still be content here!
    float color[4] = { 0, 0, 0, 0 };
    mDevice->ClearRenderTargetView(tmpRTView, color);
  } else {
    CD3D10_TEXTURE2D_DESC desc(DXGI_FORMAT_B8G8R8A8_UNORM,
                               srcSurfSize.width,
                               srcSurfSize.height,
                               1, 1);
    desc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;

    mDevice->CreateTexture2D(&desc, NULL,  byRef(tmpTexture));
    mDevice->CreateRenderTargetView(tmpTexture, NULL,  byRef(tmpRTView));
    mDevice->CreateShaderResourceView(tmpTexture, NULL,  byRef(tmpSRView));

    tmpSurfSize = srcSurfSize;
  }

  rtViews = tmpRTView;
  mDevice->OMSetRenderTargets(1, &rtViews, NULL);

  mPrivateData->mEffect->GetVariableByName("tex")->AsShaderResource()->SetResource(srView);

  // Premultiplied!
  float shadowColor[4] = { aColor.r * aColor.a, aColor.g * aColor.a,
                           aColor.b * aColor.a, aColor.a };
  mPrivateData->mEffect->GetVariableByName("ShadowColor")->AsVector()->
    SetFloatVector(shadowColor);

  float pixelOffset = 1.0f / float(srcSurfSize.width);
  float blurOffsetsH[9] = { 0, pixelOffset, -pixelOffset,
                            2.0f * pixelOffset, -2.0f * pixelOffset,
                            3.0f * pixelOffset, -3.0f * pixelOffset,
                            4.0f * pixelOffset, - 4.0f * pixelOffset };

  pixelOffset = 1.0f / float(tmpSurfSize.height);
  float blurOffsetsV[9] = { 0, pixelOffset, -pixelOffset,
                            2.0f * pixelOffset, -2.0f * pixelOffset,
                            3.0f * pixelOffset, -3.0f * pixelOffset,
                            4.0f * pixelOffset, - 4.0f * pixelOffset };

  mPrivateData->mEffect->GetVariableByName("BlurOffsetsH")->
    SetRawValue(blurOffsetsH, 0, sizeof(blurOffsetsH));
  mPrivateData->mEffect->GetVariableByName("BlurOffsetsV")->
    SetRawValue(blurOffsetsV, 0, sizeof(blurOffsetsV));

  mPrivateData->mEffect->GetTechniqueByName("SampleTextureWithShadow")->
    GetPassByIndex(0)->Apply(0);

  mDevice->Draw(4, 0);

  viewport.MaxDepth = 1;
  viewport.MinDepth = 0;
  viewport.Height = mSize.height;
  viewport.Width = mSize.width;
  viewport.TopLeftX = 0;
  viewport.TopLeftY = 0;

  mDevice->RSSetViewports(1, &viewport);

  mPrivateData->mEffect->GetVariableByName("tex")->AsShaderResource()->SetResource(tmpSRView);

  rtViews = destRTView;
  mDevice->OMSetRenderTargets(1, &rtViews, NULL);

  Point shadowDest = aDest + aOffset;

  mPrivateData->mEffect->GetVariableByName("QuadDesc")->AsVector()->
    SetFloatVector(ShaderConstantRectD3D10(-1.0f + ((shadowDest.x / mSize.width) * 2.0f),
                                           1.0f - (shadowDest.y / mSize.height * 2.0f),
                                           (Float(aSurface->GetSize().width) / mSize.width) * 2.0f,
                                           (-Float(aSurface->GetSize().height) / mSize.height) * 2.0f));
  mPrivateData->mEffect->GetVariableByName("TexCoords")->AsVector()->
    SetFloatVector(ShaderConstantRectD3D10(0, 0, Float(srcSurfSize.width) / tmpSurfSize.width,
                                                 Float(srcSurfSize.height) / tmpSurfSize.height));

  if (mPushedClips.size()) {
    mPrivateData->mEffect->GetVariableByName("mask")->AsShaderResource()->SetResource(maskSRView);
    mPrivateData->mEffect->GetVariableByName("MaskTexCoords")->AsVector()->
      SetFloatVector(ShaderConstantRectD3D10(shadowDest.x / mSize.width, shadowDest.y / mSize.height,
                                             Float(aSurface->GetSize().width) / mSize.width,
                                             Float(aSurface->GetSize().height) / mSize.height));
    mPrivateData->mEffect->GetTechniqueByName("SampleTextureWithShadow")->
      GetPassByIndex(2)->Apply(0);
  } else {
    mPrivateData->mEffect->GetTechniqueByName("SampleTextureWithShadow")->
      GetPassByIndex(1)->Apply(0);
  }

  mDevice->OMSetBlendState(GetBlendStateForOperator(aOperator), NULL, 0xffffffff);

  mDevice->Draw(4, 0);

  mPrivateData->mEffect->GetVariableByName("QuadDesc")->AsVector()->
    SetFloatVector(ShaderConstantRectD3D10(-1.0f + ((aDest.x / mSize.width) * 2.0f),
                                           1.0f - (aDest.y / mSize.height * 2.0f),
                                           (Float(aSurface->GetSize().width) / mSize.width) * 2.0f,
                                           (-Float(aSurface->GetSize().height) / mSize.height) * 2.0f));
  mPrivateData->mEffect->GetVariableByName("tex")->AsShaderResource()->SetResource(static_cast<SourceSurfaceD2DTarget*>(aSurface)->GetSRView());
  mPrivateData->mEffect->GetVariableByName("TexCoords")->AsVector()->
    SetFloatVector(ShaderConstantRectD3D10(0, 0, 1.0f, 1.0f));

  if (mPushedClips.size()) {
    mPrivateData->mEffect->GetVariableByName("MaskTexCoords")->AsVector()->
      SetFloatVector(ShaderConstantRectD3D10(aDest.x / mSize.width, aDest.y / mSize.height,
                                             Float(aSurface->GetSize().width) / mSize.width,
                                             Float(aSurface->GetSize().height) / mSize.height));
    mPrivateData->mEffect->GetTechniqueByName("SampleMaskedTexture")->
      GetPassByIndex(0)->Apply(0);
  } else {
    mPrivateData->mEffect->GetTechniqueByName("SampleTexture")->
      GetPassByIndex(0)->Apply(0);
  }

  mDevice->OMSetBlendState(GetBlendStateForOperator(aOperator), NULL, 0xffffffff);

  mDevice->Draw(4, 0);
}

void
DrawTargetD2D::ClearRect(const Rect &aRect)
{
  MarkChanged();

  FlushTransformToRT();
  PopAllClips();

  AutoSaveRestoreClippedOut restoreClippedOut(this);

  restoreClippedOut.Save();

  bool needsClip = false;

  needsClip = aRect.x > 0 || aRect.y > 0 ||
              aRect.XMost() < mSize.width ||
              aRect.YMost() < mSize.height;

  if (needsClip) {
    mRT->PushAxisAlignedClip(D2DRect(aRect), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  }
  mRT->Clear(D2D1::ColorF(0, 0.0f));
  if (needsClip) {
    mRT->PopAxisAlignedClip();
  }
  return;
}

void
DrawTargetD2D::CopySurface(SourceSurface *aSurface,
                           const IntRect &aSourceRect,
                           const IntPoint &aDestination)
{
  MarkChanged();

  Rect srcRect(Float(aSourceRect.x), Float(aSourceRect.y),
               Float(aSourceRect.width), Float(aSourceRect.height));
  Rect dstRect(Float(aDestination.x), Float(aDestination.y),
               Float(aSourceRect.width), Float(aSourceRect.height));

  mRT->SetTransform(D2D1::IdentityMatrix());
  mTransformDirty = true;
  mRT->PushAxisAlignedClip(D2DRect(dstRect), D2D1_ANTIALIAS_MODE_ALIASED);
  mRT->Clear(D2D1::ColorF(0, 0.0f));
  mRT->PopAxisAlignedClip();

  RefPtr<ID2D1Bitmap> bitmap;

  switch (aSurface->GetType()) {
  case SURFACE_D2D1_BITMAP:
    {
      SourceSurfaceD2D *srcSurf = static_cast<SourceSurfaceD2D*>(aSurface);
      bitmap = srcSurf->GetBitmap();
    }
    break;
  case SURFACE_D2D1_DRAWTARGET:
    {
      SourceSurfaceD2DTarget *srcSurf = static_cast<SourceSurfaceD2DTarget*>(aSurface);
      bitmap = srcSurf->GetBitmap(mRT);
      AddDependencyOnSource(srcSurf);
    }
    break;
  default:
    return;
  }

  if (!bitmap) {
    return;
  }

  mRT->DrawBitmap(bitmap, D2DRect(dstRect), 1.0f,
                  D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                  D2DRect(srcRect));
}

void
DrawTargetD2D::FillRect(const Rect &aRect,
                        const Pattern &aPattern,
                        const DrawOptions &aOptions)
{
  ID2D1RenderTarget *rt = GetRTForOperation(aOptions.mCompositionOp, aPattern);

  PrepareForDrawing(rt);

  rt->SetAntialiasMode(D2DAAMode(aOptions.mAntialiasMode));

  RefPtr<ID2D1Brush> brush = CreateBrushForPattern(aPattern, aOptions.mAlpha);

  if (brush) {
    rt->FillRectangle(D2DRect(aRect), brush);
  }

  FinalizeRTForOperation(aOptions.mCompositionOp, aPattern, aRect);
}

void
DrawTargetD2D::StrokeRect(const Rect &aRect,
                          const Pattern &aPattern,
                          const StrokeOptions &aStrokeOptions,
                          const DrawOptions &aOptions)
{
  ID2D1RenderTarget *rt = GetRTForOperation(aOptions.mCompositionOp, aPattern);

  PrepareForDrawing(rt);

  rt->SetAntialiasMode(D2DAAMode(aOptions.mAntialiasMode));

  RefPtr<ID2D1Brush> brush = CreateBrushForPattern(aPattern, aOptions.mAlpha);

  RefPtr<ID2D1StrokeStyle> strokeStyle = CreateStrokeStyleForOptions(aStrokeOptions);

  if (brush && strokeStyle) {
    rt->DrawRectangle(D2DRect(aRect), brush, aStrokeOptions.mLineWidth, strokeStyle);
  }

  FinalizeRTForOperation(aOptions.mCompositionOp, aPattern, aRect);
}

void
DrawTargetD2D::StrokeLine(const Point &aStart,
                          const Point &aEnd,
                          const Pattern &aPattern,
                          const StrokeOptions &aStrokeOptions,
                          const DrawOptions &aOptions)
{
  ID2D1RenderTarget *rt = GetRTForOperation(aOptions.mCompositionOp, aPattern);

  PrepareForDrawing(rt);

  rt->SetAntialiasMode(D2DAAMode(aOptions.mAntialiasMode));

  RefPtr<ID2D1Brush> brush = CreateBrushForPattern(aPattern, aOptions.mAlpha);

  RefPtr<ID2D1StrokeStyle> strokeStyle = CreateStrokeStyleForOptions(aStrokeOptions);

  if (brush && strokeStyle) {
    rt->DrawLine(D2DPoint(aStart), D2DPoint(aEnd), brush, aStrokeOptions.mLineWidth, strokeStyle);
  }

  FinalizeRTForOperation(aOptions.mCompositionOp, aPattern, Rect(0, 0, Float(mSize.width), Float(mSize.height)));
}

void
DrawTargetD2D::Stroke(const Path *aPath,
                      const Pattern &aPattern,
                      const StrokeOptions &aStrokeOptions,
                      const DrawOptions &aOptions)
{
  if (aPath->GetBackendType() != BACKEND_DIRECT2D) {
    gfxDebug() << *this << ": Ignoring drawing call for incompatible path.";
    return;
  }

  const PathD2D *d2dPath = static_cast<const PathD2D*>(aPath);

  ID2D1RenderTarget *rt = GetRTForOperation(aOptions.mCompositionOp, aPattern);

  PrepareForDrawing(rt);

  rt->SetAntialiasMode(D2DAAMode(aOptions.mAntialiasMode));

  RefPtr<ID2D1Brush> brush = CreateBrushForPattern(aPattern, aOptions.mAlpha);

  RefPtr<ID2D1StrokeStyle> strokeStyle = CreateStrokeStyleForOptions(aStrokeOptions);

  if (brush && strokeStyle) {
    rt->DrawGeometry(d2dPath->mGeometry, brush, aStrokeOptions.mLineWidth, strokeStyle);
  }

  FinalizeRTForOperation(aOptions.mCompositionOp, aPattern, Rect(0, 0, Float(mSize.width), Float(mSize.height)));
}

void
DrawTargetD2D::Fill(const Path *aPath,
                    const Pattern &aPattern,
                    const DrawOptions &aOptions)
{
  if (aPath->GetBackendType() != BACKEND_DIRECT2D) {
    gfxDebug() << *this << ": Ignoring drawing call for incompatible path.";
    return;
  }

  const PathD2D *d2dPath = static_cast<const PathD2D*>(aPath);

  ID2D1RenderTarget *rt = GetRTForOperation(aOptions.mCompositionOp, aPattern);

  PrepareForDrawing(rt);

  rt->SetAntialiasMode(D2DAAMode(aOptions.mAntialiasMode));

  RefPtr<ID2D1Brush> brush = CreateBrushForPattern(aPattern, aOptions.mAlpha);

  if (brush) {
    rt->FillGeometry(d2dPath->mGeometry, brush);
  }

  Rect bounds;
  if (aOptions.mCompositionOp != OP_OVER) {
    D2D1_RECT_F d2dbounds;
    d2dPath->mGeometry->GetBounds(D2D1::IdentityMatrix(), &d2dbounds);
    bounds = ToRect(d2dbounds);
  }
  FinalizeRTForOperation(aOptions.mCompositionOp, aPattern, bounds);
}

void
DrawTargetD2D::FillGlyphs(ScaledFont *aFont,
                          const GlyphBuffer &aBuffer,
                          const Pattern &aPattern,
                          const DrawOptions &aOptions,
                          const GlyphRenderingOptions* aRenderOptions)
{
  if (aFont->GetType() != FONT_DWRITE) {
    gfxDebug() << *this << ": Ignoring drawing call for incompatible font.";
    return;
  }

  ScaledFontDWrite *font = static_cast<ScaledFontDWrite*>(aFont);

  IDWriteRenderingParams *params = NULL;
  if (aRenderOptions) {
    if (aRenderOptions->GetType() != FONT_DWRITE) {
      gfxDebug() << *this << ": Ignoring incompatible GlyphRenderingOptions.";
      // This should never happen.
      MOZ_ASSERT(false);
    } else {
      params = static_cast<const GlyphRenderingOptionsDWrite*>(aRenderOptions)->mParams;
    }
  }

  if (mFormat == FORMAT_B8G8R8A8 && mPermitSubpixelAA &&
      aOptions.mCompositionOp == OP_OVER && aPattern.GetType() == PATTERN_COLOR) {
    if (FillGlyphsManual(font, aBuffer,
                         static_cast<const ColorPattern*>(&aPattern)->mColor,
                         params, aOptions)) {
      return;
    }
  }

  ID2D1RenderTarget *rt = GetRTForOperation(aOptions.mCompositionOp, aPattern);

  PrepareForDrawing(rt);

  if (rt != mRT || params != mTextRenderingParams) {
    rt->SetTextRenderingParams(params);
    if (rt == mRT) {
      mTextRenderingParams = params;
    }
  }

  RefPtr<ID2D1Brush> brush = CreateBrushForPattern(aPattern, aOptions.mAlpha);

  AutoDWriteGlyphRun autoRun;
  DWriteGlyphRunFromGlyphs(aBuffer, font, &autoRun);

  if (brush) {
    rt->DrawGlyphRun(D2D1::Point2F(), &autoRun, brush);
  }

  FinalizeRTForOperation(aOptions.mCompositionOp, aPattern, Rect(0, 0, (Float)mSize.width, (Float)mSize.height));
}

void
DrawTargetD2D::Mask(const Pattern &aSource,
                    const Pattern &aMask,
                    const DrawOptions &aOptions)
{
  ID2D1RenderTarget *rt = GetRTForOperation(aOptions.mCompositionOp, aSource);
  
  PrepareForDrawing(rt);

  RefPtr<ID2D1Brush> brush = CreateBrushForPattern(aSource, aOptions.mAlpha);
  RefPtr<ID2D1Brush> maskBrush = CreateBrushForPattern(aMask, 1.0f);

  RefPtr<ID2D1Layer> layer;

  layer = GetCachedLayer();

  rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), NULL,
                                      D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                      D2D1::IdentityMatrix(),
                                      1.0f, maskBrush),
                layer);

  Rect rect(0, 0, mSize.width, mSize.height);
  Matrix mat = mTransform;
  mat.Invert();
  
  rt->FillRectangle(D2DRect(mat.TransformBounds(rect)), brush);
  PopCachedLayer(rt);

  FinalizeRTForOperation(aOptions.mCompositionOp, aSource, Rect(0, 0, (Float)mSize.width, (Float)mSize.height));
}

void
DrawTargetD2D::PushClip(const Path *aPath)
{
  if (aPath->GetBackendType() != BACKEND_DIRECT2D) {
    gfxDebug() << *this << ": Ignoring clipping call for incompatible path.";
    return;
  }

  mCurrentClipMaskTexture = NULL;
  mCurrentClippedGeometry = NULL;

  RefPtr<PathD2D> pathD2D = static_cast<PathD2D*>(const_cast<Path*>(aPath));

  PushedClip clip;
  clip.mTransform = D2DMatrix(mTransform);
  clip.mPath = pathD2D;
  
  pathD2D->mGeometry->GetBounds(clip.mTransform, &clip.mBounds);
  
  clip.mLayer = GetCachedLayer();

  mPushedClips.push_back(clip);

  // The transform of clips is relative to the world matrix, since we use the total
  // transform for the clips, make the world matrix identity.
  mRT->SetTransform(D2D1::IdentityMatrix());
  mTransformDirty = true;

  if (mClipsArePushed) {
    D2D1_LAYER_OPTIONS options = D2D1_LAYER_OPTIONS_NONE;

    if (mFormat == FORMAT_B8G8R8X8) {
      options = D2D1_LAYER_OPTIONS_INITIALIZE_FOR_CLEARTYPE;
    }

    mRT->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), pathD2D->mGeometry,
                                         D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                         clip.mTransform, 1.0f, NULL,
                                         options), clip.mLayer);
  }
}

void
DrawTargetD2D::PushClipRect(const Rect &aRect)
{
  mCurrentClipMaskTexture = NULL;
  mCurrentClippedGeometry = NULL;
  if (!mTransform.IsRectilinear()) {
    // Whoops, this isn't a rectangle in device space, Direct2D will not deal
    // with this transform the way we want it to.
    // See remarks: http://msdn.microsoft.com/en-us/library/dd316860%28VS.85%29.aspx

    RefPtr<PathBuilder> pathBuilder = CreatePathBuilder();
    pathBuilder->MoveTo(aRect.TopLeft());
    pathBuilder->LineTo(aRect.TopRight());
    pathBuilder->LineTo(aRect.BottomRight());
    pathBuilder->LineTo(aRect.BottomLeft());
    pathBuilder->Close();
    RefPtr<Path> path = pathBuilder->Finish();
    return PushClip(path);
  }

  PushedClip clip;
  // Do not store the transform, just store the device space rectangle directly.
  clip.mBounds = D2DRect(mTransform.TransformBounds(aRect));

  mPushedClips.push_back(clip);

  mRT->SetTransform(D2D1::IdentityMatrix());
  mTransformDirty = true;
  if (mClipsArePushed) {
    mRT->PushAxisAlignedClip(clip.mBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  }
}

void
DrawTargetD2D::PopClip()
{
  mCurrentClipMaskTexture = NULL;
  mCurrentClippedGeometry = NULL;
  if (mClipsArePushed) {
    if (mPushedClips.back().mLayer) {
      PopCachedLayer(mRT);
    } else {
      mRT->PopAxisAlignedClip();
    }
  }
  mPushedClips.pop_back();
}

TemporaryRef<SourceSurface> 
DrawTargetD2D::CreateSourceSurfaceFromData(unsigned char *aData,
                                           const IntSize &aSize,
                                           int32_t aStride,
                                           SurfaceFormat aFormat) const
{
  RefPtr<SourceSurfaceD2D> newSurf = new SourceSurfaceD2D();

  if (!newSurf->InitFromData(aData, aSize, aStride, aFormat, mRT)) {
    return NULL;
  }

  return newSurf;
}

TemporaryRef<SourceSurface> 
DrawTargetD2D::OptimizeSourceSurface(SourceSurface *aSurface) const
{
  // Unsupported!
  return NULL;
}

TemporaryRef<SourceSurface>
DrawTargetD2D::CreateSourceSurfaceFromNativeSurface(const NativeSurface &aSurface) const
{
  if (aSurface.mType != NATIVE_SURFACE_D3D10_TEXTURE) {
    gfxDebug() << *this << ": Failure to create source surface from non-D3D10 texture native surface.";
    return NULL;
  }
  RefPtr<SourceSurfaceD2D> newSurf = new SourceSurfaceD2D();

  if (!newSurf->InitFromTexture(static_cast<ID3D10Texture2D*>(aSurface.mSurface),
                                aSurface.mFormat,
                                mRT))
  {
    gfxWarning() << *this << ": Failed to create SourceSurface from texture.";
    return NULL;
  }

  return newSurf;
}

TemporaryRef<DrawTarget>
DrawTargetD2D::CreateSimilarDrawTarget(const IntSize &aSize, SurfaceFormat aFormat) const
{
  RefPtr<DrawTargetD2D> newTarget =
    new DrawTargetD2D();

  if (!newTarget->Init(aSize, aFormat)) {
    gfxDebug() << *this << ": Failed to create optimal draw target. Size: " << aSize;
    return NULL;
  }

  return newTarget;
}

TemporaryRef<PathBuilder>
DrawTargetD2D::CreatePathBuilder(FillRule aFillRule) const
{
  RefPtr<ID2D1PathGeometry> path;
  HRESULT hr = factory()->CreatePathGeometry(byRef(path));

  if (FAILED(hr)) {
    gfxWarning() << "Failed to create Direct2D Path Geometry. Code: " << hr;
    return NULL;
  }

  RefPtr<ID2D1GeometrySink> sink;
  hr = path->Open(byRef(sink));
  if (FAILED(hr)) {
    gfxWarning() << "Failed to access Direct2D Path Geometry. Code: " << hr;
    return NULL;
  }

  if (aFillRule == FILL_WINDING) {
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
  }

  return new PathBuilderD2D(sink, path, aFillRule);
}

TemporaryRef<GradientStops>
DrawTargetD2D::CreateGradientStops(GradientStop *rawStops, uint32_t aNumStops, ExtendMode aExtendMode) const
{
  D2D1_GRADIENT_STOP *stops = new D2D1_GRADIENT_STOP[aNumStops];

  for (uint32_t i = 0; i < aNumStops; i++) {
    stops[i].position = rawStops[i].offset;
    stops[i].color = D2DColor(rawStops[i].color);
  }

  RefPtr<ID2D1GradientStopCollection> stopCollection;

  HRESULT hr =
    mRT->CreateGradientStopCollection(stops, aNumStops,
                                      D2D1_GAMMA_2_2, D2DExtend(aExtendMode),
                                      byRef(stopCollection));
  delete [] stops;

  if (FAILED(hr)) {
    gfxWarning() << "Failed to create GradientStopCollection. Code: " << hr;
    return NULL;
  }

  return new GradientStopsD2D(stopCollection);
}

void*
DrawTargetD2D::GetNativeSurface(NativeSurfaceType aType)
{
  if (aType != NATIVE_SURFACE_D3D10_TEXTURE) {
    return NULL;
  }

  return mTexture;
}

/*
 * Public functions
 */
bool
DrawTargetD2D::Init(const IntSize &aSize, SurfaceFormat aFormat)
{
  HRESULT hr;

  mSize = aSize;
  mFormat = aFormat;

  if (!Factory::GetDirect3D10Device()) {
    gfxDebug() << "Failed to Init Direct2D DrawTarget (No D3D10 Device set.)";
    return false;
  }
  mDevice = Factory::GetDirect3D10Device();

  CD3D10_TEXTURE2D_DESC desc(DXGIFormat(aFormat),
                             mSize.width,
                             mSize.height,
                             1, 1);
  desc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;

  hr = mDevice->CreateTexture2D(&desc, NULL, byRef(mTexture));

  if (FAILED(hr)) {
    gfxDebug() << "Failed to init Direct2D DrawTarget. Size: " << mSize << " Code: " << hr;
    return false;
  }

  if (!InitD2DRenderTarget()) {
    return false;
  }

  mRT->Clear(D2D1::ColorF(0, 0));
  return true;
}

bool
DrawTargetD2D::Init(ID3D10Texture2D *aTexture, SurfaceFormat aFormat)
{
  HRESULT hr;

  mTexture = aTexture;
  mFormat = aFormat;

  if (!mTexture) {
    gfxDebug() << "No valid texture for Direct2D draw target initialization.";
    return false;
  }

  RefPtr<ID3D10Device> device;
  mTexture->GetDevice(byRef(device));

  hr = device->QueryInterface((ID3D10Device1**)byRef(mDevice));

  if (FAILED(hr)) {
    gfxWarning() << "Failed to get D3D10 device from texture.";
    return false;
  }

  D3D10_TEXTURE2D_DESC desc;
  mTexture->GetDesc(&desc);
  mSize.width = desc.Width;
  mSize.height = desc.Height;

  return InitD2DRenderTarget();
}

// {0D398B49-AE7B-416F-B26D-EA3C137D1CF7}
static const GUID sPrivateDataD2D = 
{ 0xd398b49, 0xae7b, 0x416f, { 0xb2, 0x6d, 0xea, 0x3c, 0x13, 0x7d, 0x1c, 0xf7 } };

bool
DrawTargetD2D::InitD3D10Data()
{
  HRESULT hr;
  
  UINT privateDataSize;
  privateDataSize = sizeof(mPrivateData);
  hr = mDevice->GetPrivateData(sPrivateDataD2D, &privateDataSize, &mPrivateData);

  if (SUCCEEDED(hr)) {
      return true;
  }

  mPrivateData = new PrivateD3D10DataD2D;

  D3D10CreateEffectFromMemoryFunc createD3DEffect;
  HMODULE d3dModule = LoadLibraryW(L"d3d10_1.dll");
  createD3DEffect = (D3D10CreateEffectFromMemoryFunc)
      GetProcAddress(d3dModule, "D3D10CreateEffectFromMemory");

  hr = createD3DEffect((void*)d2deffect, sizeof(d2deffect), 0, mDevice, NULL, byRef(mPrivateData->mEffect));

  if (FAILED(hr)) {
    gfxWarning() << "Failed to initialize Direct2D required effects. Code: " << hr;
    return false;
  }

  privateDataSize = sizeof(mPrivateData);
  mDevice->SetPrivateData(sPrivateDataD2D, privateDataSize, &mPrivateData);

  D3D10_INPUT_ELEMENT_DESC layout[] =
  {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
  };
  D3D10_PASS_DESC passDesc;
  
  mPrivateData->mEffect->GetTechniqueByName("SampleTexture")->GetPassByIndex(0)->GetDesc(&passDesc);

  hr = mDevice->CreateInputLayout(layout,
                                  sizeof(layout) / sizeof(D3D10_INPUT_ELEMENT_DESC),
                                  passDesc.pIAInputSignature,
                                  passDesc.IAInputSignatureSize,
                                  byRef(mPrivateData->mInputLayout));

  if (FAILED(hr)) {
    gfxWarning() << "Failed to initialize Direct2D required InputLayout. Code: " << hr;
    return false;
  }

  D3D10_SUBRESOURCE_DATA data;
  Vertex vertices[] = { {0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {1.0, 1.0} };
  data.pSysMem = vertices;
  CD3D10_BUFFER_DESC bufferDesc(sizeof(vertices), D3D10_BIND_VERTEX_BUFFER);

  hr = mDevice->CreateBuffer(&bufferDesc, &data, byRef(mPrivateData->mVB));

  if (FAILED(hr)) {
    gfxWarning() << "Failed to initialize Direct2D required VertexBuffer. Code: " << hr;
    return false;
  }

  return true;
}

/*
 * Private helpers
 */
uint32_t
DrawTargetD2D::GetByteSize() const
{
  return mSize.width * mSize.height * BytesPerPixel(mFormat);
}

TemporaryRef<ID2D1Layer>
DrawTargetD2D::GetCachedLayer()
{
  RefPtr<ID2D1Layer> layer;

  if (mCurrentCachedLayer < 5) {
    if (!mCachedLayers[mCurrentCachedLayer]) {
      mRT->CreateLayer(byRef(mCachedLayers[mCurrentCachedLayer]));
      mVRAMUsageDT += GetByteSize();
    }
    layer = mCachedLayers[mCurrentCachedLayer];
  } else {
    mRT->CreateLayer(byRef(layer));
  }

  mCurrentCachedLayer++;
  return layer;
}

void
DrawTargetD2D::PopCachedLayer(ID2D1RenderTarget *aRT)
{
  aRT->PopLayer();
  mCurrentCachedLayer--;
}

bool
DrawTargetD2D::InitD2DRenderTarget()
{
  if (!factory()) {
    return false;
  }

  mRT = CreateRTForTexture(mTexture, mFormat);

  if (!mRT) {
    return false;
  }

  mRT->BeginDraw();

  if (mFormat == FORMAT_B8G8R8X8) {
    mRT->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
  }

  mVRAMUsageDT += GetByteSize();

  return InitD3D10Data();
}

void
DrawTargetD2D::PrepareForDrawing(ID2D1RenderTarget *aRT)
{
  if (!mClipsArePushed || aRT == mTempRT) {
    if (mPushedClips.size()) {
      // The transform of clips is relative to the world matrix, since we use the total
      // transform for the clips, make the world matrix identity.
      aRT->SetTransform(D2D1::IdentityMatrix());
      if (aRT == mRT) {
        mTransformDirty = true;
        mClipsArePushed = true;
      }
      PushClipsToRT(aRT);
    }
  }
  FlushTransformToRT();
  MarkChanged();

  if (aRT == mTempRT) {
    mTempRT->SetTransform(D2DMatrix(mTransform));
  }
}

void
DrawTargetD2D::MarkChanged()
{
  if (mSnapshot) {
    if (mSnapshot->hasOneRef()) {
      // Just destroy it, since no-one else knows about it.
      mSnapshot = NULL;
    } else {
      mSnapshot->DrawTargetWillChange();
      // The snapshot will no longer depend on this target.
      MOZ_ASSERT(!mSnapshot);
    }
  }
  if (mDependentTargets.size()) {
    // Copy mDependentTargets since the Flush()es below will modify it.
    TargetSet tmpTargets = mDependentTargets;
    for (TargetSet::iterator iter = tmpTargets.begin();
         iter != tmpTargets.end(); iter++) {
      (*iter)->Flush();
    }
    // The Flush() should have broken all dependencies on this target.
    MOZ_ASSERT(!mDependentTargets.size());
  }
}

ID3D10BlendState*
DrawTargetD2D::GetBlendStateForOperator(CompositionOp aOperator)
{
  if (mPrivateData->mBlendStates[aOperator]) {
    return mPrivateData->mBlendStates[aOperator];
  }

  D3D10_BLEND_DESC desc;

  memset(&desc, 0, sizeof(D3D10_BLEND_DESC));

  desc.AlphaToCoverageEnable = FALSE;
  desc.BlendEnable[0] = TRUE;
  desc.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
  desc.BlendOp = desc.BlendOpAlpha = D3D10_BLEND_OP_ADD;

  switch (aOperator) {
  case OP_ADD:
    desc.SrcBlend = desc.SrcBlendAlpha = D3D10_BLEND_ONE;
    desc.DestBlend = desc.DestBlendAlpha = D3D10_BLEND_ONE;
    break;
  case OP_IN:
    desc.SrcBlend = desc.SrcBlendAlpha = D3D10_BLEND_DEST_ALPHA;
    desc.DestBlend = desc.DestBlendAlpha = D3D10_BLEND_ZERO;
    break;
  case OP_OUT:
    desc.SrcBlend = desc.SrcBlendAlpha = D3D10_BLEND_INV_DEST_ALPHA;
    desc.DestBlend = desc.DestBlendAlpha = D3D10_BLEND_ZERO;
    break;
  case OP_ATOP:
    desc.SrcBlend = desc.SrcBlendAlpha = D3D10_BLEND_DEST_ALPHA;
    desc.DestBlend = desc.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
    break;
  case OP_DEST_IN:
    desc.SrcBlend = desc.SrcBlendAlpha = D3D10_BLEND_ZERO;
    desc.DestBlend = desc.DestBlendAlpha = D3D10_BLEND_SRC_ALPHA;
    break;
  case OP_DEST_OUT:
    desc.SrcBlend = desc.SrcBlendAlpha = D3D10_BLEND_ZERO;
    desc.DestBlend = desc.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
    break;
  case OP_DEST_ATOP:
    desc.SrcBlend = desc.SrcBlendAlpha = D3D10_BLEND_INV_DEST_ALPHA;
    desc.DestBlend = desc.DestBlendAlpha = D3D10_BLEND_SRC_ALPHA;
    break;
  case OP_DEST_OVER:
    desc.SrcBlend = desc.SrcBlendAlpha = D3D10_BLEND_INV_DEST_ALPHA;
    desc.DestBlend = desc.DestBlendAlpha = D3D10_BLEND_ONE;
    break;
  case OP_XOR:
    desc.SrcBlend = desc.SrcBlendAlpha = D3D10_BLEND_INV_DEST_ALPHA;
    desc.DestBlend = desc.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
    break;
  case OP_SOURCE:
    desc.SrcBlend = desc.SrcBlendAlpha = D3D10_BLEND_ONE;
    desc.DestBlend = desc.DestBlendAlpha = D3D10_BLEND_ZERO;
    break;
  default:
    desc.SrcBlend = desc.SrcBlendAlpha = D3D10_BLEND_ONE;
    desc.DestBlend = desc.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
  }
  
  mDevice->CreateBlendState(&desc, byRef(mPrivateData->mBlendStates[aOperator]));

  return mPrivateData->mBlendStates[aOperator];
}

/* This function prepares the temporary RT for drawing and returns it when a
 * drawing operation other than OVER is required.
 */
ID2D1RenderTarget*
DrawTargetD2D::GetRTForOperation(CompositionOp aOperator, const Pattern &aPattern)
{
  if (aOperator == OP_OVER && !IsPatternSupportedByD2D(aPattern)) {
    return mRT;
  }

  PopAllClips();

  if (mTempRT) {
    mTempRT->Clear(D2D1::ColorF(0, 0));
    return mTempRT;
  }

  EnsureViews();

  if (!mRTView || !mSRView) {
    gfxDebug() << *this << ": Failed to get required views. Defaulting to OP_OVER.";
    return mRT;
  }

  mTempRT = CreateRTForTexture(mTempTexture, FORMAT_B8G8R8A8);

  if (!mTempRT) {
    return mRT;
  }

  mVRAMUsageDT += GetByteSize();

  mTempRT->BeginDraw();

  mTempRT->Clear(D2D1::ColorF(0, 0));

  return mTempRT;
}

/* This function blends back the content of a drawing operation (drawn to an
 * empty surface with OVER, so the surface now contains the source operation
 * contents) to the rendertarget using the requested composition operation.
 * In order to respect clip for operations which are unbound by their mask,
 * the old content of the surface outside the clipped area may be blended back
 * to the surface.
 */
void
DrawTargetD2D::FinalizeRTForOperation(CompositionOp aOperator, const Pattern &aPattern, const Rect &aBounds)
{
  if (aOperator == OP_OVER && !IsPatternSupportedByD2D(aPattern)) {
    return;
  }

  if (!mTempRT) {
    return;
  }

  PopClipsFromRT(mTempRT);

  mRT->Flush();
  mTempRT->Flush();

  AutoSaveRestoreClippedOut restoreClippedOut(this);

  bool needsWriteBack =
    !IsOperatorBoundByMask(aOperator) && mPushedClips.size();

  if (needsWriteBack) {
    restoreClippedOut.Save();
  }

  ID3D10RenderTargetView *rtViews = mRTView;
  mDevice->OMSetRenderTargets(1, &rtViews, NULL);

  UINT stride = sizeof(Vertex);
  UINT offset = 0;
  ID3D10Buffer *buff = mPrivateData->mVB;

  mDevice->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  mDevice->IASetVertexBuffers(0, 1, &buff, &stride, &offset);
  mDevice->IASetInputLayout(mPrivateData->mInputLayout);

  D3D10_VIEWPORT viewport;
  viewport.MaxDepth = 1;
  viewport.MinDepth = 0;
  viewport.Height = mSize.height;
  viewport.Width = mSize.width;
  viewport.TopLeftX = 0;
  viewport.TopLeftY = 0;

  mDevice->RSSetViewports(1, &viewport);
  mPrivateData->mEffect->GetVariableByName("QuadDesc")->AsVector()->
    SetFloatVector(ShaderConstantRectD3D10(-1.0f, 1.0f, 2.0f, -2.0f));

  if (!IsPatternSupportedByD2D(aPattern)) {
    mPrivateData->mEffect->GetVariableByName("TexCoords")->AsVector()->
      SetFloatVector(ShaderConstantRectD3D10(0, 0, 1.0f, 1.0f));
    mPrivateData->mEffect->GetVariableByName("tex")->AsShaderResource()->SetResource(mSRView);
    mPrivateData->mEffect->GetTechniqueByName("SampleTexture")->GetPassByIndex(0)->Apply(0);
  } else if (aPattern.GetType() == PATTERN_RADIAL_GRADIENT) {
    const RadialGradientPattern *pat = static_cast<const RadialGradientPattern*>(&aPattern);

    if (pat->mCenter1 == pat->mCenter2 && pat->mRadius1 == pat->mRadius2) {
      // Draw nothing!
      return;
    }

    mPrivateData->mEffect->GetVariableByName("mask")->AsShaderResource()->SetResource(mSRView);

    SetupEffectForRadialGradient(pat);
  }

  mDevice->OMSetBlendState(GetBlendStateForOperator(aOperator), NULL, 0xffffffff);
  
  mDevice->Draw(4, 0);
}

TemporaryRef<ID2D1Geometry>
DrawTargetD2D::GetClippedGeometry()
{
  if (mCurrentClippedGeometry) {
    return mCurrentClippedGeometry;
  }

  RefPtr<ID2D1GeometrySink> currentSink;

  factory()->CreatePathGeometry(byRef(mCurrentClippedGeometry));
  mCurrentClippedGeometry->Open(byRef(currentSink));
      
  std::vector<DrawTargetD2D::PushedClip>::iterator iter = mPushedClips.begin();

  if (iter->mPath) {
    iter->mPath->GetGeometry()->Simplify(D2D1_GEOMETRY_SIMPLIFICATION_OPTION_CUBICS_AND_LINES,
                                         iter->mTransform, currentSink);
  } else {
    RefPtr<ID2D1RectangleGeometry> rectGeom;
    factory()->CreateRectangleGeometry(iter->mBounds, byRef(rectGeom));
    rectGeom->Simplify(D2D1_GEOMETRY_SIMPLIFICATION_OPTION_CUBICS_AND_LINES,
                       D2D1::IdentityMatrix(), currentSink);
  }
  currentSink->Close();

  iter++;
  for (;iter != mPushedClips.end(); iter++) {
    RefPtr<ID2D1PathGeometry> newGeom;
    factory()->CreatePathGeometry(byRef(newGeom));

    newGeom->Open(byRef(currentSink));

    if (iter->mPath) {
      mCurrentClippedGeometry->CombineWithGeometry(iter->mPath->GetGeometry(), D2D1_COMBINE_MODE_INTERSECT,
                                           iter->mTransform, currentSink);
    } else {
      RefPtr<ID2D1RectangleGeometry> rectGeom;
      factory()->CreateRectangleGeometry(iter->mBounds, byRef(rectGeom));
      mCurrentClippedGeometry->CombineWithGeometry(rectGeom, D2D1_COMBINE_MODE_INTERSECT,
                                                   D2D1::IdentityMatrix(), currentSink);
    }

    currentSink->Close();

    mCurrentClippedGeometry = newGeom;
  }

  return mCurrentClippedGeometry;
}

TemporaryRef<ID2D1RenderTarget>
DrawTargetD2D::CreateRTForTexture(ID3D10Texture2D *aTexture, SurfaceFormat aFormat)
{
  HRESULT hr;

  RefPtr<IDXGISurface> surface;
  RefPtr<ID2D1RenderTarget> rt;

  hr = aTexture->QueryInterface((IDXGISurface**)byRef(surface));

  if (FAILED(hr)) {
    gfxWarning() << "Failed to QI texture to surface.";
    return NULL;
  }

  D3D10_TEXTURE2D_DESC desc;
  aTexture->GetDesc(&desc);

  D2D1_ALPHA_MODE alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;

  if (aFormat == FORMAT_B8G8R8X8 && aTexture == mTexture) {
    alphaMode = D2D1_ALPHA_MODE_IGNORE;
  }

  D2D1_RENDER_TARGET_PROPERTIES props =
    D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(desc.Format, alphaMode));
  hr = factory()->CreateDxgiSurfaceRenderTarget(surface, props, byRef(rt));

  if (FAILED(hr)) {
    gfxWarning() << "Failed to create D2D render target for texture.";
    return NULL;
  }

  return rt;
}

void
DrawTargetD2D::EnsureViews()
{
  if (mTempTexture && mSRView && mRTView) {
    return;
  }

  HRESULT hr;

  CD3D10_TEXTURE2D_DESC desc(DXGI_FORMAT_B8G8R8A8_UNORM,
                             mSize.width,
                             mSize.height,
                             1, 1);
  desc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;

  hr = mDevice->CreateTexture2D(&desc, NULL, byRef(mTempTexture));

  if (FAILED(hr)) {
    gfxWarning() << *this << "Failed to create temporary texture for rendertarget. Size: "
      << mSize << " Code: " << hr;
    return;
  }

  hr = mDevice->CreateShaderResourceView(mTempTexture, NULL, byRef(mSRView));

  if (FAILED(hr)) {
    gfxWarning() << *this << "Failed to create shader resource view for temp texture. Code: " << hr;
    return;
  }

  hr = mDevice->CreateRenderTargetView(mTexture, NULL, byRef(mRTView));

  if (FAILED(hr)) {
    gfxWarning() << *this << "Failed to create rendertarget view for temp texture. Code: " << hr;
  }
}

void
DrawTargetD2D::PopAllClips()
{
  if (mClipsArePushed) {
    PopClipsFromRT(mRT);
  
    mClipsArePushed = false;
  }
}

void
DrawTargetD2D::PushClipsToRT(ID2D1RenderTarget *aRT)
{
  for (std::vector<PushedClip>::iterator iter = mPushedClips.begin();
        iter != mPushedClips.end(); iter++) {
    if (iter->mLayer) {
      D2D1_LAYER_OPTIONS options = D2D1_LAYER_OPTIONS_NONE;

      if (mFormat == FORMAT_B8G8R8X8) {
        options = D2D1_LAYER_OPTIONS_INITIALIZE_FOR_CLEARTYPE;
      }

      aRT->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), iter->mPath->mGeometry,
                                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                            iter->mTransform, 1.0f, NULL,
                                            options), iter->mLayer);
    } else {
      aRT->PushAxisAlignedClip(iter->mBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
  }
}

void
DrawTargetD2D::PopClipsFromRT(ID2D1RenderTarget *aRT)
{
  for (int i = mPushedClips.size() - 1; i >= 0; i--) {
    if (mPushedClips[i].mLayer) {
      aRT->PopLayer();
    } else {
      aRT->PopAxisAlignedClip();
    }
  }
}

void
DrawTargetD2D::EnsureClipMaskTexture()
{
  if (mCurrentClipMaskTexture || mPushedClips.empty()) {
    return;
  }
  
  CD3D10_TEXTURE2D_DESC desc(DXGI_FORMAT_A8_UNORM,
                             mSize.width,
                             mSize.height,
                             1, 1);
  desc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;

  HRESULT hr = mDevice->CreateTexture2D(&desc, NULL, byRef(mCurrentClipMaskTexture));

  if (FAILED(hr)) {
    gfxWarning() << "Failed to create texture for ClipMask!";
    return;
  }

  RefPtr<ID2D1RenderTarget> rt = CreateRTForTexture(mCurrentClipMaskTexture, FORMAT_A8);

  if (!rt) {
    gfxWarning() << "Failed to create RT for ClipMask!";
    return;
  }
  
  RefPtr<ID2D1SolidColorBrush> brush;
  rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), byRef(brush));
    
  RefPtr<ID2D1Geometry> geometry = GetClippedGeometry();

  rt->BeginDraw();
  rt->Clear(D2D1::ColorF(0, 0));
  rt->FillGeometry(geometry, brush);
  rt->EndDraw();
}

bool
DrawTargetD2D::FillGlyphsManual(ScaledFontDWrite *aFont,
                                const GlyphBuffer &aBuffer,
                                const Color &aColor,
                                IDWriteRenderingParams *aParams,
                                const DrawOptions &aOptions)
{
  HRESULT hr;

  RefPtr<IDWriteRenderingParams> params;

  if (aParams) {
    params = aParams;
  } else {
    mRT->GetTextRenderingParams(byRef(params));
  }

  DWRITE_RENDERING_MODE renderMode = DWRITE_RENDERING_MODE_DEFAULT;
  if (params) {
    hr = aFont->mFontFace->GetRecommendedRenderingMode(
      (FLOAT)aFont->mSize,
      1.0f,
      DWRITE_MEASURING_MODE_NATURAL,
      params,
      &renderMode);
    if (FAILED(hr)) {
      // this probably never happens, but let's play it safe
      renderMode = DWRITE_RENDERING_MODE_DEFAULT;
    }
  }

  // Deal with rendering modes CreateGlyphRunAnalysis doesn't accept.
  switch (renderMode) {
  case DWRITE_RENDERING_MODE_ALIASED:
    // ClearType texture creation will fail in this mode, so bail out
    return false;
  case DWRITE_RENDERING_MODE_DEFAULT:
    // As per DWRITE_RENDERING_MODE documentation, pick Natural for font
    // sizes under 16 ppem
    if (aFont->mSize < 16.0f) {
      renderMode = DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL;
    } else {
      renderMode = DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC;
    }
    break;
  case DWRITE_RENDERING_MODE_OUTLINE:
    renderMode = DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC;
    break;
  default:
    break;
  }

  DWRITE_MEASURING_MODE measureMode =
    renderMode <= DWRITE_RENDERING_MODE_CLEARTYPE_GDI_CLASSIC ? DWRITE_MEASURING_MODE_GDI_CLASSIC :
    renderMode == DWRITE_RENDERING_MODE_CLEARTYPE_GDI_NATURAL ? DWRITE_MEASURING_MODE_GDI_NATURAL :
    DWRITE_MEASURING_MODE_NATURAL;

  DWRITE_MATRIX mat = DWriteMatrixFromMatrix(mTransform);

  AutoDWriteGlyphRun autoRun;
  DWriteGlyphRunFromGlyphs(aBuffer, aFont, &autoRun);

  RefPtr<IDWriteGlyphRunAnalysis> analysis;
  hr = GetDWriteFactory()->CreateGlyphRunAnalysis(&autoRun, 1.0f, &mat,
                                                  renderMode, measureMode, 0, 0, byRef(analysis));

  if (FAILED(hr)) {
    return false;
  }

  RECT bounds;
  hr = analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);

  if (bounds.bottom <= bounds.top || bounds.right <= bounds.left) {
    // DWrite seems to do this sometimes. I'm not 100% sure why. See bug 758980.
    gfxDebug() << "Empty alpha texture bounds! Falling back to regular drawing.";
    return false;
  }
  IntRect rectBounds(bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top);
  IntRect surfBounds(IntPoint(0, 0), mSize);

  rectBounds.IntersectRect(rectBounds, surfBounds);

  if (rectBounds.IsEmpty()) {
    // Nothing to do.
    return true;
  }

  RefPtr<ID3D10Texture2D> tex = CreateTextureForAnalysis(analysis, rectBounds);

  if (!tex) {
    return false;
  }

  RefPtr<ID3D10ShaderResourceView> srView;
  hr = mDevice->CreateShaderResourceView(tex, NULL, byRef(srView));

  if (FAILED(hr)) {
    return false;
  }

  MarkChanged();

  // Prepare our background texture for drawing.
  PopAllClips();
  mRT->Flush();

  SetupStateForRendering();

  ID3D10EffectTechnique *technique = mPrivateData->mEffect->GetTechniqueByName("SampleTextTexture");

  mPrivateData->mEffect->GetVariableByName("QuadDesc")->AsVector()->
    SetFloatVector(ShaderConstantRectD3D10(-1.0f + ((Float(rectBounds.x) / mSize.width) * 2.0f),
                                           1.0f - (Float(rectBounds.y) / mSize.height * 2.0f),
                                           (Float(rectBounds.width) / mSize.width) * 2.0f,
                                           (-Float(rectBounds.height) / mSize.height) * 2.0f));
  mPrivateData->mEffect->GetVariableByName("TexCoords")->AsVector()->
    SetFloatVector(ShaderConstantRectD3D10(0, 0, 1.0f, 1.0f));
  FLOAT color[4] = { aColor.r, aColor.g, aColor.b, aColor.a };
  mPrivateData->mEffect->GetVariableByName("TextColor")->AsVector()->
    SetFloatVector(color);
  
  mPrivateData->mEffect->GetVariableByName("tex")->AsShaderResource()->SetResource(srView);

  bool isMasking = false;

  if (!mPushedClips.empty()) {
    RefPtr<ID2D1Geometry> geom = GetClippedGeometry();

    RefPtr<ID2D1RectangleGeometry> rectGeom;
    factory()->CreateRectangleGeometry(D2D1::RectF(rectBounds.x, rectBounds.y,
                                                   rectBounds.width + rectBounds.x,
                                                   rectBounds.height + rectBounds.y),
                                       byRef(rectGeom));

    D2D1_GEOMETRY_RELATION relation;
    if (FAILED(geom->CompareWithGeometry(rectGeom, D2D1::IdentityMatrix(), &relation)) ||
        relation != D2D1_GEOMETRY_RELATION_CONTAINS) {
      isMasking = true;
    }        
  }
  
  if (isMasking) {
    EnsureClipMaskTexture();

    RefPtr<ID3D10ShaderResourceView> srViewMask;
    hr = mDevice->CreateShaderResourceView(mCurrentClipMaskTexture, NULL, byRef(srViewMask));

    if (FAILED(hr)) {
      return false;
    }

    mPrivateData->mEffect->GetVariableByName("mask")->AsShaderResource()->SetResource(srViewMask);

    mPrivateData->mEffect->GetVariableByName("MaskTexCoords")->AsVector()->
      SetFloatVector(ShaderConstantRectD3D10(Float(rectBounds.x) / mSize.width, Float(rectBounds.y) / mSize.height,
                                             Float(rectBounds.width) / mSize.width, Float(rectBounds.height) / mSize.height));

    technique->GetPassByIndex(1)->Apply(0);
  } else {
    technique->GetPassByIndex(0)->Apply(0);
  }  

  RefPtr<ID3D10RenderTargetView> rtView;
  ID3D10RenderTargetView *rtViews;
  mDevice->CreateRenderTargetView(mTexture, NULL, byRef(rtView));

  rtViews = rtView;
  mDevice->OMSetRenderTargets(1, &rtViews, NULL);

  mDevice->Draw(4, 0);
  return true;
}

TemporaryRef<ID2D1Brush>
DrawTargetD2D::CreateBrushForPattern(const Pattern &aPattern, Float aAlpha)
{
  if (IsPatternSupportedByD2D(aPattern)) {
    RefPtr<ID2D1SolidColorBrush> colBrush;
    mRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), byRef(colBrush));
    return colBrush;
  }

  if (aPattern.GetType() == PATTERN_COLOR) {
    RefPtr<ID2D1SolidColorBrush> colBrush;
    Color color = static_cast<const ColorPattern*>(&aPattern)->mColor;
    mRT->CreateSolidColorBrush(D2D1::ColorF(color.r, color.g,
                                            color.b, color.a),
                               D2D1::BrushProperties(aAlpha),
                               byRef(colBrush));
    return colBrush;
  } else if (aPattern.GetType() == PATTERN_LINEAR_GRADIENT) {
    RefPtr<ID2D1LinearGradientBrush> gradBrush;
    const LinearGradientPattern *pat =
      static_cast<const LinearGradientPattern*>(&aPattern);

    GradientStopsD2D *stops = static_cast<GradientStopsD2D*>(pat->mStops.get());

    if (!stops) {
      gfxDebug() << "No stops specified for gradient pattern.";
      return NULL;
    }

    if (pat->mBegin == pat->mEnd) {
      RefPtr<ID2D1SolidColorBrush> colBrush;
      uint32_t stopCount = stops->mStopCollection->GetGradientStopCount();
      vector<D2D1_GRADIENT_STOP> d2dStops(stopCount);
      stops->mStopCollection->GetGradientStops(&d2dStops.front(), stopCount);
      mRT->CreateSolidColorBrush(d2dStops.back().color,
                                 D2D1::BrushProperties(aAlpha),
                                 byRef(colBrush));
      return colBrush;
    }

    mRT->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2DPoint(pat->mBegin),
                                                                       D2DPoint(pat->mEnd)),
                                   D2D1::BrushProperties(aAlpha, D2DMatrix(pat->mMatrix)),
                                   stops->mStopCollection,
                                   byRef(gradBrush));
    return gradBrush;
  } else if (aPattern.GetType() == PATTERN_RADIAL_GRADIENT) {
    RefPtr<ID2D1RadialGradientBrush> gradBrush;
    const RadialGradientPattern *pat =
      static_cast<const RadialGradientPattern*>(&aPattern);

    GradientStopsD2D *stops = static_cast<GradientStopsD2D*>(pat->mStops.get());

    if (!stops) {
      gfxDebug() << "No stops specified for gradient pattern.";
      return NULL;
    }

    // This will not be a complex radial gradient brush.
    mRT->CreateRadialGradientBrush(
      D2D1::RadialGradientBrushProperties(D2DPoint(pat->mCenter1),
                                          D2D1::Point2F(),
                                          pat->mRadius2, pat->mRadius2),
      D2D1::BrushProperties(aAlpha, D2DMatrix(pat->mMatrix)),
      stops->mStopCollection,
      byRef(gradBrush));

    return gradBrush;
  } else if (aPattern.GetType() == PATTERN_SURFACE) {
    RefPtr<ID2D1BitmapBrush> bmBrush;
    const SurfacePattern *pat =
      static_cast<const SurfacePattern*>(&aPattern);

    if (!pat->mSurface) {
      gfxDebug() << "No source surface specified for surface pattern";
      return NULL;
    }

    RefPtr<ID2D1Bitmap> bitmap;

    Matrix mat = pat->mMatrix;
    
    switch (pat->mSurface->GetType()) {
    case SURFACE_D2D1_BITMAP:
      {
        SourceSurfaceD2D *surf = static_cast<SourceSurfaceD2D*>(pat->mSurface.get());

        bitmap = surf->mBitmap;

        if (!bitmap) {
          return NULL;
        }
      }
      break;
    case SURFACE_D2D1_DRAWTARGET:
      {
        SourceSurfaceD2DTarget *surf =
          static_cast<SourceSurfaceD2DTarget*>(pat->mSurface.get());
        bitmap = surf->GetBitmap(mRT);
        AddDependencyOnSource(surf);
      }
      break;
    case SURFACE_DATA:
      {
        DataSourceSurface *dataSurf =
          static_cast<DataSourceSurface*>(pat->mSurface.get());
        bitmap = CreatePartialBitmapForSurface(dataSurf, mat, pat->mExtendMode);
        
        if (!bitmap) {
          return NULL;
        }
      }
      break;
    default:
      break;
    }
    
    mRT->CreateBitmapBrush(bitmap,
                           D2D1::BitmapBrushProperties(D2DExtend(pat->mExtendMode),
                                                       D2DExtend(pat->mExtendMode),
                                                       D2DFilter(pat->mFilter)),
                           D2D1::BrushProperties(aAlpha, D2DMatrix(mat)),
                           byRef(bmBrush));

    return bmBrush;
  }

  gfxWarning() << "Invalid pattern type detected.";
  return NULL;
}

TemporaryRef<ID2D1StrokeStyle>
DrawTargetD2D::CreateStrokeStyleForOptions(const StrokeOptions &aStrokeOptions)
{
  RefPtr<ID2D1StrokeStyle> style;

  D2D1_CAP_STYLE capStyle;
  D2D1_LINE_JOIN joinStyle;

  switch (aStrokeOptions.mLineCap) {
  case CAP_BUTT:
    capStyle = D2D1_CAP_STYLE_FLAT;
    break;
  case CAP_ROUND:
    capStyle = D2D1_CAP_STYLE_ROUND;
    break;
  case CAP_SQUARE:
    capStyle = D2D1_CAP_STYLE_SQUARE;
    break;
  }

  switch (aStrokeOptions.mLineJoin) {
  case JOIN_MITER:
    joinStyle = D2D1_LINE_JOIN_MITER;
    break;
  case JOIN_MITER_OR_BEVEL:
    joinStyle = D2D1_LINE_JOIN_MITER_OR_BEVEL;
    break;
  case JOIN_ROUND:
    joinStyle = D2D1_LINE_JOIN_ROUND;
    break;
  case JOIN_BEVEL:
    joinStyle = D2D1_LINE_JOIN_BEVEL;
    break;
  }


  HRESULT hr;
  if (aStrokeOptions.mDashPattern) {
    typedef vector<Float> FloatVector;
    // D2D "helpfully" multiplies the dash pattern by the line width.
    // That's not what cairo does, or is what <canvas>'s dash wants.
    // So fix the multiplication in advance.
    Float lineWidth = aStrokeOptions.mLineWidth;
    FloatVector dash(aStrokeOptions.mDashPattern,
                     aStrokeOptions.mDashPattern + aStrokeOptions.mDashLength);
    for (FloatVector::iterator it = dash.begin(); it != dash.end(); ++it) {
      *it /= lineWidth;
    }

    hr = factory()->CreateStrokeStyle(
      D2D1::StrokeStyleProperties(capStyle, capStyle,
                                  capStyle, joinStyle,
                                  aStrokeOptions.mMiterLimit,
                                  D2D1_DASH_STYLE_CUSTOM,
                                  aStrokeOptions.mDashOffset),
      &dash[0], // data() is not C++98, although it's in recent gcc
                // and VC10's STL
      dash.size(),
      byRef(style));
  } else {
    hr = factory()->CreateStrokeStyle(
      D2D1::StrokeStyleProperties(capStyle, capStyle,
                                  capStyle, joinStyle,
                                  aStrokeOptions.mMiterLimit),
      NULL, 0, byRef(style));
  }

  if (FAILED(hr)) {
    gfxWarning() << "Failed to create Direct2D stroke style.";
  }

  return style;
}

TemporaryRef<ID3D10Texture2D>
DrawTargetD2D::CreateGradientTexture(const GradientStopsD2D *aStops)
{
  CD3D10_TEXTURE2D_DESC desc(DXGI_FORMAT_B8G8R8A8_UNORM, 4096, 1, 1, 1);

  std::vector<D2D1_GRADIENT_STOP> rawStops;
  rawStops.resize(aStops->mStopCollection->GetGradientStopCount());
  aStops->mStopCollection->GetGradientStops(&rawStops.front(), rawStops.size());

  std::vector<unsigned char> textureData;
  textureData.resize(4096 * 4);
  unsigned char *texData = &textureData.front();

  float prevColorPos = 0;
  float nextColorPos = 1.0f;
  D2D1_COLOR_F prevColor = rawStops[0].color;
  D2D1_COLOR_F nextColor = prevColor;

  if (rawStops.size() >= 2) {
    nextColor = rawStops[1].color;
    nextColorPos = rawStops[1].position;
  }

  uint32_t stopPosition = 2;

  // Not the most optimized way but this will do for now.
  for (int i = 0; i < 4096; i++) {
    // The 4095 seems a little counter intuitive, but we want the gradient
    // color at offset 0 at the first pixel, and at offset 1.0f at the last
    // pixel.
    float pos = float(i) / 4095;

    while (pos > nextColorPos) {
      prevColor = nextColor;
      prevColorPos = nextColorPos;
      if (rawStops.size() > stopPosition) {
        nextColor = rawStops[stopPosition].color;
        nextColorPos = rawStops[stopPosition++].position;
      } else {
        nextColorPos = 1.0f;
      }
    }

    float interp;
    
    if (nextColorPos != prevColorPos) {
      interp = (pos - prevColorPos) / (nextColorPos - prevColorPos);
    } else {
      interp = 0;
    }

    Color newColor(prevColor.r + (nextColor.r - prevColor.r) * interp,
                    prevColor.g + (nextColor.g - prevColor.g) * interp,
                    prevColor.b + (nextColor.b - prevColor.b) * interp,
                    prevColor.a + (nextColor.a - prevColor.a) * interp);

    texData[i * 4] = (char)(255.0f * newColor.b);
    texData[i * 4 + 1] = (char)(255.0f * newColor.g);
    texData[i * 4 + 2] = (char)(255.0f * newColor.r);
    texData[i * 4 + 3] = (char)(255.0f * newColor.a);
  }

  D3D10_SUBRESOURCE_DATA data;
  data.pSysMem = &textureData.front();
  data.SysMemPitch = 4096 * 4;

  RefPtr<ID3D10Texture2D> tex;
  mDevice->CreateTexture2D(&desc, &data, byRef(tex));

  return tex;
}

TemporaryRef<ID3D10Texture2D>
DrawTargetD2D::CreateTextureForAnalysis(IDWriteGlyphRunAnalysis *aAnalysis, const IntRect &aBounds)
{
  HRESULT hr;

  uint32_t bufferSize = aBounds.width * aBounds.height * 3;

  RECT bounds;
  bounds.left = aBounds.x;
  bounds.top = aBounds.y;
  bounds.right = aBounds.x + aBounds.width;
  bounds.bottom = aBounds.y + aBounds.height;

  // Add one byte so we can safely read a 32-bit int when copying the last
  // 3 bytes.
  BYTE *texture = new BYTE[bufferSize + 1];
  hr = aAnalysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, texture, bufferSize);

  if (FAILED(hr)) {
    delete [] texture;
    return NULL;
  }

  int alignedBufferSize = aBounds.width * aBounds.height * 4;

  // Create a one-off immutable texture from system memory.
  BYTE *alignedTextureData = new BYTE[alignedBufferSize];
  for (int y = 0; y < aBounds.height; y++) {
    for (int x = 0; x < aBounds.width; x++) {
      // Copy 3 Bpp source to 4 Bpp destination memory used for
      // texture creation. D3D10 has no 3 Bpp texture format we can
      // use.
      //
      // Since we don't care what ends up in the alpha pixel of the
      // destination, therefor we can simply copy a normal 32 bit
      // integer each time, filling the alpha pixel of the destination
      // with the first subpixel of the next pixel from the source.
      *((int*)(alignedTextureData + (y * aBounds.width + x) * 4)) =
        *((int*)(texture + (y * aBounds.width + x) * 3));
    }
  }

  D3D10_SUBRESOURCE_DATA data;
  
  CD3D10_TEXTURE2D_DESC desc(DXGI_FORMAT_B8G8R8A8_UNORM,
                             aBounds.width, aBounds.height,
                             1, 1);
  desc.Usage = D3D10_USAGE_IMMUTABLE;

  data.SysMemPitch = aBounds.width * 4;
  data.pSysMem = alignedTextureData;

  RefPtr<ID3D10Texture2D> tex;
  hr = mDevice->CreateTexture2D(&desc, &data, byRef(tex));
	
  delete [] alignedTextureData;
  delete [] texture;

  if (FAILED(hr)) {
    return NULL;
  }

  return tex;
}

TemporaryRef<ID2D1Bitmap>
DrawTargetD2D::CreatePartialBitmapForSurface(DataSourceSurface *aSurface, Matrix &aMatrix, ExtendMode aExtendMode)
{
  RefPtr<ID2D1Bitmap> bitmap;

  // This is where things get complicated. The source surface was
  // created for a surface that was too large to fit in a texture.
  // We'll need to figure out if we can work with a partial upload
  // or downsample in software.

  Matrix transform = mTransform;
  Matrix invTransform = transform = aMatrix * transform;
  if (!invTransform.Invert()) {
    // Singular transform, nothing to be drawn.
    return NULL;
  }

  Rect rect(0, 0, mSize.width, mSize.height);

  // Calculate the rectangle of the source mapped to our surface.
  rect = invTransform.TransformBounds(rect);
  rect.RoundOut();

  IntSize size = aSurface->GetSize();

  Rect uploadRect(0, 0, size.width, size.height);

  // Limit the uploadRect as much as possible without supporting discontiguous uploads 
  //
  //                               region we will paint from
  //   uploadRect
  //   .---------------.              .---------------.         resulting uploadRect
  //   |               |rect          |               |
  //   |          .---------.         .----.     .----.          .---------------.
  //   |          |         |  ---->  |    |     |    |   ---->  |               |
  //   |          '---------'         '----'     '----'          '---------------'
  //   '---------------'              '---------------'
  //
  //

  if (uploadRect.Contains(rect)) {
    // Extend mode is irrelevant, the displayed rect is completely contained
    // by the source bitmap.
    uploadRect = rect;
  } else if (aExtendMode == EXTEND_CLAMP && uploadRect.Intersects(rect)) {
    // Calculate the rectangle on the source bitmap that touches our
    // surface, and upload that, for EXTEND_CLAMP we can actually guarantee
    // correct behaviour in this case.
    uploadRect = uploadRect.Intersect(rect);

    // We now proceed to check if we can limit at least one dimension of the
    // upload rect safely without looking at extend mode.
  } else if (rect.x >= 0 && rect.XMost() < size.width) {
    uploadRect.x = rect.x;
    uploadRect.width = rect.width;
  } else if (rect.y >= 0 && rect.YMost() < size.height) {
    uploadRect.y = rect.y;
    uploadRect.height = rect.height;
  }


  int stride = aSurface->Stride();

  if (uploadRect.width <= mRT->GetMaximumBitmapSize() &&
      uploadRect.height <= mRT->GetMaximumBitmapSize()) {

    // A partial upload will suffice.
    mRT->CreateBitmap(D2D1::SizeU(uint32_t(uploadRect.width), uint32_t(uploadRect.height)),
                      aSurface->GetData() + int(uploadRect.x) * 4 + int(uploadRect.y) * stride,
                      stride,
                      D2D1::BitmapProperties(D2DPixelFormat(aSurface->GetFormat())),
                      byRef(bitmap));

    aMatrix.Translate(uploadRect.x, uploadRect.y);

    return bitmap;
  } else {
    int Bpp = BytesPerPixel(aSurface->GetFormat());

    if (Bpp != 4) {
      // This shouldn't actually happen in practice!
      MOZ_ASSERT(false);
      return NULL;
    }

    ImageHalfScaler scaler(aSurface->GetData(), stride, size);

    // Calculate the maximum width/height of the image post transform.
    Point topRight = transform * Point(size.width, 0);
    Point topLeft = transform * Point(0, 0);
    Point bottomRight = transform * Point(size.width, size.height);
    Point bottomLeft = transform * Point(0, size.height);
    
    IntSize scaleSize;

    scaleSize.width = max(Distance(topRight, topLeft), Distance(bottomRight, bottomLeft));
    scaleSize.height = max(Distance(topRight, bottomRight), Distance(topLeft, bottomLeft));

    if (unsigned(scaleSize.width) > mRT->GetMaximumBitmapSize()) {
      // Ok, in this case we'd really want a downscale of a part of the bitmap,
      // perhaps we can do this later but for simplicity let's do something
      // different here and assume it's good enough, this should be rare!
      scaleSize.width = 4095;
    }
    if (unsigned(scaleSize.height) > mRT->GetMaximumBitmapSize()) {
      scaleSize.height = 4095;
    }

    scaler.ScaleForSize(scaleSize);

    IntSize newSize = scaler.GetSize();
    
    mRT->CreateBitmap(D2D1::SizeU(newSize.width, newSize.height),
                      scaler.GetScaledData(), scaler.GetStride(),
                      D2D1::BitmapProperties(D2DPixelFormat(aSurface->GetFormat())),
                      byRef(bitmap));

    aMatrix.Scale(size.width / newSize.width, size.height / newSize.height);
    return bitmap;
  }
}

void
DrawTargetD2D::SetupEffectForRadialGradient(const RadialGradientPattern *aPattern)
{
  mPrivateData->mEffect->GetTechniqueByName("SampleRadialGradient")->GetPassByIndex(0)->Apply(0);
  mPrivateData->mEffect->GetVariableByName("MaskTexCoords")->AsVector()->
    SetFloatVector(ShaderConstantRectD3D10(0, 0, 1.0f, 1.0f));

  float dimensions[] = { float(mSize.width), float(mSize.height), 0, 0 };
  mPrivateData->mEffect->GetVariableByName("dimensions")->AsVector()->
    SetFloatVector(dimensions);

  const GradientStopsD2D *stops =
    static_cast<const GradientStopsD2D*>(aPattern->mStops.get());

  RefPtr<ID3D10Texture2D> tex = CreateGradientTexture(stops);

  RefPtr<ID3D10ShaderResourceView> srView;
  mDevice->CreateShaderResourceView(tex, NULL, byRef(srView));

  mPrivateData->mEffect->GetVariableByName("tex")->AsShaderResource()->SetResource(srView);

  Point dc = aPattern->mCenter2 - aPattern->mCenter1;
  float dr = aPattern->mRadius2 - aPattern->mRadius1;

  float diffv[] = { dc.x, dc.y, dr, 0 };
  mPrivateData->mEffect->GetVariableByName("diff")->AsVector()->
    SetFloatVector(diffv);

  float center1[] = { aPattern->mCenter1.x, aPattern->mCenter1.y, dr, 0 };
  mPrivateData->mEffect->GetVariableByName("center1")->AsVector()->
    SetFloatVector(center1);

  mPrivateData->mEffect->GetVariableByName("radius1")->AsScalar()->
    SetFloat(aPattern->mRadius1);
  mPrivateData->mEffect->GetVariableByName("sq_radius1")->AsScalar()->
    SetFloat(pow(aPattern->mRadius1, 2));

  Matrix invTransform = mTransform;

  if (!invTransform.Invert()) {
    // Bail if the matrix is singular.
    return;
  }
  float matrix[] = { invTransform._11, invTransform._12, 0, 0,
                      invTransform._21, invTransform._22, 0, 0,
                      invTransform._31, invTransform._32, 1.0f, 0,
                      0, 0, 0, 1.0f };

  mPrivateData->mEffect->GetVariableByName("DeviceSpaceToUserSpace")->
    AsMatrix()->SetMatrix(matrix);

  float A = dc.x * dc.x + dc.y * dc.y - dr * dr;

  uint32_t offset = 0;
  switch (stops->mStopCollection->GetExtendMode()) {
  case D2D1_EXTEND_MODE_WRAP:
    offset = 1;
    break;
  case D2D1_EXTEND_MODE_MIRROR:
    offset = 2;
    break;
  default:
    gfxWarning() << "This shouldn't happen! Invalid extend mode for gradient stops.";
  }

  if (A == 0) {
    mPrivateData->mEffect->GetTechniqueByName("SampleRadialGradient")->
      GetPassByIndex(offset * 2 + 1)->Apply(0);
  } else {
    mPrivateData->mEffect->GetVariableByName("A")->AsScalar()->SetFloat(A);
    mPrivateData->mEffect->GetTechniqueByName("SampleRadialGradient")->
      GetPassByIndex(offset * 2)->Apply(0);
  }
}

void
DrawTargetD2D::SetupStateForRendering()
{
  UINT stride = sizeof(Vertex);
  UINT offset = 0;
  ID3D10Buffer *buff = mPrivateData->mVB;

  mDevice->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  mDevice->IASetVertexBuffers(0, 1, &buff, &stride, &offset);
  mDevice->IASetInputLayout(mPrivateData->mInputLayout);

  D3D10_VIEWPORT viewport;
  viewport.MaxDepth = 1;
  viewport.MinDepth = 0;
  viewport.Height = mSize.height;
  viewport.Width = mSize.width;
  viewport.TopLeftX = 0;
  viewport.TopLeftY = 0;

  mDevice->RSSetViewports(1, &viewport);
}

ID2D1Factory*
DrawTargetD2D::factory()
{
  if (mFactory) {
    return mFactory;
  }

  D2D1CreateFactoryFunc createD2DFactory;
  HMODULE d2dModule = LoadLibraryW(L"d2d1.dll");
  createD2DFactory = (D2D1CreateFactoryFunc)
      GetProcAddress(d2dModule, "D2D1CreateFactory");

  if (!createD2DFactory) {
    gfxWarning() << "Failed to locate D2D1CreateFactory function.";
    return NULL;
  }

  D2D1_FACTORY_OPTIONS options;
#ifdef _DEBUG
  options.debugLevel = D2D1_DEBUG_LEVEL_WARNING;
#else
  options.debugLevel = D2D1_DEBUG_LEVEL_NONE;
#endif

  HRESULT hr = createD2DFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,
                                __uuidof(ID2D1Factory),
                                &options,
                                (void**)&mFactory);

  if (FAILED(hr)) {
    gfxWarning() << "Failed to create Direct2D factory.";
  }

  return mFactory;
}

IDWriteFactory*
DrawTargetD2D::GetDWriteFactory()
{
  if (mDWriteFactory) {
    return mDWriteFactory;
  }

  DWriteCreateFactoryFunc createDWriteFactory;
  HMODULE dwriteModule = LoadLibraryW(L"dwrite.dll");
  createDWriteFactory = (DWriteCreateFactoryFunc)
    GetProcAddress(dwriteModule, "DWriteCreateFactory");

  if (!createDWriteFactory) {
    gfxWarning() << "Failed to locate DWriteCreateFactory function.";
    return NULL;
  }

  HRESULT hr = createDWriteFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&mDWriteFactory));

  if (FAILED(hr)) {
    gfxWarning() << "Failed to create DWrite Factory.";
  }

  return mDWriteFactory;
}

}
}
