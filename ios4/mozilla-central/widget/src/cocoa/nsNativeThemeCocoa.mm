/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * Mike Pinkerton (pinkerton@netscape.com).
 * Portions created by the Initial Developer are Copyright (C) 2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *    Vladimir Vukicevic <vladimir@pobox.com> (HITheme rewrite)
 *    Josh Aas <josh@mozilla.com>
 *    Colin Barrett <cbarrett@mozilla.com>
 *    Matthew Gregan <kinetik@flim.org>
 *    Markus Stange <mstange@themasta.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "nsNativeThemeCocoa.h"
#include "nsObjCExceptions.h"
#include "nsIRenderingContext.h"
#include "nsRect.h"
#include "nsSize.h"
#include "nsThemeConstants.h"
#include "nsIPresShell.h"
#include "nsPresContext.h"
#include "nsIContent.h"
#include "nsIDocument.h"
#include "nsIFrame.h"
#include "nsIAtom.h"
#include "nsIEventStateManager.h"
#include "nsINameSpaceManager.h"
#include "nsPresContext.h"
#include "nsILookAndFeel.h"
#include "nsWidgetAtoms.h"
#include "nsToolkit.h"
#include "nsCocoaWindow.h"
#include "nsNativeThemeColors.h"
#include "nsIScrollableFrame.h"

#include "gfxContext.h"
#include "gfxQuartzSurface.h"
#include "gfxQuartzNativeDrawing.h"

#define DRAW_IN_FRAME_DEBUG 0
#define SCROLLBARS_VISUAL_DEBUG 0

// private Quartz routines needed here
extern "C" {
  CG_EXTERN void CGContextSetCTM(CGContextRef, CGAffineTransform);
}

// Workaround for NSCell control tint drawing
// Without this workaround, NSCells are always drawn with the clear control tint
// as long as they're not attached to an NSControl which is a subview of an active window.
// XXXmstange Why doesn't Webkit need this?
@implementation NSCell (ControlTintWorkaround)
- (int)_realControlTint { return [self controlTint]; }
@end

// The purpose of this class is to provide objects that can be used when drawing
// NSCells using drawWithFrame:inView: without causing any harm. The only
// messages that will be sent to such an object are "isFlipped" and
// "currentEditor": isFlipped needs to return YES in order to avoid drawing bugs
// on 10.4 (see bug 465069); currentEditor (which isn't even a method of
// NSView) will be called when drawing search fields, and we only provide it in
// order to prevent "unrecognized selector" exceptions.
// There's no need to pass the actual NSView that we're drawing into to
// drawWithFrame:inView:. What's more, doing so even causes unnecessary
// invalidations as soon as we draw a focusring!
@interface CellDrawView : NSView

@end;

@implementation CellDrawView

- (BOOL)isFlipped
{
  return YES;
}

- (NSText*)currentEditor
{
  return nil;
}

@end

// Copied from nsLookAndFeel.h
// Apple hasn't defined a constant for scollbars with two arrows on each end, so we'll use this one.
static const int kThemeScrollBarArrowsBoth = 2;

#define HITHEME_ORIENTATION kHIThemeOrientationNormal
#define MAX_FOCUS_RING_WIDTH 4

// These enums are for indexing into the margin array.
enum {
  leopardOS
};

enum {
  miniControlSize,
  smallControlSize,
  regularControlSize
};

enum {
  leftMargin,
  topMargin,
  rightMargin,
  bottomMargin
};

static int EnumSizeForCocoaSize(NSControlSize cocoaControlSize) {
  if (cocoaControlSize == NSMiniControlSize)
    return miniControlSize;
  else if (cocoaControlSize == NSSmallControlSize)
    return smallControlSize;
  else
    return regularControlSize;
}

static void InflateControlRect(NSRect* rect, NSControlSize cocoaControlSize, const float marginSet[][3][4])
{
  if (!marginSet)
    return;

  static int osIndex = leopardOS;
  int controlSize = EnumSizeForCocoaSize(cocoaControlSize);
  const float* buttonMargins = marginSet[osIndex][controlSize];
  rect->origin.x -= buttonMargins[leftMargin];
  rect->origin.y -= buttonMargins[bottomMargin];
  rect->size.width += buttonMargins[leftMargin] + buttonMargins[rightMargin];
  rect->size.height += buttonMargins[bottomMargin] + buttonMargins[topMargin];
}

static NSWindow* NativeWindowForFrame(nsIFrame* aFrame,
                                      nsIWidget** aTopLevelWidget = NULL)
{
  if (!aFrame)
    return nil;  

  nsIWidget* widget = aFrame->GetNearestWidget();
  if (!widget)
    return nil;

  nsIWidget* topLevelWidget = widget->GetTopLevelWidget();
  if (aTopLevelWidget)
    *aTopLevelWidget = topLevelWidget;

  return (NSWindow*)topLevelWidget->GetNativeData(NS_NATIVE_WINDOW);
}

static BOOL FrameIsInActiveWindow(nsIFrame* aFrame)
{
  nsIWidget* topLevelWidget = NULL;
  NSWindow* win = NativeWindowForFrame(aFrame, &topLevelWidget);
  if (!topLevelWidget || !win)
    return YES;

  // XUL popups, e.g. the toolbar customization popup, can't become key windows,
  // but controls in these windows should still get the active look.
  nsWindowType windowType;
  topLevelWidget->GetWindowType(windowType);
  if (windowType == eWindowType_popup)
    return YES;
  if ([win isSheet])
    return [win isKeyWindow];
  return [win isMainWindow] && ![win attachedSheet];
}

NS_IMPL_ISUPPORTS1(nsNativeThemeCocoa, nsITheme)


nsNativeThemeCocoa::nsNativeThemeCocoa()
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  mPushButtonCell = [[NSButtonCell alloc] initTextCell:nil];
  [mPushButtonCell setButtonType:NSMomentaryPushInButton];
  [mPushButtonCell setHighlightsBy:NSPushInCellMask];

  mRadioButtonCell = [[NSButtonCell alloc] initTextCell:nil];
  [mRadioButtonCell setButtonType:NSRadioButton];

  mCheckboxCell = [[NSButtonCell alloc] initTextCell:nil];
  [mCheckboxCell setButtonType:NSSwitchButton];
  [mCheckboxCell setAllowsMixedState:YES];

  mSearchFieldCell = [[NSSearchFieldCell alloc] initTextCell:@""];
  [mSearchFieldCell setBezelStyle:NSTextFieldRoundedBezel];
  [mSearchFieldCell setBezeled:YES];
  [mSearchFieldCell setEditable:YES];
  [mSearchFieldCell setFocusRingType:NSFocusRingTypeExterior];

  mDropdownCell = [[NSPopUpButtonCell alloc] initTextCell:@"" pullsDown:NO];

  mComboBoxCell = [[NSComboBoxCell alloc] initTextCell:@""];
  [mComboBoxCell setBezeled:YES];
  [mComboBoxCell setEditable:YES];
  [mComboBoxCell setFocusRingType:NSFocusRingTypeExterior];

  mCellDrawView = [[CellDrawView alloc] init];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

nsNativeThemeCocoa::~nsNativeThemeCocoa()
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  [mPushButtonCell release];
  [mRadioButtonCell release];
  [mCheckboxCell release];
  [mSearchFieldCell release];
  [mDropdownCell release];
  [mComboBoxCell release];
  [mCellDrawView release];

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

// Limit on the area of the target rect (in pixels^2) in
// DrawCellWithScaling(), DrawButton() and DrawScrollbar(), above which we
// don't draw the object into a bitmap buffer.  This is to avoid crashes in
// [NSGraphicsContext graphicsContextWithGraphicsPort:flipped:] and
// CGContextDrawImage(), and also to avoid very poor drawing performance in
// CGContextDrawImage() when it scales the bitmap (particularly if xscale or
// yscale is less than but near 1 -- e.g. 0.9).  This value was determined
// by trial and error, on OS X 10.4.11 and 10.5.4, and on systems with
// different amounts of RAM.
#define BITMAP_MAX_AREA 500000

/*
 * Draw the given NSCell into the given cgContext.
 *
 * destRect - the size and position of the resulting control rectangle
 * controlSize - the NSControlSize which will be given to the NSCell before
 *  asking it to render
 * naturalSize - The natural dimensions of this control.
 *  If the control rect size is not equal to either of these, a scale
 *  will be applied to the context so that rendering the control at the
 *  natural size will result in it filling the destRect space.
 *  If a control has no natural dimensions in either/both axes, pass 0.0f.
 * minimumSize - The minimum dimensions of this control.
 *  If the control rect size is less than the minimum for a given axis,
 *  a scale will be applied to the context so that the minimum is used
 *  for drawing.  If a control has no minimum dimensions in either/both
 *  axes, pass 0.0f.
 * marginSet - an array of margins; a multidimensional array of [2][3][4],
 *  with the first dimension being the OS version (Tiger or Leopard),
 *  the second being the control size (mini, small, regular), and the third
 *  being the 4 margin values (left, top, right, bottom).
 * view - The NSView that we're drawing into. As far as I can tell, it doesn't
 *  matter if this is really the right view; it just has to return YES when
 *  asked for isFlipped. Otherwise we'll get drawing bugs on 10.4.
 * mirrorHorizontal - whether to mirror the cell horizontally
 */
static void DrawCellWithScaling(NSCell *cell,
                                CGContextRef cgContext,
                                const HIRect& destRect,
                                NSControlSize controlSize,
                                NSSize naturalSize,
                                NSSize minimumSize,
                                const float marginSet[][3][4],
                                NSView* view,
                                BOOL mirrorHorizontal)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  NSRect drawRect = NSMakeRect(destRect.origin.x, destRect.origin.y, destRect.size.width, destRect.size.height);

  if (naturalSize.width != 0.0f)
    drawRect.size.width = naturalSize.width;
  if (naturalSize.height != 0.0f)
    drawRect.size.height = naturalSize.height;

  // Keep aspect ratio when scaling if one dimension is free.
  if (naturalSize.width == 0.0f && naturalSize.height != 0.0f)
    drawRect.size.width = destRect.size.width * naturalSize.height / destRect.size.height;
  if (naturalSize.height == 0.0f && naturalSize.width != 0.0f)
    drawRect.size.height = destRect.size.height * naturalSize.width / destRect.size.width;

  // Honor minimum sizes.
  if (drawRect.size.width < minimumSize.width)
    drawRect.size.width = minimumSize.width;
  if (drawRect.size.height < minimumSize.height)
    drawRect.size.height = minimumSize.height;

  [NSGraphicsContext saveGraphicsState];

  // Only skip the buffer if the area of our cell (in pixels^2) is too large.
  if (drawRect.size.width * drawRect.size.height > BITMAP_MAX_AREA) {
    // Inflate the rect Gecko gave us by the margin for the control.
    InflateControlRect(&drawRect, controlSize, marginSet);

    NSGraphicsContext* savedContext = [NSGraphicsContext currentContext];
    [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithGraphicsPort:cgContext flipped:YES]];

    [cell drawWithFrame:drawRect inView:view];

    [NSGraphicsContext setCurrentContext:savedContext];
  }
  else {
    float w = ceil(drawRect.size.width);
    float h = ceil(drawRect.size.height);
    NSRect tmpRect = NSMakeRect(MAX_FOCUS_RING_WIDTH, MAX_FOCUS_RING_WIDTH, w, h);

    // inflate to figure out the frame we need to tell NSCell to draw in, to get something that's 0,0,w,h
    InflateControlRect(&tmpRect, controlSize, marginSet);

    // and then, expand by MAX_FOCUS_RING_WIDTH size to make sure we can capture any focus ring
    w += MAX_FOCUS_RING_WIDTH * 2.0;
    h += MAX_FOCUS_RING_WIDTH * 2.0;

    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(NULL,
                                             (int) w, (int) h,
                                             8, (int) w * 4,
                                             rgb, kCGImageAlphaPremultipliedFirst);
    CGColorSpaceRelease(rgb);

    // We need to flip the image twice in order to avoid drawing bugs on 10.4, see bug 465069.
    // This is the first flip transform, applied to cgContext.
    CGContextScaleCTM(cgContext, 1.0f, -1.0f);
    CGContextTranslateCTM(cgContext, 0.0f, -(2.0 * destRect.origin.y + destRect.size.height));
    if (mirrorHorizontal) {
      CGContextScaleCTM(cgContext, -1.0f, 1.0f);
      CGContextTranslateCTM(cgContext, -(2.0 * destRect.origin.x + destRect.size.width), 0.0f);
    }

    NSGraphicsContext* savedContext = [NSGraphicsContext currentContext];
    [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithGraphicsPort:ctx flipped:YES]];

    // This is the second flip transform, applied to ctx.
    CGContextScaleCTM(ctx, 1.0f, -1.0f);
    CGContextTranslateCTM(ctx, 0.0f, -(2.0 * tmpRect.origin.y + tmpRect.size.height));

    [cell drawWithFrame:tmpRect inView:view];

    [NSGraphicsContext setCurrentContext:savedContext];

    CGImageRef img = CGBitmapContextCreateImage(ctx);

    // Drop the image into the original destination rectangle, scaling to fit
    // Only scale MAX_FOCUS_RING_WIDTH by xscale/yscale when the resulting rect
    // doesn't extend beyond the overflow rect
    float xscale = destRect.size.width / drawRect.size.width;
    float yscale = destRect.size.height / drawRect.size.height;
    float scaledFocusRingX = xscale < 1.0f ? MAX_FOCUS_RING_WIDTH * xscale : MAX_FOCUS_RING_WIDTH;
    float scaledFocusRingY = yscale < 1.0f ? MAX_FOCUS_RING_WIDTH * yscale : MAX_FOCUS_RING_WIDTH;
    CGContextDrawImage(cgContext, CGRectMake(destRect.origin.x - scaledFocusRingX,
                                             destRect.origin.y - scaledFocusRingY,
                                             destRect.size.width + scaledFocusRingX * 2,
                                             destRect.size.height + scaledFocusRingY * 2),
                       img);

    CGImageRelease(img);
    CGContextRelease(ctx);
  }

  [NSGraphicsContext restoreGraphicsState];

