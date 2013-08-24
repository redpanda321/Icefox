/* -*- Mode: objc; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Util.h"

#ifdef MOZ_LOGGING
#define FORCE_PR_LOG
#endif
#include "prlog.h"

#include <unistd.h>
#include <math.h>
 
#include "nsChildView.h"
#include "nsCocoaWindow.h"

#include "nsObjCExceptions.h"
#include "nsCOMPtr.h"
#include "nsToolkit.h"
#include "nsCRT.h"

#include "nsFontMetrics.h"
#include "nsIRollupListener.h"
#include "nsIViewManager.h"
#include "nsIInterfaceRequestor.h"
#include "nsIFile.h"
#include "nsILocalFileMac.h"
#include "nsGfxCIID.h"
#include "nsIDOMSimpleGestureEvent.h"
#include "nsNPAPIPluginInstance.h"
#include "nsThemeConstants.h"

#include "nsDragService.h"
#include "nsClipboard.h"
#include "nsCursorManager.h"
#include "nsWindowMap.h"
#include "nsCocoaFeatures.h"
#include "nsCocoaUtils.h"
#include "nsMenuUtilsX.h"
#include "nsMenuBarX.h"
#ifdef __LP64__
#include "ComplexTextInputPanel.h"
#endif

#include "gfxContext.h"
#include "gfxQuartzSurface.h"
#include "nsRegion.h"
#include "Layers.h"
#include "LayerManagerOGL.h"
#include "GLContext.h"
#include "mozilla/layers/CompositorCocoaWidgetHelper.h"
#ifdef ACCESSIBILITY
#include "nsAccessibilityService.h"
#endif

#include "mozilla/Preferences.h"

#include <dlfcn.h>

#include <ApplicationServices/ApplicationServices.h>

#include "sampler.h"

using namespace mozilla;
using namespace mozilla::layers;
using namespace mozilla::gl;
using namespace mozilla::widget;
using namespace mozilla;

#undef DEBUG_UPDATE
#undef INVALIDATE_DEBUGGING  // flash areas as they are invalidated

// Don't put more than this many rects in the dirty region, just fluff
// out to the bounding-box if there are more
#define MAX_RECTS_IN_REGION 100

#ifdef PR_LOGGING
PRLogModuleInfo* sCocoaLog = nsnull;
#endif

extern "C" {
  CG_EXTERN void CGContextResetCTM(CGContextRef);
  CG_EXTERN void CGContextSetCTM(CGContextRef, CGAffineTransform);
  CG_EXTERN void CGContextResetClip(CGContextRef);
}

// defined in nsMenuBarX.mm
extern NSMenu* sApplicationMenu; // Application menu shared by all menubars

// these are defined in nsCocoaWindow.mm
extern bool gConsumeRollupEvent;

bool gChildViewMethodsSwizzled = false;

extern nsISupportsArray *gDraggedTransferables;

ChildView* ChildViewMouseTracker::sLastMouseEventView = nil;
NSEvent* ChildViewMouseTracker::sLastMouseMoveEvent = nil;
NSWindow* ChildViewMouseTracker::sWindowUnderMouse = nil;
NSPoint ChildViewMouseTracker::sLastScrollEventScreenLocation = NSZeroPoint;

#ifdef INVALIDATE_DEBUGGING
static void blinkRect(Rect* r);
static void blinkRgn(RgnHandle rgn);
#endif

nsIRollupListener * gRollupListener = nsnull;
nsIWidget         * gRollupWidget   = nsnull;

bool gUserCancelledDrag = false;

PRUint32 nsChildView::sLastInputEventCount = 0;

@interface ChildView(Private)

// sets up our view, attaching it to its owning gecko view
- (id)initWithFrame:(NSRect)inFrame geckoChild:(nsChildView*)inChild;
- (void)forceRefreshOpenGL;

// set up a gecko mouse event based on a cocoa mouse event
- (void) convertCocoaMouseEvent:(NSEvent*)aMouseEvent toGeckoEvent:(nsInputEvent*)outGeckoEvent;

- (NSMenu*)contextMenu;

- (void)setIsPluginView:(BOOL)aIsPlugin;
- (void)setPluginEventModel:(NPEventModel)eventModel;
- (void)setPluginDrawingModel:(NPDrawingModel)drawingModel;
- (NPDrawingModel)pluginDrawingModel;

- (BOOL)isRectObscuredBySubview:(NSRect)inRect;

- (void)processPendingRedraws;

- (void)drawRect:(NSRect)aRect inContext:(CGContextRef)aContext;

// Called using performSelector:withObject:afterDelay:0 to release
// aWidgetArray (and its contents) the next time through the run loop.
- (void)releaseWidgets:(NSArray*)aWidgetArray;

#if USE_CLICK_HOLD_CONTEXTMENU
 // called on a timer two seconds after a mouse down to see if we should display
 // a context menu (click-hold)
- (void)clickHoldCallback:(id)inEvent;
#endif

#ifdef ACCESSIBILITY
- (id<mozAccessible>)accessible;
#endif

- (BOOL)inactiveWindowAcceptsMouseEvent:(NSEvent*)aEvent;

@end

#pragma mark -

/* Convenience routines to go from a gecko rect to cocoa NSRects and back
 *
 * Gecko rects (nsRect) contain an origin (x,y) in a coordinate
 * system with (0,0) in the top-left of the screen. Cocoa rects
 * (NSRect) contain an origin (x,y) in a coordinate system with
 * (0,0) in the bottom-left of the screen. Both nsRect and NSRect
 * contain width/height info, with no difference in their use.
 * If a Cocoa rect is from a flipped view, there is no need to
 * convert coordinate systems.
 */

static inline void
NSRectToGeckoRect(const NSRect & inCocoaRect, nsIntRect & outGeckoRect)
{
  outGeckoRect.x = NSToIntRound(inCocoaRect.origin.x);
  outGeckoRect.y = NSToIntRound(inCocoaRect.origin.y);
  outGeckoRect.width = NSToIntRound(inCocoaRect.origin.x + inCocoaRect.size.width) - outGeckoRect.x;
  outGeckoRect.height = NSToIntRound(inCocoaRect.origin.y + inCocoaRect.size.height) - outGeckoRect.y;
}

static inline void 
ConvertGeckoRectToMacRect(const nsIntRect& aRect, Rect& outMacRect)
{
  outMacRect.left = aRect.x;
  outMacRect.top = aRect.y;
  outMacRect.right = aRect.x + aRect.width;
  outMacRect.bottom = aRect.y + aRect.height;
}

// Flips a screen coordinate from a point in the cocoa coordinate system (bottom-left rect) to a point
// that is a "flipped" cocoa coordinate system (starts in the top-left).
static inline void
FlipCocoaScreenCoordinate(NSPoint &inPoint)
{  
  inPoint.y = nsCocoaUtils::FlippedScreenY(inPoint.y);
}

void EnsureLogInitialized()
{
#ifdef PR_LOGGING
  if (!sCocoaLog) {
    sCocoaLog = PR_NewLogModule("nsCocoaWidgets");
  }
#endif // PR_LOGGING
}

#pragma mark -

nsChildView::nsChildView() : nsBaseWidget()
, mView(nsnull)
, mParentView(nsnull)
, mParentWidget(nsnull)
, mVisible(false)
, mDrawing(false)
, mPluginDrawing(false)
, mIsDispatchPaint(false)
, mPluginInstanceOwner(nsnull)
{
  EnsureLogInitialized();

  memset(&mPluginCGContext, 0, sizeof(mPluginCGContext));
#ifndef NP_NO_QUICKDRAW
  memset(&mPluginQDPort, 0, sizeof(mPluginQDPort));
#endif

  SetBackgroundColor(NS_RGB(255, 255, 255));
  SetForegroundColor(NS_RGB(0, 0, 0));
}

nsChildView::~nsChildView()
{
  // Notify the children that we're gone.  childView->ResetParent() can change
  // our list of children while it's being iterated, so the way we iterate the
  // list must allow for this.
  for (nsIWidget* kid = mLastChild; kid;) {
    nsChildView* childView = static_cast<nsChildView*>(kid);
    kid = kid->GetPrevSibling();
    childView->ResetParent();
  }

  NS_WARN_IF_FALSE(mOnDestroyCalled, "nsChildView object destroyed without calling Destroy()");

  mResizerImage = nsnull;

  // An nsChildView object that was in use can be destroyed without Destroy()
  // ever being called on it.  So we also need to do a quick, safe cleanup
  // here (it's too late to just call Destroy(), which can cause crashes).
  // It's particularly important to make sure widgetDestroyed is called on our
  // mView -- this method NULLs mView's mGeckoChild, and NULL checks on
  // mGeckoChild are used throughout the ChildView class to tell if it's safe
  // to use a ChildView object.
  [mView widgetDestroyed]; // Safe if mView is nil.
  mParentWidget = nil;
  TearDownView(); // Safe if called twice.
}

NS_IMPL_ISUPPORTS_INHERITED1(nsChildView, nsBaseWidget, nsIPluginWidget)

nsresult nsChildView::Create(nsIWidget *aParent,
                             nsNativeWidget aNativeParent,
                             const nsIntRect &aRect,
                             EVENT_CALLBACK aHandleEventFunction,
                             nsDeviceContext *aContext,
                             nsWidgetInitData *aInitData)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  // Because the hidden window is created outside of an event loop,
  // we need to provide an autorelease pool to avoid leaking cocoa objects
  // (see bug 559075).
  nsAutoreleasePool localPool;

  // See NSView (MethodSwizzling) below.
  if (!gChildViewMethodsSwizzled) {
    nsToolkit::SwizzleMethods([NSView class], @selector(mouseDownCanMoveWindow),
                              @selector(nsChildView_NSView_mouseDownCanMoveWindow));
#ifdef __LP64__
    if (nsCocoaFeatures::OnLionOrLater()) {
      nsToolkit::SwizzleMethods([NSEvent class], @selector(addLocalMonitorForEventsMatchingMask:handler:),
                                @selector(nsChildView_NSEvent_addLocalMonitorForEventsMatchingMask:handler:),
                                true);
      nsToolkit::SwizzleMethods([NSEvent class], @selector(removeMonitor:),
                                @selector(nsChildView_NSEvent_removeMonitor:), true);
    }
#endif
#ifndef NP_NO_CARBON
    TextInputHandler::SwizzleMethods();
#endif
    gChildViewMethodsSwizzled = true;
  }

  mBounds = aRect;

  // Ensure that the toolkit is created.
  nsToolkit::GetToolkit();

  BaseCreate(aParent, aRect, aHandleEventFunction, aContext, aInitData);

  // inherit things from the parent view and create our parallel 
  // NSView in the Cocoa display system
  mParentView = nil;
  if (aParent) {
    // This is the case when we're the popup content view of a popup window.
    SetBackgroundColor(aParent->GetBackgroundColor());
    SetForegroundColor(aParent->GetForegroundColor());

    // inherit the top-level window. NS_NATIVE_WIDGET is always a NSView
    // regardless of if we're asking a window or a view (for compatibility
    // with windows).
    mParentView = (NSView<mozView>*)aParent->GetNativeData(NS_NATIVE_WIDGET); 
    mParentWidget = aParent;   
  } else {
    // This is the normal case. When we're the root widget of the view hiararchy,
    // aNativeParent will be the contentView of our window, since that's what
    // nsCocoaWindow returns when asked for an NS_NATIVE_VIEW.
    mParentView = reinterpret_cast<NSView<mozView>*>(aNativeParent);
  }
  
  // create our parallel NSView and hook it up to our parent. Recall
  // that NS_NATIVE_WIDGET is the NSView.
  NSRect r;
  nsCocoaUtils::GeckoRectToNSRect(mBounds, r);
  mView = [(NSView<mozView>*)CreateCocoaView(r) retain];
  if (!mView) return NS_ERROR_FAILURE;

  [(ChildView*)mView setIsPluginView:(mWindowType == eWindowType_plugin)];

  // If this view was created in a Gecko view hierarchy, the initial state
  // is hidden.  If the view is attached only to a native NSView but has
  // no Gecko parent (as in embedding), the initial state is visible.
  if (mParentWidget)
    [mView setHidden:YES];
  else
    mVisible = true;

  // Hook it up in the NSView hierarchy.
  if (mParentView) {
    [mParentView addSubview:mView];
  }

  // if this is a ChildView, make sure that our per-window data
  // is set up
  if ([mView isKindOfClass:[ChildView class]])
    [[WindowDataMap sharedWindowDataMap] ensureDataForWindow:[mView window]];

  NS_ASSERTION(!mTextInputHandler, "mTextInputHandler has already existed");
  mTextInputHandler = new TextInputHandler(this, mView);

  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

// Creates the appropriate child view. Override to create something other than
// our |ChildView| object. Autoreleases, so caller must retain.
NSView*
nsChildView::CreateCocoaView(NSRect inFrame)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NIL;

  return [[[ChildView alloc] initWithFrame:inFrame geckoChild:this] autorelease];

  NS_OBJC_END_TRY_ABORT_BLOCK_NIL;
}

void nsChildView::TearDownView()
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!mView)
    return;

  NSWindow* win = [mView window];
  NSResponder* responder = [win firstResponder];

  // We're being unhooked from the view hierarchy, don't leave our view
  // or a child view as the window first responder.
  if (responder && [responder isKindOfClass:[NSView class]] &&
      [(NSView*)responder isDescendantOf:mView]) {
    [win makeFirstResponder:[mView superview]];
  }

  // If mView is win's contentView, win (mView's NSWindow) "owns" mView --
  // win has retained mView, and will detach it from the view hierarchy and
  // release it when necessary (when win is itself destroyed (in a call to
  // [win dealloc])).  So all we need to do here is call [mView release] (to
  // match the call to [mView retain] in nsChildView::StandardCreate()).
  // Also calling [mView removeFromSuperviewWithoutNeedingDisplay] causes
  // mView to be released again and dealloced, while remaining win's
  // contentView.  So if we do that here, win will (for a short while) have
  // an invalid contentView (for the consequences see bmo bugs 381087 and
  // 374260).
  if ([mView isEqual:[win contentView]]) {
    [mView release];
  } else {
    // Stop NSView hierarchy being changed during [ChildView drawRect:]
    [mView performSelectorOnMainThread:@selector(delayedTearDown) withObject:nil waitUntilDone:false];
  }
  mView = nil;

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

nsCocoaWindow*
nsChildView::GetXULWindowWidget()
{
  id windowDelegate = [[mView window] delegate];
  if (windowDelegate && [windowDelegate isKindOfClass:[WindowDelegate class]]) {
    return [(WindowDelegate *)windowDelegate geckoWidget];
  }
  return nsnull;
}

NS_IMETHODIMP nsChildView::Destroy()
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  if (mOnDestroyCalled)
    return NS_OK;
  mOnDestroyCalled = true;

  [mView widgetDestroyed];

  nsBaseWidget::Destroy();

  ReportDestroyEvent(); 
  mParentWidget = nil;

  TearDownView();

  nsBaseWidget::OnDestroy();

  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

#pragma mark -

#if 0
static void PrintViewHierarchy(NSView *view)
{
  while (view) {
    NSLog(@"  view is %x, frame %@", view, NSStringFromRect([view frame]));
    view = [view superview];
  }
}
#endif

// Return native data according to aDataType
void* nsChildView::GetNativeData(PRUint32 aDataType)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSNULL;

  void* retVal = nsnull;

  switch (aDataType) 
  {
    case NS_NATIVE_WIDGET:
    case NS_NATIVE_DISPLAY:
      retVal = (void*)mView;
      break;

    case NS_NATIVE_WINDOW:
      retVal = [mView window];
      break;

    case NS_NATIVE_GRAPHIC:
      NS_ERROR("Requesting NS_NATIVE_GRAPHIC on a Mac OS X child view!");
      retVal = nsnull;
      break;

    case NS_NATIVE_OFFSETX:
      retVal = 0;
      break;

    case NS_NATIVE_OFFSETY:
      retVal = 0;
      break;

    case NS_NATIVE_PLUGIN_PORT:
    case NS_NATIVE_PLUGIN_PORT_QD:
    case NS_NATIVE_PLUGIN_PORT_CG:
    {
      // The NP_CGContext pointer should always be NULL in the Cocoa event model.
      if ([(ChildView*)mView pluginEventModel] == NPEventModelCocoa)
        return nsnull;

      UpdatePluginPort();
#ifndef NP_NO_QUICKDRAW
      if (aDataType != NS_NATIVE_PLUGIN_PORT_CG) {
        retVal = (void*)&mPluginQDPort;
        break;
      }
#endif
      retVal = (void*)&mPluginCGContext;
      break;
    }
  }

  return retVal;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSNULL;
}

#pragma mark -

nsTransparencyMode nsChildView::GetTransparencyMode()
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN;

  nsCocoaWindow* windowWidget = GetXULWindowWidget();
  return windowWidget ? windowWidget->GetTransparencyMode() : eTransparencyOpaque;

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(eTransparencyOpaque);
}

