/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZ_LOGGING
#define FORCE_PR_LOG /* Allow logging in the release build */
#endif // MOZ_LOGGING
#include "prlog.h"

#include "WinMouseScrollHandler.h"
#include "nsWindow.h"
#include "KeyboardLayout.h"
#include "WinUtils.h"
#include "nsGkAtoms.h"
#include "nsIDOMWindowUtils.h"

#include "mozilla/Preferences.h"

#include <psapi.h>

namespace mozilla {
namespace widget {

#ifdef PR_LOGGING
PRLogModuleInfo* gMouseScrollLog = nsnull;

static const char* GetBoolName(bool aBool)
{
  return aBool ? "TRUE" : "FALSE";
}

static void LogKeyStateImpl()
{
  if (!PR_LOG_TEST(gMouseScrollLog, PR_LOG_DEBUG)) {
    return;
  }
  BYTE keyboardState[256];
  if (::GetKeyboardState(keyboardState)) {
    for (size_t i = 0; i < ArrayLength(keyboardState); i++) {
      if (keyboardState[i]) {
        PR_LOG(gMouseScrollLog, PR_LOG_DEBUG,
          ("    Current key state: keyboardState[0x%02X]=0x%02X (%s)",
           i, keyboardState[i],
           ((keyboardState[i] & 0x81) == 0x81) ? "Pressed and Toggled" :
           (keyboardState[i] & 0x80) ? "Pressed" :
           (keyboardState[i] & 0x01) ? "Toggled" : "Unknown"));
      }
    }
  } else {
    PR_LOG(gMouseScrollLog, PR_LOG_DEBUG,
      ("MouseScroll::Device::Elantech::HandleKeyMessage(): Failed to print "
       "current keyboard state"));
  }
}

#define LOG_KEYSTATE() LogKeyStateImpl()
#else // PR_LOGGING
#define LOG_KEYSTATE()
#endif

MouseScrollHandler* MouseScrollHandler::sInstance = nsnull;

bool MouseScrollHandler::Device::sFakeScrollableWindowNeeded = false;

bool MouseScrollHandler::Device::Elantech::sUseSwipeHack = false;
bool MouseScrollHandler::Device::Elantech::sUsePinchHack = false;
DWORD MouseScrollHandler::Device::Elantech::sZoomUntil = 0;

bool MouseScrollHandler::Device::SetPoint::sMightBeUsing = false;

// The duration until timeout of events transaction.  The value is 1.5 sec,
// it's just a magic number, it was suggested by Logitech's engineer, see
// bug 605648 comment 90.
#define DEFAULT_TIMEOUT_DURATION 1500

/******************************************************************************
 *
 * MouseScrollHandler
 *
 ******************************************************************************/

/* static */
POINTS
MouseScrollHandler::GetCurrentMessagePos()
{
  if (SynthesizingEvent::IsSynthesizing()) {
    return sInstance->mSynthesizingEvent->GetCursorPoint();
  }
  DWORD pos = ::GetMessagePos();
  return MAKEPOINTS(pos);
}

// Get rid of the GetMessagePos() API.
#define GetMessagePos()

/* static */
void
MouseScrollHandler::Initialize()
{
#ifdef PR_LOGGING
  if (!gMouseScrollLog) {
    gMouseScrollLog = PR_NewLogModule("MouseScrollHandlerWidgets");
  }
#endif
  Device::Init();
}

/* static */
void
MouseScrollHandler::Shutdown()
{
  delete sInstance;
  sInstance = nsnull;
}

/* static */
MouseScrollHandler*
MouseScrollHandler::GetInstance()
{
  if (!sInstance) {
    sInstance = new MouseScrollHandler();
  }
  return sInstance;
}

MouseScrollHandler::MouseScrollHandler() :
  mIsWaitingInternalMessage(false),
  mSynthesizingEvent(nsnull)
{
  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll: Creating an instance, this=%p, sInstance=%p",
     this, sInstance));
}

MouseScrollHandler::~MouseScrollHandler()
{
  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll: Destroying an instance, this=%p, sInstance=%p",
     this, sInstance));

  delete mSynthesizingEvent;
}

/* static */
bool
MouseScrollHandler::ProcessMessage(nsWindow* aWindow, UINT msg,
                                   WPARAM wParam, LPARAM lParam,
                                   LRESULT *aRetValue, bool &aEatMessage)
{
  Device::Elantech::UpdateZoomUntil();

  switch (msg) {
    case WM_SETTINGCHANGE:
      if (!sInstance) {
        return false;
      }
      if (wParam == SPI_SETWHEELSCROLLLINES ||
          wParam == SPI_SETWHEELSCROLLCHARS) {
        sInstance->mSystemSettings.MarkDirty();
      }
      return false;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
      GetInstance()->
        ProcessNativeMouseWheelMessage(aWindow, msg, wParam, lParam);
      sInstance->mSynthesizingEvent->NotifyNativeMessageHandlingFinished();
      // We don't need to call next wndproc for WM_MOUSEWHEEL and
      // WM_MOUSEHWHEEL.  We should consume them always.  If the messages
      // would be handled by our window again, it caused making infinite
      // message loop.
      aEatMessage = true;
      *aRetValue = (msg != WM_MOUSEHWHEEL);
      return true;

    case WM_HSCROLL:
    case WM_VSCROLL:
      aEatMessage =
        GetInstance()->ProcessNativeScrollMessage(aWindow, msg, wParam, lParam);
      sInstance->mSynthesizingEvent->NotifyNativeMessageHandlingFinished();
      *aRetValue = 0;
      return true;

    case MOZ_WM_MOUSEVWHEEL:
    case MOZ_WM_MOUSEHWHEEL:
      GetInstance()->HandleMouseWheelMessage(aWindow, msg, wParam, lParam);
      sInstance->mSynthesizingEvent->NotifyInternalMessageHandlingFinished();
      // Doesn't need to call next wndproc for internal wheel message.
      aEatMessage = true;
      return true;

    case MOZ_WM_HSCROLL:
    case MOZ_WM_VSCROLL:
      GetInstance()->
        HandleScrollMessageAsMouseWheelMessage(aWindow, msg, wParam, lParam);
      sInstance->mSynthesizingEvent->NotifyInternalMessageHandlingFinished();
      // Doesn't need to call next wndproc for internal scroll message.
      aEatMessage = true;
      return true;

    case WM_KEYDOWN:
    case WM_KEYUP:
      PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
        ("MouseScroll::ProcessMessage(): aWindow=%p, "
         "msg=%s(0x%04X), wParam=0x%02X, ::GetMessageTime()=%d",
         aWindow, msg == WM_KEYDOWN ? "WM_KEYDOWN" :
                    msg == WM_KEYUP ? "WM_KEYUP" : "Unknown", msg, wParam,
         ::GetMessageTime()));
      LOG_KEYSTATE();
      if (Device::Elantech::HandleKeyMessage(aWindow, msg, wParam)) {
        *aRetValue = 0;
        aEatMessage = true;
        return true;
      }
      return false;

    default:
      return false;
  }
}

