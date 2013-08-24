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
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Andrei Volkov <av@netscape.com>
 *   Brian Stell <bstell@netscape.com>
 *   Peter Lubczynski <peterl@netscape.com>
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

#include "windows.h"
#include "windowsx.h"

// XXXbz windowsx.h defines GetFirstChild, GetNextSibling,
// GetPrevSibling are macros, apparently... Eeevil.  We have functions
// called that on some classes, so undef them.
#undef GetFirstChild
#undef GetNextSibling
#undef GetPrevSibling

#include "nsDebug.h"

#include "nsGUIEvent.h"

#include "nsPluginSafety.h"
#include "nsPluginNativeWindow.h"
#include "nsThreadUtils.h"
#include "nsAutoPtr.h"
#include "nsTWeakRef.h"

#define NP_POPUP_API_VERSION 16

#define nsMajorVersion(v)       (((PRInt32)(v) >> 16) & 0xffff)
#define nsMinorVersion(v)       ((PRInt32)(v) & 0xffff)
#define versionOK(suppliedV, requiredV)                   \
  (nsMajorVersion(suppliedV) == nsMajorVersion(requiredV) \
   && nsMinorVersion(suppliedV) >= nsMinorVersion(requiredV))


#define NS_PLUGIN_WINDOW_PROPERTY_ASSOCIATION TEXT("MozillaPluginWindowPropertyAssociation")
#define NS_PLUGIN_CUSTOM_MSG_ID TEXT("MozFlashUserRelay")
#define WM_USER_FLASH WM_USER+1
static UINT sWM_FLASHBOUNCEMSG = 0;

typedef nsTWeakRef<class nsPluginNativeWindowWin> PluginWindowWeakRef;

/**
 *  PLEvent handling code
 */
class PluginWindowEvent : public nsRunnable {
public:
  PluginWindowEvent();
  void Init(const PluginWindowWeakRef &ref, HWND hWnd, UINT msg, WPARAM wParam,
            LPARAM lParam);
  void Clear();
  HWND   GetWnd()    { return mWnd; };
  UINT   GetMsg()    { return mMsg; };
  WPARAM GetWParam() { return mWParam; };
  LPARAM GetLParam() { return mLParam; };
  PRBool InUse()     { return (mWnd!=NULL); };

  NS_DECL_NSIRUNNABLE

protected:
  PluginWindowWeakRef mPluginWindowRef;
  HWND   mWnd;
  UINT   mMsg;
  WPARAM mWParam;
  LPARAM mLParam;
};

PluginWindowEvent::PluginWindowEvent()
{
  Clear();
}

void PluginWindowEvent::Clear()
{
  mWnd    = NULL;
  mMsg    = 0;
  mWParam = 0;
  mLParam = 0;
}

void PluginWindowEvent::Init(const PluginWindowWeakRef &ref, HWND aWnd,
                             UINT aMsg, WPARAM aWParam, LPARAM aLParam)
{
  NS_ASSERTION(aWnd != NULL, "invalid plugin event value");
  NS_ASSERTION(mWnd == NULL, "event already in use");
  mPluginWindowRef = ref;
  mWnd    = aWnd;
  mMsg    = aMsg;
  mWParam = aWParam;
  mLParam = aLParam;
}

/**
 *  nsPluginNativeWindow Windows specific class declaration
 */

typedef enum {
  nsPluginType_Unknown = 0,
  nsPluginType_Flash,
  nsPluginType_Real,
  nsPluginType_Other
} nsPluginType;

class nsPluginNativeWindowWin : public nsPluginNativeWindow {
public: 
  nsPluginNativeWindowWin();
  virtual ~nsPluginNativeWindowWin();

  virtual nsresult CallSetWindow(nsCOMPtr<nsIPluginInstance> &aPluginInstance);

private:
#ifndef WINCE
  nsresult SubclassAndAssociateWindow();
  nsresult UndoSubclassAndAssociateWindow();
#endif

public:
  // locals
  WNDPROC GetPrevWindowProc();
  WNDPROC GetWindowProc();
  PluginWindowEvent * GetPluginWindowEvent(HWND aWnd,
                                           UINT aMsg,
                                           WPARAM aWParam,
                                           LPARAM aLParam);

private:
  WNDPROC mPrevWinProc;
  WNDPROC mPluginWinProc;
  PluginWindowWeakRef mWeakRef;
  nsRefPtr<PluginWindowEvent> mCachedPluginWindowEvent;

public:
  nsPluginType mPluginType;
};