// This is called by nsContainerFrame on the root widget for all window types
// except popup windows (when nsCocoaWindow::SetTransparencyMode is used instead).
void nsChildView::SetTransparencyMode(nsTransparencyMode aMode)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  nsCocoaWindow* windowWidget = GetXULWindowWidget();
  if (windowWidget) {
    windowWidget->SetTransparencyMode(aMode);
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

NS_IMETHODIMP nsChildView::IsVisible(bool& outState)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  if (!mVisible) {
    outState = mVisible;
  }
  else {
    // mVisible does not accurately reflect the state of a hidden tabbed view
    // so verify that the view has a window as well
    outState = ([mView window] != nil);
    // now check native widget hierarchy visibility
    if (outState && NSIsEmptyRect([mView visibleRect])) {
      outState = false;
    }
  }

  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

void nsChildView::HidePlugin()
{
  NS_ASSERTION(mWindowType == eWindowType_plugin,
               "HidePlugin called on non-plugin view");

#ifndef NP_NO_QUICKDRAW
  if (mPluginInstanceOwner && mView &&
      [(ChildView*)mView pluginDrawingModel] == NPDrawingModelQuickDraw) {
    NPWindow* window;
    mPluginInstanceOwner->GetWindow(window);
    nsRefPtr<nsNPAPIPluginInstance> instance;
    mPluginInstanceOwner->GetInstance(getter_AddRefs(instance));
    if (window && instance) {
       window->clipRect.top = 0;
       window->clipRect.left = 0;
       window->clipRect.bottom = 0;
       window->clipRect.right = 0;
       instance->SetWindow(window);
    }
  }
#endif
}

void nsChildView::UpdatePluginPort()
{
  NS_ASSERTION(mWindowType == eWindowType_plugin,
               "UpdatePluginPort called on non-plugin view");

#if !defined(NP_NO_CARBON) || !defined(NP_NO_QUICKDRAW)
  NSWindow* cocoaWindow = [mView window];
  WindowRef carbonWindow = cocoaWindow ? (WindowRef)[cocoaWindow windowRef] : NULL;
#endif

  if (!mView
#ifndef NP_NO_QUICKDRAW
    || [(ChildView*)mView pluginDrawingModel] != NPDrawingModelQuickDraw
#endif
    ) {
    // [NSGraphicsContext currentContext] is supposed to "return the
    // current graphics context of the current thread."  But sometimes
    // (when called while mView isn't focused for drawing) it returns a
    // graphics context for the wrong window.  [window graphicsContext]
    // (which "provides the graphics context associated with the window
    // for the current thread") seems always to return the "right"
    // graphics context.  See bug 500130.
    mPluginCGContext.context = NULL;
    mPluginCGContext.window = NULL;
#ifndef NP_NO_CARBON
    if (carbonWindow) {
      mPluginCGContext.context = (CGContextRef)[[cocoaWindow graphicsContext] graphicsPort];
      mPluginCGContext.window = carbonWindow;
    }
#endif
  }
#ifndef NP_NO_QUICKDRAW
  else {
    if (carbonWindow) {
      mPluginQDPort.port = ::GetWindowPort(carbonWindow);

      NSPoint viewOrigin = [mView convertPoint:NSZeroPoint toView:nil];
      NSRect frame = [[cocoaWindow contentView] frame];
      viewOrigin.y = frame.size.height - viewOrigin.y;

      // need to convert view's origin to window coordinates.
      // then, encode as "SetOrigin" ready values.
      mPluginQDPort.portx = (PRInt32)-viewOrigin.x;
      mPluginQDPort.porty = (PRInt32)-viewOrigin.y;
    } else {
      mPluginQDPort.port = NULL;
    }
  }
#endif
}

static void HideChildPluginViews(NSView* aView)
{
  NSArray* subviews = [aView subviews];

  for (unsigned int i = 0; i < [subviews count]; ++i) {
    NSView* view = [subviews objectAtIndex: i];

    if (![view isKindOfClass:[ChildView class]])
      continue;

    ChildView* childview = static_cast<ChildView*>(view);
    if ([childview isPluginView]) {
      nsChildView* widget = static_cast<nsChildView*>([childview widget]);
      if (widget) {
        widget->HidePlugin();
      }
    } else {
      HideChildPluginViews(view);
    }
  }
}

// Hide or show this component
NS_IMETHODIMP nsChildView::Show(bool aState)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  if (aState != mVisible) {
    // Provide an autorelease pool because this gets called during startup
    // on the "hidden window", resulting in cocoa object leakage if there's
    // no pool in place.
    nsAutoreleasePool localPool;

    [mView setHidden:!aState];
    mVisible = aState;
    if (!mVisible && IsPluginView())
      HidePlugin();
  }
  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

// Change the parent of this widget
NS_IMETHODIMP
nsChildView::SetParent(nsIWidget* aNewParent)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  if (mOnDestroyCalled)
    return NS_OK;

  nsCOMPtr<nsIWidget> kungFuDeathGrip(this);
  
  if (mParentWidget) {
    mParentWidget->RemoveChild(this);
  }

  if (aNewParent) {
    ReparentNativeWidget(aNewParent);
  } else {
    [mView removeFromSuperview];
    mParentView = nil;
  }

  mParentWidget = aNewParent;

  if (mParentWidget) {
    mParentWidget->AddChild(this);
  }

  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

NS_IMETHODIMP
nsChildView::ReparentNativeWidget(nsIWidget* aNewParent)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  NS_PRECONDITION(aNewParent, "");

  if (mOnDestroyCalled)
    return NS_OK;

  NSView<mozView>* newParentView =
   (NSView<mozView>*)aNewParent->GetNativeData(NS_NATIVE_WIDGET); 
  NS_ENSURE_TRUE(newParentView, NS_ERROR_FAILURE);

  // we hold a ref to mView, so this is safe
  [mView removeFromSuperview];
  mParentView = newParentView;
  [mParentView addSubview:mView];
  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

void nsChildView::ResetParent()
{
  if (!mOnDestroyCalled) {
    if (mParentWidget)
      mParentWidget->RemoveChild(this);
    if (mView)
      [mView removeFromSuperview];
  }
  mParentWidget = nsnull;
}

nsIWidget*
nsChildView::GetParent()
{
  return mParentWidget;
}

float
nsChildView::GetDPI()
{
  NSWindow* window = [mView window];
  if (window && [window isKindOfClass:[BaseWindow class]]) {
    return [(BaseWindow*)window getDPI];
  }

  return 96.0;
}

NS_IMETHODIMP nsChildView::Enable(bool aState)
{
  return NS_OK;
}

NS_IMETHODIMP nsChildView::IsEnabled(bool *aState)
{
  // unimplemented
  if (aState)
   *aState = true;
  return NS_OK;
}

NS_IMETHODIMP nsChildView::SetFocus(bool aRaise)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  NSWindow* window = [mView window];
  if (window)
    [window makeFirstResponder:mView];
  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

// Override to set the cursor on the mac
NS_IMETHODIMP nsChildView::SetCursor(nsCursor aCursor)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  if ([mView isDragInProgress])
    return NS_OK; // Don't change the cursor during dragging.

  nsBaseWidget::SetCursor(aCursor);
  return [[nsCursorManager sharedInstance] setCursor:aCursor];

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

// implement to fix "hidden virtual function" warning
NS_IMETHODIMP nsChildView::SetCursor(imgIContainer* aCursor,
                                      PRUint32 aHotspotX, PRUint32 aHotspotY)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  nsBaseWidget::SetCursor(aCursor, aHotspotX, aHotspotY);
  return [[nsCursorManager sharedInstance] setCursorWithImage:aCursor hotSpotX:aHotspotX hotSpotY:aHotspotY];

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

#pragma mark -

// Get this component dimension
NS_IMETHODIMP nsChildView::GetBounds(nsIntRect &aRect)
{
  if (!mView) {
    aRect = mBounds;
  } else {
    NSRect frame = [mView frame];
    NSRectToGeckoRect(frame, aRect);
  }
  return NS_OK;
}

NS_IMETHODIMP nsChildView::ConstrainPosition(bool aAllowSlop,
                                             PRInt32 *aX, PRInt32 *aY)
{
  return NS_OK;
}

// Move this component, aX and aY are in the parent widget coordinate system
NS_IMETHODIMP nsChildView::Move(PRInt32 aX, PRInt32 aY)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  if (!mView || (mBounds.x == aX && mBounds.y == aY))
    return NS_OK;

  mBounds.x = aX;
  mBounds.y = aY;

  NSRect r;
  nsCocoaUtils::GeckoRectToNSRect(mBounds, r);
  [mView setFrame:r];

  if (mVisible)
    [mView setNeedsDisplay:YES];

  NotifyRollupGeometryChange(gRollupListener);
  ReportMoveEvent();

  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

NS_IMETHODIMP nsChildView::Resize(PRInt32 aWidth, PRInt32 aHeight, bool aRepaint)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  if (!mView || (mBounds.width == aWidth && mBounds.height == aHeight))
    return NS_OK;

  mBounds.width  = aWidth;
  mBounds.height = aHeight;

  NSRect r;
  nsCocoaUtils::GeckoRectToNSRect(mBounds, r);
  [mView setFrame:r];

  if (mVisible && aRepaint)
    [mView setNeedsDisplay:YES];

  NotifyRollupGeometryChange(gRollupListener);
  ReportSizeEvent();

  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

NS_IMETHODIMP nsChildView::Resize(PRInt32 aX, PRInt32 aY, PRInt32 aWidth, PRInt32 aHeight, bool aRepaint)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  BOOL isMoving = (mBounds.x != aX || mBounds.y != aY);
  BOOL isResizing = (mBounds.width != aWidth || mBounds.height != aHeight);
  if (!mView || (!isMoving && !isResizing))
    return NS_OK;

  if (isMoving) {
    mBounds.x = aX;
    mBounds.y = aY;
  }
  if (isResizing) {
    mBounds.width  = aWidth;
    mBounds.height = aHeight;
  }

  NSRect r;
  nsCocoaUtils::GeckoRectToNSRect(mBounds, r);
  [mView setFrame:r];

  if (mVisible && aRepaint)
    [mView setNeedsDisplay:YES];

  NotifyRollupGeometryChange(gRollupListener);
  if (isMoving) {
    ReportMoveEvent();
    if (mOnDestroyCalled)
      return NS_OK;
  }
  if (isResizing)
    ReportSizeEvent();

  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

static const PRInt32 resizeIndicatorWidth = 15;
static const PRInt32 resizeIndicatorHeight = 15;
bool nsChildView::ShowsResizeIndicator(nsIntRect* aResizerRect)
{
  NSView *topLevelView = mView, *superView = nil;
  while ((superView = [topLevelView superview]))
    topLevelView = superView;

  if (![[topLevelView window] showsResizeIndicator] ||
      !([[topLevelView window] styleMask] & NSResizableWindowMask))
    return false;

  if (aResizerRect) {
    NSSize bounds = [topLevelView bounds].size;
    NSPoint corner = NSMakePoint(bounds.width, [topLevelView isFlipped] ? bounds.height : 0);
    corner = [topLevelView convertPoint:corner toView:mView];
    aResizerRect->SetRect(NSToIntRound(corner.x) - resizeIndicatorWidth,
                          NSToIntRound(corner.y) - resizeIndicatorHeight,
                          resizeIndicatorWidth, resizeIndicatorHeight);
  }
  return true;
}

// In QuickDraw mode the coordinate system used here should be that of the
// browser window's content region (defined as everything but the 22-pixel
// high titlebar).  But in CoreGraphics mode the coordinate system should be
// that of the browser window as a whole (including its titlebar).  Both
// coordinate systems have a top-left origin.  See bmo bug 474491.
//
// There's a bug in this method's code -- it currently uses the QuickDraw
// coordinate system for both the QuickDraw and CoreGraphics drawing modes.
// This bug is fixed by the patch for bug 474491.  But the Flash plugin (both
// version 10.0.12.36 from Adobe and version 9.0 r151 from Apple) has Mozilla-
// specific code to work around this bug, which breaks when we fix it (see bmo
// bug 477077).  So we'll need to coordinate releasing a fix for this bug with
// Adobe and other major plugin vendors that support the CoreGraphics mode.
NS_IMETHODIMP nsChildView::GetPluginClipRect(nsIntRect& outClipRect, nsIntPoint& outOrigin, bool& outWidgetVisible)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  NS_ASSERTION(mWindowType == eWindowType_plugin,
               "GetPluginClipRect must only be called on a plugin widget");
  if (mWindowType != eWindowType_plugin) return NS_ERROR_FAILURE;
  
  NSWindow* window = [mView window];
  if (!window) return NS_ERROR_FAILURE;
  
  NSPoint viewOrigin = [mView convertPoint:NSZeroPoint toView:nil];
  NSRect frame = [[window contentView] frame];
  viewOrigin.y = frame.size.height - viewOrigin.y;
  
  // set up the clipping region for plugins.
  NSRect visibleBounds = [mView visibleRect];
  NSPoint clipOrigin   = [mView convertPoint:visibleBounds.origin toView:nil];
  
  // Convert from cocoa to QuickDraw coordinates
  clipOrigin.y = frame.size.height - clipOrigin.y;
  
  outClipRect.x = NSToIntRound(clipOrigin.x);
  outClipRect.y = NSToIntRound(clipOrigin.y);

  // need to convert view's origin to window coordinates.
  // then, encode as "SetOrigin" ready values.
  outOrigin.x = -NSToIntRound(viewOrigin.x);
  outOrigin.y = -NSToIntRound(viewOrigin.y);

  bool isVisible;
  IsVisible(isVisible);
  if (isVisible && [mView window] != nil) {
    outClipRect.width  = NSToIntRound(visibleBounds.origin.x + visibleBounds.size.width) - NSToIntRound(visibleBounds.origin.x);
    outClipRect.height = NSToIntRound(visibleBounds.origin.y + visibleBounds.size.height) - NSToIntRound(visibleBounds.origin.y);

    if (mClipRects) {
      nsIntRect clipBounds;
      for (PRUint32 i = 0; i < mClipRectCount; ++i) {
        clipBounds.UnionRect(clipBounds, mClipRects[i]);
      }
      outClipRect.IntersectRect(outClipRect, clipBounds - outOrigin);
    }

    // XXXroc should this be !outClipRect.IsEmpty()?
    outWidgetVisible = true;
  }
  else {
    outClipRect.width = 0;
    outClipRect.height = 0;
    outWidgetVisible = false;
  }

  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

#ifndef NP_NO_CARBON
static void InitializeEventRecord(EventRecord* event, Point* aMousePosition)
{
  memset(event, 0, sizeof(EventRecord));
  if (aMousePosition) {
    event->where = *aMousePosition;
  } else {
    ::GetGlobalMouse(&event->where);
  }
  event->when = ::TickCount();
  event->modifiers = ::GetCurrentKeyModifiers();
}
#endif

void nsChildView::PaintQD()
{
#ifndef NP_NO_CARBON
  void *pluginPort = this->GetNativeData(NS_NATIVE_PLUGIN_PORT_QD);
  void *window = ::GetWindowFromPort(static_cast<NP_Port*>(pluginPort)->port);

  NS_SUCCEEDED(StartDrawPlugin());
  EventRecord updateEvent;
  InitializeEventRecord(&updateEvent, nsnull);
  updateEvent.what = updateEvt;
  updateEvent.message = UInt32(window);

  nsRefPtr<nsNPAPIPluginInstance> instance;
  mPluginInstanceOwner->GetInstance(getter_AddRefs(instance));

  instance->HandleEvent(&updateEvent, nsnull);
  EndDrawPlugin();
#endif
}

NS_IMETHODIMP nsChildView::StartDrawPlugin()
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  NS_ASSERTION(mWindowType == eWindowType_plugin,
               "StartDrawPlugin must only be called on a plugin widget");
  if (mWindowType != eWindowType_plugin) return NS_ERROR_FAILURE;

  // This code is necessary for both Quickdraw and CoreGraphics in 32-bit builds.
  // See comments below about why. In 64-bit CoreGraphics mode we will not keep
  // this region up to date, plugins should not depend on it.
#ifndef __LP64__
  NSWindow* window = [mView window];
  if (!window)
    return NS_ERROR_FAILURE;

  // In QuickDraw drawing mode, prevent reentrant handling of any plugin event
  // (this emulates behavior on the 1.8 branch, where only QuickDraw mode is
  // supported).  But in CoreGraphics drawing mode only do this if the current
  // plugin event isn't an update/paint event.  This allows popupcontextmenu()
  // to work properly from a plugin that supports the Cocoa event model,
  // without regressing bug 409615.  See bug 435041.  (StartDrawPlugin() and
  // EndDrawPlugin() wrap every call to nsIPluginInstance::HandleEvent() --
  // not just calls that "draw" or paint.)
  bool isQDPlugin = [(ChildView*)mView pluginDrawingModel] == NPDrawingModelQuickDraw;
  if (isQDPlugin || mIsDispatchPaint) {
    if (mPluginDrawing)
      return NS_ERROR_FAILURE;
  }

  // It appears that the WindowRef from which we get the plugin port undergoes the
  // traditional BeginUpdate/EndUpdate cycle, which, if you recall, sets the visible
  // region to the intersection of the visible region and the update region. Since
  // we don't know here if we're being drawn inside a BeginUpdate/EndUpdate pair
  // (which seem to occur in [NSWindow display]), and we don't want to have the burden
  // of correctly doing Carbon invalidates of the plugin rect, we manually set the
  // visible region to be the entire port every time. It is necessary to set up our
  // window's port even for CoreGraphics plugins, because they may still use Carbon
  // internally (see bug #420527 for details).
  //
  // Don't use this code if any of the QuickDraw APIs it currently requires are
  // missing (as they probably will be on OS X 10.8 and up).
  if (::NewRgn && ::GetQDGlobalsThePort && ::GetGWorld && ::SetGWorld &&
      ::IsPortOffscreen && ::GetMainDevice && ::SetOrigin && ::RectRgn &&
      ::SetPortVisibleRegion && ::SetPortClipRegion && ::DisposeRgn) {
    CGrafPtr port = ::GetWindowPort(WindowRef([window windowRef]));
    if (isQDPlugin) {
      port = mPluginQDPort.port;
    }

    RgnHandle pluginRegion = ::NewRgn();
    if (pluginRegion) {
      bool portChanged = (port != CGrafPtr(::GetQDGlobalsThePort()));
      CGrafPtr oldPort;
      GDHandle oldDevice;

      if (portChanged) {
        ::GetGWorld(&oldPort, &oldDevice);
        ::SetGWorld(port, ::IsPortOffscreen(port) ? nsnull : ::GetMainDevice());
      }

      ::SetOrigin(0, 0);

      nsIntRect clipRect; // this is in native window coordinates
      nsIntPoint origin;
      bool visible;
      GetPluginClipRect(clipRect, origin, visible);

      // XXX if we're not visible, set an empty clip region?
      Rect pluginRect;
      ConvertGeckoRectToMacRect(clipRect, pluginRect);

      ::RectRgn(pluginRegion, &pluginRect);
      ::SetPortVisibleRegion(port, pluginRegion);
      ::SetPortClipRegion(port, pluginRegion);

      // now set up the origin for the plugin
      ::SetOrigin(origin.x, origin.y);

      ::DisposeRgn(pluginRegion);

      if (portChanged) {
        ::SetGWorld(oldPort, oldDevice);
      }
    }
  } else {
    NS_WARNING("Cannot set plugin's visible region -- required QuickDraw APIs are missing!");
  }
#endif

  mPluginDrawing = true;
  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

NS_IMETHODIMP nsChildView::EndDrawPlugin()
{
  NS_ASSERTION(mWindowType == eWindowType_plugin,
               "EndDrawPlugin must only be called on a plugin widget");
  if (mWindowType != eWindowType_plugin) return NS_ERROR_FAILURE;

  mPluginDrawing = false;
  return NS_OK;
}

NS_IMETHODIMP nsChildView::SetPluginInstanceOwner(nsIPluginInstanceOwner* aInstanceOwner)
{
  mPluginInstanceOwner = aInstanceOwner;

  return NS_OK;
}

NS_IMETHODIMP nsChildView::SetPluginEventModel(int inEventModel)
{
  [(ChildView*)mView setPluginEventModel:(NPEventModel)inEventModel];
  return NS_OK;
}

NS_IMETHODIMP nsChildView::GetPluginEventModel(int* outEventModel)
{
  *outEventModel = [(ChildView*)mView pluginEventModel];
  return NS_OK;
}

NS_IMETHODIMP nsChildView::SetPluginDrawingModel(int inDrawingModel)
{
  [(ChildView*)mView setPluginDrawingModel:(NPDrawingModel)inDrawingModel];
  return NS_OK;
}

NS_IMETHODIMP nsChildView::StartComplexTextInputForCurrentEvent()
{
  return mTextInputHandler->StartComplexTextInputForCurrentEvent();
}

nsresult nsChildView::SynthesizeNativeKeyEvent(PRInt32 aNativeKeyboardLayout,
                                               PRInt32 aNativeKeyCode,
                                               PRUint32 aModifierFlags,
                                               const nsAString& aCharacters,
                                               const nsAString& aUnmodifiedCharacters)
{
  return mTextInputHandler->SynthesizeNativeKeyEvent(aNativeKeyboardLayout,
                                                     aNativeKeyCode,
                                                     aModifierFlags,
                                                     aCharacters,
                                                     aUnmodifiedCharacters);
}

nsresult nsChildView::SynthesizeNativeMouseEvent(nsIntPoint aPoint,
                                                 PRUint32 aNativeMessage,
                                                 PRUint32 aModifierFlags)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  // Move the mouse cursor to the requested position and reconnect it to the mouse.
  CGWarpMouseCursorPosition(CGPointMake(aPoint.x, aPoint.y));
  CGAssociateMouseAndMouseCursorPosition(true);

  // aPoint is given with the origin on the top left, but convertScreenToBase
  // expects a point in a coordinate system that has its origin on the bottom left.
  NSPoint screenPoint = NSMakePoint(aPoint.x, [[NSScreen mainScreen] frame].size.height - aPoint.y);
  NSPoint windowPoint = [[mView window] convertScreenToBase:screenPoint];

  NSEvent* event = [NSEvent mouseEventWithType:aNativeMessage
                                      location:windowPoint
                                 modifierFlags:aModifierFlags
                                     timestamp:[NSDate timeIntervalSinceReferenceDate]
                                  windowNumber:[[mView window] windowNumber]
                                       context:nil
                                   eventNumber:0
                                    clickCount:1
                                      pressure:0.0];

  if (!event)
    return NS_ERROR_FAILURE;

  if ([[mView window] isKindOfClass:[BaseWindow class]]) {
    // Tracking area events don't end up in their tracking areas when sent
    // through [NSApp sendEvent:], so pass them directly to the right methods.
    BaseWindow* window = (BaseWindow*)[mView window];
    if (aNativeMessage == NSMouseEntered) {
      [window mouseEntered:event];
      return NS_OK;
    }
    if (aNativeMessage == NSMouseExited) {
      [window mouseExited:event];
      return NS_OK;
    }
    if (aNativeMessage == NSMouseMoved) {
      [window mouseMoved:event];
      return NS_OK;
    }
  }

  [NSApp sendEvent:event];
  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

// First argument has to be an NSMenu representing the application's top-level
// menu bar. The returned item is *not* retained.
static NSMenuItem* NativeMenuItemWithLocation(NSMenu* menubar, NSString* locationString)
{
  NSArray* indexes = [locationString componentsSeparatedByString:@"|"];
  unsigned int indexCount = [indexes count];
  if (indexCount == 0)
    return nil;

  NSMenu* currentSubmenu = [NSApp mainMenu];
  for (unsigned int i = 0; i < indexCount; i++) {
    int targetIndex;
    // We remove the application menu from consideration for the top-level menu
    if (i == 0)
      targetIndex = [[indexes objectAtIndex:i] intValue] + 1;
    else
      targetIndex = [[indexes objectAtIndex:i] intValue];
    int itemCount = [currentSubmenu numberOfItems];
    if (targetIndex < itemCount) {
      NSMenuItem* menuItem = [currentSubmenu itemAtIndex:targetIndex];
      // if this is the last index just return the menu item
      if (i == (indexCount - 1))
        return menuItem;
      // if this is not the last index find the submenu and keep going
      if ([menuItem hasSubmenu])
        currentSubmenu = [menuItem submenu];
      else
        return nil;
    }
  }

  return nil;
}

// Used for testing native menu system structure and event handling.
NS_IMETHODIMP nsChildView::ActivateNativeMenuItemAt(const nsAString& indexString)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  NSString* locationString = [NSString stringWithCharacters:indexString.BeginReading() length:indexString.Length()];
  NSMenuItem* item = NativeMenuItemWithLocation([NSApp mainMenu], locationString);
  // We can't perform an action on an item with a submenu, that will raise
  // an obj-c exception.
  if (item && ![item hasSubmenu]) {
    NSMenu* parent = [item menu];
    if (parent) {
      // NSLog(@"Performing action for native menu item titled: %@\n",
      //       [[currentSubmenu itemAtIndex:targetIndex] title]);
      [parent performActionForItemAtIndex:[parent indexOfItem:item]];
      return NS_OK;
    }
  }
  return NS_ERROR_FAILURE;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

// Used for testing native menu system structure and event handling.
NS_IMETHODIMP nsChildView::ForceUpdateNativeMenuAt(const nsAString& indexString)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  nsCocoaWindow *widget = GetXULWindowWidget();
  if (widget) {
    nsMenuBarX* mb = widget->GetMenuBar();
    if (mb) {
      if (indexString.IsEmpty())
        mb->ForceNativeMenuReload();
      else
        mb->ForceUpdateNativeMenuAt(indexString);
    }
  }
  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

#pragma mark -

#ifdef INVALIDATE_DEBUGGING

static Boolean KeyDown(const UInt8 theKey)
{
  KeyMap map;
  GetKeys(map);
  return ((*((UInt8 *)map + (theKey >> 3)) >> (theKey & 7)) & 1) != 0;
}

static Boolean caps_lock()
{
  return KeyDown(0x39);
}

static void blinkRect(Rect* r)
{
  StRegionFromPool oldClip;
  if (oldClip != NULL)
    ::GetClip(oldClip);

  ::ClipRect(r);
  ::InvertRect(r);
  UInt32 end = ::TickCount() + 5;
  while (::TickCount() < end) ;
  ::InvertRect(r);

  if (oldClip != NULL)
    ::SetClip(oldClip);
}

static void blinkRgn(RgnHandle rgn)
{
  StRegionFromPool oldClip;
  if (oldClip != NULL)
    ::GetClip(oldClip);

  ::SetClip(rgn);
  ::InvertRgn(rgn);
  UInt32 end = ::TickCount() + 5;
  while (::TickCount() < end) ;
  ::InvertRgn(rgn);

  if (oldClip != NULL)
    ::SetClip(oldClip);
}

#endif

// Invalidate this component's visible area
NS_IMETHODIMP nsChildView::Invalidate(const nsIntRect &aRect)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  if (!mView || !mVisible)
    return NS_OK;

  NSRect r;
  nsCocoaUtils::GeckoRectToNSRect(aRect, r);
  
  if ([NSView focusView]) {
    // if a view is focussed (i.e. being drawn), then postpone the invalidate so that we
    // don't lose it.
    [mView setNeedsPendingDisplayInRect:r];
  }
  else {
    [mView setNeedsDisplayInRect:r];
  }

  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

bool
nsChildView::GetShouldAccelerate()
{
  // Don't use OpenGL for transparent windows or for popup windows.
  if (!mView || ![[mView window] isOpaque] ||
      [[mView window] isKindOfClass:[PopupWindow class]])
    return false;

  return nsBaseWidget::GetShouldAccelerate();
}

bool
nsChildView::UseOffMainThreadCompositing()
{
  // OMTC doesn't work with Basic Layers on OS X right now. Once it works, we'll
  // still want to disable it for certain kinds of windows (e.g. popups).
  return nsBaseWidget::UseOffMainThreadCompositing() && GetShouldAccelerate();
}

inline PRUint16 COLOR8TOCOLOR16(PRUint8 color8)
{
  // return (color8 == 0xFF ? 0xFFFF : (color8 << 8));
  return (color8 << 8) | color8;  /* (color8 * 257) == (color8 * 0x0101) */
}

#pragma mark -

nsresult nsChildView::ConfigureChildren(const nsTArray<Configuration>& aConfigurations)
{
  for (PRUint32 i = 0; i < aConfigurations.Length(); ++i) {
    const Configuration& config = aConfigurations[i];
    nsChildView* child = static_cast<nsChildView*>(config.mChild);
#ifdef DEBUG
    nsWindowType kidType;
    child->GetWindowType(kidType);
#endif
    NS_ASSERTION(kidType == eWindowType_plugin,
                 "Configured widget is not a plugin type");
    NS_ASSERTION(child->GetParent() == this,
                 "Configured widget is not a child of the right widget");

    // nsIWidget::Show() doesn't get called on plugin widgets unless we call
    // it from here.  See bug 592563.
    child->Show(!config.mClipRegion.IsEmpty());

    bool repaint = false;
#ifndef NP_NO_QUICKDRAW
    repaint = child->mView &&
      [(ChildView*)child->mView pluginDrawingModel] == NPDrawingModelQuickDraw;
#endif
    child->Resize(
        config.mBounds.x, config.mBounds.y,
        config.mBounds.width, config.mBounds.height,
        repaint);

    // Store the clip region here in case GetPluginClipRect needs it.
    child->StoreWindowClipRegion(config.mClipRegion);
  }
  return NS_OK;
}

// Invokes callback and ProcessEvent methods on Event Listener object
NS_IMETHODIMP nsChildView::DispatchEvent(nsGUIEvent* event, nsEventStatus& aStatus)
{
#ifdef DEBUG
  debug_DumpEvent(stdout, event->widget, event, nsCAutoString("something"), 0);
#endif

  NS_ASSERTION(!(mTextInputHandler && mTextInputHandler->IsIMEComposing() &&
                 NS_IS_KEY_EVENT(event)),
    "Any key events should not be fired during IME composing");

  aStatus = nsEventStatus_eIgnore;

  nsCOMPtr<nsIWidget> kungFuDeathGrip = do_QueryInterface(mParentWidget ? mParentWidget : this);
  if (mParentWidget) {
    nsWindowType type;
    mParentWidget->GetWindowType(type);
    if (type == eWindowType_popup) {
      // use the parent popup's widget if there is no view
      void* clientData = nsnull;
      if (event->widget)
        event->widget->GetClientData(clientData);
      if (!clientData)
        event->widget = mParentWidget;
    }
  }

  bool restoreIsDispatchPaint = mIsDispatchPaint;
  mIsDispatchPaint = mIsDispatchPaint || event->eventStructType == NS_PAINT_EVENT;

  if (mEventCallback)
    aStatus = (*mEventCallback)(event);

  mIsDispatchPaint = restoreIsDispatchPaint;

  return NS_OK;
}

bool nsChildView::DispatchWindowEvent(nsGUIEvent &event)
{
  nsEventStatus status;
  DispatchEvent(&event, status);
  return ConvertStatus(status);
}

#pragma mark -

bool nsChildView::ReportDestroyEvent()
{
  nsGUIEvent event(true, NS_DESTROY, this);
  event.time = PR_IntervalNow();
  return DispatchWindowEvent(event);
}

bool nsChildView::ReportMoveEvent()
{
  nsGUIEvent moveEvent(true, NS_MOVE, this);
  moveEvent.refPoint.x = mBounds.x;
  moveEvent.refPoint.y = mBounds.y;
  moveEvent.time       = PR_IntervalNow();
  return DispatchWindowEvent(moveEvent);
}

bool nsChildView::ReportSizeEvent()
{
  nsSizeEvent sizeEvent(true, NS_SIZE, this);
  sizeEvent.time        = PR_IntervalNow();
  sizeEvent.windowSize  = &mBounds;
  sizeEvent.mWinWidth   = mBounds.width;
  sizeEvent.mWinHeight  = mBounds.height;
  return DispatchWindowEvent(sizeEvent);
}

#pragma mark -

//    Return the offset between this child view and the screen.
//    @return       -- widget origin in screen coordinates
nsIntPoint nsChildView::WidgetToScreenOffset()
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN;

  NSPoint temp;
  temp.x = 0;
  temp.y = 0;
  
  // 1. First translate this point into window coords. The returned point is always in
  //    bottom-left coordinates.
  temp = [mView convertPoint:temp toView:nil];  
  
  // 2. We turn the window-coord rect's origin into screen (still bottom-left) coords.
  temp = [[mView window] convertBaseToScreen:temp];
  
  // 3. Since we're dealing in bottom-left coords, we need to make it top-left coords
  //    before we pass it back to Gecko.
  FlipCocoaScreenCoordinate(temp);
  
  return nsIntPoint(NSToIntRound(temp.x), NSToIntRound(temp.y));

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(nsIntPoint(0,0));
}