/* static */
nsresult
MouseScrollHandler::SynthesizeNativeMouseScrollEvent(nsWindow* aWindow,
                                                     const nsIntPoint& aPoint,
                                                     PRUint32 aNativeMessage,
                                                     PRInt32 aDelta,
                                                     PRUint32 aModifierFlags,
                                                     PRUint32 aAdditionalFlags)
{
  bool useFocusedWindow =
    !(aAdditionalFlags & nsIDOMWindowUtils::MOUSESCROLL_PREFER_WIDGET_AT_POINT);

  POINT pt;
  pt.x = aPoint.x;
  pt.y = aPoint.y;

  HWND target = useFocusedWindow ? ::WindowFromPoint(pt) : ::GetFocus();
  NS_ENSURE_TRUE(target, NS_ERROR_FAILURE);

  WPARAM wParam = 0;
  LPARAM lParam = 0;
  switch (aNativeMessage) {
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
      lParam = MAKELPARAM(pt.x, pt.y);
      WORD mod = 0;
      if (aModifierFlags & (nsIWidget::CTRL_L | nsIWidget::CTRL_R)) {
        mod |= MK_CONTROL;
      }
      if (aModifierFlags & (nsIWidget::SHIFT_L | nsIWidget::SHIFT_R)) {
        mod |= MK_SHIFT;
      }
      wParam = MAKEWPARAM(mod, aDelta);
      break;
    }
    case WM_VSCROLL:
    case WM_HSCROLL:
      lParam = (aAdditionalFlags &
                  nsIDOMWindowUtils::MOUSESCROLL_WIN_SCROLL_LPARAM_NOT_NULL) ?
        reinterpret_cast<LPARAM>(target) : NULL;
      wParam = aDelta;
      break;
    default:
      return NS_ERROR_INVALID_ARG;
  }

  // Ensure to make the instance.
  GetInstance();

  BYTE kbdState[256];
  memset(kbdState, 0, sizeof(kbdState));

  nsAutoTArray<KeyPair,10> keySequence;
  nsWindow::SetupKeyModifiersSequence(&keySequence, aModifierFlags);

  for (PRUint32 i = 0; i < keySequence.Length(); ++i) {
    PRUint8 key = keySequence[i].mGeneral;
    PRUint8 keySpecific = keySequence[i].mSpecific;
    kbdState[key] = 0x81; // key is down and toggled on if appropriate
    if (keySpecific) {
      kbdState[keySpecific] = 0x81;
    }
  }

  if (!sInstance->mSynthesizingEvent) {
    sInstance->mSynthesizingEvent = new SynthesizingEvent();
  }

  POINTS pts;
  pts.x = static_cast<SHORT>(pt.x);
  pts.y = static_cast<SHORT>(pt.y);
  return sInstance->mSynthesizingEvent->
           Synthesize(pts, target, aNativeMessage, wParam, lParam, kbdState);
}

/* static */
bool
MouseScrollHandler::DispatchEvent(nsWindow* aWindow, nsGUIEvent& aEvent)
{
  return aWindow->DispatchWindowEvent(&aEvent);
}

/* static */
void
MouseScrollHandler::InitEvent(nsWindow* aWindow,
                              nsGUIEvent& aEvent,
                              nsIntPoint* aPoint)
{
  NS_ENSURE_TRUE(aWindow, );
  nsIntPoint point;
  if (aPoint) {
    point = *aPoint;
  } else {
    POINTS pts = GetCurrentMessagePos();
    POINT pt;
    pt.x = pts.x;
    pt.y = pts.y;
    ::ScreenToClient(aWindow->GetWindowHandle(), &pt);
    point.x = pt.x;
    point.y = pt.y;
  }
  aWindow->InitEvent(aEvent, &point);
}

/* static */
ModifierKeyState
MouseScrollHandler::GetModifierKeyState(UINT aMessage)
{
  ModifierKeyState result;
  // Assume the Control key is down if the Elantech touchpad has sent the
  // mis-ordered WM_KEYDOWN/WM_MOUSEWHEEL messages.  (See the comment in
  // MouseScrollHandler::Device::Elantech::HandleKeyMessage().)
  if ((aMessage == MOZ_WM_MOUSEVWHEEL || aMessage == WM_MOUSEWHEEL) &&
      !result.IsControl() && Device::Elantech::IsZooming()) {
    result.Set(MODIFIER_CONTROL);
  }
  return result;
}

POINT
MouseScrollHandler::ComputeMessagePos(UINT aMessage,
                                      WPARAM aWParam,
                                      LPARAM aLParam)
{
  POINT point;
  if (Device::SetPoint::IsGetMessagePosResponseValid(aMessage,
                                                     aWParam, aLParam)) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::ComputeMessagePos: Using ::GetCursorPos()"));
    ::GetCursorPos(&point);
  } else {
    POINTS pts = GetCurrentMessagePos();
    point.x = pts.x;
    point.y = pts.y;
  }
  return point;
}

MouseScrollHandler::ScrollTargetInfo
MouseScrollHandler::GetScrollTargetInfo(
                      nsWindow* aWindow,
                      const EventInfo& aEventInfo,
                      const ModifierKeyState& aModifierKeyState)
{
  ScrollTargetInfo result;
  result.dispatchPixelScrollEvent = false;
  result.reversePixelScrollDirection = false;
  result.actualScrollAmount = aEventInfo.GetScrollAmount();
  result.actualScrollAction = nsQueryContentEvent::SCROLL_ACTION_NONE;
  result.pixelsPerUnit = 0;
  if (!mUserPrefs.IsPixelScrollingEnabled()) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::GetPixelScrollInfo: Succeeded, aWindow=%p, "
       "result: { dispatchPixelScrollEvent: %s, actualScrollAmount: %d }",
       aWindow, GetBoolName(result.dispatchPixelScrollEvent),
       result.actualScrollAmount));
    return result;
  }

  nsMouseScrollEvent testEvent(true, NS_MOUSE_SCROLL, aWindow);
  InitEvent(aWindow, testEvent);
  aModifierKeyState.InitInputEvent(testEvent);

  testEvent.scrollFlags = aEventInfo.GetScrollFlags();
  testEvent.delta       = result.actualScrollAmount;
  if ((aEventInfo.IsVertical() && aEventInfo.IsPositive()) ||
      (!aEventInfo.IsVertical() && !aEventInfo.IsPositive())) {
    testEvent.delta *= -1;
  }

  nsQueryContentEvent queryEvent(true, NS_QUERY_SCROLL_TARGET_INFO, aWindow);
  InitEvent(aWindow, queryEvent);
  queryEvent.InitForQueryScrollTargetInfo(&testEvent);
  DispatchEvent(aWindow, queryEvent);

  // If the necessary interger isn't larger than 0, we should assume that
  // the event failed for us.
  if (!queryEvent.mSucceeded) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::GetPixelScrollInfo: Failed to query the "
       "scroll target information, aWindow=%p"
       "result: { dispatchPixelScrollEvent: %s, actualScrollAmount: %d }",
       aWindow, GetBoolName(result.dispatchPixelScrollEvent),
       result.actualScrollAmount));
    return result;
  }

  result.actualScrollAction = queryEvent.mReply.mComputedScrollAction;

  if (result.actualScrollAction == nsQueryContentEvent::SCROLL_ACTION_PAGE) {
    result.pixelsPerUnit =
      aEventInfo.IsVertical() ? queryEvent.mReply.mPageHeight :
                                queryEvent.mReply.mPageWidth;
  } else {
    result.pixelsPerUnit = queryEvent.mReply.mLineHeight;
  }

  result.actualScrollAmount = queryEvent.mReply.mComputedScrollAmount;

  if (result.pixelsPerUnit > 0 && result.actualScrollAmount != 0 &&
      result.actualScrollAction != nsQueryContentEvent::SCROLL_ACTION_NONE) {
    result.dispatchPixelScrollEvent = true;
    // If original delta's sign and computed delta's one are different,
    // we need to reverse the pixel scroll direction at dispatching it.
    result.reversePixelScrollDirection =
      (testEvent.delta > 0 && result.actualScrollAmount < 0) ||
      (testEvent.delta < 0 && result.actualScrollAmount > 0);
    // scroll amount must be positive.
    result.actualScrollAmount = NS_ABS(result.actualScrollAmount);
  }

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::GetPixelScrollInfo: Succeeded, aWindow=%p, "
     "result: { dispatchPixelScrollEvent: %s, reversePixelScrollDirection: %s, "
     "actualScrollAmount: %d, actualScrollAction: 0x%01X, "
     "pixelsPerUnit: %d }",
     aWindow, GetBoolName(result.dispatchPixelScrollEvent),
     GetBoolName(result.reversePixelScrollDirection), result.actualScrollAmount,
     result.actualScrollAction, result.pixelsPerUnit));

  return result;
}