#if DRAW_IN_FRAME_DEBUG
  CGContextSetRGBFillColor(cgContext, 0.0, 0.0, 0.5, 0.25);
  CGContextFillRect(cgContext, destRect);
#endif

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

struct CellRenderSettings {
  // The natural dimensions of the control.
  // If a control has no natural dimensions in either/both axes, set to 0.0f.
  NSSize naturalSizes[3];

  // The minimum dimensions of the control.
  // If a control has no minimum dimensions in either/both axes, set to 0.0f.
  NSSize minimumSizes[3];

  // A multidimensional array of [2][3][4],
  // with the first dimension being the OS version (Tiger or Leopard),
  // the second being the control size (mini, small, regular), and the third
  // being the 4 margin values (left, top, right, bottom).
  float margins[2][3][4];
};

/*
 * Draw the given NSCell into the given cgContext with a nice control size.
 *
 * This function is similar to DrawCellWithScaling, but it decides what
 * control size to use based on the destRect's size.
 * Scaling is only applied when the difference between the destRect's size
 * and the next smaller natural size is greater than snapTolerance. Otherwise
 * it snaps to the next smaller control size without scaling because unscaled
 * controls look nicer.
 */
static void DrawCellWithSnapping(NSCell *cell,
                                 CGContextRef cgContext,
                                 const HIRect& destRect,
                                 const CellRenderSettings settings,
                                 float verticalAlignFactor,
                                 NSView* view,
                                 BOOL mirrorHorizontal,
                                 float snapTolerance = 2.0f)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  const float rectWidth = destRect.size.width, rectHeight = destRect.size.height;
  const NSSize *sizes = settings.naturalSizes;
  const NSSize miniSize = sizes[EnumSizeForCocoaSize(NSMiniControlSize)];
  const NSSize smallSize = sizes[EnumSizeForCocoaSize(NSSmallControlSize)];
  const NSSize regularSize = sizes[EnumSizeForCocoaSize(NSRegularControlSize)];

  NSControlSize controlSizeX = NSRegularControlSize, controlSizeY = NSRegularControlSize;
  HIRect drawRect = destRect;

  if (rectWidth <= miniSize.width + snapTolerance && rectWidth < smallSize.width)
    controlSizeX = NSMiniControlSize;
  else if(rectWidth <= smallSize.width + snapTolerance && rectWidth < regularSize.width)
    controlSizeX = NSSmallControlSize;

  if (rectHeight <= miniSize.height + snapTolerance && rectHeight < smallSize.height)
    controlSizeY = NSMiniControlSize;
  else if(rectHeight <= smallSize.height + snapTolerance && rectHeight < regularSize.height)
    controlSizeY = NSSmallControlSize;

  NSControlSize controlSize = NSRegularControlSize;
  int sizeIndex = 0;

  // At some sizes, don't scale but snap.
  const NSControlSize smallerControlSize =
    EnumSizeForCocoaSize(controlSizeX) < EnumSizeForCocoaSize(controlSizeY) ?
    controlSizeX : controlSizeY;
  const int smallerControlSizeIndex = EnumSizeForCocoaSize(smallerControlSize);
  const NSSize size = sizes[smallerControlSizeIndex];
  float diffWidth = size.width ? rectWidth - size.width : 0.0f;
  float diffHeight = size.height ? rectHeight - size.height : 0.0f;
  if (diffWidth >= 0.0f && diffHeight >= 0.0f &&
      diffWidth <= snapTolerance && diffHeight <= snapTolerance) {
    // Snap to the smaller control size.
    controlSize = smallerControlSize;
    sizeIndex = smallerControlSizeIndex;
    // Resize and center the drawRect.
    if (sizes[sizeIndex].width) {
      drawRect.origin.x += ceil((destRect.size.width - sizes[sizeIndex].width) / 2);
      drawRect.size.width = sizes[sizeIndex].width;
    }
    if (sizes[sizeIndex].height) {
      drawRect.origin.y += floor((destRect.size.height - sizes[sizeIndex].height) * verticalAlignFactor);
      drawRect.size.height = sizes[sizeIndex].height;
    }
  } else {
    // Use the larger control size.
    controlSize = EnumSizeForCocoaSize(controlSizeX) > EnumSizeForCocoaSize(controlSizeY) ?
                  controlSizeX : controlSizeY;
    sizeIndex = EnumSizeForCocoaSize(controlSize);
   }

  [cell setControlSize:controlSize];

  NSSize minimumSize = settings.minimumSizes ? settings.minimumSizes[sizeIndex] : NSZeroSize;
  DrawCellWithScaling(cell, cgContext, drawRect, controlSize, sizes[sizeIndex],
                      minimumSize, settings.margins, view, mirrorHorizontal);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

static float VerticalAlignFactor(nsIFrame *aFrame)
{
  if (!aFrame)
    return 0.5f; // default: center

  const nsStyleCoord& va = aFrame->GetStyleTextReset()->mVerticalAlign;
  PRUint8 intval = (va.GetUnit() == eStyleUnit_Enumerated)
                     ? va.GetIntValue()
                     : NS_STYLE_VERTICAL_ALIGN_MIDDLE;
  switch (intval) {
    case NS_STYLE_VERTICAL_ALIGN_TOP:
    case NS_STYLE_VERTICAL_ALIGN_TEXT_TOP:
      return 0.0f;

    case NS_STYLE_VERTICAL_ALIGN_SUB:
    case NS_STYLE_VERTICAL_ALIGN_SUPER:
    case NS_STYLE_VERTICAL_ALIGN_MIDDLE:
    case NS_STYLE_VERTICAL_ALIGN_MIDDLE_WITH_BASELINE:
      return 0.5f;

    case NS_STYLE_VERTICAL_ALIGN_BASELINE:
    case NS_STYLE_VERTICAL_ALIGN_TEXT_BOTTOM:
    case NS_STYLE_VERTICAL_ALIGN_BOTTOM:
      return 1.0f;

    default:
      NS_NOTREACHED("invalid vertical-align");
      return 0.5f;
  }
}

// These are the sizes that Gecko needs to request to draw if it wants
// to get a standard-sized Aqua radio button drawn. Note that the rects
// that draw these are actually a little bigger.
static const CellRenderSettings radioSettings = {
  {
    NSMakeSize(11, 11), // mini
    NSMakeSize(13, 13), // small
    NSMakeSize(16, 16)  // regular
  },
  {
    NSZeroSize, NSZeroSize, NSZeroSize
  },
  {
    { // Tiger
      {0, 0, 0, 0},     // mini
      {0, 1, 1, 2},     // small
      {0, -1, 0, 1}     // regular
    },
    { // Leopard
      {0, 0, 0, 0},     // mini
      {0, 1, 1, 1},     // small
      {0, 0, 0, 0}      // regular
    }
  }
};

static const CellRenderSettings checkboxSettings = {
  {
    NSMakeSize(11, 11), // mini
    NSMakeSize(13, 13), // small
    NSMakeSize(16, 16)  // regular
  },
  {
    NSZeroSize, NSZeroSize, NSZeroSize
  },
  {
    { // Tiger
      {0, 1, 0, 0},     // mini
      {0, 2, 0, 1},     // small
      {0, 1, 0, 1}      // regular
    },
    { // Leopard
      {0, 1, 0, 0},     // mini
      {0, 1, 0, 1},     // small
      {0, 1, 0, 1}      // regular
    }
  }
};

void
nsNativeThemeCocoa::DrawCheckboxOrRadio(CGContextRef cgContext, PRBool inCheckbox,
                                        const HIRect& inBoxRect, PRBool inSelected,
                                        PRBool inDisabled, PRInt32 inState,
                                        nsIFrame* aFrame)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  NSButtonCell *cell = inCheckbox ? mCheckboxCell : mRadioButtonCell;
  NSCellStateValue state = inSelected ? NSOnState : NSOffState;

  // Check if we have an indeterminate checkbox
  if (inCheckbox && GetIndeterminate(aFrame))
    state = NSMixedState;

  [cell setEnabled:!inDisabled];
  [cell setShowsFirstResponder:(inState & NS_EVENT_STATE_FOCUS)];
  [cell setState:state];
  [cell setHighlighted:((inState & NS_EVENT_STATE_ACTIVE) && (inState & NS_EVENT_STATE_HOVER))];
  [cell setControlTint:(FrameIsInActiveWindow(aFrame) ? [NSColor currentControlTint] : NSClearControlTint)];

  // Ensure that the control is square.
  float length = PR_MIN(inBoxRect.size.width, inBoxRect.size.height);
  HIRect drawRect = CGRectMake(inBoxRect.origin.x + (int)((inBoxRect.size.width - length) / 2.0f),
                               inBoxRect.origin.y + (int)((inBoxRect.size.height - length) / 2.0f),
                               length, length);

  DrawCellWithSnapping(cell, cgContext, drawRect,
                       inCheckbox ? checkboxSettings : radioSettings,
                       VerticalAlignFactor(aFrame), mCellDrawView, NO);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

static const CellRenderSettings searchFieldSettings = {
  {
    NSMakeSize(0, 16), // mini
    NSMakeSize(0, 19), // small
    NSMakeSize(0, 22)  // regular
  },
  {
    NSMakeSize(32, 0), // mini
    NSMakeSize(38, 0), // small
    NSMakeSize(44, 0)  // regular
  },
  {
    { // Tiger
      {0, 0, 0, 0},     // mini
      {0, 0, 0, 0},     // small
      {0, 0, 0, 0}      // regular
    },
    { // Leopard
      {0, 0, 0, 0},     // mini
      {0, 0, 0, 0},     // small
      {0, 0, 0, 0}      // regular
    }
  }
};

void
nsNativeThemeCocoa::DrawSearchField(CGContextRef cgContext, const HIRect& inBoxRect,
                                    nsIFrame* aFrame)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  NSSearchFieldCell* cell = mSearchFieldCell;
  [cell setEnabled:!IsDisabled(aFrame)];
  [cell setShowsFirstResponder:IsFocused(aFrame)];

  DrawCellWithSnapping(cell, cgContext, inBoxRect, searchFieldSettings,
                       VerticalAlignFactor(aFrame), mCellDrawView,
                       IsFrameRTL(aFrame));

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

static const CellRenderSettings pushButtonSettings = {
  {
    NSMakeSize(0, 16), // mini
    NSMakeSize(0, 19), // small
    NSMakeSize(0, 22)  // regular
  },
  {
    NSMakeSize(18, 0), // mini
    NSMakeSize(26, 0), // small
    NSMakeSize(30, 0)  // regular
  },
  {
    { // Tiger
      {1, 1, 1, 1},    // mini
      {5, 0, 5, 2},    // small
      {6, 0, 6, 2}     // regular
    },
    { // Leopard
      {0, 0, 0, 0},    // mini
      {4, 0, 4, 1},    // small
      {5, 0, 5, 2}     // regular
    }
  }
};

// The height at which we start doing square buttons instead of rounded buttons
// Rounded buttons look bad if drawn at a height greater than 26, so at that point
// we switch over to doing square buttons which looks fine at any size.
#define DO_SQUARE_BUTTON_HEIGHT 26

void
nsNativeThemeCocoa::DrawPushButton(CGContextRef cgContext, const HIRect& inBoxRect,
                                   PRBool inDisabled, PRInt32 inState, nsIFrame* aFrame)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  BOOL isActive = FrameIsInActiveWindow(aFrame);

  [mPushButtonCell setEnabled:!inDisabled];
  [mPushButtonCell setHighlighted:((inState & NS_EVENT_STATE_ACTIVE) &&
                                   (inState & NS_EVENT_STATE_HOVER) && 
                                   isActive)];
  [mPushButtonCell setShowsFirstResponder:(inState & NS_EVENT_STATE_FOCUS) && !inDisabled && isActive];

  // If the button is tall enough, draw the square button style so that buttons with
  // non-standard content look good. Otherwise draw normal rounded aqua buttons.
  if (inBoxRect.size.height > DO_SQUARE_BUTTON_HEIGHT) {
    [mPushButtonCell setBezelStyle:NSShadowlessSquareBezelStyle];
    DrawCellWithScaling(mPushButtonCell, cgContext, inBoxRect, NSRegularControlSize,
                        NSZeroSize, NSMakeSize(14, 0), NULL,
                        mCellDrawView, IsFrameRTL(aFrame));
  } else {
    [mPushButtonCell setBezelStyle:NSRoundedBezelStyle];

    DrawCellWithSnapping(mPushButtonCell, cgContext, inBoxRect, pushButtonSettings,
                         0.5f, mCellDrawView, IsFrameRTL(aFrame), 1.0f);
  }

