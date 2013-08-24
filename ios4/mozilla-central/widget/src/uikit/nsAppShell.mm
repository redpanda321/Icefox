/* -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; -*- */
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
 * The Original Code is a Cocoa widget run loop and event implementation.
 *
 * The Initial Developer of the Original Code is Google Inc.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Mark Mentovai <mark@moxienet.com> (Original Author)
 *  Ted Mielczarek <ted.mielczarek@gmail.com>
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

#import <UIKit/UIApplication.h>
#import <UIKit/UIScreen.h>
#import <UIKit/UIWindow.h>

#include "nsAppShell.h"
#include "nsCOMPtr.h"
#include "nsIFile.h"
#include "nsDirectoryServiceDefs.h"
#include "nsString.h"
#include "nsIRollupListener.h"
#include "nsIWidget.h"
#include "nsThreadUtils.h"
#include "nsIWindowMediator.h"
#include "nsServiceManagerUtils.h"
#include "nsIInterfaceRequestor.h"
#include "nsIWebBrowserChrome.h"

nsAppShell *nsAppShell::gAppShell = NULL;
UIWindow *nsAppShell::gWindow = nil;
NSMutableArray *nsAppShell::gTopLevelViews = [[NSMutableArray alloc] init];

#define ALOG(args...) printf(args); printf("\n")

// AppShellDelegate
//
// Acts as a delegate for the UIApplication

@interface AppShellDelegate : NSObject <UIApplicationDelegate> {
}
@end

@implementation AppShellDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
  ALOG("[AppShellDelegate application:didFinishLaunchingWithOptions:]");
  // We only create one window, since we can only display one window at
  // a time anyway. Also, iOS 4 fails to display UIWindows if you
  // create them before calling UIApplicationMain, so this makes more sense.
  nsAppShell::gWindow = [[[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] applicationFrame]] retain];
  // just to make things more visible for now
  //nsAppShell::gWindow.backgroundColor = [UIColor blueColor];

  // add all of the top level views as children
  for (unsigned int i = 0; i < [nsAppShell::gTopLevelViews count]; i++) {
    [nsAppShell::gWindow addSubview:[nsAppShell::gTopLevelViews objectAtIndex:i]];
  }
  [nsAppShell::gTopLevelViews release];
  nsAppShell::gTopLevelViews = nil;
  [nsAppShell::gWindow makeKeyAndVisible];

  return YES;
}

- (void)applicationWillTerminate:(UIApplication *)application
{
  ALOG("[AppShellDelegate applicationWillTerminate:]");
  nsAppShell::gAppShell->WillTerminate();
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
  ALOG("[AppShellDelegate applicationDidBecomeActive:]");
}

- (void)applicationWillResignActive:(UIApplication *)application
{
  ALOG("[AppShellDelegate applicationWillResignActive:]");
}

- (void)applicationDidReceiveMemoryWarning:(UIApplication *)application
{
  ALOG("[AppShellDelegate applicationDidReceiveMemoryWarning:]");
}
@end

// nsAppShell implementation

NS_IMETHODIMP
nsAppShell::ResumeNative(void)
{
  return nsBaseAppShell::ResumeNative();
}

nsAppShell::nsAppShell()
  : mAutoreleasePool(NULL),
    mDelegate(NULL),
    mCFRunLoop(NULL),
    mCFRunLoopSource(NULL),
    mTerminated(PR_FALSE),
    mNotifiedWillTerminate(PR_FALSE)
{
  gAppShell = this;
}

nsAppShell::~nsAppShell()
{
  if (mAutoreleasePool) {
    [mAutoreleasePool release];
    mAutoreleasePool = NULL;
  }

  if (mCFRunLoop) {
    if (mCFRunLoopSource) {
      ::CFRunLoopRemoveSource(mCFRunLoop, mCFRunLoopSource,
                              kCFRunLoopCommonModes);
      ::CFRelease(mCFRunLoopSource);
    }
    ::CFRelease(mCFRunLoop);
  }

  gAppShell = NULL;
}

// Init
//
// public
nsresult
nsAppShell::Init()
{
  mAutoreleasePool = [[NSAutoreleasePool alloc] init];

  // Add a CFRunLoopSource to the main native run loop.  The source is
  // responsible for interrupting the run loop when Gecko events are ready.

  mCFRunLoop = [[NSRunLoop currentRunLoop] getCFRunLoop];
  NS_ENSURE_STATE(mCFRunLoop);
  ::CFRetain(mCFRunLoop);

  CFRunLoopSourceContext context;
  bzero(&context, sizeof(context));
  // context.version = 0;
  context.info = this;
  context.perform = ProcessGeckoEvents;
  
  mCFRunLoopSource = ::CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &context);
  NS_ENSURE_STATE(mCFRunLoopSource);

  ::CFRunLoopAddSource(mCFRunLoop, mCFRunLoopSource, kCFRunLoopCommonModes);

  return nsBaseAppShell::Init();
}

// ProcessGeckoEvents
//
// The "perform" target of mCFRunLoop, called when mCFRunLoopSource is
// signalled from ScheduleNativeEventCallback.
//
// protected static
void
nsAppShell::ProcessGeckoEvents(void* aInfo)
{
  nsAppShell* self = static_cast<nsAppShell*> (aInfo);
  self->NativeEventCallback();
  self->Release();
}

// WillTerminate
//
// public
void
nsAppShell::WillTerminate()
{
  mNotifiedWillTerminate = PR_TRUE;
  if (mTerminated)
    return;
  mTerminated = PR_TRUE;
  // We won't get another chance to process events
  NS_ProcessPendingEvents(NS_GetCurrentThread());
  
  // Unless we call nsBaseAppShell::Exit() here, it might not get called
  // at all.
  nsBaseAppShell::Exit();
}

// ScheduleNativeEventCallback
//
// protected virtual
void
nsAppShell::ScheduleNativeEventCallback()
{
  if (mTerminated)
    return;

  NS_ADDREF_THIS();

  // This will invoke ProcessGeckoEvents on the main thread.
  ::CFRunLoopSourceSignal(mCFRunLoopSource);
  ::CFRunLoopWakeUp(mCFRunLoop);
}

// ProcessNextNativeEvent
//
// protected virtual
PRBool
nsAppShell::ProcessNextNativeEvent(PRBool aMayWait)
{
  if (mTerminated)
    return PR_FALSE;

  NSString* currentMode = nil;
  NSDate* waitUntil = nil;
  if (aMayWait)
    waitUntil = [NSDate distantFuture];
  NSRunLoop* currentRunLoop = [NSRunLoop currentRunLoop];

  BOOL eventProcessed = NO;
  do {
    currentMode = [currentRunLoop currentMode];
    if (!currentMode)
      currentMode = NSDefaultRunLoopMode;

    if (aMayWait)
      eventProcessed = [currentRunLoop runMode:currentMode beforeDate:waitUntil];
    else
      [currentRunLoop acceptInputForMode:currentMode beforeDate:waitUntil];
  } while(eventProcessed && aMayWait);

  return PR_FALSE;
}

// Run
//
// public
NS_IMETHODIMP
nsAppShell::Run(void)
{
  ALOG("nsAppShell::Run");
  char *argv[1] = {"app"};
  UIApplicationMain(1, argv, nil, @"AppShellDelegate");
  // UIApplicationMain doesn't exit. :-(
  return NS_OK;
}

NS_IMETHODIMP
nsAppShell::Exit(void)
{
  if (mTerminated)
    return NS_OK;

  mTerminated = PR_TRUE;
  return nsBaseAppShell::Exit();
}