NS_IMETHODIMP nsChildView::CaptureRollupEvents(nsIRollupListener * aListener, 
                                               bool aDoCapture, 
                                               bool aConsumeRollupEvent)
{
  // this never gets called, only top-level windows can be rollup widgets
  return NS_OK;
}

NS_IMETHODIMP nsChildView::SetTitle(const nsAString& title)
{
  // child views don't have titles
  return NS_OK;
}

NS_IMETHODIMP nsChildView::GetAttention(PRInt32 aCycleCount)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  [NSApp requestUserAttention:NSInformationalRequest];
  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

/* static */
bool nsChildView::DoHasPendingInputEvent()
{
  return sLastInputEventCount != GetCurrentInputEventCount(); 
}

/* static */
PRUint32 nsChildView::GetCurrentInputEventCount()
{
  // Can't use kCGAnyInputEventType because that updates too rarely for us (and
  // always in increments of 30+!) and because apparently it's sort of broken
  // on Tiger.  So just go ahead and query the counters we care about.
  static const CGEventType eventTypes[] = {
    kCGEventLeftMouseDown,
    kCGEventLeftMouseUp,
    kCGEventRightMouseDown,
    kCGEventRightMouseUp,
    kCGEventMouseMoved,
    kCGEventLeftMouseDragged,
    kCGEventRightMouseDragged,
    kCGEventKeyDown,
    kCGEventKeyUp,
    kCGEventScrollWheel,
    kCGEventTabletPointer,
    kCGEventOtherMouseDown,
    kCGEventOtherMouseUp,
    kCGEventOtherMouseDragged
  };

  PRUint32 eventCount = 0;
  for (PRUint32 i = 0; i < ArrayLength(eventTypes); ++i) {
    eventCount +=
      CGEventSourceCounterForEventType(kCGEventSourceStateCombinedSessionState,
                                       eventTypes[i]);
  }
  return eventCount;
}

/* static */
void nsChildView::UpdateCurrentInputEventCount()
{
  sLastInputEventCount = GetCurrentInputEventCount();
}

bool nsChildView::HasPendingInputEvent()
{
  return DoHasPendingInputEvent();
}

#pragma mark -

// Force Input Method Editor to commit the uncommitted input
// Note that this and other IME methods don't necessarily
// get called on the same ChildView that input is going through.
NS_IMETHODIMP nsChildView::ResetInputState()
{
  NS_ENSURE_TRUE(mTextInputHandler, NS_ERROR_NOT_AVAILABLE);
  mTextInputHandler->CommitIMEComposition();
  return NS_OK;
}

NS_IMETHODIMP_(void)
nsChildView::SetInputContext(const InputContext& aContext,
                             const InputContextAction& aAction)
{
  NS_ENSURE_TRUE(mTextInputHandler, );
  mInputContext = aContext;
  switch (aContext.mIMEState.mEnabled) {
    case IMEState::ENABLED:
    case IMEState::PLUGIN:
      mTextInputHandler->SetASCIICapableOnly(false);
      mTextInputHandler->EnableIME(true);
      if (mInputContext.mIMEState.mOpen != IMEState::DONT_CHANGE_OPEN_STATE) {
        mTextInputHandler->SetIMEOpenState(
          mInputContext.mIMEState.mOpen == IMEState::OPEN);
      }
      break;
    case IMEState::DISABLED:
      mTextInputHandler->SetASCIICapableOnly(false);
      mTextInputHandler->EnableIME(false);
      break;
    case IMEState::PASSWORD:
      mTextInputHandler->SetASCIICapableOnly(true);
      mTextInputHandler->EnableIME(false);
      break;
    default:
      NS_ERROR("not implemented!");
  }
}

NS_IMETHODIMP_(InputContext)
nsChildView::GetInputContext()
{
  switch (mInputContext.mIMEState.mEnabled) {
    case IMEState::ENABLED:
    case IMEState::PLUGIN:
      if (mTextInputHandler) {
        mInputContext.mIMEState.mOpen =
          mTextInputHandler->IsIMEOpened() ? IMEState::OPEN : IMEState::CLOSED;
        break;
      }
      // If mTextInputHandler is null, set CLOSED instead...
    default:
      mInputContext.mIMEState.mOpen = IMEState::CLOSED;
      break;
  }
  return mInputContext;
}

// Destruct and don't commit the IME composition string.
NS_IMETHODIMP nsChildView::CancelIMEComposition()
{
  NS_ENSURE_TRUE(mTextInputHandler, NS_ERROR_NOT_AVAILABLE);
  mTextInputHandler->CancelIMEComposition();
  return NS_OK;
}

NS_IMETHODIMP nsChildView::GetToggledKeyState(PRUint32 aKeyCode,
                                              bool* aLEDState)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  NS_ENSURE_ARG_POINTER(aLEDState);
  PRUint32 key;
  switch (aKeyCode) {
    case NS_VK_CAPS_LOCK:
      key = alphaLock;
      break;
    case NS_VK_NUM_LOCK:
      key = kEventKeyModifierNumLockMask;
      break;
    // Mac doesn't support SCROLL_LOCK state.
    default:
      return NS_ERROR_NOT_IMPLEMENTED;
  }
  PRUint32 modifierFlags = ::GetCurrentKeyModifiers();
  *aLEDState = (modifierFlags & key) != 0;
  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

NS_IMETHODIMP nsChildView::OnIMEFocusChange(bool aFocus)
{
  NS_ENSURE_TRUE(mTextInputHandler, NS_ERROR_NOT_AVAILABLE);
  mTextInputHandler->OnFocusChangeInGecko(aFocus);
  // XXX Return NS_ERROR_NOT_IMPLEMENTED, see bug 496360.
  return NS_ERROR_NOT_IMPLEMENTED;
}

NSView<mozView>* nsChildView::GetEditorView()
{
  NSView<mozView>* editorView = mView;
  // We need to get editor's view. E.g., when the focus is in the bookmark
  // dialog, the view is <panel> element of the dialog.  At this time, the key
  // events are processed the parent window's view that has native focus.
  nsQueryContentEvent textContent(true, NS_QUERY_TEXT_CONTENT, this);
  textContent.InitForQueryTextContent(0, 0);
  DispatchWindowEvent(textContent);
  if (textContent.mSucceeded && textContent.mReply.mFocusedWidget) {
    NSView<mozView>* view = static_cast<NSView<mozView>*>(
      textContent.mReply.mFocusedWidget->GetNativeData(NS_NATIVE_WIDGET));
    if (view)
      editorView = view;
  }
  return editorView;
}

#pragma mark -

void
nsChildView::CreateCompositor()
{
  nsBaseWidget::CreateCompositor();
  if (mCompositorChild) {
    LayerManagerOGL *manager =
      static_cast<LayerManagerOGL*>(compositor::GetLayerManager(mCompositorParent));

    NSOpenGLContext *glContext =
      (NSOpenGLContext *) manager->gl()->GetNativeData(GLContext::NativeGLContext);

    [(ChildView *)mView setGLContext:glContext];
    [(ChildView *)mView setUsingOMTCompositor:true];
  }
}

gfxASurface*
nsChildView::GetThebesSurface()
{
  if (!mTempThebesSurface) {
    mTempThebesSurface = new gfxQuartzSurface(gfxSize(1, 1), gfxASurface::ImageFormatARGB32);
  }

  return mTempThebesSurface;
}

static void
DrawResizer(CGContextRef aCtx)
{
  CGContextSetShouldAntialias(aCtx, false);
  CGPoint points[6];
  points[0] = CGPointMake(13.0f, 4.0f);
  points[1] = CGPointMake(3.0f, 14.0f);
  points[2] = CGPointMake(13.0f, 8.0f);
  points[3] = CGPointMake(7.0f, 14.0f);
  points[4] = CGPointMake(13.0f, 12.0f);
  points[5] = CGPointMake(11.0f, 14.0f);
  CGContextSetRGBStrokeColor(aCtx, 0.00f, 0.00f, 0.00f, 0.15f);
  CGContextStrokeLineSegments(aCtx, points, 6);

  points[0] = CGPointMake(13.0f, 5.0f);
  points[1] = CGPointMake(4.0f, 14.0f);
  points[2] = CGPointMake(13.0f, 9.0f);
  points[3] = CGPointMake(8.0f, 14.0f);
  points[4] = CGPointMake(13.0f, 13.0f);
  points[5] = CGPointMake(12.0f, 14.0f);
  CGContextSetRGBStrokeColor(aCtx, 0.13f, 0.13f, 0.13f, 0.54f);
  CGContextStrokeLineSegments(aCtx, points, 6);

  points[0] = CGPointMake(13.0f, 6.0f);
  points[1] = CGPointMake(5.0f, 14.0f);
  points[2] = CGPointMake(13.0f, 10.0f);
  points[3] = CGPointMake(9.0f, 14.0f);
  points[5] = CGPointMake(13.0f, 13.9f);
  points[4] = CGPointMake(13.0f, 14.0f);
  CGContextSetRGBStrokeColor(aCtx, 0.84f, 0.84f, 0.84f, 0.55f);
  CGContextStrokeLineSegments(aCtx, points, 6);
}

void
nsChildView::DrawWindowOverlay(LayerManager* aManager, nsIntRect aRect)
{
  if (!ShowsResizeIndicator(nsnull)) {
    return;
  }

  nsRefPtr<LayerManagerOGL> manager(static_cast<LayerManagerOGL*>(aManager));
  if (!manager) {
    return;
  }

  if (!mResizerImage) {
    mResizerImage = manager->gl()->CreateTextureImage(nsIntSize(15, 15),
                                                      gfxASurface::CONTENT_COLOR_ALPHA,
                                                      LOCAL_GL_CLAMP_TO_EDGE,
                                                      TextureImage::UseNearestFilter);

    // Creation of texture images can fail.
    if (!mResizerImage)
      return;

    nsIntRegion update(nsIntRect(0, 0, 15, 15));
    gfxASurface *asurf = mResizerImage->BeginUpdate(update);
    if (!asurf) {
      mResizerImage = nsnull;
      return;
    }

    NS_ABORT_IF_FALSE(asurf->GetType() == gfxASurface::SurfaceTypeQuartz,
                  "BeginUpdate must return a Quartz surface!");

    nsRefPtr<gfxQuartzSurface> image = static_cast<gfxQuartzSurface*>(asurf);
    DrawResizer(image->GetCGContext());

    mResizerImage->EndUpdate();
  }

  NS_ABORT_IF_FALSE(mResizerImage, "Must have a texture allocated by now!");

  float bottomX = aRect.x + aRect.width;
  float bottomY = aRect.y + aRect.height;

  TextureImage::ScopedBindTexture texBind(mResizerImage, LOCAL_GL_TEXTURE0);

  ShaderProgramOGL *program =
    manager->GetProgram(mResizerImage->GetShaderProgramType());
  program->Activate();
  program->SetLayerQuadRect(nsIntRect(bottomX - 15,
                                      bottomY - 15,
                                      15,
                                      15));
  program->SetLayerTransform(gfx3DMatrix());
  program->SetLayerOpacity(1.0);
  program->SetRenderOffset(nsIntPoint(0,0));
  program->SetTextureUnit(0);

  manager->BindAndDrawQuad(program);
}

void
nsChildView::UpdateThemeGeometries(const nsTArray<ThemeGeometry>& aThemeGeometries)
{
  NSWindow* win = [mView window];
  if (!win || ![win isKindOfClass:[ToolbarWindow class]])
    return;

  float unifiedToolbarHeight = 0;
  nsIntRect topPixelStrip(0, 0, [win frame].size.width, 1);

  for (PRUint32 i = 0; i < aThemeGeometries.Length(); ++i) {
    const ThemeGeometry& g = aThemeGeometries[i];
    if ((g.mWidgetType == NS_THEME_MOZ_MAC_UNIFIED_TOOLBAR ||
         g.mWidgetType == NS_THEME_TOOLBAR) &&
        g.mRect.Contains(topPixelStrip)) {
      unifiedToolbarHeight = g.mRect.YMost();
    }
  }
  [(ToolbarWindow*)win setUnifiedToolbarHeight:unifiedToolbarHeight];
}

NS_IMETHODIMP
nsChildView::BeginSecureKeyboardInput()
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  nsresult rv = nsBaseWidget::BeginSecureKeyboardInput();
  if (NS_SUCCEEDED(rv)) {
    ::EnableSecureEventInput();
  }
  return rv;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

NS_IMETHODIMP
nsChildView::EndSecureKeyboardInput()
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  nsresult rv = nsBaseWidget::EndSecureKeyboardInput();
  if (NS_SUCCEEDED(rv)) {
    ::DisableSecureEventInput();
  }
  return rv;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

#ifdef ACCESSIBILITY
already_AddRefed<Accessible>
nsChildView::GetDocumentAccessible()
{
  if (!mozilla::a11y::ShouldA11yBeEnabled())
    return nsnull;

  Accessible *docAccessible = nsnull;
  if (mAccessible) {
    CallQueryReferent(mAccessible.get(), &docAccessible);
    return docAccessible;
  }

  // need to fetch the accessible anew, because it has gone away.
  nsEventStatus status;
  nsAccessibleEvent event(true, NS_GETACCESSIBLE, this);
  DispatchEvent(&event, status);

  // cache the accessible in our weak ptr
  mAccessible =
    do_GetWeakReference(static_cast<nsIAccessible*>(event.mAccessible));

  NS_IF_ADDREF(event.mAccessible);
  return event.mAccessible;
}
#endif

#pragma mark -

@implementation ChildView

// globalDragPboard is non-null during native drag sessions that did not originate
// in our native NSView (it is set in |draggingEntered:|). It is unset when the
// drag session ends for this view, either with the mouse exiting or when a drop
// occurs in this view.
NSPasteboard* globalDragPboard = nil;

// gLastDragView and gLastDragMouseDownEvent are used to communicate information
// to the drag service during drag invocation (starting a drag in from the view).
// gLastDragView is only non-null while mouseDragged is on the call stack.
NSView* gLastDragView = nil;
NSEvent* gLastDragMouseDownEvent = nil;

+ (void)initialize
{
  static BOOL initialized = NO;

  if (!initialized) {
    // Inform the OS about the types of services (from the "Services" menu)
    // that we can handle.

    NSArray *sendTypes = [[NSArray alloc] initWithObjects:NSStringPboardType,NSHTMLPboardType,nil];
    NSArray *returnTypes = [[NSArray alloc] initWithObjects:NSStringPboardType,NSHTMLPboardType,nil];
    
    [NSApp registerServicesMenuSendTypes:sendTypes returnTypes:returnTypes];

    [sendTypes release];
    [returnTypes release];

    initialized = YES;
  }
}

// initWithFrame:geckoChild:
- (id)initWithFrame:(NSRect)inFrame geckoChild:(nsChildView*)inChild
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NIL;

  if ((self = [super initWithFrame:inFrame])) {
    mGeckoChild = inChild;
    mIsPluginView = NO;
#ifndef NP_NO_CARBON
    mPluginEventModel = NPEventModelCarbon;
#else
    mPluginEventModel = NPEventModelCocoa;
#endif
#ifndef NP_NO_QUICKDRAW
    mPluginDrawingModel = NPDrawingModelQuickDraw;
#else
    mPluginDrawingModel = NPDrawingModelCoreGraphics;
#endif
    mPendingDisplay = NO;
    mBlockedLastMouseDown = NO;

    mLastMouseDownEvent = nil;
    mClickThroughMouseDownEvent = nil;
    mDragService = nsnull;

    mGestureState = eGestureState_None;
    mCumulativeMagnification = 0.0;
    mCumulativeRotation = 0.0;

    // We can't call forceRefreshOpenGL here because, in order to work around
    // the bug, it seems we need to have a draw already happening. Therefore,
    // we call it in drawRect:inContext:, when we know that a draw is in
    // progress.
    mDidForceRefreshOpenGL = NO;

    [self setFocusRingType:NSFocusRingTypeNone];

#ifdef __LP64__
    mSwipeAnimationCancelled = nil;
#endif
  }
  
  // register for things we'll take from other applications
  PR_LOG(sCocoaLog, PR_LOG_ALWAYS, ("ChildView initWithFrame: registering drag types\n"));
  [self registerForDraggedTypes:[NSArray arrayWithObjects:NSFilenamesPboardType,
                                                          NSStringPboardType,
                                                          NSHTMLPboardType,
                                                          NSURLPboardType,
                                                          NSFilesPromisePboardType,
                                                          kWildcardPboardType,
                                                          kCorePboardType_url,
                                                          kCorePboardType_urld,
                                                          kCorePboardType_urln,
                                                          nil]];
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(windowBecameMain:)
                                               name:NSWindowDidBecomeMainNotification
                                             object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(windowResignedMain:)
                                               name:NSWindowDidResignMainNotification
                                             object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(systemMetricsChanged)
                                               name:NSControlTintDidChangeNotification
                                             object:nil];
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(systemMetricsChanged)
                                               name:NSSystemColorsDidChangeNotification
                                             object:nil];
  [[NSDistributedNotificationCenter defaultCenter] addObserver:self
                                                      selector:@selector(systemMetricsChanged)
                                                          name:@"AppleAquaScrollBarVariantChanged"
                                                        object:nil
                                            suspensionBehavior:NSNotificationSuspensionBehaviorDeliverImmediately]; 
  [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(_surfaceNeedsUpdate:)
                                               name:NSViewGlobalFrameDidChangeNotification
                                             object:self];

  return self;

  NS_OBJC_END_TRY_ABORT_BLOCK_NIL;
}