void
MouseScrollHandler::ProcessNativeMouseWheelMessage(nsWindow* aWindow,
                                                   UINT aMessage,
                                                   WPARAM aWParam,
                                                   LPARAM aLParam)
{
  if (SynthesizingEvent::IsSynthesizing()) {
    mSynthesizingEvent->NativeMessageReceived(aWindow, aMessage,
                                              aWParam, aLParam);
  }

  POINT point = ComputeMessagePos(aMessage, aWParam, aLParam);

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::ProcessNativeMouseWheelMessage: aWindow=%p, "
     "aMessage=%s, wParam=0x%08X, lParam=0x%08X, point: { x=%d, y=%d }",
     aWindow, aMessage == WM_MOUSEWHEEL ? "WM_MOUSEWHEEL" :
              aMessage == WM_MOUSEHWHEEL ? "WM_MOUSEHWHEEL" :
              aMessage == WM_VSCROLL ? "WM_VSCROLL" : "WM_HSCROLL",
     aWParam, aLParam, point.x, point.y));
  LOG_KEYSTATE();

  HWND underCursorWnd = ::WindowFromPoint(point);
  if (!underCursorWnd) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::ProcessNativeMouseWheelMessage: "
       "No window is not found under the cursor"));
    return;
  }

  if (Device::Elantech::IsPinchHackNeeded() &&
      Device::Elantech::IsHelperWindow(underCursorWnd)) {
    // The Elantech driver places a window right underneath the cursor
    // when sending a WM_MOUSEWHEEL event to us as part of a pinch-to-zoom
    // gesture.  We detect that here, and search for our window that would
    // be beneath the cursor if that window wasn't there.
    underCursorWnd = WinUtils::FindOurWindowAtPoint(point);
    if (!underCursorWnd) {
      PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
        ("MouseScroll::ProcessNativeMouseWheelMessage: "
         "Our window is not found under the Elantech helper window"));
      return;
    }
  }

  // Handle most cases first.  If the window under mouse cursor is our window
  // except plugin window (MozillaWindowClass), we should handle the message
  // on the window.
  if (WinUtils::IsOurProcessWindow(underCursorWnd)) {
    nsWindow* destWindow = WinUtils::GetNSWindowPtr(underCursorWnd);
    if (!destWindow) {
      PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
        ("MouseScroll::ProcessNativeMouseWheelMessage: "
         "Found window under the cursor isn't managed by nsWindow..."));
      HWND wnd = ::GetParent(underCursorWnd);
      for (; wnd; wnd = ::GetParent(wnd)) {
        destWindow = WinUtils::GetNSWindowPtr(wnd);
        if (destWindow) {
          break;
        }
      }
      if (!wnd) {
        PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
          ("MouseScroll::ProcessNativeMouseWheelMessage: Our window which is "
           "managed by nsWindow is not found under the cursor"));
        return;
      }
    }

    MOZ_ASSERT(destWindow, "destWindow must not be NULL");

    // If the found window is our plugin window, it means that the message
    // has been handled by the plugin but not consumed.  We should handle the
    // message on its parent window.  However, note that the DOM event may
    // cause accessing the plugin.  Therefore, we should unlock the plugin
    // process by using PostMessage().
    if (destWindow->GetWindowType() == eWindowType_plugin) {
      destWindow = destWindow->GetParentWindow(false);
      if (!destWindow) {
        PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
          ("MouseScroll::ProcessNativeMouseWheelMessage: "
           "Our window which is a parent of a plugin window is not found"));
        return;
      }
    }
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::ProcessNativeMouseWheelMessage: Succeeded, "
       "Posting internal message to an nsWindow (%p)...",
       destWindow));
    mIsWaitingInternalMessage = true;
    UINT internalMessage = WinUtils::GetInternalMessage(aMessage);
    ::PostMessage(destWindow->GetWindowHandle(), internalMessage,
                  aWParam, aLParam);
    return;
  }

  // If the window under cursor is not in our process, it means:
  // 1. The window may be a plugin window (GeckoPluginWindow or its descendant).
  // 2. The window may be another application's window.
  HWND pluginWnd = WinUtils::FindOurProcessWindow(underCursorWnd);
  if (!pluginWnd) {
    // If there is no plugin window in ancestors of the window under cursor,
    // the window is for another applications (case 2).
    // We don't need to handle this message.
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::ProcessNativeMouseWheelMessage: "
       "Our window is not found under the cursor"));
    return;
  }

  // If we're a plugin window (MozillaWindowClass) and cursor in this window,
  // the message shouldn't go to plugin's wndproc again.  So, we should handle
  // it on parent window.  However, note that the DOM event may cause accessing
  // the plugin.  Therefore, we should unlock the plugin process by using
  // PostMessage().
  if (aWindow->GetWindowType() == eWindowType_plugin &&
      aWindow->GetWindowHandle() == pluginWnd) {
    nsWindow* destWindow = aWindow->GetParentWindow(false);
    if (!destWindow) {
      PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
        ("MouseScroll::ProcessNativeMouseWheelMessage: Our normal window which "
         "is a parent of this plugin window is not found"));
      return;
    }
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::ProcessNativeMouseWheelMessage: Succeeded, "
       "Posting internal message to an nsWindow (%p) which is parent of this "
       "plugin window...",
       destWindow));
    mIsWaitingInternalMessage = true;
    UINT internalMessage = WinUtils::GetInternalMessage(aMessage);
    ::PostMessage(destWindow->GetWindowHandle(), internalMessage,
                  aWParam, aLParam);
    return;
  }

  // If the window is a part of plugin, we should post the message to it.
  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::ProcessNativeMouseWheelMessage: Succeeded, "
     "Redirecting the message to a window which is a plugin child window"));
  ::PostMessage(underCursorWnd, aMessage, aWParam, aLParam);
}

bool
MouseScrollHandler::ProcessNativeScrollMessage(nsWindow* aWindow,
                                               UINT aMessage,
                                               WPARAM aWParam,
                                               LPARAM aLParam)
{
  if (aLParam || mUserPrefs.IsScrollMessageHandledAsWheelMessage()) {
    // Scroll message generated by Thinkpad Trackpoint Driver or similar
    // Treat as a mousewheel message and scroll appropriately
    ProcessNativeMouseWheelMessage(aWindow, aMessage, aWParam, aLParam);
    // Always consume the scroll message if we try to emulate mouse wheel
    // action.
    return true;
  }

  if (SynthesizingEvent::IsSynthesizing()) {
    mSynthesizingEvent->NativeMessageReceived(aWindow, aMessage,
                                              aWParam, aLParam);
  }

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::ProcessNativeScrollMessage: aWindow=%p, "
     "aMessage=%s, wParam=0x%08X, lParam=0x%08X",
     aWindow, aMessage == WM_VSCROLL ? "WM_VSCROLL" : "WM_HSCROLL",
     aWParam, aLParam));

  // Scroll message generated by external application
  nsContentCommandEvent commandEvent(true, NS_CONTENT_COMMAND_SCROLL, aWindow);

  commandEvent.mScroll.mIsHorizontal = (aMessage == WM_HSCROLL);

  switch (LOWORD(aWParam)) {
    case SB_LINEUP:   // SB_LINELEFT
      commandEvent.mScroll.mUnit = nsContentCommandEvent::eCmdScrollUnit_Line;
      commandEvent.mScroll.mAmount = -1;
      break;
    case SB_LINEDOWN: // SB_LINERIGHT
      commandEvent.mScroll.mUnit = nsContentCommandEvent::eCmdScrollUnit_Line;
      commandEvent.mScroll.mAmount = 1;
      break;
    case SB_PAGEUP:   // SB_PAGELEFT
      commandEvent.mScroll.mUnit = nsContentCommandEvent::eCmdScrollUnit_Page;
      commandEvent.mScroll.mAmount = -1;
      break;
    case SB_PAGEDOWN: // SB_PAGERIGHT
      commandEvent.mScroll.mUnit = nsContentCommandEvent::eCmdScrollUnit_Page;
      commandEvent.mScroll.mAmount = 1;
      break;
    case SB_TOP:      // SB_LEFT
      commandEvent.mScroll.mUnit = nsContentCommandEvent::eCmdScrollUnit_Whole;
      commandEvent.mScroll.mAmount = -1;
      break;
    case SB_BOTTOM:   // SB_RIGHT
      commandEvent.mScroll.mUnit = nsContentCommandEvent::eCmdScrollUnit_Whole;
      commandEvent.mScroll.mAmount = 1;
      break;
    default:
      return false;
  }
  // XXX If this is a plugin window, we should dispatch the event from
  //     parent window.
  DispatchEvent(aWindow, commandEvent);
  return true;
}

