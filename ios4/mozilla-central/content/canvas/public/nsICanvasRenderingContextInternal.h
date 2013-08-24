/* -*- Mode: C++; tab-width: 40; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 *   Vladimir Vukicevic <vladimir@pobox.com>
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#ifndef nsICanvasRenderingContextInternal_h___
#define nsICanvasRenderingContextInternal_h___

#include "nsISupports.h"
#include "nsIInputStream.h"
#include "nsIDocShell.h"
#include "gfxPattern.h"

// {EC90F32E-7848-4819-A1E3-02E64C682A72}
#define NS_ICANVASRENDERINGCONTEXTINTERNAL_IID \
{ 0xec90f32e, 0x7848, 0x4819, { 0xa1, 0xe3, 0x2, 0xe6, 0x4c, 0x68, 0x2a, 0x72 } }

class nsHTMLCanvasElement;
class gfxContext;
class gfxASurface;

namespace mozilla {
namespace layers {
class CanvasLayer;
class LayerManager;
}
namespace ipc {
class Shmem;
}
}

class nsICanvasRenderingContextInternal : public nsISupports {
public:
  typedef mozilla::layers::CanvasLayer CanvasLayer;
  typedef mozilla::layers::LayerManager LayerManager;

  NS_DECLARE_STATIC_IID_ACCESSOR(NS_ICANVASRENDERINGCONTEXTINTERNAL_IID)

  // This method should NOT hold a ref to aParentCanvas; it will be called
  // with nsnull when the element is going away.
  NS_IMETHOD SetCanvasElement(nsHTMLCanvasElement* aParentCanvas) = 0;

  // Sets the dimensions of the canvas, in pixels.  Called
  // whenever the size of the element changes.
  NS_IMETHOD SetDimensions(PRInt32 width, PRInt32 height) = 0;

  NS_IMETHOD InitializeWithSurface(nsIDocShell *docShell, gfxASurface *surface, PRInt32 width, PRInt32 height) = 0;

  // Render the canvas at the origin of the given gfxContext
  NS_IMETHOD Render(gfxContext *ctx, gfxPattern::GraphicsFilter aFilter) = 0;

  // Gives you a stream containing the image represented by this context.
  // The format is given in aMimeTime, for example "image/png".
  //
  // If the image format does not support transparency or aIncludeTransparency
  // is false, alpha will be discarded and the result will be the image
  // composited on black.
  NS_IMETHOD GetInputStream(const char *aMimeType,
                            const PRUnichar *aEncoderOptions,
                            nsIInputStream **aStream) = 0;

  // If this canvas context can be represented with a simple Thebes surface,
  // return the surface.  Otherwise returns an error.
  NS_IMETHOD GetThebesSurface(gfxASurface **surface) = 0;

  // If this context is opaque, the backing store of the canvas should
  // be created as opaque; all compositing operators should assume the
  // dst alpha is always 1.0.  If this is never called, the context
  // defaults to false (not opaque).
  NS_IMETHOD SetIsOpaque(PRBool isOpaque) = 0;

  // Invalidate this context and release any held resources, in preperation
  // for possibly reinitializing with SetDimensions/InitializeWithSurface.
  NS_IMETHOD Reset() = 0;

  // Return the CanvasLayer for this context, creating
  // one for the given layer manager if not available.
  virtual already_AddRefed<CanvasLayer> GetCanvasLayer(CanvasLayer *aOldLayer,
                                                       LayerManager *aManager) = 0;

  virtual void MarkContextClean() = 0;

  // Redraw the dirty rectangle of this canvas.
  NS_IMETHOD Redraw(const gfxRect &dirty) = 0;

  // If this context can be set to use Mozilla's Shmem segments as its backing
  // store, this will set it to that state. Note that if you have drawn
  // anything into this canvas before changing the shmem state, it will be
  // lost.
  NS_IMETHOD SetIsIPC(PRBool isIPC) = 0;

  // Swap this back buffer with the front, and copy its contents to the new
  // back. x, y, w, and h specify the area of |back| that is dirty.
  NS_IMETHOD Swap(mozilla::ipc::Shmem& back,
                  PRInt32 x, PRInt32 y, PRInt32 w, PRInt32 h) = 0;

  // Sync back and front buffer, move ownership of back buffer to parent
  NS_IMETHOD Swap(PRUint32 nativeID,
                  PRInt32 x, PRInt32 y, PRInt32 w, PRInt32 h) = 0;

};

NS_DEFINE_STATIC_IID_ACCESSOR(nsICanvasRenderingContextInternal,
                              NS_ICANVASRENDERINGCONTEXTINTERNAL_IID)

#endif /* nsICanvasRenderingContextInternal_h___ */