#if DRAW_IN_FRAME_DEBUG
  CGContextSetRGBFillColor(cgContext, 0.0, 0.0, 0.5, 0.25);
  CGContextFillRect(cgContext, inBoxRect);
#endif

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

typedef void (*RenderHIThemeControlFunction)(CGContextRef cgContext, const HIRect& aRenderRect, void* aData);

static void
RenderTransformedHIThemeControl(CGContextRef aCGContext, const HIRect& aRect,
                                RenderHIThemeControlFunction aFunc, void* aData,
                                BOOL mirrorHorizontally = NO)
{
  CGAffineTransform savedCTM = CGContextGetCTM(aCGContext);
  CGContextTranslateCTM(aCGContext, aRect.origin.x, aRect.origin.y);

  PRBool drawDirect;
  HIRect drawRect = aRect;
  drawRect.origin = CGPointZero;

  if (!mirrorHorizontally && savedCTM.a == 1.0f && savedCTM.b == 0.0f &&
      savedCTM.c == 0.0f && (savedCTM.d == 1.0f || savedCTM.d == -1.0f)) {
    drawDirect = TRUE;
  } else {
    drawDirect = FALSE;
  }

  // Fall back to no bitmap buffer if the area of our control (in pixels^2)
  // is too large.
  if (drawDirect || (aRect.size.width * aRect.size.height > BITMAP_MAX_AREA)) {
    aFunc(aCGContext, drawRect, aData);
  } else {
    // Inflate the buffer to capture focus rings.
    int w = ceil(drawRect.size.width) + 2 * MAX_FOCUS_RING_WIDTH;
    int h = ceil(drawRect.size.height) + 2 * MAX_FOCUS_RING_WIDTH;

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef bitmapctx = CGBitmapContextCreate(NULL, w, h, 8, w * 4,
                                                   colorSpace,
                                                   kCGImageAlphaPremultipliedFirst);
    CGColorSpaceRelease(colorSpace);

    CGContextTranslateCTM(bitmapctx, MAX_FOCUS_RING_WIDTH, MAX_FOCUS_RING_WIDTH);

    // HITheme always wants to draw into a flipped context, or things
    // get confused.
    CGContextTranslateCTM(bitmapctx, 0.0f, aRect.size.height);
    CGContextScaleCTM(bitmapctx, 1.0f, -1.0f);

    aFunc(bitmapctx, drawRect, aData);

    CGImageRef bitmap = CGBitmapContextCreateImage(bitmapctx);

    CGAffineTransform ctm = CGContextGetCTM(aCGContext);

    // We need to unflip, so that we can do a DrawImage without getting a flipped image.
    CGContextTranslateCTM(aCGContext, 0.0f, aRect.size.height);
    CGContextScaleCTM(aCGContext, 1.0f, -1.0f);

    if (mirrorHorizontally) {
      CGContextTranslateCTM(aCGContext, aRect.size.width, 0);
      CGContextScaleCTM(aCGContext, -1.0f, 1.0f);
    }

    HIRect inflatedDrawRect = CGRectMake(-MAX_FOCUS_RING_WIDTH, -MAX_FOCUS_RING_WIDTH, w, h);
    CGContextDrawImage(aCGContext, inflatedDrawRect, bitmap);

    CGContextSetCTM(aCGContext, ctm);

    CGImageRelease(bitmap);
    CGContextRelease(bitmapctx);
  }

  CGContextSetCTM(aCGContext, savedCTM);
}

static void
RenderButton(CGContextRef cgContext, const HIRect& aRenderRect, void* aData)
{
  HIThemeButtonDrawInfo* bdi = (HIThemeButtonDrawInfo*)aData;
  HIThemeDrawButton(&aRenderRect, bdi, cgContext, kHIThemeOrientationNormal, NULL);
}

void
nsNativeThemeCocoa::DrawButton(CGContextRef cgContext, ThemeButtonKind inKind,
                               const HIRect& inBoxRect, PRBool inIsDefault, PRBool inDisabled,
                               ThemeButtonValue inValue, ThemeButtonAdornment inAdornment,
                               PRInt32 inState, nsIFrame* aFrame)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  BOOL isActive = FrameIsInActiveWindow(aFrame);

  HIThemeButtonDrawInfo bdi;
  bdi.version = 0;
  bdi.kind = inKind;
  bdi.value = inValue;
  bdi.adornment = inAdornment;

  if (inDisabled) {
    bdi.state = kThemeStateUnavailable;
  }
  else if ((inState & NS_EVENT_STATE_ACTIVE) && (inState & NS_EVENT_STATE_HOVER)) {
    bdi.state = kThemeStatePressed;
  }
  else {
    if (inKind == kThemeArrowButton)
      bdi.state = kThemeStateUnavailable; // these are always drawn as unavailable
    else if (!isActive && inKind == kThemeListHeaderButton)
      bdi.state = kThemeStateInactive;
    else
      bdi.state = kThemeStateActive;
  }

  if (inState & NS_EVENT_STATE_FOCUS && isActive)
    bdi.adornment |= kThemeAdornmentFocus;

  if (inIsDefault && !inDisabled && isActive && !(inState & NS_EVENT_STATE_ACTIVE)) {
    bdi.adornment |= kThemeAdornmentDefault;
    bdi.animation.time.start = 0;
    bdi.animation.time.current = CFAbsoluteTimeGetCurrent();
  }

  HIRect drawFrame = inBoxRect;

  if (inKind == kThemePushButton) {
    drawFrame.size.height -= 2;
    if (inBoxRect.size.height < pushButtonSettings.naturalSizes[smallControlSize].height) {
      bdi.kind = kThemePushButtonMini;
    }
    else if (inBoxRect.size.height < pushButtonSettings.naturalSizes[regularControlSize].height) {
      bdi.kind = kThemePushButtonSmall;
      drawFrame.origin.y -= 1;
      drawFrame.origin.x += 1;
      drawFrame.size.width -= 2;
    }
  }
  else if (inKind == kThemeListHeaderButton) {
    CGContextClipToRect(cgContext, inBoxRect);
    // Always remove the top border.
    drawFrame.origin.y -= 1;
    drawFrame.size.height += 1;
    // Remove the left border in LTR mode and the right border in RTL mode.
    drawFrame.size.width += 1;
    PRBool isLast = IsLastTreeHeaderCell(aFrame);
    if (isLast)
      drawFrame.size.width += 1; // Also remove the other border.
    if (!IsFrameRTL(aFrame) || isLast)
      drawFrame.origin.x -= 1;
  }

  RenderTransformedHIThemeControl(cgContext, drawFrame, RenderButton, &bdi,
                                  IsFrameRTL(aFrame));

#if DRAW_IN_FRAME_DEBUG
  CGContextSetRGBFillColor(cgContext, 0.0, 0.0, 0.5, 0.25);
  CGContextFillRect(cgContext, inBoxRect);
#endif

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

static const CellRenderSettings dropdownSettings = {
  {
    NSMakeSize(0, 16), // mini
    NSMakeSize(0, 19), // small
    NSMakeSize(0, 22)  // regular
  },
  {
    NSMakeSize(18, 0), // mini
    NSMakeSize(38, 0), // small
    NSMakeSize(44, 0)  // regular
  },
  {
    { // Tiger
      {1, 1, 2, 1},    // mini
      {3, 0, 3, 1},    // small
      {3, 0, 3, 0}     // regular
    },
    { // Leopard
      {1, 1, 2, 1},    // mini
      {3, 0, 3, 1},    // small
      {3, 0, 3, 0}     // regular
    }
  }
};

static const CellRenderSettings editableMenulistSettings = {
  {
    NSMakeSize(0, 15), // mini
    NSMakeSize(0, 18), // small
    NSMakeSize(0, 21)  // regular
  },
  {
    NSMakeSize(18, 0), // mini
    NSMakeSize(38, 0), // small
    NSMakeSize(44, 0)  // regular
  },
  {
    { // Tiger
      {0, 0, 2, 2},    // mini
      {0, 0, 3, 2},    // small
      {0, 1, 3, 3}     // regular
    },
    { // Leopard
      {0, 0, 2, 2},    // mini
      {0, 0, 3, 2},    // small
      {0, 1, 3, 3}     // regular
    }
  }
};

void
nsNativeThemeCocoa::DrawDropdown(CGContextRef cgContext, const HIRect& inBoxRect,
                                 PRInt32 inState, PRUint8 aWidgetType, nsIFrame* aFrame)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  [mDropdownCell setPullsDown:(aWidgetType == NS_THEME_BUTTON)];

  BOOL isEditable = (aWidgetType == NS_THEME_DROPDOWN_TEXTFIELD);
  NSCell* cell = isEditable ? (NSCell*)mComboBoxCell : (NSCell*)mDropdownCell;

  [cell setEnabled:!IsDisabled(aFrame)];
  [cell setShowsFirstResponder:(IsFocused(aFrame) || (inState & NS_EVENT_STATE_FOCUS))];
  [cell setHighlighted:IsOpenButton(aFrame)];
  [cell setControlTint:(FrameIsInActiveWindow(aFrame) ? [NSColor currentControlTint] : NSClearControlTint)];

  const CellRenderSettings& settings = isEditable ? editableMenulistSettings : dropdownSettings;
  DrawCellWithSnapping(cell, cgContext, inBoxRect, settings,
                       0.5f, mCellDrawView, IsFrameRTL(aFrame));

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void
nsNativeThemeCocoa::DrawSpinButtons(CGContextRef cgContext, ThemeButtonKind inKind,
                                    const HIRect& inBoxRect, PRBool inDisabled,
                                    ThemeDrawState inDrawState,
                                    ThemeButtonAdornment inAdornment,
                                    PRInt32 inState, nsIFrame* aFrame)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  HIThemeButtonDrawInfo bdi;
  bdi.version = 0;
  bdi.kind = inKind;
  bdi.value = kThemeButtonOff;
  bdi.adornment = inAdornment;

  if (inDisabled)
    bdi.state = kThemeStateUnavailable;
  else
    bdi.state = FrameIsInActiveWindow(aFrame) ? inDrawState : kThemeStateActive;

  HIThemeDrawButton(&inBoxRect, &bdi, cgContext, HITHEME_ORIENTATION, NULL);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void
nsNativeThemeCocoa::DrawFrame(CGContextRef cgContext, HIThemeFrameKind inKind,
                              const HIRect& inBoxRect, PRBool inIsDisabled, PRInt32 inState)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  HIThemeFrameDrawInfo fdi;
  fdi.version = 0;
  fdi.kind = inKind;

  // We don't ever set an inactive state for this because it doesn't
  // look right (see other apps).
  fdi.state = inIsDisabled ? kThemeStateUnavailable : kThemeStateActive;

  // for some reason focus rings on listboxes draw incorrectly
  if (inKind == kHIThemeFrameListBox)
    fdi.isFocused = 0;
  else
    fdi.isFocused = (inState & NS_EVENT_STATE_FOCUS) != 0;

  // HIThemeDrawFrame takes the rect for the content area of the frame, not
  // the bounding rect for the frame. Here we reduce the size of the rect we
  // will pass to make it the size of the content.
  HIRect drawRect = inBoxRect;
  if (inKind == kHIThemeFrameTextFieldSquare) {
    SInt32 frameOutset = 0;
    ::GetThemeMetric(kThemeMetricEditTextFrameOutset, &frameOutset);
    drawRect.origin.x += frameOutset;
    drawRect.origin.y += frameOutset;
    drawRect.size.width -= frameOutset * 2;
    drawRect.size.height -= frameOutset * 2;
  }
  else if (inKind == kHIThemeFrameListBox) {
    SInt32 frameOutset = 0;
    ::GetThemeMetric(kThemeMetricListBoxFrameOutset, &frameOutset);
    drawRect.origin.x += frameOutset;
    drawRect.origin.y += frameOutset;
    drawRect.size.width -= frameOutset * 2;
    drawRect.size.height -= frameOutset * 2;
  }

#if DRAW_IN_FRAME_DEBUG
  CGContextSetRGBFillColor(cgContext, 0.0, 0.0, 0.5, 0.25);
  CGContextFillRect(cgContext, inBoxRect);
#endif

  HIThemeDrawFrame(&drawRect, &fdi, cgContext, HITHEME_ORIENTATION);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

static void
RenderProgress(CGContextRef cgContext, const HIRect& aRenderRect, void* aData)
{
  HIThemeTrackDrawInfo* tdi = (HIThemeTrackDrawInfo*)aData;
  tdi->bounds = aRenderRect;
  HIThemeDrawTrack(tdi, NULL, cgContext, kHIThemeOrientationNormal);
}

