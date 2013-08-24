/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_DEVICEMANAGERD3D9_H
#define GFX_DEVICEMANAGERD3D9_H

#include "gfxTypes.h"
#include "nsRect.h"
#include "nsAutoPtr.h"
#include "d3d9.h"
#include "nsTArray.h"

namespace mozilla {
namespace layers {

class DeviceManagerD3D9;
class LayerD3D9;
class Nv3DVUtils;
class Layer;

// Shader Constant locations
const int CBmLayerTransform = 0;
const int CBmProjection = 4;
const int CBvRenderTargetOffset = 8;
const int CBvTextureCoords = 9;
const int CBvLayerQuad = 10;
const int CBfLayerOpacity = 0;

/**
 * SwapChain class, this class manages the swap chain belonging to a
 * LayerManagerD3D9.
 */
class THEBES_API SwapChainD3D9
{
  NS_INLINE_DECL_REFCOUNTING(SwapChainD3D9)
public:
  ~SwapChainD3D9();

  /**
   * This function will prepare the device this swap chain belongs to for
   * rendering to this swap chain. Only after calling this function can the
   * swap chain be drawn to, and only until this function is called on another
   * swap chain belonging to this device will the device draw to it. Passed in
   * is the size of the swap chain. If the window size differs from the size
   * during the last call to this function the swap chain will resize. Note that
   * in no case does this function guarantee the backbuffer to still have its
   * old content.
   */
  bool PrepareForRendering();

  /**
   * This function will present the selected rectangle of the swap chain to
   * its associated window.
   */
  void Present(const nsIntRect &aRect);

private:
  friend class DeviceManagerD3D9;

  SwapChainD3D9(DeviceManagerD3D9 *aDeviceManager);
  
  bool Init(HWND hWnd);

  /**
   * This causes us to release our swap chain, clearing out our resource usage
   * so the master device may reset.
   */
  void Reset();

  nsRefPtr<IDirect3DSwapChain9> mSwapChain;
  nsRefPtr<DeviceManagerD3D9> mDeviceManager;
  HWND mWnd;
};

/**
 * Device manager, this class is used by the layer managers to share the D3D9
 * device and create swap chains for the individual windows the layer managers
 * belong to.
 */
class THEBES_API DeviceManagerD3D9
{
public:
  DeviceManagerD3D9();
  NS_IMETHOD_(nsrefcnt) AddRef(void);
  NS_IMETHOD_(nsrefcnt) Release(void);
protected:
  nsAutoRefCnt mRefCnt;
  NS_DECL_OWNINGTHREAD

public:
  bool Init();

  /**
   * Sets up the render state for the device for layer rendering.
   */
  void SetupRenderState();

  /**
   * Create a swap chain setup to work with the specified window.
   */
  already_AddRefed<SwapChainD3D9> CreateSwapChain(HWND hWnd);

  IDirect3DDevice9 *device() { return mDevice; }

  bool IsD3D9Ex() { return mDeviceEx; }

  bool HasDynamicTextures() { return mHasDynamicTextures; }

  enum ShaderMode {
    RGBLAYER,
    RGBALAYER,
    COMPONENTLAYERPASS1,
    COMPONENTLAYERPASS2,
    YCBCRLAYER,
    SOLIDCOLORLAYER
  };

  void SetShaderMode(ShaderMode aMode, Layer* aMask, bool aIs2D);

  /** 
   * Return pointer to the Nv3DVUtils instance 
   */ 
  Nv3DVUtils *GetNv3DVUtils()  { return mNv3DVUtils; }

  /**
   * Returns true if this device was removed.
   */
  bool DeviceWasRemoved() { return mDeviceWasRemoved; }

  PRUint32 GetDeviceResetCount() { return mDeviceResetCount; }

  /**
   * We keep a list of all layers here that may have hardware resource allocated
   * so we can clean their resources on reset.
   */
  nsTArray<LayerD3D9*> mLayersWithResources;