void
MouseScrollHandler::HandleMouseWheelMessage(nsWindow* aWindow,
                                            UINT aMessage,
                                            WPARAM aWParam,
                                            LPARAM aLParam)
{
  NS_ABORT_IF_FALSE(
    (aMessage == MOZ_WM_MOUSEVWHEEL || aMessage == MOZ_WM_MOUSEHWHEEL),
    "HandleMouseWheelMessage must be called with "
    "MOZ_WM_MOUSEVWHEEL or MOZ_WM_MOUSEHWHEEL");

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::HandleMouseWheelMessage: aWindow=%p, "
     "aMessage=MOZ_WM_MOUSE%sWHEEL, aWParam=0x%08X, aLParam=0x%08X",
     aWindow, aMessage == MOZ_WM_MOUSEVWHEEL ? "V" : "H",
     aWParam, aLParam));

  mIsWaitingInternalMessage = false;

  EventInfo eventInfo(aWindow, WinUtils::GetNativeMessage(aMessage),
                      aWParam, aLParam);
  if (!eventInfo.CanDispatchMouseScrollEvent()) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::HandleMouseWheelMessage: Cannot dispatch the events"));
    mLastEventInfo.ResetTransaction();
    return;
  }

  // Discard the remaining delta if current wheel message and last one are
  // received by different window or to scroll different direction or
  // different unit scroll.  Furthermore, if the last event was too old.
  if (!mLastEventInfo.CanContinueTransaction(eventInfo)) {
    mLastEventInfo.ResetTransaction();
  }

  mLastEventInfo.RecordEvent(eventInfo);

  ModifierKeyState modKeyState = GetModifierKeyState(aMessage);

  // Before dispatching line scroll event, we should get the current scroll
  // event target information for pixel scroll.
  ScrollTargetInfo scrollTargetInfo =
    GetScrollTargetInfo(aWindow, eventInfo, modKeyState);

  // Grab the widget, it might be destroyed by a DOM event handler.
  nsRefPtr<nsWindow> kungFuDethGrip(aWindow);

  bool fromLines = false;
  nsMouseScrollEvent scrollEvent(true, NS_MOUSE_SCROLL, aWindow);
  if (mLastEventInfo.InitMouseScrollEvent(aWindow, scrollEvent,
                                          scrollTargetInfo, modKeyState)) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::HandleMouseWheelMessage: dispatching "
       "NS_MOUSE_SCROLL event"));
    fromLines = true;
    DispatchEvent(aWindow, scrollEvent);
    if (aWindow->Destroyed()) {
      PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
        ("MouseScroll::HandleMouseWheelMessage: The window was destroyed "
         "by NS_MOUSE_SCROLL event"));
      mLastEventInfo.ResetTransaction();
      return;
    }
  }
#ifdef PR_LOGGING
  else {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::HandleMouseWheelMessage: NS_MOUSE_SCROLL event is not "
       "dispatched"));
  }
#endif

  nsMouseScrollEvent pixelEvent(true, NS_MOUSE_PIXEL_SCROLL, aWindow);
  if (mLastEventInfo.InitMousePixelScrollEvent(aWindow, pixelEvent,
                                               scrollTargetInfo, modKeyState)) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::HandleMouseWheelMessage: dispatching "
       "NS_MOUSE_PIXEL_SCROLL event"));
    pixelEvent.scrollFlags |= fromLines ? nsMouseScrollEvent::kFromLines : 0;
    DispatchEvent(aWindow, pixelEvent);
    if (aWindow->Destroyed()) {
      PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
        ("MouseScroll::HandleMouseWheelMessage: The window was destroyed "
         "by NS_MOUSE_PIXEL_SCROLL event"));
      mLastEventInfo.ResetTransaction();
      return;
    }
  }
#ifdef PR_LOGGING
  else {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::HandleMouseWheelMessage: NS_MOUSE_PIXEL_SCROLL event is "
       "not dispatched"));
  }
#endif
}

void
MouseScrollHandler::HandleScrollMessageAsMouseWheelMessage(nsWindow* aWindow,
                                                           UINT aMessage,
                                                           WPARAM aWParam,
                                                           LPARAM aLParam)
{
  NS_ABORT_IF_FALSE(
    (aMessage == MOZ_WM_VSCROLL || aMessage == MOZ_WM_HSCROLL),
    "HandleScrollMessageAsMouseWheelMessage must be called with "
    "MOZ_WM_VSCROLL or MOZ_WM_HSCROLL");

  mIsWaitingInternalMessage = false;

  ModifierKeyState modKeyState = GetModifierKeyState(aMessage);

  nsMouseScrollEvent scrollEvent(true, NS_MOUSE_SCROLL, aWindow);
  scrollEvent.scrollFlags =
    (aMessage == MOZ_WM_VSCROLL) ? nsMouseScrollEvent::kIsVertical :
                                   nsMouseScrollEvent::kIsHorizontal;
  switch (LOWORD(aWParam)) {
    case SB_PAGEDOWN:
      scrollEvent.scrollFlags |= nsMouseScrollEvent::kIsFullPage;
    case SB_LINEDOWN:
      scrollEvent.delta = 1;
      break;
    case SB_PAGEUP:
      scrollEvent.scrollFlags |= nsMouseScrollEvent::kIsFullPage;
    case SB_LINEUP:
      scrollEvent.delta = -1;
      break;
    default:
      return;
  }
  modKeyState.InitInputEvent(scrollEvent);
  // XXX Current mouse position may not be same as when the original message
  //     is received.  We need to know the actual mouse cursor position when
  //     the original message was received.
  InitEvent(aWindow, scrollEvent);

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::HandleScrollMessageAsMouseWheelMessage: aWindow=%p, "
     "aMessage=MOZ_WM_%sSCROLL, aWParam=0x%08X, aLParam=0x%08X, "
     "scrollEvent { refPoint: { x: %d, y: %d }, delta: %d, "
     "scrollFlags: 0x%04X, isShift: %s, isControl: %s, isAlt: %s, isMeta: %s }",
     aWindow, (aMessage == MOZ_WM_VSCROLL) ? "V" : "H",
     aWParam, aLParam, scrollEvent.refPoint.x, scrollEvent.refPoint.y,
     scrollEvent.delta, scrollEvent.scrollFlags,
     GetBoolName(scrollEvent.IsShift()),
     GetBoolName(scrollEvent.IsControl()),
     GetBoolName(scrollEvent.IsAlt()),
     GetBoolName(scrollEvent.IsMeta())));

  DispatchEvent(aWindow, scrollEvent);
}

/******************************************************************************
 *
 * EventInfo
 *
 ******************************************************************************/

MouseScrollHandler::EventInfo::EventInfo(nsWindow* aWindow,
                                         UINT aMessage,
                                         WPARAM aWParam, LPARAM aLParam)
{
  NS_ABORT_IF_FALSE(aMessage == WM_MOUSEWHEEL || aMessage == WM_MOUSEHWHEEL,
    "EventInfo must be initialized with WM_MOUSEWHEEL or WM_MOUSEHWHEEL");

  MouseScrollHandler::GetInstance()->mSystemSettings.Init();

  mIsVertical = (aMessage == WM_MOUSEWHEEL);
  mIsPage = MouseScrollHandler::sInstance->
              mSystemSettings.IsPageScroll(mIsVertical);
  mDelta = (short)HIWORD(aWParam);
  mWnd = aWindow->GetWindowHandle();
  mTimeStamp = TimeStamp::Now();
}

bool
MouseScrollHandler::EventInfo::CanDispatchMouseScrollEvent() const
{
  if (!GetScrollAmount()) {
    // XXX I think that we should dispatch mouse wheel events even if the
    // operation will not scroll because the wheel operation really happened
    // and web application may want to handle the event for non-scroll action.
    return false;
  }

  return (mDelta != 0);
}

PRInt32
MouseScrollHandler::EventInfo::GetScrollAmount() const
{
  if (mIsPage) {
    return 1;
  }
  return MouseScrollHandler::sInstance->
           mSystemSettings.GetScrollAmount(mIsVertical);
}

PRInt32
MouseScrollHandler::EventInfo::GetScrollFlags() const
{
  PRInt32 result = mIsPage ? nsMouseScrollEvent::kIsFullPage : 0;
  result |= mIsVertical ? nsMouseScrollEvent::kIsVertical :
                          nsMouseScrollEvent::kIsHorizontal;
  return result;
}

/******************************************************************************
 *
 * LastEventInfo
 *
 ******************************************************************************/