- (void)installTextInputHandler:(TextInputHandler*)aHandler
{
  mTextInputHandler = aHandler;
}

- (void)uninstallTextInputHandler
{
  mTextInputHandler = nsnull;
}

// Work around bug 603134.
// OS X has a bug that causes new OpenGL windows to only paint once or twice,
// then stop painting altogether. By clearing the drawable from the GL context,
// and then resetting the view to ourselves, we convince OS X to start updating
// again.
// This can cause a flash in new windows - bug 631339 - but it's very hard to
// fix that while maintaining this workaround.
- (void)forceRefreshOpenGL
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  [mGLContext clearDrawable];
  [mGLContext setView:self];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)setGLContext:(NSOpenGLContext *)aGLContext
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  mGLContext = aGLContext;
  [mGLContext retain];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)dealloc
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  [mGLContext release];
  [mPendingDirtyRects release];
  [mLastMouseDownEvent release];
  [mClickThroughMouseDownEvent release];
  ChildViewMouseTracker::OnDestroyView(self);

  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [[NSDistributedNotificationCenter defaultCenter] removeObserver:self];

  [super dealloc];    

#ifndef NP_NO_QUICKDRAW
  // This sets the current port to _savePort.
  // todo: Only do if a Quickdraw plugin is present in the hierarchy!
  // Check if ::SetPort() is available -- it probably won't be on
  // OS X 10.8 and up.
  if (::SetPort) {
    ::SetPort(NULL);
  }
#endif

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)updatePluginTopLevelWindowStatus:(BOOL)hasMain
{
  if (!mGeckoChild)
    return;

  nsPluginEvent pluginEvent(true, NS_PLUGIN_FOCUS_EVENT, mGeckoChild);
  NPCocoaEvent cocoaEvent;
  nsCocoaUtils::InitNPCocoaEvent(&cocoaEvent);
  cocoaEvent.type = NPCocoaEventWindowFocusChanged;
  cocoaEvent.data.focus.hasFocus = hasMain;
  nsCocoaUtils::InitPluginEvent(pluginEvent, cocoaEvent);
  mGeckoChild->DispatchWindowEvent(pluginEvent);
}

- (void)windowBecameMain:(NSNotification*)inNotification
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (mIsPluginView && mPluginEventModel == NPEventModelCocoa) {
    if ((NSWindow*)[inNotification object] == [self window]) {
      [self updatePluginTopLevelWindowStatus:YES];
    }
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)windowResignedMain:(NSNotification*)inNotification
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (mIsPluginView && mPluginEventModel == NPEventModelCocoa) {
    if ((NSWindow*)[inNotification object] == [self window]) {
      [self updatePluginTopLevelWindowStatus:NO];
    }
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)widgetDestroyed
{
  if (mTextInputHandler) {
    mTextInputHandler->OnDestroyWidget(mGeckoChild);
    mTextInputHandler = nsnull;
  }
  mGeckoChild = nsnull;

  // Just in case we're destroyed abruptly and missed the draggingExited
  // or performDragOperation message.
  NS_IF_RELEASE(mDragService);
}

// mozView method, return our gecko child view widget. Note this does not AddRef.
- (nsIWidget*) widget
{
  return static_cast<nsIWidget*>(mGeckoChild);
}

- (void)systemMetricsChanged
{
  if (!mGeckoChild)
    return;

  nsGUIEvent guiEvent(true, NS_THEMECHANGED, mGeckoChild);
  mGeckoChild->DispatchWindowEvent(guiEvent);
}

- (void)setNeedsPendingDisplay
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  mPendingFullDisplay = YES;
  if (!mPendingDisplay) {
    [self performSelector:@selector(processPendingRedraws) withObject:nil afterDelay:0];
    mPendingDisplay = YES;
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)setNeedsPendingDisplayInRect:(NSRect)invalidRect
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!mPendingDirtyRects)
    mPendingDirtyRects = [[NSMutableArray alloc] initWithCapacity:1];
  [mPendingDirtyRects addObject:[NSValue valueWithRect:invalidRect]];
  if (!mPendingDisplay) {
    [self performSelector:@selector(processPendingRedraws) withObject:nil afterDelay:0];
    mPendingDisplay = YES;
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

// Clears the queue of any pending invalides
- (void)processPendingRedraws
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (mPendingFullDisplay) {
    [self setNeedsDisplay:YES];
  }
  else if (mPendingDirtyRects) {
    unsigned int count = [mPendingDirtyRects count];
    for (unsigned int i = 0; i < count; ++i) {
      [self setNeedsDisplayInRect:[[mPendingDirtyRects objectAtIndex:i] rectValue]];
    }
  }
  mPendingFullDisplay = NO;
  mPendingDisplay = NO;
  [mPendingDirtyRects release];
  mPendingDirtyRects = nil;

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)setNeedsDisplayInRect:(NSRect)aRect
{
  [super setNeedsDisplayInRect:aRect];

  if ([[self window] isKindOfClass:[ToolbarWindow class]]) {
    ToolbarWindow* window = (ToolbarWindow*)[self window];
    if ([window drawsContentsIntoWindowFrame]) {
      // Tell it to mark the rect in the titlebar as dirty.
      NSView* borderView = [[window contentView] superview];
      [window setTitlebarNeedsDisplayInRect:[self convertRect:aRect toView:borderView]];
    }
  }
}

- (NSString*)description
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NIL;

  return [NSString stringWithFormat:@"ChildView %p, gecko child %p, frame %@", self, mGeckoChild, NSStringFromRect([self frame])];

  NS_OBJC_END_TRY_ABORT_BLOCK_NIL;
}

// Make the origin of this view the topLeft corner (gecko origin) rather
// than the bottomLeft corner (standard cocoa origin).
- (BOOL)isFlipped
{
  return YES;
}

- (BOOL)isOpaque
{
  return [[self window] isOpaque] && !mIsPluginView;
}

-(void)setIsPluginView:(BOOL)aIsPlugin
{
  mIsPluginView = aIsPlugin;
}

-(BOOL)isPluginView
{
  return mIsPluginView;
}

// Are we processing an NSLeftMouseDown event that will fail to click through?
// If so, we shouldn't focus or unfocus a plugin.
- (BOOL)isInFailingLeftClickThrough
{
  if (!mGeckoChild)
    return NO;

  if (!mClickThroughMouseDownEvent ||
      [mClickThroughMouseDownEvent type] != NSLeftMouseDown)
    return NO;

  BOOL retval =
    !ChildViewMouseTracker::WindowAcceptsEvent([self window],
                                               mClickThroughMouseDownEvent,
                                               self, true);

  // If we return YES here, this will result in us not being focused,
  // which will stop us receiving mClickThroughMouseDownEvent in
  // [ChildView mouseDown:].  So we need to release and null-out
  // mClickThroughMouseDownEvent here.
  if (retval) {
    [mClickThroughMouseDownEvent release];
    mClickThroughMouseDownEvent = nil;
  }

  return retval;
}

- (void)setPluginEventModel:(NPEventModel)eventModel
{
  mPluginEventModel = eventModel;
}

- (void)setPluginDrawingModel:(NPDrawingModel)drawingModel
{
  mPluginDrawingModel = drawingModel;
}

- (NPEventModel)pluginEventModel
{
  return mPluginEventModel;
}

- (NPDrawingModel)pluginDrawingModel
{
  return mPluginDrawingModel;
}

- (void)sendFocusEvent:(PRUint32)eventType
{
  if (!mGeckoChild)
    return;

  nsEventStatus status = nsEventStatus_eIgnore;
  nsGUIEvent focusGuiEvent(true, eventType, mGeckoChild);
  focusGuiEvent.time = PR_IntervalNow();
  mGeckoChild->DispatchEvent(&focusGuiEvent, status);
}

// We accept key and mouse events, so don't keep passing them up the chain. Allow
// this to be a 'focused' widget for event dispatch.
- (BOOL)acceptsFirstResponder
{
  return YES;
}

// Accept mouse down events on background windows
- (BOOL)acceptsFirstMouse:(NSEvent*)aEvent
{
  if (![[self window] isKindOfClass:[PopupWindow class]]) {
    // We rely on this function to tell us that the mousedown was on a
    // background window. Inside mouseDown we can't tell whether we were
    // inactive because at that point we've already been made active.
    // Unfortunately, acceptsFirstMouse is called for PopupWindows even when
    // their parent window is active, so ignore this on them for now.
    mClickThroughMouseDownEvent = [aEvent retain];
  }
  return YES;
}

- (void)viewWillMoveToWindow:(NSWindow *)newWindow
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!newWindow)
    HideChildPluginViews(self);

  [super viewWillMoveToWindow:newWindow];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)viewDidMoveToWindow
{
  if (mPluginEventModel == NPEventModelCocoa &&
      [self window] && [self isPluginView] && mGeckoChild) {
    mGeckoChild->UpdatePluginPort();
  }

  [super viewDidMoveToWindow];
}

- (void)scrollRect:(NSRect)aRect by:(NSSize)offset
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  // Update any pending dirty rects to reflect the new scroll position
  if (mPendingDirtyRects) {
    unsigned int count = [mPendingDirtyRects count];
    for (unsigned int i = 0; i < count; ++i) {
      NSRect oldRect = [[mPendingDirtyRects objectAtIndex:i] rectValue];
      NSRect newRect = NSOffsetRect(oldRect, offset.width, offset.height);
      [mPendingDirtyRects replaceObjectAtIndex:i
                                    withObject:[NSValue valueWithRect:newRect]];
    }
  }
  [super scrollRect:aRect by:offset];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (BOOL)mouseDownCanMoveWindow
{
  return NO;
}

- (void)lockFocus
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

#ifndef NP_NO_QUICKDRAW
  // Set the current GrafPort to a "safe" port before calling [NSQuickDrawView lockFocus],
  // so that the NSQuickDrawView stashes a pointer to this known-good port internally.
  // It will set the port back to this port on destruction.
  // Check if ::SetPort() is available -- it probably won't be on OS X 10.8 and up.
  if (::SetPort) {
    ::SetPort(NULL);  // todo: only do if a Quickdraw plugin is present in the hierarchy!
  }
#endif

  [super lockFocus];

  if (mGLContext) {
    if ([mGLContext view] != self) {
      [mGLContext setView:self];
    }

    [mGLContext makeCurrentContext];
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

-(void)update
{
  if (mGLContext) {
    [mGLContext update];
  }
}

- (void) _surfaceNeedsUpdate:(NSNotification*)notification
{
   [self update];
}

// The display system has told us that a portion of our view is dirty. Tell
// gecko to paint it
- (void)drawRect:(NSRect)aRect
{
  CGContextRef cgContext = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];
  [self drawRect:aRect inContext:cgContext];

  // If we're a transparent window and our contents have changed, we need
  // to make sure the shadow is updated to the new contents.
  if ([[self window] isKindOfClass:[BaseWindow class]]) {
    [(BaseWindow*)[self window] deferredInvalidateShadow];
  }
}

- (void)drawRect:(NSRect)aRect inTitlebarContext:(CGContextRef)aContext
{
  if (!mGeckoChild)
    return;

  // Title bar drawing only works if we really draw into aContext, which only
  // the basic layer manager will do.
  nsBaseWidget::AutoUseBasicLayerManager setupLayerManager(mGeckoChild);
  [self drawRect:aRect inContext:aContext];
}

- (void)drawRect:(NSRect)aRect inContext:(CGContextRef)aContext
{
  SAMPLE_LABEL("widget", "ChildView::drawRect");
  bool isVisible;
  if (!mGeckoChild || NS_FAILED(mGeckoChild->IsVisible(isVisible)) ||
      !isVisible)
    return;

#ifndef NP_NO_QUICKDRAW
  if (mIsPluginView && mPluginDrawingModel == NPDrawingModelQuickDraw) {
    mGeckoChild->PaintQD();
    return;
  }
#endif

  // Don't ever draw non-QuickDraw plugin views explicitly; they'll be drawn as
  // part of their parent widget.
  if (mIsPluginView)
    return;

#ifdef DEBUG_UPDATE
  nsIntRect geckoBounds;
  mGeckoChild->GetBounds(geckoBounds);

  fprintf (stderr, "---- Update[%p][%p] [%f %f %f %f] cgc: %p\n  gecko bounds: [%d %d %d %d]\n",
           self, mGeckoChild,
           aRect.origin.x, aRect.origin.y, aRect.size.width, aRect.size.height, aContext,
           geckoBounds.x, geckoBounds.y, geckoBounds.width, geckoBounds.height);

  CGAffineTransform xform = CGContextGetCTM(aContext);
  fprintf (stderr, "  xform in: [%f %f %f %f %f %f]\n", xform.a, xform.b, xform.c, xform.d, xform.tx, xform.ty);
#endif
  // Create the event so we can fill in its region
  nsPaintEvent paintEvent(true, NS_PAINT, mGeckoChild);
  paintEvent.didSendWillPaint = true;

  nsIntRect boundingRect =
    nsIntRect(aRect.origin.x, aRect.origin.y, aRect.size.width, aRect.size.height);
  const NSRect *rects;
  NSInteger count, i;
  [[NSView focusView] getRectsBeingDrawn:&rects count:&count];
  if (count < MAX_RECTS_IN_REGION) {
    for (i = 0; i < count; ++i) {
      // Add the rect to the region.
      const NSRect& r = [self convertRect:rects[i] fromView:[NSView focusView]];
      paintEvent.region.Or(paintEvent.region,
        nsIntRect(r.origin.x, r.origin.y, r.size.width, r.size.height));
    }
    paintEvent.region.And(paintEvent.region, boundingRect);
  } else {
    paintEvent.region = boundingRect;
  }

#ifndef NP_NO_QUICKDRAW
  // Subtract quickdraw plugin rectangles from the region
  NSArray* subviews = [self subviews];
  for (int i = 0; i < int([subviews count]); ++i) {
    NSView* view = [subviews objectAtIndex:i];
    if (![view isKindOfClass:[ChildView class]] || [view isHidden])
      continue;
    ChildView* cview = (ChildView*) view;
    if ([cview isPluginView] && [cview pluginDrawingModel] == NPDrawingModelQuickDraw) {
      NSRect frame = [view frame];
      paintEvent.region.Sub(paintEvent.region,
        nsIntRect(frame.origin.x, frame.origin.y, frame.size.width, frame.size.height));
    }
  }
#endif

  LayerManager *layerManager = mGeckoChild->GetLayerManager(nsnull);
  if (layerManager->GetBackendType() == LayerManager::LAYERS_OPENGL) {
    NSOpenGLContext *glContext;

    LayerManagerOGL *manager = static_cast<LayerManagerOGL*>(layerManager);
    manager->SetClippingRegion(paintEvent.region);
    glContext = (NSOpenGLContext *)manager->gl()->GetNativeData(mozilla::gl::GLContext::NativeGLContext);

    if (!mGLContext) {
      [self setGLContext:glContext];
    }

    mGeckoChild->DispatchWindowEvent(paintEvent);

    // Force OpenGL to refresh the very first time we draw. This works around a
    // Mac OS X bug that stops windows updating on OS X when we use OpenGL.
    if (!mDidForceRefreshOpenGL) {
      [self performSelector:@selector(forceRefreshOpenGL) withObject:nil afterDelay:0];
      mDidForceRefreshOpenGL = YES;
    }

    return;
  }

  // Create Cairo objects.
  NSSize bufferSize = [self bounds].size;
  nsRefPtr<gfxQuartzSurface> targetSurface =
    new gfxQuartzSurface(aContext, gfxSize(bufferSize.width, bufferSize.height));
  targetSurface->SetAllowUseAsSource(false);

  nsRefPtr<gfxContext> targetContext = new gfxContext(targetSurface);

  // Set up the clip region.
  nsIntRegionRectIterator iter(paintEvent.region);
  targetContext->NewPath();
  for (;;) {
    const nsIntRect* r = iter.Next();
    if (!r)
      break;
    targetContext->Rectangle(gfxRect(r->x, r->y, r->width, r->height));
  }
  targetContext->Clip();

  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  bool painted;
  {
    nsBaseWidget::AutoLayerManagerSetup
      setupLayerManager(mGeckoChild, targetContext, BasicLayerManager::BUFFER_NONE);
    painted = mGeckoChild->DispatchWindowEvent(paintEvent);
  }

  // Force OpenGL to refresh the very first time we draw. This works around a
  // Mac OS X bug that stops windows updating on OS X when we use OpenGL.
  if (painted && !mDidForceRefreshOpenGL &&
      layerManager->AsShadowManager() && mUsingOMTCompositor) {
    if (!mDidForceRefreshOpenGL) {
      [self performSelector:@selector(forceRefreshOpenGL) withObject:nil afterDelay:0];
      mDidForceRefreshOpenGL = YES;
    }
  }

  if (!painted && [self isOpaque]) {
    // Gecko refused to draw, but we've claimed to be opaque, so we have to
    // draw something--fill with white.
    CGContextSetRGBFillColor(aContext, 1, 1, 1, 1);
    CGContextFillRect(aContext, CGRectMake(aRect.origin.x, aRect.origin.y,
                                           aRect.size.width, aRect.size.height));
  }

  // note that the cairo surface *MUST* be destroyed at this point,
  // or bad things will happen (since we can't keep the cgContext around
  // beyond this drawRect message handler)

#ifdef DEBUG_UPDATE
  fprintf (stderr, "---- update done ----\n");

#if 0
  CGContextSetRGBStrokeColor (aContext,
                            ((((unsigned long)self) & 0xff)) / 255.0,
                            ((((unsigned long)self) & 0xff00) >> 8) / 255.0,
                            ((((unsigned long)self) & 0xff0000) >> 16) / 255.0,
                            0.5);
#endif 
  CGContextSetRGBStrokeColor(aContext, 1, 0, 0, 0.8);
  CGContextSetLineWidth(aContext, 4.0);
  CGContextStrokeRect(aContext,
                      CGRectMake(aRect.origin.x, aRect.origin.y, aRect.size.width, aRect.size.height));
#endif
}

- (void)releaseWidgets:(NSArray*)aWidgetArray
{
  if (!aWidgetArray) {
    return;
  }
  NSInteger count = [aWidgetArray count];
  for (NSInteger i = 0; i < count; ++i) {
    NSNumber* pointer = (NSNumber*) [aWidgetArray objectAtIndex:i];
    nsIWidget* widget = (nsIWidget*) [pointer unsignedIntegerValue];
    NS_RELEASE(widget);
  }
}

- (void)viewWillDraw
{
  if (mGeckoChild) {
    // The OS normally *will* draw our NSWindow, no matter what we do here.
    // But Gecko can delete our parent widget(s) (along with mGeckoChild)
    // while processing an NS_WILL_PAINT event, which closes our NSWindow and
    // makes the OS throw an NSInternalInconsistencyException assertion when
    // it tries to draw it.  Sometimes the OS also aborts the browser process.
    // So we need to retain our parent(s) here and not release it/them until
    // the next time through the main thread's run loop.  When we do this we
    // also need to retain and release mGeckoChild, which holds a strong
    // reference to us (otherwise we might have been deleted by the time
    // releaseWidgets: is called on us).  See bug 550392.
    nsIWidget* parent = mGeckoChild->GetParent();
    if (parent) {
      NSMutableArray* widgetArray = [NSMutableArray arrayWithCapacity:3];
      while (parent) {
        NS_ADDREF(parent);
        [widgetArray addObject:[NSNumber numberWithUnsignedInteger:(NSUInteger)parent]];
        parent = parent->GetParent();
      }
      NS_ADDREF(mGeckoChild);
      [widgetArray addObject:[NSNumber numberWithUnsignedInteger:(NSUInteger)mGeckoChild]];
      [self performSelector:@selector(releaseWidgets:)
                 withObject:widgetArray
                 afterDelay:0];
    }
    nsPaintEvent paintEvent(true, NS_WILL_PAINT, mGeckoChild);
    mGeckoChild->DispatchWindowEvent(paintEvent);
  }
  [super viewWillDraw];
}

// Allows us to turn off setting up the clip region
// before each drawRect. We already clip within gecko.
- (BOOL)wantsDefaultClipping
{
  return NO;
}

#if USE_CLICK_HOLD_CONTEXTMENU
//
// -clickHoldCallback:
//
// called from a timer two seconds after a mouse down to see if we should display
// a context menu (click-hold). |anEvent| is the original mouseDown event. If we're
// still in that mouseDown by this time, put up the context menu, otherwise just
// fuhgeddaboutit. |anEvent| has been retained by the OS until after this callback
// fires so we're ok there.
//
// This code currently messes in a bunch of edge cases (bugs 234751, 232964, 232314)
// so removing it until we get it straightened out.
//
- (void)clickHoldCallback:(id)theEvent;
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if( theEvent == [NSApp currentEvent] ) {
    // we're still in the middle of the same mousedown event here, activate
    // click-hold context menu by triggering the right mouseDown action.
    NSEvent* clickHoldEvent = [NSEvent mouseEventWithType:NSRightMouseDown
                                                  location:[theEvent locationInWindow]
                                             modifierFlags:[theEvent modifierFlags]
                                                 timestamp:[theEvent timestamp]
                                              windowNumber:[theEvent windowNumber]
                                                   context:[theEvent context]
                                               eventNumber:[theEvent eventNumber]
                                                clickCount:[theEvent clickCount]
                                                  pressure:[theEvent pressure]];
    [self rightMouseDown:clickHoldEvent];
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}
#endif