void
nsNativeThemeCocoa::DrawProgress(CGContextRef cgContext, const HIRect& inBoxRect,
                                 PRBool inIsIndeterminate, PRBool inIsHorizontal,
                                 PRInt32 inValue, PRInt32 inMaxValue,
                                 nsIFrame* aFrame)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  HIThemeTrackDrawInfo tdi;

  PRInt32 stepsPerSecond = inIsIndeterminate ? 60 : 30;
  PRInt32 milliSecondsPerStep = 1000 / stepsPerSecond;

  tdi.version = 0;
  tdi.kind = inIsIndeterminate ? kThemeMediumIndeterminateBar: kThemeMediumProgressBar;
  tdi.bounds = inBoxRect;
  tdi.min = 0;
  tdi.max = inMaxValue;
  tdi.value = inValue;
  tdi.attributes = inIsHorizontal ? kThemeTrackHorizontal : 0;
  tdi.enableState = FrameIsInActiveWindow(aFrame) ? kThemeTrackActive : kThemeTrackInactive;
  tdi.trackInfo.progress.phase = PR_IntervalToMilliseconds(PR_IntervalNow()) /
                                 milliSecondsPerStep % 16;

  RenderTransformedHIThemeControl(cgContext, inBoxRect, RenderProgress, &tdi,
                                  IsFrameRTL(aFrame));

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void
nsNativeThemeCocoa::DrawTabPanel(CGContextRef cgContext, const HIRect& inBoxRect,
                                 nsIFrame* aFrame)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  HIThemeTabPaneDrawInfo tpdi;

  tpdi.version = 1;
  tpdi.state = FrameIsInActiveWindow(aFrame) ? kThemeStateActive : kThemeStateInactive;
  tpdi.direction = kThemeTabNorth;
  tpdi.size = kHIThemeTabSizeNormal;
  tpdi.kind = kHIThemeTabKindNormal;

  HIThemeDrawTabPane(&inBoxRect, &tpdi, cgContext, HITHEME_ORIENTATION);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

void
nsNativeThemeCocoa::DrawScale(CGContextRef cgContext, const HIRect& inBoxRect,
                              PRBool inIsDisabled, PRInt32 inState,
                              PRBool inIsVertical, PRBool inIsReverse,
                              PRInt32 inCurrentValue, PRInt32 inMinValue,
                              PRInt32 inMaxValue, nsIFrame* aFrame)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  HIThemeTrackDrawInfo tdi;

  tdi.version = 0;
  tdi.kind = kThemeMediumSlider;
  tdi.bounds = inBoxRect;
  tdi.min = inMinValue;
  tdi.max = inMaxValue;
  tdi.value = inCurrentValue;
  tdi.attributes = kThemeTrackShowThumb;
  if (!inIsVertical)
    tdi.attributes |= kThemeTrackHorizontal;
  if (inIsReverse)
    tdi.attributes |= kThemeTrackRightToLeft;
  if (inState & NS_EVENT_STATE_FOCUS)
    tdi.attributes |= kThemeTrackHasFocus;
  if (inIsDisabled)
    tdi.enableState = kThemeTrackDisabled;
  else
    tdi.enableState = FrameIsInActiveWindow(aFrame) ? kThemeTrackActive : kThemeTrackInactive;
  tdi.trackInfo.slider.thumbDir = kThemeThumbPlain;
  tdi.trackInfo.slider.pressState = 0;

  HIThemeDrawTrack(&tdi, NULL, cgContext, HITHEME_ORIENTATION);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

#define NATURAL_MINI_TAB_BUTTON_HEIGHT    17
#define NATURAL_SMALL_TAB_BUTTON_HEIGHT   20
#define NATURAL_REGULAR_TAB_BUTTON_HEIGHT 23

void
nsNativeThemeCocoa::DrawTab(CGContextRef cgContext, HIRect inBoxRect,
                            PRInt32 inState, nsIFrame* aFrame)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  HIThemeTabDrawInfo tdi;
  tdi.version = 1;
  tdi.kind = kHIThemeTabKindNormal;

  PRBool isSelected = IsSelectedTab(aFrame);
  PRBool isDisabled = IsDisabled(aFrame);
  if (isSelected) {
    if (isDisabled) 
      tdi.style = kThemeTabFrontUnavailable;
    else
      tdi.style = FrameIsInActiveWindow(aFrame) ? kThemeTabFront : kThemeTabFrontInactive;
  } else {
    if (isDisabled)
      tdi.style = kThemeTabNonFrontUnavailable;
    else if ((inState & NS_EVENT_STATE_ACTIVE) && (inState & NS_EVENT_STATE_HOVER))
      tdi.style = kThemeTabNonFrontPressed;
    else
      tdi.style = FrameIsInActiveWindow(aFrame) ? kThemeTabNonFront : kThemeTabNonFrontInactive;
  }

  tdi.direction = kThemeTabNorth;
  tdi.size = kHIThemeTabSizeNormal;
  if (inBoxRect.size.height < NATURAL_REGULAR_TAB_BUTTON_HEIGHT)
    tdi.size = kHIThemeTabSizeSmall;
  if (inBoxRect.size.height < NATURAL_SMALL_TAB_BUTTON_HEIGHT)
    tdi.size = kHIThemeTabSizeMini;

  PRBool isRTL = IsFrameRTL(aFrame);
  PRBool isFirst = isRTL ? IsLastTab(aFrame) : IsFirstTab(aFrame);
  PRBool isLast = isRTL ? IsFirstTab(aFrame) : IsLastTab(aFrame);

  if (isFirst && isLast)
    tdi.position = kHIThemeTabPositionOnly;
  else if (isFirst)
    tdi.position = kHIThemeTabPositionFirst;
  else if (isLast)
    tdi.position = kHIThemeTabPositionLast;
  else
    tdi.position = kHIThemeTabPositionMiddle;

  // Tab separator management:
  // Normal tabs only draw their left separator, in the leftmost pixel row of
  // their frame. Selected tabs additionally draw their right separator, outside
  // of their frame. To prevent overlapping, the tab to the right of the
  // selected tab shouldn't draw its left separator.
  tdi.adornment = kHIThemeTabAdornmentNone;
  if (isRTL ? IsBeforeSelectedTab(aFrame) : IsAfterSelectedTab(aFrame)) {
    // On Leopard, the tab's left edge must be shifted 1px to the right.
    // On Tiger, this happens automatically when no leading separator is drawn.
    inBoxRect.origin.x += 1;
    inBoxRect.size.width -= 1;
  }
  else {
    tdi.adornment = kHIThemeTabAdornmentLeadingSeparator;
  }

  if (isSelected && !isLast) {
    tdi.adornment |= kHIThemeTabAdornmentTrailingSeparator;
    // On Tiger, the right separator is drawn outside of the frame.
    // On Leopard, the right edge must be shifted 1px to the right.
    inBoxRect.size.width += 1;
  }
  
  if (inState & NS_EVENT_STATE_FOCUS)
    tdi.adornment |= kThemeAdornmentFocus;

  HIThemeDrawTab(&inBoxRect, &tdi, cgContext, HITHEME_ORIENTATION, NULL);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

static inline UInt8
ConvertToPressState(PRInt32 aButtonState, UInt8 aPressState)
{
  // If the button is pressed, return the press state passed in. Otherwise, return 0.
  return ((aButtonState & NS_EVENT_STATE_ACTIVE) && (aButtonState & NS_EVENT_STATE_HOVER)) ? aPressState : 0;
}

void 
nsNativeThemeCocoa::GetScrollbarPressStates(nsIFrame *aFrame, PRInt32 aButtonStates[])
{
  static nsIContent::AttrValuesArray attributeValues[] = {
    &nsWidgetAtoms::scrollbarUpTop,
    &nsWidgetAtoms::scrollbarDownTop,
    &nsWidgetAtoms::scrollbarUpBottom,
    &nsWidgetAtoms::scrollbarDownBottom,
    nsnull
  };

  // Get the state of any scrollbar buttons in our child frames
  for (nsIFrame *childFrame = aFrame->GetFirstChild(nsnull); 
       childFrame;
       childFrame = childFrame->GetNextSibling()) {

    nsIContent *childContent = childFrame->GetContent();
    if (!childContent) continue;
    PRInt32 attrIndex = childContent->FindAttrValueIn(kNameSpaceID_None, nsWidgetAtoms::sbattr, 
                                                      attributeValues, eCaseMatters);
    if (attrIndex < 0) continue;

    PRInt32 currentState = GetContentState(childFrame, NS_THEME_BUTTON);
    aButtonStates[attrIndex] = currentState;
  }
}

// Both of the following sets of numbers were derived by loading the testcase in
// bmo bug 380185 in Safari and observing its behavior for various heights of scrollbar.
// These magic numbers are the minimum sizes we can draw a scrollbar and still 
// have room for everything to display, including the thumb
#define MIN_SCROLLBAR_SIZE_WITH_THUMB 61
#define MIN_SMALL_SCROLLBAR_SIZE_WITH_THUMB 49
// And these are the minimum sizes if we don't draw the thumb
#define MIN_SCROLLBAR_SIZE 56
#define MIN_SMALL_SCROLLBAR_SIZE 46

void
nsNativeThemeCocoa::GetScrollbarDrawInfo(HIThemeTrackDrawInfo& aTdi, nsIFrame *aFrame, 
                                         const CGSize& aSize, PRBool aShouldGetButtonStates)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  PRInt32 curpos = CheckIntAttr(aFrame, nsWidgetAtoms::curpos, 0);
  PRInt32 minpos = CheckIntAttr(aFrame, nsWidgetAtoms::minpos, 0);
  PRInt32 maxpos = CheckIntAttr(aFrame, nsWidgetAtoms::maxpos, 100);
  PRInt32 thumbSize = CheckIntAttr(aFrame, nsWidgetAtoms::pageincrement, 10);

  PRBool isHorizontal = aFrame->GetContent()->AttrValueIs(kNameSpaceID_None, nsWidgetAtoms::orient, 
                                                          nsWidgetAtoms::horizontal, eCaseMatters);
  PRBool isSmall = aFrame->GetStyleDisplay()->mAppearance == NS_THEME_SCROLLBAR_SMALL;

  aTdi.version = 0;
  aTdi.kind = isSmall ? kThemeSmallScrollBar : kThemeMediumScrollBar;
  aTdi.bounds.origin = CGPointZero;
  aTdi.bounds.size = aSize;
  aTdi.min = minpos;
  aTdi.max = maxpos;
  aTdi.value = curpos;
  aTdi.attributes = 0;
  aTdi.enableState = kThemeTrackActive;
  if (isHorizontal)
    aTdi.attributes |= kThemeTrackHorizontal;

  aTdi.trackInfo.scrollbar.viewsize = (SInt32)thumbSize;

  // This should be done early on so things like "kThemeTrackNothingToScroll" can
  // override the active enable state.
  aTdi.enableState = FrameIsInActiveWindow(aFrame) ? kThemeTrackActive : kThemeTrackInactive;

  /* Only display features if we have enough room for them.
   * Gecko still maintains the scrollbar info; this is just a visual issue (bug 380185).
   */
  PRInt32 longSideLength = (PRInt32)(isHorizontal ? (aSize.width) : (aSize.height));
  if (longSideLength >= (isSmall ? MIN_SMALL_SCROLLBAR_SIZE_WITH_THUMB : MIN_SCROLLBAR_SIZE_WITH_THUMB)) {
    aTdi.attributes |= kThemeTrackShowThumb;
  }
  else if (longSideLength < (isSmall ? MIN_SMALL_SCROLLBAR_SIZE : MIN_SCROLLBAR_SIZE)) {
    aTdi.enableState = kThemeTrackNothingToScroll;
    return;
  }

  aTdi.trackInfo.scrollbar.pressState = 0;

  // Only go get these scrollbar button states if we need it. For example, there's no reaon to look up scrollbar button 
  // states when we're only creating a TrackDrawInfo to determine the size of the thumb.
  if (aShouldGetButtonStates) {
    PRInt32 buttonStates[] = {0, 0, 0, 0};
    GetScrollbarPressStates(aFrame, buttonStates);
    NSString *buttonPlacement = [[NSUserDefaults standardUserDefaults] objectForKey:@"AppleScrollBarVariant"];
    // It seems that unless all four buttons are showing, kThemeTopOutsideArrowPressed is the correct constant for
    // the up scrollbar button.
    if ([buttonPlacement isEqualToString:@"DoubleBoth"]) {
      aTdi.trackInfo.scrollbar.pressState = ConvertToPressState(buttonStates[0], kThemeTopOutsideArrowPressed) |
                                            ConvertToPressState(buttonStates[1], kThemeTopInsideArrowPressed) |
                                            ConvertToPressState(buttonStates[2], kThemeBottomInsideArrowPressed) |
                                            ConvertToPressState(buttonStates[3], kThemeBottomOutsideArrowPressed);
    } else {
      aTdi.trackInfo.scrollbar.pressState = ConvertToPressState(buttonStates[0], kThemeTopOutsideArrowPressed) |
                                            ConvertToPressState(buttonStates[1], kThemeBottomOutsideArrowPressed) |
                                            ConvertToPressState(buttonStates[2], kThemeTopOutsideArrowPressed) |
                                            ConvertToPressState(buttonStates[3], kThemeBottomOutsideArrowPressed);
    }
  }

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

static void
RenderScrollbar(CGContextRef cgContext, const HIRect& aRenderRect, void* aData)
{
  HIThemeTrackDrawInfo* tdi = (HIThemeTrackDrawInfo*)aData;
  HIThemeDrawTrack(tdi, NULL, cgContext, HITHEME_ORIENTATION);
}

void
nsNativeThemeCocoa::DrawScrollbar(CGContextRef aCGContext, const HIRect& aBoxRect, nsIFrame *aFrame)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  HIThemeTrackDrawInfo tdi;
  GetScrollbarDrawInfo(tdi, aFrame, aBoxRect.size, PR_TRUE); // True means we want the press states
  RenderTransformedHIThemeControl(aCGContext, aBoxRect, RenderScrollbar, &tdi);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