static PRBool sInMessageDispatch = PR_FALSE;
static UINT sLastMsg = 0;

static PRBool ProcessFlashMessageDelayed(nsPluginNativeWindowWin * aWin, nsIPluginInstance * aInst,
                                         HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  NS_ENSURE_TRUE(aWin, NS_ERROR_NULL_POINTER);
  NS_ENSURE_TRUE(aInst, NS_ERROR_NULL_POINTER);

  if (msg == sWM_FLASHBOUNCEMSG) {
    // See PluginWindowEvent::Run() below.
    NS_ASSERTION((sWM_FLASHBOUNCEMSG != 0), "RegisterWindowMessage failed in flash plugin WM_USER message handling!");
    NS_TRY_SAFE_CALL_VOID(::CallWindowProc((WNDPROC)aWin->GetWindowProc(), hWnd, WM_USER_FLASH, wParam, lParam),
                                           nsnull, aInst);
    return TRUE;
  }

  if (msg != WM_USER_FLASH)
    return PR_FALSE; // no need to delay

  // do stuff
  nsCOMPtr<nsIRunnable> pwe = aWin->GetPluginWindowEvent(hWnd, msg, wParam, lParam);
  if (pwe) {
    NS_DispatchToCurrentThread(pwe);
    return PR_TRUE;  
  }
  return PR_FALSE;
}

class nsDelayedPopupsEnabledEvent : public nsRunnable
{
public:
  nsDelayedPopupsEnabledEvent(nsIPluginInstance *inst)
    : mInst(inst)
  {}

  NS_DECL_NSIRUNNABLE

private:
  nsCOMPtr<nsIPluginInstance> mInst;
};

NS_IMETHODIMP nsDelayedPopupsEnabledEvent::Run()
{
  mInst->PushPopupsEnabledState(PR_FALSE);
  return NS_OK;	
}

/**
 *   New plugin window procedure
 */
static LRESULT CALLBACK PluginWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  nsPluginNativeWindowWin * win = (nsPluginNativeWindowWin *)::GetProp(hWnd, NS_PLUGIN_WINDOW_PROPERTY_ASSOCIATION);
  if (!win)
    return TRUE;

  // The DispatchEvent(NS_PLUGIN_ACTIVATE) below can trigger a reentrant focus
  // event which might destroy us.  Hold a strong ref on the plugin instance
  // to prevent that, bug 374229.
  nsCOMPtr<nsIPluginInstance> inst;
  win->GetPluginInstance(inst);

  // check plugin mime type and cache whether it is Flash or not
  // Flash will need special treatment later
  if (win->mPluginType == nsPluginType_Unknown) {
    if (inst) {
      const char* mimetype = nsnull;
      inst->GetMIMEType(&mimetype);
      if (mimetype) { 
        if (!strcmp(mimetype, "application/x-shockwave-flash"))
          win->mPluginType = nsPluginType_Flash;
        else if (!strcmp(mimetype, "audio/x-pn-realaudio-plugin"))
          win->mPluginType = nsPluginType_Real;
        else
          win->mPluginType = nsPluginType_Other;
      }
    }
  }

  // Real may go into a state where it recursivly dispatches the same event
  // when subclassed. If this is Real, lets examine the event and drop it
  // on the floor if we get into this recursive situation. See bug 192914.
  if (win->mPluginType == nsPluginType_Real) {
    
    if (sInMessageDispatch && (msg == sLastMsg)) {
#ifdef DEBUG
      printf("Dropping event %d for Real on the floor\n", msg);
#endif
      return PR_TRUE;  // prevents event dispatch
    } else {
      sLastMsg = msg;  // no need to prevent dispatch
    }
  }

  PRBool enablePopups = PR_FALSE;

  // Activate/deactivate mouse capture on the plugin widget
  // here, before we pass the Windows event to the plugin
  // because its possible our widget won't get paired events
  // (see bug 131007) and we'll look frozen. Note that this
  // is also done in ChildWindow::DispatchMouseEvent.
  switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN: {
      nsCOMPtr<nsIWidget> widget;
      win->GetPluginWidget(getter_AddRefs(widget));
      if (widget)
        widget->CaptureMouse(PR_TRUE);
      break;
    }
    case WM_LBUTTONUP:
      enablePopups = PR_TRUE;

      // fall through
    case WM_MBUTTONUP:
    case WM_RBUTTONUP: {
      nsCOMPtr<nsIWidget> widget;
      win->GetPluginWidget(getter_AddRefs(widget));
      if (widget)
        widget->CaptureMouse(PR_FALSE);
      break;
    }
    case WM_KEYDOWN:
      // Ignore repeating keydown messages...
      if ((lParam & 0x40000000) != 0) {
        break;
      }

      // fall through
    case WM_KEYUP:
      enablePopups = PR_TRUE;

      break;

