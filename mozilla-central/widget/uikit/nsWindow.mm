/* -*- Mode: c++; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Mozilla Android code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@pobox.com>
 *   Dave Townsend <dtownsend@oxymoronical.com>
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

#import <UIKit/UIEvent.h>
#import <UIKit/UIGraphics.h>
#import <UIKit/UIInterface.h>
#import <UIKit/UIScreen.h>
#import <UIKit/UITouch.h>
#import <UIKit/UIView.h>
#import <UIKit/UIWindow.h>

#include "nsWindow.h"
#include "nsScreenManager.h"
#include "nsAppShell.h"
#include "nsIWidgetListener.h"

#include "nsWidgetsCID.h"
#include "nsGfxCIID.h"

#include "gfxImageSurface.h"
#include "gfxQuartzSurface.h"
#include "gfxContext.h"
#include "nsRegion.h"
#include "Layers.h"

#include "nsTArray.h"

using namespace mozilla::layers;

#define ALOG(args...) printf(args); printf("\n")

UIColor* colors[] = {
    [UIColor blueColor],
    [UIColor redColor],
    [UIColor greenColor],
    [UIColor cyanColor],
    [UIColor yellowColor],
    [UIColor magentaColor],
    [UIColor orangeColor],
    [UIColor purpleColor],
    [UIColor brownColor]
};
int nextColor = 0;
int NUM_COLORS = sizeof(colors) / sizeof(colors[0]);

static inline void
GeckoRectToCGRect(const nsIntRect & inGeckoRect, CGRect & outRect)
{
  outRect.origin.x = inGeckoRect.x;
  outRect.origin.y = inGeckoRect.y;
  outRect.size.width = inGeckoRect.width;
  outRect.size.height = inGeckoRect.height;
}

// Used to retain a Cocoa object for the remainder of a method's execution.
class nsAutoRetainUIKitObject {
public:
nsAutoRetainUIKitObject(id anObject)
{
  mObject = [anObject retain];
}
~nsAutoRetainUIKitObject()
{
  [mObject release];
}
private:
  id mObject;  // [STRONG]
};

@interface ChildView : UIView
{
@public
    nsWindow* mGeckoChild; // weak ref
}
// sets up our view, attaching it to its owning gecko view
- (id)initWithFrame:(CGRect)inFrame geckoChild:(nsWindow*)inChild;
// Our Gecko child was Destroy()ed
- (void)widgetDestroyed;
// Tear down this ChildView
- (void)delayedTearDown;
// Event handling (UIResponder)
- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event;
- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event;
- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event;
- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event;
@end

@implementation ChildView
- (id)initWithFrame:(CGRect)inFrame geckoChild:(nsWindow*)inChild
{
    if ((self = [super initWithFrame:inFrame])) {
      mGeckoChild = inChild;
    }
    ALOG("[ChildView[%p] initWithFrame:] (mGeckoChild = %p)", (void*)self, (void*)mGeckoChild);
    self.opaque = YES;
    self.alpha = 1.0;
    return self;
}

- (void)widgetDestroyed
{
  mGeckoChild = nullptr;
}

- (void)delayedTearDown
{
  ALOG("[ChildView[%p] delayedTearDown] (mGeckoChild = %p)", (void*)self, (void*)mGeckoChild);
  [self removeFromSuperview];
  [self release];
}

- (void)initGeckoEvent:(nsInputEvent*)outGeckoEvent
{
  outGeckoEvent->widget = mGeckoChild;
  outGeckoEvent->time = PR_IntervalNow();
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event
{
    ALOG("[ChildView[%p] touchesBegan", self);

    if (!mGeckoChild)
        return;

    nsIntRect rect;
    mGeckoChild->GetScreenBounds(rect);
    ALOG("Screen bounds: [%d %d %d %d]", rect.x, rect.y, rect.width, rect.height);

    if ([touches count] != 1) // ignore multitouch for now
        return;

    UITouch* theTouch = [touches anyObject];

    nsMouseEvent geckoEvent(true, NS_MOUSE_BUTTON_DOWN, nullptr, nsMouseEvent::eReal);
    geckoEvent.clickCount = theTouch.tapCount;
    geckoEvent.button = nsMouseEvent::eLeftButton;

    [self initGeckoEvent:&geckoEvent];

    // convert point to view coordinate system
    CGPoint loc = [theTouch locationInView:self];
   geckoEvent.refPoint.x = static_cast<nscoord>(loc.x);
   geckoEvent.refPoint.y = static_cast<nscoord>(loc.y);

    mGeckoChild->DispatchWindowEvent(geckoEvent);
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event
{
    //XXX: handle like touchesEnded?
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event
{
    if (!mGeckoChild)
        return;

    if ([touches count] != 1) // ignore multitouch for now
        return;

    UITouch* theTouch = [touches anyObject];

    nsMouseEvent geckoEvent(true, NS_MOUSE_BUTTON_UP, nullptr, nsMouseEvent::eReal);

    geckoEvent.button = nsMouseEvent::eLeftButton;
    geckoEvent.clickCount = theTouch.tapCount;
    [self initGeckoEvent:&geckoEvent];

    // convert point to view coordinate system
    CGPoint loc = [theTouch locationInView:self];
   geckoEvent.refPoint.x = static_cast<nscoord>(loc.x);
   geckoEvent.refPoint.y = static_cast<nscoord>(loc.y);

    mGeckoChild->DispatchWindowEvent(geckoEvent);
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event
{
    if (!mGeckoChild)
        return;

    if ([touches count] != 1) // ignore multitouch for now
        return;

    UITouch* theTouch = [touches anyObject];

    nsMouseEvent geckoEvent(true, NS_MOUSE_MOVE, nullptr, nsMouseEvent::eReal);
    geckoEvent.button = nsMouseEvent::eLeftButton;
    geckoEvent.clickCount = theTouch.tapCount;
    [self initGeckoEvent:&geckoEvent];

    // convert point to view coordinate system
    CGPoint loc = [theTouch locationInView:self];
   geckoEvent.refPoint.x = static_cast<nscoord>(loc.x);
   geckoEvent.refPoint.y = static_cast<nscoord>(loc.y);

    mGeckoChild->DispatchWindowEvent(geckoEvent);
}

// The display system has told us that a portion of our view is dirty. Tell
// gecko to paint it
- (void)drawRect:(CGRect)aRect
{
    ALOG("[ChildView[%p] drawRect:[%f %f %f %f]] (mGeckoChild = %p)", (void*)self, aRect.origin.x, aRect.origin.y, aRect.size.width, aRect.size.height, (void*)mGeckoChild);

  if (!mGeckoChild || !mGeckoChild->IsVisible())
    return;

  CGContextRef cgContext = UIGraphicsGetCurrentContext();

  nsIntRegion region =
      nsIntRect(aRect.origin.x, aRect.origin.y, aRect.size.width, aRect.size.height);

  // Subtract child view rectangles from the region
  NSArray* subviews = [self subviews];
  for (int i = 0; i < int([subviews count]); ++i) {
    UIView* view = [subviews objectAtIndex:i];
    if (![view isKindOfClass:[ChildView class]] || [view isHidden])
      continue;
    CGRect frame = [self convertRect:[view frame] fromView:view];
    region.Sub(region,
      nsIntRect(frame.origin.x, frame.origin.y, frame.size.width, frame.size.height));
  }

  //TODO: OpenGL layers
  if (mGeckoChild->GetLayerManager(nullptr)->GetBackendType() == mozilla::layers::LAYERS_OPENGL) {
      NS_WARNING("TRYING TO USE OPENGL LAYERS!");
  }

  // Create Cairo objects.
  CGSize bufferSize = [self bounds].size;
  nsRefPtr<gfxQuartzSurface> targetSurface =
    new gfxQuartzSurface(cgContext, gfxSize(bufferSize.width, bufferSize.height));

  nsRefPtr<gfxContext> targetContext = new gfxContext(targetSurface);

  // Set up the clip region.
  nsIntRegionRectIterator iter(region);
  targetContext->NewPath();
  for (;;) {
    const nsIntRect* r = iter.Next();
    if (!r)
      break;
    targetContext->Rectangle(gfxRect(r->x, r->y, r->width, r->height));
  }
  targetContext->Clip();

  nsAutoRetainUIKitObject kungFuDeathGrip(self);
  bool painted = false;
  {
      nsBaseWidget::AutoLayerManagerSetup
          setupLayerManager(mGeckoChild, targetContext, mozilla::layers::BUFFER_NONE);

      nsIWidgetListener* listener = mGeckoChild->GetWidgetListener();
      if (listener)
        painted = listener->PaintWindow(mGeckoChild, region, nsIWidgetListener::SENT_WILL_PAINT);
  }

  if (!painted && [self isOpaque]) {
    // Gecko refused to draw, but we've claimed to be opaque, so we have to
    // draw something--fill with white.
      NS_WARNING("Drawing fallback content!");
    CGContextSetRGBFillColor(cgContext, 1, 1, 1, 1);
    CGContextFillRect(cgContext, CGRectMake(aRect.origin.x, aRect.origin.y,
                                            aRect.size.width, aRect.size.height));
    }
}
@end

NS_IMPL_ISUPPORTS_INHERITED0(nsWindow, nsBaseWidget)

nsWindow::nsWindow()
: mNativeView(nullptr),
  mVisible(false),
  mParent(nullptr),
  mTempThebesSurface(nullptr)
{
}

nsWindow::~nsWindow()
{
    [mNativeView widgetDestroyed]; // Safe if mNativeView is nil.
    TearDownView(); // Safe if called twice.
}

void nsWindow::TearDownView()
{
  if (!mNativeView)
    return;

  [mNativeView performSelectorOnMainThread:@selector(delayedTearDown) withObject:nil waitUntilDone:false];
  mNativeView = nil;
}

bool
nsWindow::IsTopLevel()
{
    return mWindowType == eWindowType_toplevel ||
        mWindowType == eWindowType_dialog ||
        mWindowType == eWindowType_invisible;
}

void nsWindow::Redraw()
{
    [mNativeView setNeedsDisplay];
}

void nsWindow::InitEvent(nsGUIEvent& event, nsIntPoint* aPoint)
{
}

//
// nsIWidget
//

NS_IMETHODIMP
nsWindow::Create(nsIWidget *aParent,
                 nsNativeWidget aNativeParent,
                 const nsIntRect &aRect,
                 nsDeviceContext *aContext,
                 nsWidgetInitData *aInitData)
{
    ALOG("nsWindow[%p]::Create %p/%p [%d %d %d %d]", (void*)this, (void*)aParent, (void*)aNativeParent, aRect.x, aRect.y, aRect.width, aRect.height);
    nsWindow* parent = (nsWindow*) aParent;
    ChildView* nativeParent = (ChildView*)aNativeParent;

    if (parent == nullptr && nativeParent)
        parent = nativeParent->mGeckoChild;
    if (parent && nativeParent == nullptr)
        nativeParent = parent->mNativeView;

    // for toplevel windows, bounds are fixed to full screen size
    if (parent == nullptr) {
        if (nsAppShell::gWindow == nil) {
            mBounds = nsScreenManager::GetBounds();
        }
        else {
            CGRect cgRect = [nsAppShell::gWindow bounds];
            mBounds.x = cgRect.origin.x;
            mBounds.y = cgRect.origin.y;
            mBounds.width = cgRect.size.width;
            mBounds.height = cgRect.size.height;
        }
    }
    else {
        mBounds = aRect;
    }

    ALOG("nsWindow[%p]::Create bounds: %d %d %d %d", (void*)this,
         mBounds.x, mBounds.y, mBounds.width, mBounds.height);

    // Set defaults which can be overriden from aInitData in BaseCreate
    mWindowType = eWindowType_toplevel;
    mBorderStyle = eBorderStyle_default;

    Inherited::BaseCreate(nullptr, mBounds, aContext, aInitData);

    NS_ASSERTION(IsTopLevel() || parent, "non top level window doesn't have a parent!");

    CGRect cgRect;
    GeckoRectToCGRect(mBounds, cgRect);
    mNativeView = [[ChildView alloc] initWithFrame:cgRect geckoChild:this];
    //XXX: for easier debugging right now
    //mNativeView.backgroundColor = [UIColor greenColor];
    mNativeView.backgroundColor = colors[nextColor];
    ALOG("nsWindow[%p]::Create: mNativeView: %p, color: %d", (void*)this, (void*)mNativeView, nextColor);
    nextColor++;
    nextColor %= NUM_COLORS;

    if (parent) {
        parent->mChildren.AppendElement(this);
        mParent = parent;

        [nativeParent addSubview:mNativeView];
    }
    else if (nsAppShell::gWindow) {
        [nsAppShell::gWindow addSubview:mNativeView];
    }
    else {
        [nsAppShell::gTopLevelViews addObject:mNativeView];
    }

  return NS_OK;
}

NS_IMETHODIMP
nsWindow::Destroy(void)
{
    ALOG("nsWindow[%p]::Destroy (mNativeView = %p)", (void*)this, (void*)mNativeView);
    for (uint32_t i = 0; i < mChildren.Length(); ++i) {
        // why do we still have children?
        mChildren[i]->SetParent(nullptr);
    }

    if (mParent)
        mParent->mChildren.RemoveElement(this);

    [mNativeView widgetDestroyed];

    nsBaseWidget::Destroy();

    NotifyWindowDestroyed();

    TearDownView();

    nsBaseWidget::OnDestroy();

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::ConfigureChildren(const nsTArray<nsIWidget::Configuration>& config)
{
    for (uint32_t i = 0; i < config.Length(); ++i) {
        nsWindow *childWin = (nsWindow*) config[i].mChild;
        childWin->Resize(config[i].mBounds.x,
                         config[i].mBounds.y,
                         config[i].mBounds.width,
                         config[i].mBounds.height,
                         false);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetParent(nsIWidget* aNewParent)
{
  ALOG("nsWindow[%p]::SetParent(%p)", (void*)this, (void*)aNewParent);
  if ((nsIWidget*)mParent == aNewParent)
    return NS_OK;

  if (mParent) {
    mParent->mChildren.RemoveElement(this);
    [mNativeView removeFromSuperview];
  }

  mParent = (nsWindow*)aNewParent;

  if (mParent) {
    mParent->mChildren.AppendElement(this);
    [mParent->mNativeView addSubview:mNativeView];
  }
  //nsAppShell::ScheduleRedraw(); ?
  return NS_OK;
}

nsIWidget *nsWindow::GetParent(void)
{
  return mParent;
}

NS_IMETHODIMP
nsWindow::Show(bool aState)
{
  ALOG("nsWindow[%p]::Show(%d) (toplevel: %d)", (void*)this, aState, mParent == nullptr);
  if (aState != mVisible) {
      mNativeView.hidden = !aState;
    if (aState && mParent == nullptr) {
        [nsAppShell::gWindow bringSubviewToFront:mNativeView];
    }

    mVisible = aState;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetModal(bool aModal)
{
  return NS_OK;
}

bool
nsWindow::IsVisible() const
{
  return mVisible;
}

NS_IMETHODIMP
nsWindow::ConstrainPosition(bool aAllowSlop, int32_t *aX, int32_t *aY)
{
  return NS_OK;
}

NS_IMETHODIMP
nsWindow::Move(double aX, double aY)
{
  if (!mNativeView || (mBounds.x == aX && mBounds.y == aY))
    return NS_OK;

  mBounds.x = aX;
  mBounds.y = aY;

  CGRect r;
  GeckoRectToCGRect(mBounds, r);
  mNativeView.frame = r;

  if (mVisible)
    [mNativeView setNeedsDisplay];

  ReportMoveEvent();
  return NS_OK;
}

NS_IMETHODIMP
nsWindow::Resize(double aWidth, double aHeight, bool aRepaint)
{
  if (!mNativeView ||
      (mBounds.width == NSToIntRound(aWidth) && mBounds.height == NSToIntRound(aHeight)))
    return NS_OK;

  mBounds.width  = NSToIntRound(aWidth);
  mBounds.height = NSToIntRound(aHeight);

  CGRect r;
  GeckoRectToCGRect(mBounds, r);
  [mNativeView setFrame:r];

  if (mVisible && aRepaint)
    [mNativeView setNeedsDisplay];

  ReportSizeEvent();

  return NS_OK;
}

NS_IMETHODIMP
nsWindow::Resize(double aX, double aY,
                 double aWidth, double aHeight, bool aRepaint)
{
  BOOL isMoving = (mBounds.x != NSToIntRound(aX) || mBounds.y != NSToIntRound(aY));
  BOOL isResizing = (mBounds.width != NSToIntRound(aWidth) || mBounds.height != NSToIntRound(aHeight));
  if (!mNativeView || (!isMoving && !isResizing))
    return NS_OK;

  if (isMoving) {
    mBounds.x = NSToIntRound(aX);
    mBounds.y = NSToIntRound(aY);
  }
  if (isResizing) {
    mBounds.width  = NSToIntRound(aWidth);
    mBounds.height = NSToIntRound(aHeight);
  }

  CGRect r;
  GeckoRectToCGRect(mBounds, r);
  [mNativeView setFrame:r];

  if (mVisible && aRepaint)
    [mNativeView setNeedsDisplay];

  if (isMoving) 
    ReportMoveEvent();

  if (isResizing)
    ReportSizeEvent();

  return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetZIndex(int32_t aZIndex)
{
  return NS_OK;
}

NS_IMETHODIMP
nsWindow::PlaceBehind(nsTopLevelWidgetZPlacement aPlacement,
                      nsIWidget *aWidget,
                      bool aActivate)
{
  return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetSizeMode(int32_t aMode)
{
  return NS_OK;
}

NS_IMETHODIMP
nsWindow::Enable(bool aState)
{
  return NS_OK;
}

bool
nsWindow::IsEnabled() const
{
  return true;
}

NS_IMETHODIMP
nsWindow::Invalidate(const nsIntRect &aRect)
{
  if (!mNativeView || !mVisible)
    return NS_OK;

  CGRect r;
  GeckoRectToCGRect(aRect, r);

  [mNativeView setNeedsLayout];
  [mNativeView setNeedsDisplayInRect:r];

  return NS_OK;
}

NS_IMETHODIMP
nsWindow::Update()
{
  return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetFocus(bool aRaise)
{
  ALOG("nsWindow[%p]::SetFocus(%d)", (void*)this, aRaise);
  [[mNativeView window] makeKeyWindow];
  [mNativeView becomeFirstResponder];
  return NS_OK;
}

void nsWindow::ReportMoveEvent()
{
  if (mWidgetListener)
    mWidgetListener->WindowMoved(this, mBounds.x, mBounds.y);
}

void nsWindow::ReportSizeEvent()
{
  if (mWidgetListener)
    mWidgetListener->WindowResized(this, mBounds.width, mBounds.height);
}


NS_IMETHODIMP
nsWindow::GetScreenBounds(nsIntRect &aRect)
{
  nsIntPoint p = WidgetToScreenOffset();
  aRect.x = p.x;
  aRect.y = p.y;
  aRect.width = mBounds.width;
  aRect.height = mBounds.height;
  return NS_OK;
}

nsIntPoint nsWindow::WidgetToScreenOffset()
{
  CGPoint temp;
  temp.x = 0;
  temp.y = 0;
  
  if (!mParent && nsAppShell::gWindow) {
      // convert to screen coords
      temp = [nsAppShell::gWindow convertPoint:temp toWindow:nil];
      return nsIntPoint(NSToIntRound(temp.x), NSToIntRound(temp.y));
  }

  nsIntPoint offset(0, 0);
  temp = [mNativeView convertPoint:temp toView:nil];
  offset.x += temp.x;
  offset.y += temp.y;

  return offset;
}

NS_IMETHODIMP
nsWindow::DispatchEvent(nsGUIEvent *aEvent, nsEventStatus &aStatus)
{
  aStatus = nsEventStatus_eIgnore;
  nsCOMPtr<nsIWidget> kungFuDeathGrip = do_QueryInterface(mParent ? mParent : this);

  if (mWidgetListener)
    aStatus = mWidgetListener->HandleEvent(aEvent, mUseAttachedEvents);

  return NS_OK;
}

bool nsWindow::DispatchWindowEvent(nsGUIEvent &event)
{
  nsEventStatus status;
  DispatchEvent(&event, status);
  return ConvertStatus(status);
}

NS_IMETHODIMP
nsWindow::SetBackgroundColor(const nscolor &aColor)
{
    mNativeView.backgroundColor = [UIColor colorWithRed:NS_GET_R(aColor)
                                   green:NS_GET_G(aColor)
                                   blue:NS_GET_B(aColor)
                                   alpha:NS_GET_A(aColor)];
    return NS_OK;
}

void* nsWindow::GetNativeData(uint32_t aDataType)
{
  void* retVal = nullptr;

  switch (aDataType)
  {
    case NS_NATIVE_WIDGET:
    case NS_NATIVE_DISPLAY:
      retVal = (void*)mNativeView;
      break;

    case NS_NATIVE_WINDOW:
      retVal = [mNativeView window];
      break;

    case NS_NATIVE_GRAPHIC:
      NS_ERROR("Requesting NS_NATIVE_GRAPHIC on a UIKit child view!");
      break;

    case NS_NATIVE_OFFSETX:
      retVal = 0;
      break;

    case NS_NATIVE_OFFSETY:
      retVal = 0;
      break;

    case NS_NATIVE_PLUGIN_PORT:
        // not implemented
        break;
  }

  return retVal;
}

gfxASurface* nsWindow::GetThebesSurface()
{
  if (!mTempThebesSurface) {
    mTempThebesSurface = new gfxQuartzSurface(gfxSize(1, 1), gfxASurface::ImageFormatARGB32);
  }

  return mTempThebesSurface;
}

NS_IMETHODIMP
nsWindow::ReparentNativeWidget(nsIWidget* aNewParent)
{
  NS_PRECONDITION(aNewParent, "");

  return SetParent(aNewParent);
}