nsIFrame*
nsNativeThemeCocoa::GetParentScrollbarFrame(nsIFrame *aFrame)
{
  // Walk our parents to find a scrollbar frame
  nsIFrame *scrollbarFrame = aFrame;
  do {
    if (scrollbarFrame->GetType() == nsWidgetAtoms::scrollbarFrame) break;
  } while ((scrollbarFrame = scrollbarFrame->GetParent()));
  
  // We return null if we can't find a parent scrollbar frame
  return scrollbarFrame;
}

static PRBool
ToolbarCanBeUnified(CGContextRef cgContext, const HIRect& inBoxRect, NSWindow* aWindow)
{
  if (![aWindow isKindOfClass:[ToolbarWindow class]] ||
      [(ToolbarWindow*)aWindow drawsContentsIntoWindowFrame])
    return PR_FALSE;

  float unifiedToolbarHeight = [(ToolbarWindow*)aWindow unifiedToolbarHeight];
  return inBoxRect.origin.x == 0 &&
         inBoxRect.size.width == [aWindow frame].size.width &&
         inBoxRect.origin.y <= 0.0 &&
         inBoxRect.origin.y + inBoxRect.size.height <= unifiedToolbarHeight;
}

void
nsNativeThemeCocoa::DrawUnifiedToolbar(CGContextRef cgContext, const HIRect& inBoxRect,
                                       NSWindow* aWindow)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  float titlebarHeight = [(ToolbarWindow*)aWindow titlebarHeight];

  BOOL isMain = [aWindow isMainWindow] || ![NSView focusView];

  // Draw the gradient
  UnifiedGradientInfo info = { titlebarHeight, inBoxRect.size.height, isMain, NO };
  struct CGFunctionCallbacks callbacks = { 0, nsCocoaWindow::UnifiedShading, NULL };
  CGFunctionRef function = CGFunctionCreate(&info, 1,  NULL, 4, NULL, &callbacks);
  float srcY = inBoxRect.origin.y;
  float dstY = srcY + inBoxRect.size.height - 1;
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
  CGShadingRef shading = CGShadingCreateAxial(colorSpace,
                                              CGPointMake(0, srcY),
                                              CGPointMake(0, dstY), function,
                                              NO, NO);
  CGColorSpaceRelease(colorSpace);
  CGFunctionRelease(function);
  CGContextClipToRect(cgContext, inBoxRect);
  CGContextDrawShading(cgContext, shading);
  CGShadingRelease(shading);

  // Draw the border at the bottom of the toolbar.
  CGRect borderRect = CGRectMake(inBoxRect.origin.x, inBoxRect.origin.y +
                                 inBoxRect.size.height - 1.0f,
                                 inBoxRect.size.width, 1.0f);
  DrawNativeGreyColorInRect(cgContext, headerBorderGrey, borderRect, isMain);

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

struct GreyGradientInfo {
  float startGrey;
  float endGrey;
};

static void GreyGradientCallback(void* aInfo, const CGFloat* aIn, CGFloat* aOut)
{
  GreyGradientInfo* info = static_cast<GreyGradientInfo*>(aInfo);
  CGFloat result = (1.0f - *aIn) * info->startGrey + *aIn * info->endGrey;
  aOut[0] = result;
  aOut[1] = result;
  aOut[2] = result;
  aOut[3] = 1.0f;
}

static void DrawGreyGradient(CGContextRef cgContext, const HIRect& rect,
                             float startGrey, float endGrey)
{
  if (rect.size.height <= 0.0f)
    return;

  GreyGradientInfo info = { startGrey, endGrey };
  struct CGFunctionCallbacks callbacks = { 0, GreyGradientCallback, NULL };
  CGFunctionRef function = CGFunctionCreate(&info, 1,  NULL, 4, NULL, &callbacks);
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
  CGShadingRef shading = CGShadingCreateAxial(colorSpace,
                                              CGPointMake(0, CGRectGetMinY(rect)),
                                              CGPointMake(0, CGRectGetMaxY(rect)),
                                              function, false, false);
  CGColorSpaceRelease(colorSpace);
  CGFunctionRelease(function);
  CGContextSaveGState(cgContext);
  CGContextClipToRect(cgContext, rect);
  CGContextDrawShading(cgContext, shading);
  CGContextRestoreGState(cgContext);
  CGShadingRelease(shading);
}

void
nsNativeThemeCocoa::DrawStatusBar(CGContextRef cgContext, const HIRect& inBoxRect,
                                  nsIFrame *aFrame)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  if (inBoxRect.size.height < 2.0f)
    return;

  BOOL isMain = [NativeWindowForFrame(aFrame) isMainWindow] || ![NSView focusView];

  // Draw the borders at the top of the statusbar.
  CGRect rect = CGRectMake(inBoxRect.origin.x, inBoxRect.origin.y,
                           inBoxRect.size.width, 1.0f);
  DrawNativeGreyColorInRect(cgContext, statusbarFirstTopBorderGrey, rect, isMain);
  rect.origin.y += 1.0f;
  DrawNativeGreyColorInRect(cgContext, statusbarSecondTopBorderGrey, rect, isMain);

  // Draw the gradient.
  DrawGreyGradient(cgContext, CGRectMake(inBoxRect.origin.x, inBoxRect.origin.y + 2.0f,
                                         inBoxRect.size.width, inBoxRect.size.height - 2.0f),
                   NativeGreyColorAsFloat(statusbarGradientStartGrey, isMain),
                   NativeGreyColorAsFloat(statusbarGradientEndGrey, isMain));

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

static void
RenderResizer(CGContextRef cgContext, const HIRect& aRenderRect, void* aData)
{
  HIThemeGrowBoxDrawInfo* drawInfo = (HIThemeGrowBoxDrawInfo*)aData;
  HIThemeDrawGrowBox(&CGPointZero, drawInfo, cgContext, kHIThemeOrientationNormal);
}

void
nsNativeThemeCocoa::DrawResizer(CGContextRef cgContext, const HIRect& aRect,
                                nsIFrame *aFrame)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  HIThemeGrowBoxDrawInfo drawInfo;
  drawInfo.version = 0;
  drawInfo.state = kThemeStateActive;
  drawInfo.kind = kHIThemeGrowBoxKindNormal;
  drawInfo.direction = kThemeGrowRight | kThemeGrowDown;
  drawInfo.size = kHIThemeGrowBoxSizeNormal;

  RenderTransformedHIThemeControl(cgContext, aRect, RenderResizer, &drawInfo,
                                  IsFrameRTL(aFrame));

  NS_OBJC_END_TRY_ABORT_BLOCK;
}

static PRBool
IsWindowSpanningToolbar(nsIWidget* aWindow,
                        PRUint8 aWidgetType,
                        const nsIntRect& aRect,
                        ToolbarWindow** aCocoaWindow)
{
  nsIWidget* topLevelWidget = aWindow->GetTopLevelWidget();
  if (!topLevelWidget)
    return PR_FALSE;
  NSWindow* win = (NSWindow*)topLevelWidget->GetNativeData(NS_NATIVE_WINDOW);
  if (!win || ![win isKindOfClass:[ToolbarWindow class]])
    return PR_FALSE;

  *aCocoaWindow = (ToolbarWindow*)win;
  return (aWidgetType == NS_THEME_MOZ_MAC_UNIFIED_TOOLBAR ||
          aWidgetType == NS_THEME_TOOLBAR) &&
         aRect.x == 0 && aRect.width == [win frame].size.width;
}

NS_IMETHODIMP
nsNativeThemeCocoa::DrawWidgetBackground(nsIRenderingContext* aContext, nsIFrame* aFrame,
                                         PRUint8 aWidgetType, const nsRect& aRect,
                                         const nsRect& aDirtyRect)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  // setup to draw into the correct port
  nsCOMPtr<nsIDeviceContext> dctx;
  aContext->GetDeviceContext(*getter_AddRefs(dctx));
  PRInt32 p2a = dctx->AppUnitsPerDevPixel();

  gfxRect nativeDirtyRect(aDirtyRect.x, aDirtyRect.y, aDirtyRect.width, aDirtyRect.height);
  gfxRect nativeWidgetRect(aRect.x, aRect.y, aRect.width, aRect.height);
  nativeWidgetRect.ScaleInverse(gfxFloat(p2a));
  nativeDirtyRect.ScaleInverse(gfxFloat(p2a));
  nativeWidgetRect.Round();
  if (nativeWidgetRect.IsEmpty())
    return NS_OK; // Don't attempt to draw invisible widgets.

  nsRefPtr<gfxContext> thebesCtx = aContext->ThebesContext();
  if (!thebesCtx)
    return NS_ERROR_FAILURE;

  gfxQuartzNativeDrawing nativeDrawing(thebesCtx, nativeDirtyRect);

  CGContextRef cgContext = nativeDrawing.BeginNativeDrawing();
  if (cgContext == nsnull) {
    // The Quartz surface handles 0x0 surfaces by internally
    // making all operations no-ops; there's no cgcontext created for them.
    // Unfortunately, this means that callers that want to render
    // directly to the CGContext need to be aware of this quirk.
    return NS_OK;
  }

#if 0
  if (1 /*aWidgetType == NS_THEME_TEXTFIELD*/) {
    fprintf(stderr, "Native theme drawing widget %d [%p] dis:%d in rect [%d %d %d %d]\n",
            aWidgetType, aFrame, IsDisabled(aFrame), aRect.x, aRect.y, aRect.width, aRect.height);
    fprintf(stderr, "Cairo matrix: [%f %f %f %f %f %f]\n",
            mat.xx, mat.yx, mat.xy, mat.yy, mat.x0, mat.y0);
    fprintf(stderr, "Native theme xform[0]: [%f %f %f %f %f %f]\n",
            mm0.a, mm0.b, mm0.c, mm0.d, mm0.tx, mm0.ty);
    CGAffineTransform mm = CGContextGetCTM(cgContext);
    fprintf(stderr, "Native theme xform[1]: [%f %f %f %f %f %f]\n",
            mm.a, mm.b, mm.c, mm.d, mm.tx, mm.ty);
  }
#endif

  CGRect macRect = CGRectMake(nativeWidgetRect.X(), nativeWidgetRect.Y(),
                              nativeWidgetRect.Width(), nativeWidgetRect.Height());

#if 0
  fprintf(stderr, "    --> macRect %f %f %f %f\n",
          macRect.origin.x, macRect.origin.y, macRect.size.width, macRect.size.height);
  CGRect bounds = CGContextGetClipBoundingBox(cgContext);
  fprintf(stderr, "    --> clip bounds: %f %f %f %f\n",
          bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height);

  //CGContextSetRGBFillColor(cgContext, 0.0, 0.0, 1.0, 0.1);
  //CGContextFillRect(cgContext, bounds);
