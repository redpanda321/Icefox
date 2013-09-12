/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_GeckoContentController_h
#define mozilla_layers_GeckoContentController_h

#include "FrameMetrics.h"
#include "nsISupportsImpl.h"

namespace mozilla {
namespace layers {

class GeckoContentController {
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GeckoContentController)

  /**
   * Requests a paint of the given FrameMetrics |aFrameMetrics| from Gecko.
   * Implementations per-platform are responsible for actually handling this.
   */
  virtual void RequestContentRepaint(const FrameMetrics& aFrameMetrics) = 0;

  /**
   * Requests handling of a double tap. |aPoint| is in CSS pixels, relative to
   * the current scroll offset. This should eventually round-trip back to
   * AsyncPanZoomController::ZoomToRect with the dimensions that we want to zoom
   * to.
   */
  virtual void HandleDoubleTap(const nsIntPoint& aPoint) = 0;

  /**
   * Requests handling a single tap. |aPoint| is in CSS pixels, relative to the
   * current scroll offset. This should simulate and send to content a mouse
   * button down, then mouse button up at |aPoint|.
   */
  virtual void HandleSingleTap(const nsIntPoint& aPoint) = 0;

  /**
   * Requests handling a long tap. |aPoint| is in CSS pixels, relative to the
   * current scroll offset.
   */
  virtual void HandleLongTap(const nsIntPoint& aPoint) = 0;

  GeckoContentController() {}
  virtual ~GeckoContentController() {}
};

}
}

#endif // mozilla_layers_GeckoContentController_h