  PRInt32 GetMaxTextureSize() { return mMaxTextureSize; }

private:
  friend class SwapChainD3D9;

  ~DeviceManagerD3D9();

  /**
   * This function verifies the device is ready for rendering, internally this
   * will test the cooperative level of the device and reset the device if
   * needed. If this returns false subsequent rendering calls may return errors.
   */
  bool VerifyReadyForRendering();

  /**
   * This will fill our vertex buffer with the data of our quad, it may be
   * called when the vertex buffer is recreated.
   */
  bool CreateVertexBuffer();

  /* Array used to store all swap chains for device resets */
  nsTArray<SwapChainD3D9*> mSwapChains;

  /* The D3D device we use */
  nsRefPtr<IDirect3DDevice9> mDevice;

  /* The D3D9Ex device - only valid on Vista+ with WDDM */
  nsRefPtr<IDirect3DDevice9Ex> mDeviceEx;

  /* An instance of the D3D9 object */
  nsRefPtr<IDirect3D9> mD3D9;

  /* An instance of the D3D9Ex object - only valid on Vista+ with WDDM */
  nsRefPtr<IDirect3D9Ex> mD3D9Ex;

  /* Vertex shader used for layer quads */
  nsRefPtr<IDirect3DVertexShader9> mLayerVS;

  /* Pixel shader used for RGB textures */
  nsRefPtr<IDirect3DPixelShader9> mRGBPS;

  /* Pixel shader used for RGBA textures */
  nsRefPtr<IDirect3DPixelShader9> mRGBAPS;

  /* Pixel shader used for component alpha textures (pass 1) */
  nsRefPtr<IDirect3DPixelShader9> mComponentPass1PS;

  /* Pixel shader used for component alpha textures (pass 2) */
  nsRefPtr<IDirect3DPixelShader9> mComponentPass2PS;

  /* Pixel shader used for RGB textures */
  nsRefPtr<IDirect3DPixelShader9> mYCbCrPS;

  /* Pixel shader used for solid colors */
  nsRefPtr<IDirect3DPixelShader9> mSolidColorPS;

  /* As above, but using a mask layer */
  nsRefPtr<IDirect3DVertexShader9> mLayerVSMask;
  nsRefPtr<IDirect3DVertexShader9> mLayerVSMask3D;
  nsRefPtr<IDirect3DPixelShader9> mRGBPSMask;
  nsRefPtr<IDirect3DPixelShader9> mRGBAPSMask;
  nsRefPtr<IDirect3DPixelShader9> mRGBAPSMask3D;
  nsRefPtr<IDirect3DPixelShader9> mComponentPass1PSMask;
  nsRefPtr<IDirect3DPixelShader9> mComponentPass2PSMask;
  nsRefPtr<IDirect3DPixelShader9> mYCbCrPSMask;
  nsRefPtr<IDirect3DPixelShader9> mSolidColorPSMask;

  /* Vertex buffer containing our basic vertex structure */
  nsRefPtr<IDirect3DVertexBuffer9> mVB;

  /* Our vertex declaration */
  nsRefPtr<IDirect3DVertexDeclaration9> mVD;

  /* Our focus window - this is really a dummy window we can associate our
   * device with.
   */
  HWND mFocusWnd;

  /* we use this to help track if our device temporarily or permanently lost */
  HMONITOR mDeviceMonitor;

  PRUint32 mDeviceResetCount;

  PRUint32 mMaxTextureSize;

  /* If this device supports dynamic textures */
  bool mHasDynamicTextures;

  /* If this device was removed */
  bool mDeviceWasRemoved;

  /* Nv3DVUtils instance */ 
  nsAutoPtr<Nv3DVUtils> mNv3DVUtils; 

  /**
   * Verifies all required device capabilities are present.
   */
  bool VerifyCaps();
};

} /* namespace layers */
} /* namespace mozilla */

#endif /* GFX_DEVICEMANAGERD3D9_H */