#endif

  PRInt32 eventState = GetContentState(aFrame, aWidgetType);

  switch (aWidgetType) {
    case NS_THEME_DIALOG: {
      HIThemeSetFill(kThemeBrushDialogBackgroundActive, NULL, cgContext, HITHEME_ORIENTATION);
      CGContextFillRect(cgContext, macRect);
    }
      break;

    case NS_THEME_MENUPOPUP: {
      HIThemeMenuDrawInfo mdi = {
        version: 0,
        menuType: IsDisabled(aFrame) ? kThemeMenuTypeInactive : kThemeMenuTypePopUp
      };

      PRBool isLeftOfParent = PR_FALSE;
      if (IsSubmenu(aFrame, &isLeftOfParent) && !isLeftOfParent) {
        mdi.menuType = kThemeMenuTypeHierarchical;
      }
      
      // The rounded corners draw outside the frame.
      CGRect deflatedRect = CGRectMake(macRect.origin.x, macRect.origin.y + 4,
                                       macRect.size.width, macRect.size.height - 8);
      HIThemeDrawMenuBackground(&deflatedRect, &mdi, cgContext, HITHEME_ORIENTATION);
    }
      break;

    case NS_THEME_MENUITEM: {
      // Clear the background to get correct transparency.
      CGContextClearRect(cgContext, macRect);

      // maybe use kThemeMenuItemHierBackground or PopUpBackground instead of just Plain?
      HIThemeMenuItemDrawInfo drawInfo = {
        version: 0,
        itemType: kThemeMenuItemPlain,
        state: (IsDisabled(aFrame) ? kThemeMenuDisabled :
                CheckBooleanAttr(aFrame, nsWidgetAtoms::mozmenuactive) ? kThemeMenuSelected :
                kThemeMenuActive)
      };

      // XXX pass in the menu rect instead of always using the item rect
      HIRect ignored;
      HIThemeDrawMenuItem(&macRect, &macRect, &drawInfo, cgContext, HITHEME_ORIENTATION, &ignored);
    }
      break;

    case NS_THEME_MENUSEPARATOR: {
      ThemeMenuState menuState;
      if (IsDisabled(aFrame)) {
        menuState = kThemeMenuDisabled;
      }
      else {
        menuState = CheckBooleanAttr(aFrame, nsWidgetAtoms::mozmenuactive) ?
                    kThemeMenuSelected : kThemeMenuActive;
      }

      HIThemeMenuItemDrawInfo midi = { 0, kThemeMenuItemPlain, menuState };
      HIThemeDrawMenuSeparator(&macRect, &macRect, &midi, cgContext, HITHEME_ORIENTATION);
    }
      break;

    case NS_THEME_TOOLTIP:
      CGContextSetRGBFillColor(cgContext, 0.996, 1.000, 0.792, 0.950);
      CGContextFillRect(cgContext, macRect);
      break;

    case NS_THEME_CHECKBOX:
    case NS_THEME_RADIO: {
      PRBool isCheckbox = (aWidgetType == NS_THEME_CHECKBOX);
      DrawCheckboxOrRadio(cgContext, isCheckbox, macRect, GetCheckedOrSelected(aFrame, !isCheckbox),
                          IsDisabled(aFrame), eventState, aFrame);
    }
      break;

    case NS_THEME_BUTTON:
      if (IsDefaultButton(aFrame)) {
        DrawButton(cgContext, kThemePushButton, macRect, true, IsDisabled(aFrame), 
                   kThemeButtonOff, kThemeAdornmentNone, eventState, aFrame);
      } else if (IsButtonTypeMenu(aFrame)) {
        DrawDropdown(cgContext, macRect, eventState, aWidgetType, aFrame);
      } else {
        DrawPushButton(cgContext, macRect, IsDisabled(aFrame), eventState, aFrame);
      }
      break;

    case NS_THEME_BUTTON_BEVEL:
      DrawButton(cgContext, kThemeMediumBevelButton, macRect,
                 IsDefaultButton(aFrame), IsDisabled(aFrame), 
                 kThemeButtonOff, kThemeAdornmentNone, eventState, aFrame);
      break;

    case NS_THEME_SPINNER: {
      ThemeDrawState state = kThemeStateActive;
      nsIContent* content = aFrame->GetContent();
      if (content->AttrValueIs(kNameSpaceID_None, nsWidgetAtoms::state,
                               NS_LITERAL_STRING("up"), eCaseMatters)) {
        state = kThemeStatePressedUp;
      }
      else if (content->AttrValueIs(kNameSpaceID_None, nsWidgetAtoms::state,
                                    NS_LITERAL_STRING("down"), eCaseMatters)) {
        state = kThemeStatePressedDown;
      }

      DrawSpinButtons(cgContext, kThemeIncDecButton, macRect, IsDisabled(aFrame),
                      state, kThemeAdornmentNone, eventState, aFrame);
    }
      break;

    case NS_THEME_TOOLBAR_BUTTON:
      DrawButton(cgContext, kThemePushButton, macRect,
                 IsDefaultButton(aFrame), IsDisabled(aFrame),
                 kThemeButtonOn, kThemeAdornmentNone, eventState, aFrame);
      break;

    case NS_THEME_TOOLBAR_SEPARATOR: {
      HIThemeSeparatorDrawInfo sdi = { 0, kThemeStateActive };
      HIThemeDrawSeparator(&macRect, &sdi, cgContext, HITHEME_ORIENTATION);
    }
      break;

    case NS_THEME_MOZ_MAC_UNIFIED_TOOLBAR:
    case NS_THEME_TOOLBAR: {
      NSWindow* win = NativeWindowForFrame(aFrame);
      if (ToolbarCanBeUnified(cgContext, macRect, win)) {
        DrawUnifiedToolbar(cgContext, macRect, win);
        break;
      }
      BOOL isMain = [win isMainWindow] || ![NSView focusView];
      CGRect drawRect = macRect;

      // top border
      drawRect.size.height = 1.0f;
      DrawNativeGreyColorInRect(cgContext, toolbarTopBorderGrey, drawRect, isMain);

      // background
      drawRect.origin.y += drawRect.size.height;
      drawRect.size.height = macRect.size.height - 2.0f;
      DrawNativeGreyColorInRect(cgContext, headerEndGrey, drawRect, isMain);

      // bottom border
      drawRect.origin.y += drawRect.size.height;
      drawRect.size.height = 1.0f;
      DrawNativeGreyColorInRect(cgContext, headerBorderGrey, drawRect, isMain);
    }
      break;

    case NS_THEME_TOOLBOX: {
      HIThemeHeaderDrawInfo hdi = { 0, kThemeStateActive, kHIThemeHeaderKindWindow };
      HIThemeDrawHeader(&macRect, &hdi, cgContext, HITHEME_ORIENTATION);
    }
      break;

    case NS_THEME_STATUSBAR: 
      DrawStatusBar(cgContext, macRect, aFrame);
      break;

    case NS_THEME_DROPDOWN:
    case NS_THEME_DROPDOWN_TEXTFIELD:
      DrawDropdown(cgContext, macRect, eventState, aWidgetType, aFrame);
      break;

    case NS_THEME_DROPDOWN_BUTTON:
      DrawButton(cgContext, kThemeArrowButton, macRect, PR_FALSE,
                 IsDisabled(aFrame), kThemeButtonOn,
                 kThemeAdornmentArrowDownArrow, eventState, aFrame);
      break;

    case NS_THEME_GROUPBOX: {
      HIThemeGroupBoxDrawInfo gdi = { 0, kThemeStateActive, kHIThemeGroupBoxKindPrimary };
      HIThemeDrawGroupBox(&macRect, &gdi, cgContext, HITHEME_ORIENTATION);
      break;
    }

    case NS_THEME_TEXTFIELD:
      // HIThemeSetFill is not available on 10.3
      CGContextSetRGBFillColor(cgContext, 1.0, 1.0, 1.0, 1.0);
      CGContextFillRect(cgContext, macRect);

      // XUL textboxes set the native appearance on the containing box, while
      // concrete focus is set on the html:input element within it. We can
      // though, check the focused attribute of xul textboxes in this case.
      // On Mac, focus rings are always shown for textboxes, so we do not need
      // to check the window's focus ring state here
      if (aFrame->GetContent()->IsXUL() && IsFocused(aFrame)) {
        eventState |= NS_EVENT_STATE_FOCUS;
      }

      DrawFrame(cgContext, kHIThemeFrameTextFieldSquare,
                macRect, (IsDisabled(aFrame) || IsReadOnly(aFrame)), eventState);
      break;
      
    case NS_THEME_SEARCHFIELD:
      DrawSearchField(cgContext, macRect, aFrame);
      break;

    case NS_THEME_PROGRESSBAR:
      DrawProgress(cgContext, macRect, IsIndeterminateProgress(aFrame),
                   PR_TRUE, GetProgressValue(aFrame),
                   GetProgressMaxValue(aFrame), aFrame);
      break;

    case NS_THEME_PROGRESSBAR_VERTICAL:
      DrawProgress(cgContext, macRect, IsIndeterminateProgress(aFrame),
                   PR_FALSE, GetProgressValue(aFrame),
                   GetProgressMaxValue(aFrame), aFrame);
      break;

    case NS_THEME_PROGRESSBAR_CHUNK:
    case NS_THEME_PROGRESSBAR_CHUNK_VERTICAL:
      // do nothing, covered by the progress bar cases above
      break;

    case NS_THEME_TREEVIEW_TWISTY:
      DrawButton(cgContext, kThemeDisclosureButton, macRect, PR_FALSE, IsDisabled(aFrame), 
                 kThemeDisclosureRight, kThemeAdornmentNone, eventState, aFrame);
      break;

    case NS_THEME_TREEVIEW_TWISTY_OPEN:
      DrawButton(cgContext, kThemeDisclosureButton, macRect, PR_FALSE, IsDisabled(aFrame), 
                 kThemeDisclosureDown, kThemeAdornmentNone, eventState, aFrame);
      break;

    case NS_THEME_TREEVIEW_HEADER_CELL: {
      TreeSortDirection sortDirection = GetTreeSortDirection(aFrame);
      DrawButton(cgContext, kThemeListHeaderButton, macRect, PR_FALSE, IsDisabled(aFrame), 
                 sortDirection == eTreeSortDirection_Natural ? kThemeButtonOff : kThemeButtonOn,
                 sortDirection == eTreeSortDirection_Ascending ?
                 kThemeAdornmentHeaderButtonSortUp : kThemeAdornmentNone, eventState, aFrame);      
    }
      break;

    case NS_THEME_TREEVIEW_TREEITEM:
    case NS_THEME_TREEVIEW:
      // HIThemeSetFill is not available on 10.3
      // HIThemeSetFill(kThemeBrushWhite, NULL, cgContext, HITHEME_ORIENTATION);
      CGContextSetRGBFillColor(cgContext, 1.0, 1.0, 1.0, 1.0);
      CGContextFillRect(cgContext, macRect);
      break;

    case NS_THEME_TREEVIEW_HEADER:
      // do nothing, taken care of by individual header cells
    case NS_THEME_TREEVIEW_HEADER_SORTARROW:
      // do nothing, taken care of by treeview header
    case NS_THEME_TREEVIEW_LINE:
      // do nothing, these lines don't exist on macos
      break;

    case NS_THEME_SCALE_HORIZONTAL:
    case NS_THEME_SCALE_VERTICAL: {
      PRInt32 curpos = CheckIntAttr(aFrame, nsWidgetAtoms::curpos, 0);
      PRInt32 minpos = CheckIntAttr(aFrame, nsWidgetAtoms::minpos, 0);
      PRInt32 maxpos = CheckIntAttr(aFrame, nsWidgetAtoms::maxpos, 100);
      if (!maxpos)
        maxpos = 100;

      PRBool reverse = aFrame->GetContent()->
        AttrValueIs(kNameSpaceID_None, nsWidgetAtoms::dir,
                    NS_LITERAL_STRING("reverse"), eCaseMatters);
      DrawScale(cgContext, macRect, IsDisabled(aFrame), eventState,
                (aWidgetType == NS_THEME_SCALE_VERTICAL), reverse,
                curpos, minpos, maxpos, aFrame);
    }
      break;

    case NS_THEME_SCALE_THUMB_HORIZONTAL:
    case NS_THEME_SCALE_THUMB_VERTICAL:
      // do nothing, drawn by scale
      break;

    case NS_THEME_SCROLLBAR_SMALL:
    case NS_THEME_SCROLLBAR: {
      DrawScrollbar(cgContext, macRect, aFrame);
    }
      break;
    case NS_THEME_SCROLLBAR_THUMB_VERTICAL:
    case NS_THEME_SCROLLBAR_THUMB_HORIZONTAL:
#if SCROLLBARS_VISUAL_DEBUG
      CGContextSetRGBFillColor(cgContext, 1.0, 1.0, 0, 0.6);
      CGContextFillRect(cgContext, macRect);
    break;
#endif
    case NS_THEME_SCROLLBAR_BUTTON_UP:
    case NS_THEME_SCROLLBAR_BUTTON_LEFT:
#if SCROLLBARS_VISUAL_DEBUG
      CGContextSetRGBFillColor(cgContext, 1.0, 0, 0, 0.6);
      CGContextFillRect(cgContext, macRect);
    break;
#endif
    case NS_THEME_SCROLLBAR_BUTTON_DOWN:
    case NS_THEME_SCROLLBAR_BUTTON_RIGHT:
#if SCROLLBARS_VISUAL_DEBUG
      CGContextSetRGBFillColor(cgContext, 0, 1.0, 0, 0.6);
      CGContextFillRect(cgContext, macRect);
    break;      
#endif
    case NS_THEME_SCROLLBAR_TRACK_HORIZONTAL:
    case NS_THEME_SCROLLBAR_TRACK_VERTICAL:
      // do nothing, drawn by scrollbar
      break;

    case NS_THEME_TEXTFIELD_MULTILINE: {
      // we have to draw this by hand because there is no HITheme value for it
      CGContextSetRGBFillColor(cgContext, 1.0, 1.0, 1.0, 1.0);
      
      CGContextFillRect(cgContext, macRect);

      CGContextSetLineWidth(cgContext, 1.0);
      CGContextSetShouldAntialias(cgContext, false);

      // stroke everything but the top line of the text area
      CGContextSetRGBStrokeColor(cgContext, 0.6, 0.6, 0.6, 1.0);
      CGContextBeginPath(cgContext);
      CGContextMoveToPoint(cgContext, macRect.origin.x, macRect.origin.y + 1);
      CGContextAddLineToPoint(cgContext, macRect.origin.x, macRect.origin.y + macRect.size.height);
      CGContextAddLineToPoint(cgContext, macRect.origin.x + macRect.size.width - 1, macRect.origin.y + macRect.size.height);
      CGContextAddLineToPoint(cgContext, macRect.origin.x + macRect.size.width - 1, macRect.origin.y + 1);
      CGContextStrokePath(cgContext);

      // stroke the line across the top of the text area
      CGContextSetRGBStrokeColor(cgContext, 0.4510, 0.4510, 0.4510, 1.0);
      CGContextBeginPath(cgContext);
      CGContextMoveToPoint(cgContext, macRect.origin.x, macRect.origin.y + 1);
      CGContextAddLineToPoint(cgContext, macRect.origin.x + macRect.size.width - 1, macRect.origin.y + 1);
      CGContextStrokePath(cgContext);

      // draw a focus ring
      if (eventState & NS_EVENT_STATE_FOCUS) {
        // We need to bring the rectangle in by 1 pixel on each side.
        CGRect cgr = CGRectMake(macRect.origin.x + 1,
                                macRect.origin.y + 1,
                                macRect.size.width - 2,
                                macRect.size.height - 2);
        HIThemeDrawFocusRect(&cgr, true, cgContext, kHIThemeOrientationNormal);
      }
    }
      break;

    case NS_THEME_LISTBOX: {
      // We have to draw this by hand because kHIThemeFrameListBox drawing
      // is buggy on 10.5, see bug 579259.
      CGContextSetRGBFillColor(cgContext, 1.0, 1.0, 1.0, 1.0);
      CGContextFillRect(cgContext, macRect);

      // #8E8E8E for the top border, #BEBEBE for the rest.
      int x = macRect.origin.x, y = macRect.origin.y;
      int w = macRect.size.width, h = macRect.size.height;
      CGContextSetRGBFillColor(cgContext, 0.557, 0.557, 0.557, 1.0);
      CGContextFillRect(cgContext, CGRectMake(x, y, w, 1));
      CGContextSetRGBFillColor(cgContext, 0.745, 0.745, 0.745, 1.0);
      CGContextFillRect(cgContext, CGRectMake(x, y + 1, 1, h - 1));
      CGContextFillRect(cgContext, CGRectMake(x + w - 1, y + 1, 1, h - 1));
      CGContextFillRect(cgContext, CGRectMake(x + 1, y + h - 1, w - 2, 1));
    }
      break;
    
    case NS_THEME_TAB:
      DrawTab(cgContext, macRect, eventState, aFrame);
      break;

    case NS_THEME_TAB_PANELS:
      DrawTabPanel(cgContext, macRect, aFrame);
      break;

    case NS_THEME_RESIZER:
      DrawResizer(cgContext, macRect, aFrame);
      break;
  }

  nativeDrawing.EndNativeDrawing();

  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

