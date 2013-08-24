/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/HangMonitor.h"
#include "mozilla/Monitor.h"
#include "mozilla/Preferences.h"
#include "mozilla/Telemetry.h"
#include "nsXULAppAPI.h"
#include "nsThreadUtils.h"
#include "nsStackWalk.h"

#ifdef MOZ_CRASHREPORTER
#include "nsExceptionHandler.h"
#endif

#ifdef XP_WIN
#include <windows.h>
#endif

#if defined(MOZ_ENABLE_PROFILER_SPS) && defined(MOZ_PROFILING) && defined(XP_WIN)
  #define REPORT_CHROME_HANGS
#endif

namespace mozilla { namespace HangMonitor {

/**
 * A flag which may be set from within a debugger to disable the hang
 * monitor.
 */
volatile bool gDebugDisableHangMonitor = false;

const char kHangMonitorPrefName[] = "hangmonitor.timeout";

const char kTelemetryPrefName[] = "toolkit.telemetry.enabled";

// Monitor protects gShutdown and gTimeout, but not gTimestamp which rely on
// being atomically set by the processor; synchronization doesn't really matter
// in this use case.
Monitor* gMonitor;

// The timeout preference, in seconds.
PRInt32 gTimeout;

PRThread* gThread;

// Set when shutdown begins to signal the thread to exit immediately.
bool gShutdown;

// The timestamp of the last event notification, or PR_INTERVAL_NO_WAIT if
// we're currently not processing events.
volatile PRIntervalTime gTimestamp = PR_INTERVAL_NO_WAIT;

#ifdef REPORT_CHROME_HANGS
// Main thread ID used in reporting chrome hangs under Windows
static HANDLE winMainThreadHandle = NULL;

// Default timeout for reporting chrome hangs to Telemetry (5 seconds)
static const PRInt32 DEFAULT_CHROME_HANG_INTERVAL = 5;

// Maximum number of PCs to gather from the stack
static const PRInt32 MAX_CALL_STACK_PCS = 400;
#endif

// PrefChangedFunc
int
PrefChanged(const char*, void*)
{
  PRInt32 newval = Preferences::GetInt(kHangMonitorPrefName);
#ifdef REPORT_CHROME_HANGS
  // Monitor chrome hangs on the profiling branch if Telemetry enabled
  if (newval == 0) {
    bool telemetryEnabled = Preferences::GetBool(kTelemetryPrefName);
    if (telemetryEnabled) {
      newval = DEFAULT_CHROME_HANG_INTERVAL;
    }
  }
#endif
  MonitorAutoLock lock(*gMonitor);
  if (newval != gTimeout) {
    gTimeout = newval;
    lock.Notify();
  }

  return 0;
}

void
Crash()
{
  if (gDebugDisableHangMonitor) {
    return;
  }

#ifdef XP_WIN
  if (::IsDebuggerPresent()) {
    return;
  }
#endif

#ifdef MOZ_CRASHREPORTER
  CrashReporter::AnnotateCrashReport(NS_LITERAL_CSTRING("Hang"),
                                     NS_LITERAL_CSTRING("1"));
#endif

  NS_RUNTIMEABORT("HangMonitor triggered");
}

#ifdef REPORT_CHROME_HANGS
static void
ChromeStackWalker(void *aPC, void *aSP, void *aClosure)
{
  MOZ_ASSERT(aClosure);
  Telemetry::HangStack *callStack =
    reinterpret_cast< Telemetry::HangStack* >(aClosure);

  if (callStack->Length() < MAX_CALL_STACK_PCS)
    callStack->AppendElement(reinterpret_cast<uintptr_t>(aPC));
}

static void
GetChromeHangReport(Telemetry::HangStack &callStack, SharedLibraryInfo &moduleMap)
{
  MOZ_ASSERT(winMainThreadHandle);

  // The thread we're about to suspend might have the alloc lock
  // so allocate ahead of time
  callStack.SetCapacity(MAX_CALL_STACK_PCS);

  DWORD ret = ::SuspendThread(winMainThreadHandle);
  if (ret == -1) {
    callStack.Clear();
    moduleMap.Clear();
    return;
  }
  NS_StackWalk(ChromeStackWalker, 0, &callStack,
               reinterpret_cast<uintptr_t>(winMainThreadHandle));
  ret = ::ResumeThread(winMainThreadHandle);
  if (ret == -1) {
    callStack.Clear();
    moduleMap.Clear();
    return;
  }

  moduleMap = SharedLibraryInfo::GetInfoForSelf();
  moduleMap.SortByAddress();

  // Remove all modules not referenced by a PC on the stack
  Telemetry::HangStack sortedStack = callStack;
  sortedStack.Sort();

  size_t moduleIndex = 0;
  size_t stackIndex = 0;
  bool unreferencedModule = true;
  while (stackIndex < sortedStack.Length() && moduleIndex < moduleMap.GetSize()) {
    uintptr_t pc = sortedStack[stackIndex];
    SharedLibrary& module = moduleMap.GetEntry(moduleIndex);
    uintptr_t moduleStart = module.GetStart();
    uintptr_t moduleEnd = module.GetEnd() - 1;
    if (moduleStart <= pc && pc <= moduleEnd) {
      // If the current PC is within the current module, mark module as used
      unreferencedModule = false;
      ++stackIndex;
    } else if (pc > moduleEnd) {
      if (unreferencedModule) {
        // Remove module if no PCs within its address range
        moduleMap.RemoveEntries(moduleIndex, moduleIndex + 1);
      } else {
        // Module was referenced on stack, but current PC belongs to later module
        unreferencedModule = true;
        ++moduleIndex;
      }
    } else {
      // PC does not belong to any module
      ++stackIndex;
    }
  }

  // Clean up remaining unreferenced modules, i.e. module addresses > max(pc)
  if (moduleIndex + 1 < moduleMap.GetSize()) {
    moduleMap.RemoveEntries(moduleIndex + 1, moduleMap.GetSize());
  }
}
#endif

void
ThreadMain(void*)
{
  PR_SetCurrentThreadName("Hang Monitor");

  MonitorAutoLock lock(*gMonitor);

  // In order to avoid issues with the hang monitor incorrectly triggering
  // during a general system stop such as sleeping, the monitor thread must
  // run twice to trigger hang protection.
  PRIntervalTime lastTimestamp = 0;
  int waitCount = 0;

#ifdef REPORT_CHROME_HANGS
  Telemetry::HangStack hangStack;
  SharedLibraryInfo hangModuleMap;
#endif

  while (true) {
    if (gShutdown) {
      return; // Exit the thread
    }

    // avoid rereading the volatile value in this loop
    PRIntervalTime timestamp = gTimestamp;

    PRIntervalTime now = PR_IntervalNow();

    if (timestamp != PR_INTERVAL_NO_WAIT &&
        now < timestamp) {
      // 32-bit overflow, reset for another waiting period
      timestamp = 1; // lowest legal PRInterval value
    }

    if (timestamp != PR_INTERVAL_NO_WAIT &&
        timestamp == lastTimestamp &&
        gTimeout > 0) {
      ++waitCount;
      if (waitCount == 2) {
#ifdef REPORT_CHROME_HANGS
        GetChromeHangReport(hangStack, hangModuleMap);
#else
        PRInt32 delay =
          PRInt32(PR_IntervalToSeconds(now - timestamp));
        if (delay > gTimeout) {
          MonitorAutoUnlock unlock(*gMonitor);
          Crash();
        }
#endif
      }
    }
    else {
#ifdef REPORT_CHROME_HANGS
      if (waitCount >= 2) {
        PRUint32 hangDuration = PR_IntervalToSeconds(now - lastTimestamp);
        Telemetry::RecordChromeHang(hangDuration, hangStack, hangModuleMap);
        hangStack.Clear();
        hangModuleMap.Clear();
      }
#endif
      lastTimestamp = timestamp;
      waitCount = 0;
    }

    PRIntervalTime timeout;
    if (gTimeout <= 0) {
      timeout = PR_INTERVAL_NO_TIMEOUT;
    }
    else {
      timeout = PR_MillisecondsToInterval(gTimeout * 500);
    }
    lock.Wait(timeout);
  }
}

void
Startup()
{
  // The hang detector only runs in chrome processes. If you change this,
  // you must also deal with the threadsafety of AnnotateCrashReport in
  // non-chrome processes!
  if (GeckoProcessType_Default != XRE_GetProcessType())
    return;

  NS_ASSERTION(!gMonitor, "Hang monitor already initialized");
  gMonitor = new Monitor("HangMonitor");

  Preferences::RegisterCallback(PrefChanged, kHangMonitorPrefName, NULL);
  PrefChanged(NULL, NULL);

#ifdef REPORT_CHROME_HANGS
  Preferences::RegisterCallback(PrefChanged, kTelemetryPrefName, NULL);
  winMainThreadHandle =
    OpenThread(THREAD_ALL_ACCESS, FALSE, GetCurrentThreadId());
  if (!winMainThreadHandle)
    return;
#endif

  // Don't actually start measuring hangs until we hit the main event loop.
  // This potentially misses a small class of really early startup hangs,
  // but avoids dealing with some xpcshell tests and other situations which
  // start XPCOM but don't ever start the event loop.
  Suspend();

  gThread = PR_CreateThread(PR_USER_THREAD,
                            ThreadMain,
                            NULL, PR_PRIORITY_LOW, PR_GLOBAL_THREAD,
                            PR_JOINABLE_THREAD, 0);
}

void
Shutdown()
{
  if (GeckoProcessType_Default != XRE_GetProcessType())
    return;

  NS_ASSERTION(gMonitor, "Hang monitor not started");

  { // Scope the lock we're going to delete later
    MonitorAutoLock lock(*gMonitor);
    gShutdown = true;
    lock.Notify();
  }

  // thread creation could theoretically fail
  if (gThread) {
    PR_JoinThread(gThread);
    gThread = NULL;
  }

  delete gMonitor;
  gMonitor = NULL;
}

static bool
IsUIMessageWaiting()
{
#ifndef XP_WIN
  return false;
#else
  #define NS_WM_IMEFIRST WM_IME_SETCONTEXT
  #define NS_WM_IMELAST  WM_IME_KEYUP
  BOOL haveUIMessageWaiting = FALSE;
  MSG msg;
  haveUIMessageWaiting |= ::PeekMessageW(&msg, NULL, WM_KEYFIRST, 
                                         WM_IME_KEYLAST, PM_NOREMOVE);
  haveUIMessageWaiting |= ::PeekMessageW(&msg, NULL, NS_WM_IMEFIRST,
                                         NS_WM_IMELAST, PM_NOREMOVE);
  haveUIMessageWaiting |= ::PeekMessageW(&msg, NULL, WM_MOUSEFIRST,
                                         WM_MOUSELAST, PM_NOREMOVE);
  return haveUIMessageWaiting;
#endif
}

void
NotifyActivity(ActivityType activityType)
{
  NS_ASSERTION(NS_IsMainThread(),
    "HangMonitor::Notify called from off the main thread.");

  // Determine the activity type more specifically
  if (activityType == kGeneralActivity) {
    activityType = IsUIMessageWaiting() ? kActivityUIAVail : 
                                          kActivityNoUIAVail;
  }

  // Calculate the cumulative amount of lag time since the last UI message
  static PRUint32 cumulativeUILagMS = 0;
  switch(activityType) {
  case kActivityNoUIAVail:
    cumulativeUILagMS = 0;
    break;
  case kActivityUIAVail:
  case kUIActivity:
    if (gTimestamp != PR_INTERVAL_NO_WAIT) {
      cumulativeUILagMS += PR_IntervalToMilliseconds(PR_IntervalNow() -
                                                     gTimestamp);
    }
    break;
  }

  // This is not a locked activity because PRTimeStamp is a 32-bit quantity
  // which can be read/written atomically, and we don't want to pay locking
  // penalties here.
  gTimestamp = PR_IntervalNow();

  // If we have UI activity we should reset the timer and report it if it is
  // significant enough.
  if (activityType == kUIActivity) {
    // The minimum amount of lag time that we should report for telemetry data.
    // Mozilla's UI responsiveness goal is 50ms
    static const PRUint32 kUIResponsivenessThresholdMS = 50;
    if (cumulativeUILagMS > kUIResponsivenessThresholdMS) {
      mozilla::Telemetry::Accumulate(mozilla::Telemetry::EVENTLOOP_UI_LAG_EXP_MS,
                                     cumulativeUILagMS);
    }
    cumulativeUILagMS = 0;
  }
}

void
Suspend()
{
  NS_ASSERTION(NS_IsMainThread(), "HangMonitor::Suspend called from off the main thread.");

  // Because gTimestamp changes this resets the wait count.
  gTimestamp = PR_INTERVAL_NO_WAIT;
}

} } // namespace mozilla::HangMonitor
