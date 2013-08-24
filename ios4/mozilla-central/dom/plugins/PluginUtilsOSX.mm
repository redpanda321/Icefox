/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
// vim:set ts=2 sts=2 sw=2 et cin:
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
  * The Original Code is Mozilla.org code.
  *
  * The Initial Developer of the Original Code is
  *   Mozilla Corporation.
  * Portions created by the Initial Developer are Copyright (C) 2010
  * the Initial Developer. All Rights Reserved.
  *
  * Contributor(s):
  *   Benoit Girard <b56girard@gmail.com>
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

#import <AppKit/AppKit.h>
#include "PluginUtilsOSX.h"

// Remove definitions for try/catch interfering with ObjCException macros.
#include "nsObjCExceptions.h"

using namespace mozilla::plugins::PluginUtilsOSX;

@interface EventProcessor : NSObject {
  RemoteProcessEvents   aRemoteEvents;
  void                 *aPluginModule;
}
- (void)setRemoteEvents:(RemoteProcessEvents) remoteEvents pluginModule:(void*) pluginModule;
- (void)onTick;
@end

@implementation EventProcessor
- (void) onTick
{
    aRemoteEvents(aPluginModule);
}

- (void)setRemoteEvents:(RemoteProcessEvents) remoteEvents pluginModule:(void*) pluginModule
{
    aRemoteEvents = remoteEvents;
    aPluginModule = pluginModule;
}
@end

#define EVENT_PROCESS_DELAY 0.05 // 50 ms

NPError mozilla::plugins::PluginUtilsOSX::ShowCocoaContextMenu(void* aMenu, int aX, int aY, void* pluginModule, RemoteProcessEvents remoteEvent) 
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;

  // Create a timer to process browser events while waiting
  // on the menu. This prevents the browser from hanging
  // during the lifetime of the menu.
  EventProcessor* eventProcessor = [[EventProcessor alloc] init];
  [eventProcessor setRemoteEvents:remoteEvent pluginModule:pluginModule];
  NSTimer *eventTimer = [NSTimer timerWithTimeInterval:EVENT_PROCESS_DELAY
                             target:eventProcessor selector:@selector(onTick) 
                             userInfo:nil repeats:TRUE];
  // Use NSEventTrackingRunLoopMode otherwise the timer will
  // not fire during the right click menu.
  [[NSRunLoop currentRunLoop] addTimer:eventTimer 
                              forMode:NSEventTrackingRunLoopMode];

  NSMenu* nsmenu = reinterpret_cast<NSMenu*>(aMenu);
  NSPoint screen_point = ::NSMakePoint(aX, aY);

  [nsmenu popUpMenuPositioningItem:nil atLocation:screen_point inView:nil];

  [eventTimer invalidate];
  [eventProcessor release];

  return NPERR_NO_ERROR;

  NS_OBJC_END_TRY_ABORT_BLOCK_RETURN(NPERR_GENERIC_ERROR);
}

void mozilla::plugins::PluginUtilsOSX::InvokeNativeEventLoop()
{
  NS_OBJC_BEGIN_TRY_ABORT_BLOCK;
  ::CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.0, true);
  NS_OBJC_END_TRY_ABORT_BLOCK;
}