void
nsNativeThemeCocoa::RegisterWidgetGeometry(nsIWidget* aWindow,
                                           PRUint8 aWidgetType,
                                           const nsIntRect& aRect)
{
  ToolbarWindow* cocoaWindow;
  if (IsWindowSpanningToolbar(aWindow, aWidgetType, aRect, &cocoaWindow) &&
      ![cocoaWindow drawsContentsIntoWindowFrame]) {
    [cocoaWindow notifyToolbarAt:aRect.y height:aRect.height];
  }
}
                                         
nsIntMargin
nsNativeThemeCocoa::RTLAwareMargin(const nsIntMargin& aMargin, nsIFrame* aFrame)
{
  if (IsFrameRTL(aFrame))
    return nsIntMargin(aMargin.right, aMargin.top, aMargin.left, aMargin.bottom);

  return aMargin;
}

static const nsIntMargin kAquaDropdownBorder(5, 1, 22, 2);
static const nsIntMargin kAquaComboboxBorder(4, 3, 20, 3);
static const nsIntMargin kAquaSearchfieldBorder(19, 3, 5, 2);

NS_IMETHODIMP
nsNativeThemeCocoa::GetWidgetBorder(nsIDeviceContext* aContext, 
                                    nsIFrame* aFrame,
                                    PRUint8 aWidgetType,
                                    nsIntMargin* aResult)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  aResult->SizeTo(0, 0, 0, 0);

  switch (aWidgetType) {
    case NS_THEME_BUTTON:
    {
      if (IsButtonTypeMenu(aFrame)) {
        *aResult = RTLAwareMargin(kAquaDropdownBorder, aFrame);
      } else {
        aResult->SizeTo(7, 1, 7, 3);
      }
      break;
    }

    case NS_THEME_CHECKBOX:
    case NS_THEME_RADIO:
    {
      // nsFormControlFrame::GetIntrinsicWidth and nsFormControlFrame::GetIntrinsicHeight
      // assume a border width of 2px.
      aResult->SizeTo(2, 2, 2, 2);
      break;
    }

    case NS_THEME_DROPDOWN:
    case NS_THEME_DROPDOWN_BUTTON:
      *aResult = RTLAwareMargin(kAquaDropdownBorder, aFrame);
      break;

    case NS_THEME_DROPDOWN_TEXTFIELD:
      *aResult = RTLAwareMargin(kAquaComboboxBorder, aFrame);
      break;

    case NS_THEME_TEXTFIELD:
    {
      SInt32 frameOutset = 0;
      ::GetThemeMetric(kThemeMetricEditTextFrameOutset, &frameOutset);

      SInt32 textPadding = 0;
      ::GetThemeMetric(kThemeMetricEditTextWhitespace, &textPadding);

      frameOutset += textPadding;

      aResult->SizeTo(frameOutset, frameOutset, frameOutset, frameOutset);
      break;
    }

    case NS_THEME_TEXTFIELD_MULTILINE:
      aResult->SizeTo(1, 1, 1, 1);
      break;

    case NS_THEME_SEARCHFIELD:
      *aResult = RTLAwareMargin(kAquaSearchfieldBorder, aFrame);
      break;

    case NS_THEME_LISTBOX:
    {
      SInt32 frameOutset = 0;
      ::GetThemeMetric(kThemeMetricListBoxFrameOutset, &frameOutset);
      aResult->SizeTo(frameOutset, frameOutset, frameOutset, frameOutset);
      break;
    }

    case NS_THEME_SCROLLBAR_TRACK_HORIZONTAL:
    case NS_THEME_SCROLLBAR_TRACK_VERTICAL:
    {
      // There's only an endcap to worry about when both arrows are on the bottom
      NSString *buttonPlacement = [[NSUserDefaults standardUserDefaults] objectForKey:@"AppleScrollBarVariant"];
      if (!buttonPlacement || [buttonPlacement isEqualToString:@"DoubleMax"]) {
        PRBool isHorizontal = (aWidgetType == NS_THEME_SCROLLBAR_TRACK_HORIZONTAL);

        nsIFrame *scrollbarFrame = GetParentScrollbarFrame(aFrame);
        if (!scrollbarFrame) return NS_ERROR_FAILURE;
        PRBool isSmall = (scrollbarFrame->GetStyleDisplay()->mAppearance == NS_THEME_SCROLLBAR_SMALL);

        // There isn't a metric for this, so just hardcode a best guess at the value.
        // This value is even less exact due to the fact that the endcap is partially concave.
        PRInt32 endcapSize = isSmall ? 5 : 6;

        if (isHorizontal)
          aResult->SizeTo(endcapSize, 0, 0, 0);
        else
          aResult->SizeTo(0, endcapSize, 0, 0);
      }
      break;
    }

    case NS_THEME_STATUSBAR:
      aResult->SizeTo(0, 1, 0, 0);
      break;
  }

  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

// Return PR_FALSE here to indicate that CSS padding values should be used. There is
// no reason to make a distinction between padding and border values, just specify
// whatever values you want in GetWidgetBorder and only use this to return PR_TRUE
// if you want to override CSS padding values.
PRBool
nsNativeThemeCocoa::GetWidgetPadding(nsIDeviceContext* aContext, 
                                     nsIFrame* aFrame,
                                     PRUint8 aWidgetType,
                                     nsIntMargin* aResult)
{
  // We don't want CSS padding being used for certain widgets.
  // See bug 381639 for an example of why.
  switch (aWidgetType) {
    case NS_THEME_BUTTON:
    // Radios and checkboxes return a fixed size in GetMinimumWidgetSize
    // and have a meaningful baseline, so they can't have
    // author-specified padding.
    case NS_THEME_CHECKBOX:
    case NS_THEME_RADIO:
      aResult->SizeTo(0, 0, 0, 0);
      return PR_TRUE;
  }
  return PR_FALSE;
}

PRBool
nsNativeThemeCocoa::GetWidgetOverflow(nsIDeviceContext* aContext, nsIFrame* aFrame,
                                      PRUint8 aWidgetType, nsRect* aOverflowRect)
{
  switch (aWidgetType) {
    case NS_THEME_BUTTON:
    case NS_THEME_TEXTFIELD:
    case NS_THEME_TEXTFIELD_MULTILINE:
    case NS_THEME_SEARCHFIELD:
    case NS_THEME_LISTBOX:
    case NS_THEME_DROPDOWN:
    case NS_THEME_DROPDOWN_BUTTON:
    case NS_THEME_DROPDOWN_TEXTFIELD:
    case NS_THEME_CHECKBOX:
    case NS_THEME_RADIO:
    case NS_THEME_TAB:
    {
      // We assume that the above widgets can draw a focus ring that will be less than
      // or equal to 4 pixels thick.
      nsIntMargin extraSize = nsIntMargin(MAX_FOCUS_RING_WIDTH, MAX_FOCUS_RING_WIDTH, MAX_FOCUS_RING_WIDTH, MAX_FOCUS_RING_WIDTH);
      PRInt32 p2a = aContext->AppUnitsPerDevPixel();
      nsMargin m(NSIntPixelsToAppUnits(extraSize.left, p2a),
                 NSIntPixelsToAppUnits(extraSize.top, p2a),
                 NSIntPixelsToAppUnits(extraSize.right, p2a),
                 NSIntPixelsToAppUnits(extraSize.bottom, p2a));
      aOverflowRect->Inflate(m);
      return PR_TRUE;
    }
  }

  return PR_FALSE;
}

static const PRInt32 kRegularScrollbarThumbMinSize = 22;
static const PRInt32 kSmallScrollbarThumbMinSize = 19;

NS_IMETHODIMP
nsNativeThemeCocoa::GetMinimumWidgetSize(nsIRenderingContext* aContext,
                                         nsIFrame* aFrame,
                                         PRUint8 aWidgetType,
                                         nsIntSize* aResult,
                                         PRBool* aIsOverridable)
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK_NSRESULT;

  aResult->SizeTo(0,0);
  *aIsOverridable = PR_TRUE;

  switch (aWidgetType) {
    case NS_THEME_BUTTON:
    {
      aResult->SizeTo(pushButtonSettings.minimumSizes[miniControlSize].width,
                      pushButtonSettings.naturalSizes[miniControlSize].height);
      break;
    }

    case NS_THEME_SPINNER:
    {
      SInt32 buttonHeight = 0, buttonWidth = 0;
      ::GetThemeMetric(kThemeMetricLittleArrowsWidth, &buttonWidth);
      ::GetThemeMetric(kThemeMetricLittleArrowsHeight, &buttonHeight);
      aResult->SizeTo(buttonWidth, buttonHeight);
      *aIsOverridable = PR_FALSE;
      break;
    }

    case NS_THEME_DROPDOWN:
    case NS_THEME_DROPDOWN_BUTTON:
    {
      SInt32 popupHeight = 0;
      ::GetThemeMetric(kThemeMetricPopupButtonHeight, &popupHeight);
      aResult->SizeTo(0, popupHeight);
      break;
    }
 
    case NS_THEME_TEXTFIELD:
    case NS_THEME_TEXTFIELD_MULTILINE:
    case NS_THEME_SEARCHFIELD:
    {
      // at minimum, we should be tall enough for 9pt text.
      // I'm using hardcoded values here because the appearance manager
      // values for the frame size are incorrect.
      aResult->SizeTo(0, (2 + 2) /* top */ + 9 + (1 + 1) /* bottom */);
      break;
    }
      
    case NS_THEME_PROGRESSBAR:
    {
      SInt32 barHeight = 0;
      ::GetThemeMetric(kThemeMetricNormalProgressBarThickness, &barHeight);
      aResult->SizeTo(0, barHeight);
      break;
    }

    case NS_THEME_TREEVIEW_TWISTY:
    case NS_THEME_TREEVIEW_TWISTY_OPEN:   
    {
      SInt32 twistyHeight = 0, twistyWidth = 0;
      ::GetThemeMetric(kThemeMetricDisclosureButtonWidth, &twistyWidth);
      ::GetThemeMetric(kThemeMetricDisclosureButtonHeight, &twistyHeight);
      aResult->SizeTo(twistyWidth, twistyHeight);
      *aIsOverridable = PR_FALSE;
      break;
    }
    
    case NS_THEME_TREEVIEW_HEADER:
    case NS_THEME_TREEVIEW_HEADER_CELL:
    {
      SInt32 headerHeight = 0;
      ::GetThemeMetric(kThemeMetricListHeaderHeight, &headerHeight);
      aResult->SizeTo(0, headerHeight - 1); // We don't need the top border.
      break;
    }

    case NS_THEME_TAB:
    {
      aResult->SizeTo(0, NATURAL_MINI_TAB_BUTTON_HEIGHT);
      break;
    }

    case NS_THEME_SCALE_HORIZONTAL:
    {
      SInt32 scaleHeight = 0;
      ::GetThemeMetric(kThemeMetricHSliderHeight, &scaleHeight);
      aResult->SizeTo(scaleHeight, scaleHeight);
      *aIsOverridable = PR_FALSE;
      break;
    }

    case NS_THEME_SCALE_VERTICAL:
    {
      SInt32 scaleWidth = 0;
      ::GetThemeMetric(kThemeMetricVSliderWidth, &scaleWidth);
      aResult->SizeTo(scaleWidth, scaleWidth);
      *aIsOverridable = PR_FALSE;
      break;
    }
      
    case NS_THEME_SCROLLBAR_SMALL:
    {
      SInt32 scrollbarWidth = 0;
      ::GetThemeMetric(kThemeMetricSmallScrollBarWidth, &scrollbarWidth);
      aResult->SizeTo(scrollbarWidth, scrollbarWidth);
      *aIsOverridable = PR_FALSE;
      break;
    }

    case NS_THEME_SCROLLBAR_THUMB_HORIZONTAL:
    case NS_THEME_SCROLLBAR_THUMB_VERTICAL:
    {
      // Find our parent scrollbar frame in order to find out whether we're in
      // a small or a large scrollbar.
      nsIFrame *scrollbarFrame = GetParentScrollbarFrame(aFrame);
      if (!scrollbarFrame)
        return NS_ERROR_FAILURE;

      PRBool isSmall = (scrollbarFrame->GetStyleDisplay()->mAppearance == NS_THEME_SCROLLBAR_SMALL);
      PRBool isHorizontal = (aWidgetType == NS_THEME_SCROLLBAR_THUMB_HORIZONTAL);
      PRInt32& minSize = isHorizontal ? aResult->width : aResult->height;
      minSize = isSmall ? kSmallScrollbarThumbMinSize : kRegularScrollbarThumbMinSize;
      break;
    }

    case NS_THEME_SCROLLBAR:
    case NS_THEME_SCROLLBAR_TRACK_VERTICAL:
    case NS_THEME_SCROLLBAR_TRACK_HORIZONTAL:
    {
      // yeah, i know i'm cheating a little here, but i figure that it
      // really doesn't matter if the scrollbar is vertical or horizontal
      // and the width metric is a really good metric for every piece
      // of the scrollbar.

      nsIFrame *scrollbarFrame = GetParentScrollbarFrame(aFrame);
      if (!scrollbarFrame) return NS_ERROR_FAILURE;

      PRInt32 themeMetric = (scrollbarFrame->GetStyleDisplay()->mAppearance == NS_THEME_SCROLLBAR_SMALL) ?
                            kThemeMetricSmallScrollBarWidth :
                            kThemeMetricScrollBarWidth;
      SInt32 scrollbarWidth = 0;
      ::GetThemeMetric(themeMetric, &scrollbarWidth);
      aResult->SizeTo(scrollbarWidth, scrollbarWidth);
      *aIsOverridable = PR_FALSE;
      break;
    }

    case NS_THEME_SCROLLBAR_BUTTON_UP:
    case NS_THEME_SCROLLBAR_BUTTON_DOWN:
    case NS_THEME_SCROLLBAR_BUTTON_LEFT:
    case NS_THEME_SCROLLBAR_BUTTON_RIGHT:
    {
      nsIFrame *scrollbarFrame = GetParentScrollbarFrame(aFrame);
      if (!scrollbarFrame) return NS_ERROR_FAILURE;

      // Since there is no NS_THEME_SCROLLBAR_BUTTON_UP_SMALL we need to ask the parent what appearance style it has.
      PRInt32 themeMetric = (scrollbarFrame->GetStyleDisplay()->mAppearance == NS_THEME_SCROLLBAR_SMALL) ?
                            kThemeMetricSmallScrollBarWidth :
                            kThemeMetricScrollBarWidth;
      SInt32 scrollbarWidth = 0;
      ::GetThemeMetric(themeMetric, &scrollbarWidth);

      // It seems that for both sizes of scrollbar, the buttons are one pixel "longer".
      if (aWidgetType == NS_THEME_SCROLLBAR_BUTTON_LEFT || aWidgetType == NS_THEME_SCROLLBAR_BUTTON_RIGHT)
        aResult->SizeTo(scrollbarWidth+1, scrollbarWidth);
      else
        aResult->SizeTo(scrollbarWidth, scrollbarWidth+1);
 
      *aIsOverridable = PR_FALSE;
      break;
    }
    case NS_THEME_RESIZER:
    {
      HIThemeGrowBoxDrawInfo drawInfo;
      drawInfo.version = 0;
      drawInfo.state = kThemeStateActive;
      drawInfo.kind = kHIThemeGrowBoxKindNormal;
      drawInfo.direction = kThemeGrowRight | kThemeGrowDown;
      drawInfo.size = kHIThemeGrowBoxSizeNormal;
      HIPoint pnt = { 0, 0 };
      HIRect bounds;
      HIThemeGetGrowBoxBounds(&pnt, &drawInfo, &bounds);
      aResult->SizeTo(bounds.size.width, bounds.size.height);
      *aIsOverridable = PR_FALSE;
    }
  }

  return NS_OK;

  NS_OBJC_END_TRY_ABORT_BLOCK_NSRESULT;
}