bool
MouseScrollHandler::LastEventInfo::CanContinueTransaction(
                                     const EventInfo& aNewEvent)
{
  PRInt32 timeout = MouseScrollHandler::sInstance->
                      mUserPrefs.GetMouseScrollTransactionTimeout();
  return !mWnd ||
           (mWnd == aNewEvent.GetWindowHandle() &&
            IsPositive() == aNewEvent.IsPositive() &&
            mIsVertical == aNewEvent.IsVertical() &&
            mIsPage == aNewEvent.IsPage() &&
            (timeout < 0 ||
             TimeStamp::Now() - mTimeStamp <=
               TimeDuration::FromMilliseconds(timeout)));
}

void
MouseScrollHandler::LastEventInfo::ResetTransaction()
{
  if (!mWnd) {
    return;
  }

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::LastEventInfo::ResetTransaction()"));

  mWnd = nsnull;
  mRemainingDeltaForScroll = 0;
  mRemainingDeltaForPixel = 0;
}

void
MouseScrollHandler::LastEventInfo::RecordEvent(const EventInfo& aEvent)
{
  mWnd = aEvent.GetWindowHandle();
  mDelta = aEvent.GetNativeDelta();
  mIsVertical = aEvent.IsVertical();
  mIsPage = aEvent.IsPage();
  mTimeStamp = TimeStamp::Now();
}

/* static */
PRInt32
MouseScrollHandler::LastEventInfo::RoundDelta(double aDelta)
{
  return (aDelta >= 0) ? (PRInt32)floor(aDelta) : (PRInt32)ceil(aDelta);
}

bool
MouseScrollHandler::LastEventInfo::InitMouseScrollEvent(
                                     nsWindow* aWindow,
                                     nsMouseScrollEvent& aMouseScrollEvent,
                                     const ScrollTargetInfo& aScrollTargetInfo,
                                     const ModifierKeyState& aModKeyState)
{
  NS_ABORT_IF_FALSE(aMouseScrollEvent.message == NS_MOUSE_SCROLL,
    "aMouseScrollEvent must be NS_MOUSE_SCROLL");

  // XXX Why don't we use lParam value? We should use lParam value because
  //     our internal message is always posted by original message handler.
  //     So, GetMessagePos() may return different cursor position.
  InitEvent(aWindow, aMouseScrollEvent);

  aModKeyState.InitInputEvent(aMouseScrollEvent);

  // If we dispatch pixel scroll event after the line scroll event,
  // we should set kHasPixels flag to the line scroll event.
  aMouseScrollEvent.scrollFlags =
    aScrollTargetInfo.dispatchPixelScrollEvent ?
      nsMouseScrollEvent::kHasPixels : 0;
  aMouseScrollEvent.scrollFlags |= GetScrollFlags();

  // Our positive delta value means to bottom or right.
  // But positive native delta value means to top or right.
  // Use orienter for computing our delta value with native delta value.
  PRInt32 orienter = mIsVertical ? -1 : 1;

  // NOTE: Don't use aScrollTargetInfo.actualScrollAmount for computing the
  //       delta value of line/page scroll event.  The value will be recomputed
  //       in ESM.
  PRInt32 nativeDelta = mDelta + mRemainingDeltaForScroll;
  if (IsPage()) {
    aMouseScrollEvent.delta = nativeDelta * orienter / WHEEL_DELTA;
    PRInt32 recomputedNativeDelta =
      aMouseScrollEvent.delta * orienter / WHEEL_DELTA;
    mRemainingDeltaForScroll = nativeDelta - recomputedNativeDelta;
  } else {
    double deltaPerUnit = (double)WHEEL_DELTA / GetScrollAmount();
    aMouseScrollEvent.delta = 
      RoundDelta((double)nativeDelta * orienter / deltaPerUnit);
    PRInt32 recomputedNativeDelta =
      (PRInt32)(aMouseScrollEvent.delta * orienter * deltaPerUnit);
    mRemainingDeltaForScroll = nativeDelta - recomputedNativeDelta;
  }

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::LastEventInfo::InitMouseScrollEvent: aWindow=%p, "
     "aMouseScrollEvent { refPoint: { x: %d, y: %d }, delta: %d, "
     "scrollFlags: 0x%04X, isShift: %s, isControl: %s, isAlt: %s, "
     "isMeta: %s }, mRemainingDeltaForScroll: %d",
     aWindow, aMouseScrollEvent.refPoint.x, aMouseScrollEvent.refPoint.y,
     aMouseScrollEvent.delta, aMouseScrollEvent.scrollFlags,
     GetBoolName(aMouseScrollEvent.IsShift()),
     GetBoolName(aMouseScrollEvent.IsControl()),
     GetBoolName(aMouseScrollEvent.IsAlt()),
     GetBoolName(aMouseScrollEvent.IsMeta()), mRemainingDeltaForScroll));

  return (aMouseScrollEvent.delta != 0);
}

bool
MouseScrollHandler::LastEventInfo::InitMousePixelScrollEvent(
                                     nsWindow* aWindow,
                                     nsMouseScrollEvent& aPixelScrollEvent,
                                     const ScrollTargetInfo& aScrollTargetInfo,
                                     const ModifierKeyState& aModKeyState)
{
  NS_ABORT_IF_FALSE(aPixelScrollEvent.message == NS_MOUSE_PIXEL_SCROLL,
    "aPixelScrollEvent must be NS_MOUSE_PIXEL_SCROLL");

  if (!aScrollTargetInfo.dispatchPixelScrollEvent) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::LastEventInfo::InitMousePixelScrollEvent: aWindow=%p, "
       "PixelScroll is disabled",
       aWindow, mRemainingDeltaForPixel));

    mRemainingDeltaForPixel = 0;
    return false;
  }

  // XXX Why don't we use lParam value? We should use lParam value because
  //     our internal message is always posted by original message handler.
  //     So, GetMessagePos() may return different cursor position.
  InitEvent(aWindow, aPixelScrollEvent);

  aModKeyState.InitInputEvent(aPixelScrollEvent);

  aPixelScrollEvent.scrollFlags = nsMouseScrollEvent::kAllowSmoothScroll;
  aPixelScrollEvent.scrollFlags |= mIsVertical ?
    nsMouseScrollEvent::kIsVertical : nsMouseScrollEvent::kIsHorizontal;
  if (aScrollTargetInfo.actualScrollAction ==
        nsQueryContentEvent::SCROLL_ACTION_PAGE) {
    aPixelScrollEvent.scrollFlags |= nsMouseScrollEvent::kIsFullPage;
  }

  // Our positive delta value means to bottom or right.
  // But positive native delta value means to top or right.
  // Use orienter for computing our delta value with native delta value.
  PRInt32 orienter = mIsVertical ? -1 : 1;
  // However, pixel scroll event won't be recomputed the scroll amout and
  // direction by ESM.  Therefore, we need to set the computed amout and
  // direction here.
  if (aScrollTargetInfo.reversePixelScrollDirection) {
    orienter *= -1;
  }

  PRInt32 nativeDelta = mDelta + mRemainingDeltaForPixel;
  double deltaPerPixel = (double)WHEEL_DELTA /
    aScrollTargetInfo.actualScrollAmount / aScrollTargetInfo.pixelsPerUnit;
  aPixelScrollEvent.delta =
    RoundDelta((double)nativeDelta * orienter / deltaPerPixel);
  PRInt32 recomputedNativeDelta =
    (PRInt32)(aPixelScrollEvent.delta * orienter * deltaPerPixel);
  mRemainingDeltaForPixel = nativeDelta - recomputedNativeDelta;

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::LastEventInfo::InitMousePixelScrollEvent: aWindow=%p, "
     "aPixelScrollEvent { refPoint: { x: %d, y: %d }, delta: %d, "
     "scrollFlags: 0x%04X, isShift: %s, isControl: %s, isAlt: %s, "
     "isMeta: %s }, mRemainingDeltaForScroll: %d",
     aWindow, aPixelScrollEvent.refPoint.x, aPixelScrollEvent.refPoint.y,
     aPixelScrollEvent.delta, aPixelScrollEvent.scrollFlags,
     GetBoolName(aPixelScrollEvent.IsShift()),
     GetBoolName(aPixelScrollEvent.IsControl()),
     GetBoolName(aPixelScrollEvent.IsAlt()),
     GetBoolName(aPixelScrollEvent.IsMeta()), mRemainingDeltaForPixel));

  return (aPixelScrollEvent.delta != 0);
}

/******************************************************************************
 *
 * SystemSettings
 *
 ******************************************************************************/

