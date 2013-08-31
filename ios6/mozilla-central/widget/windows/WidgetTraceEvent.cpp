/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Windows widget support for event loop instrumentation.
 * See toolkit/xre/EventTracer.cpp for more details.
 */

#include <stdio.h>
#include <windows.h>

#include "mozilla/WidgetTraceEvent.h"
#include "nsAppShellCID.h"
#include "nsComponentManagerUtils.h"
#include "nsCOMPtr.h"
#include "nsIAppShellService.h"
#include "nsIBaseWindow.h"
#include "nsIDocShell.h"
#include "nsIWidget.h"
#include "nsIXULWindow.h"
#include "nsAutoPtr.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nsWindowDefs.h"

namespace {

// Used for signaling the background thread from the main thread.
HANDLE sEventHandle = NULL;

// We need a runnable in order to find the hidden window on the main
// thread.
class HWNDGetter : public nsRunnable {
public:
  HWNDGetter() : hidden_window_hwnd(NULL) {}

  HWND hidden_window_hwnd;

  NS_IMETHOD Run() {
    // Jump through some hoops to locate the hidden window.
    nsCOMPtr<nsIAppShellService> appShell(do_GetService(NS_APPSHELLSERVICE_CONTRACTID));
    nsCOMPtr<nsIXULWindow> hiddenWindow;

    nsresult rv = appShell->GetHiddenWindow(getter_AddRefs(hiddenWindow));
    if (NS_FAILED(rv)) {
      return rv;
    }

    nsCOMPtr<nsIDocShell> docShell;
    rv = hiddenWindow->GetDocShell(getter_AddRefs(docShell));
    if (NS_FAILED(rv) || !docShell) {
      return rv;
    }

    nsCOMPtr<nsIBaseWindow> baseWindow(do_QueryInterface(docShell));
    
    if (!baseWindow)
      return NS_ERROR_FAILURE;

    nsCOMPtr<nsIWidget> widget;
    baseWindow->GetMainWidget(getter_AddRefs(widget));

    if (!widget)
      return NS_ERROR_FAILURE;

    hidden_window_hwnd = (HWND)widget->GetNativeData(NS_NATIVE_WINDOW);

    return NS_OK;
  }
};

HWND GetHiddenWindowHWND()
{
  // Need to dispatch this to the main thread because plenty of
  // the things it wants to access are main-thread-only.
  nsRefPtr<HWNDGetter> getter = new HWNDGetter();
  NS_DispatchToMainThread(getter, NS_DISPATCH_SYNC);
  return getter->hidden_window_hwnd;
}

} // namespace

namespace mozilla {

bool InitWidgetTracing()
{
  sEventHandle = CreateEvent(NULL, FALSE, FALSE, NULL);
  return sEventHandle != NULL;
}

void CleanUpWidgetTracing()
{
  CloseHandle(sEventHandle);
  sEventHandle = NULL;
}

// This function is called from the main (UI) thread.
void SignalTracerThread()
{
  if (sEventHandle != NULL)
    SetEvent(sEventHandle);
}

// This function is called from the background tracer thread.
bool FireAndWaitForTracerEvent()
{
  NS_ABORT_IF_FALSE(sEventHandle, "Tracing not initialized!");

  // First, try to find the hidden window.
  static HWND hidden_window = NULL;
  if (hidden_window == NULL) {
    hidden_window = GetHiddenWindowHWND();
  }

  if (hidden_window == NULL)
    return false;

  // Post the tracer message into the hidden window's message queue,
  // and then block until it's processed.
  PostMessage(hidden_window, MOZ_WM_TRACE, 0, 0);
  WaitForSingleObject(sEventHandle, INFINITE);
  return true;
}

}  // namespace mozilla