// If we've just created a non-native context menu, we need to mark it as
// such and let the OS (and other programs) know when it opens and closes
// (this is how the OS knows to close other programs' context menus when
// ours open).  We send the initial notification here, but others are sent
// in nsCocoaWindow::Show().
- (void)maybeInitContextMenuTracking
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!gRollupWidget)
    return;

#ifdef MOZ_USE_NATIVE_POPUP_WINDOWS
  return;
#endif /* MOZ_USE_NATIVE_POPUP_WINDOWS */

  NSWindow *popupWindow = (NSWindow*)gRollupWidget->GetNativeData(NS_NATIVE_WINDOW);
  if (!popupWindow || ![popupWindow isKindOfClass:[PopupWindow class]])
    return;

  [[NSDistributedNotificationCenter defaultCenter]
    postNotificationName:@"com.apple.HIToolbox.beginMenuTrackingNotification"
                  object:@"org.mozilla.gecko.PopupWindow"];
  [(PopupWindow*)popupWindow setIsContextMenu:YES];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

// Returns true if the event should no longer be processed, false otherwise.
// This does not return whether or not anything was rolled up.
- (BOOL)maybeRollup:(NSEvent*)theEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN;

  BOOL consumeEvent = NO;

  if (gRollupWidget && gRollupListener) {
    NSWindow* currentPopup = static_cast<NSWindow*>(gRollupWidget->GetNativeData(NS_NATIVE_WINDOW));
    if (!nsCocoaUtils::IsEventOverWindow(theEvent, currentPopup)) {
      // event is not over the rollup window, default is to roll up
      bool shouldRollup = true;

      // check to see if scroll events should roll up the popup
      if ([theEvent type] == NSScrollWheel) {
        shouldRollup = gRollupListener->ShouldRollupOnMouseWheelEvent();
        // always consume scroll events that aren't over the popup
        consumeEvent = YES;
      }

      // if we're dealing with menus, we probably have submenus and
      // we don't want to rollup if the click is in a parent menu of
      // the current submenu
      PRUint32 popupsToRollup = PR_UINT32_MAX;
      if (gRollupListener) {
        nsAutoTArray<nsIWidget*, 5> widgetChain;
        PRUint32 sameTypeCount = gRollupListener->GetSubmenuWidgetChain(&widgetChain);
        for (PRUint32 i = 0; i < widgetChain.Length(); i++) {
          nsIWidget* widget = widgetChain[i];
          NSWindow* currWindow = (NSWindow*)widget->GetNativeData(NS_NATIVE_WINDOW);
          if (nsCocoaUtils::IsEventOverWindow(theEvent, currWindow)) {
            // don't roll up if the mouse event occurred within a menu of the
            // same type. If the mouse event occurred in a menu higher than
            // that, roll up, but pass the number of popups to Rollup so
            // that only those of the same type close up.
            if (i < sameTypeCount) {
              shouldRollup = false;
            }
            else {
              popupsToRollup = sameTypeCount;
            }
            break;
          }
        }
      }

      if (shouldRollup) {
        gRollupListener->Rollup(popupsToRollup);
        consumeEvent = (BOOL)gConsumeRollupEvent;
      }
    }
  }

  return consumeEvent;

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(NO);
}

/*
 * XXX - The swipeWithEvent, beginGestureWithEvent, magnifyWithEvent,
 * rotateWithEvent, and endGestureWithEvent methods are part of a
 * PRIVATE interface exported by nsResponder and reverse-engineering
 * was necessary to obtain the methods' prototypes. Thus, Apple may
 * change the interface in the future without notice.
 *
 * The prototypes were obtained from the following link:
 * http://cocoadex.com/2008/02/nsevent-modifications-swipe-ro.html
 */

- (void)swipeWithEvent:(NSEvent *)anEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!anEvent || !mGeckoChild)
    return;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  float deltaX = [anEvent deltaX];  // left=1.0, right=-1.0
  float deltaY = [anEvent deltaY];  // up=1.0, down=-1.0

  // Setup the "swipe" event.
  nsSimpleGestureEvent geckoEvent(true, NS_SIMPLE_GESTURE_SWIPE, mGeckoChild, 0, 0.0);
  [self convertCocoaMouseEvent:anEvent toGeckoEvent:&geckoEvent];

  // Record the left/right direction.
  if (deltaX > 0.0)
    geckoEvent.direction |= nsIDOMSimpleGestureEvent::DIRECTION_LEFT;
  else if (deltaX < 0.0)
    geckoEvent.direction |= nsIDOMSimpleGestureEvent::DIRECTION_RIGHT;

  // Record the up/down direction.
  if (deltaY > 0.0)
    geckoEvent.direction |= nsIDOMSimpleGestureEvent::DIRECTION_UP;
  else if (deltaY < 0.0)
    geckoEvent.direction |= nsIDOMSimpleGestureEvent::DIRECTION_DOWN;

  // Send the event.
  mGeckoChild->DispatchWindowEvent(geckoEvent);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)beginGestureWithEvent:(NSEvent *)anEvent
{
  NS_ASSERTION(mGestureState == eGestureState_None, "mGestureState should be eGestureState_None");

  if (!anEvent)
    return;

  mGestureState = eGestureState_StartGesture;
  mCumulativeMagnification = 0;
  mCumulativeRotation = 0.0;
}

- (void)magnifyWithEvent:(NSEvent *)anEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!anEvent || !mGeckoChild)
    return;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  float deltaZ = [anEvent deltaZ];

  PRUint32 msg;
  switch (mGestureState) {
  case eGestureState_StartGesture:
    msg = NS_SIMPLE_GESTURE_MAGNIFY_START;
    mGestureState = eGestureState_MagnifyGesture;
    break;

  case eGestureState_MagnifyGesture:
    msg = NS_SIMPLE_GESTURE_MAGNIFY_UPDATE;
    break;

  case eGestureState_None:
  case eGestureState_RotateGesture:
  default:
    return;
  }

  // Setup the event.
  nsSimpleGestureEvent geckoEvent(true, msg, mGeckoChild, 0, deltaZ);
  [self convertCocoaMouseEvent:anEvent toGeckoEvent:&geckoEvent];

  // Send the event.
  mGeckoChild->DispatchWindowEvent(geckoEvent);

  // Keep track of the cumulative magnification for the final "magnify" event.
  mCumulativeMagnification += deltaZ;
  
  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)rotateWithEvent:(NSEvent *)anEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!anEvent || !mGeckoChild)
    return;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  float rotation = [anEvent rotation];

  PRUint32 msg;
  switch (mGestureState) {
  case eGestureState_StartGesture:
    msg = NS_SIMPLE_GESTURE_ROTATE_START;
    mGestureState = eGestureState_RotateGesture;
    break;

  case eGestureState_RotateGesture:
    msg = NS_SIMPLE_GESTURE_ROTATE_UPDATE;
    break;

  case eGestureState_None:
  case eGestureState_MagnifyGesture:
  default:
    return;
  }

  // Setup the event.
  nsSimpleGestureEvent geckoEvent(true, msg, mGeckoChild, 0, 0.0);
  [self convertCocoaMouseEvent:anEvent toGeckoEvent:&geckoEvent];
  geckoEvent.delta = -rotation;
  if (rotation > 0.0) {
    geckoEvent.direction = nsIDOMSimpleGestureEvent::ROTATION_COUNTERCLOCKWISE;
  } else {
    geckoEvent.direction = nsIDOMSimpleGestureEvent::ROTATION_CLOCKWISE;
  }

  // Send the event.
  mGeckoChild->DispatchWindowEvent(geckoEvent);

  // Keep track of the cumulative rotation for the final "rotate" event.
  mCumulativeRotation += rotation;

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)endGestureWithEvent:(NSEvent *)anEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!anEvent || !mGeckoChild) {
    // Clear the gestures state if we cannot send an event.
    mGestureState = eGestureState_None;
    mCumulativeMagnification = 0.0;
    mCumulativeRotation = 0.0;
    return;
  }

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  switch (mGestureState) {
  case eGestureState_MagnifyGesture:
    {
      // Setup the "magnify" event.
      nsSimpleGestureEvent geckoEvent(true, NS_SIMPLE_GESTURE_MAGNIFY,
                                      mGeckoChild, 0, mCumulativeMagnification);
      [self convertCocoaMouseEvent:anEvent toGeckoEvent:&geckoEvent];

      // Send the event.
      mGeckoChild->DispatchWindowEvent(geckoEvent);
    }
    break;

  case eGestureState_RotateGesture:
    {
      // Setup the "rotate" event.
      nsSimpleGestureEvent geckoEvent(true, NS_SIMPLE_GESTURE_ROTATE, mGeckoChild, 0, 0.0);
      [self convertCocoaMouseEvent:anEvent toGeckoEvent:&geckoEvent];
      geckoEvent.delta = -mCumulativeRotation;
      if (mCumulativeRotation > 0.0) {
        geckoEvent.direction = nsIDOMSimpleGestureEvent::ROTATION_COUNTERCLOCKWISE;
      } else {
        geckoEvent.direction = nsIDOMSimpleGestureEvent::ROTATION_CLOCKWISE;
      }

      // Send the event.
      mGeckoChild->DispatchWindowEvent(geckoEvent);
    }
    break;

  case eGestureState_None:
  case eGestureState_StartGesture:
  default:
    break;
  }

  // Clear the gestures state.
  mGestureState = eGestureState_None;
  mCumulativeMagnification = 0.0;
  mCumulativeRotation = 0.0;

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

// Support fluid swipe tracking on OS X 10.7 and higher.  We must be careful
// to only invoke this support on a horizontal two-finger gesture that really
// is a swipe (and not a scroll) -- in other words, the app is responsible
// for deciding which is which.  But once the decision is made, the OS tracks
// the swipe until it has finished, and decides whether or not it succeeded.
// A swipe has the same functionality as the Back and Forward buttons.  For
// now swipe animation is unsupported (e.g. no bounces).  This method is
// partly based on Apple sample code available at
// http://developer.apple.com/library/mac/#releasenotes/Cocoa/AppKit.html
// (under Fluid Swipe Tracking API).
#ifdef __LP64__
- (void)maybeTrackScrollEventAsSwipe:(NSEvent *)anEvent
                      scrollOverflow:(PRInt32)overflow
{
  if (!nsCocoaFeatures::OnLionOrLater()) {
    return;
  }
  // This method checks whether the AppleEnableSwipeNavigateWithScrolls global
  // preference is set.  If it isn't, fluid swipe tracking is disabled, and a
  // horizontal two-finger gesture is always a scroll (even in Safari).  This
  // preference can't (currently) be set from the Preferences UI -- only using
  // 'defaults write'.
  if (![NSEvent isSwipeTrackingFromScrollEventsEnabled]) {
    return;
  }
  if ([anEvent type] != NSScrollWheel) {
    return;
  }

  // If a swipe is currently being tracked kill it -- it's been interrupted by
  // another gesture or legacy scroll wheel event.
  if (mSwipeAnimationCancelled && (*mSwipeAnimationCancelled == NO)) {
    *mSwipeAnimationCancelled = YES;
    mSwipeAnimationCancelled = nil;
  }

  // Only initiate tracking if the user has tried to scroll past the edge of
  // the current page (as indicated by 'overflow' being non-zero).  Gecko only
  // sets nsMouseScrollEvent.scrollOverflow when it's processing
  // NS_MOUSE_PIXEL_SCROLL events (not NS_MOUSE_SCROLL events).
  // nsMouseScrollEvent.scrollOverflow only indicates left or right overflow
  // for horizontal NS_MOUSE_PIXEL_SCROLL events.
  if (!overflow) {
    return;
  }
  // Only initiate tracking for gestures that have just begun -- otherwise a
  // scroll to one side of the page can have a swipe tacked on to it.
  if ([anEvent phase] != NSEventPhaseBegan) {
    return;
  }
  CGFloat deltaX, deltaY;
  if ([anEvent hasPreciseScrollingDeltas]) {
    deltaX = [anEvent scrollingDeltaX];
    deltaY = [anEvent scrollingDeltaY];
  } else {
    deltaX = [anEvent deltaX];
    deltaY = [anEvent deltaY];
  }
  // Only initiate tracking for events whose horizontal element is at least
  // eight times larger than its vertical element.  This minimizes performance
  // problems with vertical scrolls (by minimizing the possibility that they'll
  // be misinterpreted as horizontal swipes), while still tolerating a small
  // vertical element to a true horizontal swipe.  The number '8' was arrived
  // at by trial and error.
  if ((deltaX == 0) || (fabs(deltaX) <= fabs(deltaY) * 8)) {
    return;
  }

  __block BOOL animationCancelled = NO;
  // At this point, anEvent is the first scroll wheel event in a two-finger
  // horizontal gesture that we've decided to treat as a swipe.  When we call
  // [NSEvent trackSwipeEventWithOptions:...], the OS interprets all
  // subsequent scroll wheel events that are part of this gesture as a swipe,
  // and stops sending them to us.  The OS calls the trackingHandler "block"
  // multiple times, asynchronously (sometimes after [NSEvent
  // maybeTrackScrollEventAsSwipe:...] has returned).  The OS determines when
  // the gesture has finished, and whether or not it was "successful" -- this
  // information is passed to trackingHandler.  We must be careful to only
  // call [NSEvent maybeTrackScrollEventAsSwipe:...] on a "real" swipe --
  // otherwise two-finger scrolling performance will suffer significantly.
  // Note that we use anEvent inside the block. This extends the lifetime of
  // the anEvent object because it's retained by the block, see bug 682445.
  // The block will release it when the block goes away at the end of the
  // animation, or when the animation is canceled.
  [anEvent trackSwipeEventWithOptions:0
             dampenAmountThresholdMin:-1
                                  max:1
                         usingHandler:^(CGFloat gestureAmount, NSEventPhase phase, BOOL isComplete, BOOL *stop) {
      // Since this tracking handler can be called asynchronously, mGeckoChild
      // might have become NULL here (our child widget might have been
      // destroyed).
      if (animationCancelled || !mGeckoChild) {
        *stop = YES;
        return;
      }
      // gestureAmount is documented to be '-1', '0' or '1' when isComplete
      // is TRUE, but the docs don't say anything about its value at other
      // times.  However, tests show that, when phase == NSEventPhaseEnded,
      // gestureAmount is negative when it will be '-1' at isComplete, and
      // positive when it will be '1'.  And phase is never equal to
      // NSEventPhaseEnded when gestureAmount will be '0' at isComplete.
      // Not waiting until isComplete is TRUE substantially reduces the
      // time it takes to change pages after a swipe, and helps resolve
      // bug 678891.
      if (phase == NSEventPhaseEnded) {
        if (gestureAmount) {
          nsSimpleGestureEvent geckoEvent(true, NS_SIMPLE_GESTURE_SWIPE, mGeckoChild, 0, 0.0);
          [self convertCocoaMouseEvent:anEvent toGeckoEvent:&geckoEvent];
          if (gestureAmount > 0) {
            geckoEvent.direction |= nsIDOMSimpleGestureEvent::DIRECTION_LEFT;
          } else {
            geckoEvent.direction |= nsIDOMSimpleGestureEvent::DIRECTION_RIGHT;
          }
          mGeckoChild->DispatchWindowEvent(geckoEvent);
        }
        mSwipeAnimationCancelled = nil;
      } else if (phase == NSEventPhaseCancelled) {
        mSwipeAnimationCancelled = nil;
      }
    }];

  // We keep a pointer to the __block variable (animationCanceled) so we
  // can cancel our block handler at any time.  Note: We must assign
  // &animationCanceled after our block creation and copy -- its address
  // isn't resolved until then!
  mSwipeAnimationCancelled = &animationCancelled;
}
#endif // #ifdef __LP64__

- (void)setUsingOMTCompositor:(BOOL)aUseOMTC
{
  mUsingOMTCompositor = aUseOMTC;
}


// Returning NO from this method only disallows ordering on mousedown - in order
// to prevent it for mouseup too, we need to call [NSApp preventWindowOrdering]
// when handling the mousedown event.
- (BOOL)shouldDelayWindowOrderingForEvent:(NSEvent*)aEvent
{
  // Always using system-provided window ordering for normal windows.
  if (![[self window] isKindOfClass:[PopupWindow class]])
    return NO;

  // Don't reorder when we don't have a parent window, like when we're a
  // context menu or a tooltip.
  return ![[self window] parentWindow];
}

- (void)mouseDown:(NSEvent*)theEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if ([self shouldDelayWindowOrderingForEvent:theEvent]) {
    [NSApp preventWindowOrdering];
  }

  // If we've already seen this event due to direct dispatch from menuForEvent:
  // just bail; if not, remember it.
  if (mLastMouseDownEvent == theEvent) {
    [mLastMouseDownEvent release];
    mLastMouseDownEvent = nil;
    return;
  }
  else {
    [mLastMouseDownEvent release];
    mLastMouseDownEvent = [theEvent retain];
  }

  [gLastDragMouseDownEvent release];
  gLastDragMouseDownEvent = [theEvent retain];

  // We need isClickThrough because at this point the window we're in might
  // already have become main, so the check for isMainWindow in
  // WindowAcceptsEvent isn't enough. It also has to check isClickThrough.
  BOOL isClickThrough = (theEvent == mClickThroughMouseDownEvent);
  [mClickThroughMouseDownEvent release];
  mClickThroughMouseDownEvent = nil;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  if ([self maybeRollup:theEvent] ||
      !ChildViewMouseTracker::WindowAcceptsEvent([self window], theEvent, self, isClickThrough)) {
    // Remember blocking because that means we want to block mouseup as well.
    mBlockedLastMouseDown = YES;
    return;
  }

#if USE_CLICK_HOLD_CONTEXTMENU
  // fire off timer to check for click-hold after two seconds. retains |theEvent|
  [self performSelector:@selector(clickHoldCallback:) withObject:theEvent afterDelay:2.0];
#endif

  // in order to send gecko events we'll need a gecko widget
  if (!mGeckoChild)
    return;

  NSUInteger modifierFlags = [theEvent modifierFlags];

  nsMouseEvent geckoEvent(true, NS_MOUSE_BUTTON_DOWN, mGeckoChild, nsMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];

  NSInteger clickCount = [theEvent clickCount];
  if (mBlockedLastMouseDown && clickCount > 1) {
    // Don't send a double click if the first click of the double click was
    // blocked.
    clickCount--;
  }
  geckoEvent.clickCount = clickCount;

  if (modifierFlags & NSControlKeyMask)
    geckoEvent.button = nsMouseEvent::eRightButton;
  else
    geckoEvent.button = nsMouseEvent::eLeftButton;

  // Create event for use by plugins.
  // This is going to our child view so we don't need to look up the destination
  // event type.
#ifndef NP_NO_CARBON
  EventRecord carbonEvent;
  if (mPluginEventModel == NPEventModelCarbon) {
    carbonEvent.what = mouseDown;
    carbonEvent.message = 0;
    carbonEvent.when = ::TickCount();
    ::GetGlobalMouse(&carbonEvent.where);
    carbonEvent.modifiers = ::GetCurrentKeyModifiers();
    geckoEvent.pluginEvent = &carbonEvent;
  }
#endif
  NPCocoaEvent cocoaEvent;
  if (mPluginEventModel == NPEventModelCocoa) {
    nsCocoaUtils::InitNPCocoaEvent(&cocoaEvent);
    NSPoint point = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    cocoaEvent.type = NPCocoaEventMouseDown;
    cocoaEvent.data.mouse.modifierFlags = modifierFlags;
    cocoaEvent.data.mouse.pluginX = point.x;
    cocoaEvent.data.mouse.pluginY = point.y;
    cocoaEvent.data.mouse.buttonNumber = [theEvent buttonNumber];
    cocoaEvent.data.mouse.clickCount = clickCount;
    cocoaEvent.data.mouse.deltaX = [theEvent deltaX];
    cocoaEvent.data.mouse.deltaY = [theEvent deltaY];
    cocoaEvent.data.mouse.deltaZ = [theEvent deltaZ];
    geckoEvent.pluginEvent = &cocoaEvent;
  }

  mGeckoChild->DispatchWindowEvent(geckoEvent);
  mBlockedLastMouseDown = NO;

  // XXX maybe call markedTextSelectionChanged:client: here?

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)mouseUp:(NSEvent *)theEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!mGeckoChild || mBlockedLastMouseDown)
    return;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

#ifndef NP_NO_CARBON
  EventRecord carbonEvent;