NS_IMETHODIMP
nsNativeThemeCocoa::WidgetStateChanged(nsIFrame* aFrame, PRUint8 aWidgetType, 
                                     nsIAtom* aAttribute, PRBool* aShouldRepaint)
{
  // Some widget types just never change state.
  switch (aWidgetType) {
    case NS_THEME_TOOLBOX:
    case NS_THEME_TOOLBAR:
    case NS_THEME_MOZ_MAC_UNIFIED_TOOLBAR:
    case NS_THEME_TOOLBAR_BUTTON:
    case NS_THEME_SCROLLBAR_TRACK_VERTICAL: 
    case NS_THEME_SCROLLBAR_TRACK_HORIZONTAL:
    case NS_THEME_STATUSBAR:
    case NS_THEME_STATUSBAR_PANEL:
    case NS_THEME_STATUSBAR_RESIZER_PANEL:
    case NS_THEME_TOOLTIP:
    case NS_THEME_TAB_PANELS:
    case NS_THEME_TAB_PANEL:
    case NS_THEME_DIALOG:
    case NS_THEME_MENUPOPUP:
    case NS_THEME_GROUPBOX:
      *aShouldRepaint = PR_FALSE;
      return NS_OK;
    case NS_THEME_PROGRESSBAR_CHUNK:
    case NS_THEME_PROGRESSBAR_CHUNK_VERTICAL:
    case NS_THEME_PROGRESSBAR:
    case NS_THEME_PROGRESSBAR_VERTICAL:
      *aShouldRepaint = (aAttribute == nsWidgetAtoms::step);
      return NS_OK;
  }

  // XXXdwh Not sure what can really be done here.  Can at least guess for
  // specific widgets that they're highly unlikely to have certain states.
  // For example, a toolbar doesn't care about any states.
  if (!aAttribute) {
    // Hover/focus/active changed.  Always repaint.
    *aShouldRepaint = PR_TRUE;
  } else {
    // Check the attribute to see if it's relevant.  
    // disabled, checked, dlgtype, default, etc.
    *aShouldRepaint = PR_FALSE;
    if (aAttribute == nsWidgetAtoms::disabled ||
        aAttribute == nsWidgetAtoms::checked ||
        aAttribute == nsWidgetAtoms::selected ||
        aAttribute == nsWidgetAtoms::mozmenuactive ||
        aAttribute == nsWidgetAtoms::sortdirection ||
        aAttribute == nsWidgetAtoms::focused ||
        aAttribute == nsWidgetAtoms::_default ||
        aAttribute == nsWidgetAtoms::step ||
        aAttribute == nsWidgetAtoms::open)
      *aShouldRepaint = PR_TRUE;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsNativeThemeCocoa::ThemeChanged()
{
  // This is unimplemented because we don't care if gecko changes its theme
  // and Mac OS X doesn't have themes.
  return NS_OK;
}

PRBool 
nsNativeThemeCocoa::ThemeSupportsWidget(nsPresContext* aPresContext, nsIFrame* aFrame,
                                      PRUint8 aWidgetType)
{
  // We don't have CSS set up to render non-native scrollbars on Mac OS X so we
  // render natively even if native theme support is disabled.
  if (aWidgetType != NS_THEME_SCROLLBAR &&
      aPresContext && !aPresContext->PresShell()->IsThemeSupportEnabled())
    return PR_FALSE;

  // if this is a dropdown button in a combobox the answer is always no
  if (aWidgetType == NS_THEME_DROPDOWN_BUTTON) {
    nsIFrame* parentFrame = aFrame->GetParent();
    if (parentFrame && (parentFrame->GetType() == nsWidgetAtoms::comboboxControlFrame))
      return PR_FALSE;
  }

  switch (aWidgetType) {
    case NS_THEME_LISTBOX:

    case NS_THEME_DIALOG:
    case NS_THEME_WINDOW:
    case NS_THEME_MENUPOPUP:
    case NS_THEME_MENUITEM:
    case NS_THEME_MENUSEPARATOR:
    case NS_THEME_TOOLTIP:
    
    case NS_THEME_CHECKBOX:
    case NS_THEME_CHECKBOX_CONTAINER:
    case NS_THEME_RADIO:
    case NS_THEME_RADIO_CONTAINER:
    case NS_THEME_GROUPBOX:
    case NS_THEME_BUTTON:
    case NS_THEME_BUTTON_BEVEL:
    case NS_THEME_SPINNER:
    case NS_THEME_TOOLBAR:
    case NS_THEME_MOZ_MAC_UNIFIED_TOOLBAR:
    case NS_THEME_STATUSBAR:
    case NS_THEME_TEXTFIELD:
    case NS_THEME_TEXTFIELD_MULTILINE:
    case NS_THEME_SEARCHFIELD:
    //case NS_THEME_TOOLBOX:
    //case NS_THEME_TOOLBAR_BUTTON:
    case NS_THEME_PROGRESSBAR:
    case NS_THEME_PROGRESSBAR_VERTICAL:
    case NS_THEME_PROGRESSBAR_CHUNK:
    case NS_THEME_PROGRESSBAR_CHUNK_VERTICAL:
    case NS_THEME_TOOLBAR_SEPARATOR:
    
    case NS_THEME_TAB_PANELS:
    case NS_THEME_TAB:
    
    case NS_THEME_TREEVIEW_TWISTY:
    case NS_THEME_TREEVIEW_TWISTY_OPEN:
    case NS_THEME_TREEVIEW:
    case NS_THEME_TREEVIEW_HEADER:
    case NS_THEME_TREEVIEW_HEADER_CELL:
    case NS_THEME_TREEVIEW_HEADER_SORTARROW:
    case NS_THEME_TREEVIEW_TREEITEM:
    case NS_THEME_TREEVIEW_LINE:

    case NS_THEME_SCALE_HORIZONTAL:
    case NS_THEME_SCALE_THUMB_HORIZONTAL:
    case NS_THEME_SCALE_VERTICAL:
    case NS_THEME_SCALE_THUMB_VERTICAL:

    case NS_THEME_SCROLLBAR:
    case NS_THEME_SCROLLBAR_SMALL:
    case NS_THEME_SCROLLBAR_BUTTON_UP:
    case NS_THEME_SCROLLBAR_BUTTON_DOWN:
    case NS_THEME_SCROLLBAR_BUTTON_LEFT:
    case NS_THEME_SCROLLBAR_BUTTON_RIGHT:
    case NS_THEME_SCROLLBAR_THUMB_HORIZONTAL:
    case NS_THEME_SCROLLBAR_THUMB_VERTICAL:
    case NS_THEME_SCROLLBAR_TRACK_VERTICAL:
    case NS_THEME_SCROLLBAR_TRACK_HORIZONTAL:

    case NS_THEME_DROPDOWN:
    case NS_THEME_DROPDOWN_BUTTON:
    case NS_THEME_DROPDOWN_TEXT:
    case NS_THEME_DROPDOWN_TEXTFIELD:
      return !IsWidgetStyled(aPresContext, aFrame, aWidgetType);
      break;

    case NS_THEME_RESIZER:
    {
      nsIFrame* parentFrame = aFrame->GetParent();
      if (!parentFrame || parentFrame->GetType() != nsWidgetAtoms::scrollFrame)
        return PR_TRUE;

      // Note that IsWidgetStyled is not called for resizers on Mac. This is
      // because for scrollable containers, the native resizer looks better
      // when scrollbars are present even when the style is overriden, and the
      // custom transparent resizer looks better when scrollbars are not
      // present.
      nsIScrollableFrame* scrollFrame = do_QueryFrame(parentFrame);
      return (scrollFrame && scrollFrame->GetScrollbarVisibility());
      break;
    }
  }

  return PR_FALSE;
}

PRBool
nsNativeThemeCocoa::WidgetIsContainer(PRUint8 aWidgetType)
{
  // flesh this out at some point
  switch (aWidgetType) {
   case NS_THEME_DROPDOWN_BUTTON:
   case NS_THEME_RADIO:
   case NS_THEME_CHECKBOX:
   case NS_THEME_PROGRESSBAR:
    return PR_FALSE;
    break;
  }
  return PR_TRUE;
}

PRBool
nsNativeThemeCocoa::ThemeDrawsFocusForWidget(nsPresContext* aPresContext, nsIFrame* aFrame, PRUint8 aWidgetType)
{
  if (aWidgetType == NS_THEME_DROPDOWN ||
      aWidgetType == NS_THEME_DROPDOWN_TEXTFIELD ||
      aWidgetType == NS_THEME_BUTTON ||
      aWidgetType == NS_THEME_RADIO ||
      aWidgetType == NS_THEME_CHECKBOX)
    return PR_TRUE;

  return PR_FALSE;
}

PRBool
nsNativeThemeCocoa::ThemeNeedsComboboxDropmarker()
{
  return PR_FALSE;
}

nsITheme::Transparency
nsNativeThemeCocoa::GetWidgetTransparency(nsIFrame* aFrame, PRUint8 aWidgetType)
{
  switch (aWidgetType) {
  case NS_THEME_MENUPOPUP:
  case NS_THEME_TOOLTIP:
    return eTransparent;

  case NS_THEME_SCROLLBAR_SMALL:
  case NS_THEME_SCROLLBAR:
  case NS_THEME_STATUSBAR:
    // Knowing that scrollbars and statusbars are opaque improves
    // performance, because we create layers for them.
    return eOpaque;

  default:
    return eUnknownTransparency;
  }
}
