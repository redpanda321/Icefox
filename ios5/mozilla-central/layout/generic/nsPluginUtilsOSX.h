/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
// vim:set ts=2 sts=2 sw=2 et cin:
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// We can use Carbon in this header but not Cocoa. Cocoa pointers must be void.

#if !defined(__LP64__) && defined(MOZ_WIDGET_COCOA)
#import <Carbon/Carbon.h>
#endif

#include "nsRect.h"
#include "nsIWidget.h"
#include "npapi.h"

// We use void pointers to avoid exporting native event types to cross-platform code.

#if !defined(__LP64__) && defined(MOZ_WIDGET_COCOA)
// Get the rect for an entire top-level Carbon window in screen coords.
void NS_NPAPI_CarbonWindowFrame(WindowRef aWindow, nsRect& outRect);
#endif

// Get the rect for an entire top-level Cocoa window in screen coords.
void NS_NPAPI_CocoaWindowFrame(void* aWindow, nsRect& outRect);

// Returns whether or not a Cocoa NSWindow has main status.
bool NS_NPAPI_CocoaWindowIsMain(void* aWindow);

// Puts up a Cocoa context menu (NSMenu) for a particular NPCocoaEvent.
NPError NS_NPAPI_ShowCocoaContextMenu(void* menu, nsIWidget* widget, NPCocoaEvent* event);

NPBool NS_NPAPI_ConvertPointCocoa(void* inView,
                                  double sourceX, double sourceY, NPCoordinateSpace sourceSpace,
                                  double *destX, double *destY, NPCoordinateSpace destSpace);