#endif // ifndef NP_NO_CARBON
  NPCocoaEvent cocoaEvent;
	
  nsMouseEvent geckoEvent(true, NS_MOUSE_BUTTON_UP, mGeckoChild, nsMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  if ([theEvent modifierFlags] & NSControlKeyMask)
    geckoEvent.button = nsMouseEvent::eRightButton;
  else
    geckoEvent.button = nsMouseEvent::eLeftButton;

  // Create event for use by plugins.
  // This is going to our child view so we don't need to look up the destination
  // event type.
  if (mIsPluginView) {
#ifndef NP_NO_CARBON
    if (mPluginEventModel == NPEventModelCarbon) {
      carbonEvent.what = mouseUp;
      carbonEvent.message = 0;
      carbonEvent.when = ::TickCount();
      ::GetGlobalMouse(&carbonEvent.where);
      carbonEvent.modifiers = ::GetCurrentKeyModifiers();
      geckoEvent.pluginEvent = &carbonEvent;
    }
#endif
    if (mPluginEventModel == NPEventModelCocoa) {
      nsCocoaUtils::InitNPCocoaEvent(&cocoaEvent);
      NSPoint point = [self convertPoint:[theEvent locationInWindow] fromView:nil];
      cocoaEvent.type = NPCocoaEventMouseUp;
      cocoaEvent.data.mouse.modifierFlags = [theEvent modifierFlags];
      cocoaEvent.data.mouse.pluginX = point.x;
      cocoaEvent.data.mouse.pluginY = point.y;
      cocoaEvent.data.mouse.buttonNumber = [theEvent buttonNumber];
      cocoaEvent.data.mouse.clickCount = [theEvent clickCount];
      cocoaEvent.data.mouse.deltaX = [theEvent deltaX];
      cocoaEvent.data.mouse.deltaY = [theEvent deltaY];
      cocoaEvent.data.mouse.deltaZ = [theEvent deltaZ];
      geckoEvent.pluginEvent = &cocoaEvent;
    }
  }

  // This might destroy our widget (and null out mGeckoChild).
  mGeckoChild->DispatchWindowEvent(geckoEvent);

  // If our mouse-up event's location is over some other object (as might
  // happen if it came at the end of a dragging operation), also send our
  // Gecko frame a mouse-exit event.
  if (mGeckoChild && mIsPluginView) {
#ifndef NP_NO_CARBON
    if (mPluginEventModel == NPEventModelCocoa)
#endif
    {
      if (ChildViewMouseTracker::ViewForEvent(theEvent) != self) {
        nsMouseEvent geckoExitEvent(true, NS_MOUSE_EXIT, mGeckoChild, nsMouseEvent::eReal);
        [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoExitEvent];

        NPCocoaEvent cocoaEvent;
        nsCocoaUtils::InitNPCocoaEvent(&cocoaEvent);
        NSPoint point = [self convertPoint:[theEvent locationInWindow] fromView:nil];
        cocoaEvent.type = NPCocoaEventMouseExited;
        cocoaEvent.data.mouse.modifierFlags = [theEvent modifierFlags];
        cocoaEvent.data.mouse.pluginX = point.x;
        cocoaEvent.data.mouse.pluginY = point.y;
        cocoaEvent.data.mouse.buttonNumber = [theEvent buttonNumber];
        cocoaEvent.data.mouse.deltaX = [theEvent deltaX];
        cocoaEvent.data.mouse.deltaY = [theEvent deltaY];
        cocoaEvent.data.mouse.deltaZ = [theEvent deltaZ];
        geckoExitEvent.pluginEvent = &cocoaEvent;

        mGeckoChild->DispatchWindowEvent(geckoExitEvent);
      }
    }
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)sendMouseEnterOrExitEvent:(NSEvent*)aEvent
                            enter:(BOOL)aEnter
                             type:(nsMouseEvent::exitType)aType
{
  if (!mGeckoChild)
    return;

  NSPoint windowEventLocation = nsCocoaUtils::EventLocationForWindow(aEvent, [self window]);
  NSPoint localEventLocation = [self convertPoint:windowEventLocation fromView:nil];

  PRUint32 msg = aEnter ? NS_MOUSE_ENTER : NS_MOUSE_EXIT;
  nsMouseEvent event(true, msg, mGeckoChild, nsMouseEvent::eReal);
  event.refPoint.x = nscoord((PRInt32)localEventLocation.x);
  event.refPoint.y = nscoord((PRInt32)localEventLocation.y);

  // Create event for use by plugins.
  // This is going to our child view so we don't need to look up the destination
  // event type.
#ifndef NP_NO_CARBON  
  EventRecord carbonEvent;
#endif
  NPCocoaEvent cocoaEvent;
  if (mIsPluginView) {
#ifndef NP_NO_CARBON  
    if (mPluginEventModel == NPEventModelCarbon) {
      carbonEvent.what = NPEventType_AdjustCursorEvent;
      carbonEvent.message = 0;
      carbonEvent.when = ::TickCount();
      ::GetGlobalMouse(&carbonEvent.where);
      carbonEvent.modifiers = ::GetCurrentKeyModifiers();
      event.pluginEvent = &carbonEvent;
    }
#endif
    if (mPluginEventModel == NPEventModelCocoa) {
      nsCocoaUtils::InitNPCocoaEvent(&cocoaEvent);
      cocoaEvent.type = ((msg == NS_MOUSE_ENTER) ? NPCocoaEventMouseEntered : NPCocoaEventMouseExited);
      cocoaEvent.data.mouse.modifierFlags = [aEvent modifierFlags];
      cocoaEvent.data.mouse.pluginX = 5;
      cocoaEvent.data.mouse.pluginY = 5;
      cocoaEvent.data.mouse.buttonNumber = [aEvent buttonNumber];
      cocoaEvent.data.mouse.deltaX = [aEvent deltaX];
      cocoaEvent.data.mouse.deltaY = [aEvent deltaY];
      cocoaEvent.data.mouse.deltaZ = [aEvent deltaZ];
      event.pluginEvent = &cocoaEvent;
    }
  }

  event.exit = aType;

  nsEventStatus status; // ignored
  mGeckoChild->DispatchEvent(&event, status);
}

- (void)handleMouseMoved:(NSEvent*)theEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!mGeckoChild)
    return;

  nsMouseEvent geckoEvent(true, NS_MOUSE_MOVE, mGeckoChild, nsMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];

  // Create event for use by plugins.
  // This is going to our child view so we don't need to look up the destination
  // event type.
#ifndef NP_NO_CARBON
  EventRecord carbonEvent;
#endif
  NPCocoaEvent cocoaEvent;
  if (mIsPluginView) {
#ifndef NP_NO_CARBON
    if (mPluginEventModel == NPEventModelCarbon) {
      carbonEvent.what = NPEventType_AdjustCursorEvent;
      carbonEvent.message = 0;
      carbonEvent.when = ::TickCount();
      ::GetGlobalMouse(&carbonEvent.where);
      carbonEvent.modifiers = ::GetCurrentKeyModifiers();
      geckoEvent.pluginEvent = &carbonEvent;
    }
#endif
    if (mPluginEventModel == NPEventModelCocoa) {
      nsCocoaUtils::InitNPCocoaEvent(&cocoaEvent);
      NSPoint point = [self convertPoint:[theEvent locationInWindow] fromView:nil];
      cocoaEvent.type = NPCocoaEventMouseMoved;
      cocoaEvent.data.mouse.modifierFlags = [theEvent modifierFlags];
      cocoaEvent.data.mouse.pluginX = point.x;
      cocoaEvent.data.mouse.pluginY = point.y;
      cocoaEvent.data.mouse.buttonNumber = [theEvent buttonNumber];
      cocoaEvent.data.mouse.clickCount = [theEvent clickCount];
      cocoaEvent.data.mouse.deltaX = [theEvent deltaX];
      cocoaEvent.data.mouse.deltaY = [theEvent deltaY];
      cocoaEvent.data.mouse.deltaZ = [theEvent deltaZ];
      geckoEvent.pluginEvent = &cocoaEvent;
    }
  }
  mGeckoChild->DispatchWindowEvent(geckoEvent);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)mouseDragged:(NSEvent*)theEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!mGeckoChild)
    return;

  gLastDragView = self;

#ifndef NP_NO_CARBON
  EventRecord carbonEvent;
#endif // ifndef NP_NO_CARBON
  NPCocoaEvent cocoaEvent;

  nsMouseEvent geckoEvent(true, NS_MOUSE_MOVE, mGeckoChild, nsMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];

  // create event for use by plugins
  if (mIsPluginView) {
#ifndef NP_NO_CARBON
    if (mPluginEventModel == NPEventModelCarbon) {
      carbonEvent.what = NPEventType_AdjustCursorEvent;
      carbonEvent.message = 0;
      carbonEvent.when = ::TickCount();
      ::GetGlobalMouse(&carbonEvent.where);
      carbonEvent.modifiers = btnState | ::GetCurrentKeyModifiers();
      geckoEvent.pluginEvent = &carbonEvent;
    }
#endif
    if (mPluginEventModel == NPEventModelCocoa) {
      nsCocoaUtils::InitNPCocoaEvent(&cocoaEvent);
      NSPoint point = [self convertPoint:[theEvent locationInWindow] fromView:nil];
      cocoaEvent.type = NPCocoaEventMouseDragged;
      cocoaEvent.data.mouse.modifierFlags = [theEvent modifierFlags];
      cocoaEvent.data.mouse.pluginX = point.x;
      cocoaEvent.data.mouse.pluginY = point.y;
      cocoaEvent.data.mouse.buttonNumber = [theEvent buttonNumber];
      cocoaEvent.data.mouse.clickCount = [theEvent clickCount];
      cocoaEvent.data.mouse.deltaX = [theEvent deltaX];
      cocoaEvent.data.mouse.deltaY = [theEvent deltaY];
      cocoaEvent.data.mouse.deltaZ = [theEvent deltaZ];
      geckoEvent.pluginEvent = &cocoaEvent;
    }
  }

  mGeckoChild->DispatchWindowEvent(geckoEvent);

  // Note, sending the above event might have destroyed our widget since we didn't retain.
  // Fine so long as we don't access any local variables from here on.
  gLastDragView = nil;

  // XXX maybe call markedTextSelectionChanged:client: here?

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)rightMouseDown:(NSEvent *)theEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  [self maybeRollup:theEvent];
  if (!mGeckoChild)
    return;

  // The right mouse went down, fire off a right mouse down event to gecko
  nsMouseEvent geckoEvent(true, NS_MOUSE_BUTTON_DOWN, mGeckoChild, nsMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  geckoEvent.button = nsMouseEvent::eRightButton;
  geckoEvent.clickCount = [theEvent clickCount];

  // create event for use by plugins
#ifndef NP_NO_CARBON
  EventRecord carbonEvent;
  if (mPluginEventModel == NPEventModelCarbon) {
    carbonEvent.what = mouseDown;
    carbonEvent.message = 0;
    carbonEvent.when = ::TickCount();
    ::GetGlobalMouse(&carbonEvent.where);
    carbonEvent.modifiers = controlKey;  // fake a context menu click
    geckoEvent.pluginEvent = &carbonEvent;    
  }
#endif
  NPCocoaEvent cocoaEvent;
  if (mPluginEventModel == NPEventModelCocoa) {
    nsCocoaUtils::InitNPCocoaEvent(&cocoaEvent);
    NSPoint point = [self convertPoint:[theEvent locationInWindow] fromView:nil];
    cocoaEvent.type = NPCocoaEventMouseDown;
    cocoaEvent.data.mouse.modifierFlags = [theEvent modifierFlags];
    cocoaEvent.data.mouse.pluginX = point.x;
    cocoaEvent.data.mouse.pluginY = point.y;
    cocoaEvent.data.mouse.buttonNumber = [theEvent buttonNumber];
    cocoaEvent.data.mouse.clickCount = [theEvent clickCount];
    cocoaEvent.data.mouse.deltaX = [theEvent deltaX];
    cocoaEvent.data.mouse.deltaY = [theEvent deltaY];
    cocoaEvent.data.mouse.deltaZ = [theEvent deltaZ];
    geckoEvent.pluginEvent = &cocoaEvent;
  }

  mGeckoChild->DispatchWindowEvent(geckoEvent);
  if (!mGeckoChild)
    return;

  // Let the superclass do the context menu stuff.
  [super rightMouseDown:theEvent];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)rightMouseUp:(NSEvent *)theEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!mGeckoChild)
    return;

#ifndef NP_NO_CARBON
  EventRecord carbonEvent;
#endif // ifndef NP_NO_CARBON
  NPCocoaEvent cocoaEvent;

  nsMouseEvent geckoEvent(true, NS_MOUSE_BUTTON_UP, mGeckoChild, nsMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  geckoEvent.button = nsMouseEvent::eRightButton;
  geckoEvent.clickCount = [theEvent clickCount];

  // create event for use by plugins
  if (mIsPluginView) {
#ifndef NP_NO_CARBON
    if (mPluginEventModel == NPEventModelCarbon) {
      carbonEvent.what = mouseUp;
      carbonEvent.message = 0;
      carbonEvent.when = ::TickCount();
      ::GetGlobalMouse(&carbonEvent.where);
      carbonEvent.modifiers = controlKey;  // fake a context menu click
      geckoEvent.pluginEvent = &carbonEvent;
    }
#endif
    if (mPluginEventModel == NPEventModelCocoa) {
      nsCocoaUtils::InitNPCocoaEvent(&cocoaEvent);
      NSPoint point = [self convertPoint:[theEvent locationInWindow] fromView:nil];
      cocoaEvent.type = NPCocoaEventMouseUp;
      cocoaEvent.data.mouse.modifierFlags = [theEvent modifierFlags];
      cocoaEvent.data.mouse.pluginX = point.x;
      cocoaEvent.data.mouse.pluginY = point.y;
      cocoaEvent.data.mouse.buttonNumber = [theEvent buttonNumber];
      cocoaEvent.data.mouse.clickCount = [theEvent clickCount];
      cocoaEvent.data.mouse.deltaX = [theEvent deltaX];
      cocoaEvent.data.mouse.deltaY = [theEvent deltaY];
      cocoaEvent.data.mouse.deltaZ = [theEvent deltaZ];
      geckoEvent.pluginEvent = &cocoaEvent;
    }
  }

  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  mGeckoChild->DispatchWindowEvent(geckoEvent);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)rightMouseDragged:(NSEvent*)theEvent
{
  if (!mGeckoChild)
    return;

  nsMouseEvent geckoEvent(true, NS_MOUSE_MOVE, mGeckoChild, nsMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  geckoEvent.button = nsMouseEvent::eRightButton;

  // send event into Gecko by going directly to the
  // the widget.
  mGeckoChild->DispatchWindowEvent(geckoEvent);
}

- (void)otherMouseDown:(NSEvent *)theEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  if ([self maybeRollup:theEvent] ||
      !ChildViewMouseTracker::WindowAcceptsEvent([self window], theEvent, self))
    return;

  if (!mGeckoChild)
    return;

  nsMouseEvent geckoEvent(true, NS_MOUSE_BUTTON_DOWN, mGeckoChild, nsMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  geckoEvent.button = nsMouseEvent::eMiddleButton;
  geckoEvent.clickCount = [theEvent clickCount];

  mGeckoChild->DispatchWindowEvent(geckoEvent);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)otherMouseUp:(NSEvent *)theEvent
{
  if (!mGeckoChild)
    return;

  nsMouseEvent geckoEvent(true, NS_MOUSE_BUTTON_UP, mGeckoChild, nsMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  geckoEvent.button = nsMouseEvent::eMiddleButton;

  mGeckoChild->DispatchWindowEvent(geckoEvent);
}

- (void)otherMouseDragged:(NSEvent*)theEvent
{
  if (!mGeckoChild)
    return;

  nsMouseEvent geckoEvent(true, NS_MOUSE_MOVE, mGeckoChild, nsMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  geckoEvent.button = nsMouseEvent::eMiddleButton;

  // send event into Gecko by going directly to the
  // the widget.
  mGeckoChild->DispatchWindowEvent(geckoEvent);
}

// Handle an NSScrollWheel event for a single axis only.
-(void)scrollWheel:(NSEvent*)theEvent forAxis:(enum nsMouseScrollEvent::nsMouseScrollFlags)inAxis
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!mGeckoChild)
    return;

  float scrollDelta = 0;
  float scrollDeltaPixels = 0;
  bool checkPixels =
    Preferences::GetBool("mousewheel.enable_pixel_scrolling", true);

  // Calling deviceDeltaX or deviceDeltaY on theEvent will trigger a Cocoa
  // assertion and an Objective-C NSInternalInconsistencyException if the
  // underlying "Carbon" event doesn't contain pixel scrolling information.
  // For these events, carbonEventKind is kEventMouseWheelMoved instead of
  // kEventMouseScroll.
  if (checkPixels) {
    EventRef theCarbonEvent = [theEvent _eventRef];
    UInt32 carbonEventKind = theCarbonEvent ? ::GetEventKind(theCarbonEvent) : 0;
    if (carbonEventKind != kEventMouseScroll)
      checkPixels = false;
  }

  // Some scrolling devices supports pixel scrolling, e.g. a Macbook
  // touchpad or a Mighty Mouse. On those devices, [theEvent deviceDeltaX/Y]
  // contains the amount of pixels to scroll. Since Lion this has changed 
  // to [theEvent scrollingDeltaX/Y].
  if (inAxis & nsMouseScrollEvent::kIsVertical) {
    scrollDelta       = -[theEvent deltaY];
    if (checkPixels && (scrollDelta == 0 || scrollDelta != floor(scrollDelta))) {
      if ([theEvent respondsToSelector:@selector(scrollingDeltaY)]) {
        scrollDeltaPixels = -[theEvent scrollingDeltaY];
      } else {
        scrollDeltaPixels = -[theEvent deviceDeltaY];
      }
    }
  } else if (inAxis & nsMouseScrollEvent::kIsHorizontal) {
    scrollDelta       = -[theEvent deltaX];
    if (checkPixels && (scrollDelta == 0 || scrollDelta != floor(scrollDelta))) {
      if ([theEvent respondsToSelector:@selector(scrollingDeltaX)]) {
        scrollDeltaPixels = -[theEvent scrollingDeltaX];
      } else {
        scrollDeltaPixels = -[theEvent deviceDeltaX];
      }
    }
  } else {
    return; // caller screwed up
  }

  BOOL hasPixels = (scrollDeltaPixels != 0);

  if (!hasPixels && scrollDelta == 0)
    // No sense in firing off a Gecko event.
     return;

  BOOL isMomentumScroll = nsCocoaUtils::IsMomentumScrollEvent(theEvent);

  if (scrollDelta != 0) {
    // Send the line scroll event.
    nsMouseScrollEvent geckoEvent(true, NS_MOUSE_SCROLL, mGeckoChild);
    [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
    geckoEvent.scrollFlags |= inAxis;

    if (hasPixels)
      geckoEvent.scrollFlags |= nsMouseScrollEvent::kHasPixels;

    if (isMomentumScroll)
      geckoEvent.scrollFlags |= nsMouseScrollEvent::kIsMomentum;

    // Gecko only understands how to scroll by an integer value. Using floor
    // and ceil is better than truncating the fraction, especially when
    // |delta| < 1.
    if (scrollDelta < 0)
      geckoEvent.delta = (PRInt32)floorf(scrollDelta);
    else
      geckoEvent.delta = (PRInt32)ceilf(scrollDelta);

    NPCocoaEvent cocoaEvent;
    if (mPluginEventModel == NPEventModelCocoa) {
      nsCocoaUtils::InitNPCocoaEvent(&cocoaEvent);
      NSPoint point = [self convertPoint:[theEvent locationInWindow] fromView:nil];
      cocoaEvent.type = NPCocoaEventScrollWheel;
      cocoaEvent.data.mouse.modifierFlags = [theEvent modifierFlags];
      cocoaEvent.data.mouse.pluginX = point.x;
      cocoaEvent.data.mouse.pluginY = point.y;
      cocoaEvent.data.mouse.buttonNumber = [theEvent buttonNumber];
      cocoaEvent.data.mouse.clickCount = 0;
      if (inAxis & nsMouseScrollEvent::kIsHorizontal)
        cocoaEvent.data.mouse.deltaX = [theEvent deltaX];
      else
        cocoaEvent.data.mouse.deltaX = 0.0;
      if (inAxis & nsMouseScrollEvent::kIsVertical)
        cocoaEvent.data.mouse.deltaY = [theEvent deltaY];
      else
        cocoaEvent.data.mouse.deltaY = 0.0;
      cocoaEvent.data.mouse.deltaZ = 0.0;
      geckoEvent.pluginEvent = &cocoaEvent;
    }

    nsAutoRetainCocoaObject kungFuDeathGrip(self);
    mGeckoChild->DispatchWindowEvent(geckoEvent);
    if (!mGeckoChild)
      return;

#ifndef NP_NO_CARBON
    // dispatch scroll wheel carbon event for plugins
    if (mPluginEventModel == NPEventModelCarbon) {
      EventRef theEvent;
      OSStatus err = ::CreateEvent(NULL,
                                   kEventClassMouse,
                                   kEventMouseWheelMoved,
                                   TicksToEventTime(TickCount()),
                                   kEventAttributeUserEvent,
                                   &theEvent);
      if (err == noErr) {
        EventMouseWheelAxis axis;
        if (inAxis & nsMouseScrollEvent::kIsVertical)
          axis = kEventMouseWheelAxisY;
        else if (inAxis & nsMouseScrollEvent::kIsHorizontal)
          axis = kEventMouseWheelAxisX;
        
        SetEventParameter(theEvent,
                          kEventParamMouseWheelAxis,
                          typeMouseWheelAxis,
                          sizeof(EventMouseWheelAxis),
                          &axis);
        
        SInt32 delta = (SInt32)-geckoEvent.delta;
        SetEventParameter(theEvent,
                          kEventParamMouseWheelDelta,
                          typeLongInteger,
                          sizeof(SInt32),
                          &delta);
        
        Point mouseLoc;
        ::GetGlobalMouse(&mouseLoc);
        SetEventParameter(theEvent,
                          kEventParamMouseLocation,
                          typeQDPoint,
                          sizeof(Point),
                          &mouseLoc);
        
        ::SendEventToEventTarget(theEvent, GetWindowEventTarget((WindowRef)[[self window] windowRef]));
        ReleaseEvent(theEvent);
      }
    }
#endif
  }

  if (hasPixels) {
    // Send the pixel scroll event.
    nsMouseScrollEvent geckoEvent(true, NS_MOUSE_PIXEL_SCROLL, mGeckoChild);
    [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
    geckoEvent.scrollFlags |= inAxis;
    if (isMomentumScroll)
      geckoEvent.scrollFlags |= nsMouseScrollEvent::kIsMomentum;
    geckoEvent.delta = NSToIntRound(scrollDeltaPixels);
    nsAutoRetainCocoaObject kungFuDeathGrip(self);
    mGeckoChild->DispatchWindowEvent(geckoEvent);
#ifdef __LP64__
    // scrollOverflow tells us when the user has tried to scroll past the edge
    // of a page (in those cases it's non-zero).  Gecko only sets it when
    // processing NS_MOUSE_PIXEL_SCROLL events (not MS_MOUSE_SCROLL events).
    // It only means left/right overflow when Gecko is processing a horizontal
    // event.
    if (inAxis & nsMouseScrollEvent::kIsHorizontal) {
      [self maybeTrackScrollEventAsSwipe:theEvent
                          scrollOverflow:geckoEvent.scrollOverflow];
    }
#endif // #ifdef __LP64__
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

-(void)scrollWheel:(NSEvent*)theEvent
{
  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  ChildViewMouseTracker::MouseScrolled(theEvent);

  if ([self maybeRollup:theEvent])
    return;

  // It's possible for a single NSScrollWheel event to carry both useful
  // deltaX and deltaY, for example, when the "wheel" is a trackpad.
  // NSMouseScrollEvent can only carry one axis at a time, so the system
  // event will be split into two Gecko events if necessary.
  [self scrollWheel:theEvent forAxis:nsMouseScrollEvent::kIsVertical];
  [self scrollWheel:theEvent forAxis:nsMouseScrollEvent::kIsHorizontal];
}

-(NSMenu*)menuForEvent:(NSEvent*)theEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NIL;

  if (!mGeckoChild || [self isPluginView])
    return nil;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  [self maybeRollup:theEvent];
  if (!mGeckoChild)
    return nil;

  // Cocoa doesn't always dispatch a mouseDown: for a control-click event,
  // depends on what we return from menuForEvent:. Gecko always expects one
  // and expects the mouse down event before the context menu event, so
  // get that event sent first if this is a left mouse click.
  if ([theEvent type] == NSLeftMouseDown) {
    [self mouseDown:theEvent];
    if (!mGeckoChild)
      return nil;
  }

  nsMouseEvent geckoEvent(true, NS_CONTEXTMENU, mGeckoChild, nsMouseEvent::eReal);
  [self convertCocoaMouseEvent:theEvent toGeckoEvent:&geckoEvent];
  geckoEvent.button = nsMouseEvent::eRightButton;
  mGeckoChild->DispatchWindowEvent(geckoEvent);
  if (!mGeckoChild)
    return nil;

  [self maybeInitContextMenuTracking];

  // Go up our view chain to fetch the correct menu to return.
  return [self contextMenu];

  NS_OBJC_END_TRY_ABORT_BLOCK_NIL;
}

- (NSMenu*)contextMenu
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NIL;

  NSView* superView = [self superview];
  if ([superView respondsToSelector:@selector(contextMenu)])
    return [(NSView<mozView>*)superView contextMenu];

  return nil;

  NS_OBJC_END_TRY_ABORT_BLOCK_NIL;
}

- (void) convertCocoaMouseEvent:(NSEvent*)aMouseEvent toGeckoEvent:(nsInputEvent*)outGeckoEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  NS_ASSERTION(outGeckoEvent, "convertCocoaMouseEvent:toGeckoEvent: requires non-null aoutGeckoEvent");
  if (!outGeckoEvent)
    return;

  nsCocoaUtils::InitInputEvent(*outGeckoEvent, aMouseEvent);

  // convert point to view coordinate system
  NSPoint locationInWindow = nsCocoaUtils::EventLocationForWindow(aMouseEvent, [self window]);
  NSPoint localPoint = [self convertPoint:locationInWindow fromView:nil];
  outGeckoEvent->refPoint.x = static_cast<nscoord>(localPoint.x);
  outGeckoEvent->refPoint.y = static_cast<nscoord>(localPoint.y);

  nsMouseEvent_base* mouseEvent =
    static_cast<nsMouseEvent_base*>(outGeckoEvent);
  mouseEvent->buttons = 0;
  NSUInteger mouseButtons =
    nsCocoaFeatures::OnSnowLeopardOrLater() ? [NSEvent pressedMouseButtons] : 0;

  if (mouseButtons & 0x01) {
    mouseEvent->buttons |= nsMouseEvent::eLeftButtonFlag;
  }
  if (mouseButtons & 0x02) {
    mouseEvent->buttons |= nsMouseEvent::eRightButtonFlag;
  }
  if (mouseButtons & 0x04) {
    mouseEvent->buttons |= nsMouseEvent::eMiddleButtonFlag;
  }
  if (mouseButtons & 0x08) {
    mouseEvent->buttons |= nsMouseEvent::e4thButtonFlag;
  }
  if (mouseButtons & 0x10) {
    mouseEvent->buttons |= nsMouseEvent::e5thButtonFlag;
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}


#pragma mark -
// NSTextInput implementation

- (void)insertText:(id)insertString
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  NS_ENSURE_TRUE(mGeckoChild, );

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  NSAttributedString* attrStr;
  if ([insertString isKindOfClass:[NSAttributedString class]]) {
    attrStr = static_cast<NSAttributedString*>(insertString);
  } else {
    attrStr =
      [[[NSAttributedString alloc] initWithString:insertString] autorelease];
  }

  mTextInputHandler->InsertText(attrStr);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)insertNewline:(id)sender
{
  [self insertText:@"\n"];
}

- (void) doCommandBySelector:(SEL)aSelector
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!mGeckoChild || !mTextInputHandler) {
    return;
  }

  const char* sel = reinterpret_cast<const char*>(aSelector);
  if (!mTextInputHandler->DoCommandBySelector(sel)) {
    [super doCommandBySelector:aSelector];
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void) setMarkedText:(id)aString selectedRange:(NSRange)selRange
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  NS_ENSURE_TRUE(mTextInputHandler, );

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  NSAttributedString* attrStr;
  if ([aString isKindOfClass:[NSAttributedString class]]) {
    attrStr = static_cast<NSAttributedString*>(aString);
  } else {
    attrStr = [[[NSAttributedString alloc] initWithString:aString] autorelease];
  }

  mTextInputHandler->SetMarkedText(attrStr, selRange);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void) unmarkText
{
  NS_ENSURE_TRUE(mTextInputHandler, );
  mTextInputHandler->CommitIMEComposition();
}