void
MouseScrollHandler::SystemSettings::Init()
{
  if (mInitialized) {
    return;
  }

  mInitialized = true;

  MouseScrollHandler::UserPrefs& userPrefs =
    MouseScrollHandler::sInstance->mUserPrefs;

  mScrollLines = userPrefs.GetOverriddenVerticalScrollAmout();
  if (mScrollLines >= 0) {
    // overridden by the pref.
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::SystemSettings::Init(): mScrollLines is overridden by "
       "the pref: %d",
       mScrollLines));
  } else if (!::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0,
                                     &mScrollLines, 0)) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::SystemSettings::Init(): ::SystemParametersInfo("
         "SPI_GETWHEELSCROLLLINES) failed"));
    mScrollLines = 3;
  }

  if (mScrollLines > WHEEL_DELTA) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::SystemSettings::Init(): the result of "
         "::SystemParametersInfo(SPI_GETWHEELSCROLLLINES) is too large: %d",
       mScrollLines));
    // sScrollLines usually equals 3 or 0 (for no scrolling)
    // However, if sScrollLines > WHEEL_DELTA, we assume that
    // the mouse driver wants a page scroll.  The docs state that
    // sScrollLines should explicitly equal WHEEL_PAGESCROLL, but
    // since some mouse drivers use an arbitrary large number instead,
    // we have to handle that as well.
    mScrollLines = WHEEL_PAGESCROLL;
  }

  mScrollChars = userPrefs.GetOverriddenHorizontalScrollAmout();
  if (mScrollChars >= 0) {
    // overridden by the pref.
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::SystemSettings::Init(): mScrollChars is overridden by "
       "the pref: %d",
       mScrollChars));
  } else if (!::SystemParametersInfo(SPI_GETWHEELSCROLLCHARS, 0,
                                     &mScrollChars, 0)) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::SystemSettings::Init(): ::SystemParametersInfo("
         "SPI_GETWHEELSCROLLCHARS) failed, %s",
       WinUtils::GetWindowsVersion() >= WinUtils::VISTA_VERSION ?
         "this is unexpected on Vista or later" :
         "but on XP or earlier, this is not a problem"));
    mScrollChars = 1;
  }

  if (mScrollChars > WHEEL_DELTA) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::SystemSettings::Init(): the result of "
         "::SystemParametersInfo(SPI_GETWHEELSCROLLCHARS) is too large: %d",
       mScrollChars));
    // See the comments for the case mScrollLines > WHEEL_DELTA.
    mScrollChars = WHEEL_PAGESCROLL;
  }

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::SystemSettings::Init(): initialized, "
       "mScrollLines=%d, mScrollChars=%d",
     mScrollLines, mScrollChars));
}

void
MouseScrollHandler::SystemSettings::MarkDirty()
{
  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScrollHandler::SystemSettings::MarkDirty(): "
       "Marking SystemSettings dirty"));
  mInitialized = false;
  // When system settings are changed, we should reset current transaction.
  MOZ_ASSERT(sInstance,
    "Must not be called at initializing MouseScrollHandler");
  MouseScrollHandler::sInstance->mLastEventInfo.ResetTransaction();
}

/******************************************************************************
 *
 * UserPrefs
 *
 ******************************************************************************/

MouseScrollHandler::UserPrefs::UserPrefs() :
  mInitialized(false)
{
  // We need to reset mouse wheel transaction when all of mousewheel related
  // prefs are changed.
  DebugOnly<nsresult> rv =
    Preferences::RegisterCallback(OnChange, "mousewheel.", this);
  MOZ_ASSERT(NS_SUCCEEDED(rv),
    "Failed to register callback for mousewheel.");
}

MouseScrollHandler::UserPrefs::~UserPrefs()
{
  DebugOnly<nsresult> rv =
    Preferences::UnregisterCallback(OnChange, "mousewheel.", this);
  MOZ_ASSERT(NS_SUCCEEDED(rv),
    "Failed to unregister callback for mousewheel.");
}

void
MouseScrollHandler::UserPrefs::Init()
{
  if (mInitialized) {
    return;
  }

  mInitialized = true;

  mPixelScrollingEnabled =
    Preferences::GetBool("mousewheel.enable_pixel_scrolling", true);
  mScrollMessageHandledAsWheelMessage =
    Preferences::GetBool("mousewheel.emulate_at_wm_scroll", false);
  mOverriddenVerticalScrollAmount =
    Preferences::GetInt("mousewheel.windows.vertical_amount_override", -1);
  mOverriddenHorizontalScrollAmount =
    Preferences::GetInt("mousewheel.windows.horizontal_amount_override", -1);
  mMouseScrollTransactionTimeout =
    Preferences::GetInt("mousewheel.windows.transaction.timeout",
                        DEFAULT_TIMEOUT_DURATION);

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::UserPrefs::Init(): initialized, "
       "mPixelScrollingEnabled=%s, mScrollMessageHandledAsWheelMessage=%s, "
       "mOverriddenVerticalScrollAmount=%d, "
       "mOverriddenHorizontalScrollAmount=%d, "
       "mMouseScrollTransactionTimeout=%d",
     GetBoolName(mPixelScrollingEnabled),
     GetBoolName(mScrollMessageHandledAsWheelMessage),
     mOverriddenVerticalScrollAmount, mOverriddenHorizontalScrollAmount,
     mMouseScrollTransactionTimeout));
}

void
MouseScrollHandler::UserPrefs::MarkDirty()
{
  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScrollHandler::UserPrefs::MarkDirty(): Marking UserPrefs dirty"));
  mInitialized = false;
  // Some prefs might override system settings, so, we should mark them dirty.
  MouseScrollHandler::sInstance->mSystemSettings.MarkDirty();
  // When user prefs for mousewheel are changed, we should reset current
  // transaction.
  MOZ_ASSERT(sInstance,
    "Must not be called at initializing MouseScrollHandler");
  MouseScrollHandler::sInstance->mLastEventInfo.ResetTransaction();
}

/******************************************************************************
 *
 * Device
 *
 ******************************************************************************/

/* static */
bool
MouseScrollHandler::Device::GetWorkaroundPref(const char* aPrefName,
                                              bool aValueIfAutomatic)
{
  if (!aPrefName) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::Device::GetWorkaroundPref(): Failed, aPrefName is NULL"));
    return aValueIfAutomatic;
  }

  PRInt32 lHackValue = 0;
  if (NS_FAILED(Preferences::GetInt(aPrefName, &lHackValue))) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::Device::GetWorkaroundPref(): Preferences::GetInt() failed,"
       " aPrefName=\"%s\", aValueIfAutomatic=%s",
       aPrefName, GetBoolName(aValueIfAutomatic)));
    return aValueIfAutomatic;
  }

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::Device::GetWorkaroundPref(): Succeeded, "
     "aPrefName=\"%s\", aValueIfAutomatic=%s, lHackValue=%d",
     aPrefName, GetBoolName(aValueIfAutomatic), lHackValue));

  switch (lHackValue) {
    case 0: // disabled
      return false;
    case 1: // enabled
      return true;
    default: // -1: autodetect
      return aValueIfAutomatic;
  }
}

/* static */
void
MouseScrollHandler::Device::Init()
{
  sFakeScrollableWindowNeeded =
    GetWorkaroundPref("ui.trackpoint_hack.enabled",
                      (TrackPoint::IsDriverInstalled() ||
                       UltraNav::IsObsoleteDriverInstalled()));

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::Device::Init(): sFakeScrollableWindowNeeded=%s",
     GetBoolName(sFakeScrollableWindowNeeded)));

  Elantech::Init();
}

/******************************************************************************
 *
 * Device::Elantech
 *
 ******************************************************************************/

/* static */
void
MouseScrollHandler::Device::Elantech::Init()
{
  PRInt32 version = GetDriverMajorVersion();
  bool needsHack =
    Device::GetWorkaroundPref("ui.elantech_gesture_hacks.enabled",
                              version != 0);
  sUseSwipeHack = needsHack && version <= 7;
  sUsePinchHack = needsHack && version <= 8;

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::Device::Elantech::Init(): version=%d, sUseSwipeHack=%s, "
     "sUsePinchHack=%s",
     version, GetBoolName(sUseSwipeHack), GetBoolName(sUsePinchHack)));
}