#ifndef WINCE
    case WM_MOUSEACTIVATE: {
      // If a child window of this plug-in is already focused,
      // don't focus the parent to avoid focus dance. We'll 
      // receive a follow up WM_SETFOCUS which will notify
      // the appropriate window anyway.
      HWND focusedWnd = ::GetFocus();
      if (!::IsChild((HWND)win->window, focusedWnd)) {
        // Notify the dom / focus manager the plugin has focus when one of
        // it's child windows receives it. OOPP specific - this code is
        // critical in notifying the dom of focus changes when the plugin
        // window in the child process receives focus via a mouse click.
        // WM_MOUSEACTIVATE is sent by nsWindow via a custom window event
        // sent from PluginInstanceParent in response to focus events sent
        // from the child. (bug 540052) Note, this gui event could also be
        // sent directly from widget.
        nsCOMPtr<nsIWidget> widget;
        win->GetPluginWidget(getter_AddRefs(widget));
        if (widget) {
          nsGUIEvent event(PR_TRUE, NS_PLUGIN_ACTIVATE, widget);
          nsEventStatus status;
          widget->DispatchEvent(&event, status);
        }
      }
    }
    break;

    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
      // RealPlayer can crash, don't process the message for those, see bug 328675
      if (win->mPluginType == nsPluginType_Real && msg == sLastMsg)
        return TRUE;
      // Make sure setfocus and killfocus get through
      // even if they are eaten by the plugin
      WNDPROC prevWndProc = win->GetPrevWindowProc();
      if (prevWndProc)
        ::CallWindowProc(prevWndProc, hWnd, msg, wParam, lParam);
      break;
    }
#endif
  }

  // Macromedia Flash plugin may flood the message queue with some special messages
  // (WM_USER+1) causing 100% CPU consumption and GUI freeze, see mozilla bug 132759;
  // we can prevent this from happening by delaying the processing such messages;
  if (win->mPluginType == nsPluginType_Flash) {
    if (ProcessFlashMessageDelayed(win, inst, hWnd, msg, wParam, lParam))
      return TRUE;
  }

  if (enablePopups && inst) {
    PRUint16 apiVersion;
    if (NS_SUCCEEDED(inst->GetPluginAPIVersion(&apiVersion)) &&
        !versionOK(apiVersion, NP_POPUP_API_VERSION)) {
      inst->PushPopupsEnabledState(PR_TRUE);
    }
  }

  sInMessageDispatch = PR_TRUE;

  LRESULT res = TRUE;
  NS_TRY_SAFE_CALL_RETURN(res, 
                          ::CallWindowProc((WNDPROC)win->GetWindowProc(), hWnd, msg, wParam, lParam),
                          nsnull, inst);

  sInMessageDispatch = PR_FALSE;

  if (inst) {
    // Popups are enabled (were enabled before the call to
    // CallWindowProc()). Some plugins (at least the flash player)
    // post messages from their key handlers etc that delay the actual
    // processing, so we need to delay the disabling of popups so that
    // popups remain enabled when the flash player ends up processing
    // the actual key handlers. We do this by posting an event that
    // does the disabling, this way our disabling will happen after
    // the handlers in the plugin are done.

    // Note that it's not fatal if any of this fails (which won't
    // happen unless we're out of memory anyways) since the plugin
    // code will pop any popup state pushed by this plugin on
    // destruction.

    nsCOMPtr<nsIRunnable> event = new nsDelayedPopupsEnabledEvent(inst);
    if (event)
      NS_DispatchToCurrentThread(event);
  }

  return res;
}