- (BOOL) hasMarkedText
{
  NS_ENSURE_TRUE(mTextInputHandler, NO);
  return mTextInputHandler->HasMarkedText();
}

- (NSInteger) conversationIdentifier
{
  NS_ENSURE_TRUE(mTextInputHandler, reinterpret_cast<NSInteger>(self));
  return mTextInputHandler->ConversationIdentifier();
}

- (NSAttributedString *) attributedSubstringFromRange:(NSRange)theRange
{
  NS_ENSURE_TRUE(mTextInputHandler, nil);
  return mTextInputHandler->GetAttributedSubstringFromRange(theRange);
}

- (NSRange) markedRange
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN;

  NS_ENSURE_TRUE(mTextInputHandler, NSMakeRange(NSNotFound, 0));
  return mTextInputHandler->MarkedRange();

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(NSMakeRange(0, 0));
}

- (NSRange) selectedRange
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN;

  NS_ENSURE_TRUE(mTextInputHandler, NSMakeRange(NSNotFound, 0));
  return mTextInputHandler->SelectedRange();

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(NSMakeRange(0, 0));
}

- (NSRect) firstRectForCharacterRange:(NSRange)theRange
{
  NSRect rect;
  NS_ENSURE_TRUE(mTextInputHandler, rect);
  return mTextInputHandler->FirstRectForCharacterRange(theRange);
}

- (NSUInteger)characterIndexForPoint:(NSPoint)thePoint
{
  NS_ENSURE_TRUE(mTextInputHandler, 0);
  return mTextInputHandler->CharacterIndexForPoint(thePoint);
}

- (NSArray*) validAttributesForMarkedText
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NIL;

  NS_ENSURE_TRUE(mTextInputHandler, [NSArray array]);
  return mTextInputHandler->GetValidAttributesForMarkedText();

  NS_OBJC_END_TRY_ABORT_BLOCK_NIL;
}

#pragma mark -

#ifdef NP_NO_CARBON
- (NSTextInputContext *)inputContext
{
  if (mIsPluginView && mPluginEventModel == NPEventModelCocoa)
    return [[ComplexTextInputPanel sharedComplexTextInputPanel] inputContext];
  else
    return [super inputContext];
}
#endif

// This is a private API that Cocoa uses.
// Cocoa will call this after the menu system returns "NO" for "performKeyEquivalent:".
// We want all they key events we can get so just return YES. In particular, this fixes
// ctrl-tab - we don't get a "keyDown:" call for that without this.
- (BOOL)_wantsKeyDownForEvent:(NSEvent*)event
{
  return YES;
}

- (void)keyDown:(NSEvent*)theEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (mGeckoChild && mTextInputHandler && mIsPluginView) {
    mTextInputHandler->HandleKeyDownEventForPlugin(theEvent);
    return;
  }

  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  bool handled = false;
  if (mGeckoChild && mTextInputHandler) {
    handled = mTextInputHandler->HandleKeyDownEvent(theEvent);
  }

  // We always allow keyboard events to propagate to keyDown: but if they are not
  // handled we give special Application menu items a chance to act.
  if (!handled && sApplicationMenu) {
    [sApplicationMenu performKeyEquivalent:theEvent];
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)keyUp:(NSEvent*)theEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  NS_ENSURE_TRUE(mGeckoChild, );

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  if (mIsPluginView) {
    mTextInputHandler->HandleKeyUpEventForPlugin(theEvent);
    return;
  }

  mTextInputHandler->HandleKeyUpEvent(theEvent);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)flagsChanged:(NSEvent*)theEvent
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  NS_ENSURE_TRUE(mGeckoChild, );

  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  mTextInputHandler->HandleFlagsChanged(theEvent);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (BOOL) isFirstResponder
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN;

  NSResponder* resp = [[self window] firstResponder];
  return (resp == (NSResponder*)self);

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(NO);
}

- (BOOL)isDragInProgress
{
  if (!mDragService)
    return NO;

  nsCOMPtr<nsIDragSession> dragSession;
  mDragService->GetCurrentSession(getter_AddRefs(dragSession));
  return dragSession != nsnull;
}

- (BOOL)inactiveWindowAcceptsMouseEvent:(NSEvent*)aEvent
{
  // If we're being destroyed assume the default -- return YES.
  if (!mGeckoChild)
    return YES;

  nsMouseEvent geckoEvent(true, NS_MOUSE_ACTIVATE, mGeckoChild, nsMouseEvent::eReal);
  [self convertCocoaMouseEvent:aEvent toGeckoEvent:&geckoEvent];
  return !mGeckoChild->DispatchWindowEvent(geckoEvent);
}

// Don't focus a plugin if the user has clicked on a DOM element above it.
// In this case the user has actually clicked on the plugin's ChildView
// (underneath the non-plugin DOM element).  But we shouldn't allow the
// ChildView to be focused.  See bug 627649.
- (BOOL)currentEventShouldFocusPlugin
{
  if (!mGeckoChild)
    return NO;

  NSEvent* currentEvent = [NSApp currentEvent];
  if ([currentEvent type] != NSLeftMouseDown)
    return YES;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  NSPoint eventLoc = nsCocoaUtils::ScreenLocationForEvent(currentEvent);
  eventLoc.y = nsCocoaUtils::FlippedScreenY(eventLoc.y);
  nsIntPoint widgetLoc(NSToIntRound(eventLoc.x), NSToIntRound(eventLoc.y));
  widgetLoc -= mGeckoChild->WidgetToScreenOffset();

  nsQueryContentEvent hitTest(true, NS_QUERY_DOM_WIDGET_HITTEST, mGeckoChild);
  hitTest.InitForQueryDOMWidgetHittest(widgetLoc);
  // This might destroy our widget (and null out mGeckoChild).
  mGeckoChild->DispatchWindowEvent(hitTest);
  if (!mGeckoChild)
    return NO;
  if (hitTest.mSucceeded && !hitTest.mReply.mWidgetIsHit)
    return NO;

  return YES;
}

// Don't focus a plugin if we're in a left click-through that will fail (see
// [ChildView isInFailingLeftClickThrough] above).
- (BOOL)shouldFocusPlugin:(BOOL)getFocus
{
  if (!mGeckoChild)
    return NO;

  nsCocoaWindow* windowWidget = mGeckoChild->GetXULWindowWidget();
  if (windowWidget && !windowWidget->ShouldFocusPlugin())
    return NO;

  if (getFocus && ![self currentEventShouldFocusPlugin])
    return NO;

  return YES;
}

// Returns NO if the plugin shouldn't be focused/unfocused.
- (BOOL)updatePluginFocusStatus:(BOOL)getFocus
{
  if (!mGeckoChild)
    return NO;

  if (![self shouldFocusPlugin:getFocus])
    return NO;

  if (mPluginEventModel == NPEventModelCocoa) {
    nsPluginEvent pluginEvent(true, NS_PLUGIN_FOCUS_EVENT, mGeckoChild);
    NPCocoaEvent cocoaEvent;
    nsCocoaUtils::InitNPCocoaEvent(&cocoaEvent);
    cocoaEvent.type = NPCocoaEventFocusChanged;
    cocoaEvent.data.focus.hasFocus = getFocus;
    nsCocoaUtils::InitPluginEvent(pluginEvent, cocoaEvent);
    mGeckoChild->DispatchWindowEvent(pluginEvent);

    if (getFocus)
      [self sendFocusEvent:NS_PLUGIN_FOCUS];
  }

  return YES;
}

// We must always call through to our superclass, even when mGeckoChild is
// nil -- otherwise the keyboard focus can end up in the wrong NSView.
- (BOOL)becomeFirstResponder
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN;

  if (mIsPluginView) {
    if (![self updatePluginFocusStatus:YES])
      return NO;
  }

  return [super becomeFirstResponder];

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(YES);
}

// We must always call through to our superclass, even when mGeckoChild is
// nil -- otherwise the keyboard focus can end up in the wrong NSView.
- (BOOL)resignFirstResponder
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN;

  if (mIsPluginView) {
    if (![self updatePluginFocusStatus:NO])
      return NO;
  }

  return [super resignFirstResponder];

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(YES);
}

- (void)viewsWindowDidBecomeKey
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (!mGeckoChild)
    return;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  // check to see if the window implements the mozWindow protocol. This
  // allows embedders to avoid re-entrant calls to -makeKeyAndOrderFront,
  // which can happen because these activate calls propagate out
  // to the embedder via nsIEmbeddingSiteWindow::SetFocus().
  BOOL isMozWindow = [[self window] respondsToSelector:@selector(setSuppressMakeKeyFront:)];
  if (isMozWindow)
    [[self window] setSuppressMakeKeyFront:YES];

  [self sendFocusEvent:NS_ACTIVATE];

  if (isMozWindow)
    [[self window] setSuppressMakeKeyFront:NO];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

- (void)viewsWindowDidResignKey
{
  if (!mGeckoChild)
    return;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  [self sendFocusEvent:NS_DEACTIVATE];
}

// If the call to removeFromSuperview isn't delayed from nsChildView::
// TearDownView(), the NSView hierarchy might get changed during calls to
// [ChildView drawRect:], which leads to "beyond bounds" exceptions in
// NSCFArray.  For more info see bmo bug 373122.  Apple's docs claim that
// removeFromSuperviewWithoutNeedingDisplay "can be safely invoked during
// display" (whatever "display" means).  But it's _not_ true that it can be
// safely invoked during calls to [NSView drawRect:].  We use
// removeFromSuperview here because there's no longer any danger of being
// "invoked during display", and because doing do clears up bmo bug 384343.
- (void)delayedTearDown
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  [self removeFromSuperview];
  [self release];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

#pragma mark -

// drag'n'drop stuff
#define kDragServiceContractID "@mozilla.org/widget/dragservice;1"

- (NSDragOperation)dragOperationForSession:(nsIDragSession*)aDragSession
{
  PRUint32 dragAction;
  aDragSession->GetDragAction(&dragAction);
  if (nsIDragService::DRAGDROP_ACTION_LINK & dragAction)
    return NSDragOperationLink;
  if (nsIDragService::DRAGDROP_ACTION_COPY & dragAction)
    return NSDragOperationCopy;
  if (nsIDragService::DRAGDROP_ACTION_MOVE & dragAction)
    return NSDragOperationGeneric;
  return NSDragOperationNone;
}

// This is a utility function used by NSView drag event methods
// to send events. It contains all of the logic needed for Gecko
// dragging to work. Returns the appropriate cocoa drag operation code.
- (NSDragOperation)doDragAction:(PRUint32)aMessage sender:(id)aSender
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN;

  if (!mGeckoChild)
    return NSDragOperationNone;

  PR_LOG(sCocoaLog, PR_LOG_ALWAYS, ("ChildView doDragAction: entered\n"));

  if (!mDragService) {
    CallGetService(kDragServiceContractID, &mDragService);
    NS_ASSERTION(mDragService, "Couldn't get a drag service - big problem!");
    if (!mDragService)
      return NSDragOperationNone;
  }

  if (aMessage == NS_DRAGDROP_ENTER)
    mDragService->StartDragSession();

  nsCOMPtr<nsIDragSession> dragSession;
  mDragService->GetCurrentSession(getter_AddRefs(dragSession));
  if (dragSession) {
    if (aMessage == NS_DRAGDROP_OVER) {
      // fire the drag event at the source. Just ignore whether it was
      // cancelled or not as there isn't actually a means to stop the drag
      mDragService->FireDragEventAtSource(NS_DRAGDROP_DRAG);
      dragSession->SetCanDrop(false);
    }
    else if (aMessage == NS_DRAGDROP_DROP) {
      // We make the assumption that the dragOver handlers have correctly set
      // the |canDrop| property of the Drag Session.
      bool canDrop = false;
      if (!NS_SUCCEEDED(dragSession->GetCanDrop(&canDrop)) || !canDrop) {
        [self doDragAction:NS_DRAGDROP_EXIT sender:aSender];

        nsCOMPtr<nsIDOMNode> sourceNode;
        dragSession->GetSourceNode(getter_AddRefs(sourceNode));
        if (!sourceNode) {
          mDragService->EndDragSession(false);
        }
        return NSDragOperationNone;
      }
    }
    
    unsigned int modifierFlags = [[NSApp currentEvent] modifierFlags];
    PRUint32 action = nsIDragService::DRAGDROP_ACTION_MOVE;
    // force copy = option, alias = cmd-option, default is move
    if (modifierFlags & NSAlternateKeyMask) {
      if (modifierFlags & NSCommandKeyMask)
        action = nsIDragService::DRAGDROP_ACTION_LINK;
      else
        action = nsIDragService::DRAGDROP_ACTION_COPY;
    }
    dragSession->SetDragAction(action);
  }

  // set up gecko event
  nsDragEvent geckoEvent(true, aMessage, mGeckoChild);
  nsCocoaUtils::InitInputEvent(geckoEvent, [NSApp currentEvent]);

  // Use our own coordinates in the gecko event.
  // Convert event from gecko global coords to gecko view coords.
  NSPoint localPoint = [self convertPoint:[aSender draggingLocation] fromView:nil];
  geckoEvent.refPoint.x = static_cast<nscoord>(localPoint.x);
  geckoEvent.refPoint.y = static_cast<nscoord>(localPoint.y);

  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  mGeckoChild->DispatchWindowEvent(geckoEvent);
  if (!mGeckoChild)
    return NSDragOperationNone;

  if (dragSession) {
    switch (aMessage) {
      case NS_DRAGDROP_ENTER:
      case NS_DRAGDROP_OVER:
        return [self dragOperationForSession:dragSession];
      case NS_DRAGDROP_EXIT:
      case NS_DRAGDROP_DROP: {
        nsCOMPtr<nsIDOMNode> sourceNode;
        dragSession->GetSourceNode(getter_AddRefs(sourceNode));
        if (!sourceNode) {
          // We're leaving a window while doing a drag that was
          // initiated in a different app. End the drag session,
          // since we're done with it for now (until the user
          // drags back into mozilla).
          mDragService->EndDragSession(false);
        }
      }
    }
  }

  return NSDragOperationGeneric;

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(NSDragOperationNone);
}

- (NSDragOperation)draggingEntered:(id <NSDraggingInfo>)sender
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN;

  PR_LOG(sCocoaLog, PR_LOG_ALWAYS, ("ChildView draggingEntered: entered\n"));
  
  // there should never be a globalDragPboard when "draggingEntered:" is
  // called, but just in case we'll take care of it here.
  [globalDragPboard release];

  // Set the global drag pasteboard that will be used for this drag session.
  // This will be set back to nil when the drag session ends (mouse exits
  // the view or a drop happens within the view).
  globalDragPboard = [[sender draggingPasteboard] retain];

  return [self doDragAction:NS_DRAGDROP_ENTER sender:sender];

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(NSDragOperationNone);
}

- (NSDragOperation)draggingUpdated:(id <NSDraggingInfo>)sender
{
  PR_LOG(sCocoaLog, PR_LOG_ALWAYS, ("ChildView draggingUpdated: entered\n"));

  return [self doDragAction:NS_DRAGDROP_OVER sender:sender];
}

- (void)draggingExited:(id <NSDraggingInfo>)sender
{
  PR_LOG(sCocoaLog, PR_LOG_ALWAYS, ("ChildView draggingExited: entered\n"));

  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  [self doDragAction:NS_DRAGDROP_EXIT sender:sender];
  NS_IF_RELEASE(mDragService);
}

- (BOOL)performDragOperation:(id <NSDraggingInfo>)sender
{
  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  BOOL handled = [self doDragAction:NS_DRAGDROP_DROP sender:sender] != NSDragOperationNone;
  NS_IF_RELEASE(mDragService);
  return handled;
}

// NSDraggingSource
- (void)draggedImage:(NSImage *)anImage movedTo:(NSPoint)aPoint
{
  // Get the drag service if it isn't already cached. The drag service
  // isn't cached when dragging over a different application.
  nsCOMPtr<nsIDragService> dragService = mDragService;
  if (!dragService) {
    dragService = do_GetService(kDragServiceContractID);
  }

  if (dragService) {
    NSPoint pnt = [NSEvent mouseLocation];
    FlipCocoaScreenCoordinate(pnt);
    dragService->DragMoved(NSToIntRound(pnt.x), NSToIntRound(pnt.y));
  }
}