/* static */
PRInt32
MouseScrollHandler::Device::Elantech::GetDriverMajorVersion()
{
  PRUnichar buf[40];
  // The driver version is found in one of these two registry keys.
  bool foundKey =
    WinUtils::GetRegistryKey(HKEY_CURRENT_USER,
                             L"Software\\Elantech\\MainOption",
                             L"DriverVersion",
                             buf, sizeof buf);
  if (!foundKey) {
    foundKey =
      WinUtils::GetRegistryKey(HKEY_CURRENT_USER,
                               L"Software\\Elantech",
                               L"DriverVersion",
                               buf, sizeof buf);
  }

  if (!foundKey) {
    return 0;
  }

  // Assume that the major version number can be found just after a space
  // or at the start of the string.
  for (PRUnichar* p = buf; *p; p++) {
    if (*p >= L'0' && *p <= L'9' && (p == buf || *(p - 1) == L' ')) {
      return wcstol(p, NULL, 10);
    }
  }

  return 0;
}

/* static */
bool
MouseScrollHandler::Device::Elantech::IsHelperWindow(HWND aWnd)
{
  // The helper window cannot be distinguished based on its window class, so we
  // need to check if it is owned by the helper process, ETDCtrl.exe.

  const PRUnichar* filenameSuffix = L"\\etdctrl.exe";
  const int filenameSuffixLength = 12;

  DWORD pid;
  ::GetWindowThreadProcessId(aWnd, &pid);

  HANDLE hProcess = ::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
  if (!hProcess) {
    return false;
  }

  bool result = false;
  PRUnichar path[256] = {L'\0'};
  if (::GetProcessImageFileNameW(hProcess, path, ArrayLength(path))) {
    int pathLength = lstrlenW(path);
    if (pathLength >= filenameSuffixLength) {
      if (lstrcmpiW(path + pathLength - filenameSuffixLength,
                    filenameSuffix) == 0) {
        result = true;
      }
    }
  }
  ::CloseHandle(hProcess);

  return result;
}

/* static */
bool
MouseScrollHandler::Device::Elantech::HandleKeyMessage(nsWindow* aWindow,
                                                       UINT aMsg,
                                                       WPARAM aWParam)
{
  // The Elantech touchpad driver understands three-finger swipe left and
  // right gestures, and translates them into Page Up and Page Down key
  // events for most applications.  For Firefox 3.6, it instead sends
  // Alt+Left and Alt+Right to trigger browser back/forward actions.  As
  // with the Thinkpad Driver hack in nsWindow::Create, the change in
  // HWND structure makes Firefox not trigger the driver's heuristics
  // any longer.
  //
  // The Elantech driver actually sends these messages for a three-finger
  // swipe right:
  //
  //   WM_KEYDOWN virtual_key = 0xCC or 0xFF (depending on driver version)
  //   WM_KEYDOWN virtual_key = VK_NEXT
  //   WM_KEYUP   virtual_key = VK_NEXT
  //   WM_KEYUP   virtual_key = 0xCC or 0xFF
  //
  // so we use the 0xCC or 0xFF key modifier to detect whether the Page Down
  // is due to the gesture rather than a regular Page Down keypress.  We then
  // pretend that we should dispatch "Go Forward" command.  Similarly
  // for VK_PRIOR and "Go Back" command.
  if (sUseSwipeHack &&
      (aWParam == VK_NEXT || aWParam == VK_PRIOR) &&
      (IS_VK_DOWN(0xFF) || IS_VK_DOWN(0xCC))) {
    if (aMsg == WM_KEYDOWN) {
      PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
        ("MouseScroll::Device::Elantech::HandleKeyMessage(): Dispatching "
         "%s command event",
         aWParam == VK_NEXT ? "Forward" : "Back"));

      nsCommandEvent commandEvent(true, nsGkAtoms::onAppCommand,
        (aWParam == VK_NEXT) ? nsGkAtoms::Forward : nsGkAtoms::Back, aWindow);
      InitEvent(aWindow, commandEvent);
      MouseScrollHandler::DispatchEvent(aWindow, commandEvent);
    }
#ifdef PR_LOGGING
    else {
      PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
        ("MouseScroll::Device::Elantech::HandleKeyMessage(): Consumed"));
    }
#endif
    return true; // consume the message (doesn't need to dispatch key events)
  }

  // Version 8 of the Elantech touchpad driver sends these messages for
  // zoom gestures:
  //
  //   WM_KEYDOWN    virtual_key = 0xCC        time = 10
  //   WM_KEYDOWN    virtual_key = VK_CONTROL  time = 10
  //   WM_MOUSEWHEEL                           time = ::GetTickCount()
  //   WM_KEYUP      virtual_key = VK_CONTROL  time = 10
  //   WM_KEYUP      virtual_key = 0xCC        time = 10
  //
  // The result of this is that we process all of the WM_KEYDOWN/WM_KEYUP
  // messages first because their timestamps make them appear to have
  // been sent before the WM_MOUSEWHEEL message.  To work around this,
  // we store the current time when we process the WM_KEYUP message and
  // assume that any WM_MOUSEWHEEL message with a timestamp before that
  // time is one that should be processed as if the Control key was down.
  if (sUsePinchHack && aMsg == WM_KEYUP &&
      aWParam == VK_CONTROL && ::GetMessageTime() == 10) {
    // We look only at the bottom 31 bits of the system tick count since
    // GetMessageTime returns a LONG, which is signed, so we want values
    // that are more easily comparable.
    sZoomUntil = ::GetTickCount() & 0x7FFFFFFF;

    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::Device::Elantech::HandleKeyMessage(): sZoomUntil=%d",
       sZoomUntil));
  }

  return false;
}

/* static */
void
MouseScrollHandler::Device::Elantech::UpdateZoomUntil()
{
  if (!sZoomUntil) {
    return;
  }

  // For the Elantech Touchpad Zoom Gesture Hack, we should check that the
  // system time (32-bit milliseconds) hasn't wrapped around.  Otherwise we
  // might get into the situation where wheel events for the next 50 days of
  // system uptime are assumed to be Ctrl+Wheel events.  (It is unlikely that
  // we would get into that state, because the system would already need to be
  // up for 50 days and the Control key message would need to be processed just
  // before the system time overflow and the wheel message just after.)
  //
  // We also take the chance to reset sZoomUntil if we simply have passed that
  // time.
  LONG msgTime = ::GetMessageTime();
  if ((sZoomUntil >= 0x3fffffffu && DWORD(msgTime) < 0x40000000u) ||
      (sZoomUntil < DWORD(msgTime))) {
    sZoomUntil = 0;

    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::Device::Elantech::UpdateZoomUntil(): "
       "sZoomUntil was reset"));
  }
}

/* static */
bool
MouseScrollHandler::Device::Elantech::IsZooming()
{
  // Assume the Control key is down if the Elantech touchpad has sent the
  // mis-ordered WM_KEYDOWN/WM_MOUSEWHEEL messages.  (See the comment in
  // OnKeyUp.)
  return (sZoomUntil && static_cast<DWORD>(::GetMessageTime()) < sZoomUntil);
}

/******************************************************************************
 *
 * Device::TrackPoint
 *
 ******************************************************************************/

/* static */
bool
MouseScrollHandler::Device::TrackPoint::IsDriverInstalled()
{
  if (WinUtils::HasRegistryKey(HKEY_CURRENT_USER,
                               L"Software\\Lenovo\\TrackPoint")) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::Device::TrackPoint::IsDriverInstalled(): "
       "Lenovo's TrackPoint driver is found"));
    return true;
  }

  if (WinUtils::HasRegistryKey(HKEY_CURRENT_USER,
                               L"Software\\Alps\\Apoint\\TrackPoint")) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::Device::TrackPoint::IsDriverInstalled(): "
       "Alps's TrackPoint driver is found"));
  }

  return false;
}

/******************************************************************************
 *
 * Device::UltraNav
 *
 ******************************************************************************/