/**
 *   nsPluginNativeWindowWin implementation
 */
nsPluginNativeWindowWin::nsPluginNativeWindowWin() : nsPluginNativeWindow()
{
  // initialize the struct fields
  window = nsnull; 
  x = 0; 
  y = 0; 
  width = 0; 
  height = 0; 

  mPrevWinProc = NULL;
  mPluginWinProc = NULL;
  mPluginType = nsPluginType_Unknown;
  
  if (sWM_FLASHBOUNCEMSG == 0)
    sWM_FLASHBOUNCEMSG = ::RegisterWindowMessage(NS_PLUGIN_CUSTOM_MSG_ID);

}

nsPluginNativeWindowWin::~nsPluginNativeWindowWin()
{
  // clear weak reference to self to prevent any pending events from
  // dereferencing this.
  mWeakRef.forget();
}

WNDPROC nsPluginNativeWindowWin::GetPrevWindowProc()
{
  return mPrevWinProc;
}

WNDPROC nsPluginNativeWindowWin::GetWindowProc()
{
  return mPluginWinProc;
}

NS_IMETHODIMP PluginWindowEvent::Run()
{
  nsPluginNativeWindowWin *win = mPluginWindowRef.get();
  if (!win)
    return NS_OK;

  HWND hWnd = GetWnd();
  if (!hWnd)
    return NS_OK;

  nsCOMPtr<nsIPluginInstance> inst;
  win->GetPluginInstance(inst);

  if (GetMsg() == WM_USER_FLASH) {
    // XXX Unwind issues related to runnable event callback depth for this
    // event and destruction of the plugin. (Bug 493601)
    ::PostMessage(hWnd, sWM_FLASHBOUNCEMSG, GetWParam(), GetLParam());
  }
  else {
    // Currently not used, but added so that processing events here
    // is more generic.
    NS_TRY_SAFE_CALL_VOID(::CallWindowProc(win->GetWindowProc(), 
                          hWnd, 
                          GetMsg(), 
                          GetWParam(), 
                          GetLParam()),
                          nsnull, inst);
  }

  Clear();
  return NS_OK;
}

PluginWindowEvent * 
nsPluginNativeWindowWin::GetPluginWindowEvent(HWND aWnd, UINT aMsg, WPARAM aWParam, LPARAM aLParam)
{
  if (!mWeakRef) {
    mWeakRef = this;
    if (!mWeakRef)
      return nsnull;
  }

  PluginWindowEvent *event;

  // We have the ability to alloc if needed in case in the future some plugin
  // should post multiple PostMessages. However, this could lead to many
  // alloc's per second which could become a performance issue. See bug 169247.
  if (!mCachedPluginWindowEvent) 
  {
    event = new PluginWindowEvent();
    if (!event) return nsnull;
    mCachedPluginWindowEvent = event;
  }
  else if (mCachedPluginWindowEvent->InUse())
  {
    event = new PluginWindowEvent();
    if (!event) return nsnull;
  }
  else
  {
    event = mCachedPluginWindowEvent;
  }

  event->Init(mWeakRef, aWnd, aMsg, aWParam, aLParam);
  return event;
}