// NSDraggingSource
- (void)draggedImage:(NSImage *)anImage endedAt:(NSPoint)aPoint operation:(NSDragOperation)operation
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  gDraggedTransferables = nsnull;

  NSEvent *currentEvent = [NSApp currentEvent];
  gUserCancelledDrag = ([currentEvent type] == NSKeyDown &&
                        [currentEvent keyCode] == kVK_Escape);

  if (!mDragService) {
    CallGetService(kDragServiceContractID, &mDragService);
    NS_ASSERTION(mDragService, "Couldn't get a drag service - big problem!");
  }

  if (mDragService) {
    // set the dragend point from the current mouse location
    nsDragService* dragService = static_cast<nsDragService *>(mDragService);
    NSPoint pnt = [NSEvent mouseLocation];
    FlipCocoaScreenCoordinate(pnt);
    dragService->SetDragEndPoint(nsIntPoint(NSToIntRound(pnt.x), NSToIntRound(pnt.y)));

    // XXX: dropEffect should be updated per |operation|. 
    // As things stand though, |operation| isn't well handled within "our"
    // events, that is, when the drop happens within the window: it is set
    // either to NSDragOperationGeneric or to NSDragOperationNone.
    // For that reason, it's not yet possible to override dropEffect per the
    // given OS value, and it's also unclear what's the correct dropEffect 
    // value for NSDragOperationGeneric that is passed by other applications.
    // All that said, NSDragOperationNone is still reliable.
    if (operation == NSDragOperationNone) {
      nsCOMPtr<nsIDOMDataTransfer> dataTransfer;
      dragService->GetDataTransfer(getter_AddRefs(dataTransfer));
      if (dataTransfer)
        dataTransfer->SetDropEffectInt(nsIDragService::DRAGDROP_ACTION_NONE);
    }

    mDragService->EndDragSession(true);
    NS_RELEASE(mDragService);
  }

  [globalDragPboard release];
  globalDragPboard = nil;
  [gLastDragMouseDownEvent release];
  gLastDragMouseDownEvent = nil;

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

// NSDraggingSource
// this is just implemented so we comply with the NSDraggingSource informal protocol
- (NSDragOperation)draggingSourceOperationMaskForLocal:(BOOL)isLocal
{
  return UINT_MAX;
}

// This method is a callback typically invoked in response to a drag ending on the desktop
// or a Findow folder window; the argument passed is a path to the drop location, to be used
// in constructing a complete pathname for the file(s) we want to create as a result of
// the drag.
- (NSArray *)namesOfPromisedFilesDroppedAtDestination:(NSURL*)dropDestination
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NIL;

  nsresult rv;

  PR_LOG(sCocoaLog, PR_LOG_ALWAYS, ("ChildView namesOfPromisedFilesDroppedAtDestination: entering callback for promised files\n"));

  nsCOMPtr<nsIFile> targFile;
  NS_NewLocalFile(EmptyString(), true, getter_AddRefs(targFile));
  nsCOMPtr<nsILocalFileMac> macLocalFile = do_QueryInterface(targFile);
  if (!macLocalFile) {
    NS_ERROR("No Mac local file");
    return nil;
  }

  if (!NS_SUCCEEDED(macLocalFile->InitWithCFURL((CFURLRef)dropDestination))) {
    NS_ERROR("failed InitWithCFURL");
    return nil;
  }

  if (!gDraggedTransferables)
    return nil;

  PRUint32 transferableCount;
  rv = gDraggedTransferables->Count(&transferableCount);
  if (NS_FAILED(rv))
    return nil;

  for (PRUint32 i = 0; i < transferableCount; i++) {
    nsCOMPtr<nsISupports> genericItem;
    gDraggedTransferables->GetElementAt(i, getter_AddRefs(genericItem));
    nsCOMPtr<nsITransferable> item(do_QueryInterface(genericItem));
    if (!item) {
      NS_ERROR("no transferable");
      return nil;
    }
    item->Init(nsnull);

    item->SetTransferData(kFilePromiseDirectoryMime, macLocalFile, sizeof(nsIFile*));
    
    // now request the kFilePromiseMime data, which will invoke the data provider
    // If successful, the returned data is a reference to the resulting file.
    nsCOMPtr<nsISupports> fileDataPrimitive;
    PRUint32 dataSize = 0;
    item->GetTransferData(kFilePromiseMime, getter_AddRefs(fileDataPrimitive), &dataSize);
  }
  
  NSPasteboard* generalPboard = [NSPasteboard pasteboardWithName:NSDragPboard];
  NSData* data = [generalPboard dataForType:@"application/x-moz-file-promise-dest-filename"];
  NSString* name = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
  NSArray* rslt = [NSArray arrayWithObject:name];

  [name release];

  return rslt;

  NS_OBJC_END_TRY_ABORT_BLOCK_NIL;
}

#pragma mark -

// Support for the "Services" menu. We currently only support sending strings
// and HTML to system services.

- (id)validRequestorForSendType:(NSString *)sendType
                     returnType:(NSString *)returnType
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NIL;

  // sendType contains the type of data that the service would like this
  // application to send to it.  sendType is nil if the service is not
  // requesting any data.
  //
  // returnType contains the type of data the the service would like to
  // return to this application (e.g., to overwrite the selection).
  // returnType is nil if the service will not return any data.
  //
  // The following condition thus triggers when the service expects a string
  // or HTML from us or no data at all AND when the service will either not
  // send back any data to us or will send a string or HTML back to us.

#define IsSupportedType(typeStr) ([typeStr isEqual:NSStringPboardType] || [typeStr isEqual:NSHTMLPboardType])

  id result = nil;

  if ((!sendType || IsSupportedType(sendType)) &&
      (!returnType || IsSupportedType(returnType))) {
    if (mGeckoChild) {
      // Assume that this object will be able to handle this request.
      result = self;

      // Keep the ChildView alive during this operation.
      nsAutoRetainCocoaObject kungFuDeathGrip(self);
      
      // Determine if there is a selection (if sending to the service).
      if (sendType) {
        nsQueryContentEvent event(true, NS_QUERY_CONTENT_STATE, mGeckoChild);
        // This might destroy our widget (and null out mGeckoChild).
        mGeckoChild->DispatchWindowEvent(event);
        if (!mGeckoChild || !event.mSucceeded || !event.mReply.mHasSelection)
          result = nil;
      }

      // Determine if we can paste (if receiving data from the service).
      if (mGeckoChild && returnType) {
        nsContentCommandEvent command(true, NS_CONTENT_COMMAND_PASTE_TRANSFERABLE, mGeckoChild, true);
        // This might possibly destroy our widget (and null out mGeckoChild).
        mGeckoChild->DispatchWindowEvent(command);
        if (!mGeckoChild || !command.mSucceeded || !command.mIsEnabled)
          result = nil;
      }
    }
  }

#undef IsSupportedType

  // Give the superclass a chance if this object will not handle this request.
  if (!result)
    result = [super validRequestorForSendType:sendType returnType:returnType];

  return result;

  NS_OBJC_END_TRY_ABORT_BLOCK_NIL;
}

- (BOOL)writeSelectionToPasteboard:(NSPasteboard *)pboard
                             types:(NSArray *)types
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_RETURN;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);

  // Make sure that the service will accept strings or HTML.
  if ([types containsObject:NSStringPboardType] == NO &&
      [types containsObject:NSHTMLPboardType] == NO)
    return NO;

  // Bail out if there is no Gecko object.
  if (!mGeckoChild)
    return NO;

  // Obtain the current selection.
  nsQueryContentEvent event(true,
                            NS_QUERY_SELECTION_AS_TRANSFERABLE,
                            mGeckoChild);
  mGeckoChild->DispatchWindowEvent(event);
  if (!event.mSucceeded || !event.mReply.mTransferable)
    return NO;

  // Transform the transferable to an NSDictionary.
  NSDictionary* pasteboardOutputDict = nsClipboard::PasteboardDictFromTransferable(event.mReply.mTransferable);
  if (!pasteboardOutputDict)
    return NO;

  // Declare the pasteboard types.
  unsigned int typeCount = [pasteboardOutputDict count];
  NSMutableArray * types = [NSMutableArray arrayWithCapacity:typeCount];
  [types addObjectsFromArray:[pasteboardOutputDict allKeys]];
  [pboard declareTypes:types owner:nil];

  // Write the data to the pasteboard.
  for (unsigned int i = 0; i < typeCount; i++) {
    NSString* currentKey = [types objectAtIndex:i];
    id currentValue = [pasteboardOutputDict valueForKey:currentKey];

    if (currentKey == NSStringPboardType ||
        currentKey == kCorePboardType_url ||
        currentKey == kCorePboardType_urld ||
        currentKey == kCorePboardType_urln) {
      [pboard setString:currentValue forType:currentKey];
    } else if (currentKey == NSHTMLPboardType) {
      [pboard setString:(nsClipboard::WrapHtmlForSystemPasteboard(currentValue)) forType:currentKey];
    } else if (currentKey == NSTIFFPboardType) {
      [pboard setData:currentValue forType:currentKey];
    } else if (currentKey == NSFilesPromisePboardType) {
      [pboard setPropertyList:currentValue forType:currentKey];        
    }
  }

  return YES;

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(NO);
}

// Called if the service wants us to replace the current selection.
- (BOOL)readSelectionFromPasteboard:(NSPasteboard *)pboard
{
  nsresult rv;
  nsCOMPtr<nsITransferable> trans = do_CreateInstance("@mozilla.org/widget/transferable;1", &rv);
  if (NS_FAILED(rv))
    return NO;
  trans->Init(nsnull);

  trans->AddDataFlavor(kUnicodeMime);
  trans->AddDataFlavor(kHTMLMime);

  rv = nsClipboard::TransferableFromPasteboard(trans, pboard);
  if (NS_FAILED(rv))
    return NO;

  NS_ENSURE_TRUE(mGeckoChild, false);

  nsContentCommandEvent command(true,
                                NS_CONTENT_COMMAND_PASTE_TRANSFERABLE,
                                mGeckoChild);
  command.mTransferable = trans;
  mGeckoChild->DispatchWindowEvent(command);
  
  return command.mSucceeded && command.mIsEnabled;
}

#pragma mark -

#ifdef ACCESSIBILITY

/* Every ChildView has a corresponding mozDocAccessible object that is doing all
   the heavy lifting. The topmost ChildView corresponds to a mozRootAccessible
   object.

   All ChildView needs to do is to route all accessibility calls (from the NSAccessibility APIs)
   down to its object, pretending that they are the same.
*/
- (id<mozAccessible>)accessible
{
  if (!mGeckoChild)
    return nil;

  id<mozAccessible> nativeAccessible = nil;

  nsAutoRetainCocoaObject kungFuDeathGrip(self);
  nsCOMPtr<nsIWidget> kungFuDeathGrip2(mGeckoChild);
  nsRefPtr<Accessible> accessible = mGeckoChild->GetDocumentAccessible();
  if (!mGeckoChild)
    return nil;

  if (accessible)
    accessible->GetNativeInterface((void**)&nativeAccessible);

#ifdef DEBUG_hakan
  NSAssert(![nativeAccessible isExpired], @"native acc is expired!!!");
#endif
  
  return nativeAccessible;
}

/* Implementation of formal mozAccessible formal protocol (enabling mozViews
   to talk to mozAccessible objects in the accessibility module). */

- (BOOL)hasRepresentedView
{
  return YES;
}

- (id)representedView
{
  return self;
}

- (BOOL)isRoot
{
  return [[self accessible] isRoot];
}

#ifdef DEBUG
- (void)printHierarchy
{
  [[self accessible] printHierarchy];
}
#endif

#pragma mark -

// general

- (BOOL)accessibilityIsIgnored
{
  return [[self accessible] accessibilityIsIgnored];
}

- (id)accessibilityHitTest:(NSPoint)point
{
  return [[self accessible] accessibilityHitTest:point];
}

- (id)accessibilityFocusedUIElement
{
  return [[self accessible] accessibilityFocusedUIElement];
}

// actions

- (NSArray*)accessibilityActionNames
{
  return [[self accessible] accessibilityActionNames];
}

- (NSString*)accessibilityActionDescription:(NSString*)action
{
  return [[self accessible] accessibilityActionDescription:action];
}

- (void)accessibilityPerformAction:(NSString*)action
{
  return [[self accessible] accessibilityPerformAction:action];
}

// attributes

- (NSArray*)accessibilityAttributeNames
{
  return [[self accessible] accessibilityAttributeNames];
}

- (BOOL)accessibilityIsAttributeSettable:(NSString*)attribute
{
  return [[self accessible] accessibilityIsAttributeSettable:attribute];
}

- (id)accessibilityAttributeValue:(NSString*)attribute
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NIL;

  id<mozAccessible> accessible = [self accessible];
  
  // if we're the root (topmost) accessible, we need to return our native AXParent as we
  // traverse outside to the hierarchy of whoever embeds us. thus, fall back on NSView's
  // default implementation for this attribute.
  if ([attribute isEqualToString:NSAccessibilityParentAttribute] && [accessible isRoot]) {
    id parentAccessible = [super accessibilityAttributeValue:attribute];
    return parentAccessible;
  }

  return [accessible accessibilityAttributeValue:attribute];

  NS_OBJC_END_TRY_ABORT_BLOCK_NIL;
}

#endif /* ACCESSIBILITY */

@end

#pragma mark -

void
ChildViewMouseTracker::OnDestroyView(ChildView* aView)
{
  if (sLastMouseEventView == aView) {
    sLastMouseEventView = nil;
    [sLastMouseMoveEvent release];
    sLastMouseMoveEvent = nil;
  }
}

void
ChildViewMouseTracker::OnDestroyWindow(NSWindow* aWindow)
{
  if (sWindowUnderMouse == aWindow) {
    sWindowUnderMouse = nil;
  }
}

void
ChildViewMouseTracker::MouseEnteredWindow(NSEvent* aEvent)
{
  sWindowUnderMouse = [aEvent window];
  ReEvaluateMouseEnterState(aEvent);
}

void
ChildViewMouseTracker::MouseExitedWindow(NSEvent* aEvent)
{
  if (sWindowUnderMouse == [aEvent window]) {
    sWindowUnderMouse = nil;
    ReEvaluateMouseEnterState(aEvent);
  }
}

void
ChildViewMouseTracker::ReEvaluateMouseEnterState(NSEvent* aEvent)
{
  ChildView* oldView = sLastMouseEventView;
  sLastMouseEventView = ViewForEvent(aEvent);
  if (sLastMouseEventView != oldView) {
    // Send enter and / or exit events.
    nsMouseEvent::exitType type = [sLastMouseEventView window] == [oldView window] ?
                                    nsMouseEvent::eChild : nsMouseEvent::eTopLevel;
    [oldView sendMouseEnterOrExitEvent:aEvent enter:NO type:type];
    // After the cursor exits the window set it to a visible regular arrow cursor.
    if (type == nsMouseEvent::eTopLevel) {
      [[nsCursorManager sharedInstance] setCursor:eCursor_standard];
    }
    [sLastMouseEventView sendMouseEnterOrExitEvent:aEvent enter:YES type:type];
  }
}

void
ChildViewMouseTracker::ResendLastMouseMoveEvent()
{
  if (sLastMouseMoveEvent) {
    MouseMoved(sLastMouseMoveEvent);
  }
}

void
ChildViewMouseTracker::MouseMoved(NSEvent* aEvent)
{
  MouseEnteredWindow(aEvent);
  [sLastMouseEventView handleMouseMoved:aEvent];
  if (sLastMouseMoveEvent != aEvent) {
    [sLastMouseMoveEvent release];
    sLastMouseMoveEvent = [aEvent retain];
  }
}

void
ChildViewMouseTracker::MouseScrolled(NSEvent* aEvent)
{
  if (!nsCocoaUtils::IsMomentumScrollEvent(aEvent)) {
    // Store the position so we can pin future momentum scroll events.
    sLastScrollEventScreenLocation = nsCocoaUtils::ScreenLocationForEvent(aEvent);
  }
}

ChildView*
ChildViewMouseTracker::ViewForEvent(NSEvent* aEvent)
{
  NSWindow* window = sWindowUnderMouse;
  if (!window)
    return nil;

  NSPoint windowEventLocation = nsCocoaUtils::EventLocationForWindow(aEvent, window);
  NSView* view = [[[window contentView] superview] hitTest:windowEventLocation];
  if (![view isKindOfClass:[ChildView class]])
    return nil;

  ChildView* childView = (ChildView*)view;
  // If childView is being destroyed return nil.
  if (![childView widget])
    return nil;
  return WindowAcceptsEvent(window, aEvent, childView) ? childView : nil;
}

BOOL
ChildViewMouseTracker::WindowAcceptsEvent(NSWindow* aWindow, NSEvent* aEvent,
                                          ChildView* aView, BOOL aIsClickThrough)
{
  // Right mouse down events may get through to all windows, even to a top level
  // window with an open sheet.
  if (!aWindow || [aEvent type] == NSRightMouseDown)
    return YES;

  id delegate = [aWindow delegate];
  if (!delegate || ![delegate isKindOfClass:[WindowDelegate class]])
    return YES;

  nsIWidget *windowWidget = [(WindowDelegate *)delegate geckoWidget];
  if (!windowWidget)
    return YES;

  nsWindowType windowType;
  windowWidget->GetWindowType(windowType);

  NSWindow* topLevelWindow = nil;

  switch (windowType) {
    case eWindowType_popup:
      // If this is a context menu, it won't have a parent. So we'll always
      // accept mouse move events on context menus even when none of our windows
      // is active, which is the right thing to do.
      // For panels, the parent window is the XUL window that owns the panel.
      return WindowAcceptsEvent([aWindow parentWindow], aEvent, aView, aIsClickThrough);

    case eWindowType_toplevel:
    case eWindowType_dialog:
      if ([aWindow attachedSheet])
        return NO;

      topLevelWindow = aWindow;
      break;
    case eWindowType_sheet: {
      nsIWidget* parentWidget = windowWidget->GetSheetWindowParent();
      if (!parentWidget)
        return YES;

      topLevelWindow = (NSWindow*)parentWidget->GetNativeData(NS_NATIVE_WINDOW);
      break;
    }

    default:
      return YES;
  }

  if (!topLevelWindow ||
      ([topLevelWindow isMainWindow] && !aIsClickThrough) ||
      [aEvent type] == NSOtherMouseDown ||
      (([aEvent modifierFlags] & NSCommandKeyMask) != 0 &&
       [aEvent type] != NSMouseMoved))
    return YES;

  // If we're here then we're dealing with a left click or mouse move on an
  // inactive window or something similar. Ask Gecko what to do.
  return [aView inactiveWindowAcceptsMouseEvent:aEvent];
}

#pragma mark -

@interface NSView (MethodSwizzling)
- (BOOL)nsChildView_NSView_mouseDownCanMoveWindow;
@end

@implementation NSView (MethodSwizzling)

// All top-level browser windows belong to the ToolbarWindow class and have
// NSTexturedBackgroundWindowMask turned on in their "style" (see particularly
// [ToolbarWindow initWithContentRect:...] in nsCocoaWindow.mm).  This style
// normally means the window "may be moved by clicking and dragging anywhere
// in the window background", but we've suppressed this by giving the
// ChildView class a mouseDownCanMoveWindow method that always returns NO.
// Normally a ToolbarWindow's contentView (not a ChildView) returns YES when
// NSTexturedBackgroundWindowMask is turned on.  But normally this makes no
// difference.  However, under some (probably very unusual) circumstances
// (and only on Leopard) it *does* make a difference -- for example it
// triggers bmo bugs 431902 and 476393.  So here we make sure that a
// ToolbarWindow's contentView always returns NO from the
// mouseDownCanMoveWindow method.
- (BOOL)nsChildView_NSView_mouseDownCanMoveWindow
{
  NSWindow *ourWindow = [self window];
  NSView *contentView = [ourWindow contentView];
  if ([ourWindow isKindOfClass:[ToolbarWindow class]] && (self == contentView))
    return NO;
  return [self nsChildView_NSView_mouseDownCanMoveWindow];
}

@end

#ifdef __LP64__
// When using blocks, at least on OS X 10.7, the OS sometimes calls
// +[NSEvent removeMonitor:] more than once on a single event monitor, which
// causes crashes.  See bug 678607.  We hook these methods to work around
// the problem.
@interface NSEvent (MethodSwizzling)
+ (id)nsChildView_NSEvent_addLocalMonitorForEventsMatchingMask:(unsigned long long)mask handler:(id)block;
+ (void)nsChildView_NSEvent_removeMonitor:(id)eventMonitor;
@end

// This is a local copy of the AppKit frameworks sEventObservers hashtable.
// It only stores "local monitors".  We use it to ensure that +[NSEvent
// removeMonitor:] is never called more than once on the same local monitor.
static NSHashTable *sLocalEventObservers = nil;

@implementation NSEvent (MethodSwizzling)

+ (id)nsChildView_NSEvent_addLocalMonitorForEventsMatchingMask:(unsigned long long)mask handler:(id)block
{
  if (!sLocalEventObservers) {
    sLocalEventObservers = [[NSHashTable hashTableWithOptions:
      NSHashTableStrongMemory | NSHashTableObjectPointerPersonality] retain];
  }
  id retval =
    [self nsChildView_NSEvent_addLocalMonitorForEventsMatchingMask:mask handler:block];
  if (sLocalEventObservers && retval && ![sLocalEventObservers containsObject:retval]) {
    [sLocalEventObservers addObject:retval];
  }
  return retval;
}

+ (void)nsChildView_NSEvent_removeMonitor:(id)eventMonitor
{
  if (sLocalEventObservers && [eventMonitor isKindOfClass: ::NSClassFromString(@"_NSLocalEventObserver")]) {
    if (![sLocalEventObservers containsObject:eventMonitor]) {
      return;
    }
    [sLocalEventObservers removeObject:eventMonitor];
  }
  [self nsChildView_NSEvent_removeMonitor:eventMonitor];
}

@end
#endif // #ifdef __LP64__