/* static */
bool
MouseScrollHandler::Device::UltraNav::IsObsoleteDriverInstalled()
{
  if (WinUtils::HasRegistryKey(HKEY_CURRENT_USER,
                               L"Software\\Lenovo\\UltraNav")) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::Device::UltraNav::IsObsoleteDriverInstalled(): "
       "Lenovo's UltraNav driver is found"));
    return true;
  }

  bool installed = false;
  if (WinUtils::HasRegistryKey(HKEY_CURRENT_USER,
        L"Software\\Synaptics\\SynTPEnh\\UltraNavUSB")) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::Device::UltraNav::IsObsoleteDriverInstalled(): "
       "Synaptics's UltraNav (USB) driver is found"));
    installed = true;
  } else if (WinUtils::HasRegistryKey(HKEY_CURRENT_USER,
               L"Software\\Synaptics\\SynTPEnh\\UltraNavPS2")) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::Device::UltraNav::IsObsoleteDriverInstalled(): "
       "Synaptics's UltraNav (PS/2) driver is found"));
    installed = true;
  }

  if (!installed) {
    return false;
  }

  PRUnichar buf[40];
  bool foundKey =
    WinUtils::GetRegistryKey(HKEY_LOCAL_MACHINE,
                             L"Software\\Synaptics\\SynTP\\Install",
                             L"DriverVersion",
                             buf, sizeof buf);
  if (!foundKey) {
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::Device::UltraNav::IsObsoleteDriverInstalled(): "
       "Failed to get UltraNav driver version"));
    return false;
  }

  int majorVersion = wcstol(buf, NULL, 10);
  int minorVersion = 0;
  PRUnichar* p = wcschr(buf, L'.');
  if (p) {
    minorVersion = wcstol(p + 1, NULL, 10);
  }
  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScroll::Device::UltraNav::IsObsoleteDriverInstalled(): "
     "found driver version = %d.%d",
     majorVersion, minorVersion));
  return majorVersion < 15 || majorVersion == 15 && minorVersion == 0;
}

/******************************************************************************
 *
 * Device::SetPoint
 *
 ******************************************************************************/

/* static */
bool
MouseScrollHandler::Device::SetPoint::IsGetMessagePosResponseValid(
                                        UINT aMessage,
                                        WPARAM aWParam,
                                        LPARAM aLParam)
{
  if (aMessage != WM_MOUSEHWHEEL) {
    return false;
  }

  POINTS pts = MouseScrollHandler::GetCurrentMessagePos();
  LPARAM messagePos = MAKELPARAM(pts.x, pts.y);

  // XXX We should check whether SetPoint is installed or not by registry.

  // SetPoint, Logitech (Logicool) mouse driver, (confirmed with 4.82.11 and
  // MX-1100) always sets 0 to the lParam of WM_MOUSEHWHEEL.  The driver SENDs
  // one message at first time, this time, ::GetMessagePos() works fine.
  // Then, we will return 0 (0 means we process it) to the message. Then, the
  // driver will POST the same messages continuously during the wheel tilted.
  // But ::GetMessagePos() API always returns (0, 0) for them, even if the
  // actual mouse cursor isn't 0,0.  Therefore, we cannot trust the result of
  // ::GetMessagePos API if the sender is SetPoint.
  if (!sMightBeUsing && !aLParam && aLParam != messagePos &&
      ::InSendMessage()) {
    sMightBeUsing = true;
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::Device::SetPoint::IsGetMessagePosResponseValid(): "
       "Might using SetPoint"));
  } else if (sMightBeUsing && aLParam != 0 && ::InSendMessage()) {
    // The user has changed the mouse from Logitech's to another one (e.g.,
    // the user has changed to the touchpad of the notebook.
    sMightBeUsing = false;
    PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
      ("MouseScroll::Device::SetPoint::IsGetMessagePosResponseValid(): "
       "Might stop using SetPoint"));
  }
  return (sMightBeUsing && !aLParam && !messagePos);
}

/******************************************************************************
 *
 * SynthesizingEvent
 *
 ******************************************************************************/

/* static */
bool
MouseScrollHandler::SynthesizingEvent::IsSynthesizing()
{
  return MouseScrollHandler::sInstance &&
    MouseScrollHandler::sInstance->mSynthesizingEvent &&
    MouseScrollHandler::sInstance->mSynthesizingEvent->mStatus !=
      NOT_SYNTHESIZING;
}

nsresult
MouseScrollHandler::SynthesizingEvent::Synthesize(const POINTS& aCursorPoint,
                                                  HWND aWnd,
                                                  UINT aMessage,
                                                  WPARAM aWParam,
                                                  LPARAM aLParam,
                                                  const BYTE (&aKeyStates)[256])
{
  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScrollHandler::SynthesizingEvent::Synthesize(): aCursorPoint: { "
     "x: %d, y: %d }, aWnd=0x%X, aMessage=0x%04X, aWParam=0x%08X, "
     "aLParam=0x%08X, IsSynthesized()=%s, mStatus=%s",
     aCursorPoint.x, aCursorPoint.y, aWnd, aMessage, aWParam, aLParam,
     GetBoolName(IsSynthesizing()), GetStatusName()));

  if (IsSynthesizing()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  ::GetKeyboardState(mOriginalKeyState);

  // Note that we cannot use ::SetCursorPos() because it works asynchronously.
  // We should SEND the message for reducing the possibility of receiving
  // unexpected message which were not sent from here.
  mCursorPoint = aCursorPoint;

  mWnd = aWnd;
  mMessage = aMessage;
  mWParam = aWParam;
  mLParam = aLParam;

  memcpy(mKeyState, aKeyStates, sizeof(mKeyState));
  ::SetKeyboardState(mKeyState);

  mStatus = SENDING_MESSAGE;

  // Don't assume that aWnd is always managed by nsWindow.  It might be
  // a plugin window.
  ::SendMessage(aWnd, aMessage, aWParam, aLParam);

  return NS_OK;
}

void
MouseScrollHandler::SynthesizingEvent::NativeMessageReceived(nsWindow* aWindow,
                                                             UINT aMessage,
                                                             WPARAM aWParam,
                                                             LPARAM aLParam)
{
  if (mStatus == SENDING_MESSAGE && mMessage == aMessage &&
      mWParam == aWParam && mLParam == aLParam) {
    mStatus = NATIVE_MESSAGE_RECEIVED;
    if (aWindow && aWindow->GetWindowHandle() == mWnd) {
      return;
    }
    // If the target window is not ours and received window is our plugin
    // window, it comes from child window of the plugin.
    if (aWindow && aWindow->GetWindowType() == eWindowType_plugin &&
        !WinUtils::GetNSWindowPtr(mWnd)) {
      return;
    }
    // Otherwise, the message may not be sent by us.
  }

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScrollHandler::SynthesizingEvent::NativeMessageReceived(): "
     "aWindow=%p, aWindow->GetWindowHandle()=0x%X, mWnd=0x%X, "
     "aMessage=0x%04X, aWParam=0x%08X, aLParam=0x%08X, mStatus=%s",
     aWindow, aWindow ? aWindow->GetWindowHandle() : 0, mWnd,
     aMessage, aWParam, aLParam, GetStatusName()));

  // We failed to receive our sent message, we failed to do the job.
  Finish();

  return;
}

void
MouseScrollHandler::SynthesizingEvent::NotifyNativeMessageHandlingFinished()
{
  if (!IsSynthesizing()) {
    return;
  }

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScrollHandler::SynthesizingEvent::"
     "NotifyNativeMessageHandlingFinished(): IsWaitingInternalMessage=%s",
     GetBoolName(MouseScrollHandler::IsWaitingInternalMessage())));

  if (MouseScrollHandler::IsWaitingInternalMessage()) {
    mStatus = INTERNAL_MESSAGE_POSTED;
    return;
  }

  // If the native message handler didn't post our internal message,
  // we our job is finished.
  // TODO: When we post the message to plugin window, there is remaning job.
  Finish();
}

void
MouseScrollHandler::SynthesizingEvent::NotifyInternalMessageHandlingFinished()
{
  if (!IsSynthesizing()) {
    return;
  }

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScrollHandler::SynthesizingEvent::"
     "NotifyInternalMessageHandlingFinished()"));

  Finish();
}

void
MouseScrollHandler::SynthesizingEvent::Finish()
{
  if (!IsSynthesizing()) {
    return;
  }

  PR_LOG(gMouseScrollLog, PR_LOG_ALWAYS,
    ("MouseScrollHandler::SynthesizingEvent::Finish()"));

  // Restore the original key state.
  ::SetKeyboardState(mOriginalKeyState);

  mStatus = NOT_SYNTHESIZING;
}

} // namespace widget
} // namespace mozilla