nsresult nsPluginNativeWindowWin::CallSetWindow(nsCOMPtr<nsIPluginInstance> &aPluginInstance)
{
  // check the incoming instance, null indicates that window is going away and we are
  // not interested in subclassing business any more, undo and don't subclass

  // WINCE does not subclass windows.  See bug 300011 for the details.
#ifndef WINCE
  if (!aPluginInstance) {
    UndoSubclassAndAssociateWindow();
    mPrevWinProc = NULL;
  }

  // We need WndProc before plug-ins do subclass in nsPluginNativeWindow::CallSetWindow.
  if (aPluginInstance) {
    WNDPROC currentWndProc = (WNDPROC)::GetWindowLongPtr((HWND)window, GWLP_WNDPROC);
    if (currentWndProc != PluginWndProc)
      mPrevWinProc = currentWndProc;
  }
#endif

  nsPluginNativeWindow::CallSetWindow(aPluginInstance);

#ifndef WINCE
  if (aPluginInstance)
    SubclassAndAssociateWindow();
#endif

  return NS_OK;
}

#ifndef WINCE

nsresult nsPluginNativeWindowWin::SubclassAndAssociateWindow()
{
  if (type != NPWindowTypeWindow)
    return NS_ERROR_FAILURE;

  HWND hWnd = (HWND)window;
  if (!hWnd)
    return NS_ERROR_FAILURE;

  // check if we need to re-subclass
  WNDPROC currentWndProc = (WNDPROC)::GetWindowLongPtr(hWnd, GWLP_WNDPROC);
  if (PluginWndProc == currentWndProc)
    return NS_OK;

  LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
#ifdef MOZ_IPC
  // Out of process plugins must not have the WS_CLIPCHILDREN style set on their
  // parent windows or else synchronous paints (via UpdateWindow() and others)
  // will cause deadlocks.
  if (::GetPropW(hWnd, L"PluginInstanceParentProperty"))
    style &= ~WS_CLIPCHILDREN;
  else
    style |= WS_CLIPCHILDREN;
#else
  style |= WS_CLIPCHILDREN;
#endif
  SetWindowLongPtr(hWnd, GWL_STYLE, style);

  mPluginWinProc = SubclassWindow(hWnd, (LONG_PTR)PluginWndProc);
  if (!mPluginWinProc)
    return NS_ERROR_FAILURE;

  nsPluginNativeWindowWin * win = (nsPluginNativeWindowWin *)::GetProp(hWnd, NS_PLUGIN_WINDOW_PROPERTY_ASSOCIATION);
  NS_ASSERTION(!win || (win == this), "plugin window already has property and this is not us");
  
  if (!::SetProp(hWnd, NS_PLUGIN_WINDOW_PROPERTY_ASSOCIATION, (HANDLE)this))
    return NS_ERROR_FAILURE;

  return NS_OK;
}

nsresult nsPluginNativeWindowWin::UndoSubclassAndAssociateWindow()
{
  // release plugin instance
  SetPluginInstance(nsnull);

  // remove window property
  HWND hWnd = (HWND)window;
  if (IsWindow(hWnd))
    ::RemoveProp(hWnd, NS_PLUGIN_WINDOW_PROPERTY_ASSOCIATION);

  // restore the original win proc
  // but only do this if this were us last time
  if (mPluginWinProc) {
    WNDPROC currentWndProc = (WNDPROC)::GetWindowLongPtr(hWnd, GWLP_WNDPROC);
    if (currentWndProc == PluginWndProc)
      SubclassWindow(hWnd, (LONG_PTR)mPluginWinProc);

    LONG_PTR style = GetWindowLongPtr(hWnd, GWL_STYLE);
    style &= ~WS_CLIPCHILDREN;
    SetWindowLongPtr(hWnd, GWL_STYLE, style);
  }

  return NS_OK;
}
#endif // WINCE

nsresult PLUG_NewPluginNativeWindow(nsPluginNativeWindow ** aPluginNativeWindow)
{
  NS_ENSURE_ARG_POINTER(aPluginNativeWindow);

  *aPluginNativeWindow = new nsPluginNativeWindowWin();

  return *aPluginNativeWindow ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

nsresult PLUG_DeletePluginNativeWindow(nsPluginNativeWindow * aPluginNativeWindow)
{
  NS_ENSURE_ARG_POINTER(aPluginNativeWindow);
  nsPluginNativeWindowWin *p = (nsPluginNativeWindowWin *)aPluginNativeWindow;
  delete p;
  return NS_OK;
}
